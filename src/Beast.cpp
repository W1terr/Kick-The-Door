#include "Beast.h"

#include "Doors.h"
#include "Forms.h"
#include "MathUtil.h"
#include "Ray.h"
#include "Settings.h"

namespace Beast
{
	namespace
	{
		using M::Dot;
		using M::Length;
		using RE::NiPoint3;

		constexpr float kSpellSpeed = 2500.0f;  // a spell reaches a door this fast (units/s)
		constexpr float kInFront = 0.3f;        // a blow lands on a door within ~70 degrees of where the beast faces

		std::unordered_set<RE::FormID> werewolves;
		std::unordered_set<RE::FormID> vampireLords;

		bool blowDone{ false };  // this attack's hit already went to a door (or to nothing)
		RE::ATTACK_STATE_ENUM lastAttack{ RE::ATTACK_STATE_ENUM::kNone };

		struct Bolt
		{
			RE::ObjectRefHandle door;
			float               left{ 0.0f };  // seconds until it gets there
		};
		std::vector<Bolt> bolts;

		const char* Name(Form a_form) { return a_form == Form::kWerewolf ? "werewolf" : "vampire lord"; }

		bool IsDoor(const RE::TESObjectREFR* a_ref)
		{
			const auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
			return base && base->Is(RE::FormType::Door);
		}

		RE::TESObjectREFR* CrosshairRef()
		{
			auto* const pick = RE::CrosshairPickData::GetSingleton();
			return pick ? pick->GetActiveTarget().get().get() : nullptr;
		}

		// the door the blow lands on: under the crosshair or straight ahead (chest or head height), within reach of the
		// body and in front of it
		RE::TESObjectREFR* DoorAhead(RE::PlayerCharacter* a_player, float a_reach)
		{
			const NiPoint3 position = a_player->GetPosition();
			const float    heading = a_player->GetAngleZ();
			const NiPoint3 face{ std::sin(heading), std::cos(heading), 0.0f };
			std::vector<RE::TESObjectREFR*> candidates;
			if (auto* const ref = CrosshairRef(); IsDoor(ref)) {
				candidates.push_back(ref);
			}
			for (const float height : { 70.0f, 120.0f, 170.0f }) {
				const NiPoint3 from = position + NiPoint3{ 0.0f, 0.0f, height };
				if (const auto ray = ::Ray::Cast(a_player, from, from + face * (a_reach + 40.0f)); ray.hit && IsDoor(ray.ref)) {
					candidates.push_back(ray.ref);
				}
			}
			for (auto* const ref : candidates) {
				const auto shape = Doors::GetShape(ref);
				if (!shape || Doors::Distance(*shape, position) > a_reach) {
					continue;
				}
				NiPoint3 to = shape->center - position;
				to.z = 0.0f;
				if (Length(to) > 60.0f && Dot(M::Normalized(to), face) < kInFront) {
					continue;
				}
				return ref;
			}
			return nullptr;
		}

		// the door gives (any lock the mod may force, one blow); one that won't only takes the thud
		void Smash(RE::PlayerCharacter* a_player, RE::TESObjectREFR* a_door, Form a_form, const char* a_how)
		{
			Doors::Target target;
			const auto    verdict = Doors::Check(a_door, target);
			const auto    from = a_player->GetPosition();
			if (Settings::Get().debugLog) {
				logs::info("{} {} at {} [{:08X}]: {}", Name(a_form), a_how, Forms::Name(a_door), a_door->GetFormID(), Doors::Describe(verdict, target.level));
			}
			switch (verdict) {
			case Doors::Verdict::kOk:
				Doors::Break(target, from, Doors::Cause::kBeast, false);
				return;
			case Doors::Verdict::kNotADoor:
			case Doors::Verdict::kNotLocked:
			case Doors::Verdict::kOpen:
			case Doors::Verdict::kNotLoaded:
				return;
			default:
				Doors::Hit(target, from, false);
				Doors::Notify(Doors::Describe(verdict, target.level));
				return;
			}
		}

		// the attack under way is a power attack (its attack data, as the race defines it)
		bool PowerAttack(RE::PlayerCharacter* a_player)
		{
			auto* const process = a_player->GetActorRuntimeData().currentProcess;
			auto* const high = process ? process->high : nullptr;
			return high && high->attackData && high->attackData->data.flags.any(RE::AttackData::AttackFlag::kPowerAttack);
		}

