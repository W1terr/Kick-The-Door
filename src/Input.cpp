#include "Input.h"

#include "Keys.h"
#include "Kick.h"
#include "Settings.h"

namespace Input
{
	namespace
	{
		constexpr int kCodes = 282;  // keyboard 0-255, mouse 256-265, gamepad 266-281

		std::array<bool, kCodes> held{};
		std::array<bool, kCodes> kept{};  // pressed to complete a pair: the game doesn't get it until it comes up

		struct Pair
		{
			int  first{ Keys::kNone };
			int  second{ Keys::kNone };
			bool fired{ false };
		};
		std::array<Pair, 2> pairs;  // keyboard / mouse, gamepad

		int CodeOf(const RE::InputEvent* a_event)
		{
			const auto* button = a_event->AsButtonEvent();
			const int   code = button ? Keys::Code(button) : Keys::kNone;
			return code >= 0 && code < kCodes ? code : Keys::kNone;
		}

		bool Valid(int a_code) { return a_code >= 0 && a_code < kCodes; }

		bool Playing()
		{
			auto* const ui = RE::UI::GetSingleton();
			return ui && !ui->GameIsPaused() && !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME);
		}

		// the pairs as set now (none while only the Kick power kicks)
		void Refresh()
		{
			const auto& s = Settings::Get();
			const std::array<std::pair<int, int>, 2> wanted{ { { s.kickKey, s.kickKey2 }, { s.kickGamepadKey, s.kickGamepadKey2 } } };
			for (std::size_t i = 0; i < pairs.size(); ++i) {
				auto [first, second] = wanted[i];
				if (s.kickPower || !Valid(first) || !Valid(second) || first == second) {
					first = second = Keys::kNone;
				}
				if (pairs[i].first != first || pairs[i].second != second) {
					pairs[i] = { first, second, false };
				}
			}
		}

		// This frame's events, once per sink that sees them (the same events give the same result each time): which
		// keys are held, whether one of them completes a pair (then it is kept), whether the kick goes off.
		void Scan(RE::InputEvent* a_head)
		{
			Refresh();
			if (pairs[0].first == Keys::kNone && pairs[1].first == Keys::kNone) {
				kept.fill(false);
				return;
			}
			for (auto* event = a_head; event; event = event->next) {
				if (const int code = CodeOf(event); code != Keys::kNone) {
					held[code] = event->AsButtonEvent()->IsPressed();
				}
			}
			const float holdTime = Settings::Get().holdTime;
			for (auto* event = a_head; event; event = event->next) {
				const int code = CodeOf(event);
				if (code == Keys::kNone) {
					continue;
				}
				for (auto& pair : pairs) {
					if (pair.first == Keys::kNone || (code != pair.first && code != pair.second)) {
						continue;
					}
					const auto* button = event->AsButtonEvent();
					if (button->IsDown()) {
						kept[code] = held[code == pair.first ? pair.second : pair.first] && Playing();
						if (kept[code]) {
							pair.fired = false;
						}
					}
					if (kept[code] && button->IsPressed() && !pair.fired && button->HeldDuration() >= holdTime) {
						pair.fired = true;
						Kick::Request();
					}
				}
			}
		}

		bool Kept(const RE::InputEvent* a_event)
		{
			const int code = CodeOf(a_event);
			return code != Keys::kNone && kept[code];
		}

		// the sink gets this frame's events without the kept ones (relinked for the call, put back right after)
		template <int N>
		struct ProcessEvent
		{
			static RE::BSEventNotifyControl thunk(RE::BSTEventSink<RE::InputEvent*>* a_this, RE::InputEvent* const* a_events,
				RE::BSTEventSource<RE::InputEvent*>* a_source)
			{
				auto* const head = a_events ? *a_events : nullptr;
				Scan(head);
				bool any = false;
				for (auto* event = head; event && !any; event = event->next) {
					any = Kept(event);
				}
				if (!any) {
					return func(a_this, a_events, a_source);
				}
				static std::vector<std::pair<RE::InputEvent*, RE::InputEvent*>> links;  // the game's input thread only
				links.clear();
				for (auto* event = head; event; event = event->next) {
					links.emplace_back(event, event->next);
				}
				RE::InputEvent* first = nullptr;
				RE::InputEvent* last = nullptr;
				for (const auto& [event, next] : links) {
					if (Kept(event)) {
						continue;
					}
					(last ? last->next : first) = event;
					last = event;
				}
				if (last) {
					last->next = nullptr;
				}
				RE::InputEvent* const filtered = first;
				const auto            result = func(a_this, &filtered, a_source);
				for (const auto& [event, next] : links) {
					event->next = next;
				}
				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <int N>
		void Hook(REL::VariantID a_vtable)
		{
			REL::Relocation<std::uintptr_t> vtable{ a_vtable };
			ProcessEvent<N>::func = vtable.write_vfunc(0x1, ProcessEvent<N>::thunk);
		}
	}

	void Install()
	{
		Hook<0>(RE::VTABLE_PlayerControls[0]);
		Hook<1>(RE::VTABLE_MenuControls[0]);
	}
}
