#pragma once

// The Kick lesser power (KickTheDoor.esp KTD_KickPower), for those out of keys or buttons: with [Kick] bKickPower on
// the player kicks only through it (the kick key and gamepad button do nothing) and has it in the magic menu (Powers).
// Equipped, the shout key kicks instead of casting it (the shout handler is switched off meanwhile, so no casting
// animation plays first); cast any other way (a hotkey mod) it kicks too. Off: the power is taken away again.
namespace Power
{
	void Init();  // after the data loaded: the power, the cast event
	void Sync();  // the player has the power if and only if the setting is on (a game loaded, the menu changed it)

	void OnKey(const RE::ButtonEvent* a_event);  // the shout key with the power equipped
	void Update();                               // every frame: the shout handler off while the power is equipped
	void Reset();
}
