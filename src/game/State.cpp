#include "game/State.h"

#include "game/Config.h"
#include "game/Database.h"
#include "game/Forms.h"

namespace game
{
	namespace
	{
		// Local form id ranges of the slot records in AtelierAlchemy.esp (see tools/build_esp.py).
		constexpr RE::FormID kPotionBase = 0x800;
		constexpr RE::FormID kMaterialBase = 0xA00;
		constexpr int        kMaxSlots = 512;

		constexpr std::uint32_t kRecordLedger = 'LEDG';
		constexpr std::uint32_t kRecordSlots = 'SLOT';
		constexpr std::uint32_t kRecordKnown = 'KNOW';
		constexpr std::uint32_t kVersion = 1;

		std::mt19937& Rng()
		{
			static std::mt19937 rng{ std::random_device{}() };
			return rng;
		}

		// --- small co-save stream helpers ---------------------------------------------
		struct Writer
		{
			SKSE::SerializationInterface* intfc;

			template <class T>
				requires std::is_trivially_copyable_v<T>
			void Put(const T& a_value)
			{
				intfc->WriteRecordData(&a_value, sizeof(T));
			}
			void Put(const std::string& a_str)
			{
				Put(static_cast<std::uint32_t>(a_str.size()));
				if (!a_str.empty()) {
					intfc->WriteRecordData(a_str.data(), static_cast<std::uint32_t>(a_str.size()));
				}
			}
			void Put(const std::vector<std::string>& a_list)
			{
				Put(static_cast<std::uint32_t>(a_list.size()));
				for (const auto& s : a_list) {
					Put(s);
				}
			}
		};

		struct Reader
		{
			SKSE::SerializationInterface* intfc;
			bool                          ok{ true };

			template <class T>
				requires std::is_trivially_copyable_v<T>
			T Get()
			{
				T value{};
				if (ok && intfc->ReadRecordData(&value, sizeof(T)) != sizeof(T)) {
					ok = false;
				}
				return value;
			}
			std::string GetString()
			{
				const auto  size = Get<std::uint32_t>();
				std::string out;
				if (ok && size > 0) {
					if (size > (1u << 20)) {
						ok = false;
						return out;
					}
					out.resize(size);
					if (intfc->ReadRecordData(out.data(), size) != size) {
						ok = false;
					}
				}
				return out;
			}
			std::vector<std::string> GetStrings()
			{
				const auto               count = Get<std::uint32_t>();
				std::vector<std::string> out;
				for (std::uint32_t i = 0; ok && i < count; ++i) {
					out.push_back(GetString());
				}
				return out;
			}
		};

		void WriteBrew(Writer& a_out, const atelier::BrewData& a_brew)
		{
			a_out.Put(a_brew.recipeId);
			a_out.Put(a_brew.name);
			a_out.Put(static_cast<std::uint8_t>(a_brew.kind));
			a_out.Put(static_cast<std::int32_t>(a_brew.quality));
			a_out.Put(a_brew.traits);
			a_out.Put(static_cast<std::uint32_t>(a_brew.effects.size()));
			for (const auto& effect : a_brew.effects) {
				a_out.Put(effect.effectKey);
				a_out.Put(effect.magnitude);
				a_out.Put(static_cast<std::int32_t>(effect.duration));
			}
			a_out.Put(static_cast<std::int32_t>(a_brew.value));
			a_out.Put(a_brew.weight);
			for (const auto v : a_brew.elements) {
				a_out.Put(static_cast<std::int32_t>(v));
			}
			a_out.Put(a_brew.categories);
		}

