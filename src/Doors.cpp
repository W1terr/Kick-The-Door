#include "Doors.h"

#include "DoorModel.h"
#include "Fight.h"
#include "Forms.h"
#include "Lang.h"
#include "MathUtil.h"
#include "Settings.h"

namespace Doors
{
	namespace
	{
		using M::Dot;
		using M::Length;
		using M::Normalized;
		using RE::NiPoint3;

		constexpr float kBehindDepth = 120.0f;  // how far behind the panel an actor still gets hit
		constexpr float kBehindSide = 45.0f;    // and how far past its edges
		constexpr float kKnockCooldown = 4.0f;  // the same actor isn't knocked over again this soon
		constexpr float kPanelHalfThickness = 10.0f;  // a door's half thickness when its plane comes from the hinges

		struct Sounds
		{
			RE::BGSSoundDescriptorForm* swing{ nullptr };   // WPNSwingUnarmed
			RE::BGSSoundDescriptorForm* impact{ nullptr };  // KTD_DoorImpactSD (KickTheDoor.esp)
			RE::BGSSoundDescriptorForm* rattle{ nullptr };     // DRSLockedSD
			RE::BGSSoundDescriptorForm* lockBreak{ nullptr };  // KTD_LockBreakSD (KickTheDoor.esp)
			RE::BGSSoundDescriptorForm* snap{ nullptr };       // UILockpickingPickBreak, UILockpickingUnlock: without the esp
			RE::BGSSoundDescriptorForm* unlock{ nullptr };
		} sounds;

		std::unordered_set<RE::FormID> excluded;
		std::unordered_set<RE::FormID> included;

		std::mutex                             progressLock;  // the save callbacks run on another thread
		std::unordered_map<RE::FormID, int>    kicks;

		// the door shaking after a kick that didn't break it
		struct Wobble
		{
			RE::NiPointer<RE::NiAVObject> root;
			NiPoint3                      original;  // local translation
			NiPoint3                      push;      // world direction
			float                         time{ 0.0f };
		};
		std::vector<Wobble> wobbles;
		constexpr float     kWobbleTime = 0.35f;

		// an actor behind a door, knocked over when the panel reaches them
		struct Knock
		{
			RE::ActorHandle actor;
			NiPoint3        source;
			float           delay{ 0.0f };
		};
		std::vector<Knock>                                  knocks;
		std::unordered_map<RE::FormID, float>               knockedAt;  // seconds since load
		float                                               clock{ 0.0f };

		void Play(RE::BGSSoundDescriptorForm* a_sound, const NiPoint3& a_at)
		{
			auto* const audio = RE::BSAudioManager::GetSingleton();
			RE::BSSoundHandle handle;
			if (a_sound && audio && audio->GetSoundHandle(handle, a_sound)) {
				handle.SetPosition(a_at);
				handle.Play();
			}
		}

		NiPoint3 Horizontal(const NiPoint3& a_v)
		{
			return Normalized(NiPoint3{ a_v.x, a_v.y, 0.0f }, NiPoint3{ 0.0f, 1.0f, 0.0f });
		}

		bool IsQuestDoor(RE::TESObjectREFR* a_ref, bool a_teleport)
		{
			if (const auto* aliases = a_ref->extraList.GetByType<RE::ExtraAliasInstanceArray>(); aliases && !aliases->aliases.empty()) {
				return true;
			}
			// A locked door scripts point at is made persistent by the Creation Kit; plain doors aren't. Load doors are
			// always persistent, so for them only the aliases tell.
			return !a_teleport && a_ref->IsPersistent();
		}

		RE::NiControllerManager* FindManager(RE::NiAVObject* a_object, int a_depth)
		{
			if (!a_object) {
				return nullptr;
			}
			for (auto* controller = a_object->GetControllers(); controller; controller = controller->next.get()) {
				if (auto* manager = netimmerse_cast<RE::NiControllerManager*>(controller)) {
					return manager;
				}
			}
			if (auto* node = a_depth > 0 ? a_object->AsNode() : nullptr) {
				for (auto& child : node->GetChildren()) {
					if (auto* manager = FindManager(child.get(), a_depth - 1)) {
						return manager;
					}
				}
			}
			return nullptr;
		}

		RE::NiControllerSequence* OpenSequence(RE::NiControllerManager* a_manager)
		{
			if (!a_manager) {
				return nullptr;
			}
			if (auto* open = a_manager->GetSequenceByName("Open")) {
				return open;
			}
			for (auto& sequence : a_manager->sequenceArray) {
				if (sequence && sequence->name.c_str() && _stricmp(sequence->name.c_str(), "open") == 0) {
					return sequence.get();
				}
			}
			return nullptr;
		}

		// ---- doors opening away from the hit ----------------------------------------------------------------------
		// A door's Open animation turns its panel one way only. While a forced door is tracked here, every pose its
		// animation controllers write to the panel is mirrored about the hinge (the turn from the closed pose is
		// inverted) if the panel would otherwise swing towards where the force came from. The close animation is
		// mirrored the same way, so the door also closes from that side; tracking ends once it is closed again.

		struct SwingNode
		{
			RE::NiPointer<RE::NiAVObject> node;
			RE::NiMatrix3                 closedRotate;
			NiPoint3                      closedTranslate;
			RE::NiMatrix3                 writtenRotate;
			NiPoint3                      writtenTranslate;
			bool                          wrote{ false };
			bool                          panel{ false };  // swings on its own vertical hinge (a door or gate wing)
			RE::NiMatrix3                 carryRotate;     // this frame's correction of a panel (parent space):
			NiPoint3                      carryTranslate;  // mirrored = carry * natural; small parts on it get it too
		};

