// SKSE Menu Framework 3 API (https://github.com/QTR-Modding/SKSE-Menu-Framework-3-API, LGPL-2.1).
// It pulls in windows.h, so only this file includes it.
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#pragma warning(push, 0)
#include "SKSEMenuFramework.h"
#pragma warning(pop)
#undef GetObject
#undef PlaySound

#include "Menu.h"

#include "Doors.h"
#include "Forms.h"
#include "Keys.h"
#include "Kick.h"
#include "Lang.h"
#include "Power.h"
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

		void __stdcall RenderSettings()
		{
			std::scoped_lock lock(Settings::Lock());
			auto&            s = Settings::Get();

			Heading("General");
			Checkbox(Id("Kick doors open").c_str(), &s.kicking);
			Hint("Look at a door and press the kick key. Lockpicking still works as always.");
			Checkbox(Id("Unrelenting Force breaks doors").c_str(), &s.shouting);
			Hint("The first door the shout's wave reaches gets the hit. How many words each lock needs is set below.");
			Checkbox(Id("Knock down whoever stands behind the door").c_str(), &s.knockdown);
			Hint("Actors right behind the door are knocked over (or staggered, if they can't fall) when it bursts open. Nobody is hurt.");
			SliderFloat(Id("Door swing speed").c_str(), &s.openSpeed, 1.0f, 3.0f, "x %.2f");
			Hint("How much faster than normal a kicked or shouted door swings open. Load doors take you through without a swing.");
			Checkbox(Id("Doors swing away from the hit").c_str(), &s.openAway);
			Hint("A kicked or shouted door swings away from you, whichever way it normally opens. It also closes from that side; once it is closed it opens its usual way again.");

			Heading("Kicking");
			if (Checkbox(Id("Kick only as a lesser power").c_str(), &s.kickPower)) {
				SKSE::GetTaskInterface()->AddTask([] { Power::Sync(); });
			}
			Hint("Adds Kick to your Powers, and only the power kicks (the keys below do nothing). Equip it and press the shout key to kick (no casting animation), or put it on a hotkey or favorites mod: handy on a gamepad with no button left. Turned off, the power is taken away again.");
			BeginDisabled(s.kickPower);
			Text("%s", T("Kick key"));
			SameLine(190.0f);
			KeyButton("kb", s.kickKey, false);
			Text("%s", T("Together with"));
			SameLine(190.0f);
			KeyButton("kb2", s.kickKey2, false);
			Hint("A second key: hold the kick key and press this one (or press both at once) to kick, for example Left Shift + G. The key that completes the pair does nothing else meanwhile; the one you hold first keeps its own action. None: the kick key kicks on its own.");
			Text("%s", T("Gamepad button"));
			SameLine(190.0f);
			KeyButton("pad", s.kickGamepadKey, true);
			Text("%s", T("Together with"));
			SameLine(190.0f);
			KeyButton("pad2", s.kickGamepadKey2, true);
			Hint("A second gamepad button: hold the first one and press this one (or press both at once) to kick. The button that completes the pair does nothing else meanwhile; the one you hold first keeps its own action, so pick one whose action doesn't matter (or helps). None: the first button kicks on its own.");
			EndDisabled();
			SliderFloat(Id("Hold for").c_str(), &s.holdTime, 0.0f, 1.0f, "%.2f s");
			Hint("How long the key is held before the kick starts (0: on press).");
			SliderFloat(Id("Reach").c_str(), &s.kickReach, 100.0f, 250.0f, T("%.0f units"));
			Hint("The farthest you can stand from the door. If it is farther than the kick reaches, you step in first.");
			Checkbox(Id("Go through a kicked load door").c_str(), &s.enterLoadDoors);
			Hint("A load door whose lock you kick in takes you through once the kick is over. Off: it only stays unlocked.");
			Checkbox(Id("Only with a weapon drawn").c_str(), &s.weaponDrawnOnly);
			Hint("The kick key (and the Kick power) only kicks while your weapon or fists are out. Sheathed, the key is free for other mods.");

			Heading("Kicking people");
			Checkbox(Id("Kick people and creatures").c_str(), &s.fighting);
			Hint("Press the kick key anywhere but at a door: the kick goes for whoever is in front of you and staggers them, like the first word of Unrelenting Force. They take it as an attack.");
			SliderFloat(Id("Stamina cost", "fight").c_str(), &s.kickCost, 0.0f, 150.0f, "%.0f");
			Hint("What such a kick takes. With less stamina you can't kick.");
			SliderFloat(Id("Stagger strength").c_str(), &s.kickStagger, 0.25f, 1.5f, "%.2f");
			Hint("The first word of Unrelenting Force staggers with 0.75.");
			Checkbox(Id("Sprint kicks knock people down").c_str(), &s.sprintKnockdown);
			Hint("Kicked while you sprint, people in light armor or none fall over. Heavy armor, big creatures and those who can't fall are only staggered.");
			BeginDisabled(!s.sprintKnockdown);
			SliderFloat(Id("Knockdown force", "fight").c_str(), &s.sprintKnockdownForce, 0.0f, 12.0f, "%.1f");
			EndDisabled();
			SliderFloat(Id("Sprint slide").c_str(), &s.sprintSlide, 0.0f, 200.0f, T("%.0f units"));
			Hint("Kicking out of a sprint, the run carries you on from your running speed, slowing down evenly until a little after the hit: at most this far (0: you stop dead).");
			Checkbox(Id("Kick during attacks").c_str(), &s.comboKicks);
			Hint("The kick key works in the middle of a swing or a combo: the attack stops and a shorter, quicker kick plays. After the foot lands, attacking again goes straight on with the combo.");

			Heading("Chests");
			Checkbox(Id("Force chest locks").c_str(), &s.chests);
			Hint("Look at a locked chest and press the kick key: you turn a little to the left and drive one powerful straight-leg kick into the lock, and once it gives the chest opens. Stamina and kicks per lock level are set below; the crime setting applies too. Unrelenting Force doesn't open chests.");
			BeginDisabled(!s.chests);
			Checkbox(Id("Open the chest once it gives").c_str(), &s.openChests);
			Hint("The chest opens as soon as the lock gives. Off: it only stays unlocked.");
			Spacing();
			Text("%s", T("Chest locks"));
			TextWrapped("%s", T("A kick needs at least the stamina shown (with less you don't kick) and takes the cost when it lands. The lock gives after the kicks shown (a weakened lock stays weakened)."));
			if (BeginTable("chestLocks", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
				TableSetupColumn(T("Lock"));
				TableSetupColumn(T("Can be forced"));
				TableSetupColumn(T("Stamina needed"));
				TableSetupColumn(T("Stamina cost"));
				TableSetupColumn(T("Kicks"));
				TableHeadersRow();
				for (std::size_t i = 0; i < Settings::kLevels; ++i) {
					auto& c = s.chestLocks[i];
					TableNextRow();
					TableNextColumn();
					Text("%s", Level(static_cast<int>(i)));
					TableNextColumn();
					Checkbox(std::format("##chestOn{}", i).c_str(), &c.enabled);
					BeginDisabled(!c.enabled);
					TableNextColumn();
					SetNextItemWidth(-1.0f);
					SliderFloat(std::format("##chestNeed{}", i).c_str(), &c.staminaNeeded, 0.0f, 500.0f, "%.0f");
					TableNextColumn();
					SetNextItemWidth(-1.0f);
					SliderFloat(std::format("##chestCost{}", i).c_str(), &c.staminaCost, 0.0f, 200.0f, "%.0f");
					TableNextColumn();
					SetNextItemWidth(-1.0f);
					SliderInt(std::format("##chestKicks{}", i).c_str(), &c.kicks, 1, Settings::kMaxKicks);
					EndDisabled();
				}
				EndTable();
			}
			if (Button(Id("Defaults", "chestLocks").c_str())) {
				const Settings::Values defaults;
				for (std::size_t i = 0; i < Settings::kLevels; ++i) {
					s.chestLocks[i].staminaNeeded = defaults.chestLocks[i].staminaNeeded;
					s.chestLocks[i].staminaCost = defaults.chestLocks[i].staminaCost;
					s.chestLocks[i].kicks = defaults.chestLocks[i].kicks;
				}
			}
			EndDisabled();

			Heading("Door locks");
			TextWrapped("%s", T("A kick needs at least the stamina shown and takes the cost when it lands; with less, the door only rattles. A door gives after the kicks shown (a weakened door stays weakened)."));
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
					SetNextItemWidth(-1.0f);
					SliderInt(std::format("##kicks{}", i).c_str(), &l.kicks, 1, Settings::kMaxKicks);
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
				SetNextItemWidth(-1.0f);
				SliderInt("##kicksU", &u.kicks, 1, Settings::kMaxKicks);
				TableNextColumn();
				Checkbox(Id("Any", "shoutU").c_str(), &s.shoutUnlocked);
				if (IsItemHovered()) {
					SetTooltip("%s", T("Unrelenting Force (any number of words) throws unlocked doors open."));
				}
				EndDisabled();
				EndTable();
			}
			if (Button(Id("Defaults", "locks").c_str())) {
				const Settings::Values defaults;
				for (std::size_t i = 0; i < Settings::kLevels; ++i) {
					s.locks[i].staminaNeeded = defaults.locks[i].staminaNeeded;
					s.locks[i].staminaCost = defaults.locks[i].staminaCost;
					s.locks[i].shoutWords = defaults.locks[i].shoutWords;
					s.locks[i].kicks = defaults.locks[i].kicks;
				}
				s.unlocked.staminaNeeded = defaults.unlocked.staminaNeeded;
				s.unlocked.staminaCost = defaults.unlocked.staminaCost;
				s.unlocked.kicks = defaults.unlocked.kicks;
			}

			Heading("Beast forms");
			TextWrapped("%s", T("Werewolves and vampire lords don't kick. A blow breaks the door instead: one is enough for any lock the door lock table lets you force."));
			Checkbox(Id("Werewolf power attacks break doors").c_str(), &s.werewolfDoors);
			Hint("A werewolf's power attack breaks the locked (or closed) door in front of it.");
			Checkbox(Id("Vampire lord power attacks break doors").c_str(), &s.vampireLordDoors);
			Hint("On the ground, a vampire lord's power attack breaks the door in front of it.");
			Checkbox(Id("Vampire lord spells break doors").c_str(), &s.vampireLordMagic);
			Hint("A hostile spell a vampire lord aims at a door (Vampiric Drain, Vampiric Grip...) breaks it.");
			SliderFloat(Id("Reach", "beast").c_str(), &s.beastReach, 100.0f, 400.0f, T("%.0f units"));
			Hint("The farthest the door may be from the beast for a blow.");
			BeginDisabled(!s.vampireLordMagic);
			SliderFloat(Id("Spell reach").c_str(), &s.beastMagicReach, 300.0f, 4000.0f, T("%.0f units"));
			EndDisabled();

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
			Checkbox(Id("Pant when out of stamina").c_str(), &s.panting);
			Hint("Trying to kick without the stamina for it, you pant like when you sprint with none left (in your own voice).");
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
