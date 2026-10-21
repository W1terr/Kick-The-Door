#pragma once

// Unrelenting Force against a locked door. Every cone projectile the player's Unrelenting Force launches is followed:
// at launch the first locked door inside its cone that it can reach (in range, nothing solid in between) becomes its
// target, and when the wave has travelled that far the door gets the hit. One door per shout, once.
namespace Shout
{
	void Init();  // after the data loaded: which spells are Unrelenting Force, and how many words each is
	void Update(float a_delta);
	void Reset();

	// words of Unrelenting Force this spell is (0: not Unrelenting Force)
	int Words(const RE::MagicItem* a_spell);
}
