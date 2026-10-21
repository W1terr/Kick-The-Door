// Runs DoorModel::Parse on .nif files: which parts swing on a vertical hinge and when the first one starts. Not part
// of the mod.
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
			std::string wings;
			for (const auto& p : info->panels) {
				wings += "'" + p + "' ";
			}
			std::printf("%-36s Open %.2f s, wings %s- first starts at %.2f s\n", name.c_str(), info->length, wings.c_str(), info->panelStart);
		} catch (const std::exception& e) {
			std::printf("%-36s error: %s\n", name.c_str(), e.what());
		}
		std::fflush(stdout);
	}
	return 0;
}
