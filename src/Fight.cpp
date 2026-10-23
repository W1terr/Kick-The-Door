#include "Fight.h"

#include "Forms.h"
#include "MathUtil.h"
#include "Ray.h"
#include "Settings.h"

namespace Fight
{
	namespace
	{
		using RE::NiPoint3;

		constexpr float kReachCone = 40.0f;   // degrees either side of where the player faces (widened by a target's girth)
		constexpr float kKickLow = 10.0f;     // the kick sweeps this band above the player's feet
		constexpr float kKickHigh = 100.0f;
		constexpr int   kSightChecks = 3;     // candidates tried for a clear line before giving up

		RE::SpellItem*              spell{ nullptr };   // KTD_KickSpell: StaggerAttackFFContact, hostile
		RE::SpellItem*              damage{ nullptr };  // KTD_KickDamage: 2 health
		RE::BGSSoundDescriptorForm* thud{ nullptr };   // FXMeleePunchLarge

		bool Kickable(RE::PlayerCharacter* a_player, RE::Actor* a_actor)
		{
			return a_actor && a_actor != a_player && !a_actor->IsDead() && a_actor->Is3DLoaded() && !a_actor->IsDisabled();
		}

		// an actor as the kick sees it: how far their body is, how far off to the side, how tall
		struct Body
		{
			RE::Actor* actor{ nullptr };
			float      edge{ 0.0f };   // horizontal distance from the player to the near side of their body
			float      angle{ 0.0f };  // degrees off where the player faces (to their center)
			NiPoint3   aim;            // the point a line of sight goes to
			float      score{ 0.0f };  // lower = the one the foot meets first
		};

		std::optional<Body> Measure(RE::PlayerCharacter* a_player, RE::Actor* a_actor, const NiPoint3& a_from, float a_reach)
		{
			const auto at = a_actor->GetPosition();
			const float dx = at.x - a_from.x;
			const float dy = at.y - a_from.y;
			const float far = a_reach + 200.0f;  // cheap test first: nobody is wider than that
			if (dx * dx + dy * dy > far * far) {
				return std::nullopt;
			}
			// their bounds: girth and height (big creatures reach farther and higher)
			const auto  min = a_actor->GetBoundMin();
			const auto  max = a_actor->GetBoundMax();
			const float radius = std::clamp(((max.x - min.x) + (max.y - min.y)) * 0.25f, 10.0f, 200.0f);
			float       bottom = at.z + min.z;
			float       top = at.z + max.z;
			if (top - bottom < 20.0f) {
				bottom = at.z;
				top = at.z + 120.0f;
			}
			if (bottom > a_from.z + kKickHigh || top < a_from.z + kKickLow) {
				return std::nullopt;  // above or below the kick (on a ledge, down the stairs, flying)
			}
			const float distance = std::sqrt(dx * dx + dy * dy);
			const float edge = std::max(distance - radius, 0.0f);
			if (edge > a_reach) {
				return std::nullopt;
			}
			const float angle = std::abs(a_player->GetHeadingAngle(at, false));
			const float girth = distance > 1.0f ? std::atan2(radius, distance) / M::kDegrees : 90.0f;
			if (angle > kReachCone + girth) {
				return std::nullopt;
			}
			const float height = std::clamp(a_from.z + 60.0f, bottom + 10.0f, std::max(top - 10.0f, bottom + 10.0f));
			return Body{ a_actor, edge, angle, NiPoint3{ at.x, at.y, height }, edge + angle * 0.5f };
		}

		// nothing solid between the player and them (another actor in the way counts: the foot meets that one first)
		bool InSight(RE::PlayerCharacter* a_player, const Body& a_body, const NiPoint3& a_from)
		{
			const NiPoint3 eye{ a_from.x, a_from.y, a_from.z + 60.0f };
			const auto     ray = ::Ray::Cast(a_player, eye, a_body.aim);
			return !ray.hit || ray.ref == a_body.actor || ray.distance >= M::Length(a_body.aim - eye) - 25.0f;
		}

		bool HeavyArmor(RE::Actor* a_actor)
		{
			auto* const body = a_actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kBody);
			return body && body->GetArmorType() == RE::BGSBipedObjectForm::ArmorType::kHeavyArmor;
		}

		// what keeps someone on their feet when a sprint kick lands: heavy armor, a mount, being big or unable to fall
		const char* StaysUp(RE::Actor* a_actor)
		{
			auto* const race = a_actor->GetRace();
			auto* const state = a_actor->AsActorState();
			if (HeavyArmor(a_actor)) {
				return "heavy armor";
			}
			if (!race || race->data.flags.any(RE::RACE_DATA::Flag::kNoKnockdowns) || race->data.raceSize.get() > RE::RACE_SIZE::kMedium) {
				return "can't be knocked down";
			}
			if (a_actor->IsOnMount() || a_actor->IsInKillMove() || a_actor->IsInRagdollState() || !state ||
				state->GetKnockState() != RE::KNOCK_STATE_ENUM::kNormal || !a_actor->GetActorRuntimeData().currentProcess) {
				return "busy";
			}
			return nullptr;
		}

