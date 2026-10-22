#include "Kick.h"

#include "Clip.h"
#include "Fight.h"
#include "Forms.h"
#include "Keys.h"
#include "Lang.h"
#include "MathUtil.h"
#include "Ray.h"
#include "Settings.h"

namespace Kick
{
	namespace
	{
		using RE::NiPoint3;

		// when things happen during a kick (seconds) and how the player moves; without an animation: these
		struct Timing
		{
			float swing{ 0.20f };      // the whoosh
			float impact{ 0.34f };     // the foot meets the door
			float last{ 0.80f };       // the animation's last frame
			float stepStart{ 0.04f };  // moved towards the door from here
			float stepEnd{ 0.24f };    // to here
			float turnEnd{ 0.22f };    // facing the door by now
			float standOff{ 55.0f };   // the step stops this far from the panel
			float blendIn{ 0.08f };
			float blendOut{ 0.15f };   // after the last frame
			float endStep{ 0.0f };     // moved forward while blending out
			float maxStep{ 50.0f };    // the step towards the target goes this far at most
			float lunge{ 0.0f };       // with nothing to aim at: moved this far forward
			float end() const { return last + blendOut; }  // standing again
		};
		constexpr float kMaxBack = 25.0f;   // back, when standing closer than the kick needs
		constexpr float kReachSlack = 10.0f;  // the foot reaches a body this much past where a door would be

		// what the kick goes for
		enum class Mode
		{
			kDoor,  // a door under the crosshair (forced, or only the thud if it can't be)
			kFree   // straight ahead, wherever the player faces: whoever the foot reaches at the impact gets it
		};

		struct State
		{
			bool               active{ false };
			Mode               mode{ Mode::kFree };
			Doors::Verdict     doorVerdict{ Doors::Verdict::kOk };
			bool               sprinting{ false };
			int                animation{ -1 };  // the kick animation playing (none: the kick happens without one)
			bool               keepArms{ true }; // with a weapon drawn the arms keep the weapon stance
			Timing             timing;
			float              time{ 0.0f };
			RE::ObjectRefHandle door;
			bool               enoughStamina{ false };
			bool               swung{ false };
			bool               resolved{ false };
			bool               enterAfter{ false };
			bool               thirdPerson{ false };  // the camera was switched to 3rd person for the kick
			float              startHeading{ 0.0f };
			float              heading{ 0.0f };
			NiPoint3           face;                  // horizontal direction into the door
			float              step{ 0.0f };          // how far the player steps in (negative: back)
			float              stepped{ 0.0f };
			float              endStepped{ 0.0f };
			std::vector<RE::PlayerInputHandler*> blocked;
		} state;

		// key: the press that already started a kick doesn't start another while held
		bool fired{ false };
		bool requested{ false };

		bool ImprovedCamera()
		{
			static const bool loaded = REX::W32::GetModuleHandleW(L"ImprovedCameraSE.dll") != nullptr;
			return loaded;
		}

		float Stamina(RE::PlayerCharacter* a_player)
		{
			return a_player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kStamina);
		}

