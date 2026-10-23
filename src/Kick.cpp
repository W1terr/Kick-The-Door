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
			float standOff{ 55.0f };   // the step stops this far from the panel
			float blendIn{ 0.08f };
			float blendOut{ 0.15f };   // after the last frame
			float end() const { return last + blendOut; }  // standing again
		};
		constexpr float kMaxStep = 50.0f;   // the step to a door: forward at most,
		constexpr float kMaxBack = 25.0f;   // back, when standing closer than the kick needs
		constexpr float kSprintBlendIn = 0.2f;      // out of a sprint the body takes this long to go over from the run
		constexpr float kComboBlendIn = 0.12f;      // out of an attack, from the swing
		constexpr float kCancelBlendOut = 0.12f;    // an attack after the hit takes over this fast
		constexpr float kSlideAfterImpact = 0.2f;   // and the run's momentum dies out this long after the foot lands
		constexpr float kAssumedRun = 400.0f;       // sprinting, but the speed couldn't be read (units/s)
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
			bool               animated{ false };  // kick.hkx plays (missing: the kick happens without an animation)
			Timing             timing;
			float              time{ 0.0f };     // in the animation (starts later out of a sprint or an attack)
			float              speed{ 1.0f };    // animation seconds per second (faster out of an attack)
			bool               attacking{ false };  // kicked out of a melee attack (a combo)
			bool               attackFreed{ false };  // the attack button works again (after the hit)
			float              startTime{ 0.0f };
			float              elapsed{ 0.0f };  // since the kick started (the blend in)
			float              blendIn{ 0.0f };
			RE::ObjectRefHandle door;
			bool               enoughStamina{ false };
			bool               swung{ false };
			bool               resolved{ false };
			bool               enterAfter{ false };
			NiPoint3           face;                  // horizontal: where the player faces, or into the door
			float              step{ 0.0f };          // how far the player steps in (negative: back)
			float              stepped{ 0.0f };
			float              slide{ 0.0f };      // kicked out of a sprint: the run carries the player this far,
			float              slideTime{ 0.0f };  // over this long from the press, starting at the running speed and
			float              slid{ 0.0f };       // slowing down evenly
			std::vector<RE::PlayerInputHandler*> blocked;
		} state;

		// key: the press that already started a kick doesn't start another while held
		bool fired{ false };
		bool requested{ false };

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
		// a melee swing, power attack or bash (not a bow or a spell)
		bool MeleeAttack(RE::ATTACK_STATE_ENUM a_state)
		{
			return a_state >= RE::ATTACK_STATE_ENUM::kDraw && a_state <= RE::ATTACK_STATE_ENUM::kBash;
		}

		// the swing or combo in progress ends where it is (as the game does when a weapon is swapped mid-swing): the
		// behavior leaves the attack, its hits don't land
		void StopAttack(RE::PlayerCharacter* a_player)
		{
			a_player->NotifyAnimationGraph("attackStop");
			a_player->NotifyAnimationGraph("PowerAttackStop");
			if (auto* const actorState = a_player->AsActorState()) {
				actorState->actorState1.meleeAttackState = RE::ATTACK_STATE_ENUM::kNone;
			}
		}

		bool CanKick(RE::PlayerCharacter* a_player)
		{
			auto* const actorState = a_player->AsActorState();
			const auto  attack = actorState ? actorState->GetAttackState() : RE::ATTACK_STATE_ENUM::kNone;
			return a_player->Is3DLoaded() && !a_player->IsDead() && !a_player->IsOnMount() && !a_player->IsInKillMove() &&
			       !a_player->IsInRagdollState() && !a_player->IsInMidair() && actorState &&
			       actorState->GetSitSleepState() == RE::SIT_SLEEP_STATE::kNormal && actorState->GetKnockState() == RE::KNOCK_STATE_ENUM::kNormal &&
			       !actorState->IsSwimming() && !actorState->IsBleedingOut() && !actorState->IsFlying() &&
			       (attack == RE::ATTACK_STATE_ENUM::kNone || (Settings::Get().comboKicks && MeleeAttack(attack)));
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
						 controls->readyWeaponHandler, controls->activateHandler }) {
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

		// the attack button works again (the rest of the controls only when the kick is over)
		void FreeAttack()
		{
			auto* const controls = RE::PlayerControls::GetSingleton();
			if (state.attackFreed || !controls || !controls->attackBlockHandler) {
				return;
			}
			state.attackFreed = true;
			if (const auto it = std::ranges::find(state.blocked, controls->attackBlockHandler); it != state.blocked.end()) {
				controls->attackBlockHandler->SetInputEventHandlingEnabled(true);
				state.blocked.erase(it);
			}
		}

		void Finish()
		{
			Clip::Release();
			BlockControls(false);
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

		// the game's own movement stops for the kick: a sprint ends and what speed is left doesn't carry the player on
		// (the kick's steps move them itself). Gravity stays.
		void Halt(RE::PlayerCharacter* a_player)
		{
			auto* const controller = a_player->GetCharController();
			if (!controller) {
				return;
			}
			RE::hkVector4 velocity;
			controller->GetLinearVelocityImpl(velocity);
			controller->SetLinearVelocityImpl(RE::hkVector4(0.0f, 0.0f, velocity.quad.m128_f32[2], 0.0f));
		}

		// how fast the player is moving over the ground (units/s)
		float GroundSpeed(RE::PlayerCharacter* a_player)
		{
			auto* const controller = a_player->GetCharController();
			if (!controller) {
				return 0.0f;
			}
			RE::hkVector4 velocity;
			controller->GetLinearVelocityImpl(velocity);
			const float x = velocity.quad.m128_f32[0];
			const float y = velocity.quad.m128_f32[1];
			return std::sqrt(x * x + y * y) / RE::bhkWorld::GetWorldScale();
		}

		void EndSprint(RE::PlayerCharacter* a_player)
		{
			if (auto* const actorState = a_player->AsActorState(); actorState && actorState->IsSprinting()) {
				a_player->NotifyAnimationGraph("SprintStop");
				actorState->actorState1.sprinting = 0;
			}
		}

		// a step forward stops short of a wall or anyone else in the way
		float Clear(RE::PlayerCharacter* a_player, const NiPoint3& a_face, float a_step)
		{
			constexpr float kBody = 30.0f;   // the body stops this far from what the ray meets
			constexpr float kAhead = 15.0f;  // the rays start in front of the player's own body
			if (a_step <= 0.0f) {
				return a_step;
			}
			const auto at = a_player->GetPosition() + a_face * kAhead;
			for (const float height : { 50.0f, 95.0f }) {  // above steps and clutter on the floor
				const NiPoint3 from = at + NiPoint3{ 0.0f, 0.0f, height };
				if (const auto ray = ::Ray::Cast(a_player, from, from + a_face * (a_step + kBody)); ray.hit) {
					a_step = std::min(a_step, std::max(ray.distance + kAhead - kBody, 0.0f));
				}
			}
			return a_step;
		}

		void Start(RE::PlayerCharacter* a_player)
		{
			const auto& s = Settings::Get();
			auto* const ref = DoorInFront(a_player, s.kickReach);
			const auto  position = a_player->GetPosition();
			const bool  sprinting = a_player->AsActorState()->IsSprinting();
			const float runSpeed = sprinting ? GroundSpeed(a_player) : 0.0f;
			const bool  attacking = MeleeAttack(a_player->AsActorState()->GetAttackState());
			// the quick kick: out of an attack, or in a fight (weapon out) unless the run carries into it
			const bool  quick = attacking || (a_player->AsActorState()->IsWeaponDrawn() && !sprinting);

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
			state.attacking = attacking;
			if (const auto clip = Clip::Get()) {
				state.animated = true;
				auto& t = state.timing;
				t.last = clip->duration;
				t.impact = std::clamp(clip->impact, 0.05f, t.last);
				t.swing = std::clamp(clip->swing, 0.0f, t.last);
				t.stepStart = std::clamp(clip->stepStart, 0.0f, t.impact);
				t.stepEnd = std::clamp(clip->stepEnd, t.stepStart + 0.01f, std::max(t.impact, t.stepStart + 0.01f));
				t.standOff = clip->standOff;
				t.blendIn = clip->blendIn;
				t.blendOut = clip->blendOut;
				if (quick) {
					// out of a swing or a combo, or with a weapon out (a fight): a shorter, quicker kick that fits
					// between the strikes (its slow start and its long return to standing are left out)
					state.startTime = std::clamp(clip->comboStart, 0.0f, t.impact * 0.8f);
					state.speed = clip->comboSpeed;
					if (clip->comboEnd > 0.0f) {
						t.last = std::clamp(clip->comboEnd, t.impact + 0.05f, t.last);
					}
					t.stepStart = std::max(t.stepStart, state.startTime);
					t.stepEnd = std::max(t.stepEnd, t.stepStart + 0.01f);
				} else if (sprinting) {
					// out of a sprint the run flows straight into the kick: what the animation does before (a guard...)
					// is skipped and the body blends over from the run more slowly
					state.startTime = std::clamp(clip->sprintStart, 0.0f, t.impact * 0.8f);
					t.stepStart = std::max(t.stepStart, state.startTime);
					t.stepEnd = std::max(t.stepEnd, t.stepStart + 0.01f);
				}
			}
			const auto& t = state.timing;
			state.time = state.startTime;
			state.blendIn = quick ? std::max(t.blendIn, kComboBlendIn) : sprinting ? std::max(t.blendIn, kSprintBlendIn) : t.blendIn;
			const float heading = a_player->GetAngleZ();
			if (atDoor) {
				state.mode = Mode::kDoor;
				state.door = ref->GetHandle();
				state.doorVerdict = verdict;
				state.enoughStamina = verdict == Doors::Verdict::kOk && Stamina(a_player) >= Settings::LockFor(door.level).staminaNeeded;
				state.face = Doors::NormalTowards(door.shape, position) * -1.0f;
				if (sprinting) {
					state.slide = std::max(doorDistance - t.standOff, 0.0f);  // never into the door
				}
				state.step = std::clamp(doorDistance - t.standOff - state.slide, -kMaxBack, kMaxStep);
			} else {
				// no turning to anyone and no pulling towards them: the kick goes where the player faces (a sprint
				// carries them on a little, stopping short of whatever is in the way)
				a_player->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kStamina, s.kickCost);
				state.mode = Mode::kFree;
				state.face = NiPoint3{ std::sin(heading), std::cos(heading), 0.0f };
				if (sprinting) {
					state.slide = Clear(a_player, state.face, s.sprintSlide);
				}
			}
			if (sprinting && state.slide > 0.0f) {
				// The run carries on: it starts at the running speed (no sudden brake) and slows down evenly until a
				// little after the foot lands. Something in the way or the "Sprint slide" limit: it stops sooner,
				// still from the running speed.
				const float speed = runSpeed > 150.0f ? runSpeed : kAssumedRun;
				state.slideTime = (t.impact - state.startTime) + kSlideAfterImpact;
				state.slide = std::min({ state.slide, s.sprintSlide, speed * state.slideTime * 0.5f });
				state.slideTime = 2.0f * state.slide / speed;
			} else {
				state.slide = 0.0f;
			}
			BlockControls(true);
			if (attacking) {
				StopAttack(a_player);
			}
			EndSprint(a_player);
			Halt(a_player);
			if (s.debugLog) {
				if (atDoor) {
					logs::info("Kick at {} [{:08X}] ({} lock, {} kick(s) so far), {:.0f} units away, stamina {:.0f} / {:.0f}{}", Forms::Name(ref),
						ref->GetFormID(), Doors::LevelName(door.level), Doors::Kicks(ref->GetFormID()), doorDistance, Stamina(a_player),
						Settings::LockFor(door.level).staminaNeeded, verdict == Doors::Verdict::kOk ? "" : " - it won't give");
				} else {
					logs::info("Kick{}{}{}, step {:.0f}, slide {:.0f} (run {:.0f}/s)", sprinting ? " while sprinting" : "", attacking ? " out of an attack" : "",
						quick ? " (quick)" : "",
						state.step, state.slide, runSpeed);
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
					Doors::Notify(Doors::Describe(verdict, target.level));
				}
				return;  // unlocked or changed meanwhile: the foot meets a door that gives anyway
			}
			const auto& lock = Settings::LockFor(target.level);
			if (!state.enoughStamina) {
				Doors::Hit(target, from, false);
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
			state.time += a_delta * state.speed;
			state.elapsed += a_delta;
			const auto& timing = state.timing;
			if (Interrupted(a_player) && state.time < timing.impact) {
				Finish();  // knocked over, killed... before the foot landed
				return;
			}

			// step in (or back) before the kick; the player keeps facing their own way (the camera is theirs)
			if (state.step != 0.0f && before < timing.stepEnd) {
				const float t = M::Smooth((std::min(state.time, timing.stepEnd) - timing.stepStart) / (timing.stepEnd - timing.stepStart));
				const float target = state.step * t;
				if (std::abs(target - state.stepped) > 0.01f) {
					a_player->SetPosition(a_player->GetPosition() + state.face * (target - state.stepped), true);
					state.stepped = target;
				}
			}
			// out of a sprint the run carries the player on, slowing down evenly to a stop
			if (state.slide > 0.0f && state.slid < state.slide) {
				const float u = std::min(state.elapsed / std::max(state.slideTime, 0.01f), 1.0f);
				const float target = state.slide * (1.0f - (1.0f - u) * (1.0f - u));
				a_player->SetPosition(a_player->GetPosition() + state.face * (target - state.slid), true);
				state.slid = target;
			}
			if (auto* const controls = RE::PlayerControls::GetSingleton()) {
				controls->data.moveInputVec = {};
			}
			Halt(a_player);

			if (!state.swung && state.time >= timing.swing) {
				state.swung = true;
				Doors::PlaySwing(a_player->GetPosition() + NiPoint3{ 0.0f, 0.0f, 60.0f });
			}
			if (!state.resolved && state.time >= timing.impact) {
				state.resolved = true;
				Resolve(a_player);
			}
			// after the hit, with a weapon out, attacking again cuts the kick's end short: the combo goes on
			if (state.resolved && a_player->AsActorState()->IsWeaponDrawn()) {
				FreeAttack();
				if (MeleeAttack(a_player->AsActorState()->GetAttackState()) && state.time < timing.last) {
					auto& t = state.timing;
					t.last = state.time;
					t.blendOut = kCancelBlendOut * state.speed;
				}
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
		if (state.active && state.animated) {
			auto* const player = RE::PlayerCharacter::GetSingleton();
			const auto& t = state.timing;
			const float in = state.blendIn > 0.0f ? M::Smooth(state.elapsed / state.blendIn) : 1.0f;
			const float out = t.blendOut > 0.0f ? M::Smooth((t.end() - state.time) / t.blendOut) : (state.time < t.last ? 1.0f : 0.0f);
			const float weight = std::min(in, out);
			Clip::Apply(player, state.time, weight, player->AsActorState()->IsWeaponDrawn());  // a weapon out: the arms keep it
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