		// a power attack's hit (the attack state past the swing): once per attack
		void Blows(RE::PlayerCharacter* a_player, Form a_form)
		{
			auto* const actorState = a_player->AsActorState();
			const auto  attack = actorState ? actorState->GetAttackState() : RE::ATTACK_STATE_ENUM::kNone;
			if (attack != lastAttack && Settings::Get().debugLog) {
				logs::info("{} attack state {} -> {}{}", Name(a_form), std::to_underlying(lastAttack), std::to_underlying(attack),
					PowerAttack(a_player) ? " (power attack)" : "");
			}
			lastAttack = attack;
			const bool hit = attack == RE::ATTACK_STATE_ENUM::kHit || attack == RE::ATTACK_STATE_ENUM::kNextAttack ||
			                 attack == RE::ATTACK_STATE_ENUM::kFollowThrough;
			if (!hit) {
				blowDone = false;
				return;
			}
			if (std::exchange(blowDone, true) || !PowerAttack(a_player)) {
				return;
			}
			const auto& s = Settings::Get();
			if (!(a_form == Form::kWerewolf ? s.werewolfDoors : s.vampireLordDoors)) {
				return;
			}
			if (auto* const door = DoorAhead(a_player, s.beastReach)) {
				Smash(a_player, door, a_form, "power attack");
			}
		}

		// where the camera looks: the door a spell is aimed at (the crosshair, or the first thing along the view)
		RE::TESObjectREFR* AimedDoor(RE::PlayerCharacter* a_player, float a_reach)
		{
			if (auto* const ref = CrosshairRef(); IsDoor(ref)) {
				return ref;
			}
			auto* const camera = RE::PlayerCamera::GetSingleton();
			auto* const root = camera ? camera->cameraRoot.get() : nullptr;
			if (!root) {
				return nullptr;
			}
			const NiPoint3 eye = root->world.translate;
			const NiPoint3 view = M::Normalized(M::Column(root->world.rotate, 1), { 0.0f, 1.0f, 0.0f });
			const float    behind = Length(eye - a_player->GetPosition());  // a 3rd person camera sits behind the body
			const auto     ray = ::Ray::Cast(a_player, eye, eye + view * (a_reach + behind));
			return ray.hit && IsDoor(ray.ref) ? ray.ref : nullptr;
		}

		void Cast(RE::PlayerCharacter* a_player, const RE::MagicItem* a_spell)
		{
			const auto& s = Settings::Get();
			if (!s.vampireLordMagic || !a_spell || !a_spell->IsHostile() || a_spell->GetDelivery() == RE::MagicSystem::Delivery::kSelf ||
				Of(a_player) != Form::kVampireLord) {
				return;
			}
			auto* const door = AimedDoor(a_player, s.beastMagicReach);
			const auto  shape = door ? Doors::GetShape(door) : std::nullopt;
			if (!shape) {
				return;
			}
			const float distance = Doors::Distance(*shape, a_player->GetPosition());
			if (distance > s.beastMagicReach) {
				return;
			}
			if (s.debugLog) {
				logs::info("vampire lord casts {} at {} [{:08X}], {:.0f} units away", Forms::Name(a_spell), Forms::Name(door), door->GetFormID(), distance);
			}
			bolts.push_back({ door->GetHandle(), std::max(distance / kSpellSpeed, 0.1f) });
		}

		class CastSink : public RE::BSTEventSink<RE::TESSpellCastEvent>
		{
		public:
			static CastSink* GetSingleton()
			{
				static CastSink sink;
				return &sink;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent* a_event, RE::BSTEventSource<RE::TESSpellCastEvent>*) override
			{
				if (a_event && a_event->object && a_event->object->IsPlayerRef() && !vampireLords.empty()) {
					Cast(RE::PlayerCharacter::GetSingleton(), RE::TESForm::LookupByID<RE::MagicItem>(a_event->spell));
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Init()
	{
		werewolves = Forms::ResolveAll(Settings::GetLists().werewolfRaces, "Werewolf race");
		vampireLords = Forms::ResolveAll(Settings::GetLists().vampireLordRaces, "Vampire lord race");
		logs::info("Beast forms: {} werewolf race(s), {} vampire lord race(s)", werewolves.size(), vampireLords.size());
		RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESSpellCastEvent>(CastSink::GetSingleton());
	}

	Form Of(const RE::PlayerCharacter* a_player)
	{
		const auto* race = a_player ? a_player->GetRace() : nullptr;
		if (!race) {
			return Form::kNone;
		}
		if (werewolves.contains(race->GetFormID())) {
			return Form::kWerewolf;
		}
		return vampireLords.contains(race->GetFormID()) ? Form::kVampireLord : Form::kNone;
	}

	void Update(float a_delta)
	{
		auto* const player = RE::PlayerCharacter::GetSingleton();
		const Form  form = Of(player);
		if (form == Form::kNone) {
			blowDone = false;
			lastAttack = RE::ATTACK_STATE_ENUM::kNone;
			bolts.clear();
			return;
		}
		Blows(player, form);
		for (auto it = bolts.begin(); it != bolts.end();) {
			it->left -= a_delta;
			if (it->left > 0.0f) {
				++it;
				continue;
			}
			if (const auto door = it->door.get()) {
				Smash(player, door.get(), form, "spell");
			}
			it = bolts.erase(it);
		}
	}

	void Reset()
	{
		blowDone = false;
		lastAttack = RE::ATTACK_STATE_ENUM::kNone;
		bolts.clear();
	}
}
