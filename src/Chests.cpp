#include "Chests.h"

#include "Doors.h"
#include "Forms.h"
#include "Lang.h"
#include "MathUtil.h"
#include "Settings.h"

namespace Chests
{
	namespace
	{
		using RE::NiPoint3;

		constexpr float kMaxHeight = 110.0f;  // taller containers (wardrobes, bookcases, urns on stands) aren't kicked open

		struct Sounds
		{
			RE::BGSSoundDescriptorForm* crack{ nullptr };  // WPNImpactBlunt2HandVsWood
		} sounds;

		void Play(RE::BGSSoundDescriptorForm* a_sound, const NiPoint3& a_at, float a_volume = 1.0f)
		{
			auto* const audio = RE::BSAudioManager::GetSingleton();
			RE::BSSoundHandle handle;
			if (a_sound && audio && audio->GetSoundHandle(handle, a_sound)) {
				handle.SetPosition(a_at);
				handle.SetVolume(a_volume);
				handle.Play();
			}
		}

		NiPoint3 Horizontal(const NiPoint3& a_v)
		{
			return M::Normalized(NiPoint3{ a_v.x, a_v.y, 0.0f }, NiPoint3{ 0.0f, 1.0f, 0.0f });
		}

		// where the foot meets it: the top of its front face
		NiPoint3 LockAt(const Target& a_target)
		{
			return a_target.center + a_target.front * a_target.halfDepth + NiPoint3{ 0.0f, 0.0f, a_target.top - a_target.center.z };
		}
	}

	void Init()
	{
		sounds.crack = Forms::Vanilla<RE::BGSSoundDescriptorForm>(0xE4F0E);
	}

	Verdict Check(RE::TESObjectREFR* a_ref, const NiPoint3& a_player, Target& a_out)
	{
		a_out = {};
		a_out.ref = a_ref;
		auto* const base = a_ref ? a_ref->GetBaseObject() : nullptr;
		if (!base || !base->Is(RE::FormType::Container) || a_ref->IsDisabled() || a_ref->IsMarkedForDeletion() || a_ref->IsDeleted()) {
			return Verdict::kNotAChest;
		}
		auto* const root = a_ref->Get3D();
		if (!a_ref->Is3DLoaded() || !root) {
			return Verdict::kNotLoaded;
		}

		// its box from the bounds (a few containers have none: the size of the common chest then)
		const auto& bound = base->boundData;
		NiPoint3    min{ static_cast<float>(bound.boundMin.x), static_cast<float>(bound.boundMin.y), static_cast<float>(bound.boundMin.z) };
		NiPoint3    max{ static_cast<float>(bound.boundMax.x), static_cast<float>(bound.boundMax.y), static_cast<float>(bound.boundMax.z) };
		if (max.x - min.x < 1.0f || max.y - min.y < 1.0f || max.z - min.z < 1.0f) {
			min = { -48.0f, -25.0f, 0.0f };
			max = { 48.0f, 24.0f, 48.0f };
		}
		const float scale = root->world.scale > 0.0f ? root->world.scale : a_ref->GetScale();
		const auto& rotate = root->world.rotate;
		if (std::abs(M::Column(rotate, 2).z) < 0.85f) {
			return Verdict::kNotAChest;  // lying on its side
		}
		const NiPoint3 half = (max - min) * (0.5f * scale);
		const NiPoint3 center = root->world.translate + rotate * ((min + max) * (0.5f * scale));
		a_out.top = center.z + half.z;
		a_out.center = { center.x, center.y, center.z - half.z };
		if (a_out.top - a_player.z > kMaxHeight) {
			return Verdict::kNotAChest;
		}
		// forced from the long side the player is on (the lock is on the model's +y face; from behind works too)
		const NiPoint3 alongX = Horizontal(M::Column(rotate, 0));
		const NiPoint3 alongY = Horizontal(M::Column(rotate, 1));
		const bool     wideX = half.x >= half.y;
		const NiPoint3 across = wideX ? alongY : alongX;
		a_out.front = M::Dot(a_player - center, across) >= 0.0f ? across : across * -1.0f;
		a_out.halfDepth = wideX ? half.y : half.x;

		const auto level = a_ref->GetLockLevel();
		if (level == RE::LOCK_LEVEL::kUnlocked) {
			return Verdict::kNotLocked;
		}
		const int value = static_cast<int>(level);
		if (level == RE::LOCK_LEVEL::kRequiresKey || value < 0 || value >= static_cast<int>(Settings::kLevels)) {
			return Verdict::kNeedsKey;
		}
		a_out.level = value;
		if (a_ref->IsActivationBlocked()) {
			return Verdict::kBlocked;
		}
		if (!Settings::ChestLockFor(value).enabled) {
			return Verdict::kLevelOff;
		}
		return Verdict::kOk;
	}

	std::string Describe(Verdict a_verdict, int a_level)
	{
		switch (a_verdict) {
		case Verdict::kNeedsKey:
			return Lang::T("This lock requires a key.");
		case Verdict::kLevelOff:
			return Lang::F("Forcing {} locks is turned off.", Lang::T(Doors::LevelName(a_level)));
		case Verdict::kBlocked:
			return Lang::T("This chest can't be forced open.");
		default:
			return {};
		}
	}

	NiPoint3 StandAt(const Target& a_target, float a_standOff)
	{
		return a_target.center + a_target.front * (a_target.halfDepth + a_standOff);
	}

	void Hit(const Target& a_target)
	{
		if (!a_target.ref) {
			return;
		}
		const NiPoint3 at = LockAt(a_target);
		Play(sounds.crack, at);
		Doors::Noise(a_target.ref, at);
	}

	void Break(const Target& a_target)
	{
		auto* const chest = a_target.ref;
		auto* const lock = chest ? chest->GetLock() : nullptr;
		if (!lock) {
			return;
		}
		// the same state change a picked lock makes: unlocked, saved with the reference, scripts hear about it
		lock->SetLocked(false);
		chest->AddLockChange();
		RE::TESLockChangedEvent event{ RE::NiPointer<RE::TESObjectREFR>(chest) };
		RE::ScriptEventSourceHolder::GetSingleton()->SendEvent(&event);
		Doors::SetKicks(chest->GetFormID(), 0);
		const NiPoint3 at = LockAt(a_target);
		Play(sounds.crack, at);
		Doors::PlayLockBreak(at);
		Doors::Crime(chest);
		Doors::Noise(chest, at);
		if (Settings::Get().debugLog) {
			logs::info("Chest {} [{:08X}] ({} lock) forced open", Forms::Name(chest), chest->GetFormID(), Doors::LevelName(a_target.level));
		}
	}

	void Open(RE::TESObjectREFR* a_chest)
	{
		if (a_chest && !a_chest->IsLocked()) {
			a_chest->ActivateRef(RE::PlayerCharacter::GetSingleton(), 0, nullptr, 1, false);
		}
	}
}
