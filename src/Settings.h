#pragma once

#include "Keys.h"

namespace Settings
{
	// the five pickable lock levels, in the game's order (LOCK_LEVEL 0..4)
	inline constexpr std::size_t kLevels = 5;
	inline constexpr std::array<const char*, kLevels> kLevelNames{ "Novice", "Apprentice", "Adept", "Expert", "Master" };

	// the "level" of a door that isn't locked
	inline constexpr int kUnlocked = -1;

	inline constexpr int kMaxKicks = 5;

	struct LockRule
	{
		bool  enabled{ true };       // kicks and Unrelenting Force may break locks of this level
		float staminaNeeded{ 0.0f };  // current stamina a kick needs to do anything
		float staminaCost{ 0.0f };    // stamina a kick that lands takes
		int   shoutWords{ 1 };        // words of Unrelenting Force that break it (1 Fus, 2 Fus Ro, 3 Fus Ro Dah)
		int   kicks{ 1 };             // kicks that must land before it gives (1 - kMaxKicks)
	};

	// a chest's lock, per level (Unrelenting Force never opens chests)
	struct ChestRule
	{
		bool  enabled{ true };       // kicks may break chest locks of this level
		float staminaNeeded{ 0.0f };  // current stamina a kick at the chest needs (with less there is no kick)
		float staminaCost{ 0.0f };    // stamina a kick that lands takes
		int   kicks{ 1 };             // kicks that must land before the lock gives (1 - kMaxKicks)
	};

	struct Values
	{
		// general
		bool  kicking{ true };
		bool  shouting{ true };
		bool  knockdown{ true };   // actors right behind a door that bursts open are knocked over
		float openSpeed{ 1.9f };   // forced doors swing open this much faster than normal
		bool  openAway{ true };    // forced doors swing away from the hit, whichever way they normally open

		// kicking
		int   kickKey{ 0x22 };          // G
		int   kickKey2{ Keys::kNone };  // set: hold kickKey and press this one (or both at once) to kick
		int   kickGamepadKey{ Keys::kNone };
		int   kickGamepadKey2{ Keys::kNone };  // set: hold kickGamepadKey and press this one (or both at once) to kick
		float holdTime{ 0.0f };         // how long the key is held before the kick starts (0: on press)
		float kickReach{ 170.0f };      // the farthest the player may stand from the door
		bool  enterLoadDoors{ true };   // a kicked load door takes you through right away
		bool  weaponDrawnOnly{ false }; // the kick key (and the Kick power) only does something with a weapon (or fists) out
		bool  kickPower{ false };       // kick only through the Kick lesser power in the magic menu (the keys don't kick)

		std::array<LockRule, kLevels> locks{ {
			{ true, 120.0f, 30.0f, 1, 1 },
			{ true, 140.0f, 40.0f, 1, 1 },
			{ true, 170.0f, 50.0f, 2, 1 },
			{ true, 200.0f, 60.0f, 3, 2 },
			{ true, 250.0f, 75.0f, 3, 2 },
		} };

		// doors that aren't locked: one kick (or any Unrelenting Force) throws them open
		LockRule unlocked{ true, 60.0f, 15.0f, 1, 1 };
		bool shoutUnlocked{ true };

		// kicking people and creatures (anything that isn't a door)
		bool  fighting{ true };              // the kick key kicks anywhere: whoever it hits is staggered
		float kickCost{ 40.0f };             // stamina such a kick takes
		float kickStagger{ 0.75f };          // stagger strength (the first word of Unrelenting Force: 0.75)
		bool  sprintKnockdown{ true };       // kicked while sprinting, people in light or no armor fall over
		float sprintKnockdownForce{ 4.0f };
		float sprintSlide{ 120.0f };         // kicked out of a sprint, the run carries the player this far (units) first
		bool  comboKicks{ true };            // the kick key works during melee swings and combos: the attack stops, the kick plays

		// chests: the kick key at a locked chest kicks its lock in (own lock table; the doors' crime and noise settings)
		bool chests{ true };
		bool openChests{ true };  // the forced chest opens (its inventory) as soon as the lock gives
		std::array<ChestRule, kLevels> chestLocks{ {
			{ true, 120.0f, 30.0f, 1 },
			{ true, 140.0f, 40.0f, 1 },
			{ true, 170.0f, 50.0f, 1 },
			{ true, 200.0f, 60.0f, 2 },
			{ true, 250.0f, 75.0f, 2 },
		} };

		// beast forms (no kicks in them): a werewolf's power attack breaks the door in front, so does a vampire lord's
		// power attack or a hostile spell they aim at a door
		bool  werewolfDoors{ true };
		bool  vampireLordDoors{ true };
		bool  vampireLordMagic{ true };
		float beastReach{ 230.0f };   // the farthest the door may be from the beast's body for a blow
		float beastMagicReach{ 1500.0f };

		// Unrelenting Force
		float shoutDistance{ 1500.0f };  // doors farther than this from the shout's origin are out of reach

		// doors
		bool  questDoors{ false };    // doors filled into quest aliases or held by quest scripts
		bool  crime{ true };          // breaking an owned lock in front of witnesses is trespassing
		bool  noise{ true };          // the crash is loud: actors nearby hear it
		float knockdownForce{ 4.0f };
		float minDoorHeight{ 100.0f };  // smaller "doors" (display cases, hatches, trap triggers) are left alone
		float barredDelay{ 0.5f };      // doors whose model animates longer than this (s) before it swings are left alone (0: none)

		// feedback
		bool messages{ true };
		bool panting{ true };  // a kick without the stamina for it: the player pants (the vanilla out of breath voice)
		bool debugLog{ false };

		std::string language{ "auto" };  // menu and message language: "auto" = the game's, or english, french...
	};

	// Form lists: "Plugin.esp|0x123456" (local form id) or an editor id
	struct Lists
	{
		std::vector<std::string> shouts;        // shouts whose spells count as Unrelenting Force
		std::vector<std::string> excludeDoors;  // door base forms or references never forced
		std::vector<std::string> includeDoors;  // door base forms or references always treated as doors (size check skipped)
		std::vector<std::string> sceneFactions; // the player in one of these factions is in a scene (OStim, SexLab...): no kicks
		std::vector<std::string> werewolfRaces;     // the player's race in werewolf form
		std::vector<std::string> vampireLordRaces;  // and as a vampire lord
	};

	void Load();
	void Save();

	Values&      Get();
	// the rule for a lock level, or for unlocked doors (kUnlocked)
	const LockRule& LockFor(int a_level);
	// kicks a door of this level takes
	inline int KicksNeeded(int a_level) { return std::clamp(LockFor(a_level).kicks, 1, kMaxKicks); }
	// the rule for a chest's lock level (0 Novice .. 4 Master)
	const ChestRule& ChestLockFor(int a_level);
	inline int       ChestKicksNeeded(int a_level) { return std::clamp(ChestLockFor(a_level).kicks, 1, kMaxKicks); }
	const Lists& GetLists();
	std::mutex&  Lock();  // around changes from the menu
}