		void Stagger(RE::Actor* a_actor, const NiPoint3& a_source, float a_magnitude)
		{
			float direction = a_actor->GetHeadingAngle(a_source, false) / 360.0f;  // 0 = from the front
			if (direction < 0.0f) {
				direction += 1.0f;
			}
			a_actor->SetGraphVariableFloat("staggerDirection", direction);
			a_actor->SetGraphVariableFloat("staggerMagnitude", a_magnitude);
			a_actor->NotifyAnimationGraph("staggerStart");
		}
	}

	void Init()
	{
		spell = RE::TESDataHandler::GetSingleton()->LookupForm<RE::SpellItem>(0x801, "KickTheDoor.esp");
		damage = RE::TESDataHandler::GetSingleton()->LookupForm<RE::SpellItem>(0x803, "KickTheDoor.esp");
		thud = Forms::Vanilla<RE::BGSSoundDescriptorForm>(0x0AF664);
		if (!spell) {
			logs::warn("The kick spell is missing (is KickTheDoor.esp enabled?): kicked people are staggered, but not angered");
		}
	}

	RE::Actor* Reached(RE::PlayerCharacter* a_player, float a_reach)
	{
		auto* const lists = RE::ProcessLists::GetSingleton();
		if (!lists) {
			return nullptr;
		}
		const auto        from = a_player->GetPosition();
		std::vector<Body> bodies;
		for (const auto& handle : lists->highActorHandles) {
			const auto actor = handle.get();
			if (!Kickable(a_player, actor.get())) {
				continue;
			}
			if (const auto body = Measure(a_player, actor.get(), from, a_reach)) {
				bodies.push_back(*body);
			}
		}
		std::ranges::sort(bodies, {}, &Body::score);
		for (std::size_t i = 0; i < bodies.size() && i < kSightChecks; ++i) {
			if (InSight(a_player, bodies[i], from)) {
				if (Settings::Get().debugLog) {
					logs::info("The kick reaches {} [{:08X}]: {:.0f} units to their body, {:.0f} deg off ({} in reach)", Forms::Name(bodies[i].actor),
						bodies[i].actor->GetFormID(), bodies[i].edge, bodies[i].angle, bodies.size());
				}
				return bodies[i].actor;
			}
		}
		return nullptr;
	}

	void Damage(RE::PlayerCharacter* a_player, RE::Actor* a_target)
	{
		auto* const caster = damage && a_target && !a_target->IsDead() ? a_player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant) : nullptr;
		if (caster) {
			caster->CastSpellImmediate(damage, true, a_target, 1.0f, false, 0.0f, a_player);
		}
	}

	void Hit(RE::PlayerCharacter* a_player, RE::Actor* a_target, bool a_sprinting)
	{
		const auto& s = Settings::Get();
		const auto  from = a_player->GetPosition();
		// the hit itself: the target and everyone around take it as an attack by the player (combat, assault); its
		// Stagger effect staggers them
		if (auto* const caster = spell ? a_player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant) : nullptr) {
			caster->CastSpellImmediate(spell, false, a_target, 1.0f, false, s.kickStagger, a_player);
		} else {
			Stagger(a_target, from, s.kickStagger);
		}
		Damage(a_player, a_target);
		const char* stays = a_sprinting ? StaysUp(a_target) : "not sprinting";
		const bool  down = a_sprinting && s.sprintKnockdown && !stays && s.sprintKnockdownForce > 0.0f;
		if (down) {
			a_target->GetActorRuntimeData().currentProcess->KnockExplosion(a_target, from, s.sprintKnockdownForce);
		}
		if (auto* const audio = RE::BSAudioManager::GetSingleton(); audio && thud) {
			RE::BSSoundHandle handle;
			if (audio->GetSoundHandle(handle, thud)) {
				handle.SetPosition(a_target->GetPosition() + NiPoint3{ 0.0f, 0.0f, 60.0f });
				handle.Play();
			}
		}
		if (s.debugLog) {
			const char* why = !s.sprintKnockdown || s.sprintKnockdownForce <= 0.0f ? "knockdown off" : stays;
			logs::info("Kicked {} [{:08X}]: {}", Forms::Name(a_target), a_target->GetFormID(), down ? "knocked down" : std::format("staggered ({})", why));
		}
	}
}
