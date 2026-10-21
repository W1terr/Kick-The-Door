#include "Forms.h"

namespace Forms
{
	RE::TESForm* Resolve(std::string_view a_spec)
	{
		const auto bar = a_spec.find('|');
		if (bar == std::string_view::npos) {
			return RE::TESForm::LookupByEditorID(a_spec);
		}
		const std::string plugin(a_spec.substr(0, bar));
		const std::string id(a_spec.substr(bar + 1));
		RE::FormID        local = 0;
		try {
			local = static_cast<RE::FormID>(std::stoul(id, nullptr, 16));
		} catch (...) {
			logs::warn("'{}': '{}' is not a hex form id", a_spec, id);
			return nullptr;
		}
		auto* const data = RE::TESDataHandler::GetSingleton();
		const auto* mod = data->LookupModByName(plugin);
		if (!mod) {
			return nullptr;
		}
		return data->LookupForm(local & (mod->IsLight() ? 0xFFF : 0xFFFFFF), plugin);
	}

	std::unordered_set<RE::FormID> ResolveAll(const std::vector<std::string>& a_specs, const char* a_what)
	{
		std::unordered_set<RE::FormID> ids;
		for (const auto& spec : a_specs) {
			if (const auto* form = Resolve(spec)) {
				ids.insert(form->GetFormID());
			} else {
				logs::info("{}: '{}' not found (plugin not loaded?)", a_what, spec);
			}
		}
		return ids;
	}

	std::string Name(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return "none";
		}
		if (const char* name = a_form->GetName(); name && *name) {
			return name;
		}
		if (const char* edid = a_form->GetFormEditorID(); edid && *edid) {
			return edid;
		}
		return std::format("{:08X}", a_form->GetFormID());
	}
}
