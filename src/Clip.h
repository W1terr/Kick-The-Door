#pragma once

// The kick animation file, Data\Meshes\KickTheDoor\kick.hkx (Skyrim SE / 64 bit Havok packfile, spline compressed like
// the game's own animations or uncompressed). It is read and played by the plugin itself, on the player's 3rd person
// skeleton, so no behavior patch (Nemesis, Pandora, OAR) is needed. Annotations "Swing" and "Impact" set when the
// whoosh plays and when the foot meets the door.
namespace Clip
{
	struct Info
	{
		bool        loaded{ false };
		std::string error;          // why it didn't load
		std::string format;         // "spline compressed" / "uncompressed"
		float       duration{ 0.0f };
		int         frames{ 0 };
		int         tracks{ 0 };
		int         bones{ 0 };     // tracks that found a bone name
		float       swing{ 0.0f };
		float       impact{ 0.0f };
		bool        swingMarked{ false };
		bool        impactMarked{ false };
	};

	inline constexpr auto kPath = "Data/Meshes/KickTheDoor/kick.hkx";

	void        Load();  // once, after the data loaded
	const Info& GetInfo();
	bool        Ready();

	// the clip a_time seconds in, a_weight (0..1) over what the game animated; a_keepArms leaves the arms to the game
	void Apply(RE::Actor* a_player, float a_time, float a_weight, bool a_keepArms);
	void Release();  // puts every bone back the way the game left it
}
