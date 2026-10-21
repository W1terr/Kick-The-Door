#pragma once

namespace Forms
{
	// "Plugin.esp|0x123456" (local form id) or an editor id; nullptr if it doesn't exist
	RE::TESForm* Resolve(std::string_view a_spec);
	// every entry of a list that resolves, as form ids
	std::unordered_set<RE::FormID> ResolveAll(const std::vector<std::string>& a_specs, const char* a_what);

	template <class T>
	T* Vanilla(RE::FormID a_id)
	{
		auto* const form = RE::TESDataHandler::GetSingleton()->LookupForm(a_id, "Skyrim.esm"sv);
		return form ? form->As<T>() : nullptr;
	}

	std::string Name(const RE::TESForm* a_form);
}