		struct Swing
		{
			RE::ObjectRefHandle           door;
			RE::NiPointer<RE::NiAVObject> root;
			RE::NiObjectNET*              owner{ nullptr };  // where the door's controllers sit
			std::vector<SwingNode>        nodes;
			NiPoint3                      from;   // world point the force came from
			NiPoint3                      probe;  // world point on the closed panel
			int                           mirror{ -1 };  // -1 not decided yet, 0 swings away already, 1 mirrored
			float                         age{ 0.0f };
			std::string                   name;
			std::array<int, 3>            calls{};     // controller updates seen, per hooked controller type
			int                           mirrored{ 0 };  // poses mirrored
			float                         maxAngle{ 0.0f };  // degrees the animation turned the panel at most
			bool                          reported{ false };
			int                           panel{ -1 };        // the node that swings on the hinge (index into nodes)
			// the faster Open animation (see StopPacing)
			RE::NiPointer<RE::NiControllerSequence> open;
			float                         normalFrequency{ 1.0f };
			bool                          pacing{ false };
			float                         restoreAt{ 0.0f };  // age at which the normal speed comes back
		};

		std::mutex         swingLock;
		std::vector<Swing> swings;
		std::atomic<int>   swingCount{ 0 };

		constexpr float kDecideAngle = 1.0f * M::kDegrees;  // the panel turned this far: its direction is clear
		constexpr float kHingeUp = 0.7f;                     // a hinge axis at least this vertical (world z)

		NiPoint3 Axis(const RE::NiMatrix3& a_m)
		{
			return Normalized(NiPoint3{ a_m.entry[2][1] - a_m.entry[1][2], a_m.entry[0][2] - a_m.entry[2][0], a_m.entry[1][0] - a_m.entry[0][1] });
		}

		// the door's wings: the parts its model turns about an axis that is vertical where the door stands (a handle or
		// a knob turns about another axis; some models lie on their side in the file and the reference stands them up)
		struct Wing
		{
			RE::NiAVObject* node{ nullptr };
			float           turn{ 0.0f };   // degrees
			float           start{ 0.0f };  // seconds into the Open sequence before it swings
		};

		std::vector<Wing> FindWings(RE::NiAVObject* a_root, const DoorModel::Info& a_model)
		{
			std::vector<Wing> wings;
			for (const auto& part : a_model.parts) {
				auto* const node = a_root->GetObjectByName(RE::BSFixedString(part.name.c_str()));
				if (!node) {
					continue;
				}
				const auto* space = node->parent ? node->parent : node;
				if (std::abs((space->world.rotate * part.axis).z) >= kHingeUp) {
					wings.push_back({ node, part.turn, part.start });
				}
			}
			return wings;
		}

		// A door's wings, found once per loaded 3D (searching the model's nodes on every check would be wasted: the
		// kick, the menu's status page and every shout wave ask). Holds the 3D, so the node pointers stay valid; a door
		// that reloaded gets a new 3D and is looked up again.
		struct Hinges
		{
			RE::NiPointer<RE::NiAVObject> root;
			std::vector<Wing>             wings;
			float                         start{ 0.0f };  // when the widest wing starts to swing (s into Open)
		};

		std::mutex                             hingeLock;  // the menu thread asks too
		std::unordered_map<RE::FormID, Hinges> hinges;
		constexpr std::size_t                  kMaxHinges = 32;

		Hinges HingesOf(RE::TESObjectREFR* a_ref)
		{
			auto* const root = a_ref ? a_ref->Get3D() : nullptr;
			if (!root) {
				return {};
			}
			std::scoped_lock guard(hingeLock);
			if (const auto it = hinges.find(a_ref->GetFormID()); it != hinges.end() && it->second.root.get() == root) {
				return it->second;
			}
			Hinges found;
			found.root.reset(root);
			if (const auto model = DoorModel::Get(a_ref)) {
				found.wings = FindWings(root, *model);
				if (const auto widest = std::ranges::max_element(found.wings, {}, &Wing::turn); widest != found.wings.end()) {
					found.start = widest->start;
				}
			}
			if (hinges.size() >= kMaxHinges) {
				hinges.clear();
			}
			hinges[a_ref->GetFormID()] = found;
			return found;
		}

		bool Fresh(const SwingNode& a_node)
		{
			const auto& local = a_node.node->local;
			return !(a_node.wrote && M::Same(local.rotate, a_node.writtenRotate) && local.translate == a_node.writtenTranslate);
		}

		void Write(Swing& a_swing, SwingNode& a_node, const RE::NiMatrix3& a_rotate, const NiPoint3& a_translate)
		{
			a_node.node->local.rotate = a_rotate;
			a_node.node->local.translate = a_translate;
			a_node.writtenRotate = a_rotate;
			a_node.writtenTranslate = a_translate;
			a_node.wrote = true;
			++a_swing.mirrored;
		}

		bool Below(const RE::NiAVObject* a_node, const RE::NiAVObject* a_ancestor)
		{
			for (auto* p = a_node ? a_node->parent : nullptr; p; p = p->parent) {
				if (p == a_ancestor) {
					return true;
				}
			}
			return false;
		}

