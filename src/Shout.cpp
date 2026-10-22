#include "Shout.h"

#include "Doors.h"
#include "Forms.h"
#include "Lang.h"
#include "MathUtil.h"
#include "Ray.h"
#include "Settings.h"

namespace Shout
{
	namespace
	{
		using M::Dot;
		using M::Length;
		using M::Normalized;
		using RE::NiPoint3;

		constexpr float kDefaultSpeed = 1536.0f;            // VoicePushProjectile01-03
		constexpr float kDefaultTangent = 0.2679f;          // tan(15 degrees), their cone spread
		constexpr float kLateSlack = 300.0f;                // a wave that vanished this close before the door still hit it
		constexpr float kForget = 10.0f;                    // seconds a seen projectile is remembered

		std::unordered_map<const RE::MagicItem*, int> spells;  // Unrelenting Force spell -> words

		struct Wave
		{
			RE::ProjectileHandle projectile;
			RE::ObjectRefHandle  door;
			float                along{ 0.0f };     // how far the door is from the shout's origin
			float                speed{ kDefaultSpeed };
			float                elapsed{ 0.0f };
			float                lastSeen{ 0.0f };  // distance travelled when the projectile was last there
			int                  words{ 1 };
		};
		std::vector<Wave>                             waves;
		std::unordered_map<std::uint64_t, float>      seen;  // projectile (handle and address) -> seconds since seen first

		// nothing solid between the shout and the door: one of three lines to the panel gets there (actors don't block)
		bool Reaches(RE::PlayerCharacter* a_player, const NiPoint3& a_from, RE::TESObjectREFR* a_door, const Doors::Shape& a_shape)
		{
			for (const float side : { 0.0f, -0.5f, 0.5f }) {
				const NiPoint3 to = a_shape.center + a_shape.width * (a_shape.halfWidth * side);
				const float    length = Length(to - a_from);
				NiPoint3       from = a_from;
				for (int tries = 0; tries < 3; ++tries) {
					const auto ray = ::Ray::Cast(a_player, from, to);
					const float travelled = Length(from - a_from) + ray.distance;
					if (!ray.hit || ray.ref == a_door || travelled >= length - 30.0f) {
						return true;
					}
					if (!ray.ref || !ray.ref->Is(RE::FormType::ActorCharacter)) {
						break;  // a wall, another door...
					}
					from = from + Normalized(to - from) * (ray.distance + 40.0f);  // past the actor in the way
				}
			}
			return false;
		}

		NiPoint3 Direction(RE::Projectile* a_projectile, float& a_speed)
		{
			const auto& velocity = a_projectile->GetProjectileRuntimeData().linearVelocity;
			const float speed = Length(velocity);
			if (speed > 100.0f) {
				a_speed = speed;
				return velocity * (1.0f / speed);
			}
			a_speed = kDefaultSpeed;
			const auto& angle = a_projectile->GetAngle();
			return { std::sin(angle.z) * std::cos(angle.x), std::cos(angle.z) * std::cos(angle.x), -std::sin(angle.x) };
		}

