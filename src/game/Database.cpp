#include "game/Database.h"

#include "game/Config.h"
#include "game/Forms.h"

#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace game
{
	namespace
	{
		constexpr auto kDataDir = "Data/SKSE/Plugins/AtelierAlchemy";

		atelier::ElementValues ParseElements(const json& a_obj)
		{
			atelier::ElementValues values{};
			if (!a_obj.is_object()) {
				return values;
			}
			for (const auto& [name, value] : a_obj.items()) {
				if (const auto element = atelier::ParseElement(name); element && value.is_number()) {
					atelier::At(values, *element) += value.get<int>();
				}
			}
			return values;
		}

		std::vector<std::string> StringList(const json& a_obj, std::string_view a_key)
		{
			std::vector<std::string> out;
			const auto               it = a_obj.find(a_key);
			if (it == a_obj.end()) {
				return out;
			}
			if (it->is_string()) {
				out.push_back(it->get<std::string>());
			} else if (it->is_array()) {
				for (const auto& v : *it) {
					if (v.is_string()) {
						out.push_back(v.get<std::string>());
					}
				}
			}
			return out;
		}

		template <class T>
		T Value(const json& a_obj, std::string_view a_key, T a_default)
		{
			const auto it = a_obj.find(a_key);
			if (it == a_obj.end() || it->is_null()) {
				return a_default;
			}
			return it->get<T>();
		}
	}

	namespace
	{
		std::optional<json> ReadJson(const std::filesystem::path& a_path, std::vector<std::string>& a_errors)
		{
			std::ifstream file(a_path);
			if (!file) {
				return std::nullopt;
			}
			try {
				return json::parse(file, nullptr, true, true);  // allow comments
			} catch (const std::exception& e) {
				a_errors.push_back(std::format("{}: {}", a_path.filename().string(), e.what()));
				logger::error("{}", a_errors.back());
				return std::nullopt;
			}
		}
	}

	Database& Database::Get()
	{
		static Database db;
		return db;
	}

	void Database::Error(std::string a_message)
	{
		logger::error("{}", a_message);
		_errors.push_back(std::move(a_message));
	}

	bool Database::Load()
	{
		std::vector<std::filesystem::path> files;
		std::error_code                    ec;
		for (const auto& entry : std::filesystem::directory_iterator(kDataDir, ec)) {
			if (entry.is_regular_file() && Lower(entry.path().extension().string()) == ".json") {
				files.push_back(entry.path());
			}
		}
		if (files.empty()) {
			Error(std::format("No data files found in {}", kDataDir));
			return false;
		}
		std::ranges::sort(files);

		// Each file may contain any section; later files add to / override earlier ones,
		// so add-on packs can simply drop another .json next to the base files.
		for (const auto& file : files) {
			const auto doc = ReadJson(file, _errors);
			if (!doc) {
				continue;
			}
			const auto name = file.filename().string();
			LoadEffects(*doc, name);
			LoadTraits(*doc, name);
			LoadIngredients(*doc, name);
			LoadRecipes(*doc, name);
			LoadCatalysts(*doc, name);
		}

		ResolveEffects();
		BuildIngredientProfiles();
		MarkObtainable();
		BuildTemplates();
		Validate();

		logger::info("Database: {} recipes, {} traits, {} catalysts, {} effects, {} ingredients ({} errors)",
			_catalog.recipes.size(), _catalog.traits.size(), _catalog.catalysts.size(), _effects.size(),
			_ingredients.size(), _errors.size());
		return true;
	}

	// ---------------------------------------------------------------------------------
	// JSON sections
	// ---------------------------------------------------------------------------------

	void Database::LoadEffects(const json& a_doc, const std::string& a_file)
	{
		const auto* doc = &a_doc;

		if (const auto it = doc->find("effects"); it != doc->end() && it->is_object()) {
			for (const auto& [key, def] : it->items()) {
				EffectSignature sig;
				sig.name = Value<std::string>(def, "name", key);
				for (const auto& avName : StringList(def, "av")) {
					if (const auto av = ParseActorValue(avName)) {
						sig.avs.push_back(*av);
					} else {
						Error(std::format("{}: effect '{}' has unknown actor value '{}'", a_file, key, avName));
					}
				}
				if (const auto arch = def.find("archetype"); arch != def.end() && arch->is_string()) {
					sig.archetype = ParseArchetype(arch->get<std::string>());
					if (!sig.archetype) {
						Error(std::format("effect '{}' has unknown archetype '{}'", key, arch->get<std::string>()));
					}
				}
				if (const auto d = def.find("detrimental"); d != def.end() && d->is_boolean()) {
					sig.detrimental = d->get<bool>();
				}
				if (const auto r = def.find("recover"); r != def.end() && r->is_boolean()) {
					sig.recover = r->get<bool>();
				}
				sig.nameContains = Lower(Value<std::string>(def, "nameContains", ""));
				sig.formRef = Value<std::string>(def, "form", "");
				for (const auto& word : StringList(def, "nameExcludes")) {
					sig.nameExcludes.push_back(Lower(word));
				}
				_signatures[key] = std::move(sig);
			}
		}

		if (const auto it = doc->find("elementMap"); it != doc->end() && it->is_object()) {
			for (const auto& [key, value] : it->items()) {
				const auto element = value.is_string() ? atelier::ParseElement(value.get<std::string>()) : std::nullopt;
				if (!element) {
					Error(std::format("elementMap '{}': unknown element", key));
					continue;
				}
				if (key.starts_with("archetype:")) {
					if (const auto arch = ParseArchetype(key.substr(10))) {
						_archetypeElements[*arch] = *element;
					}
				} else if (const auto av = ParseActorValue(key)) {
					_avElements[*av] = *element;
				} else {
					Error(std::format("elementMap: unknown actor value '{}'", key));
				}
			}
		}
	}

	void Database::LoadTraits(const json& a_doc, const std::string& a_file)
	{
		const auto* doc = &a_doc;

		if (const auto it = doc->find("traits"); it != doc->end() && it->is_array()) {
			for (const auto& def : *it) {
				atelier::TraitDef trait;
				trait.id = Value<std::string>(def, "id", "");
				if (trait.id.empty()) {
					Error(std::format("{}: trait without id", a_file));
					continue;
				}
				trait.name = Value<std::string>(def, "name", trait.id);
				trait.description = Value<std::string>(def, "description", "");
				trait.grade = Value<int>(def, "grade", 1);
				trait.magnitudePct = Value<float>(def, "magnitude", 0.0f);
				trait.durationPct = Value<float>(def, "duration", 0.0f);
				trait.valuePct = Value<float>(def, "value", 0.0f);
				trait.weightPct = Value<float>(def, "weight", 0.0f);
				trait.qualityBonus = Value<int>(def, "quality", 0);
				trait.ringWidthDeg = Value<float>(def, "ringWidth", 0.0f);
				trait.potionsOnly = Value<bool>(def, "potionsOnly", false);
				trait.poisonsOnly = Value<bool>(def, "poisonsOnly", false);
				_catalog.traits[trait.id] = std::move(trait);
			}
		}

		if (const auto it = doc->find("traitCombos"); it != doc->end() && it->is_array()) {
			for (const auto& combo : *it) {
				if (combo.is_array() && combo.size() == 3) {
					_catalog.combos.push_back({ combo[0].get<std::string>(), combo[1].get<std::string>(), combo[2].get<std::string>() });
				}
			}
		}

		if (const auto it = doc->find("categoryTraitPools"); it != doc->end() && it->is_object()) {
			for (const auto& [category, list] : it->items()) {
				auto& pool = _categoryTraitPools[Lower(category)];
				for (const auto& id : list) {
					pool.push_back(id.get<std::string>());
				}
			}
		}
	}

	void Database::LoadIngredients(const json& a_doc, const std::string& a_file)
	{
		const auto* doc = &a_doc;

		if (const auto it = doc->find("categoryKeywords"); it != doc->end() && it->is_object()) {
			for (const auto& [category, list] : it->items()) {
				auto& keywords = _categoryKeywords[category];
				for (const auto& word : list) {
					keywords.push_back(Lower(word.get<std::string>()));
				}
			}
		}

		if (const auto it = doc->find("ingredients"); it != doc->end() && it->is_array()) {
			for (const auto& def : *it) {
				IngredientOverride entry;
				entry.name = Value<std::string>(def, "name", "");
				entry.formRef = Value<std::string>(def, "form", "");
				entry.key = Value<std::string>(def, "key", "");
				entry.categories = StringList(def, "categories");
				entry.traits = StringList(def, "traits");
				if (const auto e = def.find("elements"); e != def.end()) {
					entry.elementBonus = ParseElements(*e);
				}
				if (const auto r = def.find("rarity"); r != def.end() && r->is_number()) {
					entry.rarity = std::clamp(r->get<int>(), 0, 3);
				}
				if (entry.name.empty() && entry.formRef.empty()) {
					Error(std::format("{}: ingredient entry needs a name or form", a_file));
					continue;
				}
				_ingredientOverrides.push_back(std::move(entry));
			}
		}
	}

	void Database::LoadRecipes(const json& a_doc, const std::string& a_file)
	{
		const auto* doc = &a_doc;
		const auto it = doc->find("recipes");
		if (it == doc->end() || !it->is_array()) {
			return;
		}

		for (const auto& def : *it) {
			atelier::RecipeDef recipe;
			recipe.id = Value<std::string>(def, "id", "");
			if (recipe.id.empty()) {
				Error(std::format("{}: recipe without id", a_file));
				continue;
			}
			recipe.name = Value<std::string>(def, "name", recipe.id);
			recipe.description = Value<std::string>(def, "description", "");
			const auto kind = atelier::ParseOutputKind(Value<std::string>(def, "kind", "potion"));
			if (!kind) {
				Error(std::format("recipe '{}': unknown kind", recipe.id));
				continue;
			}
			recipe.kind = *kind;
			recipe.level = Value<int>(def, "level", 1);
			recipe.baseYield = std::max(1, Value<int>(def, "yield", 1));
			recipe.baseValue = Value<int>(def, "value", 10);
			recipe.baseWeight = Value<float>(def, "weight", 0.5f);
			recipe.templateRef = Value<std::string>(def, "template", "");
			recipe.knownAtStart = Value<bool>(def, "known", false);
			recipe.unlockSkill = Value<int>(def, "unlockSkill", -1);
			recipe.outputCategories = StringList(def, "categories");
			if (const auto e = def.find("elements"); e != def.end()) {
				recipe.outputElements = ParseElements(*e);
			}

			if (const auto reqs = def.find("ingredients"); reqs != def.end() && reqs->is_array()) {
				for (const auto& req : *reqs) {
					atelier::Requirement r;
					r.count = std::max(1, Value<int>(req, "count", 1));
					if (req.contains("category")) {
						r.isCategory = true;
						r.key = req["category"].get<std::string>();
					} else if (req.contains("item")) {
						r.isCategory = false;
						r.key = req["item"].get<std::string>();
					} else {
						Error(std::format("recipe '{}': ingredient needs 'category' or 'item'", recipe.id));
						continue;
					}
					recipe.requirements.push_back(std::move(r));
				}
			}

			if (const auto lines = def.find("lines"); lines != def.end() && lines->is_array()) {
				for (const auto& lineDef : *lines) {
					atelier::EffectLine line;
					line.name = Value<std::string>(lineDef, "name", "");
					const auto element = atelier::ParseElement(Value<std::string>(lineDef, "element", ""));
					if (!element) {
						Error(std::format("recipe '{}': line '{}' has unknown element", recipe.id, line.name));
						continue;
					}
					line.element = *element;
					for (const auto& tierDef : lineDef.value("tiers", json::array())) {
						atelier::EffectTier tier;
						tier.threshold = Value<int>(tierDef, "at", 0);
						tier.label = Value<std::string>(tierDef, "label", "");
						if (tierDef.contains("effect")) {
							tier.kind = atelier::EffectTier::Kind::kEffect;
							tier.effectKey = tierDef["effect"].get<std::string>();
							tier.magnitude = Value<float>(tierDef, "magnitude", 0.0f);
							tier.duration = Value<int>(tierDef, "duration", 0);
							// Relative to vanilla unless the tier gives absolute values; -1 = by tier position.
							tier.vanillaScale = Value<float>(tierDef, "vanilla", tierDef.contains("magnitude") ? 0.0f : -1.0f);
						} else if (tierDef.contains("quality")) {
							tier.kind = atelier::EffectTier::Kind::kQuality;
							tier.amount = tierDef["quality"].get<int>();
						} else if (tierDef.contains("yield")) {
							tier.kind = atelier::EffectTier::Kind::kYield;
							tier.amount = tierDef["yield"].get<int>();
						} else if (tierDef.contains("traitSlot")) {
							tier.kind = atelier::EffectTier::Kind::kTraitSlot;
							tier.amount = tierDef["traitSlot"].get<int>();
						} else if (tierDef.contains("element")) {
							const auto boost = atelier::ParseElement(tierDef["element"].get<std::string>());
							if (!boost) {
								Error(std::format("recipe '{}': tier has unknown element", recipe.id));
								continue;
							}
							tier.kind = atelier::EffectTier::Kind::kElement;
							tier.element = *boost;
							tier.amount = Value<int>(tierDef, "amount", 1);
						} else {
							Error(std::format("recipe '{}': tier at {} has no effect", recipe.id, tier.threshold));
							continue;
						}
						line.tiers.push_back(std::move(tier));
					}
					std::ranges::stable_sort(line.tiers, {}, &atelier::EffectTier::threshold);
					// Default vanilla shares by position among the line's tiers of the same effect:
					// 80%, 100%, 110%, then +10% per further tier.
					std::map<std::string, int> seen;
					for (auto& tier : line.tiers) {
						if (tier.kind == atelier::EffectTier::Kind::kEffect && tier.vanillaScale < 0.0f) {
							const int n = seen[tier.effectKey]++;
							tier.vanillaScale = n == 0 ? 0.8f : n == 1 ? 1.0f : 1.1f + 0.1f * static_cast<float>(n - 2);
						}
					}
					recipe.lines.push_back(std::move(line));
				}
			}

			if (const auto unlocks = def.find("unlocks"); unlocks != def.end() && unlocks->is_array()) {
				for (const auto& u : *unlocks) {
					atelier::UnlockRule rule;
					rule.recipe = Value<std::string>(u, "recipe", "");
					rule.useItem = Value<std::string>(u, "item", "");
					rule.useCategory = Value<std::string>(u, "category", "");
					rule.minQuality = Value<int>(u, "minQuality", 0);
					rule.hint = Value<std::string>(u, "hint", "");
					if (!rule.recipe.empty()) {
						recipe.unlocks.push_back(std::move(rule));
					}
				}
			}

			_catalog.recipes[recipe.id] = std::move(recipe);
		}
	}

	void Database::LoadCatalysts(const json& a_doc, const std::string& a_file)
	{
		const auto* doc = &a_doc;
		const auto it = doc->find("catalysts");
		if (it == doc->end() || !it->is_array()) {
			return;
		}
		for (const auto& def : *it) {
			atelier::CatalystDef catalyst;
			catalyst.itemKey = Value<std::string>(def, "item", "");
			if (catalyst.itemKey.empty()) {
				Error(std::format("{}: catalyst without item", a_file));
				continue;
			}
			catalyst.name = Value<std::string>(def, "name", "");
			catalyst.description = Value<std::string>(def, "description", "");
			if (const auto e = def.find("elements"); e != def.end()) {
				catalyst.addElements = ParseElements(*e);
			}
			catalyst.qualityBonus = Value<int>(def, "quality", 0);
			catalyst.ringWidthDeg = Value<float>(def, "ringWidth", 0.0f);
			catalyst.ringSpeedMult = Value<float>(def, "ringSpeed", 1.0f);
			catalyst.extraRingAreas = Value<int>(def, "extraAreas", 0);
			_catalog.catalysts[catalyst.itemKey] = std::move(catalyst);
		}
	}

	// ---------------------------------------------------------------------------------
	// Load order derived data
	// ---------------------------------------------------------------------------------

	void Database::ResolveEffects()
	{
		// Every magic effect used by an ingredient or potion, with how often it is used.
		// Picking the most common match makes signatures robust against odd mod effects.
		std::unordered_map<RE::EffectSetting*, int> usage;
		auto                                        count = [&](RE::MagicItem* a_item) {
            for (const auto* effect : a_item->effects) {
                if (effect && effect->baseEffect) {
                    ++usage[effect->baseEffect];
                }
            }
		};
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		for (auto* ingredient : dataHandler->GetFormArray<RE::IngredientItem>()) {
			if (ingredient) {
				count(ingredient);
			}
		}
		for (auto* potion : dataHandler->GetFormArray<RE::AlchemyItem>()) {
			if (potion && !potion->IsFood()) {
				count(potion);
			}
		}

		// Reference list for effects.json: every alchemy magic effect in the load order.
		std::vector<std::pair<RE::EffectSetting*, int>> sorted(usage.begin(), usage.end());
		std::ranges::sort(sorted, [](const auto& a, const auto& b) { return a.first->GetFormID() < b.first->GetFormID(); });
		logger::info("Alchemy magic effects in this load order ({}):", sorted.size());
		for (const auto& [mgef, uses] : sorted) {
			const auto& data = mgef->data;
			logger::info("  \"form\": \"{}\"  {:<28} av {}, archetype {}, detrimental {}, recover {}, used {}x", FormKey(mgef), mgef->GetName(),
				ActorValueName(data.primaryAV), static_cast<int>(data.archetype), mgef->IsDetrimental(),
				data.flags.all(RE::EffectSetting::EffectSettingData::Flag::kRecover), uses);
		}

		for (const auto& [key, sig] : _signatures) {
			// An explicit form wins. Overhauls usually edit vanilla effects in place, so a
			// vanilla FormID stays correct in every language and with most effect mods.
			if (!sig.formRef.empty()) {
				auto* form = ResolveFormRef(sig.formRef);
				if (auto* mgef = form ? form->As<RE::EffectSetting>() : nullptr) {
					const char* gameName = mgef->GetName();
					EffectInfo  info;
					info.key = key;
					info.form = mgef;
					info.name = gameName && *gameName ? gameName : sig.name;
					logger::info("Effect '{}' -> {} [{:08X}] (by form)", key, info.name, mgef->GetFormID());
					_effects[key] = std::move(info);
					continue;
				}
				logger::warn("Effect '{}': form {} is not a magic effect in this load order, falling back to matching", key, sig.formRef);
			}

			RE::EffectSetting* best = nullptr;
			int                bestCount = 0;
			for (const auto& [mgef, uses] : usage) {
				const auto& data = mgef->data;
				if (!sig.avs.empty() && std::ranges::find(sig.avs, data.primaryAV) == sig.avs.end()) {
					continue;
				}
				if (sig.archetype && data.archetype != *sig.archetype) {
					continue;
				}
				if (sig.detrimental && mgef->IsDetrimental() != *sig.detrimental) {
					continue;
				}
				if (sig.recover && data.flags.all(RE::EffectSetting::EffectSettingData::Flag::kRecover) != *sig.recover) {
					continue;
				}
				if (!sig.nameContains.empty() || !sig.nameExcludes.empty()) {
					const char* raw = mgef->GetName();
					const auto  name = Lower(raw ? raw : "");
					if ((!sig.nameContains.empty() && name.find(sig.nameContains) == std::string::npos) ||
						std::ranges::any_of(sig.nameExcludes, [&](const std::string& w) { return name.find(w) != std::string::npos; })) {
						continue;
					}
				}
				if (uses > bestCount || (uses == bestCount && best && mgef->GetFormID() < best->GetFormID())) {
					best = mgef;
					bestCount = uses;
				}
			}

			if (!best) {
				logger::warn("Effect '{}' could not be matched to any magic effect in the load order", key);
				// List similarly named effects with the properties a signature can match on,
				// so effects.json can be fixed from the log alone.
				const auto wanted = Lower(sig.name);
				for (const auto& [mgef, uses] : usage) {
					const char* raw = mgef->GetName();
					if (raw && Lower(raw).find(wanted) != std::string::npos) {
						const auto& data = mgef->data;
						logger::warn("  candidate {} [{:08X}]: av {}, archetype {}, detrimental {}, recover {} (used {}x)", raw, mgef->GetFormID(),
							static_cast<int>(data.primaryAV), static_cast<int>(data.archetype), mgef->IsDetrimental(),
							data.flags.all(RE::EffectSetting::EffectSettingData::Flag::kRecover), uses);
					}
				}
				continue;
			}
			EffectInfo info;
			info.key = key;
			info.form = best;
			const char* gameName = best->GetName();
			info.name = gameName && *gameName ? gameName : sig.name;
			logger::info("Effect '{}' -> {} [{:08X}] (used {}x)", key, info.name, best->GetFormID(), bestCount);
			_effects[key] = std::move(info);
		}

		// Vanilla baselines, so recipe tiers can be expressed relative to vanilla alchemy.
		// The most common base values among the ingredients carrying the effect, not the
		// strongest: one unusual ingredient (often from a mod) must not raise the bar.
		using Values = std::pair<float, int>;  // magnitude, duration
		auto collect = [](const RE::EffectSetting* a_mgef, RE::MagicItem* a_item, std::map<Values, int>& a_counts) {
			for (const auto* effect : a_item->effects) {
				if (effect && effect->baseEffect == a_mgef) {
					++a_counts[{ effect->effectItem.magnitude, static_cast<int>(effect->effectItem.duration) }];
				}
			}
		};
		for (auto& [key, info] : _effects) {
			const auto flags = info.form->data.flags;
			info.powerAffectsMagnitude = flags.all(RE::EffectSetting::EffectSettingData::Flag::kPowerAffectsMagnitude);
			info.powerAffectsDuration = flags.all(RE::EffectSetting::EffectSettingData::Flag::kPowerAffectsDuration);

			std::map<Values, int> counts;
			const char*           source = "ingredients";
			for (auto* ingredient : dataHandler->GetFormArray<RE::IngredientItem>()) {
				if (ingredient) {
					collect(info.form, ingredient, counts);
				}
			}
			if (counts.empty()) {
				source = "potions";
				for (auto* potion : dataHandler->GetFormArray<RE::AlchemyItem>()) {
					if (potion && !potion->IsFood()) {
						collect(info.form, potion, counts);
					}
				}
			}
			if (counts.empty()) {
				logger::warn("Effect '{}' has no vanilla base values; its recipe tiers use their own magnitude", key);
				continue;
			}

			// std::map is ordered by value, so on a tie the weaker values win.
			const auto mode = std::ranges::max_element(counts, [](const auto& a, const auto& b) { return a.second < b.second; });
			info.baseMagnitude = mode->first.first;
			info.baseDuration = mode->first.second;
			int total = 0;
			for (const auto& [values, n] : counts) {
				total += n;
			}
			info.baseSource = std::format("{} of {} {}", mode->second, total, source);
			logger::info("Effect '{}' vanilla base: magnitude {}, duration {}s ({}; strongest {} / {}s), potency scales {}", key, info.baseMagnitude,
				info.baseDuration, info.baseSource, counts.rbegin()->first.first, counts.rbegin()->first.second,
				info.powerAffectsMagnitude ? "magnitude" : info.powerAffectsDuration ? "duration" : "nothing");
		}
	}

	std::optional<atelier::Element> Database::ElementForEffect(const RE::EffectSetting* a_effect) const
	{
		if (!a_effect) {
			return std::nullopt;
		}
		if (const auto it = _avElements.find(a_effect->data.primaryAV); it != _avElements.end()) {
			return it->second;
		}
		if (const auto it = _archetypeElements.find(a_effect->data.archetype); it != _archetypeElements.end()) {
			return it->second;
		}
		return static_cast<atelier::Element>(a_effect->GetFormID() % atelier::kElementCount);
	}

	void Database::BuildIngredientProfiles()
	{
		const auto& config = Config::Current();
		static constexpr std::array<int, 4> kRarityQuality{ 20, 35, 55, 80 };

		// Index overrides by lowercase name / resolved form.
		std::unordered_map<std::string, const IngredientOverride*> byName;
		std::unordered_map<RE::FormID, const IngredientOverride*> byForm;
		for (const auto& entry : _ingredientOverrides) {
			if (!entry.formRef.empty()) {
				if (const auto form = ResolveFormRef(entry.formRef)) {
					byForm[form->GetFormID()] = &entry;
				}
			}
			if (!entry.name.empty()) {
				byName[Lower(entry.name)] = &entry;
			}
		}

		for (auto* form : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::IngredientItem>()) {
			if (!form || !form->GetName() || !*form->GetName()) {
				continue;
			}

			IngredientProfile profile;
			profile.form = form;
			profile.name = form->GetName();
			if (const auto* file = form->GetFile(0)) {
				profile.plugin = std::string(file->GetFilename());
			}

			const IngredientOverride* entry = nullptr;
			if (const auto it = byForm.find(form->GetFormID()); it != byForm.end()) {
				entry = it->second;
			} else if (const auto it2 = byName.find(Lower(profile.name)); it2 != byName.end()) {
				entry = it2->second;
			}

			profile.key = entry && !entry->key.empty() ? entry->key : Slugify(profile.name);

			const int value = form->GetGoldValue();
			profile.rarity = value <= 2 ? 0 : value <= 8 ? 1 : value <= 30 ? 2 : 3;
			if (entry && entry->rarity) {
				profile.rarity = *entry->rarity;
			}
			profile.baseQuality = static_cast<int>(static_cast<float>(kRarityQuality[static_cast<std::size_t>(profile.rarity)]) * config.ingredientQualityMult);

			// Elements come from the ingredient's own alchemical effects, so every
			// ingredient - including ones added by other mods - gets a sensible profile.
			bool first = true;
			for (const auto* effect : form->effects) {
				if (!effect) {
					continue;
				}
				if (const auto element = ElementForEffect(effect->baseEffect)) {
					atelier::At(profile.elements, *element) += 1 + (first ? profile.rarity : 0);
					first = false;
				}
			}
			if (entry) {
				for (std::size_t i = 0; i < atelier::kElementCount; ++i) {
					profile.elements[i] = std::max(0, profile.elements[i] + entry->elementBonus[i]);
				}
			}

			if (entry && !entry->categories.empty()) {
				profile.categories = entry->categories;
			} else {
				std::string haystack = Lower(profile.name);
				if (const auto model = form->GetModel()) {
					haystack += ' ';
					haystack += Lower(model);
				}
				for (const auto& [category, keywords] : _categoryKeywords) {
					if (std::ranges::any_of(keywords, [&](const std::string& k) { return haystack.find(k) != std::string::npos; })) {
						profile.categories.push_back(category);
					}
				}
				if (profile.categories.empty()) {
					profile.categories.push_back("Misc");
				}
			}

			if (entry) {
				profile.traitPool = entry->traits;
			}
			for (const auto& category : profile.categories) {
				for (const auto& trait : CategoryTraitPool(category)) {
					if (std::ranges::find(profile.traitPool, trait) == profile.traitPool.end()) {
						profile.traitPool.push_back(trait);
					}
				}
			}

			_ingredientsByKey[profile.key] = form->GetFormID();
			_ingredients[form->GetFormID()] = std::move(profile);
		}
	}

	void Database::MarkObtainable()
	{
		// Vanilla, the DLCs and AE's Creation Club content are trusted: their ingredients are
		// obtainable even when they come from scripts or single placed items this can't see.
		const auto official = [](std::string_view a_plugin) {
			static constexpr std::array<std::string_view, 5> kMasters{ "skyrim.esm", "update.esm", "dawnguard.esm", "hearthfires.esm", "dragonborn.esm" };
			const auto lower = Lower(a_plugin);
			return std::ranges::find(kMasters, lower) != kMasters.end() || lower.starts_with("cc");
		};

		// Everything else must be reachable: carried by an NPC or container, produced by a
		// plant, crafted, or on a leveled list one of those (or an official plugin) uses. Asset
		// masters define ingredients and lists for content that isn't installed; nothing
		// reaches those.
		auto*                                 dataHandler = RE::TESDataHandler::GetSingleton();
		std::unordered_set<const RE::TESForm*> reached;
		std::vector<const RE::TESLevItem*>    pending;
		const auto reach = [&](const RE::TESForm* a_form) {
			if (a_form && reached.insert(a_form).second) {
				if (const auto* list = a_form->As<RE::TESLevItem>()) {
					pending.push_back(list);
				}
			}
		};
		const auto reachContainer = [&](const RE::TESContainer* a_container) {
			for (std::uint32_t i = 0; a_container && i < a_container->numContainerObjects; ++i) {
				if (const auto* entry = a_container->containerObjects[i]) {
					reach(entry->obj);
				}
			}
		};

		for (const auto* container : dataHandler->GetFormArray<RE::TESObjectCONT>()) {
			reachContainer(container);
		}
		for (const auto* npc : dataHandler->GetFormArray<RE::TESNPC>()) {
			if (npc) {
				reachContainer(npc);
				reach(npc->deathItem);
			}
		}
		for (const auto* flora : dataHandler->GetFormArray<RE::TESFlora>()) {
			if (flora) {
				reach(flora->produceItem);
			}
		}
		for (const auto* tree : dataHandler->GetFormArray<RE::TESObjectTREE>()) {
			if (tree) {
				reach(tree->produceItem);
			}
		}
		for (const auto* recipe : dataHandler->GetFormArray<RE::BGSConstructibleObject>()) {
			if (recipe) {
				reach(recipe->createdItem);
			}
		}
		for (const auto* list : dataHandler->GetFormArray<RE::TESLevItem>()) {
			if (const auto* file = list ? list->GetFile(0) : nullptr; file && official(file->GetFilename())) {
				reach(list);
			}
		}
		while (!pending.empty()) {
			const auto* list = pending.back();
			pending.pop_back();
			for (const auto& entry : list->entries) {
				reach(entry.form);
			}
		}

		std::map<std::string, int> hidden;
		for (auto& [formID, profile] : _ingredients) {
			profile.obtainable = official(profile.plugin) || reached.contains(profile.form);
			if (!profile.obtainable) {
				++hidden[profile.plugin];
			}
		}
		for (const auto& [plugin, count] : hidden) {
			logger::info("{} ingredient(s) from {} are not carried, grown, crafted or looted anywhere; hidden in the category list", count, plugin);
		}
	}

	void Database::BuildTemplates()
	{
		for (auto* potion : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::AlchemyItem>()) {
			if (!potion || potion->IsFood() || !potion->GetModel() || !*potion->GetModel()) {
				continue;
			}
			const auto file = potion->GetFile(0);
			if (!file || !IEquals(file->GetFilename(), "Skyrim.esm")) {
				continue;
			}
			if (const auto costliest = potion->GetCostliestEffectItem(); costliest && costliest->baseEffect) {
				_potionTemplates.try_emplace(costliest->baseEffect, potion);
			}
			auto& fallback = potion->IsPoison() ? _fallbackPoison : _fallbackPotion;
			if (!fallback) {
				fallback = potion;
			}
		}
	}

	void Database::Validate()
	{
		for (const auto& [id, recipe] : _catalog.recipes) {
			for (const auto& line : recipe.lines) {
				for (const auto& tier : line.tiers) {
					if (tier.kind == atelier::EffectTier::Kind::kEffect && !FindEffect(tier.effectKey)) {
						Error(std::format("recipe '{}': effect '{}' is not available in this load order", id, tier.effectKey));
					}
				}
			}
			for (const auto& rule : recipe.unlocks) {
				if (!_catalog.FindRecipe(rule.recipe)) {
					Error(std::format("recipe '{}': unlocks unknown recipe '{}'", id, rule.recipe));
				}
			}
			for (const auto& req : recipe.requirements) {
				if (!req.isCategory && !FindIngredientByKey(req.key) && !_catalog.FindRecipe(req.key)) {
					Error(std::format("recipe '{}': unknown item '{}'", id, req.key));
				}
			}
		}
		for (const auto& combo : _catalog.combos) {
			if (!_catalog.FindTrait(combo.a) || !_catalog.FindTrait(combo.b) || !_catalog.FindTrait(combo.result)) {
				Error(std::format("trait combo {} + {} -> {} references an unknown trait", combo.a, combo.b, combo.result));
			}
		}
		for (const auto& [key, catalyst] : _catalog.catalysts) {
			if (!FindIngredientByKey(key) && !_catalog.FindRecipe(key)) {
				logger::warn("catalyst '{}' does not match any ingredient or material", key);
			}
		}
	}

	// ---------------------------------------------------------------------------------
	// Queries
	// ---------------------------------------------------------------------------------

	const EffectInfo* Database::FindEffect(std::string_view a_key) const
	{
		const auto it = _effects.find(a_key);
		return it != _effects.end() ? &it->second : nullptr;
	}

	std::string Database::EffectName(std::string_view a_key) const
	{
		if (const auto info = FindEffect(a_key)) {
			return info->name;
		}
		if (const auto it = _signatures.find(a_key); it != _signatures.end()) {
			return it->second.name;
		}
		return std::string(a_key);
	}

	const IngredientProfile* Database::FindIngredient(RE::FormID a_formID) const
	{
		const auto it = _ingredients.find(a_formID);
		return it != _ingredients.end() ? &it->second : nullptr;
	}

	const IngredientProfile* Database::FindIngredientByKey(std::string_view a_key) const
	{
		const auto it = _ingredientsByKey.find(a_key);
		return it != _ingredientsByKey.end() ? FindIngredient(it->second) : nullptr;
	}

	const std::vector<std::string>& Database::CategoryTraitPool(std::string_view a_category) const
	{
		static const std::vector<std::string> empty;
		const auto                            it = _categoryTraitPools.find(Lower(a_category));
		return it != _categoryTraitPools.end() ? it->second : empty;
	}

	std::string Database::ItemName(std::string_view a_itemKey) const
	{
		if (const auto ingredient = FindIngredientByKey(a_itemKey)) {
			return ingredient->name;
		}
		if (const auto recipe = _catalog.FindRecipe(a_itemKey)) {
			return recipe->name;
		}
		return std::string(a_itemKey);
	}

	RE::TESForm* Database::TemplateFor(const atelier::RecipeDef& a_recipe, const atelier::BrewData& a_brew) const
	{
		if (!a_recipe.templateRef.empty()) {
			if (const auto form = ResolveFormRef(a_recipe.templateRef)) {
				return form;
			}
		}

		if (a_recipe.kind == atelier::OutputKind::kMaterial) {
			for (const auto& req : a_recipe.requirements) {
				if (!req.isCategory) {
					if (const auto ingredient = FindIngredientByKey(req.key)) {
						return ingredient->form;
					}
				}
			}
			return _fallbackPotion;
		}

		for (const auto& effect : a_brew.effects) {
			if (const auto info = FindEffect(effect.effectKey)) {
				if (const auto it = _potionTemplates.find(info->form); it != _potionTemplates.end()) {
					return it->second;
				}
			}
		}
		return a_recipe.kind == atelier::OutputKind::kPoison ? _fallbackPoison : _fallbackPotion;
	}
}
