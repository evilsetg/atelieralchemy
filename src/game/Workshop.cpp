#include "game/Workshop.h"

#include "game/Config.h"
#include "game/Database.h"
#include "game/Forms.h"
#include "game/Perks.h"

namespace game
{
	namespace
	{
		bool IsAlchemyBench(const RE::TESForm* a_base)
		{
			const auto furniture = a_base ? a_base->As<RE::TESFurniture>() : nullptr;
			if (!furniture) {
				return false;
			}
			using Bench = RE::TESFurniture::WorkBenchData::BenchType;
			const auto type = furniture->workBenchData.benchType.get();
			return type == Bench::kAlchemy || type == Bench::kAlchemyExperiment;
		}

		std::vector<std::string> CategoriesForBrew(const atelier::BrewData& a_brew)
		{
			if (a_brew.kind == atelier::OutputKind::kMaterial) {
				return a_brew.categories;
			}
			return { a_brew.kind == atelier::OutputKind::kPoison ? "Poisons" : "Potions" };
		}
	}

	WorkshopSnapshot TakeSnapshot()
	{
		WorkshopSnapshot snapshot;
		auto*            player = RE::PlayerCharacter::GetSingleton();
		auto&            state = State::Get();
		const auto&      db = Database::Get();

		snapshot.skill = player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kAlchemy);
		Perks::LogPlayerAlchemyPerks();
		if (Config::Current().usePerks) {
			// Group identical results so the log and UI show each perk combination once.
			std::map<std::string, std::vector<std::string>> groups;
			for (const auto& [key, info] : db.Effects()) {
				for (const bool poison : { false, true }) {
					auto potency = Perks::AlchemyEffectiveness(info.form, poison);
					const auto label = std::format("{} x{:.2f}{}", poison ? "Poisons" : "Potions", potency.multiplier,
						potency.breakdown.empty() ? "" : std::format(" ({})", potency.breakdown));
					groups[label].push_back(key);
					(poison ? snapshot.poisonPotency : snapshot.potionPotency)[key] = std::move(potency);
				}
			}
			for (const auto& [label, keys] : groups) {
				std::string effects;
				for (const auto& key : keys) {
					effects += (effects.empty() ? "" : ", ") + key;
				}
				logger::info("Perk potency: {} for {}", label, effects);
				snapshot.potencySummary.push_back(std::format("{}: {}", label, effects));
			}
		}

		if (auto* settings = RE::GameSettingCollection::GetSingleton()) {
			if (auto* init = settings->GetSetting("fAlchemyIngredientInitMult")) {
				snapshot.ingredientInitMult = init->GetFloat();
			}
			if (auto* factor = settings->GetSetting("fAlchemySkillFactor")) {
				snapshot.skillFactor = factor->GetFloat();
			}
		}

		snapshot.newlyLearned = state.RefreshKnowledge(snapshot.skill);
		snapshot.known = state.Known();
		state.ReconcileAll();

		for (const auto& [object, count] : player->GetInventoryCounts()) {
			if (!object || count <= 0) {
				continue;
			}

			if (auto* ingredient = object->As<RE::IngredientItem>()) {
				const auto profile = db.FindIngredient(ingredient->GetFormID());
				if (!profile) {
					continue;
				}
				std::map<IngredientInstance, int> groups;
				for (const auto& instance : state.Instances(ingredient->GetFormID())) {
					++groups[instance];
				}
				for (const auto& [instance, n] : groups) {
					Candidate c;
					c.source = Candidate::Source::kIngredient;
					c.formID = ingredient->GetFormID();
					c.available = n;
					c.instance = instance;
					c.material.itemKey = profile->key;
					c.material.displayName = profile->name;
					c.material.categories = profile->categories;
					c.material.elements = profile->elements;
					c.material.quality = instance.quality;
					c.material.traits = instance.traits;
					c.isCatalyst = db.Catalog().FindCatalyst(profile->key) != nullptr;
					snapshot.candidates.push_back(std::move(c));
				}
				continue;
			}

			if (const auto slotRef = state.FindSlot(object->GetFormID())) {
				const auto slot = state.GetSlot(*slotRef);
				if (!slot) {
					continue;
				}
				Candidate c;
				c.source = Candidate::Source::kBrew;
				c.formID = object->GetFormID();
				c.available = count;
				c.slot = *slotRef;
				c.material.itemKey = slot->data.recipeId;
				c.material.displayName = slot->data.name;
				c.material.categories = CategoriesForBrew(slot->data);
				c.material.elements = slot->data.elements;
				c.material.quality = slot->data.quality;
				c.material.traits = slot->data.traits;
				c.isCatalyst = db.Catalog().FindCatalyst(slot->data.recipeId) != nullptr;
				snapshot.candidates.push_back(std::move(c));
			}
		}

