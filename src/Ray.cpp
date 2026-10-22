#include "Ray.h"

#include "MathUtil.h"

namespace Ray
{
	Hit Cast(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to)
	{
		Hit         ray;
		auto* const cell = a_player->GetParentCell();
		auto* const world = cell ? cell->GetbhkWorld() : nullptr;
		if (!world) {
			return ray;
		}
		const float     scale = RE::bhkWorld::GetWorldScale();
		RE::bhkPickData pick{};
		pick.rayInput.from = RE::hkVector4(a_from.x * scale, a_from.y * scale, a_from.z * scale, 0.0f);
		pick.rayInput.to = RE::hkVector4(a_to.x * scale, a_to.y * scale, a_to.z * scale, 0.0f);
		RE::CFilter filter{};
		a_player->GetCollisionFilterInfo(filter);  // the player's own group: the ray passes through the player's body
		pick.rayInput.filterInfo.filter = (filter.filter & 0xFFFF0000) | static_cast<std::uint32_t>(RE::COL_LAYER::kLOS);
		if (world->PickObject(pick) && pick.rayOutput.HasHit()) {
			ray.hit = true;
			ray.distance = M::Length(a_to - a_from) * pick.rayOutput.hitFraction;
			ray.ref = pick.rayOutput.rootCollidable ? RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable) : nullptr;
		}
		return ray;
	}
}
