// SKSE Menu Framework 3 API (https://github.com/QTR-Modding/SKSE-Menu-Framework-3-API, LGPL-2.1).
// It pulls in windows.h, so only this file includes it.
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#pragma warning(push, 0)
#include "SKSEMenuFramework.h"
#pragma warning(pop)
#undef GetObject
#undef PlaySound

#include "Menu.h"

#include "Clip.h"
#include "Doors.h"
#include "Forms.h"
#include "Keys.h"
#include "Kick.h"
#include "Lang.h"
#include "Settings.h"

namespace Menu
{
	namespace
	{
		using namespace ImGuiMCP;
		using Lang::F;
		using Lang::T;

		constexpr auto kSection = "Kick the Door";

		const ImVec4 kGood{ 0.55f, 0.85f, 0.55f, 1.0f };
		const ImVec4 kWarn{ 1.0f, 0.65f, 0.35f, 1.0f };
		const ImVec4 kBad{ 1.0f, 0.45f, 0.4f, 1.0f };

		int*              capturing{ nullptr };  // the key setting waiting for a key press
		bool              captureGamepad{ false };
		std::atomic<bool> saveNow{ false };      // a captured key: written with the next frame

		// translated label with a fixed ImGui id (it must not change with the language)
		std::string Id(const char* a_english, const char* a_suffix = "")
		{
			return std::format("{}###{}{}", T(a_english), a_english, a_suffix);
		}

		void Hint(const char* a_english)
		{
			SameLine();
			TextDisabled("(?)");
			if (IsItemHovered()) {
				SetTooltip("%s", T(a_english));
			}
		}

		void Heading(const char* a_english)
		{
			SeparatorText(T(a_english));
		}

		void KeyButton(const char* a_id, int& a_code, bool a_gamepad)
		{
			const bool waiting = capturing == &a_code;
			const auto name = waiting ? std::string(T("Press a key...")) : a_code == Keys::kNone ? std::string(T("None")) : Keys::Name(a_code);
			if (Button(std::format("{}###{}", name, a_id).c_str(), ImVec2(150.0f, 0.0f))) {
				capturing = waiting ? nullptr : &a_code;
				captureGamepad = a_gamepad;
			}
			SameLine();
			if (Button(Id("Clear", a_id).c_str())) {
				a_code = Keys::kNone;
				capturing = nullptr;
			}
		}

		const char* Level(int a_level) { return T(Doors::LevelName(a_level)); }

