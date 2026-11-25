#include "game/Perks.h"

#include "game/Forms.h"

#include <map>
#include <set>

namespace game::Perks
{
	namespace
	{
		using EP = RE::BGSEntryPoint::ENTRY_POINT;
		using FID = RE::FUNCTION_DATA::FunctionID;

		constexpr std::array<std::pair<EP, std::string_view>, 6> kAlchemyEntryPoints{ {
			{ EP::kModAlchemyEffectiveness, "ModAlchemyEffectiveness" },
			{ EP::kModInitialIngredientEffectsLearned, "ModInitialIngredientEffectsLearned" },
			{ EP::kPurifyAlchemyIngredients, "PurifyAlchemyIngredients" },
			{ EP::kModPoisonDoseCount, "ModPoisonDoseCount" },
			{ EP::kModIngredientsHarvested, "ModIngredientsHarvested" },
			{ EP::kModPotionsCreated, "ModPotionsCreated" },
		} };

		std::optional<std::string_view> AlchemyEntryPointName(EP a_entryPoint)
		{
			for (const auto& [ep, name] : kAlchemyEntryPoints) {
				if (ep == a_entryPoint) {
					return name;
				}
			}
			return std::nullopt;
		}

		std::string_view FunctionName(RE::BGSEntryPointFunction::ENTRY_POINT_FUNCTION a_function)
		{
			using F = RE::BGSEntryPointFunction::ENTRY_POINT_FUNCTION;
			switch (a_function) {
			case F::kSetValue:
				return "Set";
			case F::kAddValue:
				return "Add";
			case F::kMultiplyValue:
				return "Multiply";
			case F::kAddRangeToValue:
				return "AddRange";
			case F::kAddActorValueMult:
				return "AddActorValueMult";
			case F::kAbsoluteValue:
				return "Absolute";
			case F::kNegativeAbsoluteValue:
				return "NegativeAbsolute";
			case F::kSetToActorValueMult:
				return "SetToActorValueMult";
			case F::kMultiplyActorValueMult:
				return "MultiplyActorValueMult";
			case F::kMultiplyOnePlusActorValueMult:
				return "MultiplyOnePlusActorValueMult";
			default:
				return "Other";
			}
		}

		std::string FormText(const void* a_param)
		{
			const auto* form = static_cast<const RE::TESForm*>(a_param);
			if (!form) {
				return "none";
			}
			const char* edid = form->GetFormEditorID();
			const char* name = form->GetName();
			return std::format("{}{}{}", FormKey(form), edid && *edid ? std::format(" '{}'", edid) : "", name && *name ? std::format(" \"{}\"", name) : "");
		}

		// Parameters are only dereferenced for functions known to take a form.
		std::string ParamText(FID a_function, void* const (&a_params)[2])
		{
			switch (a_function) {
			case FID::kEPAlchemyEffectHasKeyword:
			case FID::kHasKeyword:
			case FID::kWornHasKeyword:
			case FID::kHasMagicEffectKeyword:
			case FID::kHasPerk:
			case FID::kHasSpell:
			case FID::kGetIsID:
			case FID::kGetIsRace:
			case FID::kGetInFaction:
			case FID::kGetGlobalValue:
			case FID::kGetEquipped:
			case FID::kIsInList:
				return FormText(a_params[0]);
			case FID::kGetActorValue:
			case FID::kGetBaseActorValue:
				return ActorValueName(static_cast<RE::ActorValue>(reinterpret_cast<std::uintptr_t>(a_params[0])));
			default:
				return std::format("0x{:X}, 0x{:X}", reinterpret_cast<std::uintptr_t>(a_params[0]), reinterpret_cast<std::uintptr_t>(a_params[1]));
			}
		}

