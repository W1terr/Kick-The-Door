#pragma once

// Which locked doors can be forced, and forcing them: unlocking the way a picked lock is unlocked, opening through the
// door's own activation, the faster swing, the knockdown behind it, and the kicks each door has taken.
namespace Doors
{
	enum class Verdict
	{
		kOk,
		kNotADoor,   // no door, or a "door" that isn't one to kick (display case, hatch, trap trigger, excluded)
		kNotLocked,  // not locked, and kicking unlocked doors is off
		kOpen,       // already open
		kNeedsKey,
		kLevelOff,   // the lock level is switched off in the settings
		kQuest,      // held by a quest
		kBlocked,    // its activation is taken over by a script
		kBarred,     // its model unlocks and unbars itself for a long while before it swings (door replacers)
		kNotLoaded,
	};

	// the door panel as seen from above: a box standing on the floor
	struct Shape
	{
		RE::NiPoint3 center;           // world center of the bounds
		RE::NiPoint3 width;            // horizontal unit vector along the panel
		RE::NiPoint3 normal;           // horizontal unit vector through the panel
		float        halfWidth{ 0.0f };
		float        halfThickness{ 0.0f };
		float        bottom{ 0.0f };
		float        top{ 0.0f };
		bool         upright{ false };
	};

	struct Target
	{
		RE::TESObjectREFR* ref{ nullptr };
		int                level{ -1 };  // 0 Novice .. 4 Master, Settings::kUnlocked
		bool               teleport{ false };
		Shape              shape;
	};

	enum class Cause
	{
		kKick,
		kShout,
		kBeast,  // a werewolf's or vampire lord's blow or spell
	};

	void Init();  // after the data loaded: sounds, include / exclude lists

	std::optional<Shape> GetShape(RE::TESObjectREFR* a_ref);
	Verdict              Check(RE::TESObjectREFR* a_ref, Target& a_out);
	std::string          Describe(Verdict a_verdict, int a_level);  // in the menu language
	const char*          LevelName(int a_level);                    // English (the translation key)

	// horizontal distance from a point to the door panel
	float Distance(const Shape& a_shape, const RE::NiPoint3& a_point);
	// a_shape.normal turned towards a_point
	RE::NiPoint3 NormalTowards(const Shape& a_shape, const RE::NiPoint3& a_point);

	// unlocks it like a picked lock (if it was locked), plays the crash, opens it (load doors only when a_enter) and knocks
	// over who stands right behind it; a_from = where the force came from (the player)
	void Break(const Target& a_target, const RE::NiPoint3& a_from, Cause a_cause, bool a_enter);
	// the lock held: a thud and a rattle; a_weakened = the first of two kicks landed
	void Hit(const Target& a_target, const RE::NiPoint3& a_from, bool a_weakened);
	// goes through a load door that is already unlocked
	void Enter(RE::TESObjectREFR* a_door);

	// breaking an owned lock (door or chest): trespassing if someone sees it (Settings crime)
	void Crime(RE::TESObjectREFR* a_ref);
	// the crash is heard around a_at (Settings noise)
	void Noise(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_at);

	void PlaySwing(const RE::NiPoint3& a_at);
	// a lock (door or chest) giving way: a metal hit (KickTheDoor.esp KTD_LockBreakSD, the vanilla lockpicking clicks without it)
	void PlayLockBreak(const RE::NiPoint3& a_at);
	// a line in the top left corner (if messages are on)
	void Notify(const std::string& a_text);

	// kicks that landed on a door (or a chest's lock) that hasn't given yet (saved with the game)
	int  Kicks(RE::FormID a_door);
	void SetKicks(RE::FormID a_door, int a_kicks);

	void Update(float a_delta);  // door swing speeds, wobbles, cleanup
	// a door animation controller just wrote this frame's pose (any thread): forced doors swing away from the hit
	void OnControllerUpdate(RE::NiObjectNET* a_target, int a_kind);  // a_kind: 0 manager, 1 multi-target, 2 transform
	void Reset();                // a game is being loaded: put every door back to normal

	void Save(SKSE::SerializationInterface* a_intfc);
	void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length);
	void Revert();
}
