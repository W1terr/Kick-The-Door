#include "Kick.h"

#include "Clip.h"
#include "Forms.h"
#include "Keys.h"
#include "Lang.h"
#include "MathUtil.h"
#include "Settings.h"

namespace Kick
{
	namespace
	{
		using RE::NiPoint3;

		constexpr float kFadeIn = 0.08f;   // the animation file blends in and out over these
		constexpr float kFadeOut = 0.15f;

		// when things happen during a kick (seconds)
		struct Timing
		{
			float swing{ 0.20f };   // the whoosh
			float impact{ 0.34f };  // the foot meets the door
			float end{ 0.80f };     // standing again
			float turnEnd() const { return impact * 0.65f; }      // facing the door by now
			float stepStart() const { return impact * 0.12f; }
			float stepEnd() const { return impact * 0.70f; }
		};
		constexpr float kStandOff = 55.0f;  // the step in stops this far from the panel
		constexpr float kMaxStep = 50.0f;

		struct State
		{
			bool               active{ false };
			bool               clip{ false };  // playing kick.hkx (missing: the kick happens without an animation)
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
			float              step{ 0.0f };          // how far the player steps in
			float              stepped{ 0.0f };
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

		void Start(RE::PlayerCharacter* a_player)
		{
			const auto& s = Settings::Get();
			auto* const ref = CrosshairRef();
			if (!ref) {
				return;
			}
			Doors::Target target;
			const auto    verdict = Doors::Check(ref, target);
			if (verdict == Doors::Verdict::kNotLocked || verdict == Doors::Verdict::kNotLoaded || verdict == Doors::Verdict::kOpen) {
				return;  // nothing to kick (an unlocked door still opens with Activate as always)
			}
			if (verdict == Doors::Verdict::kNotADoor && !ref->GetBaseObject()->Is(RE::FormType::Door)) {
				return;  // not a door at all: the key may belong to something else
			}
			if (verdict != Doors::Verdict::kOk) {
				Doors::Notify(Doors::Describe(verdict, target.level));
				return;
			}
			const auto  position = a_player->GetPosition();
			const float distance = Doors::Distance(target.shape, position);
			if (distance > s.kickReach) {
				Doors::Notify(Lang::T("Too far away to kick it."));
				return;
			}

			state = {};
			state.active = true;
			state.door = ref->GetHandle();
			state.enoughStamina = Stamina(a_player) >= Settings::LockFor(target.level).staminaNeeded;
			state.face = Doors::NormalTowards(target.shape, position) * -1.0f;
			state.startHeading = a_player->GetAngleZ();
			state.heading = std::atan2(state.face.x, state.face.y);
			state.step = std::clamp(distance - kStandOff, 0.0f, kMaxStep);
			state.clip = Clip::Ready();
			if (state.clip) {
				const auto& clip = Clip::GetInfo();
				state.timing = { std::clamp(clip.swing, 0.0f, clip.duration), std::clamp(clip.impact, 0.05f, clip.duration), clip.duration };
			}
			BlockControls(true);
			if (s.firstPerson == 1 && !ImprovedCamera()) {
				auto* const camera = RE::PlayerCamera::GetSingleton();
				if (camera && camera->IsInFirstPerson()) {
					camera->ForceThirdPerson();
					state.thirdPerson = true;
				}
			}
			if (s.debugLog) {
				logs::info("Kick at {} [{:08X}] ({} lock, {} kick(s) so far), {:.0f} units away, stamina {:.0f} / {:.0f}", Forms::Name(ref),
					ref->GetFormID(), Doors::LevelName(target.level), Doors::Kicks(ref->GetFormID()), distance, Stamina(a_player),
					Settings::LockFor(target.level).staminaNeeded);
			}
		}

		void Resolve(RE::PlayerCharacter* a_player)
		{
			const auto& s = Settings::Get();
			const auto  ref = state.door.get();
			if (!ref) {
				return;
			}
			Doors::Target target;
			const auto    verdict = Doors::Check(ref.get(), target);
			if (verdict != Doors::Verdict::kOk) {
				return;  // unlocked or changed meanwhile: the foot meets a door that gives anyway
			}
			const auto  from = a_player->GetPosition();
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
				Doors::Notify(Lang::F("The door shudders. One more kick should break the {} lock.", Lang::T(Doors::LevelName(target.level))));
				return;
			}
			state.enterAfter = target.teleport && s.enterLoadDoors;
			Doors::Break(target, from, Doors::Cause::kKick, false);
			RE::ShakeCamera(0.4f, target.shape.center, 0.3f);
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

			// turn to the door and step in while the leg comes up
			if (before < timing.turnEnd()) {
				const float t = M::Smooth(std::min(state.time / timing.turnEnd(), 1.0f));
				a_player->SetHeading(M::LerpAngle(state.startHeading, state.heading, t));
			}
			if (state.step > 0.0f && before < timing.stepEnd()) {
				const float t = M::Smooth((std::min(state.time, timing.stepEnd()) - timing.stepStart()) / (timing.stepEnd() - timing.stepStart()));
				const float target = state.step * t;
				if (target > state.stepped + 0.01f) {
					a_player->SetPosition(a_player->GetPosition() + state.face * (target - state.stepped), true);
					state.stepped = target;
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
			if (state.time >= timing.end) {
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
		if (std::exchange(requested, false) && !state.active && Settings::Get().kicking && GameReady() && CanKick(player)) {
			Start(player);
		}
		if (state.active) {
			Step(player, a_delta);
		}
	}

	void ApplyPose()
	{
		if (state.active && state.clip) {
			// with a weapon drawn the arms keep the weapon stance
			auto* const player = RE::PlayerCharacter::GetSingleton();
			const float weight = std::min(M::Smooth(state.time / kFadeIn), M::Smooth((state.timing.end - state.time) / kFadeOut));
			Clip::Apply(player, state.time, weight, player->AsActorState()->IsWeaponDrawn());
		}
	}

	bool Active() { return state.active; }

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
