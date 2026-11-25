#include "game/Forms.h"

namespace game
{
	std::string Lower(std::string_view a_str)
	{
		std::string out(a_str);
		std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
	}

	bool IEquals(std::string_view a_lhs, std::string_view a_rhs)
	{
		return a_lhs.size() == a_rhs.size() && Lower(a_lhs) == Lower(a_rhs);
	}

	std::string Slugify(std::string_view a_name)
	{
		std::string out;
		for (const unsigned char c : a_name) {
			if (std::isalnum(c)) {
				out.push_back(static_cast<char>(std::tolower(c)));
			} else if ((c == ' ' || c == '-' || c == '_') && !out.empty() && out.back() != '_') {
				out.push_back('_');
			}
		}
		while (!out.empty() && out.back() == '_') {
			out.pop_back();
		}
		return out;
	}

	namespace
	{
		// Names follow CommonLib's enumerators without the leading 'k'.
		const std::vector<std::pair<std::string_view, RE::ActorValue>>& ActorValueTable()
		{
			using AV = RE::ActorValue;
			static const std::vector<std::pair<std::string_view, AV>> table = [] {
				std::vector<std::pair<std::string_view, AV>> t;
#define AV_ENTRY(name) t.emplace_back(#name##sv, AV::k##name)
			AV_ENTRY(OneHanded);
			AV_ENTRY(TwoHanded);
			AV_ENTRY(Archery);
			AV_ENTRY(Block);
			AV_ENTRY(Smithing);
			AV_ENTRY(HeavyArmor);
			AV_ENTRY(LightArmor);
			AV_ENTRY(Pickpocket);
			AV_ENTRY(Lockpicking);
			AV_ENTRY(Sneak);
			AV_ENTRY(Alchemy);
			AV_ENTRY(Speech);
			AV_ENTRY(Alteration);
			AV_ENTRY(Conjuration);
			AV_ENTRY(Destruction);
			AV_ENTRY(Illusion);
			AV_ENTRY(Restoration);
			AV_ENTRY(Enchanting);
			AV_ENTRY(Health);
			AV_ENTRY(Magicka);
			AV_ENTRY(Stamina);
			AV_ENTRY(HealRate);
			AV_ENTRY(MagickaRate);
			AV_ENTRY(StaminaRate);
			AV_ENTRY(HealRateMult);
			AV_ENTRY(MagickaRateMult);
			AV_ENTRY(StaminaRateMult);
			AV_ENTRY(SpeedMult);
			AV_ENTRY(CarryWeight);
			AV_ENTRY(DamageResist);
			AV_ENTRY(PoisonResist);
			AV_ENTRY(ResistFire);
			AV_ENTRY(ResistShock);
			AV_ENTRY(ResistFrost);
			AV_ENTRY(ResistMagic);
			AV_ENTRY(ResistDisease);
			AV_ENTRY(Paralysis);
			AV_ENTRY(Invisibility);
			AV_ENTRY(NightEye);
			AV_ENTRY(WaterBreathing);
			AV_ENTRY(WaterWalking);
			AV_ENTRY(MeleeDamage);
			AV_ENTRY(UnarmedDamage);
			AV_ENTRY(CriticalChance);
			AV_ENTRY(WeaponSpeedMult);
			AV_ENTRY(AttackDamageMult);
			AV_ENTRY(ReflectDamage);
			AV_ENTRY(OneHandedModifier);
			AV_ENTRY(TwoHandedModifier);
			AV_ENTRY(MarksmanModifier);
			AV_ENTRY(BlockModifier);
			AV_ENTRY(SmithingModifier);
			AV_ENTRY(HeavyArmorModifier);
			AV_ENTRY(LightArmorModifier);
			AV_ENTRY(PickpocketModifier);
			AV_ENTRY(LockpickingModifier);
			AV_ENTRY(SneakingModifier);
			AV_ENTRY(AlchemyModifier);
			AV_ENTRY(SpeechcraftModifier);
			AV_ENTRY(AlterationModifier);
			AV_ENTRY(ConjurationModifier);
			AV_ENTRY(DestructionModifier);
			AV_ENTRY(IllusionModifier);
			AV_ENTRY(RestorationModifier);
			AV_ENTRY(EnchantingModifier);
			AV_ENTRY(OneHandedPowerModifier);
			AV_ENTRY(TwoHandedPowerModifier);
			AV_ENTRY(MarksmanPowerModifier);
			AV_ENTRY(BlockPowerModifier);
			AV_ENTRY(SmithingPowerModifier);
			AV_ENTRY(HeavyArmorPowerModifier);
			AV_ENTRY(LightArmorPowerModifier);
			AV_ENTRY(PickpocketPowerModifier);
			AV_ENTRY(LockpickingPowerModifier);
			AV_ENTRY(SneakingPowerModifier);
			AV_ENTRY(AlchemyPowerModifier);
			AV_ENTRY(SpeechcraftPowerModifier);
			AV_ENTRY(AlterationPowerModifier);
			AV_ENTRY(ConjurationPowerModifier);
			AV_ENTRY(DestructionPowerModifier);
			AV_ENTRY(IllusionPowerModifier);
			AV_ENTRY(RestorationPowerModifier);
			AV_ENTRY(EnchantingPowerModifier);
#undef AV_ENTRY
				return t;
			}();
			return table;
		}
	}

