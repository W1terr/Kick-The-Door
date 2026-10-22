// Runs DoorModel::Parse on .nif files: which parts turn, about which axis (parent space) and from when. Not part of
// the mod.
#include "../src/DoorModel.h"

#include <cstdio>

int main(int argc, char** argv)
{
	for (int i = 1; i < argc; ++i) {
		const auto name = std::filesystem::path(argv[i]).filename().string();
		std::ifstream file(argv[i], std::ios::binary);
		std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
		try {
			const auto info = DoorModel::Parse(bytes);
			if (!info) {
				std::printf("%-36s no Open sequence\n", name.c_str());
				continue;
			}
			std::printf("%-36s Open %.2f s\n", name.c_str(), info->length);
			for (const auto& p : info->parts) {
				std::printf("    '%s' %.0f deg about (%.2f %.2f %.2f) from %.2f s\n", p.name.c_str(), p.turn, p.axis.x, p.axis.y, p.axis.z, p.start);
			}
		} catch (const std::exception& e) {
			std::printf("%-36s error: %s\n", name.c_str(), e.what());
		}
		std::fflush(stdout);
	}
	return 0;
}
