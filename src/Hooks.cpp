#include "Hooks.h"

#include "Beast.h"
#include "Doors.h"
#include "Input.h"
#include "Kick.h"
#include "Power.h"
#include "Shout.h"

namespace Hooks
{
	namespace
	{
		bool skeletonHook = false;

		// once a frame, on the main thread, while the game runs
		struct PlayerUpdate
		{
			static void thunk(RE::PlayerCharacter* a_this, float a_delta)
			{
				func(a_this, a_delta);
				Power::Update();
				Kick::Update(a_delta);
				Beast::Update(a_delta);
				Shout::Update(a_delta);
				Doors::Update(a_delta);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// The player's 3D update (SE 39446 / AE 40522) animates and updates the 3rd person body, then calls
		// NiAVObject::Update on the 1st person skeleton (+0xD7, this call). The kick pose goes on right after it.
		// Other mods (Improved Camera, Item Inspection, Cat Carry) hook the same call; the hooks chain.
		struct PlayerSkeletonsUpdate
		{
			static void thunk(RE::NiAVObject* a_object, RE::NiUpdateData* a_data)
			{
				func(a_object, a_data);
				Kick::ApplyPose();
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// door animation controllers: after one wrote its node poses, forced doors may get mirrored (Doors)
		template <int Kind>
		struct ControllerUpdate
		{
			static void thunk(RE::NiTimeController* a_this, float a_time)
			{
				func(a_this, a_time);
				Doors::OnControllerUpdate(a_this->target, Kind);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <int N>
		void HookController(REL::VariantID a_vtable)
		{
			REL::Relocation<std::uintptr_t> vtable{ a_vtable };
			ControllerUpdate<N>::func = vtable.write_vfunc(0x27, ControllerUpdate<N>::thunk);
		}

		class InputSink : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static InputSink* GetSingleton()
			{
				static InputSink sink;
				return &sink;
			}

			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_events, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				for (auto* event = a_events ? *a_events : nullptr; event; event = event->next) {
					if (const auto* button = event->AsButtonEvent()) {
						Kick::OnKey(button);
						Power::OnKey(button);
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Install()
	{
		const auto address = REL::RelocationID(39446, 40522).address() + 0xD7;
		if (*reinterpret_cast<const std::uint8_t*>(address) == 0xE8) {
			PlayerSkeletonsUpdate::func = SKSE::GetTrampoline().write_call<5>(address, PlayerSkeletonsUpdate::thunk);
			skeletonHook = true;
		} else {
			logs::error("Player skeleton update call not found: the kick plays without its animation");
		}

		HookController<0>(RE::VTABLE_NiControllerManager[0]);
		HookController<1>(RE::VTABLE_NiMultiTargetTransformController[0]);
		HookController<2>(RE::VTABLE_NiTransformController[0]);

		REL::Relocation<std::uintptr_t> player{ RE::VTABLE_PlayerCharacter[0] };
		PlayerUpdate::func = player.write_vfunc(0xAD, PlayerUpdate::thunk);

		Input::Install();

		logs::info("Hooks installed{}", skeletonHook ? "" : " (no kick animation)");
	}

	void InstallInput()
	{
		if (auto* input = RE::BSInputDeviceManager::GetSingleton(); input) {
			input->AddEventSink(InputSink::GetSingleton());
		}
	}
}
