#pragma once

#include "Keys.h"

namespace Settings
{
	// the five pickable lock levels, in the game's order (LOCK_LEVEL 0..4)
	inline constexpr std::size_t kLevels = 5;
	inline constexpr std::array<const char*, kLevels> kLevelNames{ "Novice", "Apprentice", "Adept", "Expert", "Master" };

	// Expert and Master locks always take two kicks, the others one
	inline constexpr std::array<int, kLevels> kKicksNeeded{ 1, 1, 1, 2, 2 };

	// the "level" of a door that isn't locked
	inline constexpr int kUnlocked = -1;

	inline int KicksNeeded(int a_level) { return a_level >= 0 && a_level < static_cast<int>(kLevels) ? kKicksNeeded[a_level] : 1; }

	struct LockRule
	{
		bool  enabled{ true };       // kicks and Unrelenting Force may break locks of this level
		float staminaNeeded{ 0.0f };  // current stamina a kick needs to do anything
		float staminaCost{ 0.0f };    // stamina a kick that lands takes
		int   shoutWords{ 1 };        // words of Unrelenting Force that break it (1 Fus, 2 Fus Ro, 3 Fus Ro Dah)
	};

	struct Values
	{
		// general
		bool  kicking{ true };
		bool  shouting{ true };
		bool  knockdown{ true };   // actors right behind a door that bursts open are knocked over
		float openSpeed{ 1.5f };   // forced doors swing open this much faster than normal
		bool  openAway{ true };    // forced doors swing away from the hit, whichever way they normally open

		// kicking
		int   kickKey{ 0x22 };          // G
		int   kickGamepadKey{ Keys::kNone };
		float holdTime{ 0.3f };         // how long the key is held before the kick starts
		float kickReach{ 170.0f };      // the farthest the player may stand from the door
		int   firstPerson{ 1 };         // 0: the kick stays in 1st person, 1: the camera goes to 3rd person for it
		bool  enterLoadDoors{ true };   // a kicked load door takes you through right away

		std::array<LockRule, kLevels> locks{ {
			{ true, 120.0f, 30.0f, 1 },
			{ true, 140.0f, 40.0f, 1 },
			{ true, 170.0f, 50.0f, 2 },
			{ true, 200.0f, 60.0f, 3 },
			{ true, 250.0f, 75.0f, 3 },
		} };

		// doors that aren't locked: one kick (or any Unrelenting Force) throws them open
		LockRule unlocked{ true, 60.0f, 15.0f, 1 };
		bool shoutUnlocked{ true };

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
		bool debugLog{ false };

		std::string language{ "auto" };  // menu and message language: "auto" = the game's, or english, french...
	};

	// Form lists: "Plugin.esp|0x123456" (local form id) or an editor id
	struct Lists
	{
		std::vector<std::string> shouts;        // shouts whose spells count as Unrelenting Force
		std::vector<std::string> excludeDoors;  // door base forms or references never forced
		std::vector<std::string> includeDoors;  // door base forms or references always treated as doors (size check skipped)
	};

	void Load();
	void Save();

	Values&      Get();
	// the rule for a lock level, or for unlocked doors (kUnlocked)
	const LockRule& LockFor(int a_level);
	const Lists& GetLists();
	std::mutex&  Lock();  // around changes from the menu
}
