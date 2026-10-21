#pragma once

// Kick animations (Skyrim SE / 64 bit Havok packfiles, spline compressed like the game's own animations or uncompressed),
// read and played by the plugin itself on the player's 3rd person skeleton, so no behavior patch (Nemesis, Pandora, OAR)
// is needed. Annotations "Swing" and "Impact" set when the whoosh plays and when the foot meets the door.
//
// Animation packs: every folder in Data\Meshes\KickTheDoor\Animations is one pack, holding one or more .hkx files (one
// of them is picked at random for each kick) and an optional config.json (name, author, priority, conditions, timing;
// see ANIMATIONS.md). The pack with the highest priority whose conditions match the kick plays; the mod's own kick is the
// pack "Default" (priority 0, no conditions).
namespace Clip
{
	inline constexpr auto kFolder = "Data/Meshes/KickTheDoor/Animations";

	// one kick: what the pack conditions are checked against
	struct Situation
	{
		bool female{ false };
		bool weaponDrawn{ false };
		bool sneaking{ false };
		bool lockedDoor{ false };
		RE::FormID race{ 0 };
	};

	// the animation picked for a kick
	struct Choice
	{
		int   animation{ -1 };
		float swing{ 0.0f };   // seconds
		float impact{ 0.0f };
		float duration{ 0.0f };
		bool  keepArms{ true };  // with a weapon drawn the arms keep the weapon stance
	};

	// a pack as the menu shows it
	struct PackInfo
	{
		std::string              folder;
		std::string              name;
		std::string              author;
		int                      priority{ 0 };
		std::vector<std::string> conditions;  // English words ("female", "weapon drawn"...), empty = always
		std::vector<std::string> races;       // as written in config.json
		int                      animations{ 0 };
		std::vector<std::string> errors;      // files that didn't load, a broken config.json
	};

	void Load();  // once, after the data loaded

	// a_pack: the pack's folder name, or "auto" (highest priority that matches)
	std::optional<Choice> Pick(const Situation& a_situation, std::string_view a_pack);
	const std::vector<PackInfo>& Packs();

	// animation a_animation a_time seconds in, a_weight (0..1) over what the game animated; a_keepArms leaves the arms
	// to the game
	void Apply(RE::Actor* a_player, int a_animation, float a_time, float a_weight, bool a_keepArms);
	void Release();  // puts every bone back the way the game left it
}
