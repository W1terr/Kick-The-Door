#include "Beast.h"
#include "Chests.h"
#include "Clip.h"
#include "Doors.h"
#include "Fight.h"
#include "Hooks.h"
#include "Kick.h"
#include "Lang.h"
#include "Menu.h"
#include "Power.h"
#include "Settings.h"
#include "Shout.h"

namespace
{
	constexpr std::uint32_t kSaveID = 'KTDR';

	void OnSave(SKSE::SerializationInterface* a_intfc) { Doors::Save(a_intfc); }

	void OnLoad(SKSE::SerializationInterface* a_intfc)
	{
		std::uint32_t type, version, length;
		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (type == 'KICK') {
				Doors::Load(a_intfc, version, length);
			}
		}
	}

	void OnRevert(SKSE::SerializationInterface*) { Doors::Revert(); }

	void Reset()
	{
		Kick::Reset();
		Power::Reset();
		Beast::Reset();
		Shout::Reset();
		Doors::Reset();
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			Doors::Init();
			Chests::Init();
			Fight::Init();
			Shout::Init();
			Power::Init();
			Beast::Init();
			Clip::Load();
			Lang::Set(Settings::Get().language);
			Hooks::InstallInput();
			Menu::Register();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			Reset();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			Reset();
			Power::Sync();
			break;
		default:
			break;
		}
	}
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, { .trampoline = true, .trampolineSize = 64 });
	logs::info("Kick the Door loading");
	Settings::Load();
	Hooks::Install();
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	if (auto* serialization = SKSE::GetSerializationInterface(); serialization) {
		serialization->SetUniqueID(kSaveID);
		serialization->SetSaveCallback(OnSave);
		serialization->SetLoadCallback(OnLoad);
		serialization->SetRevertCallback(OnRevert);
	}
	return true;
}
