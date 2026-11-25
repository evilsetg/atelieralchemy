#pragma once

#include "core/Catalog.h"
#include "core/Synthesis.h"

#include <nlohmann/json_fwd.hpp>

namespace game
{
	struct IngredientProfile
	{
		RE::IngredientItem*      form{ nullptr };
		std::string              key;
		std::string              name;
		std::vector<std::string> categories;
		atelier::ElementValues   elements{};
		int                      rarity{ 0 };  // 0 common .. 3 legendary
		int                      baseQuality{ 25 };
		std::vector<std::string> traitPool;
		std::string              plugin;             // file that defines the ingredient
		bool                     obtainable{ true };  // see Database::MarkObtainable
	};

	struct EffectInfo
	{
		std::string        key;
		std::string        name;
		RE::EffectSetting* form{ nullptr };

		// Vanilla baseline: the strongest ingredient carrying this effect (a potion if no
		// ingredient does). Vanilla alchemy multiplies one of the two by its potency.
		float       baseMagnitude{ 0.0f };
		int         baseDuration{ 0 };
		int         vanillaDuration{ 0 };  // before an effects.json "duration" override; used for pricing
		bool        powerAffectsMagnitude{ true };
		bool        powerAffectsDuration{ false };
		std::string baseSource;
		[[nodiscard]] bool HasBase() const { return !baseSource.empty(); }

		[[nodiscard]] atelier::EffectCostInfo CostInfo() const
		{
			using Flag = RE::EffectSetting::EffectSettingData::Flag;
			return { .baseCost = form->data.baseCost,
				.usesMagnitude = !form->data.flags.all(Flag::kNoMagnitude),
				.usesDuration = !form->data.flags.all(Flag::kNoDuration),
				.pricedDurationScale = baseDuration > 0 && vanillaDuration > 0 ? static_cast<float>(vanillaDuration) / static_cast<float>(baseDuration) : 1.0f };
		}
	};

	// Static data: JSON definitions from Data/SKSE/Plugins/AtelierAlchemy/ plus
	// everything derived from the load order (ingredient profiles, magic effects).
	class Database
	{
	public:
		static Database& Get();

		bool Load();

		[[nodiscard]] const atelier::Catalog& Catalog() const { return _catalog; }

		[[nodiscard]] const EffectInfo* FindEffect(std::string_view a_key) const;
		[[nodiscard]] const auto&       Effects() const { return _effects; }
		[[nodiscard]] std::string       EffectName(std::string_view a_key) const;

		[[nodiscard]] const IngredientProfile* FindIngredient(RE::FormID a_formID) const;
		[[nodiscard]] const auto&              IngredientProfiles() const { return _ingredients; }
		// Whether any ingredient in the load order belongs to the category (otherwise only
		// synthesized materials, potions or poisons can fill it).
		[[nodiscard]] bool                     CategoryHasIngredients(std::string_view a_category) const;
		[[nodiscard]] const IngredientProfile* FindIngredientByKey(std::string_view a_key) const;
		[[nodiscard]] const std::vector<std::string>& CategoryTraitPool(std::string_view a_category) const;

		// Display name for an item key (ingredient or synthesized material).
		[[nodiscard]] std::string ItemName(std::string_view a_itemKey) const;

		// Form whose model/icon/sounds a brew of this recipe should copy.
		[[nodiscard]] RE::TESForm* TemplateFor(const atelier::RecipeDef& a_recipe, const atelier::BrewData& a_brew) const;

		[[nodiscard]] const std::vector<std::string>& Errors() const { return _errors; }

	private:
		void LoadTraits(const nlohmann::json& a_doc, const std::string& a_file);
		void LoadRecipes(const nlohmann::json& a_doc, const std::string& a_file);
		void LoadCatalysts(const nlohmann::json& a_doc, const std::string& a_file);
		void LoadEffects(const nlohmann::json& a_doc, const std::string& a_file);
		void LoadIngredients(const nlohmann::json& a_doc, const std::string& a_file);
		void LoadRingPatterns(const nlohmann::json& a_doc, const std::string& a_file);
		void ResolveRingPatterns();
		void MarkObtainable();
		void AuditRecipes() const;
		void ResolveEffects();
		void BuildIngredientProfiles();
		void BuildTemplates();
		void Validate();

		std::optional<atelier::Element> ElementForEffect(const RE::EffectSetting* a_effect) const;
		void                            Error(std::string a_message);

		struct EffectSignature
		{
			std::vector<RE::ActorValue>                       avs;
			std::optional<RE::EffectArchetypes::ArchetypeID> archetype;
			std::optional<bool>                               detrimental;
			std::optional<bool>                               recover;
			std::string                                       name;
			std::string                                       nameContains;  // lower case, matched against the MGEF name
			std::vector<std::string>                          nameExcludes;
			std::string                                       formRef;  // "Plugin.esp|0x123456", preferred over the rest
			int                                               duration{ 0 };  // replaces the vanilla base duration, 0 = keep
		};

		struct IngredientOverride
		{
			std::string              key;
			std::string              name;
			std::string              formRef;
			std::vector<std::string> categories;
			atelier::ElementValues   elementBonus{};
			std::optional<int>       rarity;
			std::vector<std::string> traits;
		};

		atelier::Catalog                                     _catalog;
		std::map<std::string, EffectSignature, std::less<>> _signatures;
		std::map<std::string, EffectInfo, std::less<>>      _effects;
		std::unordered_map<RE::ActorValue, atelier::Element> _avElements;
		std::unordered_map<RE::EffectArchetypes::ArchetypeID, atelier::Element> _archetypeElements;

		std::map<std::string, std::vector<atelier::RingShapeArea>, std::less<>> _ringPatterns;
		std::map<std::string, nlohmann::json, std::less<>>                    _recipeRings;  // recipe id -> "ring" value
		std::vector<IngredientOverride>                                  _ingredientOverrides;
		std::map<std::string, std::vector<std::string>, std::less<>>     _categoryKeywords;
		std::map<std::string, std::vector<std::string>, std::less<>>     _categoryTraitPools;
		std::unordered_map<RE::FormID, IngredientProfile>                _ingredients;
		std::map<std::string, RE::FormID, std::less<>>                   _ingredientsByKey;
		std::unordered_map<const RE::EffectSetting*, RE::AlchemyItem*>  _potionTemplates;
		RE::AlchemyItem*                                                 _fallbackPotion{ nullptr };
		RE::AlchemyItem*                                                 _fallbackPoison{ nullptr };
		std::vector<std::string>                                         _errors;
	};
}
