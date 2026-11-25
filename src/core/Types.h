#pragma once

// Core data model for Atelier Alchemy. Nothing in src/core depends on the game,
// so it can be unit tested on the host (see tests/).

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace atelier
{
	// Ryza-style elements. Every material carries some amount of each; recipes
	// have effect lines keyed to one element that unlock tiers as the total rises.
	enum class Element : std::uint8_t
	{
		kFire,
		kIce,
		kLightning,
		kWind,
	};
	inline constexpr std::size_t kElementCount = 4;
	using ElementValues = std::array<int, kElementCount>;

	inline constexpr int kMinQuality = 1;
	inline constexpr int kMaxQuality = 999;

	const char*            ElementName(Element a_element);
	std::optional<Element> ParseElement(std::string_view a_name);

	inline int& At(ElementValues& a_values, Element a_element) { return a_values[static_cast<std::size_t>(a_element)]; }
	inline int  At(const ElementValues& a_values, Element a_element) { return a_values[static_cast<std::size_t>(a_element)]; }

	enum class OutputKind : std::uint8_t
	{
		kPotion,
		kPoison,
		kMaterial,
	};
	const char*               OutputKindName(OutputKind a_kind);
	std::optional<OutputKind> ParseOutputKind(std::string_view a_name);

	struct TraitDef
	{
		std::string id;
		std::string name;
		std::string description;
		int         grade{ 1 };  // for display/sorting only
		float       magnitudePct{ 0.0f };
		float       durationPct{ 0.0f };
		float       valuePct{ 0.0f };
		float       weightPct{ 0.0f };
		int         qualityBonus{ 0 };
		float       ringWidthDeg{ 0.0f };  // widens Judgement Ring hit areas when the material is used
		ElementValues elements{};           // added to every synthesis the carrier is used in
		bool        potionsOnly{ false };
		bool        poisonsOnly{ false };
	};

	struct TraitCombo
	{
		std::string a;
		std::string b;
		std::string result;
	};

	struct EffectTier
	{
		enum class Kind : std::uint8_t
		{
			kEffect,     // grants/upgrades the line's magic effect
			kQuality,    // +amount quality
			kYield,      // +amount items
			kTraitSlot,  // +amount inheritable traits
			kElement,    // material outputs: +amount of `element`
		};

		int         threshold{ 0 };
		Kind        kind{ Kind::kEffect };
		std::string effectKey;  // kEffect
		float       magnitude{ 0.0f };  // absolute values, used when vanillaScale is 0
		int         duration{ 0 };
		float       vanillaScale{ 0.0f };  // > 0: share of what vanilla alchemy makes right now
		int         amount{ 0 };
		Element     element{ Element::kFire };  // kElement
		std::string label;                      // optional override for UI text
	};

	struct EffectLine
	{
		std::string             name;
		Element                 element{ Element::kFire };
		std::vector<EffectTier> tiers;  // sorted by threshold ascending
	};

	struct Requirement
	{
		bool        isCategory{ true };
		std::string key;  // category name, or item key
		int         count{ 1 };
	};

	struct UnlockRule
	{
		std::string recipe;       // recipe id that gets unlocked
		std::string useItem;      // any material with this item key was used
		std::string useCategory;  // any material in this category was used
		int         minQuality{ 0 };
		std::string hint;  // shown on the recipe page ("Try using a (Fungus)...")
	};

	struct RecipeDef
	{
		std::string              id;
		std::string              name;
		std::string              description;
		OutputKind               kind{ OutputKind::kPotion };
		int                      level{ 1 };
		std::vector<Requirement> requirements;
		std::vector<EffectLine>  lines;
		int                      baseYield{ 1 };
		int                      baseValue{ 10 };
		float                    baseWeight{ 0.5f };
		std::string              templateRef;  // resolved by the game layer (model/icon/sounds)
		// material outputs
		std::vector<std::string> outputCategories;
		ElementValues            outputElements{};
		// discovery
		bool                    knownAtStart{ false };
		int                     unlockSkill{ -1 };  // < 0: not unlocked by skill
		std::vector<UnlockRule> unlocks;

		[[nodiscard]] int SlotCount() const;
	};

	struct CatalystDef
	{
		std::string   itemKey;
		std::string   name;
		ElementValues addElements{};
		int           qualityBonus{ 0 };
		float         ringWidthDeg{ 0.0f };
		float         ringSpeedMult{ 1.0f };
		int           extraRingAreas{ 0 };
		std::string   description;
	};

	// A concrete thing that can be put into the cauldron.
	struct MaterialInput
	{
		std::string              itemKey;
		std::string              displayName;
		std::vector<std::string> categories;
		ElementValues            elements{};
		int                      quality{ 50 };
		std::vector<std::string> traits;

		[[nodiscard]] bool HasCategory(std::string_view a_category) const;
		[[nodiscard]] bool Satisfies(const Requirement& a_req) const;
	};

	struct OutputEffect
	{
		std::string effectKey;
		float       magnitude{ 0.0f };
		int         duration{ 0 };
		std::string breakdown;  // how the value came about, for the UI (not saved)
	};

	// The persistent description of a synthesized item (one stack/batch).
	struct BrewData
	{
		std::string               recipeId;
		std::string               name;
		OutputKind                kind{ OutputKind::kPotion };
		int                       quality{ 50 };
		std::vector<std::string>  traits;
		std::vector<OutputEffect> effects;
		int                       value{ 10 };
		float                     weight{ 0.5f };
		ElementValues             elements{};
		std::vector<std::string>  categories;
	};

	const char* QualityRank(int a_quality);
}
