#pragma once

// The mod's animations, Data\Meshes\KickTheDoor\kick.hkx and chest.hkx (Skyrim SE / 64 bit Havok packfiles, spline
// compressed like the game's own animations or uncompressed), read and played by the plugin itself on the player's 3rd
// person skeleton, so no behavior patch (Nemesis, Pandora, OAR) is needed. Their annotations set the timing: "Swing" (the
// whoosh), "Impact" (the foot meets the target / the chest's lock breaks), "StepStart" / "StepEnd" (when the player is
// moved to the door or the chest), "Open" (the forced chest opens).
// In first person kick_1st.hkx and chest_1st.hkx play instead (made on the same skeleton, same length and markers): their
// legs on the 3rd person body, which is drawn for it squashed above the waist (only the hips and legs are seen), their
// arms on the 1st person skeleton.
namespace Clip
{
	enum class Anim
	{
		kKick,
		kChest,  // turned a little to the left, a straight-leg push kick into a chest's lock
	};
	// the 3rd person files, then their first person versions (Anim + kFirstPerson)
	inline constexpr std::size_t                kFirstPerson = 2;
	inline constexpr std::array<const char*, 4> kPaths{ "Data/Meshes/KickTheDoor/kick.hkx", "Data/Meshes/KickTheDoor/chest.hkx",
		"Data/Meshes/KickTheDoor/kick_1st.hkx", "Data/Meshes/KickTheDoor/chest_1st.hkx" };

	// how the kick is played (times in seconds into the animation)
	struct Choice
	{
		float swing{ 0.0f };        // the whoosh
		float impact{ 0.0f };       // the foot meets the target (the chest's lock breaks)
		float open{ 0.0f };         // the chest opens
		float duration{ 0.0f };     // the animation's last frame
		float stepStart{ 0.0f };    // the player is moved towards the door from here
		float stepEnd{ 0.0f };      // to here
		float standOff{ 55.0f };    // the step stops this far from the door / the chest's front (units)
		float blendIn{ 0.08f };     // blends in over this from the start
		float blendOut{ 0.15f };    // after the last frame, back to what the game animates over this
		float sprintStart{ 0.0f };  // out of a sprint the animation starts this far in
		float comboStart{ 0.0f };   // out of an attack or with a weapon out: starts this far in,
		float comboSpeed{ 1.0f };   // plays this much faster
		float comboEnd{ 0.0f };     // and ends here (0: its last frame), blending back from there
	};

	void Load();  // once, after the data loaded
	std::optional<Choice> Get(Anim a_anim);  // nullopt: no animation (missing or broken file, logged)

	enum class View
	{
		kThirdPerson,
		kFirstPerson,     // the first person file: the body's legs shown, its arms on the 1st person skeleton
		kImprovedCamera,  // first person with Improved Camera (it draws the body): the first person file's legs, the
		                  // 3rd person file's upper body
	};

	// the animation a_time seconds in, a_weight (0..1) over what the game animated; a_keepArms leaves the arms to the
	// game. Without a first person file the 3rd person one plays in every view.
	void Apply(RE::Actor* a_player, Anim a_anim, float a_time, float a_weight, bool a_keepArms, View a_view);
	void Release();  // puts every bone back the way the game left it (and hides the body again in first person)
}
