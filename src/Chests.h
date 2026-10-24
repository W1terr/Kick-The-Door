#pragma once

// Locked chests: containers with a pickable lock, low enough to kick the lock of (chests, strongboxes, Dwemer and ruin
// chests; not wardrobes or bookcases). Turned a little to the left, the player kicks the lock with the straight right leg: it breaks the way a picked lock opens,
// and the chest opens at once. Chests have their own lock table (stamina needed and taken, kicks per level; a weakened lock stays
// weakened, counted with the doors' kicks in the save); crime and noise follow the doors' settings. Unrelenting Force
// leaves chests alone.
namespace Chests
{
	enum class Verdict
	{
		kOk,
		kNotAChest,  // no container, or one too tall to kick open (wardrobes, bookcases)
		kNotLocked,
		kNeedsKey,
		kLevelOff,   // the lock level is switched off in the settings
		kBlocked,    // its activation is taken over by a script
		kNotLoaded,
	};

	struct Target
	{
		RE::TESObjectREFR* ref{ nullptr };
		int                level{ 0 };     // 0 Novice .. 4 Master
		RE::NiPoint3       center;         // world center of its bounds, on the floor
		RE::NiPoint3       front;          // horizontal unit vector out of the face the player forces it from
		float              halfDepth{ 0.0f };  // from the center to that face
		float              top{ 0.0f };        // world z of its top
	};

	void Init();  // after the data loaded: sounds

	// a_player: where the player stands (the chest is forced from the long side facing them)
	Verdict     Check(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_player, Target& a_out);
	std::string Describe(Verdict a_verdict, int a_level);  // in the menu language

	// where the player stands to kick it: a_standOff in front of its face, centered
	RE::NiPoint3 StandAt(const Target& a_target, float a_standOff);

	// unlocks it like a picked lock, with the crack of the lock giving
	void Break(const Target& a_target);
	// the lock held this kick: a thud, and it is heard
	void Hit(const Target& a_target);
	// opens it for the player (its inventory)
	void Open(RE::TESObjectREFR* a_chest);
}
