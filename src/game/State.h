#pragma once

#include "core/Types.h"

#include <set>

namespace game
{
	// One ingredient in the player's inventory, with Atelier-style quality and traits.
	// Skyrim stacks ingredients without per-item data, so we keep a parallel ledger
	// per ingredient form and reconcile it with the real count.
	struct IngredientInstance
	{
		int                      quality{ 0 };
		std::vector<std::string> traits;

		auto operator<=>(const IngredientInstance&) const = default;
	};

	enum class SlotTable : std::uint8_t
	{
		kPotion,    // ALCH forms (potions and poisons)
		kMaterial,  // MISC forms (synthesized intermediates)
	};

	struct SlotRef
	{
		SlotTable table{ SlotTable::kPotion };
		int       index{ 0 };

		auto operator<=>(const SlotRef&) const = default;
	};

	// Each synthesis batch is written into one of a pool of generic forms shipped in
	// AtelierAlchemy.esp, so every brew is a distinct, save-persistent inventory item.
	struct BrewSlot
	{
		bool              used{ false };
		std::uint64_t     serial{ 0 };  // creation order, for recycling
		atelier::BrewData data;
	};

	class State
	{
	public:
		static State& Get();

		void InitForms();
		[[nodiscard]] bool FormsAvailable() const { return !_potionForms.empty(); }

		// --- ingredient ledger (call from the main thread) ---------------------------
		void                                          Reconcile(RE::IngredientItem* a_form, std::int32_t a_count);
		void                                          ReconcileAll();
		[[nodiscard]] std::vector<IngredientInstance> Instances(RE::FormID a_form) const;
		bool                                          TakeInstance(RE::FormID a_form, const IngredientInstance& a_instance);

		// --- brew slots -------------------------------------------------------------
		[[nodiscard]] std::optional<SlotRef>   FindSlot(RE::FormID a_form) const;
		[[nodiscard]] std::optional<BrewSlot>  GetSlot(SlotRef a_ref) const;
		[[nodiscard]] RE::TESBoundObject*      SlotForm(SlotRef a_ref) const;
		// Main thread only: allocates a free (or recyclable) slot and applies the brew to its form.
		std::optional<SlotRef> CreateBrew(const atelier::BrewData& a_brew);
		void                   ApplyAll();

		// --- recipe knowledge ---------------------------------------------------------
		[[nodiscard]] bool                  Knows(std::string_view a_recipe) const;
		bool                                Learn(const std::string& a_recipe);
		[[nodiscard]] std::set<std::string> Known() const;
		// Grants starting recipes and skill unlocks. Returns newly learned ids.
		std::vector<std::string> RefreshKnowledge(float a_alchemySkill);

		// --- SKSE co-save ------------------------------------------------------------
		static void OnSave(SKSE::SerializationInterface* a_intfc);
		static void OnLoad(SKSE::SerializationInterface* a_intfc);
		static void OnRevert(SKSE::SerializationInterface* a_intfc);

	private:
		IngredientInstance RollInstance(const RE::IngredientItem* a_form) const;
		void               ApplySlot(SlotRef a_ref);
		void               ResetSlotForm(SlotRef a_ref);
		std::vector<BrewSlot>&       Table(SlotTable a_table) { return a_table == SlotTable::kPotion ? _potions : _materials; }
		const std::vector<BrewSlot>& Table(SlotTable a_table) const { return a_table == SlotTable::kPotion ? _potions : _materials; }

		mutable std::recursive_mutex _lock;

		std::unordered_map<RE::FormID, std::vector<IngredientInstance>> _ledger;
		std::vector<BrewSlot>                                           _potions;
		std::vector<BrewSlot>                                           _materials;
		std::uint64_t                                                   _nextSerial{ 1 };
		std::set<std::string>                                           _known;

		// forms from the ESP, by slot index
		std::vector<RE::AlchemyItem*>                     _potionForms;
		std::vector<RE::TESObjectMISC*>                   _materialForms;
		std::unordered_map<RE::FormID, SlotRef>           _slotByForm;
		std::vector<std::array<RE::Effect*, 6>>           _effectPool;
		std::vector<RE::Effect*>                          _originalEffects;  // from the ESP, used if nothing resolves
		std::vector<std::string>                          _defaultPotionNames;
		std::vector<std::string>                          _defaultMaterialNames;
	};
}
