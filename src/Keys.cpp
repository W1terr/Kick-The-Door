#include "Keys.h"

namespace Keys
{
	namespace
	{
		// XInput button mask (gamepad idCode) -> SKSE code
		constexpr std::array<std::pair<std::uint32_t, int>, 16> kGamepad{ {
			{ 0x0001, 266 }, { 0x0002, 267 }, { 0x0004, 268 }, { 0x0008, 269 }, { 0x0010, 270 }, { 0x0020, 271 },
			{ 0x0040, 272 }, { 0x0080, 273 }, { 0x0100, 274 }, { 0x0200, 275 }, { 0x1000, 276 }, { 0x2000, 277 },
			{ 0x4000, 278 }, { 0x8000, 279 }, { 0x0009, 280 }, { 0x000A, 281 } } };

		constexpr std::array<std::pair<int, std::string_view>, 48> kNames{ {
			{ 0x01, "Esc" }, { 0x0C, "-" }, { 0x0D, "=" }, { 0x0E, "Backspace" }, { 0x0F, "Tab" }, { 0x1A, "[" }, { 0x1B, "]" },
			{ 0x1C, "Enter" }, { 0x1D, "Left Ctrl" }, { 0x27, ";" }, { 0x28, "'" }, { 0x29, "~" }, { 0x2A, "Left Shift" },
			{ 0x2B, "\\" }, { 0x33, "," }, { 0x34, "." }, { 0x35, "/" }, { 0x36, "Right Shift" }, { 0x38, "Left Alt" },
			{ 0x39, "Space" }, { 0x3A, "Caps Lock" }, { 0x57, "F11" }, { 0x58, "F12" }, { 0x9D, "Right Ctrl" }, { 0xB8, "Right Alt" },
			{ 0xC7, "Home" }, { 0xC8, "Up" }, { 0xC9, "Page Up" }, { 0xCB, "Left" }, { 0xCD, "Right" }, { 0xCF, "End" },
			{ 0xD0, "Down" }, { 0xD1, "Page Down" }, { 0xD2, "Insert" }, { 0xD3, "Delete" },
			{ 256, "Left Mouse" }, { 257, "Right Mouse" }, { 258, "Middle Mouse" }, { 259, "Mouse 4" }, { 260, "Mouse 5" },
			{ 270, "Start" }, { 271, "Back" }, { 272, "LS" }, { 273, "RS" }, { 274, "LB" }, { 275, "RB" }, { 280, "LT" }, { 281, "RT" } } };
	}

	int Code(const RE::ButtonEvent* a_event)
	{
		const auto id = a_event->GetIDCode();
		switch (a_event->GetDevice()) {
		case RE::INPUT_DEVICE::kKeyboard:
			return static_cast<int>(id);
		case RE::INPUT_DEVICE::kMouse:
			return id < RE::BSWin32MouseDevice::Key::kWheelUp ? kMouseBase + static_cast<int>(id) : kNone;
		case RE::INPUT_DEVICE::kGamepad:
			for (const auto& [mask, code] : kGamepad) {
				if (mask == id) {
					return code;
				}
			}
			return kNone;
		default:
			return kNone;
		}
	}

	bool IsGamepad(int a_code)
	{
		return a_code >= kGamepadFirst;
	}

	std::string Name(int a_code)
	{
		if (a_code == kNone) {
			return "None";
		}
		for (const auto& [code, name] : kNames) {
			if (code == a_code) {
				return std::string(name);
			}
		}
		static constexpr std::string_view kRows[] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
		static constexpr int              kRowStart[] = { 0x10, 0x1E, 0x2C };
		for (int row = 0; row < 3; ++row) {
			if (a_code >= kRowStart[row] && a_code < kRowStart[row] + static_cast<int>(kRows[row].size())) {
				return std::string(1, kRows[row][a_code - kRowStart[row]]);
			}
		}
		if (a_code >= 0x02 && a_code <= 0x0B) {
			return std::to_string((a_code - 1) % 10);
		}
		if (a_code >= 0x3B && a_code <= 0x44) {
			return std::format("F{}", a_code - 0x3A);
		}
		if (a_code >= 266 && a_code <= 269) {
			static constexpr std::string_view kPad[] = { "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right" };
			return std::string(kPad[a_code - 266]);
		}
		if (a_code >= 276 && a_code <= 279) {
			static constexpr std::string_view kFace[] = { "A", "B", "X", "Y" };
			return std::string(kFace[a_code - 276]);
		}
		return std::format("Key {}", a_code);
	}
}
