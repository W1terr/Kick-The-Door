#pragma once

// What a door's model animates before the door itself moves. Door replacers (Glorious Doors of Skyrim...) make their
// Open animation first turn locks and slide bars, and only then swing the panel. Read from the model's own .nif (loose
// or in a BSA): the time the panel starts to swing, so a forced door can skip what comes before.
namespace DoorModel
{
	struct Info
	{
		float       panelStart{ 0.0f };  // seconds into the Open sequence (0: the panel swings right away)
		std::string panel;               // the node that swings (the widest turn; for the log)
		std::vector<std::string> panels; // every node turning on a vertical hinge (both wings of a double door)
		float       length{ 0.0f };      // the Open sequence's length (s)
	};

	// cached per model; nullopt if the model has no readable Open sequence
	std::optional<Info> Get(RE::TESObjectREFR* a_door);

	// the parsing, on a .nif file's bytes (testable outside the game)
	std::optional<Info> Parse(std::span<const std::uint8_t> a_nif);
}
