#pragma once

// Key codes as stored in the INI: keyboard = DirectInput scan code, mouse = 256 + button, gamepad = SKSE code 266-281
namespace Keys
{
	inline constexpr int kNone = -1;
	inline constexpr int kMouseBase = 256;
	inline constexpr int kGamepadFirst = 266;

	int         Code(const RE::ButtonEvent* a_event);  // kNone for keys without a code (mouse wheel, ...)
	bool        IsGamepad(int a_code);
	std::string Name(int a_code);
}