		// Which way the door opens, from the panel: the node turning about a vertical axis that moves the closed
		// panel's center the most (handles, bolts and latches turn about other axes or barely move it). Seen from
		// above: towards the side the force came from -> mirror. Made once.
		void Decide(Swing& a_swing)
		{
			int      best = -1;
			NiPoint3 bestMove;
			float    bestAngle = 0.0f;
			NiPoint3 bestAxis;
			for (std::size_t i = 0; i < a_swing.nodes.size(); ++i) {
				const auto& n = a_swing.nodes[i];
				const auto* parent = n.node->parent;
				const auto  turn = n.node->local.rotate * n.closedRotate.Transpose();
				const float angle = M::Angle(turn);
				if (!parent || angle < kDecideAngle) {
					continue;
				}
				const auto axis = parent->world.rotate * Axis(turn);
				if (std::abs(axis.z) < kHingeUp) {
					continue;  // a handle turning, a bolt sliding: not the panel
				}
				const float scale = std::max(parent->world.scale, 0.01f);
				const auto  probe = parent->world.rotate.Transpose() * (a_swing.probe - parent->world.translate) * (1.0f / scale);
				const auto  moved = parent->world.rotate * (turn * (probe - n.closedTranslate) + n.node->local.translate - probe);
				if (best < 0 || M::Length(moved) > M::Length(bestMove)) {
					best = static_cast<int>(i);
					bestMove = moved;
					bestAngle = angle;
					bestAxis = axis;
				}
			}
			if (best < 0) {
				return;
			}
			const NiPoint3 move{ bestMove.x, bestMove.y, 0.0f };
			const NiPoint3 toward{ a_swing.from.x - a_swing.probe.x, a_swing.from.y - a_swing.probe.y, 0.0f };
			a_swing.panel = best;
			a_swing.mirror = Settings::Get().openAway && Dot(move, toward) > 0.0f ? 1 : 0;
			if (Settings::Get().debugLog) {
				logs::info("[swing] {}: panel '{}' turned {:.1f} deg about ({:.2f} {:.2f} {:.2f}); its center moves ({:.1f} {:.1f}), the hit came from ({:.0f} {:.0f}) -> {}",
					a_swing.name, a_swing.nodes[static_cast<std::size_t>(best)].node->name.c_str(), bestAngle / M::kDegrees, bestAxis.x, bestAxis.y, bestAxis.z,
					move.x, move.y, toward.x, toward.y, a_swing.mirror ? "swings towards the hit: MIRROR" : "swings away from the hit: leave");
			}
		}

		void Mirror(Swing& a_swing)
		{
			bool fresh = false;
			for (const auto& n : a_swing.nodes) {
				if (Fresh(n)) {
					fresh = true;
					a_swing.maxAngle = std::max(a_swing.maxAngle, M::Angle(n.node->local.rotate * n.closedRotate.Transpose()) / M::kDegrees);
				}
			}
			if (!fresh) {
				return;
			}
			if (a_swing.mirror < 0) {
				Decide(a_swing);
			}
			if (a_swing.mirror != 1) {
				return;
			}
			// without the model's list of swinging parts: the one the decision was made from
			if (std::ranges::none_of(a_swing.nodes, [](const SwingNode& a_n) { return a_n.panel; }) && a_swing.panel >= 0) {
				a_swing.nodes[static_cast<std::size_t>(a_swing.panel)].panel = true;
			}
			// every wing: the same turn the other way about its own hinge (rotation inverted; the hinge stays put, so an
			// origin off the hinge moves to the mirrored place in the plane of the turn)
			for (auto& n : a_swing.nodes) {
				if (!n.panel || !Fresh(n)) {
					continue;
				}
				const auto& local = n.node->local;
				const auto  natural = local;
				const auto  turn = local.rotate * n.closedRotate.Transpose();
				const auto  back = turn.Transpose();
				const auto  shift = local.translate - n.closedTranslate;
				NiPoint3    along = shift;
				if (M::Angle(turn) > 1e-4f) {
					const auto axis = Axis(turn);
					along = axis * Dot(axis, shift);
				}
				const auto rotate = back * n.closedRotate;
				const auto translate = n.closedTranslate + along - back * (shift - along);
				n.carryRotate = rotate * natural.rotate.Transpose();
				n.carryTranslate = translate - n.carryRotate * natural.translate;
				Write(a_swing, n, rotate, translate);
			}
			// small parts animated beside a wing (a handle, a bolt) go along with the nearest wing; parts under a wing
			// follow it anyway and keep their own animation
			for (auto& n : a_swing.nodes) {
				if (n.panel || !Fresh(n)) {
					continue;
				}
				const SwingNode* carrier = nullptr;
				float            nearest = 0.0f;
				bool             below = false;
				for (const auto& p : a_swing.nodes) {
					if (!p.panel) {
						continue;
					}
					if (Below(n.node.get(), p.node.get())) {
						below = true;
						break;
					}
					if (p.node->parent != n.node->parent) {
						continue;
					}
					const float distance = M::Length(p.closedTranslate - n.closedTranslate);
					if (!carrier || distance < nearest) {
						carrier = &p;
						nearest = distance;
					}
				}
				if (below || !carrier) {
					continue;
				}
				const auto natural = n.node->local;
				Write(a_swing, n, carrier->carryRotate * natural.rotate, carrier->carryRotate * natural.translate + carrier->carryTranslate);
			}
		}

