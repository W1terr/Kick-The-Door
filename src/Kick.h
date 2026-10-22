#pragma once

#include "Doors.h"

// Pressing the kick key. At a door: the player turns to it, steps in and kicks; at the impact stamina is checked and
// taken, Expert and Master locks need a second kick, the lock breaks and the door flies open. Anywhere else (Fight): the
// kick costs stamina up front and goes for whoever is in front, who is staggered (knocked down after a sprint).
// The animation (Clip) sets the timing.
namespace Kick
{
	// the kick key went down / is held / came up (input thread = main thread)
	void OnKey(const RE::ButtonEvent* a_event);

	void Update(float a_delta);  // every frame on the main thread while the game runs
	void ApplyPose();            // right after the player's skeleton was updated

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
