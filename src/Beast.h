#pragma once

// Beast forms don't kick (Kick: no kicks in a race that isn't playable). A werewolf breaks a door with a power attack
// instead: the door in front of it gives at the blow's hit. A vampire lord does the same with a power attack on the
// ground, or with a hostile spell aimed at the door (Vampiric Drain, Vampiric Grip...). One blow or spell breaks any
// lock the mod may force (the lock table's levels; doors that need a key, quest and barred doors still hold).
namespace Beast
{
	enum class Form
	{
		kNone,
		kWerewolf,
		kVampireLord,
	};

	void Init();  // after the data loaded: the races ([Beasts] WerewolfRaces / VampireLordRaces), the spell cast event
	Form Of(const RE::PlayerCharacter* a_player);

	void Update(float a_delta);  // every frame on the main thread: the blows, spells on their way to a door
	void Reset();
}
