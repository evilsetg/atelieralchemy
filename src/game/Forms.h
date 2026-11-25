#pragma once

#include "core/Types.h"

namespace game
{
	inline constexpr std::string_view kPluginName = "AtelierAlchemy.esp";

	std::string Lower(std::string_view a_str);
	bool        IEquals(std::string_view a_lhs, std::string_view a_rhs);
	// "Blue Mountain Flower" -> "blue_mountain_flower"
	std::string Slugify(std::string_view a_name);

	std::optional<RE::ActorValue>                         ParseActorValue(std::string_view a_name);
	std::string                                           ActorValueName(RE::ActorValue a_av);
	std::optional<RE::EffectArchetypes::ArchetypeID>      ParseArchetype(std::string_view a_name);

	// Resolves a form reference string:
	//   "Skyrim.esm|0x03EB15"    plugin + local form id
	//   "INGR:Salt Pile"         first ingredient with that (English) name
	//   "ALCH:..." / "MISC:..."  same for potions / misc items
	RE::TESForm* ResolveFormRef(std::string_view a_ref);

	std::string FormKey(const RE::TESForm* a_form);  // "Plugin.esp|0x000123"

	// Copies model, icon and pickup/putdown sounds from a template form onto a brew slot form.
	void CopyPresentation(RE::TESForm* a_from, RE::TESBoundObject* a_to);
}
