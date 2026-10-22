#pragma once

// Kick animations (Skyrim SE / 64 bit Havok packfiles, spline compressed like the game's own animations or uncompressed),
// read and played by the plugin itself on the player's 3rd person skeleton, so no behavior patch (Nemesis, Pandora, OAR)
// is needed. Annotations "Swing" and "Impact" set when the whoosh plays and when the foot meets the door.
//
// Animation packs: every folder in Data\Meshes\KickTheDoor\Animations is one pack, holding one or more .hkx files (one
// of them is picked at random for each kick) and an optional config.json (name, author, priority, conditions, timing;
// see ANIMATIONS.md). The pack with the highest priority whose conditions match the kick plays (equal priority: the one
// with more conditions); the mod's own kicks are the packs "Default" (priority 0, no conditions) and "Sprint" (priority
// 0, sprinting).
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
		bool sprinting{ false };
		RE::FormID race{ 0 };
	};

	// the animation picked for a kick (times in seconds from its start)
	struct Choice
	{
		int   animation{ -1 };
		float swing{ 0.0f };      // the whoosh
		float impact{ 0.0f };     // the foot meets the door
		float duration{ 0.0f };   // the animation's last frame
		float stepStart{ 0.0f };  // the player is moved towards the door from here
		float stepEnd{ 0.0f };    // to here
		float turnEnd{ 0.0f };    // facing the door by now
		float standOff{ 55.0f };  // the step stops this far from the door (units)
		float blendIn{ 0.08f };   // blends in over this from the start
		float blendOut{ 0.15f };  // after the last frame, back to what the game animates over this
		float endStep{ 0.0f };    // the player is moved this far forward while blending out (units)
		float maxStep{ 50.0f };   // the step towards the target goes this far at most (units)
		float lunge{ 0.0f };      // with nothing to aim at the player is moved this far forward (units)
		bool  keepArms{ true };   // with a weapon drawn the arms keep the weapon stance
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