		// ---- the opening's speed ------------------------------------------------------------------------------------
		// A forced door's Open animation plays Settings openSpeed times faster. Its normal speed comes back once the
		// animation would be over even at that speed (it stays at its last frame then, nothing moves).

		void StopPacing(Swing& a_swing)
		{
			if (a_swing.pacing && a_swing.open) {
				a_swing.open->frequency = a_swing.normalFrequency;
			}
			a_swing.pacing = false;
		}

		void TrackSwing(const Target& a_target, const NiPoint3& a_from)
		{
			auto* const root = a_target.ref->Get3D();
			auto* const manager = FindManager(root, 3);
			auto* const open = OpenSequence(manager);
			if (!open) {
				if (Settings::Get().debugLog) {
					logs::info("[swing] {}: {} - not tracked", Forms::Name(a_target.ref), manager ? "no Open sequence" : "no NiControllerManager in its 3D");
				}
				return;
			}
			Swing swing;
			swing.door = a_target.ref->GetHandle();
			swing.root.reset(root);
			swing.owner = manager->target;
			swing.from = a_from;
			swing.probe = a_target.shape.center;
			swing.name = std::format("{} [{:08X}]", Forms::Name(a_target.ref), a_target.ref->GetFormID());
			if (open->frequency > 0.0f) {
				const auto& s = Settings::Get();
				swing.open.reset(open);
				swing.normalFrequency = open->frequency;
				swing.pacing = true;
				swing.restoreAt = std::max(open->endKeyTime - open->beginKeyTime, 0.1f) / swing.normalFrequency + 0.75f;
				open->frequency = swing.normalFrequency * s.openSpeed;
			}
			const auto count = std::min<std::size_t>(open->arraySize, open->idTagArray.size());
			for (std::size_t i = 0; i < count; ++i) {
				const auto& name = open->idTagArray[i].avObjectName;
				auto* const node = name.empty() ? nullptr : root->GetObjectByName(name);
				if (node && std::ranges::none_of(swing.nodes, [&](const SwingNode& a_n) { return a_n.node.get() == node; })) {
					swing.nodes.push_back({ RE::NiPointer<RE::NiAVObject>(node), node->local.rotate, node->local.translate, {}, {}, false });
				}
			}
			const auto hinged = HingesOf(a_target.ref);
			for (auto& n : swing.nodes) {
				n.panel = std::ranges::any_of(hinged.wings, [&](const Wing& a_wing) { return a_wing.node == n.node.get(); });
			}
			if (Settings::Get().debugLog) {
				std::string names;
				for (const auto& n : swing.nodes) {
					names += std::format("'{}'{} ", n.node->name.c_str(), n.panel ? " (wing)" : "");
				}
				logs::info("[swing] {}: tracking {} node(s) {}(sequence '{}', {} id tags, controllers on '{}', from ({:.0f} {:.0f} {:.0f}), panel center ({:.0f} {:.0f} {:.0f}))",
					swing.name, swing.nodes.size(), names, open->name.c_str(), count, manager->target ? manager->target->name.c_str() : "?", a_from.x, a_from.y,
					a_from.z, swing.probe.x, swing.probe.y, swing.probe.z);
			}
			std::scoped_lock guard(swingLock);
			std::erase_if(swings, [&](const Swing& a_s) { return a_s.root.get() == root; });
			swings.push_back(std::move(swing));
			swingCount = static_cast<int>(swings.size());
		}

		// main thread: forget doors that unloaded or closed again
		void UpdateSwings(float a_delta)
		{
			if (swingCount == 0) {
				return;
			}
			std::scoped_lock guard(swingLock);
			auto report = [](const Swing& a_s, const char* a_why) {
				if (!Settings::Get().debugLog) {
					return;
				}
				logs::info("[swing] {}: done ({}) - controller updates seen: manager {}, multi-target {}, transform {}; turned up to {:.0f} deg; {} pose(s) mirrored; decision {}",
					a_s.name, a_why, a_s.calls[0], a_s.calls[1], a_s.calls[2], a_s.maxAngle, a_s.mirrored, a_s.mirror < 0 ? "never made" : (a_s.mirror ? "mirror" : "leave"));
			};
			std::erase_if(swings, [&](Swing& a_s) {
				a_s.age += a_delta;
				const auto door = a_s.door.get();
				if (!door || door->Get3D() != a_s.root.get()) {
					report(a_s, "unloaded");
					StopPacing(a_s);
					return true;
				}
				if (a_s.pacing && a_s.age >= a_s.restoreAt) {
					StopPacing(a_s);
				}
				if (a_s.age >= 4.0f && !a_s.reported) {
					a_s.reported = true;
					report(a_s, "4 s in");
				}
				if (a_s.age < 2.0f) {
					return false;
				}
				using State = RE::BGSOpenCloseForm::OPEN_STATE;
				if (RE::BGSOpenCloseForm::GetOpenState(door.get()) != State::kClosed) {
					return false;
				}
				const bool closed = std::ranges::all_of(a_s.nodes, [](const SwingNode& a_n) {
					return M::Angle(a_n.node->local.rotate * a_n.closedRotate.Transpose()) < 0.5f * M::kDegrees;
				});
				if (closed) {
					report(a_s, "closed again");
					StopPacing(a_s);
				}
				return closed;
			});
			swingCount = static_cast<int>(swings.size());
		}