		std::ranges::sort(snapshot.candidates, [](const Candidate& a, const Candidate& b) {
			if (a.material.displayName != b.material.displayName) {
				return a.material.displayName < b.material.displayName;
			}
			return a.material.quality > b.material.quality;
		});
		return snapshot;
	}

	atelier::AlchemistStats StatsFor(const WorkshopSnapshot& a_snapshot, const atelier::RecipeDef& a_recipe)
	{
		atelier::AlchemistStats stats;
		stats.skill = a_snapshot.skill;
		stats.globalMagnitudeMult = Config::Current().magnitudeMult;
		stats.valueMult = Config::Current().valueMult;

		// Only used by tiers with absolute values (add-ons): the perk potency of the recipe's
		// first effect. Vanilla-relative tiers get theirs per effect via EffectReferences.
		const auto& potencies = a_recipe.kind == atelier::OutputKind::kPoison ? a_snapshot.poisonPotency : a_snapshot.potionPotency;
		const float mult = [&] {
			for (const auto& line : a_recipe.lines) {
				for (const auto& tier : line.tiers) {
					if (const auto it = potencies.find(tier.effectKey); tier.kind == atelier::EffectTier::Kind::kEffect && it != potencies.end()) {
						return it->second.multiplier;
					}
				}
			}
			return 1.0f;
		}();
		stats.magnitudeMult = mult;
		return stats;
	}

	std::unordered_map<std::string, atelier::EffectReference> EffectReferences(const WorkshopSnapshot& a_snapshot, const atelier::RecipeDef& a_recipe)
	{
		// Vanilla potency: fAlchemyIngredientInitMult * (1 + (fAlchemySkillFactor - 1) * skill / 100)
		// * every "Mod Alchemy Effectiveness" perk entry (which also applies Fortify Alchemy).
		const float base = a_snapshot.ingredientInitMult * (1.0f + (a_snapshot.skillFactor - 1.0f) * a_snapshot.skill / 100.0f);
		const auto& potencies = a_recipe.kind == atelier::OutputKind::kPoison ? a_snapshot.poisonPotency : a_snapshot.potionPotency;

		std::unordered_map<std::string, atelier::EffectReference> refs;
		for (const auto& line : a_recipe.lines) {
			for (const auto& tier : line.tiers) {
				if (tier.kind != atelier::EffectTier::Kind::kEffect || refs.contains(tier.effectKey)) {
					continue;
				}
				const auto* info = Database::Get().FindEffect(tier.effectKey);
				if (!info || !info->HasBase()) {
					continue;
				}
				const auto  potency = potencies.find(tier.effectKey);
				const float power = base * (potency != potencies.end() ? potency->second.multiplier : 1.0f);

				atelier::EffectReference ref;
				ref.magnitude = info->baseMagnitude;
				ref.duration = static_cast<float>(info->baseDuration);
				ref.customDuration = info->baseDuration != info->vanillaDuration;
				if (info->powerAffectsMagnitude) {
					ref.magnitude *= power;
				} else if (info->powerAffectsDuration) {
					ref.duration *= power;
					ref.scalesDuration = true;
				}
				refs.emplace(tier.effectKey, ref);
			}
		}
		return refs;
	}

	atelier::SynthesisInput BuildInput(const WorkshopSnapshot& a_snapshot, const atelier::RecipeDef& a_recipe,
		const std::vector<int>& a_materials, int a_catalyst, const std::vector<std::string>& a_traits)
	{
		atelier::SynthesisInput input;
		input.recipe = &a_recipe;
		input.stats = StatsFor(a_snapshot, a_recipe);
		input.references = EffectReferences(a_snapshot, a_recipe);
		for (const auto& line : a_recipe.lines) {
			for (const auto& tier : line.tiers) {
				if (tier.kind == atelier::EffectTier::Kind::kEffect) {
					if (const auto* info = Database::Get().FindEffect(tier.effectKey)) {
						input.costs.emplace(tier.effectKey, info->CostInfo());
					}
				}
			}
		}
		input.chosenTraits = a_traits;
		for (const auto index : a_materials) {
			if (index >= 0 && index < static_cast<int>(a_snapshot.candidates.size())) {
				input.materials.push_back(a_snapshot.candidates[static_cast<std::size_t>(index)].material);
			}
		}
		if (a_catalyst >= 0 && a_catalyst < static_cast<int>(a_snapshot.candidates.size())) {
			input.catalyst = a_snapshot.candidates[static_cast<std::size_t>(a_catalyst)].material;
		}
		return input;
	}

	atelier::RingParams RingParamsFor(const WorkshopSnapshot& a_snapshot, const atelier::RecipeDef& a_recipe,
		const std::vector<int>& a_materials, int a_catalyst)
	{
		const auto& config = Config::Current();
		const auto& catalog = Database::Get().Catalog();

		float bonusWidth = 0.0f;
		float speedMult = config.ringSpeedMult;
		int   extraAreas = 0;
		auto  addTraits = [&](const atelier::MaterialInput& a_material) {
            for (const auto& id : a_material.traits) {
                if (const auto trait = catalog.FindTrait(id)) {
                    bonusWidth += trait->ringWidthDeg;
                }
            }
		};
		for (const auto index : a_materials) {
			if (index >= 0 && index < static_cast<int>(a_snapshot.candidates.size())) {
				addTraits(a_snapshot.candidates[static_cast<std::size_t>(index)].material);
			}
		}
		if (a_catalyst >= 0 && a_catalyst < static_cast<int>(a_snapshot.candidates.size())) {
			const auto& material = a_snapshot.candidates[static_cast<std::size_t>(a_catalyst)].material;
			addTraits(material);
			if (const auto def = catalog.FindCatalyst(material.itemKey)) {
				bonusWidth += def->ringWidthDeg;
				speedMult *= def->ringSpeedMult;
				extraAreas += def->extraRingAreas;
			}
		}

		// One area per distinct ingredient requirement, at least two: each is a stage
		// of the synthesis (Shadow Hearts rings have 2-5 areas).
		// A recipe's ring pattern sets the number, position and size of its areas; without one,
		// there is one area per distinct ingredient requirement plus one.
		auto shape = a_recipe.ringShape;
		if (!shape.empty()) {
			// Catalyst extra areas go, small, into the widest gaps of the pattern.
			for (int n = 0; n < extraAreas && shape.size() < 8; ++n) {
				std::vector<float> centers{ 0.0f, 1.0f };
				for (const auto& area : shape) {
					centers.push_back(area.center);
				}
				std::ranges::sort(centers);
				std::size_t widest = 0;
				for (std::size_t i = 1; i + 1 < centers.size(); ++i) {
					if (centers[i + 1] - centers[i] > centers[widest + 1] - centers[widest]) {
						widest = i;
					}
				}
				shape.push_back({ (centers[widest] + centers[widest + 1]) * 0.5f, 0.7f, 1.0f });
			}
		}
		const int areas = shape.empty() ? std::clamp(static_cast<int>(a_recipe.requirements.size()) + 1 + extraAreas, 2, 6) : static_cast<int>(shape.size());
		auto      params = atelier::MakeRingParams(areas, a_snapshot.skill, a_recipe.level, bonusWidth, speedMult);
		params.shape = std::move(shape);
		params.hitWidthDeg *= config.ringWidthMult;
		params.strikeWidthDeg *= config.ringWidthMult;
		params.missEndsRing = config.missEndsRing;
		switch (config.ringMode) {
		case RingMode::kEasy:
			params.degreesPerSecond *= 0.7f;
			params.hitWidthDeg *= 1.35f;
			params.strikeWidthDeg *= 1.5f;
			params.missEndsRing = false;
			break;
		case RingMode::kAuto:
			params.autoHit = true;
			break;
		case RingMode::kNormal:
			break;
		}
		// Re-clamp after multipliers so areas never overlap.
		const float maxWidth = 320.0f / static_cast<float>(params.areaCount) - 4.0f;
		params.hitWidthDeg = std::min(params.hitWidthDeg, maxWidth);
		params.strikeWidthDeg = std::min(params.strikeWidthDeg, params.hitWidthDeg * 0.5f);
		return params;
	}

	CommitResult Commit(const WorkshopSnapshot& a_snapshot, const CommitRequest& a_request)
	{
		CommitResult result;
		result.ring = a_request.ring;
		auto*       player = RE::PlayerCharacter::GetSingleton();
		auto&       state = State::Get();
		const auto& db = Database::Get();
		const auto  recipe = db.Catalog().FindRecipe(a_request.recipeId);
		if (!recipe) {
			result.error = "Unknown recipe.";
			return result;
		}

		// Tally how many units of each candidate are used and check they still exist.
		std::map<int, int> usage;
		for (const auto index : a_request.materials) {
			++usage[index];
		}
		if (a_request.catalyst >= 0) {
			++usage[a_request.catalyst];
		}
		const auto counts = player->GetInventoryCounts();
		std::unordered_map<RE::FormID, int> perForm;
		for (const auto& [index, n] : usage) {
			if (index < 0 || index >= static_cast<int>(a_snapshot.candidates.size())) {
				result.error = "Invalid material selection.";
				return result;
			}
			const auto& candidate = a_snapshot.candidates[static_cast<std::size_t>(index)];
			if (n > candidate.available) {
				result.error = std::format("Not enough {}.", candidate.material.displayName);
				return result;
			}
			perForm[candidate.formID] += n;
		}
		for (const auto& [formID, n] : perForm) {
			auto*      form = RE::TESForm::LookupByID<RE::TESBoundObject>(formID);
			const auto it = counts.find(form);
			if (!form || it == counts.end() || it->second < n) {
				result.error = "Some materials are no longer in your inventory.";
				return result;
			}
		}

		const auto input = BuildInput(a_snapshot, *recipe, a_request.materials, a_request.catalyst, a_request.traits);
		const auto outcome = atelier::Evaluate(input, db.Catalog(), &a_request.ring, Config::Current().baseTraitSlots);
		if (!outcome.valid) {
			result.error = outcome.error.empty() ? "The synthesis cannot be performed." : outcome.error;
			return result;
		}

		const auto slot = state.CreateBrew(outcome.brew);
		if (!slot) {
			result.error = "Your satchel holds too many different brews - sell, drop or use some first.";
			return result;
		}

		// Consume materials.
		for (const auto& [index, n] : usage) {
			const auto& candidate = a_snapshot.candidates[static_cast<std::size_t>(index)];
			auto*       form = RE::TESForm::LookupByID<RE::TESBoundObject>(candidate.formID);
			if (candidate.source == Candidate::Source::kIngredient) {
				for (int i = 0; i < n; ++i) {
					state.TakeInstance(candidate.formID, candidate.instance);
				}
			}
			player->RemoveItem(form, n, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		}

		// "Potions created" perks (Ordinator's Double Toil and Trouble, ...) multiply the batch
		// of a potion or poison, like they double a vanilla brew. Materials are unaffected.
		int yield = outcome.yield;
		if (recipe->kind != atelier::OutputKind::kMaterial && Config::Current().usePerks) {
			const auto created = Perks::PotionsCreated(recipe->kind == atelier::OutputKind::kPoison);
			yield = std::max(1, static_cast<int>(std::lround(static_cast<float>(outcome.yield) * created.multiplier)));
			result.yieldBonus = created.breakdown;
		}
		player->AddObjectToContainer(state.SlotForm(*slot), nullptr, yield, nullptr);

		for (const auto& id : atelier::CheckUnlocks(*recipe, input, outcome)) {
			for (auto& newId : state.Learn(id)) {
				result.unlocked.push_back(std::move(newId));
			}
		}

		// Vanilla alchemy awards experience equal to the potion's value; do the same per
		// batch, with diminishing credit for extra items and a bonus for a perfect ring.
		const float xp = std::max(10.0f, static_cast<float>(outcome.brew.value)) * (1.0f + 0.25f * static_cast<float>(outcome.yield - 1)) *
		                 (a_request.ring.Perfect() ? 1.25f : 1.0f) * Config::Current().xpMult;
		player->AddSkillExperience(RE::ActorValue::kAlchemy, xp);
		RE::PlaySound("UIAlchemyCreatePotion");

		result.ok = true;
		result.brew = outcome.brew;
		result.yield = yield;
		logger::info("Synthesized {}x {} (Q{}) from {}{}", result.yield, result.brew.name, result.brew.quality, recipe->id,
			result.yieldBonus.empty() ? "" : std::format(" [{}]", result.yieldBonus));
		for (const auto& effect : result.brew.effects) {
			logger::info("  {} magnitude {} duration {}s ({})", effect.effectKey, effect.magnitude, effect.duration,
				effect.breakdown.empty() ? "no breakdown" : effect.breakdown);
		}
		return result;
	}

	bool IsUsingAlchemyLab()
	{
		auto*      player = RE::PlayerCharacter::GetSingleton();
		const auto furniture = player ? player->GetOccupiedFurniture().get() : nullptr;
		return furniture && IsAlchemyBench(furniture->GetBaseObject());
	}

	bool CanSynthesizeHere(std::string* a_reason)
	{
		if (!State::Get().FormsAvailable()) {
			if (a_reason) {
				*a_reason = "AtelierAlchemy.esp is not active.";
			}
			return false;
		}
		if (!Config::Current().requireAlchemyLab || IsUsingAlchemyLab()) {
			return true;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		bool  found = false;
		if (auto* tes = RE::TES::GetSingleton(); tes && player) {
			tes->ForEachReferenceInRange(player, Config::Current().labSearchRadius, [&](RE::TESObjectREFR* a_ref) {
				if (a_ref && IsAlchemyBench(a_ref->GetBaseObject())) {
					found = true;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}
		if (!found && a_reason) {
			*a_reason = "You need to be near an alchemy lab.";
		}
		return found;
	}
}