		void LogConditions(const RE::TESCondition& a_conditions, std::string_view a_label)
		{
			static constexpr std::array<std::string_view, 6> kOps{ "==", "!=", ">", ">=", "<", "<=" };
			for (auto* item = a_conditions.head; item; item = item->next) {
				const auto& d = item->data;
				const auto  function = d.functionData.function.get();
				const auto  op = kOps[std::min<std::size_t>(static_cast<std::size_t>(d.flags.opCode), kOps.size() - 1)];
				const auto  value = d.flags.global ? FormText(d.comparisonValue.g) : std::format("{}", d.comparisonValue.f);
				logger::info("      {} function {}({}) {} {}{}{}", a_label, static_cast<int>(function), ParamText(function, d.functionData.params), op, value,
					d.flags.isOR ? " OR" : "", d.flags.swapTarget ? " (swap subject)" : "");
			}
		}

		using Data = RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA;

		std::optional<float> OneValue(const RE::BGSEntryPointPerkEntry* a_entry)
		{
			if (!a_entry->functionData || a_entry->functionData->GetType() != Data::kOneValue) {
				return std::nullopt;
			}
			return static_cast<const RE::BGSEntryPointFunctionDataOneValue*>(a_entry->functionData)->data;
		}

		// Two-value data (actor value index, multiplier) has no CommonLib class; same layout as
		// the one-value class with a second float.
		struct FunctionDataTwoValue : RE::BGSEntryPointFunctionData
		{
			float data1;  // 08
			float data2;  // 0C
		};

		std::optional<std::pair<float, float>> TwoValue(const RE::BGSEntryPointPerkEntry* a_entry)
		{
			if (!a_entry->functionData || a_entry->functionData->GetType() != Data::kTwoValue) {
				return std::nullopt;
			}
			const auto* data = static_cast<const FunctionDataTwoValue*>(a_entry->functionData);
			return std::pair{ data->data1, data->data2 };
		}

		bool Compare(float a_lhs, const RE::CONDITION_ITEM_DATA& a_data)
		{
			using Op = RE::CONDITION_ITEM_DATA::OpCode;
			const float rhs = a_data.flags.global ? (a_data.comparisonValue.g ? a_data.comparisonValue.g->value : 0.0f) : a_data.comparisonValue.f;
			switch (a_data.flags.opCode) {
			case Op::kEqualTo:
				return a_lhs == rhs;
			case Op::kNotEqualTo:
				return a_lhs != rhs;
			case Op::kGreaterThan:
				return a_lhs > rhs;
			case Op::kGreaterThanOrEqualTo:
				return a_lhs >= rhs;
			case Op::kLessThan:
				return a_lhs < rhs;
			case Op::kLessThanOrEqualTo:
				return a_lhs <= rhs;
			}
			return false;
		}

		// The two alchemy-menu functions read the vanilla crafting menu's state, so they are
		// answered here for the effect being brewed. Everything else goes to the engine.
		bool ItemIsTrue(const RE::TESConditionItem* a_item, RE::PlayerCharacter* a_player, const RE::EffectSetting* a_effect, bool a_makingPoison)
		{
			const auto& d = a_item->data;
			switch (d.functionData.function.get()) {
			case FID::kEPAlchemyGetMakingPoison:
				return Compare(a_makingPoison ? 1.0f : 0.0f, d);
			case FID::kEPAlchemyEffectHasKeyword:
				{
					const auto* keyword = static_cast<const RE::BGSKeyword*>(d.functionData.params[0]);
					return Compare(a_effect && keyword && a_effect->HasKeyword(keyword) ? 1.0f : 0.0f, d);
				}
			default:
				{
					RE::ConditionCheckParams params(a_player, a_player);
					return a_item->IsTrue(params);
				}
			}
		}

		// Skyrim's condition logic: consecutive OR'ed items form a group, groups are AND'ed.
		bool ConditionsHold(const RE::TESCondition& a_conditions, RE::PlayerCharacter* a_player, const RE::EffectSetting* a_effect, bool a_makingPoison)
		{
			bool all = true;
			bool any = false;
			bool open = false;
			for (auto* item = a_conditions.head; item; item = item->next) {
				any = any || ItemIsTrue(item, a_player, a_effect, a_makingPoison);
				open = item->data.flags.isOR;
				if (!open) {
					all = all && any;
					any = false;
				}
			}
			return open ? all && any : all;
		}

