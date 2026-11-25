#include "core/Catalog.h"

#include <algorithm>
#include <cctype>

namespace atelier
{
	namespace
	{
		bool IEquals(std::string_view a_lhs, std::string_view a_rhs)
		{
			return a_lhs.size() == a_rhs.size() &&
			       std::equal(a_lhs.begin(), a_lhs.end(), a_rhs.begin(), [](char a, char b) {
					   return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
				   });
		}
	}

	const char* ElementName(Element a_element)
	{
		switch (a_element) {
		case Element::kFire:
			return "Fire";
		case Element::kIce:
			return "Ice";
		case Element::kLightning:
			return "Lightning";
		case Element::kWind:
			return "Wind";
		}
		return "?";
	}

	std::optional<Element> ParseElement(std::string_view a_name)
	{
		for (std::size_t i = 0; i < kElementCount; ++i) {
			const auto element = static_cast<Element>(i);
			if (IEquals(a_name, ElementName(element))) {
				return element;
			}
		}
		return std::nullopt;
	}

	const char* OutputKindName(OutputKind a_kind)
	{
		switch (a_kind) {
		case OutputKind::kPotion:
			return "potion";
		case OutputKind::kPoison:
			return "poison";
		case OutputKind::kMaterial:
			return "material";
		}
		return "?";
	}

	std::optional<OutputKind> ParseOutputKind(std::string_view a_name)
	{
		for (auto kind : { OutputKind::kPotion, OutputKind::kPoison, OutputKind::kMaterial }) {
			if (IEquals(a_name, OutputKindName(kind))) {
				return kind;
			}
		}
		return std::nullopt;
	}

	const char* QualityRank(int a_quality)
	{
		if (a_quality >= 400) {
			return "SS";
		}
		if (a_quality >= 250) {
			return "S";
		}
		if (a_quality >= 150) {
			return "A";
		}
		if (a_quality >= 100) {
			return "B";
		}
		if (a_quality >= 60) {
			return "C";
		}
		if (a_quality >= 30) {
			return "D";
		}
		return "E";
	}

	int RecipeDef::SlotCount() const
	{
		int total = 0;
		for (const auto& req : requirements) {
			total += std::max(req.count, 0);
		}
		return total;
	}

	bool MaterialInput::HasCategory(std::string_view a_category) const
	{
		return std::ranges::any_of(categories, [&](const std::string& c) { return IEquals(c, a_category); });
	}

	bool MaterialInput::Satisfies(const Requirement& a_req) const
	{
		return a_req.isCategory ? HasCategory(a_req.key) : IEquals(itemKey, a_req.key);
	}

	const TraitDef* Catalog::FindTrait(std::string_view a_id) const
	{
		const auto it = traits.find(a_id);
		return it != traits.end() ? &it->second : nullptr;
	}

	const RecipeDef* Catalog::FindRecipe(std::string_view a_id) const
	{
		const auto it = recipes.find(a_id);
		return it != recipes.end() ? &it->second : nullptr;
	}

	const CatalystDef* Catalog::FindCatalyst(std::string_view a_itemKey) const
	{
		const auto it = catalysts.find(a_itemKey);
		return it != catalysts.end() ? &it->second : nullptr;
	}

	const TraitDef* Catalog::Combine(std::string_view a_first, std::string_view a_second) const
	{
		for (const auto& combo : combos) {
			if ((combo.a == a_first && combo.b == a_second) || (combo.a == a_second && combo.b == a_first)) {
				return FindTrait(combo.result);
			}
		}
		return nullptr;
	}

	std::vector<std::string> FuseTraits(const Catalog& a_catalog, std::vector<std::string> a_selected)
	{
		bool changed = true;
		while (changed) {
			changed = false;
			for (std::size_t i = 0; i < a_selected.size() && !changed; ++i) {
				for (std::size_t j = i + 1; j < a_selected.size() && !changed; ++j) {
					if (const auto fused = a_catalog.Combine(a_selected[i], a_selected[j])) {
						a_selected[i] = fused->id;
						a_selected.erase(a_selected.begin() + static_cast<std::ptrdiff_t>(j));
						changed = true;
					}
				}
			}
		}

		std::vector<std::string> unique;
		for (auto& id : a_selected) {
			if (std::ranges::find(unique, id) == unique.end()) {
				unique.push_back(std::move(id));
			}
		}
		return unique;
	}
}