	std::optional<RE::ActorValue> ParseActorValue(std::string_view a_name)
	{
		for (const auto& [name, av] : ActorValueTable()) {
			if (IEquals(name, a_name)) {
				return av;
			}
		}
		return std::nullopt;
	}

	std::string ActorValueName(RE::ActorValue a_av)
	{
		for (const auto& [name, av] : ActorValueTable()) {
			if (av == a_av) {
				return std::string(name);
			}
		}
		return a_av == RE::ActorValue::kNone ? "None" : std::format("#{}", static_cast<int>(a_av));
	}

	std::optional<RE::EffectArchetypes::ArchetypeID> ParseArchetype(std::string_view a_name)
	{
		using A = RE::EffectArchetypes::ArchetypeID;
		static const std::unordered_map<std::string, A> table = {
			{ "valuemodifier", A::kValueModifier },
			{ "peakvaluemodifier", A::kPeakValueModifier },
			{ "dualvaluemodifier", A::kDualValueModifier },
			{ "script", A::kScript },
			{ "curedisease", A::kCureDisease },
			{ "curepoison", A::kCurePoison },
			{ "cureparalysis", A::kCureParalysis },
			{ "invisibility", A::kInvisibility },
			{ "paralysis", A::kParalysis },
			{ "frenzy", A::kFrenzy },
			{ "calm", A::kCalm },
			{ "demoralize", A::kDemoralize },
			{ "nighteye", A::kNightEye },
			{ "light", A::kLight },
			{ "absorb", A::kAbsorb },
			{ "slowtime", A::kSlowTime },
			{ "etherealize", A::kEtherealize },
			{ "detectlife", A::kDetectLife },
		};
		const auto it = table.find(Lower(a_name));
		if (it == table.end()) {
			return std::nullopt;
		}
		return it->second;
	}

	RE::TESForm* ResolveFormRef(std::string_view a_ref)
	{
		if (const auto bar = a_ref.find('|'); bar != std::string_view::npos) {
			const auto plugin = a_ref.substr(0, bar);
			const auto idStr = std::string(a_ref.substr(bar + 1));
			try {
				const auto localID = static_cast<RE::FormID>(std::stoul(idStr, nullptr, 16));
				return RE::TESDataHandler::GetSingleton()->LookupForm(localID, plugin);
			} catch (...) {
				return nullptr;
			}
		}

		const auto colon = a_ref.find(':');
		if (colon == std::string_view::npos) {
			return nullptr;
		}
		const auto type = a_ref.substr(0, colon);
		const auto name = a_ref.substr(colon + 1);

		auto search = [&](auto& a_array) -> RE::TESForm* {
			for (auto* form : a_array) {
				if (form && IEquals(form->GetName(), name)) {
					return form;
				}
			}
			return nullptr;
		};

		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (IEquals(type, "INGR")) {
			return search(dataHandler->GetFormArray<RE::IngredientItem>());
		}
		if (IEquals(type, "ALCH")) {
			return search(dataHandler->GetFormArray<RE::AlchemyItem>());
		}
		if (IEquals(type, "MISC")) {
			return search(dataHandler->GetFormArray<RE::TESObjectMISC>());
		}
		return nullptr;
	}

	std::string FormKey(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return {};
		}
		const auto file = a_form->GetFile(0);
		return std::format("{}|0x{:06X}", file ? file->GetFilename() : "Dynamic"sv, a_form->GetLocalFormID());
	}

	void CopyPresentation(RE::TESForm* a_from, RE::TESBoundObject* a_to)
	{
		if (!a_from || !a_to) {
			return;
		}

		const auto fromModel = a_from->As<RE::TESModel>();
		const auto toModel = a_to->As<RE::TESModel>();
		if (fromModel && toModel && fromModel->GetModel() && *fromModel->GetModel()) {
			toModel->SetModel(fromModel->GetModel());
		}

		const auto fromIcon = a_from->As<RE::TESIcon>();
		const auto toIcon = a_to->As<RE::TESIcon>();
		if (fromIcon && toIcon) {
			toIcon->textureName = fromIcon->textureName;
		}

		const auto fromSounds = a_from->As<RE::BGSPickupPutdownSounds>();
		const auto toSounds = a_to->As<RE::BGSPickupPutdownSounds>();
		if (fromSounds && toSounds) {
			toSounds->pickupSound = fromSounds->pickupSound;
			toSounds->putdownSound = fromSounds->putdownSound;
		}

		const auto fromPotion = a_from->As<RE::AlchemyItem>();
		const auto toPotion = a_to->As<RE::AlchemyItem>();
		if (fromPotion && toPotion) {
			toPotion->data.consumptionSound = fromPotion->data.consumptionSound;
		}
	}
}
