#pragma once

#include "game/Perks.h"

#include "core/Synthesis.h"
#include "game/State.h"

#include <set>

namespace game
{
	// Something in the player's inventory that can go into the cauldron. Identical
	// ingredient instances (same quality and traits) are grouped.
	struct Candidate
	{
		enum class Source : std::uint8_t
		{
			kIngredient,
			kBrew,
		};

		Source                 source{ Source::kIngredient };
		RE::FormID             formID{ 0 };
		int                    available{ 0 };
		atelier::MaterialInput material;
		IngredientInstance     instance;  // kIngredient
		SlotRef                slot;      // kBrew
		bool                   isCatalyst{ false };
	};

	// Everything the UI needs, captured on the main thread when the menu opens.
	struct WorkshopSnapshot
	{
		std::vector<Candidate> candidates;
		std::set<std::string>  known;
		float                  skill{ 15.0f };
		// Perk potency per effect key (Fortify Alchemy included), for potions and poisons.
		std::unordered_map<std::string, Perks::Potency> potionPotency;
		std::unordered_map<std::string, Perks::Potency> poisonPotency;
		std::vector<std::string>                        potencySummary;  // for the UI
		float                  ingredientInitMult{ 4.0f };  // game settings of the vanilla potency formula
		float                  skillFactor{ 1.5f };
		std::vector<std::string> newlyLearned;  // recipes unlocked by skill since last visit
	};

	struct CommitRequest
	{
		std::string              recipeId;
		std::vector<int>         materials;  // candidate index per recipe slot
		int                      catalyst{ -1 };
		std::vector<std::string> traits;
		atelier::RingSummary     ring;
	};

	struct CommitResult
	{
		bool                     ok{ false };
		std::string              error;
		atelier::BrewData        brew;
		int                      yield{ 0 };
		std::string              yieldBonus;  // perks that changed the batch size, for the UI
		std::vector<std::string> unlocked;
		atelier::RingSummary     ring;
	};

	// Main thread only.
	WorkshopSnapshot TakeSnapshot();
	CommitResult     Commit(const WorkshopSnapshot& a_snapshot, const CommitRequest& a_request);
	bool             CanSynthesizeHere(std::string* a_reason);
	bool             IsUsingAlchemyLab();

	// Pure helpers usable from the UI thread.
	atelier::AlchemistStats StatsFor(const WorkshopSnapshot& a_snapshot, const atelier::RecipeDef& a_recipe);
	// What vanilla alchemy would make of each of the recipe's effects with the player's
	// current skill, perks and Fortify Alchemy.
	std::unordered_map<std::string, atelier::EffectReference> EffectReferences(const WorkshopSnapshot& a_snapshot, const atelier::RecipeDef& a_recipe);

	atelier::SynthesisInput BuildInput(const WorkshopSnapshot& a_snapshot, const atelier::RecipeDef& a_recipe,
		const std::vector<int>& a_materials, int a_catalyst, const std::vector<std::string>& a_traits);
	atelier::RingParams RingParamsFor(const WorkshopSnapshot& a_snapshot, const atelier::RecipeDef& a_recipe,
		const std::vector<int>& a_materials, int a_catalyst);
}
