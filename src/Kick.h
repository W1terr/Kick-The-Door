#pragma once

#include "Doors.h"

// Holding the kick key at a locked door: the player turns to it, steps in and kicks. The kick lands 0.34 s in: stamina
// is checked and taken, Expert and Master locks need a second kick, the lock breaks and the door flies open.
namespace Kick
{
	// the kick key went down / is held / came up (input thread = main thread)
	void OnKey(const RE::ButtonEvent* a_event);

	void Update(float a_delta);  // every frame on the main thread while the game runs
	void ApplyPose();            // right after the player's skeleton was updated

	bool Active();
	void Reset();  // a game is loading: stop any kick right away

	// what a kick at this door would do now, for the status page
	struct Preview
	{
		Doors::Verdict verdict{ Doors::Verdict::kNotADoor };
		Doors::Target  target;
		float          distance{ 0.0f };
		float          stamina{ 0.0f };
		int            kicksDone{ 0 };
	};
	std::optional<Preview> Look();
}