		// the first locked door in the cone that the wave can reach
		void Follow(RE::PlayerCharacter* a_player, RE::ConeProjectile* a_cone, RE::ProjectileHandle a_handle, int a_words)
		{
			const auto& runtime = a_cone->GetProjectileRuntimeData();
			const auto& cone = a_cone->GetConeRuntimeData();
			float       speed = kDefaultSpeed;
			const auto  direction = Direction(a_cone, speed);
			const auto  origin = Length(cone.origin) > 1.0f ? cone.origin : a_cone->GetPosition();
			const float tangent = cone.coneAngleTangent > 0.0f ? cone.coneAngleTangent : kDefaultTangent;
			const float radius = std::max(cone.initialCollisionSphereRadius, 0.0f);
			const float reach = std::min(runtime.range > 0.0f ? runtime.range : 1000.0f, Settings::Get().shoutDistance);

			struct Candidate
			{
				float              along;
				RE::TESObjectREFR* door;
				Doors::Shape       shape;
			};
			std::vector<Candidate> candidates;
			const bool             unlocked = Settings::Get().shoutUnlocked && Settings::Get().unlocked.enabled;
			if (auto* const tes = RE::TES::GetSingleton()) {
				tes->ForEachReferenceInRange(a_player, reach + 400.0f, [&](RE::TESObjectREFR* a_ref) {
					auto* const base = a_ref ? a_ref->GetBaseObject() : nullptr;
					if (base && base->Is(RE::FormType::Door) && a_ref->Is3DLoaded() && !a_ref->IsDisabled() && (unlocked || a_ref->IsLocked())) {
						if (const auto shape = Doors::GetShape(a_ref)) {
							const NiPoint3 offset = shape->center - origin;
							const float    along = Dot(offset, direction);
							const float    off = Length(offset - direction * along);
							if (along > 0.0f && along <= reach + shape->halfWidth && off <= radius + along * tangent + shape->halfWidth * 0.5f + 20.0f) {
								candidates.push_back({ along, a_ref, *shape });
							}
						}
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
			std::ranges::sort(candidates, {}, &Candidate::along);
			for (const auto& c : candidates) {
				if (Reaches(a_player, origin, c.door, c.shape)) {
					waves.push_back({ a_handle, c.door->GetHandle(), c.along, speed, 0.0f, 0.0f, a_words });
					if (Settings::Get().debugLog) {
						logs::info("Unrelenting Force ({} word(s)) heads for {} [{:08X}], {:.0f} units away", a_words, Forms::Name(c.door),
							c.door->GetFormID(), c.along);
					}
					return;
				}
			}
		}

		void Poll(RE::PlayerCharacter* a_player)
		{
			auto* const manager = RE::Projectile::Manager::GetSingleton();
			if (!manager) {
				return;
			}
			static std::vector<RE::ProjectileHandle> handles;  // kept between frames: no allocation every frame
			handles.clear();
			{
				RE::BSSpinLockGuard guard(manager->projectileLock);
				for (const auto* list : { &manager->unlimited, &manager->limited, &manager->pending }) {
					handles.insert(handles.end(), list->begin(), list->end());
				}
			}
			for (auto& handle : handles) {
				const auto projectile = handle.get();
				if (!projectile || projectile->GetFormType() != RE::FormType::ProjectileCone) {
					continue;
				}
				// handles are reused once a projectile is gone: the address tells two of them apart
				const auto key = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(projectile.get())) ^ handle.native_handle();
				if (!seen.emplace(key, 0.0f).second) {
					continue;
				}
				const auto& runtime = projectile->GetProjectileRuntimeData();
				if (runtime.shooter.get().get() != a_player) {
					continue;
				}
				if (const int words = Words(runtime.spell); words > 0) {
					Follow(a_player, static_cast<RE::ConeProjectile*>(projectile.get()), handle, words);
				}
			}
		}

		void Strike(RE::PlayerCharacter* a_player, const Wave& a_wave)
		{
			const auto door = a_wave.door.get();
			if (!door) {
				return;
			}
			Doors::Target target;
			const auto    verdict = Doors::Check(door.get(), target);
			if (verdict == Doors::Verdict::kNotLocked || verdict == Doors::Verdict::kNotLoaded || verdict == Doors::Verdict::kNotADoor ||
				verdict == Doors::Verdict::kOpen || verdict == Doors::Verdict::kBarred) {
				return;
			}
			if (target.level == Settings::kUnlocked) {
				if (verdict == Doors::Verdict::kOk && Settings::Get().shoutUnlocked) {
					Doors::Break(target, a_player->GetPosition(), Doors::Cause::kShout, false);
				}
				return;
			}
			const auto from = a_player->GetPosition();
			if (verdict != Doors::Verdict::kOk) {
				Doors::Hit(target, from, false);  // the shout slams into it, the lock holds
				if (Settings::Get().debugLog) {
					logs::info("Unrelenting Force hit {} [{:08X}]: {}", Forms::Name(door.get()), door->GetFormID(), Doors::Describe(verdict, target.level));
				}
				return;
			}
			const int needed = Settings::Get().locks[target.level].shoutWords;
			if (a_wave.words < needed) {
				Doors::Hit(target, from, false);
				// needed is 2 or 3 here (every shout has at least one word)
				Doors::Notify(Lang::F("The {} lock holds. It takes {} words of Unrelenting Force.", Lang::T(Doors::LevelName(target.level)), needed));
				return;
			}
			Doors::Break(target, from, Doors::Cause::kShout, false);
		}
	}

	void Init()
	{
		spells.clear();
		for (const auto& spec : Settings::GetLists().shouts) {
			auto* const form = Forms::Resolve(spec);
			auto* const shout = form ? form->As<RE::TESShout>() : nullptr;
			if (!shout) {
				logs::info("Shout '{}' not found", spec);
				continue;
			}
			for (int i = 0; i < RE::TESShout::VariationIDs::kTotal; ++i) {
				if (auto* const spell = shout->variations[i].spell) {
					spells.emplace(spell, i + 1);  // a spell two variations share counts as the fewer words
				}
			}
			logs::info("Unrelenting Force: {} [{:08X}]", Forms::Name(shout), shout->GetFormID());
		}
	}

	int Words(const RE::MagicItem* a_spell)
	{
		const auto it = a_spell ? spells.find(a_spell) : spells.end();
		return it != spells.end() ? it->second : 0;
	}

	void Update(float a_delta)
	{
		auto* const player = RE::PlayerCharacter::GetSingleton();
		if (!player || spells.empty()) {
			return;
		}
		if (!Settings::Get().shouting) {
			waves.clear();
			return;
		}
		Poll(player);

		for (auto it = waves.begin(); it != waves.end();) {
			it->elapsed += a_delta;
			const float travelled = it->elapsed * it->speed;
			const bool  alive = it->projectile.get() != nullptr;
			if (alive) {
				it->lastSeen = travelled;
			}
			if (travelled >= it->along) {
				if (alive || it->lastSeen >= it->along - kLateSlack) {
					Strike(player, *it);
				}
				it = waves.erase(it);
			} else if (!alive && it->lastSeen < it->along - kLateSlack) {
				it = waves.erase(it);  // the wave died out (or hit something) well before the door
			} else {
				++it;
			}
		}

		for (auto it = seen.begin(); it != seen.end();) {
			it->second += a_delta;
			it = it->second > kForget ? seen.erase(it) : std::next(it);
		}
	}

	void Reset()
	{
		waves.clear();
		seen.clear();
	}
}
