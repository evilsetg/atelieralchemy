#pragma once

#include "core/Types.h"

#include <map>

namespace atelier
{
	// All static, data-driven definitions. Filled from JSON by the game layer
	// (or by hand in tests).
	struct Catalog
	{
		std::map<std::string, TraitDef, std::less<>>    traits;
		std::vector<TraitCombo>                         combos;
		std::map<std::string, RecipeDef, std::less<>>   recipes;
		std::map<std::string, CatalystDef, std::less<>> catalysts;  // keyed by item key

		[[nodiscard]] const TraitDef*    FindTrait(std::string_view a_id) const;
		[[nodiscard]] const RecipeDef*   FindRecipe(std::string_view a_id) const;
		[[nodiscard]] const CatalystDef* FindCatalyst(std::string_view a_itemKey) const;
		// Result of fusing two traits, if a combination exists (order independent).
		[[nodiscard]] const TraitDef* Combine(std::string_view a_first, std::string_view a_second) const;
	};

	// Repeatedly fuses pairs from the selection until no combination applies,
	// then removes duplicates. Order of the selection is preserved where possible.
	std::vector<std::string> FuseTraits(const Catalog& a_catalog, std::vector<std::string> a_selected);
}
