#include "core/Synthesis.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace atelier
{
	namespace
	{
		constexpr int kMaxTraitSlots = 5;

		std::vector<const Requirement*> ExpandSlots(const RecipeDef& a_recipe)
		{
			std::vector<const Requirement*> slots;
			for (const auto& req : a_recipe.requirements) {
				for (int i = 0; i < req.count; ++i) {
					slots.push_back(&req);
				}
			}
			return slots;
		}

		bool TraitAllowed(const TraitDef& a_trait, OutputKind a_kind)
		{
			if (a_trait.potionsOnly && a_kind != OutputKind::kPotion) {
				return false;
			}
			if (a_trait.poisonsOnly && a_kind != OutputKind::kPoison) {
				return false;
			}
			return true;
		}

		float RoundMagnitude(float a_value)
		{
			if (a_value >= 10.0f) {
				return std::round(a_value);
			}
			return std::max(std::round(a_value * 10.0f) / 10.0f, 0.1f);
		}
	}

	ElementValues SumElements(const SynthesisInput& a_input, const Catalog& a_catalog)
	{
		ElementValues total{};
		auto          add = [&](const ElementValues& a_values) {
            for (std::size_t i = 0; i < kElementCount; ++i) {
                total[i] += a_values[i];
            }
		};
		for (const auto& material : a_input.materials) {
			add(material.elements);
		}
		if (a_input.catalyst) {
			add(a_input.catalyst->elements);
			if (const auto def = a_catalog.FindCatalyst(a_input.catalyst->itemKey)) {
				add(def->addElements);
			}
		}
		return total;
	}

	bool ValidateMaterials(const RecipeDef& a_recipe, const std::vector<MaterialInput>& a_materials, std::string* a_error)
	{
		const auto slots = ExpandSlots(a_recipe);
		if (slots.size() != a_materials.size()) {
			if (a_error) {
				*a_error = "Not every ingredient slot is filled.";
			}
			return false;
		}
		for (std::size_t i = 0; i < slots.size(); ++i) {
			if (!a_materials[i].Satisfies(*slots[i])) {
				if (a_error) {
					*a_error = a_materials[i].displayName + " does not fit the " + slots[i]->key + " slot.";
				}
				return false;
			}
		}
		return true;
	}

	float QualityMagnitudeMult(int a_quality)
	{
		const float q = static_cast<float>(std::clamp(a_quality, kMinQuality, kMaxQuality));
		return std::min(0.6f + q / 200.0f, 2.0f);
	}

	float RingQualityFactor(const RingSummary& a_ring)
	{
		const float hitBonus = a_ring.automatic ? 1.0f : kRingHitQuality;
		float       factor = std::pow(hitBonus, static_cast<float>(a_ring.hits - a_ring.strikes)) *
		               std::pow(kRingStrikeQuality, static_cast<float>(a_ring.strikes)) *
		               std::pow(kRingMissQuality, static_cast<float>(a_ring.misses));
		if (a_ring.Perfect()) {
			factor *= kRingPerfectQuality;
		}
		if (a_ring.firstAreaMissed) {
			factor *= kRingBotchQuality;
		}
		return factor;
	}

	float QualityDurationMult(int a_quality)
	{
		const float q = static_cast<float>(std::clamp(a_quality, kMinQuality, kMaxQuality));
		return std::min(0.8f + q / 400.0f, 1.5f);
	}

	SynthesisOutcome Evaluate(const SynthesisInput& a_input, const Catalog& a_catalog, const RingSummary* a_ring, int a_baseTraitSlots)
	{
		SynthesisOutcome outcome;
		if (!a_input.recipe) {
			outcome.error = "No recipe selected.";
			return outcome;
		}
		const auto& recipe = *a_input.recipe;
		const bool  complete = ValidateMaterials(recipe, a_input.materials, &outcome.error);

		// --- elements and effect lines -------------------------------------------------
		outcome.elements = SumElements(a_input, a_catalog);

		int           tierQuality = 0;
		int           tierYield = 0;
		int           tierTraitSlots = 0;
		ElementValues tierElements{};
		outcome.lines.resize(recipe.lines.size());
		for (std::size_t i = 0; i < recipe.lines.size(); ++i) {
			const auto& line = recipe.lines[i];
			auto&       state = outcome.lines[i];
			state.value = At(outcome.elements, line.element);
			for (std::size_t t = 0; t < line.tiers.size(); ++t) {
				const auto& tier = line.tiers[t];
				if (state.value < tier.threshold) {
					break;
				}
				state.reachedTier = static_cast<int>(t);
				switch (tier.kind) {
				case EffectTier::Kind::kQuality:
					tierQuality += tier.amount;
					break;
				case EffectTier::Kind::kYield:
					tierYield += tier.amount;
					break;
				case EffectTier::Kind::kTraitSlot:
					tierTraitSlots += tier.amount;
					break;
				case EffectTier::Kind::kElement:
					At(tierElements, tier.element) += tier.amount;
					break;
				case EffectTier::Kind::kEffect:
					break;
				}
			}
		}

		// --- traits --------------------------------------------------------------------
		std::vector<std::string> available;
		auto                     collect = [&](const MaterialInput& a_material) {
            for (const auto& trait : a_material.traits) {
                if (std::ranges::find(available, trait) == available.end()) {
                    available.push_back(trait);
                }
            }
		};
		for (const auto& material : a_input.materials) {
			collect(material);
		}
		if (a_input.catalyst) {
			collect(*a_input.catalyst);
		}

		std::vector<std::string> chosen;
		for (const auto& id : a_input.chosenTraits) {
			const auto def = a_catalog.FindTrait(id);
			if (def && TraitAllowed(*def, recipe.kind) && std::ranges::find(available, id) != available.end() &&
				std::ranges::find(chosen, id) == chosen.end()) {
				chosen.push_back(id);
			}
		}
		outcome.traitSlots = std::clamp(a_baseTraitSlots + tierTraitSlots, 0, kMaxTraitSlots);
		outcome.finalTraits = FuseTraits(a_catalog, std::move(chosen));
		if (static_cast<int>(outcome.finalTraits.size()) > outcome.traitSlots) {
			outcome.finalTraits.resize(static_cast<std::size_t>(outcome.traitSlots));
		}

		float magnitudePct = 0.0f;
		float durationPct = 0.0f;
		float valuePct = 0.0f;
		float weightPct = 0.0f;
		int   traitQuality = 0;
		for (const auto& id : outcome.finalTraits) {
			if (const auto def = a_catalog.FindTrait(id)) {
				magnitudePct += def->magnitudePct;
				durationPct += def->durationPct;
				valuePct += def->valuePct;
				weightPct += def->weightPct;
				traitQuality += def->qualityBonus;
			}
		}
		magnitudePct = std::min(magnitudePct, kMaxTraitMagnitudePct);
		durationPct = std::min(durationPct, kMaxTraitDurationPct);

		// --- quality -------------------------------------------------------------------
		float qualitySum = 0.0f;
		int   qualityCount = 0;
		for (const auto& material : a_input.materials) {
			qualitySum += static_cast<float>(material.quality);
			++qualityCount;
		}
		int catalystQuality = 0;
		if (a_input.catalyst) {
			qualitySum += static_cast<float>(a_input.catalyst->quality);
			++qualityCount;
			if (const auto def = a_catalog.FindCatalyst(a_input.catalyst->itemKey)) {
				catalystQuality = def->qualityBonus;
			}
		}
		float quality = qualityCount > 0 ? qualitySum / static_cast<float>(qualityCount) : 0.0f;
		quality += static_cast<float>(catalystQuality + traitQuality + tierQuality);
		quality += a_input.stats.skill * 0.25f;

		int yield = recipe.baseYield + tierYield;
		if (a_ring) {
			quality *= RingQualityFactor(*a_ring);
			if (a_ring->Perfect()) {
				yield += 1;
			}
			if (a_ring->firstAreaMissed) {
				yield -= 1;
			}
		}
		outcome.quality = std::clamp(static_cast<int>(std::lround(quality)), kMinQuality, kMaxQuality);
		outcome.yield = std::max(yield, 1);

		// --- output item ---------------------------------------------------------------
		auto& brew = outcome.brew;
		brew.recipeId = recipe.id;
		brew.name = recipe.name;
		brew.kind = recipe.kind;
		brew.quality = outcome.quality;
		brew.traits = outcome.finalTraits;

		// Quality and traits; perks only for absolute tiers, vanilla references include them.
		const float qualityMult = QualityMagnitudeMult(outcome.quality);
		const float traitMult = std::max(0.0f, 1.0f + magnitudePct / 100.0f);
		const float potency = qualityMult * a_input.stats.globalMagnitudeMult * traitMult;
		const auto  factors = [&] {
			auto text = std::format("quality x{:.2f}, traits {:+.0f}%", qualityMult, magnitudePct);
			if (a_input.stats.globalMagnitudeMult != 1.0f) {
				text += std::format(", INI x{:.2f}", a_input.stats.globalMagnitudeMult);
			}
			return text;
		};
		const float durMult = QualityDurationMult(outcome.quality) * std::max(0.0f, 1.0f + durationPct / 100.0f);

		for (std::size_t i = 0; i < recipe.lines.size(); ++i) {
			const auto& line = recipe.lines[i];
			const int   reached = outcome.lines[i].reachedTier;
			const EffectTier* best = nullptr;
			for (int t = 0; t <= reached; ++t) {
				if (line.tiers[static_cast<std::size_t>(t)].kind == EffectTier::Kind::kEffect) {
					best = &line.tiers[static_cast<std::size_t>(t)];
				}
			}
			if (!best) {
				continue;
			}

			OutputEffect effect;
			effect.effectKey = best->effectKey;
			const auto ref = a_input.references.find(best->effectKey);
			if (best->vanillaScale > 0.0f && ref != a_input.references.end()) {
				const auto& r = ref->second;
				const float scale = best->vanillaScale * potency;
				if (r.scalesDuration) {
					// Fixed magnitude: tier, quality and traits all lengthen the effect.
					effect.magnitude = RoundMagnitude(r.magnitude);
					effect.duration = static_cast<int>(std::lround(r.duration * scale * std::max(0.0f, 1.0f + durationPct / 100.0f)));
					effect.breakdown = std::format("vanilla {:.0f}s x tier {:.0f}% ({}, duration traits {:+.0f}%)", r.duration,
						best->vanillaScale * 100.0f, factors(), durationPct);
				} else {
					effect.magnitude = RoundMagnitude(r.magnitude * scale);
					effect.duration = static_cast<int>(std::lround(r.duration * durMult));
					effect.breakdown = std::format("vanilla {:.1f} x tier {:.0f}% ({})", r.magnitude, best->vanillaScale * 100.0f, factors());
				}
			} else {
				effect.magnitude = RoundMagnitude(best->magnitude * potency * a_input.stats.magnitudeMult);
				effect.duration = static_cast<int>(std::lround(static_cast<float>(best->duration) * durMult));
				effect.breakdown = std::format("recipe {:.1f} x perks {:.2f} ({})", best->magnitude, a_input.stats.magnitudeMult, factors());
			}

			const auto existing = std::ranges::find(brew.effects, effect.effectKey, &OutputEffect::effectKey);
			if (existing == brew.effects.end()) {
				brew.effects.push_back(effect);
			} else {
				existing->magnitude = std::max(existing->magnitude, effect.magnitude);
				existing->duration = std::max(existing->duration, effect.duration);
			}
		}

		const float value = static_cast<float>(recipe.baseValue) * (0.5f + static_cast<float>(outcome.quality) / 100.0f) *
		                    std::max(0.1f, 1.0f + valuePct / 100.0f);
		brew.value = std::max(1, static_cast<int>(std::lround(value)));
		brew.weight = std::max(0.01f, recipe.baseWeight * std::max(0.1f, 1.0f + weightPct / 100.0f));

		if (recipe.kind == OutputKind::kMaterial) {
			for (std::size_t i = 0; i < kElementCount; ++i) {
				brew.elements[i] = recipe.outputElements[i] + tierElements[i];
			}
			brew.categories = recipe.outputCategories;
		}

		outcome.valid = complete && (recipe.kind == OutputKind::kMaterial || !brew.effects.empty());
		if (complete && !outcome.valid) {
			outcome.error = "No effect line has reached its first tier yet.";
		}
		return outcome;
	}

	std::vector<std::string> CheckUnlocks(const RecipeDef& a_recipe, const SynthesisInput& a_input, const SynthesisOutcome& a_outcome)
	{
		std::vector<const MaterialInput*> used;
		for (const auto& material : a_input.materials) {
			used.push_back(&material);
		}
		if (a_input.catalyst) {
			used.push_back(&*a_input.catalyst);
		}

		std::vector<std::string> unlocked;
		for (const auto& rule : a_recipe.unlocks) {
			if (!rule.useItem.empty() &&
				std::ranges::none_of(used, [&](const MaterialInput* m) { return m->itemKey == rule.useItem; })) {
				continue;
			}
			if (!rule.useCategory.empty() &&
				std::ranges::none_of(used, [&](const MaterialInput* m) { return m->HasCategory(rule.useCategory); })) {
				continue;
			}
			if (a_outcome.quality < rule.minQuality) {
				continue;
			}
			unlocked.push_back(rule.recipe);
		}
		return unlocked;
	}
}