		bool SameName(std::string_view a, std::string_view b)
		{
			return std::ranges::equal(a, b, [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
		}

		void AnimationCombo(std::string& a_pack)
		{
			const auto& packs = Clip::Packs();
			std::string preview = T("Automatic");
			for (const auto& pack : packs) {
				if (SameName(pack.folder, a_pack)) {
					preview = pack.name;
				}
			}
			if (BeginCombo(Id("Kick animation").c_str(), preview.c_str())) {
				if (Selectable(Id("Automatic").c_str(), a_pack == "auto")) {
					a_pack = "auto";
				}
				for (const auto& pack : packs) {
					if (pack.animations == 0) {
						continue;
					}
					if (Selectable(std::format("{}###pack{}", pack.name, pack.folder).c_str(), SameName(pack.folder, a_pack))) {
						a_pack = pack.folder;
					}
				}
				EndCombo();
			}
		}

		void PackList()
		{
			const auto& packs = Clip::Packs();
			if (packs.empty()) {
				TextColored(kBad, "%s", T("No animation packs found: the kick plays without an animation."));
				return;
			}
			for (const auto& pack : packs) {
				Bullet();
				TextColored(pack.animations > 0 ? kGood : kBad, "%s", pack.name.c_str());
				SameLine();
				TextDisabled("%s", F("({}, priority {}, {} animation(s))", pack.folder, pack.priority, pack.animations).c_str());
				Indent();
				if (!pack.author.empty()) {
					Text("%s", F("by {}", pack.author).c_str());
				}
				std::string when;
				for (const auto& condition : pack.conditions) {
					when += std::format("{}{}", when.empty() ? "" : ", ", T(condition.c_str()));
				}
				for (const auto& race : pack.races) {
					when += std::format("{}{}", when.empty() ? "" : ", ", race);
				}
				Text("%s", when.empty() ? T("Plays for every kick") : F("Plays when: {}", when).c_str());
				for (const auto& error : pack.errors) {
					TextColored(kWarn, "%s", error.c_str());
				}
				Unindent();
			}
		}

		void __stdcall RenderSettings()
		{
			std::scoped_lock lock(Settings::Lock());
			auto&            s = Settings::Get();

			Heading("General");
			Checkbox(Id("Kick doors open").c_str(), &s.kicking);
			Hint("Look at a door and hold the kick key. Lockpicking still works as always.");
			Checkbox(Id("Unrelenting Force breaks doors").c_str(), &s.shouting);
			Hint("The first door the shout's wave reaches gets the hit. How many words each lock needs is set below.");
			Checkbox(Id("Knock down whoever stands behind the door").c_str(), &s.knockdown);
			Hint("Actors right behind the door are knocked over (or staggered, if they can't fall) when it bursts open. Nobody is hurt.");
			SliderFloat(Id("Door swing speed").c_str(), &s.openSpeed, 1.0f, 3.0f, "x %.2f");
			Hint("How much faster than normal a kicked or shouted door swings open. Load doors take you through without a swing.");
			Checkbox(Id("Doors swing away from the hit").c_str(), &s.openAway);
			Hint("A kicked or shouted door swings away from you, whichever way it normally opens. It also closes from that side; once it is closed it opens its usual way again.");

			Heading("Kicking");
			Text("%s", T("Kick key"));
			SameLine(190.0f);
			KeyButton("kb", s.kickKey, false);
			Text("%s", T("Gamepad button"));
			SameLine(190.0f);
			KeyButton("pad", s.kickGamepadKey, true);
			SliderFloat(Id("Hold for").c_str(), &s.holdTime, 0.0f, 1.0f, "%.2f s");
			Hint("How long the key is held before the kick starts (0: on press).");
			SliderFloat(Id("Reach").c_str(), &s.kickReach, 100.0f, 250.0f, T("%.0f units"));
			Hint("The farthest you can stand from the door. If it is farther than the kick reaches, you step in first.");
			const char* views[] = { T("Stay in first person"), T("Third person for the kick") };
			Combo(Id("In first person").c_str(), &s.firstPerson, views, 2);
			Hint("Without a visible body in first person you won't see the kick. With Improved Camera the view stays in first person.");
			Checkbox(Id("Go through a kicked load door").c_str(), &s.enterLoadDoors);
			Hint("A load door whose lock you kick in takes you through once the kick is over. Off: it only stays unlocked.");
			AnimationCombo(s.animation);
			Hint("Animation packs are folders in Meshes\\KickTheDoor\\Animations. Automatic: the pack with the highest priority whose conditions fit the kick plays (with several animations in a pack, one at random). The Status page lists the packs found.");

			Heading("Locks");
			TextWrapped("%s", T("Expert and Master locks always take two kicks. A kick needs at least the stamina shown and takes the cost when it lands; with less, the door only rattles."));
			if (BeginTable("locks", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
				TableSetupColumn(T("Lock"));
				TableSetupColumn(T("Can be forced"));
				TableSetupColumn(T("Stamina needed"));
				TableSetupColumn(T("Stamina cost"));
				TableSetupColumn(T("Kicks"));
				TableSetupColumn(T("Shout words"));
				TableHeadersRow();
				for (std::size_t i = 0; i < Settings::kLevels; ++i) {
					auto& l = s.locks[i];
					TableNextRow();
					TableNextColumn();
					Text("%s", Level(static_cast<int>(i)));
					TableNextColumn();
					Checkbox(std::format("##on{}", i).c_str(), &l.enabled);
					BeginDisabled(!l.enabled);
					TableNextColumn();
					SetNextItemWidth(-1.0f);
					SliderFloat(std::format("##need{}", i).c_str(), &l.staminaNeeded, 0.0f, 500.0f, "%.0f");
					TableNextColumn();
					SetNextItemWidth(-1.0f);
					SliderFloat(std::format("##cost{}", i).c_str(), &l.staminaCost, 0.0f, 200.0f, "%.0f");
					TableNextColumn();
					Text("%d", Settings::kKicksNeeded[i]);
					TableNextColumn();
					SetNextItemWidth(-1.0f);
					const char* words[] = { "Fus", "Fus Ro", "Fus Ro Dah" };
					int         word = std::clamp(l.shoutWords, 1, 3) - 1;
					if (Combo(std::format("##words{}", i).c_str(), &word, words, 3)) {
						l.shoutWords = word + 1;
					}
					EndDisabled();
				}
				// doors that aren't locked
				auto& u = s.unlocked;
				TableNextRow();
				TableNextColumn();
				Text("%s", T("Unlocked door"));
				TableNextColumn();
				Checkbox("##onU", &u.enabled);
				if (IsItemHovered()) {
					SetTooltip("%s", T("Off: the kick key does nothing at unlocked doors."));
				}
				BeginDisabled(!u.enabled);
				TableNextColumn();
				SetNextItemWidth(-1.0f);
				SliderFloat("##needU", &u.staminaNeeded, 0.0f, 500.0f, "%.0f");
				TableNextColumn();
				SetNextItemWidth(-1.0f);
				SliderFloat("##costU", &u.staminaCost, 0.0f, 200.0f, "%.0f");
				TableNextColumn();
				Text("%d", 1);
				TableNextColumn();
				Checkbox(Id("Any", "shoutU").c_str(), &s.shoutUnlocked);
				if (IsItemHovered()) {
					SetTooltip("%s", T("Unrelenting Force (any number of words) throws unlocked doors open."));
				}
				EndDisabled();
				EndTable();
			}
			if (Button(Id("Default stamina and words").c_str())) {
				const Settings::Values defaults;
				for (std::size_t i = 0; i < Settings::kLevels; ++i) {
					s.locks[i].staminaNeeded = defaults.locks[i].staminaNeeded;
					s.locks[i].staminaCost = defaults.locks[i].staminaCost;
					s.locks[i].shoutWords = defaults.locks[i].shoutWords;
				}
				s.unlocked.staminaNeeded = defaults.unlocked.staminaNeeded;
				s.unlocked.staminaCost = defaults.unlocked.staminaCost;
			}

			Heading("Unrelenting Force");
			SliderFloat(Id("Reach", "shout").c_str(), &s.shoutDistance, 300.0f, 4000.0f, T("%.0f units"));
			Hint("Doors farther away than this (or than the shout itself carries) are out of reach. A load door hit by the shout only has its lock broken: you walk through yourself.");

			Heading("Doors");
			Checkbox(Id("Quest doors too").c_str(), &s.questDoors);
			Hint("Doors a quest holds (in a quest alias, or a locked door scripts keep track of) stay locked unless this is on. Doors that need a key never break.");
			Checkbox(Id("Breaking an owned lock is a crime").c_str(), &s.crime);
			Hint("Like picking it: trespassing if someone sees you.");
			Checkbox(Id("Others hear the crash").c_str(), &s.noise);
			SliderFloat(Id("Knockdown force").c_str(), &s.knockdownForce, 0.0f, 12.0f, "%.1f");
			Hint("How hard the door throws someone standing behind it (0: a stagger instead).");

			Heading("Feedback");
			Checkbox(Id("Messages").c_str(), &s.messages);
			Hint("Not enough stamina, the door gave a little, the lock needs a key...");
			Checkbox(Id("Log details (SKSE log)").c_str(), &s.debugLog);
		}

		void DoorStatus()
		{
			auto* const player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->Is3DLoaded()) {
				TextDisabled("%s", T("Load a game to see the status."));
				return;
			}
			const auto& s = Settings::Get();
			Heading("The door you look at");
			const auto preview = Kick::Look();
			if (!preview) {
				TextDisabled("%s", T("Look at a door (close enough to open it) before opening this menu."));
				return;
			}
			const auto& t = preview->target;
			Text("%s", Forms::Name(t.ref).c_str());
			SameLine();
			TextDisabled("[%08X]%s%s", t.ref->GetFormID(), t.teleport ? " " : "", t.teleport ? T("load door") : "");
			if (preview->verdict != Doors::Verdict::kOk) {
				TextColored(preview->verdict == Doors::Verdict::kNotLocked ? kGood : kBad, "%s", Doors::Describe(preview->verdict, t.level).c_str());
				return;
			}
			const auto& lock = Settings::LockFor(t.level);
			const bool  unlocked = t.level == Settings::kUnlocked;
			if (unlocked) {
				TextColored(kGood, "%s", T("Unlocked: one kick throws it open"));
			} else {
				TextColored(kGood, "%s", F("{} lock: can be forced", Level(t.level)).c_str());
			}
			const bool enough = preview->stamina >= lock.staminaNeeded;
			TextColored(enough ? kGood : kWarn, "%s", F("Stamina {:.0f} / {:.0f} needed (a kick takes {:.0f})", preview->stamina, lock.staminaNeeded, lock.staminaCost).c_str());
			Text("%s", F("Kicks {} / {}", preview->kicksDone, Settings::KicksNeeded(t.level)).c_str());
			if (unlocked) {
				Text("%s", T(s.shoutUnlocked ? "Unrelenting Force: any words" : "Unrelenting Force: off"));
			} else if (lock.shoutWords == 1) {
				Text("%s", T("Unrelenting Force: 1 word"));
			} else {
				Text("%s", F("Unrelenting Force: {} words", lock.shoutWords).c_str());
			}
			TextColored(preview->distance <= s.kickReach ? kGood : kWarn, "%s", F("{:.0f} units away (reach {:.0f})", preview->distance, s.kickReach).c_str());
		}

		void __stdcall RenderStatus()
		{
			DoorStatus();
			Heading("Kick animations");
			PackList();
		}

		void __stdcall RenderLanguages();

		// the sidebar pages: English name (the translation key), page, name it has in the framework now
		struct Page
		{
			const char*                              english;
			SKSEMenuFramework::Model::RenderFunction render;
			std::string                              shown;
		};
		std::array pages{
			Page{ "Settings", RenderSettings, {} },
			Page{ "Status", RenderStatus, {} },
			Page{ "Language", RenderLanguages, {} },
		};

		// exported by SKSE Menu Framework 3.8+, not in its header: renames a page at runtime
		using RenameSectionFunction = bool (*)(const char* a_path, const char* a_newName);

		RenameSectionFunction RenameSection()
		{
			static const auto func = SKSEMenuFramework::Model::Internal::GetFunction<RenameSectionFunction>("RenameSection");
			return func;
		}

		void RenamePages()
		{
			if (!RenameSection()) {
				return;
			}
			for (auto& page : pages) {
				const std::string wanted = T(page.english);
				if (wanted != page.shown && RenameSection()(std::format("{}/{}", kSection, page.shown).c_str(), wanted.c_str())) {
					page.shown = wanted;
				}
			}
		}

		void __stdcall RenderLanguages()
		{
			std::scoped_lock lock(Settings::Lock());
			auto&            s = Settings::Get();

			Heading("Language");
			bool changed = false;
			if (RadioButton(Id("Same as the game").c_str(), s.language == "auto")) {
				s.language = "auto";
				changed = true;
			}
			for (const auto& language : Lang::All()) {
				if (RadioButton(language.name, s.language == language.id)) {
					s.language = language.id;
					changed = true;
				}
			}
			if (changed) {
				Lang::Set(s.language);
				RenamePages();
			}
			Spacing();
			if (!RenameSection()) {
				TextWrapped("%s", T("The names of the pages on the left change after you restart the game."));
			}
			// without the right glyphs the framework draws "?" for every letter, so this is also shown in English
			if (const auto glyphs = Lang::Current().glyphs) {
				constexpr auto kGlyphHint = "If you see ? instead of letters: in SKSE Menu Framework open Options > Open Settings and set Character Glyphs to {}.";
				Spacing();
				TextWrapped("%s", F(kGlyphHint, glyphs).c_str());
				TextDisabled("%s", std::vformat(kGlyphHint, std::make_format_args(glyphs)).c_str());
			}
		}

		void __stdcall OnMenuEvent(SKSEMenuFramework::Model::EventType a_type)
		{
			if (a_type == SKSEMenuFramework::Model::EventType::kBeforeRender && saveNow.exchange(false)) {
				std::scoped_lock lock(Settings::Lock());
				Settings::Save();
			}
			if (a_type == SKSEMenuFramework::Model::EventType::kCloseMenu) {
				capturing = nullptr;
				std::scoped_lock lock(Settings::Lock());
				Settings::Save();
			}
		}

		// key capture for the key buttons; true = the game doesn't get this input
		bool __stdcall OnInput(RE::InputEvent* a_event)
		{
			if (!capturing) {
				return false;
			}
			for (auto* event = a_event; event; event = event->next) {
				const auto* button = event->AsButtonEvent();
				if (!button || !button->IsDown()) {
					continue;
				}
				const int code = Keys::Code(button);
				if (code == 0x01) {  // Esc
					capturing = nullptr;
					return true;
				}
				if (code == Keys::kNone || Keys::IsGamepad(code) != captureGamepad) {
					continue;
				}
				*capturing = code;
				capturing = nullptr;
				saveNow = true;
				return true;
			}
			return false;
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled() || !GetMenuFrameworkModule()) {
			logs::info("SKSE Menu Framework is not installed: settings only through the ini");
			return;
		}
		SKSEMenuFramework::SetSection(kSection);
		for (auto& page : pages) {
			page.shown = T(page.english);
			SKSEMenuFramework::AddSectionItem(page.shown, page.render);
		}
		SKSEMenuFramework::AddInputEvent(OnInput);
		SKSEMenuFramework::AddEvent(OnMenuEvent, 0.0f);
		logs::info("SKSE Menu Framework {} found, pages added", SKSEMenuFramework::GetMenuFrameworkVersion());
	}
}