		atelier::BrewData ReadBrew(Reader& a_in)
		{
			atelier::BrewData brew;
			brew.recipeId = a_in.GetString();
			brew.name = a_in.GetString();
			brew.kind = static_cast<atelier::OutputKind>(a_in.Get<std::uint8_t>());
			brew.quality = a_in.Get<std::int32_t>();
			brew.traits = a_in.GetStrings();
			const auto effectCount = a_in.Get<std::uint32_t>();
			for (std::uint32_t i = 0; a_in.ok && i < effectCount && i < 16; ++i) {
				atelier::OutputEffect effect;
				effect.effectKey = a_in.GetString();
				effect.magnitude = a_in.Get<float>();
				effect.duration = a_in.Get<std::int32_t>();
				brew.effects.push_back(std::move(effect));
			}
			brew.value = a_in.Get<std::int32_t>();
			brew.weight = a_in.Get<float>();
			for (auto& v : brew.elements) {
				v = a_in.Get<std::int32_t>();
			}
			brew.categories = a_in.GetStrings();
			return brew;
		}

		float EffectCost(const RE::EffectSetting* a_effect, float a_magnitude, int a_duration)
		{
			using Flag = RE::EffectSetting::EffectSettingData::Flag;
			float cost = a_effect->data.baseCost;
			if (!a_effect->data.flags.all(Flag::kNoMagnitude)) {
				cost *= std::pow(std::max(a_magnitude, 1.0f), 1.1f);
			}
			if (!a_effect->data.flags.all(Flag::kNoDuration) && a_duration > 0) {
				cost *= std::pow(static_cast<float>(std::max(a_duration, 10)) / 10.0f, 1.1f);
			}
			return cost;
		}

		// Vendor keywords decide which merchants buy an item. Looked up by editor ID
		// (keywords keep theirs at runtime) so the ESP needs no hard-coded form IDs.
		void SetVendorKeyword(RE::BGSKeywordForm* a_form, atelier::OutputKind a_kind)
		{
			static auto* potion = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("VendorItemPotion");
			static auto* poison = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("VendorItemPoison");
			static auto* ingredient = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("VendorItemIngredient");
			for (auto* keyword : { potion, poison, ingredient }) {
				if (keyword && a_form->HasKeyword(keyword)) {
					a_form->RemoveKeyword(keyword);
				}
			}
			auto* wanted = a_kind == atelier::OutputKind::kPotion ? potion : a_kind == atelier::OutputKind::kPoison ? poison : ingredient;
			if (wanted) {
				a_form->AddKeyword(wanted);
			}
		}

		std::string BrewDisplayName(const atelier::BrewData& a_brew)
		{
			return std::format("{} ({} Q{})", a_brew.name, atelier::QualityRank(a_brew.quality), a_brew.quality);
		}
	}

	State& State::Get()
	{
		static State state;
		return state;
	}

	void State::InitForms()
	{
		std::scoped_lock lock(_lock);
		auto*            dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler->LookupModByName(kPluginName)) {
			logger::critical("{} is not active - synthesized items cannot be created", kPluginName);
			return;
		}

		for (int i = 0; i < kMaxSlots; ++i) {
			auto* form = dataHandler->LookupForm<RE::AlchemyItem>(kPotionBase + static_cast<RE::FormID>(i), kPluginName);
			if (!form) {
				break;
			}
			_slotByForm[form->GetFormID()] = { SlotTable::kPotion, i };
			_potionForms.push_back(form);
			_defaultPotionNames.emplace_back(form->GetName());
			_originalEffects.push_back(form->effects.empty() ? nullptr : form->effects.front());
		}
		for (int i = 0; i < kMaxSlots; ++i) {
			auto* form = dataHandler->LookupForm<RE::TESObjectMISC>(kMaterialBase + static_cast<RE::FormID>(i), kPluginName);
			if (!form) {
				break;
			}
			_slotByForm[form->GetFormID()] = { SlotTable::kMaterial, i };
			_materialForms.push_back(form);
			_defaultMaterialNames.emplace_back(form->GetName());
		}

