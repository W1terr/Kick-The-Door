// Under the working directory: reads, changes and writes back Data/SKSE/Plugins/KickTheDoor.ini, loads the kick animation
// packs from Data/Meshes/KickTheDoor/Animations and prints what Clip::Pick chooses for a few kicks. Not part of the mod.
#include "Clip.h"
#include "Settings.h"

#include <cstdio>

int main()
{
	Settings::Load();
	for (int l = -1; l < 5; ++l) {
		std::printf("level %d: kicks %d, stamina %.0f / %.0f\n", l, Settings::KicksNeeded(l), Settings::LockFor(l).staminaNeeded, Settings::LockFor(l).staminaCost);
	}
	Settings::Get().locks[3].kicks = 4;
	Settings::Save();
	Settings::Load();
	std::printf("after save: expert kicks %d, fight cost %.0f\n", Settings::KicksNeeded(3), Settings::Get().kickCost);
	Clip::Load();
	for (const auto& pack : Clip::Packs()) {
		std::printf("pack %s '%s' by '%s' priority %d, %d animation(s)\n", pack.folder.c_str(), pack.name.c_str(), pack.author.c_str(), pack.priority,
			pack.animations);
		for (const auto& c : pack.conditions) {
			std::printf("  when %s\n", c.c_str());
		}
		for (const auto& e : pack.errors) {
			std::printf("  problem: %s\n", e.c_str());
		}
	}
	const auto show = [](const char* a_what, const Clip::Situation& a_s, std::string_view a_pack) {
		std::printf("%s:", a_what);
		for (int i = 0; i < 2; ++i) {
			const auto c = Clip::Pick(a_s, a_pack);
			if (!c) {
				std::printf(" none");
				continue;
			}
			std::printf(" #%d(%.2f/%.2f/%.2f%s step %.2f-%.2f turn %.2f stand %.0f blend %.2f/%.2f end step %.0f max %.0f lunge %.0f)", c->animation, c->swing, c->impact,
				c->duration, c->keepArms ? "" : " arms", c->stepStart, c->stepEnd, c->turnEnd, c->standOff, c->blendIn, c->blendOut, c->endStep, c->maxStep, c->lunge);
		}
		std::printf("\n");
	};
	show("plain", {}, "auto");
	show("sprinting", { .sprinting = true }, "auto");
	show("female sneaking", { .female = true, .sneaking = true }, "auto");
	show("weapon drawn, locked", { .weaponDrawn = true, .lockedDoor = true }, "auto");
	show("forced default", { .weaponDrawn = true, .lockedDoor = true }, "default");
	show("unknown pack", {}, "nope");
	return 0;
}