		void Open(const Target& a_target, const NiPoint3& a_from)
		{
			auto* const door = a_target.ref;
			using State = RE::BGSOpenCloseForm::OPEN_STATE;
			const auto state = RE::BGSOpenCloseForm::GetOpenState(door);
			if (state == State::kOpen || state == State::kOpening) {
				return;
			}
			TrackSwing(a_target, a_from);  // closed pose taken and the burst set before the animation starts
			door->ActivateRef(RE::PlayerCharacter::GetSingleton(), 0, nullptr, 1, false);
		}

		void Stagger(RE::Actor* a_actor, const NiPoint3& a_source)
		{
			float direction = a_actor->GetHeadingAngle(a_source, false) / 360.0f;  // 0 = the hit came from the front
			if (direction < 0.0f) {
				direction += 1.0f;
			}
			a_actor->SetGraphVariableFloat("staggerDirection", direction);
			a_actor->SetGraphVariableFloat("staggerMagnitude", 1.0f);
			a_actor->NotifyAnimationGraph("staggerStart");
		}

		void KnockOver(RE::Actor* a_actor, const NiPoint3& a_source)
		{
			if (!a_actor || a_actor->IsDead() || !a_actor->Is3DLoaded()) {
				return;
			}
			auto* const race = a_actor->GetRace();
			auto* const state = a_actor->AsActorState();
			auto* const process = a_actor->GetActorRuntimeData().currentProcess;
			const bool  ragdoll = race && !race->data.flags.any(RE::RACE_DATA::Flag::kNoKnockdowns) && process && !a_actor->IsOnMount() &&
			                     !a_actor->IsInKillMove() && !a_actor->IsInRagdollState() && state &&
			                     state->GetKnockState() == RE::KNOCK_STATE_ENUM::kNormal && Settings::Get().knockdownForce > 0.0f;
			Fight::Damage(RE::PlayerCharacter::GetSingleton(), a_actor);  // the door hits them
			if (ragdoll) {
				process->KnockExplosion(a_actor, a_source, Settings::Get().knockdownForce);
			} else {
				Stagger(a_actor, a_source);
			}
			if (Settings::Get().debugLog) {
				logs::info("{} [{:08X}] behind the door: {}", Forms::Name(a_actor), a_actor->GetFormID(), ragdoll ? "knocked down" : "staggered");
			}
		}

		// actors standing right behind the panel, on the far side from a_from
		void FindBehind(const Target& a_target, const NiPoint3& a_from)
		{
			auto* const lists = RE::ProcessLists::GetSingleton();
			if (!lists || !Settings::Get().knockdown || a_target.teleport) {
				return;
			}
			const auto& shape = a_target.shape;
			const auto  away = NormalTowards(shape, a_from) * -1.0f;
			const auto  source = shape.center - away * 60.0f;
			const float delay = 0.1f;  // the bursting panel reaching them
			for (const auto& handle : lists->highActorHandles) {
				const auto actor = handle.get();
				if (!actor || actor->IsPlayerRef() || actor->IsDead() || actor->IsDisabled() || actor->IsChild()) {
					continue;
				}
				const auto pos = actor->GetPosition();
				const auto offset = pos - shape.center;
				const float behind = Dot(offset, away);
				const float side = std::abs(Dot(offset, shape.width));
				if (behind < 0.0f || behind > kBehindDepth + shape.halfThickness || side > shape.halfWidth + kBehindSide ||
					pos.z < shape.bottom - 60.0f || pos.z > shape.top) {
					continue;
				}
				if (const auto it = knockedAt.find(actor->GetFormID()); it != knockedAt.end() && clock - it->second < kKnockCooldown) {
					continue;
				}
				knockedAt[actor->GetFormID()] = clock;
				knocks.push_back({ actor->GetHandle(), source, delay });
			}
		}

		void StartWobble(RE::TESObjectREFR* a_door, const NiPoint3& a_push)
		{
			auto* const root = a_door->Get3D();
			if (!root || std::ranges::any_of(wobbles, [&](const Wobble& a_w) { return a_w.root.get() == root; })) {
				return;
			}
			wobbles.push_back({ RE::NiPointer<RE::NiAVObject>(root), root->local.translate, a_push, 0.0f });
		}

		void UpdateWobbles(float a_delta)
		{
			for (auto it = wobbles.begin(); it != wobbles.end();) {
				auto& w = *it;
				w.time += a_delta;
				const float t = std::min(w.time / kWobbleTime, 1.0f);
				// a hard push in, then a fading bounce
				const float amount = t >= 1.0f ? 0.0f : 2.5f * std::sin(t * 3.0f * M::kPi) * (1.0f - t);
				NiPoint3    push = w.push * amount;
				if (const auto* parent = w.root->parent) {
					push = parent->world.rotate.Transpose() * push * (1.0f / std::max(parent->world.scale, 0.01f));
				}
				w.root->local.translate = w.original + push;
				RE::NiUpdateData context{};
				w.root->Update(context);
				if (t >= 1.0f) {
					it = wobbles.erase(it);
				} else {
					++it;
				}
			}
		}

	}

