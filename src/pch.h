#pragma once

// windows.h ends up in here through the libraries; keep its macros from breaking std::min/max and RE:: names
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <mutex>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace logs = SKSE::log;
using namespace std::literals;

#undef GetObject
#undef PlaySound
#undef far
#undef near
