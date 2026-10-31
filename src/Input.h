#pragma once

// Key pairs for the kick: [Kick] iKey2 (keyboard / mouse) and iGamepadKey2 (gamepad) set: hold iKey / iGamepadKey and
// press the second one, or press both at once. The key that completes the pair is kept from the game until it is
// released (its own action doesn't happen: the player controls and the menu hotkeys never see it); the one held first
// does what it always does.
namespace Input
{
	void Install();  // the hooks on the player controls' and the menu controls' input
}
