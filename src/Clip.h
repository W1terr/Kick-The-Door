#pragma once

// The kick animation, Data\Meshes\KickTheDoor\kick.hkx (Skyrim SE / 64 bit Havok packfile, spline compressed like the
// game's own animations or uncompressed), read and played by the plugin itself on the player's 3rd person skeleton, so
// no behavior patch (Nemesis, Pandora, OAR) is needed. Its annotations set the timing: "Swing" (the whoosh), "Impact"
// (the foot meets the target), "StepStart" / "StepEnd" (when the player is moved to the door).
namespace Clip
{
	inline constexpr auto kPath = "Data/Meshes/KickTheDoor/kick.hkx";

	// how the kick is played (times in seconds into the animation)
	struct Choice
	{
		float swing{ 0.0f };        // the whoosh
		float impact{ 0.0f };       // the foot meets the target
		float duration{ 0.0f };     // the animation's last frame
		float stepStart{ 0.0f };    // the player is moved towards the door from here
		float stepEnd{ 0.0f };      // to here
		float standOff{ 55.0f };    // the step stops this far from the door (units)
		float blendIn{ 0.08f };     // blends in over this from the start
		float blendOut{ 0.15f };    // after the last frame, back to what the game animates over this
		float sprintStart{ 0.0f };  // out of a sprint the animation starts this far in
		float comboStart{ 0.0f };   // out of an attack or with a weapon out: starts this far in,
		float comboSpeed{ 1.0f };   // plays this much faster
		float comboEnd{ 0.0f };     // and ends here (0: its last frame), blending back from there
	};

	void Load();  // once, after the data loaded
	std::optional<Choice> Get();  // nullopt: no animation (missing or broken file, logged)

	// the kick a_time seconds in, a_weight (0..1) over what the game animated; a_keepArms leaves the arms to the game
	void Apply(RE::Actor* a_player, float a_time, float a_weight, bool a_keepArms);
	void Release();  // puts every bone back the way the game left it
}
