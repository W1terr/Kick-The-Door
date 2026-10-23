#pragma once

// Kicking people and creatures: who a kick goes for and what it does to them. A kick staggers whoever it hits (like
// the first word of Unrelenting Force); kicked while the player sprints, people in light or no armor fall over. The hit
// is a spell the player casts on them (KickTheDoor.esp), so aggression and assault work as with any attack.
namespace Fight
{
	void Init();  // after the data loaded: the kick spell, the hit sound

	// at the impact: who the foot meets. The kick doesn't aim, it goes where the player faces: the actor whose body is
	// nearest in front (within a_reach units of the player, horizontally, to the near side of their body; a little to
	// the side counts more against them; big creatures are wider and taller), in the kick's height band and with nothing
	// solid in between
	RE::Actor* Reached(RE::PlayerCharacter* a_player, float a_reach);

	// the foot lands on a_target
	void Hit(RE::PlayerCharacter* a_player, RE::Actor* a_target, bool a_sprinting);

	// the kick's damage (2 health, fixed) on a_target, dealt by the player: a kick, or a door the player forced throwing
	// someone over
	void Damage(RE::PlayerCharacter* a_player, RE::Actor* a_target);
}