		struct NumericEntry
		{
			RE::BGSPerk*                perk;
			RE::BGSEntryPointPerkEntry* entry;
		};

		// All perk entries of one entry point in the load order, in the engine's priority order.
		const std::vector<NumericEntry>& EntriesFor(EP a_entryPoint)
		{
			static std::map<EP, std::vector<NumericEntry>> cache;
			auto [it, inserted] = cache.try_emplace(a_entryPoint);
			if (inserted) {
				for (auto* perk : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::BGSPerk>()) {
					if (!perk) {
						continue;
					}
					for (auto* entry : perk->perkEntries) {
						if (entry && entry->GetType() == RE::PERK_ENTRY_TYPE::kEntryPoint) {
							auto* ep = static_cast<RE::BGSEntryPointPerkEntry*>(entry);
							if (ep->entryData.entryPoint.get() == a_entryPoint) {
								it->second.push_back({ perk, ep });
							}
						}
					}
				}
				std::ranges::stable_sort(it->second, {}, [](const NumericEntry& e) { return e.entry->header.priority; });
			}
			return it->second;
		}

		// Runs the player's entries of a numeric entry point on a_start, as the engine would.
		Potency Evaluate(EP a_entryPoint, float a_start, const RE::EffectSetting* a_effect, bool a_makingPoison)
		{
			using F = RE::BGSEntryPointFunction::ENTRY_POINT_FUNCTION;
			Potency out;
			out.multiplier = a_start;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return out;
			}
			auto* avs = player->AsActorValueOwner();
			for (const auto& [perk, entry] : EntriesFor(a_entryPoint)) {
				if (!player->HasPerk(perk)) {
					continue;
				}
				if (entry->conditions.size() > 0 && !ConditionsHold(entry->conditions[0], player, a_effect, a_makingPoison)) {
					continue;
				}
				const float before = out.multiplier;
				const auto  one = OneValue(entry);
				const auto  two = TwoValue(entry);
				const float avMult = two ? avs->GetActorValue(static_cast<RE::ActorValue>(static_cast<int>(two->first))) * two->second : 0.0f;
				switch (entry->entryData.function.get()) {
				case F::kSetValue:
					out.multiplier = one.value_or(out.multiplier);
					break;
				case F::kAddValue:
					out.multiplier += one.value_or(0.0f);
					break;
				case F::kMultiplyValue:
					out.multiplier *= one.value_or(1.0f);
					break;
				case F::kAddActorValueMult:
					out.multiplier += avMult;
					break;
				case F::kMultiplyActorValueMult:
					out.multiplier *= avMult;
					break;
				case F::kMultiplyOnePlusActorValueMult:
					out.multiplier *= 1.0f + avMult;
					break;
				case F::kSetToActorValueMult:
					out.multiplier = avMult;
					break;
				case F::kAbsoluteValue:
					out.multiplier = std::abs(out.multiplier);
					break;
				case F::kNegativeAbsoluteValue:
					out.multiplier = -std::abs(out.multiplier);
					break;
				default:
					continue;  // random ranges and non-numeric functions are not modelled
				}
				if (out.multiplier != before) {
					const char* name = perk->GetName();
					const auto  label = name && *name ? std::string(name) : FormKey(perk);
					out.breakdown += std::format("{}{} x{:.2f}", out.breakdown.empty() ? "" : ", ", label, before != 0.0f ? out.multiplier / before : out.multiplier);
				}
			}
			out.multiplier = std::max(out.multiplier, 0.0f);
			return out;
		}

		bool TouchesAlchemy(const RE::BGSPerk* a_perk)
		{
			return std::ranges::any_of(a_perk->perkEntries, [](const RE::BGSPerkEntry* a_entry) {
				if (!a_entry || a_entry->GetType() != RE::PERK_ENTRY_TYPE::kEntryPoint) {
					return false;
				}
				const auto* ep = static_cast<const RE::BGSEntryPointPerkEntry*>(a_entry);
				return AlchemyEntryPointName(ep->entryData.entryPoint.get()).has_value();
			});
		}

