#pragma once

// What a door's model turns in its Open animation, read from the model's own .nif (loose or in a BSA): every part
// and the axis it turns about. Which of them are the door's wings (turning on a vertical hinge) is only known in the
// game, where the model stands: some models lie on their side in the file and the reference stands them up. Door
// replacers (Glorious Doors of Skyrim...) first turn locks and slide bars, and only then swing the wings: when that
// starts tells a barred door.
namespace DoorModel
{
	struct Part
	{
		std::string  name;           // the animated node
		float        turn{ 0.0f };   // the largest turn from the first key (degrees)
		RE::NiPoint3 axis;           // about this axis, in the node's parent space
		float        start{ 0.0f };  // seconds into the Open sequence before it starts turning
	};

	struct Info
	{
		std::vector<Part> parts;           // parts turning more than kMinTurn
		float             length{ 0.0f };  // the Open sequence's length (s)
	};

	inline constexpr float kMinTurn = 20.0f;  // a wing turns at least this far (degrees)

	// cached per model; nullopt if the model has no readable Open sequence
	std::optional<Info> Get(RE::TESObjectREFR* a_door);

	// the parsing, on a .nif file's bytes (testable outside the game)
	std::optional<Info> Parse(std::span<const std::uint8_t> a_nif);
}
