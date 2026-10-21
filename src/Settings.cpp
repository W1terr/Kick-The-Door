#include "Settings.h"

namespace Settings
{
	namespace
	{
		constexpr auto kPath = "Data/SKSE/Plugins/KickTheDoor.ini";

		Values     values;
		Lists      lists;
		std::mutex lock;

		std::string Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) {
				return {};
			}
			const auto last = a_text.find_last_not_of(" \t\r\n");
			return std::string(a_text.substr(first, last - first + 1));
		}

		std::string Lower(std::string a_text)
		{
			std::ranges::transform(a_text, a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_text;
		}

		struct Entry
		{
			std::string key;  // "section.key", lower case
			std::string value;
		};

		std::vector<Entry> Read(const std::filesystem::path& a_path)
		{
			std::vector<Entry> entries;
			std::ifstream      file(a_path);
			std::string        line;
			std::string        section;
			while (std::getline(file, line)) {
				line = Trim(line);
				if (line.empty() || line[0] == ';' || line[0] == '#') {
					continue;
				}
				if (line.front() == '[' && line.back() == ']') {
					section = Lower(Trim(std::string_view(line).substr(1, line.size() - 2)));
					continue;
				}
				const auto equals = line.find('=');
				if (equals == std::string::npos) {
					continue;
				}
				std::string value = Trim(std::string_view(line).substr(equals + 1));
				if (const auto comment = value.find(';'); comment != std::string::npos) {
					value = Trim(std::string_view(value).substr(0, comment));
				}
				entries.push_back({ section + "." + Lower(Trim(std::string_view(line).substr(0, equals))), std::move(value) });
			}
			return entries;
		}

		// One setting: where it lives in the ini, how to read it and how to write it back
		struct Field
		{
			const char*                                   section;
			const char*                                   key;
			std::function<void(const std::string&)>       read;
			std::function<std::string()>                  write;
		};

		std::string Format(float a_value)
		{
			auto text = std::format("{:.2f}", a_value);
			while (text.back() == '0') {
				text.pop_back();
			}
			if (text.back() == '.') {
				text.pop_back();
			}
			return text;
		}

		Field Bool(const char* a_section, const char* a_key, bool& a_value)
		{
			return { a_section, a_key,
				[&a_value](const std::string& a_text) {
					const auto v = Lower(a_text);
					a_value = v == "1" || v == "true" || v == "yes" || v == "on";
				},
				[&a_value] { return std::string(a_value ? "1" : "0"); } };
		}

		Field Float(const char* a_section, const char* a_key, float& a_value, float a_min, float a_max)
		{
			return { a_section, a_key,
				[&a_value, a_key, a_min, a_max](const std::string& a_text) {
					try {
						a_value = std::clamp(std::stof(a_text), a_min, a_max);
					} catch (...) {
						logs::warn("{}: '{}' is not a number", a_key, a_text);
					}
				},
				[&a_value] { return Format(a_value); } };
		}

		Field Int(const char* a_section, const char* a_key, int& a_value, int a_min, int a_max)
		{
			return { a_section, a_key,
				[&a_value, a_key, a_min, a_max](const std::string& a_text) {
					try {
						a_value = std::clamp(static_cast<int>(std::stol(a_text, nullptr, 0)), a_min, a_max);
					} catch (...) {
						logs::warn("{}: '{}' is not a number", a_key, a_text);
					}
				},
				[&a_value] { return std::to_string(a_value); } };
		}

		Field Text(const char* a_section, const char* a_key, std::string& a_value)
		{
			return { a_section, a_key, [&a_value](const std::string& a_text) { a_value = a_text.empty() ? "auto" : Lower(a_text); },
				[&a_value] { return a_value; } };
		}

		std::vector<Field>& Fields()
		{
			static std::vector<Field> fields = [] {
				auto&              v = values;
				std::vector<Field> f{
					Bool("General", "bKicking", v.kicking),
					Bool("General", "bUnrelentingForce", v.shouting),
					Bool("General", "bKnockdown", v.knockdown),
					Float("General", "fOpenSpeed", v.openSpeed, 1.0f, 3.0f),
					Bool("General", "bOpenAwayFromHit", v.openAway),
					Int("Kick", "iKey", v.kickKey, -1, 281),
					Int("Kick", "iGamepadKey", v.kickGamepadKey, -1, 281),
					Float("Kick", "fHoldTime", v.holdTime, 0.0f, 2.0f),
					Float("Kick", "fReach", v.kickReach, 80.0f, 300.0f),
					Int("Kick", "iFirstPerson", v.firstPerson, 0, 1),
					Bool("Kick", "bEnterLoadDoors", v.enterLoadDoors),
					Text("Kick", "sAnimation", v.animation),
					Float("UnrelentingForce", "fMaxDistance", v.shoutDistance, 200.0f, 5000.0f),
					Bool("UnrelentingForce", "bUnlockedDoors", v.shoutUnlocked),
					Bool("Locks", "bUnlocked", v.unlocked.enabled),
					Float("Locks", "fUnlockedStamina", v.unlocked.staminaNeeded, 0.0f, 1000.0f),
					Float("Locks", "fUnlockedStaminaCost", v.unlocked.staminaCost, 0.0f, 1000.0f),
					Bool("Doors", "bQuestDoors", v.questDoors),
					Bool("Doors", "bCrime", v.crime),
					Bool("Doors", "bNoise", v.noise),
					Float("Doors", "fKnockdownForce", v.knockdownForce, 0.0f, 20.0f),
					Float("Doors", "fMinDoorHeight", v.minDoorHeight, 0.0f, 400.0f),
					Float("Doors", "fBarredDelay", v.barredDelay, 0.0f, 10.0f),
					Bool("Feedback", "bMessages", v.messages),
					Bool("Feedback", "bDebugLog", v.debugLog),
					Text("General", "sLanguage", v.language),
				};
				// [Locks] bNovice, fNoviceStamina, fNoviceStaminaCost, iNoviceShoutWords, ...
				static std::array<std::array<std::string, 4>, kLevels> keys;
				for (std::size_t i = 0; i < kLevels; ++i) {
					const std::string name = kLevelNames[i];
					keys[i] = { "b" + name, "f" + name + "Stamina", "f" + name + "StaminaCost", "i" + name + "ShoutWords" };
					auto& l = v.locks[i];
					f.push_back(Bool("Locks", keys[i][0].c_str(), l.enabled));
					f.push_back(Float("Locks", keys[i][1].c_str(), l.staminaNeeded, 0.0f, 1000.0f));
					f.push_back(Float("Locks", keys[i][2].c_str(), l.staminaCost, 0.0f, 1000.0f));
					f.push_back(Int("Locks", keys[i][3].c_str(), l.shoutWords, 1, 3));
				}
				return f;
			}();
			return fields;
		}

		// comma separated, the same key again adds to the list
		void List(const Entry& a_entry, const char* a_key, std::vector<std::string>& a_list)
		{
			if (a_entry.key != a_key) {
				return;
			}
			std::string_view rest = a_entry.value;
			while (!rest.empty()) {
				const auto comma = rest.find(',');
				auto       item = Trim(rest.substr(0, comma));
				if (!item.empty() && std::ranges::find(a_list, item) == a_list.end()) {
					a_list.push_back(std::move(item));
				}
				if (comma == std::string_view::npos) {
					break;
				}
				rest.remove_prefix(comma + 1);
			}
		}
	}

	void Load()
	{
		values = {};
		lists = {};
		if (!std::filesystem::exists(kPath)) {
			logs::warn("{} is missing, using the defaults", kPath);
		}
		const auto entries = Read(kPath);
		for (const auto& e : entries) {
			for (auto& field : Fields()) {
				if (e.key == Lower(std::format("{}.{}", field.section, field.key))) {
					field.read(e.value);
				}
			}
			List(e, "unrelentingforce.shouts", lists.shouts);
			List(e, "doors.exclude", lists.excludeDoors);
			List(e, "doors.include", lists.includeDoors);
		}
		if (lists.shouts.empty()) {
			lists.shouts.emplace_back("Skyrim.esm|0x13E07");
		}
		logs::info("Settings: kicking {}, Unrelenting Force {}, knockdown {}, open speed x{}, key {} / {}, quest doors {}",
			values.kicking, values.shouting, values.knockdown, values.openSpeed, Keys::Name(values.kickKey), Keys::Name(values.kickGamepadKey),
			values.questDoors);
		logs::info("  Unlocked doors: kick {} (stamina {}, cost {}), Unrelenting Force {}", values.unlocked.enabled, values.unlocked.staminaNeeded,
			values.unlocked.staminaCost, values.shoutUnlocked);
		for (std::size_t i = 0; i < kLevels; ++i) {
			const auto& l = values.locks[i];
			logs::info("  {}: {}, stamina {} (cost {}), {} kick(s), {} word(s)", kLevelNames[i], l.enabled ? "on" : "off", l.staminaNeeded,
				l.staminaCost, kKicksNeeded[i], l.shoutWords);
		}
	}

	// Writes every setting back into the ini: known keys are replaced where they are (their comments stay), missing ones
	// go to the end of their section (a missing section is added). The lists are never touched.
	void Save()
	{
		std::vector<std::string> lines;
		{
			std::ifstream file(kPath);
			std::string   line;
			while (std::getline(file, line)) {
				lines.push_back(line);
			}
		}

		std::vector<const Field*> pending;
		for (const auto& field : Fields()) {
			pending.push_back(&field);
		}

		std::string                                  section;
		std::unordered_map<std::string, std::size_t> sectionEnd;  // lower case section -> line after its last entry
		for (std::size_t i = 0; i < lines.size(); ++i) {
			const auto trimmed = Trim(lines[i]);
			if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']') {
				section = Lower(Trim(std::string_view(trimmed).substr(1, trimmed.size() - 2)));
				sectionEnd[section] = i + 1;
				continue;
			}
			if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
				continue;
			}
			sectionEnd[section] = i + 1;
			const auto equals = trimmed.find('=');
			if (equals == std::string::npos) {
				continue;
			}
			const auto key = Lower(Trim(std::string_view(trimmed).substr(0, equals)));
			const auto it = std::ranges::find_if(pending, [&](const Field* a_field) {
				return Lower(a_field->section) == section && Lower(a_field->key) == key;
			});
			if (it == pending.end()) {
				continue;
			}
			std::string comment;
			if (const auto semicolon = trimmed.find(';', equals); semicolon != std::string::npos) {
				comment = "  " + trimmed.substr(semicolon);
			}
			lines[i] = std::format("{} = {}{}", (*it)->key, (*it)->write(), comment);
			pending.erase(it);
		}

		for (const auto* field : pending) {
			const auto name = Lower(field->section);
			auto       it = sectionEnd.find(name);
			if (it == sectionEnd.end()) {
				if (!lines.empty() && !Trim(lines.back()).empty()) {
					lines.emplace_back();
				}
				lines.push_back(std::format("[{}]", field->section));
				it = sectionEnd.emplace(name, lines.size()).first;
			}
			const auto at = it->second;
			lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), std::format("{} = {}", field->key, field->write()));
			for (auto& [other, end] : sectionEnd) {
				if (end >= at && other != name) {
					++end;
				}
			}
			++it->second;
		}

		std::ofstream file(kPath, std::ios::trunc);
		for (const auto& line : lines) {
			file << line << '\n';
		}
		if (!file) {
			logs::error("Couldn't write {}", kPath);
		}
	}

	Values& Get() { return values; }

	const LockRule& LockFor(int a_level)
	{
		return a_level >= 0 && a_level < static_cast<int>(kLevels) ? values.locks[a_level] : values.unlocked;
	}

	const Lists& GetLists() { return lists; }

	std::mutex& Lock() { return lock; }
}