		void LogPerk(RE::BGSPerk* a_perk)
		{
			RE::BSString description;
			a_perk->GetDescription(description, a_perk);
			logger::info("  Perk {} - {}", FormText(a_perk), description.c_str());
			if (a_perk->nextPerk) {
				logger::info("    next rank: {}", FormText(a_perk->nextPerk));
			}
			LogConditions(a_perk->perkConditions, "perk condition:");
			for (auto* entry : a_perk->perkEntries) {
				if (!entry || entry->GetType() != RE::PERK_ENTRY_TYPE::kEntryPoint) {
					continue;
				}
				auto*      ep = static_cast<RE::BGSEntryPointPerkEntry*>(entry);
				const auto name = AlchemyEntryPointName(ep->entryData.entryPoint.get());
				if (!name) {
					continue;
				}
				const auto function = ep->entryData.function.get();
				std::string value = "-";
				if (const auto one = OneValue(ep)) {
					value = std::format("{}", *one);
				} else if (const auto two = TwoValue(ep)) {
					value = std::format("{} x {}", ActorValueName(static_cast<RE::ActorValue>(static_cast<int>(two->first))), two->second);
				}
				logger::info("    entry {} {} {} (rank {}, priority {}, {} condition tab(s))", *name, FunctionName(function), value, entry->header.rank,
					entry->header.priority, ep->conditions.size());
				for (std::uint32_t tab = 0; tab < ep->conditions.size(); ++tab) {
					LogConditions(ep->conditions[tab], std::format("tab {}:", tab));
				}
			}
		}
	}

	void DumpAlchemyPerks()
	{
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		int   count = 0;
		logger::info("Alchemy perks in this load order:");
		for (auto* perk : dataHandler->GetFormArray<RE::BGSPerk>()) {
			if (perk && TouchesAlchemy(perk)) {
				LogPerk(perk);
				++count;
			}
		}
		logger::info("{} alchemy perk(s)", count);

		// The tree shows how the perks are arranged, including ones that work through scripts.
		auto* info = RE::ActorValueList::GetActorValueInfo(RE::ActorValue::kAlchemy);
		if (!info || !info->perkTree) {
			return;
		}
		logger::info("Alchemy perk tree:");
		std::vector<RE::BGSSkillPerkTreeNode*> queue{ info->perkTree };
		std::set<RE::BGSSkillPerkTreeNode*>    seen{ info->perkTree };
		for (std::size_t i = 0; i < queue.size(); ++i) {
			auto* node = queue[i];
			if (node->perk) {
				std::string ranks;
				for (auto* rank = node->perk->nextPerk; rank && ranks.size() < 200; rank = rank->nextPerk) {
					ranks += " -> " + FormKey(rank);
				}
				RE::BSString description;
				node->perk->GetDescription(description, node->perk);
				logger::info("  [{},{}] {}{} - {}", node->perkGridX, node->perkGridY, FormText(node->perk), ranks, description.c_str());
			}
			for (auto* child : node->children) {
				if (child && seen.insert(child).second) {
					queue.push_back(child);
				}
			}
		}
	}

	void LogPlayerAlchemyPerks()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		std::string owned;
		for (auto* perk : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::BGSPerk>()) {
			if (perk && TouchesAlchemy(perk) && player->HasPerk(perk)) {
				owned += (owned.empty() ? "" : ", ") + FormText(perk);
			}
		}
		logger::info("Player alchemy perks: {}", owned.empty() ? "none" : owned);
	}

	Potency AlchemyEffectiveness(const RE::EffectSetting* a_effect, bool a_makingPoison)
	{
		return Evaluate(EP::kModAlchemyEffectiveness, 1.0f, a_effect, a_makingPoison);
	}

	Potency PotionsCreated(bool a_makingPoison)
	{
		// Vanilla makes one potion per brew; the result is how many that becomes.
		return Evaluate(EP::kModPotionsCreated, 1.0f, nullptr, a_makingPoison);
	}
}
