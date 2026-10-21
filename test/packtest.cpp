// Loads the kick animation packs from Data/Meshes/KickTheDoor/Animations under the working directory and prints what
// Clip::Pick chooses for a few kicks. Not part of the mod.
#include "Clip.h"

#include <cstdio>

int main()
{
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
		for (int i = 0; i < 8; ++i) {
			const auto c = Clip::Pick(a_s, a_pack);
			if (!c) {
				std::printf(" none");
				continue;
			}
			std::printf(" #%d(%.2f/%.2f/%.2f%s)", c->animation, c->swing, c->impact, c->duration, c->keepArms ? "" : " arms");
		}
		std::printf("\n");
	};
	show("plain", {}, "auto");
	show("female sneaking", { .female = true, .sneaking = true }, "auto");
	show("weapon drawn, locked", { .weaponDrawn = true, .lockedDoor = true }, "auto");
	show("forced default", { .weaponDrawn = true, .lockedDoor = true }, "default");
	show("unknown pack", {}, "nope");
	return 0;
}