	void Init()
	{
		sounds.swing = Forms::Vanilla<RE::BGSSoundDescriptorForm>(0x0C06D1);
		sounds.impact = RE::TESDataHandler::GetSingleton()->LookupForm<RE::BGSSoundDescriptorForm>(0x800, "KickTheDoor.esp");
		if (!sounds.impact) {
			logs::warn("The door impact sound is missing: is KickTheDoor.esp enabled in the load order?");
		}
		sounds.rattle = Forms::Vanilla<RE::BGSSoundDescriptorForm>(0x023F0E);
		sounds.lockBreak = RE::TESDataHandler::GetSingleton()->LookupForm<RE::BGSSoundDescriptorForm>(0x804, "KickTheDoor.esp");
		sounds.snap = Forms::Vanilla<RE::BGSSoundDescriptorForm>(0x0C1916);
		sounds.unlock = Forms::Vanilla<RE::BGSSoundDescriptorForm>(0x0C1917);
		excluded = Forms::ResolveAll(Settings::GetLists().excludeDoors, "Exclude");
		included = Forms::ResolveAll(Settings::GetLists().includeDoors, "Include");
		logs::info("Doors: {} excluded, {} included; sounds {}/{}/{}/{}/{}/{}", excluded.size(), included.size(), sounds.swing != nullptr,
			sounds.impact != nullptr, sounds.rattle != nullptr, sounds.lockBreak != nullptr, sounds.snap != nullptr, sounds.unlock != nullptr);
	}

	std::optional<Shape> GetShape(RE::TESObjectREFR* a_ref)
	{
		auto* const base = a_ref ? a_ref->GetBaseObject() : nullptr;
		auto* const root = a_ref ? a_ref->Get3D() : nullptr;
		if (!base || !root) {
			return std::nullopt;
		}
		const auto&    bound = base->boundData;
		const NiPoint3 min{ static_cast<float>(bound.boundMin.x), static_cast<float>(bound.boundMin.y), static_cast<float>(bound.boundMin.z) };
		const NiPoint3 max{ static_cast<float>(bound.boundMax.x), static_cast<float>(bound.boundMax.y), static_cast<float>(bound.boundMax.z) };
		const float    scale = root->world.scale > 0.0f ? root->world.scale : a_ref->GetScale();
		const auto&    rotate = root->world.rotate;
		const NiPoint3 half = (max - min) * (0.5f * scale);
		if (half.x <= 0.0f && half.y <= 0.0f && half.z <= 0.0f) {
			return std::nullopt;
		}

		Shape shape;
		shape.center = root->world.translate + rotate * ((min + max) * (0.5f * scale));
		const std::array<NiPoint3, 3> axes{ M::Column(rotate, 0), M::Column(rotate, 1), M::Column(rotate, 2) };
		const std::array<float, 3>    extent{ half.x, half.y, half.z };
		int                           up = 0;
		for (int i = 1; i < 3; ++i) {
			if (std::abs(axes[i].z) > std::abs(axes[up].z)) {
				up = i;
			}
		}
		const int a = (up + 1) % 3;
		const int b = (up + 2) % 3;
		const int along = extent[a] >= extent[b] ? a : b;
		const int across = along == a ? b : a;
		shape.upright = std::abs(axes[up].z) > 0.85f;
		shape.width = Horizontal(axes[along]);
		shape.normal = Horizontal(M::Cross(NiPoint3{ 0.0f, 0.0f, 1.0f }, shape.width));
		shape.halfWidth = extent[along];
		shape.halfThickness = extent[across];
		const float vertical = extent[up] * std::abs(axes[up].z) + extent[a] * std::abs(axes[a].z) + extent[b] * std::abs(axes[b].z);
		shape.bottom = shape.center.z - vertical;
		shape.top = shape.center.z + vertical;

		// The bounds can hold more than the door (the stockade gate's include its wall posts, far behind the wings):
		// the door's plane goes through its hinges, else standing close in front could count as standing behind it.
		if (const auto hinged = HingesOf(a_ref); !hinged.wings.empty()) {
			float offset = 0.0f;
			for (const auto& wing : hinged.wings) {
				offset += Dot(wing.node->world.translate - shape.center, shape.normal);
			}
			offset /= static_cast<float>(hinged.wings.size());
			shape.center += shape.normal * offset;
			shape.halfThickness = std::min(shape.halfThickness, kPanelHalfThickness);
		}
		return shape;
	}

