#pragma once

// A line through the havok world as line of sight sees it (the player's own body doesn't block it).
namespace Ray
{
	struct Hit
	{
		bool               hit{ false };
		float              distance{ 0.0f };
		RE::TESObjectREFR* ref{ nullptr };
	};

	Hit Cast(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to);
}