		_potions.assign(_potionForms.size(), {});
		_materials.assign(_materialForms.size(), {});
		_effectPool.assign(_potionForms.size(), {});
		logger::info("Brew slots: {} potion, {} material", _potionForms.size(), _materialForms.size());
	}

	// ---------------------------------------------------------------------------------
	// Ingredient ledger
	// ---------------------------------------------------------------------------------

	IngredientInstance State::RollInstance(const RE::IngredientItem* a_form) const
	{
		const auto& config = Config::Current();
		const auto  profile = Database::Get().FindIngredient(a_form->GetFormID());
		const float skill = RE::PlayerCharacter::GetSingleton()->AsActorValueOwner()->GetActorValue(RE::ActorValue::kAlchemy);

		IngredientInstance instance;
		const float        base = profile ? static_cast<float>(profile->baseQuality) : 25.0f;
		std::uniform_real_distribution<float> spread(0.75f, 1.3f);
		instance.quality = std::clamp(static_cast<int>(base * (1.0f + skill / 100.0f) * spread(Rng())), atelier::kMinQuality, atelier::kMaxQuality);

		if (profile && !profile->traitPool.empty()) {
			std::uniform_real_distribution<float> chance(0.0f, 1.0f);
			std::uniform_int_distribution<std::size_t> pick(0, profile->traitPool.size() - 1);
			if (chance(Rng()) < config.traitChance) {
				instance.traits.push_back(profile->traitPool[pick(Rng())]);
				if (chance(Rng()) < config.traitChance / 3.0f) {
					const auto& second = profile->traitPool[pick(Rng())];
					if (second != instance.traits.front()) {
						instance.traits.push_back(second);
					}
				}
			}
		}
		return instance;
	}

	void State::Reconcile(RE::IngredientItem* a_form, std::int32_t a_count)
	{
		if (!a_form) {
			return;
		}
		std::scoped_lock lock(_lock);
		auto&            list = _ledger[a_form->GetFormID()];
		const auto       target = static_cast<std::size_t>(std::max(a_count, 0));
		while (list.size() < target) {
			list.push_back(RollInstance(a_form));
		}
		// Keep the best first; when items disappear outside our control (eaten, sold,
		// used at a vanilla alchemy lab) we drop the worst ones.
		std::ranges::stable_sort(list, std::greater{}, &IngredientInstance::quality);
		if (list.size() > target) {
			list.resize(target);
		}
		if (list.empty()) {
			_ledger.erase(a_form->GetFormID());
		}
	}

	void State::ReconcileAll()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		const auto counts = player->GetInventoryCounts([](RE::TESBoundObject& a_obj) { return a_obj.Is(RE::FormType::Ingredient); });

		std::scoped_lock lock(_lock);
		std::unordered_set<RE::FormID> seen;
		for (const auto& [object, count] : counts) {
			if (auto* ingredient = object->As<RE::IngredientItem>()) {
				Reconcile(ingredient, count);
				seen.insert(ingredient->GetFormID());
			}
		}
		std::erase_if(_ledger, [&](const auto& a_entry) { return !seen.contains(a_entry.first); });
	}

	std::vector<IngredientInstance> State::Instances(RE::FormID a_form) const
	{
		std::scoped_lock lock(_lock);
		const auto       it = _ledger.find(a_form);
		return it != _ledger.end() ? it->second : std::vector<IngredientInstance>{};
	}

	bool State::TakeInstance(RE::FormID a_form, const IngredientInstance& a_instance)
	{
		std::scoped_lock lock(_lock);
		const auto       it = _ledger.find(a_form);
		if (it == _ledger.end()) {
			return false;
		}
		const auto pos = std::ranges::find(it->second, a_instance);
		if (pos == it->second.end()) {
			return false;
		}
		it->second.erase(pos);
		return true;
	}

	// ---------------------------------------------------------------------------------
	// Brew slots
	// ---------------------------------------------------------------------------------

	std::optional<SlotRef> State::FindSlot(RE::FormID a_form) const
	{
		std::scoped_lock lock(_lock);
		const auto       it = _slotByForm.find(a_form);
		if (it == _slotByForm.end() || !Table(it->second.table)[static_cast<std::size_t>(it->second.index)].used) {
			return std::nullopt;
		}
		return it->second;
	}

	std::optional<BrewSlot> State::GetSlot(SlotRef a_ref) const
	{
		std::scoped_lock lock(_lock);
		const auto&      table = Table(a_ref.table);
		if (a_ref.index < 0 || a_ref.index >= static_cast<int>(table.size())) {
			return std::nullopt;
		}
		return table[static_cast<std::size_t>(a_ref.index)];
	}

	RE::TESBoundObject* State::SlotForm(SlotRef a_ref) const
	{
		if (a_ref.index < 0) {
			return nullptr;
		}
		const auto i = static_cast<std::size_t>(a_ref.index);
		if (a_ref.table == SlotTable::kPotion) {
			return i < _potionForms.size() ? _potionForms[i] : nullptr;
		}
		return i < _materialForms.size() ? _materialForms[i] : nullptr;
	}

	std::optional<SlotRef> State::CreateBrew(const atelier::BrewData& a_brew)
	{
		std::scoped_lock lock(_lock);
		const auto       tableId = a_brew.kind == atelier::OutputKind::kMaterial ? SlotTable::kMaterial : SlotTable::kPotion;
		auto&            table = Table(tableId);

		int chosen = -1;
		for (std::size_t i = 0; i < table.size(); ++i) {
			if (!table[i].used) {
				chosen = static_cast<int>(i);
				break;
			}
		}

		if (chosen < 0) {
			// Recycle the oldest brew the player no longer carries. (Copies sold to a
			// merchant or left in a chest will change into the new brew - documented.)
			const auto counts = RE::PlayerCharacter::GetSingleton()->GetInventoryCounts();
			std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();
			for (std::size_t i = 0; i < table.size(); ++i) {
				const auto form = SlotForm({ tableId, static_cast<int>(i) });
				const auto it = counts.find(form);
				if ((it == counts.end() || it->second <= 0) && table[i].serial < oldest) {
					oldest = table[i].serial;
					chosen = static_cast<int>(i);
				}
			}
		}
		if (chosen < 0) {
			return std::nullopt;
		}

		auto& slot = table[static_cast<std::size_t>(chosen)];
		slot.used = true;
		slot.serial = _nextSerial++;
		slot.data = a_brew;
		const SlotRef ref{ tableId, chosen };
		ApplySlot(ref);
		return ref;
	}

	void State::ApplySlot(SlotRef a_ref)
	{
		const auto& slot = Table(a_ref.table)[static_cast<std::size_t>(a_ref.index)];
		if (!slot.used) {
			ResetSlotForm(a_ref);
			return;
		}

		const auto& brew = slot.data;
		const auto& db = Database::Get();
		const auto  recipe = db.Catalog().FindRecipe(brew.recipeId);
		const auto  form = SlotForm(a_ref);
		if (!form) {
			return;
		}

		if (recipe) {
			CopyPresentation(db.TemplateFor(*recipe, brew), form);
		}

		const auto name = BrewDisplayName(brew);
		if (a_ref.table == SlotTable::kMaterial) {
			auto* misc = form->As<RE::TESObjectMISC>();
			misc->fullName = name;
			misc->value = brew.value;
			misc->weight = brew.weight;
			SetVendorKeyword(misc, brew.kind);
			return;
		}

		auto* potion = form->As<RE::AlchemyItem>();
		auto& pool = _effectPool[static_cast<std::size_t>(a_ref.index)];
		potion->fullName = name;
		potion->effects.clear();
		potion->hostileCount = 0;

		std::size_t used = 0;
		for (const auto& effect : brew.effects) {
			const auto info = db.FindEffect(effect.effectKey);
			if (!info || used >= pool.size()) {
				continue;
			}
			auto*& entry = pool[used++];
			if (!entry) {
				entry = new RE::Effect();
			}
			entry->baseEffect = info->form;
			entry->effectItem.magnitude = effect.magnitude;
			entry->effectItem.duration = static_cast<std::uint32_t>(std::max(effect.duration, 0));
			entry->effectItem.area = 0;
			entry->cost = EffectCost(info->form, effect.magnitude, effect.duration);
			potion->effects.push_back(entry);
			if (info->form->IsHostile()) {
				++potion->hostileCount;
			}
		}

		if (potion->effects.empty()) {
			// An item without effects can crash the inventory UI; keep the ESP placeholder.
			if (const auto original = _originalEffects[static_cast<std::size_t>(a_ref.index)]) {
				potion->effects.push_back(original);
			}
		}

		using Flag = RE::AlchemyItem::AlchemyFlag;
		potion->data.flags.set(Flag::kCostOverride);
		potion->data.flags.reset(Flag::kFoodItem);
		if (brew.kind == atelier::OutputKind::kPoison) {
			potion->data.flags.set(Flag::kPoison);
		} else {
			potion->data.flags.reset(Flag::kPoison);
		}
		potion->data.costOverride = brew.value;
		potion->weight = brew.weight;
		SetVendorKeyword(potion, brew.kind);
	}

	void State::ResetSlotForm(SlotRef a_ref)
	{
		const auto form = SlotForm(a_ref);
		if (!form) {
			return;
		}
		const auto  i = static_cast<std::size_t>(a_ref.index);
		const auto& names = a_ref.table == SlotTable::kPotion ? _defaultPotionNames : _defaultMaterialNames;
		if (auto* named = form->As<RE::TESFullName>(); named && i < names.size()) {
			named->fullName = names[i];
		}
	}

	void State::ApplyAll()
	{
		std::scoped_lock lock(_lock);
		for (int i = 0; i < static_cast<int>(_potions.size()); ++i) {
			ApplySlot({ SlotTable::kPotion, i });
		}
		for (int i = 0; i < static_cast<int>(_materials.size()); ++i) {
			ApplySlot({ SlotTable::kMaterial, i });
		}
	}

	// ---------------------------------------------------------------------------------
	// Recipe knowledge
	// ---------------------------------------------------------------------------------

	bool State::Knows(std::string_view a_recipe) const
	{
		std::scoped_lock lock(_lock);
		return _known.contains(std::string(a_recipe));
	}

	std::vector<std::string> State::Learn(const std::string& a_recipe)
	{
		const auto& db = Database::Get();
		const auto& catalog = db.Catalog();
		std::vector<std::string> learned;
		std::vector<std::string> pending{ a_recipe };
		std::scoped_lock         lock(_lock);
		while (!pending.empty()) {
			const auto id = pending.back();
			pending.pop_back();
			// An already known recipe still has its dependencies checked (saves made before a
			// recipe gained a material requirement); already known dependencies stop the walk.
			if (_known.insert(id).second) {
				learned.push_back(id);
			} else if (id != a_recipe) {
				continue;
			}
			const auto recipe = catalog.FindRecipe(id);
			if (!recipe) {
				continue;
			}
			for (const auto& req : recipe->requirements) {
				if (!req.isCategory) {
					if (const auto dep = catalog.FindRecipe(req.key); dep && dep->kind == atelier::OutputKind::kMaterial) {
						pending.push_back(dep->id);
					}
				} else if (!db.CategoryHasIngredients(req.key)) {
					for (const auto& [depId, dep] : catalog.recipes) {
						if (dep.kind == atelier::OutputKind::kMaterial && std::ranges::find(dep.outputCategories, req.key) != dep.outputCategories.end()) {
							pending.push_back(depId);
						}
					}
				}
			}
		}
		return learned;
	}

	bool State::IsKnown(const std::string& a_recipe) const
	{
		std::scoped_lock lock(_lock);
		return _known.contains(a_recipe);
	}

	std::set<std::string> State::Known() const
	{
		std::scoped_lock lock(_lock);
		return _known;
	}

	std::vector<std::string> State::RefreshKnowledge(float a_alchemySkill)
	{
		std::vector<std::string> learned;
		for (const auto& [id, recipe] : Database::Get().Catalog().recipes) {
			const bool bySkill = recipe.unlockSkill >= 0 && a_alchemySkill >= static_cast<float>(recipe.unlockSkill);
			if (recipe.knownAtStart || bySkill || IsKnown(id)) {
				for (auto& newId : Learn(id)) {
					learned.push_back(std::move(newId));
				}
			}
		}
		return learned;
	}

	// ---------------------------------------------------------------------------------
	// Serialization
	// ---------------------------------------------------------------------------------

	void State::OnSave(SKSE::SerializationInterface* a_intfc)
	{
		auto&            self = Get();
		std::scoped_lock lock(self._lock);
		Writer           out{ a_intfc };

		if (a_intfc->OpenRecord(kRecordLedger, kVersion)) {
			out.Put(static_cast<std::uint32_t>(self._ledger.size()));
			for (const auto& [formID, list] : self._ledger) {
				out.Put(formID);
				out.Put(static_cast<std::uint32_t>(list.size()));
				for (const auto& instance : list) {
					out.Put(static_cast<std::int32_t>(instance.quality));
					out.Put(instance.traits);
				}
			}
		}

		if (a_intfc->OpenRecord(kRecordSlots, kVersion)) {
			out.Put(self._nextSerial);
			for (const auto table : { SlotTable::kPotion, SlotTable::kMaterial }) {
				const auto& slots = self.Table(table);
				std::uint32_t used = 0;
				for (const auto& slot : slots) {
					used += slot.used ? 1 : 0;
				}
				out.Put(used);
				for (std::size_t i = 0; i < slots.size(); ++i) {
					if (slots[i].used) {
						out.Put(static_cast<std::uint32_t>(i));
						out.Put(slots[i].serial);
						WriteBrew(out, slots[i].data);
					}
				}
			}
		}

		if (a_intfc->OpenRecord(kRecordKnown, kVersion)) {
			out.Put(std::vector<std::string>(self._known.begin(), self._known.end()));
		}
	}

	void State::OnLoad(SKSE::SerializationInterface* a_intfc)
	{
		auto&            self = Get();
		std::scoped_lock lock(self._lock);

		std::uint32_t type = 0;
		std::uint32_t version = 0;
		std::uint32_t length = 0;
		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (version != kVersion) {
				logger::warn("Skipping co-save record {:08X} with unknown version {}", type, version);
				continue;
			}
			Reader in{ a_intfc };
			switch (type) {
			case kRecordLedger:
				{
					const auto forms = in.Get<std::uint32_t>();
					for (std::uint32_t f = 0; in.ok && f < forms; ++f) {
						const auto oldID = in.Get<RE::FormID>();
						const auto count = in.Get<std::uint32_t>();
						std::vector<IngredientInstance> list;
						for (std::uint32_t i = 0; in.ok && i < count; ++i) {
							IngredientInstance instance;
							instance.quality = in.Get<std::int32_t>();
							instance.traits = in.GetStrings();
							list.push_back(std::move(instance));
						}
						RE::FormID newID = 0;
						if (in.ok && a_intfc->ResolveFormID(oldID, newID)) {
							self._ledger[newID] = std::move(list);
						}
					}
					break;
				}
			case kRecordSlots:
				{
					self._nextSerial = in.Get<std::uint64_t>();
					for (const auto table : { SlotTable::kPotion, SlotTable::kMaterial }) {
						auto&      slots = self.Table(table);
						const auto used = in.Get<std::uint32_t>();
						for (std::uint32_t n = 0; in.ok && n < used; ++n) {
							const auto index = in.Get<std::uint32_t>();
							const auto serial = in.Get<std::uint64_t>();
							auto       brew = ReadBrew(in);
							if (in.ok && index < slots.size()) {
								slots[index] = { true, serial, std::move(brew) };
							} else if (in.ok) {
								logger::warn("Saved brew '{}' refers to slot {} which this ESP does not have", brew.name, index);
							}
						}
					}
					break;
				}
			case kRecordKnown:
				for (auto& id : in.GetStrings()) {
					self._known.insert(std::move(id));
				}
				break;
			default:
				logger::warn("Unknown co-save record {:08X}", type);
				break;
			}
			if (!in.ok) {
				logger::error("Co-save record {:08X} was truncated", type);
			}
		}

		self.ApplyAll();
	}

	void State::OnRevert(SKSE::SerializationInterface*)
	{
		auto&            self = Get();
		std::scoped_lock lock(self._lock);
		self._ledger.clear();
		self._known.clear();
		self._nextSerial = 1;
		for (auto& slot : self._potions) {
			slot = {};
		}
		for (auto& slot : self._materials) {
			slot = {};
		}
		self.ApplyAll();
	}
}
