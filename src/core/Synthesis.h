#pragma once

#include "core/Catalog.h"
#include "core/JudgementRing.h"

#include <unordered_map>

namespace atelier
{
	struct AlchemistStats
	{
		float skill{ 15.0f };
		float magnitudeMult{ 1.0f };  // perks such as Alchemist / Physician / Benefactor / Poisoner
		float globalMagnitudeMult{ 1.0f };
	};

	// What vanilla alchemy can make of an effect right now: the strongest ingredient's
	// base values with the player's potency (skill, perks, Fortify Alchemy) applied.
	struct EffectReference
	{
		float magnitude{ 0.0f };
		float duration{ 0.0f };
		bool  scalesDuration{ false };  // potency lengthens the effect instead (Invisibility, Paralysis...)
	};

	struct SynthesisInput
	{
		const RecipeDef*           recipe{ nullptr };
		std::vector<MaterialInput> materials;  // one per required slot, in requirement order
		std::optional<MaterialInput> catalyst;
		std::vector<std::string>     chosenTraits;  // trait ids picked from the materials
		AlchemistStats               stats;
		std::unordered_map<std::string, EffectReference> references;  // by effect key
	};

	struct LineState
	{
		int value{ 0 };
		int reachedTier{ -1 };  // index into the line's tiers, -1 if none reached
	};

	struct SynthesisOutcome
	{
		bool                     valid{ false };
		std::string              error;
		ElementValues            elements{};
		std::vector<LineState>   lines;
		int                      quality{ 0 };
		int                      yield{ 0 };
		int                      traitSlots{ 0 };
		std::vector<std::string> finalTraits;  // after fusion and slot limit
		BrewData                 brew;
	};

	// A material's own elements plus those of its element traits ("Charged" +2 Lightning, ...).
	ElementValues EffectiveElements(const MaterialInput& a_material, const Catalog& a_catalog);

	// Total element values (materials + catalyst). Exposed for UI previews.
	ElementValues SumElements(const SynthesisInput& a_input, const Catalog& a_catalog);

	// Checks that every requirement slot is filled by a material that satisfies it.
	bool ValidateMaterials(const RecipeDef& a_recipe, const std::vector<MaterialInput>& a_materials, std::string* a_error);

	// Quality multipliers for effects, linear up to a cap so late-game brews stay sane.
	// Magnitude: Q1 = 0.6x, Q80 = 1.0x, Q200 = 1.6x, capped at 2.0x from Q280.
	// Duration gets half the slope: Q80 = 1.0x, Q200 = 1.3x, capped at 1.5x from Q280.
	float QualityMagnitudeMult(int a_quality);
	float QualityDurationMult(int a_quality);

	// Traits stack, but their combined bonus is capped (penalties are not).
	inline constexpr float kMaxTraitMagnitudePct = 25.0f;
	inline constexpr float kMaxTraitDurationPct = 50.0f;

	// Judgement Ring quality factors, per area.
	inline constexpr float kRingHitQuality = 1.02f;     // a hit outside the strike zone
	inline constexpr float kRingStrikeQuality = 1.06f;  // a strike (critical)
	inline constexpr float kRingMissQuality = 0.85f;
	inline constexpr float kRingPerfectQuality = 1.1f;  // on top of the strikes; also +1 item
	inline constexpr float kRingBotchQuality = 0.6f;    // first area missed; also -1 item

	// Combined quality factor of a finished ring.
	float RingQualityFactor(const RingSummary& a_ring);

	// Evaluates a synthesis. Without a ring summary this is the preview the UI shows
	// while the player picks materials; with one it is the final result.
	SynthesisOutcome Evaluate(const SynthesisInput& a_input, const Catalog& a_catalog, const RingSummary* a_ring, int a_baseTraitSlots = 3);

	// Unlock rules of `a_recipe` that this synthesis satisfies.
	std::vector<std::string> CheckUnlocks(const RecipeDef& a_recipe, const SynthesisInput& a_input, const SynthesisOutcome& a_outcome);
}