	Verdict Check(RE::TESObjectREFR* a_ref, Target& a_out)
	{
		a_out = {};
		a_out.ref = a_ref;
		auto* const base = a_ref ? a_ref->GetBaseObject() : nullptr;
		if (!base || !base->Is(RE::FormType::Door) || a_ref->IsDisabled() || a_ref->IsMarkedForDeletion() || a_ref->IsDeleted()) {
			return Verdict::kNotADoor;
		}
		if (excluded.contains(a_ref->GetFormID()) || excluded.contains(base->GetFormID())) {
			return Verdict::kNotADoor;
		}
		if (!a_ref->Is3DLoaded()) {
			return Verdict::kNotLoaded;
		}
		const auto level = a_ref->GetLockLevel();
		a_out.teleport = a_ref->extraList.GetByType<RE::ExtraTeleport>() != nullptr;
		const auto shape = GetShape(a_ref);
		if (!shape) {
			return Verdict::kNotADoor;
		}
		a_out.shape = *shape;
		const bool forced = included.contains(a_ref->GetFormID()) || included.contains(base->GetFormID());
		if (!forced && (!shape->upright || shape->top - shape->bottom < Settings::Get().minDoorHeight)) {
			return Verdict::kNotADoor;
		}
		if (const float limit = Settings::Get().barredDelay; limit > 0.0f) {
			// door replacers (Glorious Doors of Skyrim...): a door that turns locks and slides bars for a long while
			// before it swings is barred, kicks and shouts don't open it
			if (HingesOf(a_ref).start > limit) {
				return Verdict::kBarred;
			}
		}
		if (level == RE::LOCK_LEVEL::kUnlocked) {
			// a door anyone may open: no quest or level rules, only open / blocked
			a_out.level = Settings::kUnlocked;
			using State = RE::BGSOpenCloseForm::OPEN_STATE;
			const auto state = RE::BGSOpenCloseForm::GetOpenState(a_ref);
			if (state == State::kOpen || state == State::kOpening) {
				return Verdict::kOpen;
			}
			if (a_ref->IsActivationBlocked()) {
				return Verdict::kBlocked;
			}
			return Settings::Get().unlocked.enabled ? Verdict::kOk : Verdict::kNotLocked;
		}
		const int value = static_cast<int>(level);
		if (level == RE::LOCK_LEVEL::kRequiresKey || value < 0 || value >= static_cast<int>(Settings::kLevels)) {
			return Verdict::kNeedsKey;
		}
		a_out.level = value;
		if (a_ref->IsActivationBlocked()) {
			return Verdict::kBlocked;
		}
		if (!Settings::Get().questDoors && IsQuestDoor(a_ref, a_out.teleport)) {
			return Verdict::kQuest;
		}
		if (!Settings::Get().locks[value].enabled) {
			return Verdict::kLevelOff;
		}
		return Verdict::kOk;
	}

	const char* LevelName(int a_level)
	{
		if (a_level == Settings::kUnlocked) {
			return "Unlocked";
		}
		return a_level >= 0 && a_level < static_cast<int>(Settings::kLevels) ? Settings::kLevelNames[a_level] : "?";
	}

	std::string Describe(Verdict a_verdict, int a_level)
	{
		switch (a_verdict) {
		case Verdict::kOk:
			return a_level == Settings::kUnlocked ? Lang::T("Unlocked door") : Lang::F("{} lock", Lang::T(LevelName(a_level)));
		case Verdict::kNotADoor:
			return Lang::T("This can't be forced open.");
		case Verdict::kNotLocked:
			return Lang::T("Not locked (kicking unlocked doors is off).");
		case Verdict::kOpen:
			return Lang::T("Already open.");
		case Verdict::kNeedsKey:
			return Lang::T("This lock requires a key.");
		case Verdict::kLevelOff:
			return Lang::F("Forcing {} locks is turned off.", Lang::T(LevelName(a_level)));
		case Verdict::kQuest:
			return Lang::T("This door is held by a quest. It won't give.");
		case Verdict::kBlocked:
			return Lang::T("This door can't be forced open.");
		case Verdict::kBarred:
			return Lang::T("This door is barred. It won't give.");
		case Verdict::kNotLoaded:
		default:
			return {};
		}
	}

	void Crime(RE::TESObjectREFR* a_ref)
	{
		auto* const player = RE::PlayerCharacter::GetSingleton();
		if (!Settings::Get().crime) {
			return;
		}
		auto* const owner = a_ref->GetOwner();
		if (owner && !a_ref->IsAnOwner(player, true, false)) {
			player->TrespassAlarm(a_ref, owner, RE::PackageNS::CRIME_TYPES::kTrespass);
		}
	}

	void Noise(RE::TESObjectREFR* a_ref, const NiPoint3& a_at)
	{
		auto* const player = RE::PlayerCharacter::GetSingleton();
		auto* const process = player->GetActorRuntimeData().currentProcess;
		if (Settings::Get().noise && process) {
			process->SetActorsDetectionEvent(player, a_at, RE::AIFormulas::GetSoundLevelValue(RE::SOUND_LEVEL::kVeryLoud), a_ref);
		}
	}

	float Distance(const Shape& a_shape, const NiPoint3& a_point)
	{
		NiPoint3 offset = a_point - a_shape.center;
		offset.z = 0.0f;
		const float along = Dot(offset, a_shape.width);
		const float across = std::max(std::abs(Dot(offset, a_shape.normal)) - a_shape.halfThickness, 0.0f);
		const float past = std::max(std::abs(along) - a_shape.halfWidth, 0.0f);
		return std::sqrt(across * across + past * past);
	}

	NiPoint3 NormalTowards(const Shape& a_shape, const NiPoint3& a_point)
	{
		return Dot(a_point - a_shape.center, a_shape.normal) >= 0.0f ? a_shape.normal : a_shape.normal * -1.0f;
	}

	void Break(const Target& a_target, const NiPoint3& a_from, Cause a_cause, bool a_enter)
	{
		auto* const door = a_target.ref;
		if (!door) {
			return;
		}
		const auto& shape = a_target.shape;
		const bool  locked = a_target.level != Settings::kUnlocked;
		if (locked) {
			auto* const lock = door->GetLock();
			if (!lock) {
				return;
			}
			// the same state change a picked lock makes: unlocked, saved with the reference, scripts hear about it
			lock->SetLocked(false);
			door->AddLockChange();
			RE::TESLockChangedEvent event{ RE::NiPointer<RE::TESObjectREFR>(door) };
			RE::ScriptEventSourceHolder::GetSingleton()->SendEvent(&event);
			PlayLockBreak(shape.center);
			Crime(door);
		}
		SetKicks(door->GetFormID(), 0);
		Play(sounds.impact, shape.center);
		Noise(door, shape.center);
		if (Settings::Get().debugLog) {
			logs::info("{} [{:08X}] ({}{}) {} by {}", Forms::Name(door), door->GetFormID(), LevelName(a_target.level), a_target.teleport ? ", load door" : "",
				locked ? "broken" : "thrown open", a_cause == Cause::kKick ? "a kick" : a_cause == Cause::kShout ? "Unrelenting Force" : "a beast");
		}

		if (a_target.teleport) {
			if (a_enter) {
				Enter(door);
			}
			return;
		}
		FindBehind(a_target, a_from);
		Open(a_target, a_from);
	}