		bool GameReady()
		{
			auto* const ui = RE::UI::GetSingleton();
			return ui && !ui->GameIsPaused() && !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) && !ui->IsMenuOpen(RE::Console::MENU_NAME) &&
			       !ui->IsMenuOpen(RE::LockpickingMenu::MENU_NAME);
		}

		// standing on its own feet, free to move
		bool CanKick(RE::PlayerCharacter* a_player)
		{
			auto* const actorState = a_player->AsActorState();
			return a_player->Is3DLoaded() && !a_player->IsDead() && !a_player->IsOnMount() && !a_player->IsInKillMove() &&
			       !a_player->IsInRagdollState() && !a_player->IsInMidair() && actorState &&
			       actorState->GetSitSleepState() == RE::SIT_SLEEP_STATE::kNormal && actorState->GetKnockState() == RE::KNOCK_STATE_ENUM::kNormal &&
			       !actorState->IsSwimming() && !actorState->IsBleedingOut() && !actorState->IsFlying() &&
			       actorState->GetAttackState() == RE::ATTACK_STATE_ENUM::kNone;
		}

		bool Interrupted(RE::PlayerCharacter* a_player)
		{
			auto* const actorState = a_player->AsActorState();
			return a_player->IsDead() || a_player->IsInRagdollState() || a_player->IsOnMount() || !actorState ||
			       actorState->GetKnockState() != RE::KNOCK_STATE_ENUM::kNormal || actorState->IsSwimming() || actorState->IsBleedingOut();
		}

		RE::TESObjectREFR* CrosshairRef()
		{
			auto* const pick = RE::CrosshairPickData::GetSingleton();
			return pick ? pick->GetActiveTarget().get().get() : nullptr;
		}

		// no walking, jumping, attacking, shouting, activating or switching the camera during the kick
		void BlockControls(bool a_block)
		{
			auto* const controls = RE::PlayerControls::GetSingleton();
			if (!controls) {
				return;
			}
			if (a_block) {
				for (auto* handler : std::initializer_list<RE::PlayerInputHandler*>{ controls->movementHandler, controls->autoMoveHandler,
						 controls->sprintHandler, controls->jumpHandler, controls->attackBlockHandler, controls->shoutHandler,
						 controls->readyWeaponHandler, controls->activateHandler, controls->togglePOVHandler }) {
					if (handler && handler->IsInputEventHandlingEnabled()) {
						handler->SetInputEventHandlingEnabled(false);
						state.blocked.push_back(handler);
					}
				}
				controls->data.moveInputVec = {};
				controls->data.autoMove = false;
			} else {
				for (auto* handler : state.blocked) {
					handler->SetInputEventHandlingEnabled(true);
				}
				state.blocked.clear();
			}
		}

		void Finish()
		{
			Clip::Release();
			BlockControls(false);
			if (state.thirdPerson) {
				auto* const camera = RE::PlayerCamera::GetSingleton();
				if (camera && !camera->IsInFirstPerson()) {
					camera->ForceFirstPerson();
				}
			}
			const auto door = state.door.get();
			const bool enter = state.enterAfter;
			state = {};
			if (enter && door) {
				Doors::Enter(door.get());
			}
		}

		bool IsDoor(const RE::TESObjectREFR* a_ref)
		{
			const auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
			return base && base->Is(RE::FormType::Door);
		}

		// the door the kick is for: the one under the crosshair, else one straight ahead of the player within reach
		// (the camera can look past a door the player stands right in front of, in 3rd person especially)
		RE::TESObjectREFR* DoorInFront(RE::PlayerCharacter* a_player, float a_reach)
		{
			if (auto* const ref = CrosshairRef(); IsDoor(ref)) {
				return ref;
			}
			const float    heading = a_player->GetAngleZ();
			const NiPoint3 face{ std::sin(heading), std::cos(heading), 0.0f };
			const NiPoint3 eye = a_player->GetPosition() + NiPoint3{ 0.0f, 0.0f, 70.0f };
			const auto     ray = ::Ray::Cast(a_player, eye, eye + face * (a_reach + 20.0f));
			return ray.hit && IsDoor(ray.ref) ? ray.ref : nullptr;
		}

		// a step forward stops short of a wall or anyone else in the way
		float Clear(RE::PlayerCharacter* a_player, const NiPoint3& a_face, float a_step)
		{
			constexpr float kBody = 30.0f;
			if (a_step <= 0.0f) {
				return a_step;
			}
			const auto at = a_player->GetPosition();
			for (const float height : { 35.0f, 85.0f }) {
				const NiPoint3 from = at + NiPoint3{ 0.0f, 0.0f, height };
				if (const auto ray = ::Ray::Cast(a_player, from, from + a_face * (a_step + kBody)); ray.hit) {
					a_step = std::min(a_step, std::max(ray.distance - kBody, 0.0f));
				}
			}
			return a_step;
		}

		Clip::Situation SituationOf(RE::PlayerCharacter* a_player, bool a_lockedDoor, bool a_sprinting)
		{
			Clip::Situation situation;
			situation.female = a_player->GetActorBase() && a_player->GetActorBase()->GetSex() == RE::SEX::kFemale;
			situation.weaponDrawn = a_player->AsActorState()->IsWeaponDrawn();
			situation.sneaking = a_player->IsSneaking();
			situation.lockedDoor = a_lockedDoor;
			situation.sprinting = a_sprinting;
			situation.race = a_player->GetRace() ? a_player->GetRace()->GetFormID() : 0;
			return situation;
		}

		void Start(RE::PlayerCharacter* a_player)
		{
			const auto& s = Settings::Get();
			auto* const ref = DoorInFront(a_player, s.kickReach);
			const auto  position = a_player->GetPosition();
			const bool  sprinting = a_player->AsActorState()->IsSprinting();

			// a door in front, close enough: kicked as a door (forced, or only the thud if it can't be)
			Doors::Target door;
			auto          verdict = Doors::Verdict::kNotADoor;
			float         doorDistance = 0.0f;
			if (ref && s.kicking) {
				verdict = Doors::Check(ref, door);
				if (verdict == Doors::Verdict::kNotLocked || verdict == Doors::Verdict::kNotLoaded || verdict == Doors::Verdict::kOpen) {
					verdict = Doors::Verdict::kNotADoor;  // nothing to force: kicked like anything else
				} else if (verdict != Doors::Verdict::kNotADoor) {
					doorDistance = Doors::Distance(door.shape, position);
					if (doorDistance > s.kickReach) {
						if (!s.fighting) {
							Doors::Notify(Lang::T("Too far away to kick it."));
							return;
						}
						verdict = Doors::Verdict::kNotADoor;
					}
				}
			}
			const bool atDoor = verdict != Doors::Verdict::kNotADoor;
			if (!atDoor && !s.fighting) {
				return;
			}
			if (!atDoor && Stamina(a_player) < s.kickCost) {
				RE::HUDMenu::FlashMeter(RE::ActorValue::kStamina);
				Doors::Notify(Lang::F("Not enough stamina to kick ({:.0f} needed).", s.kickCost));
				return;
			}

			state = {};
			state.active = true;
			state.sprinting = sprinting;
			if (const auto clip = Clip::Pick(SituationOf(a_player, atDoor && door.level != Settings::kUnlocked, sprinting), s.animation)) {
				state.animation = clip->animation;
				state.keepArms = clip->keepArms;
				auto& t = state.timing;
				t.last = clip->duration;
				t.impact = std::clamp(clip->impact, 0.05f, t.last);
				t.swing = std::clamp(clip->swing, 0.0f, t.last);
				t.stepStart = std::clamp(clip->stepStart, 0.0f, t.impact);
				t.stepEnd = std::clamp(clip->stepEnd, t.stepStart + 0.01f, std::max(t.impact, t.stepStart + 0.01f));
				t.turnEnd = std::clamp(clip->turnEnd, 0.05f, t.impact);
				t.standOff = clip->standOff;
				t.blendIn = clip->blendIn;
				t.blendOut = clip->blendOut;
				t.endStep = clip->endStep;
				t.maxStep = clip->maxStep;
				t.lunge = clip->lunge;
			}
			const auto& t = state.timing;
			state.startHeading = a_player->GetAngleZ();
			if (atDoor) {
				state.mode = Mode::kDoor;
				state.door = ref->GetHandle();
				state.doorVerdict = verdict;
				state.enoughStamina = verdict == Doors::Verdict::kOk && Stamina(a_player) >= Settings::LockFor(door.level).staminaNeeded;
				state.face = Doors::NormalTowards(door.shape, position) * -1.0f;
				state.step = std::clamp(doorDistance - t.standOff, -kMaxBack, t.maxStep);
			} else {
				// no turning to anyone and no pulling towards them: the kick goes where the player faces (a sprint
				// carries them on a little, stopping short of whatever is in the way)
				a_player->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kStamina, s.kickCost);
				state.mode = Mode::kFree;
				state.face = NiPoint3{ std::sin(state.startHeading), std::cos(state.startHeading), 0.0f };
				state.step = Clear(a_player, state.face, sprinting ? t.lunge : 0.0f);
			}
			state.heading = state.mode == Mode::kDoor ? std::atan2(state.face.x, state.face.y) : state.startHeading;
			BlockControls(true);
			if (s.firstPerson == 1 && !ImprovedCamera()) {
				auto* const camera = RE::PlayerCamera::GetSingleton();
				if (camera && camera->IsInFirstPerson()) {
					camera->ForceThirdPerson();
					state.thirdPerson = true;
				}
			}
			if (s.debugLog) {
				if (atDoor) {
					logs::info("Kick at {} [{:08X}] ({} lock, {} kick(s) so far), {:.0f} units away, stamina {:.0f} / {:.0f}{}", Forms::Name(ref),
						ref->GetFormID(), Doors::LevelName(door.level), Doors::Kicks(ref->GetFormID()), doorDistance, Stamina(a_player),
						Settings::LockFor(door.level).staminaNeeded, verdict == Doors::Verdict::kOk ? "" : " - it won't give");
				} else {
					logs::info("Kick{}, step {:.0f}", sprinting ? " while sprinting" : "", state.step);
				}
			}
		}

		void ResolveDoor(RE::PlayerCharacter* a_player)
		{
			const auto& s = Settings::Get();
			const auto  ref = state.door.get();
			if (!ref) {
				return;
			}
			Doors::Target target;
			const auto    verdict = Doors::Check(ref.get(), target);
			const auto    from = a_player->GetPosition();
			if (verdict != Doors::Verdict::kOk) {
				if (verdict == state.doorVerdict) {
					// a door that won't give (needs a key, barred, held by a quest...): only the thud
					Doors::Hit(target, from, false);
					RE::ShakeCamera(0.15f, target.shape.center, 0.2f);
					Doors::Notify(Doors::Describe(verdict, target.level));
				}
				return;  // unlocked or changed meanwhile: the foot meets a door that gives anyway
			}
			const auto& lock = Settings::LockFor(target.level);
			if (!state.enoughStamina) {
				Doors::Hit(target, from, false);
				RE::ShakeCamera(0.15f, target.shape.center, 0.2f);
				RE::HUDMenu::FlashMeter(RE::ActorValue::kStamina);
				if (target.level == Settings::kUnlocked) {
					Doors::Notify(Lang::F("Not enough stamina. Kicking a door open needs {:.0f} (you have {:.0f}).", lock.staminaNeeded, Stamina(a_player)));
				} else {
					Doors::Notify(Lang::F("Not enough stamina. {} locks need {:.0f} (you have {:.0f}).", Lang::T(Doors::LevelName(target.level)),
						lock.staminaNeeded, Stamina(a_player)));
				}
				return;
			}
			a_player->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kStamina, lock.staminaCost);
			const int done = Doors::Kicks(ref->GetFormID()) + 1;
			const int needed = Settings::KicksNeeded(target.level);
			if (done < needed) {
				Doors::SetKicks(ref->GetFormID(), done);
				Doors::Hit(target, from, true);
				RE::ShakeCamera(0.25f, target.shape.center, 0.25f);
				const int left = needed - done;
				if (target.level == Settings::kUnlocked) {
					Doors::Notify(left == 1 ? std::string(Lang::T("The door shudders. One more kick should break it open.")) :
					                          Lang::F("The door shudders. {} more kicks should break it open.", left));
				} else {
					const auto* level = Lang::T(Doors::LevelName(target.level));
					Doors::Notify(left == 1 ? Lang::F("The door shudders. One more kick should break the {} lock.", level) :
					                          Lang::F("The door shudders. {} more kicks should break the {} lock.", left, level));
				}
				return;
			}
			state.enterAfter = target.teleport && s.enterLoadDoors;
			Doors::Break(target, from, Doors::Cause::kKick, false);
			RE::ShakeCamera(0.4f, target.shape.center, 0.3f);
		}

		void Resolve(RE::PlayerCharacter* a_player)
		{
			if (state.mode == Mode::kDoor) {
				ResolveDoor(a_player);
				return;
			}
			const float reach = state.timing.standOff + kReachSlack;  // as far as the toe gets
			if (auto* const actor = Fight::Reached(a_player, reach)) {
				Fight::Hit(a_player, actor, state.sprinting);
			}
		}

		void Step(RE::PlayerCharacter* a_player, float a_delta)
		{
			const float before = state.time;
			state.time += a_delta;
			const auto& timing = state.timing;
			if (Interrupted(a_player) && state.time < timing.impact) {
				Finish();  // knocked over, killed... before the foot landed
				return;
			}

			// turn to the door and step in (or back) before the kick
			if (before < timing.turnEnd) {
				const float t = M::Smooth(std::min(state.time / timing.turnEnd, 1.0f));
				a_player->SetHeading(M::LerpAngle(state.startHeading, state.heading, t));
			}
			if (state.step != 0.0f && before < timing.stepEnd) {
				const float t = M::Smooth((std::min(state.time, timing.stepEnd) - timing.stepStart) / (timing.stepEnd - timing.stepStart));
				const float target = state.step * t;
				if (std::abs(target - state.stepped) > 0.01f) {
					a_player->SetPosition(a_player->GetPosition() + state.face * (target - state.stepped), true);
					state.stepped = target;
				}
			}
			// the animation's stance comes back under the player while it blends out (its forward step stays put)
			if (timing.endStep != 0.0f && state.time > timing.last && timing.blendOut > 0.0f) {
				const float target = timing.endStep * M::Smooth((state.time - timing.last) / timing.blendOut);
				if (std::abs(target - state.endStepped) > 0.01f) {
					a_player->SetPosition(a_player->GetPosition() + state.face * (target - state.endStepped), true);
					state.endStepped = target;
				}
			}
			if (auto* const controls = RE::PlayerControls::GetSingleton()) {
				controls->data.moveInputVec = {};
			}

			if (!state.swung && state.time >= timing.swing) {
				state.swung = true;
				Doors::PlaySwing(a_player->GetPosition() + NiPoint3{ 0.0f, 0.0f, 60.0f });
			}
			if (!state.resolved && state.time >= timing.impact) {
				state.resolved = true;
				Resolve(a_player);
			}
			if (state.time >= timing.end()) {
				Finish();
			}
		}
	}

	void OnKey(const RE::ButtonEvent* a_event)
	{
		const auto& s = Settings::Get();
		const int   code = Keys::Code(a_event);
		if (code == Keys::kNone || (code != s.kickKey && code != s.kickGamepadKey)) {
			return;
		}
		if (a_event->IsUp()) {
			fired = false;
			return;
		}
		if (fired || !a_event->IsPressed() || a_event->HeldDuration() < s.holdTime) {
			return;
		}
		fired = true;
		requested = GameReady();  // a key pressed in a menu doesn't kick once the menu closes
	}

	void Update(float a_delta)
	{
		auto* const player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		const auto& s = Settings::Get();
		if (std::exchange(requested, false) && !state.active && (s.kicking || s.fighting) && GameReady() && CanKick(player)) {
			Start(player);
		}
		if (state.active) {
			Step(player, a_delta);
		}
	}

	void ApplyPose()
	{
		if (state.active && state.animation >= 0) {
			auto* const player = RE::PlayerCharacter::GetSingleton();
			const auto& t = state.timing;
			const float in = t.blendIn > 0.0f ? M::Smooth(state.time / t.blendIn) : 1.0f;
			const float out = t.blendOut > 0.0f ? M::Smooth((t.end() - state.time) / t.blendOut) : (state.time < t.last ? 1.0f : 0.0f);
			const float weight = std::min(in, out);
			Clip::Apply(player, state.animation, state.time, weight, state.keepArms && player->AsActorState()->IsWeaponDrawn());
		}
	}

	void Reset()
	{
		if (state.active) {
			Clip::Release();
			BlockControls(false);
		}
		state = {};
		requested = false;
		fired = false;
	}

	std::optional<Preview> Look()
	{
		auto* const player = RE::PlayerCharacter::GetSingleton();
		auto* const ref = CrosshairRef();
		if (!player || !ref || !ref->GetBaseObject() || !ref->GetBaseObject()->Is(RE::FormType::Door)) {
			return std::nullopt;
		}
		Preview preview;
		preview.verdict = Doors::Check(ref, preview.target);
		preview.distance = Doors::Distance(preview.target.shape, player->GetPosition());
		preview.stamina = Stamina(player);
		preview.kicksDone = Doors::Kicks(ref->GetFormID());
		return preview;
	}
}
