#include "Power.h"

#include "Kick.h"
#include "Settings.h"

namespace Power
{
	namespace
	{
		RE::SpellItem* power{ nullptr };
		bool           shoutOff{ false };  // we switched the shout handler off

		bool Equipped()
		{
			auto* const player = RE::PlayerCharacter::GetSingleton();
			return power && player && Settings::Get().kickPower && player->GetActorRuntimeData().selectedPower == power;
		}

		void ShoutHandler(bool a_on)
		{
			auto* const controls = RE::PlayerControls::GetSingleton();
			auto* const handler = controls ? controls->shoutHandler : nullptr;
			if (!handler) {
				return;
			}
			if (!a_on && !shoutOff && handler->IsInputEventHandlingEnabled()) {
				handler->SetInputEventHandlingEnabled(false);
				shoutOff = true;
			} else if (a_on && shoutOff) {
				handler->SetInputEventHandlingEnabled(true);
				shoutOff = false;
			}
		}

		// cast without the shout key (a hotkey mod, a script): it kicks all the same
		class CastSink : public RE::BSTEventSink<RE::TESSpellCastEvent>
		{
		public:
			static CastSink* GetSingleton()
			{
				static CastSink sink;
				return &sink;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent* a_event, RE::BSTEventSource<RE::TESSpellCastEvent>*) override
			{
				if (a_event && power && a_event->spell == power->GetFormID() && a_event->object && a_event->object->IsPlayerRef() &&
					Settings::Get().kickPower) {
					Kick::Request();
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Init()
	{
		power = RE::TESDataHandler::GetSingleton()->LookupForm<RE::SpellItem>(0x806, "KickTheDoor.esp");
		if (!power) {
			logs::warn("The Kick power is missing (is KickTheDoor.esp enabled?)");
			return;
		}
		RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESSpellCastEvent>(CastSink::GetSingleton());
	}

	void Sync()
	{
		auto* const player = RE::PlayerCharacter::GetSingleton();
		if (!power || !player || !player->GetParentCell()) {
			return;  // no game loaded yet (the main menu): done when one is
		}
		const bool want = Settings::Get().kickPower;
		if (want != player->HasSpell(power)) {
			if (want) {
				player->AddSpell(power);
			} else {
				player->RemoveSpell(power);
			}
			if (Settings::Get().debugLog) {
				logs::info("Kick power {}", want ? "given" : "taken away");
			}
		}
		if (!want) {
			ShoutHandler(true);
		}
	}

	void OnKey(const RE::ButtonEvent* a_event)
	{
		if (a_event->IsDown() && Equipped() && a_event->QUserEvent() == RE::UserEvents::GetSingleton()->shout) {
			Kick::Request();
		}
	}

	void Update()
	{
		ShoutHandler(!Equipped());
	}

	void Reset()
	{
		ShoutHandler(true);
	}
}