	void Hit(const Target& a_target, const NiPoint3& a_from, bool a_weakened)
	{
		const auto& shape = a_target.shape;
		Play(sounds.impact, shape.center);
		Play(sounds.rattle, shape.center);
		if (a_weakened) {
			Noise(a_target.ref, shape.center);
		}
		StartWobble(a_target.ref, NormalTowards(shape, a_from) * -1.0f);
	}

	void Enter(RE::TESObjectREFR* a_door)
	{
		if (a_door && !a_door->IsLocked()) {
			a_door->ActivateRef(RE::PlayerCharacter::GetSingleton(), 0, nullptr, 1, false);
		}
	}

	void PlaySwing(const NiPoint3& a_at)
	{
		Play(sounds.swing, a_at);
	}

	void PlayLockBreak(const NiPoint3& a_at)
	{
		if (sounds.lockBreak) {
			Play(sounds.lockBreak, a_at);
		} else {
			Play(sounds.snap, a_at);
			Play(sounds.unlock, a_at);
		}
	}

	void Notify(const std::string& a_text)
	{
		if (Settings::Get().messages && !a_text.empty()) {
			RE::SendHUDMessage::ShowHUDMessage(a_text.c_str(), nullptr, true);
		}
	}

	int Kicks(RE::FormID a_door)
	{
		std::scoped_lock guard(progressLock);
		const auto       it = kicks.find(a_door);
		return it != kicks.end() ? it->second : 0;
	}

	void SetKicks(RE::FormID a_door, int a_kicks)
	{
		std::scoped_lock guard(progressLock);
		if (a_kicks <= 0) {
			kicks.erase(a_door);
		} else {
			kicks[a_door] = a_kicks;
		}
	}

	void Update(float a_delta)
	{
		clock += a_delta;
		for (auto it = knocks.begin(); it != knocks.end();) {
			it->delay -= a_delta;
			if (it->delay <= 0.0f) {
				KnockOver(it->actor.get().get(), it->source);
				it = knocks.erase(it);
			} else {
				++it;
			}
		}
		if (!wobbles.empty()) {
			UpdateWobbles(a_delta);
		}
		UpdateSwings(a_delta);
		if (knockedAt.size() > 32) {
			std::erase_if(knockedAt, [](const auto& a_kv) { return clock - a_kv.second > kKnockCooldown; });
		}
	}

	void Reset()
	{
		for (auto& w : wobbles) {
			w.root->local.translate = w.original;
		}
		wobbles.clear();
		knocks.clear();
		knockedAt.clear();
		{
			std::scoped_lock hingeGuard(hingeLock);
			hinges.clear();
		}
		std::scoped_lock guard(swingLock);
		for (auto& swing : swings) {
			StopPacing(swing);
		}
		swings.clear();
		swingCount = 0;
	}

	void OnControllerUpdate(RE::NiObjectNET* a_target, int a_kind)
	{
		if (swingCount.load(std::memory_order_relaxed) == 0 || !a_target) {
			return;
		}
		std::scoped_lock guard(swingLock);
		for (auto& swing : swings) {
			if (swing.owner == a_target || std::ranges::any_of(swing.nodes, [&](const SwingNode& a_n) { return a_n.node.get() == a_target; })) {
				++swing.calls[static_cast<std::size_t>(std::clamp(a_kind, 0, 2))];
				Mirror(swing);
			}
		}
	}

	void Save(SKSE::SerializationInterface* a_intfc)
	{
		std::scoped_lock guard(progressLock);
		if (kicks.empty() || !a_intfc->OpenRecord('KICK', 1)) {
			return;
		}
		const auto count = static_cast<std::uint32_t>(kicks.size());
		a_intfc->WriteRecordData(count);
		for (const auto& [door, done] : kicks) {
			a_intfc->WriteRecordData(door);
			a_intfc->WriteRecordData(static_cast<std::int32_t>(done));
		}
	}

	void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t)
	{
		std::scoped_lock guard(progressLock);
		kicks.clear();
		std::uint32_t count = 0;
		if (a_version != 1 || !a_intfc->ReadRecordData(count)) {
			return;
		}
		for (std::uint32_t i = 0; i < count; ++i) {
			RE::FormID   door = 0;
			std::int32_t done = 0;
			if (!a_intfc->ReadRecordData(door) || !a_intfc->ReadRecordData(done)) {
				break;
			}
			if (a_intfc->ResolveFormID(door, door) && done > 0) {
				kicks[door] = done;
			}
		}
		logs::info("{} weakened door(s) and chest(s) in this save", kicks.size());
	}

	void Revert()
	{
		std::scoped_lock guard(progressLock);
		kicks.clear();
	}
}
