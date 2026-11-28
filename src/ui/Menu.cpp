#include "ui/Menu.h"

#include "core/JudgementRing.h"
#include "core/Synthesis.h"
#include "game/Config.h"
#include "game/Database.h"
#include "game/Forms.h"
#include "game/Workshop.h"
#include "ui/Input.h"
#include "ui/Renderer.h"
#include "ui/Sound.h"
#include "ui/Style.h"

#include <imgui.h>
#include <numbers>

namespace ui::Menu
{
	namespace
	{
		// -----------------------------------------------------------------------------
		// Session state (shared between the main thread and the render thread)
		// -----------------------------------------------------------------------------

		enum class Screen
		{
			kWorkshop,
			kRing,
			kBrewing,
			kResult,
		};

		struct Session
		{
			game::WorkshopSnapshot   snapshot;
			std::string              recipeId;
			std::vector<int>         slots;  // candidate index per expanded slot, -1 = empty
			int                      catalyst{ -1 };
			std::vector<std::string> traits;
			Screen                   screen{ Screen::kWorkshop };
			std::string              notice;
			// Element the player is trying to raise: pickers sort by it and highlight it.
			std::optional<atelier::Element> focus;
			// Search fields start empty every time the menu opens.
			std::array<char, 64> recipeSearch{};
			std::array<char, 64> categorySearch{};
			// Screen rectangle of the ring's Skip button last frame: clicks there aren't ring presses.
			ImVec2 skipMin{ -1.0f, -1.0f };
			ImVec2 skipMax{ -1.0f, -1.0f };

			atelier::JudgementRing ring;
			atelier::RingParams    ringParams;
			float                  countdown{ 0.0f };
			float                  sinceFinished{ 0.0f };
			Clock::time_point      lastRingUpdate{};

			std::optional<game::CommitResult> result;
		};

		std::recursive_mutex g_lock;
		Session              g_session;
		std::atomic_bool     g_open{ false };
		std::atomic_bool     g_ringActive{ false };
		std::atomic_bool     g_hintVisible{ false };

		void AddTask(std::function<void()> a_task)
		{
			SKSE::GetTaskInterface()->AddTask(std::move(a_task));
		}

		void Notify(const std::string& a_text)
		{
			RE::SendHUDMessage::ShowHUDMessage(a_text.c_str());
		}

		void RequestClose()
		{
			AddTask([] {
				if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
					queue->AddMessage(kMenuName, RE::UI_MESSAGE_TYPE::kHide, nullptr);
				}
			});
		}

		// -----------------------------------------------------------------------------
		// The engine-side menu. It renders nothing itself (an empty SWF keeps the
		// engine happy); its job is to pause the game and own input focus.
		// -----------------------------------------------------------------------------

		class AtelierMenu final : public RE::IMenu
		{
		public:
			AtelierMenu()
			{
				using Flag = RE::UI_MENU_FLAGS;
				// kUsesCursor makes the engine show and update its menu cursor, which drives
				// ImGui's cursor (see ApplyQueuedInput).
				menuFlags.set(Flag::kPausesGame, Flag::kModal, Flag::kDisablePauseMenu, Flag::kUsesMenuContext, Flag::kUsesCursor);
				depthPriority = 11;
				inputContext = Context::kMenuMode;
				if (auto* scaleform = RE::BSScaleformManager::GetSingleton()) {
					scaleform->LoadMovieEx(this, "AtelierAlchemy", [](RE::GFxMovieDef*) {});
				}
				if (uiMovie) {
					uiMovie->SetBackgroundAlpha(0.0f);
				}
			}

			static RE::IMenu* Create() { return new AtelierMenu(); }

			RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& a_message) override
			{
				switch (*a_message.type) {
				case RE::UI_MESSAGE_TYPE::kShow:
					OnShow();
					return RE::UI_MESSAGE_RESULTS::kHandled;
				case RE::UI_MESSAGE_TYPE::kHide:
				case RE::UI_MESSAGE_TYPE::kForceHide:
					OnHide();
					return RE::UI_MESSAGE_RESULTS::kHandled;
				default:
					return RE::IMenu::ProcessMessage(a_message);
				}
			}

		private:
			static void OnShow()
			{
				std::scoped_lock lock(g_lock);
				g_session = {};
				g_session.snapshot = game::TakeSnapshot();
				for (const auto& id : g_session.snapshot.newlyLearned) {
					if (const auto recipe = game::Database::Get().Catalog().FindRecipe(id)) {
						g_session.notice += std::format("New recipe: {}   ", recipe->name);
					}
				}
				ClearInput();
				g_open = true;
				if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
					queue->AddMessage(RE::CursorMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
				}
			}

			static void OnHide()
			{
				g_open = false;
				g_ringActive = false;
				ClearInput();
			}
		};

		class MenuWatcher final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			static MenuWatcher* Get()
			{
				static MenuWatcher watcher;
				return &watcher;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!a_event) {
					return RE::BSEventNotifyControl::kContinue;
				}
				if (a_event->menuName == RE::CraftingMenu::MENU_NAME) {
					g_hintVisible = a_event->opening && game::Config::Current().showCraftingHint && game::IsUsingAlchemyLab();
				}
				// If a full-screen game menu still manages to open on top of ours (hotkeys
				// are not all blocked by modal menus), step aside instead of fighting for input.
				static constexpr std::array<std::string_view, 12> kConflicting{ "TweenMenu", "InventoryMenu", "MagicMenu", "MapMenu",
					"Journal Menu", "StatsMenu", "Console", "FavoritesMenu", "ContainerMenu", "Lockpicking Menu", "Sleep/Wait Menu",
					"Dialogue Menu" };
				if (a_event->opening && g_open && std::ranges::find(kConflicting, std::string_view(a_event->menuName.c_str())) != kConflicting.end()) {
					RequestClose();
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// -----------------------------------------------------------------------------
		// Text helpers
		// -----------------------------------------------------------------------------

		const game::Database& Db() { return game::Database::Get(); }
		const atelier::Catalog& Cat() { return game::Database::Get().Catalog(); }

		std::string EffectText(std::string_view a_key, float a_magnitude, int a_duration)
		{
			std::string text = Db().EffectName(a_key);
			// Duration-based effects (Invisibility, Muffle...) have no meaningful magnitude.
			if (const auto* info = Db().FindEffect(a_key); !info || info->powerAffectsMagnitude || !info->powerAffectsDuration) {
				text += ' ';
				text += a_magnitude >= 10.0f ? std::format("{:.0f}", a_magnitude) : std::format("{:.1f}", a_magnitude);
			}
			if (a_duration > 0) {
				text += std::format(" for {}s", a_duration);
			}
			return text;
		}

		void EffectBreakdownTooltip(const atelier::OutputEffect& a_effect)
		{
			if (!a_effect.breakdown.empty() && ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", a_effect.breakdown.c_str());
			}
		}

		// A tier before quality: its share of vanilla, and what that means right now.
		std::string TierText(const atelier::EffectTier& a_tier, const std::unordered_map<std::string, atelier::EffectReference>& a_refs, float a_potency)
		{
			if (!a_tier.label.empty()) {
				return a_tier.label;
			}
			using Kind = atelier::EffectTier::Kind;
			switch (a_tier.kind) {
			case Kind::kEffect:
				if (const auto ref = a_refs.find(a_tier.effectKey); a_tier.vanillaScale > 0.0f && ref != a_refs.end()) {
					const auto& r = ref->second;
					const float share = a_tier.vanillaScale * a_potency;
					float mag = r.scalesDuration ? r.magnitude : r.magnitude * share;
					float dur = r.scalesDuration ? r.duration * share : r.duration;
					if (r.maxMagnitude > 0.0f) {
						mag = std::min(mag, r.maxMagnitude);
					}
					if (r.maxDuration > 0) {
						dur = std::min(dur, static_cast<float>(r.maxDuration));
					}
					return std::format("{}  ({:.0f}% of {})", EffectText(a_tier.effectKey, mag, static_cast<int>(std::lround(dur))), share * 100.0f,
						r.customMagnitude ? "base" : "vanilla");
				}
				return EffectText(a_tier.effectKey, a_tier.magnitude, a_tier.duration) + " (base)";
			case Kind::kQuality:
				return std::format("Quality +{}", a_tier.amount);
			case Kind::kYield:
				return std::format("Yield +{}", a_tier.amount);
			case Kind::kTraitSlot:
				return std::format("Trait slot +{}", a_tier.amount);
			case Kind::kElement:
				return std::format("Product gains {} +{}", atelier::ElementName(a_tier.element), a_tier.amount);
			}
			return {};
		}

		std::string TraitName(std::string_view a_id)
		{
			const auto trait = Cat().FindTrait(a_id);
			return trait ? trait->name : std::string(a_id);
		}

		std::string RequirementLabel(const atelier::Requirement& a_req)
		{
			return a_req.isCategory ? std::format("({})", a_req.key) : Db().ItemName(a_req.key);
		}

		std::vector<const atelier::Requirement*> ExpandSlots(const atelier::RecipeDef& a_recipe)
		{
			std::vector<const atelier::Requirement*> slots;
			for (const auto& req : a_recipe.requirements) {
				for (int i = 0; i < req.count; ++i) {
					slots.push_back(&req);
				}
			}
			return slots;
		}

		void TraitTooltip(std::string_view a_id)
		{
			const auto trait = Cat().FindTrait(a_id);
			if (!trait) {
				return;
			}
			ImGui::TextColored(ToVec4(Colors::kGold), "%s", trait->name.c_str());
			if (!trait->description.empty()) {
				ImGui::TextWrapped("%s", trait->description.c_str());
			}
			auto stat = [](const char* a_label, float a_value) {
				if (a_value != 0.0f) {
					ImGui::BulletText("%s %+.0f%%", a_label, a_value);
				}
			};
			stat("Magnitude", trait->magnitudePct);
			stat("Duration", trait->durationPct);
			stat("Value", trait->valuePct);
			stat("Weight", trait->weightPct);
			if (trait->qualityBonus != 0) {
				ImGui::BulletText("Quality %+d", trait->qualityBonus);
			}
			if (trait->ringWidthDeg != 0.0f) {
				ImGui::BulletText("Judgement Ring hit areas %+.0f deg", trait->ringWidthDeg);
			}
			for (std::size_t i = 0; i < atelier::kElementCount; ++i) {
				if (trait->elements[i] != 0) {
					const auto element = static_cast<atelier::Element>(i);
					ImGui::Bullet();
					ImGui::TextColored(ToVec4(ElementColor(element)), "%s %+d in every synthesis it is used in", atelier::ElementName(element), trait->elements[i]);
				}
			}
			if (trait->potionsOnly) {
				ImGui::TextDisabled("Only inherited by potions.");
			}
			if (trait->poisonsOnly) {
				ImGui::TextDisabled("Only inherited by poisons.");
			}
		}

		void ElementChips(const atelier::ElementValues& a_values, std::optional<atelier::Element> a_focus = std::nullopt)
		{
			static constexpr std::array<const char*, 4> kShort{ "Fi", "Ic", "Li", "Wi" };
			bool                                         any = false;
			for (std::size_t i = 0; i < atelier::kElementCount; ++i) {
				const auto element = static_cast<atelier::Element>(i);
				const bool focused = a_focus == element;
				if (a_values[i] <= 0 && !focused) {
					continue;
				}
				if (any) {
					ImGui::SameLine(0.0f, 6.0f);
				}
				if (focused) {
					// The element being raised: bracketed, and greyed when the item has none.
					ImGui::TextColored(ToVec4(a_values[i] > 0 ? ElementColor(element) : Colors::kMuted), "[%s%d]", kShort[i], a_values[i]);
				} else {
					ImGui::TextColored(ToVec4(ElementColor(element)), "%s%d", kShort[i], a_values[i]);
				}
				any = true;
			}
			if (!any) {
				ImGui::TextDisabled("-");
			}
		}

		std::string CategoryList(const std::vector<std::string>& a_categories)
		{
			std::string text;
			for (const auto& c : a_categories) {
				text += (text.empty() ? "" : ", ") + std::format("({})", c);
			}
			return text;
		}

		// Known recipes with a slot this item (by key or by one of its categories) can fill.
		std::vector<const atelier::RecipeDef*> RecipesUsing(const Session& a_session, std::string_view a_itemKey, const std::vector<std::string>& a_categories)
		{
			std::vector<const atelier::RecipeDef*> out;
			for (const auto& [id, recipe] : Cat().recipes) {
				if (!a_session.snapshot.known.contains(id)) {
					continue;
				}
				const bool fits = std::ranges::any_of(recipe.requirements, [&](const atelier::Requirement& r) {
					return r.isCategory ? std::ranges::find(a_categories, r.key) != a_categories.end() : r.key == a_itemKey;
				});
				if (fits) {
					out.push_back(&recipe);
				}
			}
			std::ranges::sort(out, [](auto* a, auto* b) { return std::tie(a->level, a->name) < std::tie(b->level, b->name); });
			return out;
		}

		std::string RecipeNames(const std::vector<const atelier::RecipeDef*>& a_recipes)
		{
			std::string text;
			for (const auto* r : a_recipes) {
				text += (text.empty() ? "" : ", ") + r->name;
			}
			return text;
		}

		// "Fills (Extract), (Plants) slots - used by ..." for a material.
		void FillsText(const Session& a_session, std::string_view a_itemKey, const std::vector<std::string>& a_categories)
		{
			if (!a_categories.empty()) {
				ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(Colors::kMaterial));
				ImGui::TextWrapped("Fills %s slots", CategoryList(a_categories).c_str());
				ImGui::PopStyleColor();
			}
			const auto users = RecipesUsing(a_session, a_itemKey, a_categories);
			if (!users.empty()) {
				ImGui::TextDisabled("Used by: %s", RecipeNames(users).c_str());
			}
		}

		void CandidateTooltip(const Session& a_session, const game::Candidate& a_candidate)
		{
			const auto& m = a_candidate.material;
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
			ImGui::TextColored(ToVec4(Colors::kGold), "%s", m.displayName.c_str());
			ImGui::Text("Quality %d (%s)   x%d available", m.quality, atelier::QualityRank(m.quality), a_candidate.available);
			FillsText(a_session, m.itemKey, m.categories);
			ElementChips(atelier::EffectiveElements(m, Cat()), a_session.focus);
			if (const auto catalyst = Cat().FindCatalyst(m.itemKey)) {
				ImGui::Separator();
				ImGui::TextColored(ToVec4(Colors::kMaterial), "Catalyst");
				if (!catalyst->description.empty()) {
					ImGui::TextWrapped("%s", catalyst->description.c_str());
				}
			}
			for (const auto& trait : m.traits) {
				ImGui::Separator();
				TraitTooltip(trait);
			}
			ImGui::PopTextWrapPos();
		}

		std::string CandidateLabel(const game::Candidate& a_candidate)
		{
			const auto& m = a_candidate.material;
			std::string label = std::format("{}  Q{} [{}]", m.displayName, m.quality, atelier::QualityRank(m.quality));
			for (const auto& trait : m.traits) {
				label += std::format("  <{}>", TraitName(trait));
			}
			return label;
		}

		// -----------------------------------------------------------------------------
		// Selection bookkeeping
		// -----------------------------------------------------------------------------

		int UsedCount(const Session& a_session, int a_candidate)
		{
			int used = static_cast<int>(std::ranges::count(a_session.slots, a_candidate));
			if (a_session.catalyst == a_candidate) {
				++used;
			}
			return used;
		}

		bool CanUse(const Session& a_session, int a_candidate, int a_currentChoice)
		{
			const auto& c = a_session.snapshot.candidates[static_cast<std::size_t>(a_candidate)];
			const int   used = UsedCount(a_session, a_candidate) - (a_currentChoice == a_candidate ? 1 : 0);
			return used < c.available;
		}

		std::vector<std::string> AvailableTraits(const Session& a_session)
		{
			std::vector<std::string> traits;
			auto                     add = [&](int a_index) {
                if (a_index < 0) {
                    return;
                }
                for (const auto& t : a_session.snapshot.candidates[static_cast<std::size_t>(a_index)].material.traits) {
                    if (std::ranges::find(traits, t) == traits.end()) {
                        traits.push_back(t);
                    }
                }
			};
			for (const auto index : a_session.slots) {
				add(index);
			}
			add(a_session.catalyst);
			return traits;
		}

		void PruneTraits(Session& a_session)
		{
			const auto available = AvailableTraits(a_session);
			std::erase_if(a_session.traits, [&](const std::string& t) { return std::ranges::find(available, t) == available.end(); });
		}

		void SelectRecipe(Session& a_session, const std::string& a_id)
		{
			a_session.recipeId = a_id;
			a_session.slots.clear();
			a_session.catalyst = -1;
			a_session.traits.clear();
			if (const auto recipe = Cat().FindRecipe(a_id)) {
				a_session.slots.assign(static_cast<std::size_t>(recipe->SlotCount()), -1);
			}
		}

		void AutoFill(Session& a_session, const atelier::RecipeDef& a_recipe)
		{
			const auto slots = ExpandSlots(a_recipe);
			for (std::size_t s = 0; s < slots.size(); ++s) {
				if (a_session.slots[s] >= 0) {
					continue;
				}
				int best = -1;
				for (int i = 0; i < static_cast<int>(a_session.snapshot.candidates.size()); ++i) {
					const auto& c = a_session.snapshot.candidates[static_cast<std::size_t>(i)];
					if (!c.material.Satisfies(*slots[s]) || !CanUse(a_session, i, -1)) {
						continue;
					}
					// Prefer non-catalysts, then the best quality.
					const auto& b = best >= 0 ? a_session.snapshot.candidates[static_cast<std::size_t>(best)] : c;
					if (best < 0 || (b.isCatalyst && !c.isCatalyst) ||
						(b.isCatalyst == c.isCatalyst && c.material.quality > b.material.quality)) {
						best = i;
					}
				}
				a_session.slots[s] = best;
			}
			PruneTraits(a_session);
		}

		// -----------------------------------------------------------------------------
		// Workshop screen
		// -----------------------------------------------------------------------------

		void DrawRecipeList(Session& a_session)
		{
			const auto& known = a_session.snapshot.known;
			std::vector<const atelier::RecipeDef*> recipes;
			for (const auto& [id, recipe] : Cat().recipes) {
				if (known.contains(id)) {
					recipes.push_back(&recipe);
				}
			}
			std::ranges::sort(recipes, [](auto* a, auto* b) { return std::tie(a->level, a->name) < std::tie(b->level, b->name); });

			ImGui::TextColored(ToVec4(Colors::kGold), "Recipe Book  (%zu)", recipes.size());
			auto& search = a_session.recipeSearch;
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputTextWithHint("##recipe_search", "Search recipes, effects, ingredients", search.data(), search.size());
			if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) {
				ImGui::SetTooltip("Matches recipe names, the effects they can make, what they need and the slots materials fill.");
			}
			ImGui::Separator();

			const auto needle = game::Lower(search.data());
			const auto matches = [&](const atelier::RecipeDef& a_recipe) {
				if (needle.empty()) {
					return true;
				}
				const auto hit = [&](std::string_view a_text) { return game::Lower(a_text).find(needle) != std::string::npos; };
				if (hit(a_recipe.name) || hit(KindLabel(a_recipe.kind))) {
					return true;
				}
				for (const auto& line : a_recipe.lines) {
					for (const auto& tier : line.tiers) {
						if (tier.kind == atelier::EffectTier::Kind::kEffect && hit(Db().EffectName(tier.effectKey))) {
							return true;
						}
					}
				}
				for (const auto& req : a_recipe.requirements) {
					if (hit(RequirementLabel(req))) {
						return true;
					}
				}
				return std::ranges::any_of(a_recipe.outputCategories, hit);
			};
			auto shown = recipes;  // the ideas below still use every known recipe
			std::erase_if(shown, [&](const atelier::RecipeDef* r) { return !matches(*r); });
			if (shown.empty()) {
				ImGui::TextDisabled("No known recipe matches.");
			}

			for (const auto* recipe : shown) {
				ImGui::PushID(recipe->id.c_str());
				const bool selected = a_session.recipeId == recipe->id;
				if (ImGui::Selectable("##recipe", selected, ImGuiSelectableFlags_None, { 0.0f, ImGui::GetTextLineHeightWithSpacing() })) {
					SelectRecipe(a_session, recipe->id);
				}
				if (recipe->kind == atelier::OutputKind::kMaterial && ImGui::IsItemHovered()) {
					ImGui::BeginTooltip();
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
					ImGui::TextColored(ToVec4(Colors::kGold), "%s", recipe->name.c_str());
					FillsText(a_session, recipe->id, recipe->outputCategories);
					ImGui::PopTextWrapPos();
					ImGui::EndTooltip();
				}
				ImGui::SameLine();
				ImGui::TextColored(ToVec4(KindColor(recipe->kind)), "%c", KindLabel(recipe->kind)[0]);
				ImGui::SameLine();
				ImGui::TextUnformatted(recipe->name.c_str());
				ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::CalcTextSize("Lv 100").x);
				ImGui::TextDisabled("Lv %d", recipe->level);
				ImGui::PopID();
			}

			// Ideas: hints for recipes the player can still discover.
			std::vector<std::string> ideas;
			for (const auto* recipe : recipes) {
				for (const auto& rule : recipe->unlocks) {
					if (known.contains(rule.recipe)) {
						continue;
					}
					std::string hint = rule.hint;
					if (hint.empty()) {
						hint = std::format("Synthesize {}", recipe->name);
						if (!rule.useItem.empty()) {
							hint += std::format(" using {}", Db().ItemName(rule.useItem));
						}
						if (!rule.useCategory.empty()) {
							hint += std::format(" using a ({})", rule.useCategory);
						}
						if (rule.minQuality > 0) {
							hint += std::format(" at quality {}+", rule.minQuality);
						}
						hint += ".";
					}
					ideas.push_back(std::move(hint));
				}
			}
			for (const auto& [id, recipe] : Cat().recipes) {
				if (!known.contains(id) && recipe.unlockSkill >= 0) {
					ideas.push_back(std::format("Reach Alchemy {} to devise a new recipe.", recipe.unlockSkill));
				}
			}
			std::ranges::sort(ideas);
			ideas.erase(std::unique(ideas.begin(), ideas.end()), ideas.end());
			if (!ideas.empty()) {
				ImGui::Spacing();
				ImGui::TextColored(ToVec4(Colors::kGold), "Ideas");
				ImGui::Separator();
				for (const auto& idea : ideas) {
					ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(Colors::kMuted));
					ImGui::TextWrapped("? %s", idea.c_str());
					ImGui::PopStyleColor();
				}
			}
		}

		bool DrawCandidatePicker(Session& a_session, const char* a_popupId, int& a_choice, const std::function<bool(const game::Candidate&)>& a_filter)
		{
			bool changed = false;
			if (ImGui::BeginPopup(a_popupId)) {
				// Sort by an element: what the player is trying to raise for an effect line.
				ImGui::TextDisabled("Sort:");
				ImGui::SameLine();
				if (ImGui::SmallButton(a_session.focus ? "Name" : "[Name]")) {
					a_session.focus.reset();
				}
				for (std::size_t e = 0; e < atelier::kElementCount; ++e) {
					const auto element = static_cast<atelier::Element>(e);
					ImGui::SameLine();
					ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(ElementColor(element)));
					const auto label = a_session.focus == element ? std::format("[{}]", atelier::ElementName(element)) : std::string(atelier::ElementName(element));
					if (ImGui::SmallButton(label.c_str())) {
						a_session.focus = element;
					}
					ImGui::PopStyleColor();
				}
				ImGui::Separator();

				std::vector<int> order;
				for (int i = 0; i < static_cast<int>(a_session.snapshot.candidates.size()); ++i) {
					if (a_filter(a_session.snapshot.candidates[static_cast<std::size_t>(i)])) {
						order.push_back(i);
					}
				}
				if (a_session.focus) {
					const auto value = [&](int i) { return atelier::At(atelier::EffectiveElements(a_session.snapshot.candidates[static_cast<std::size_t>(i)].material, Cat()), *a_session.focus); };
					std::ranges::stable_sort(order, std::greater{}, value);
				}

				int shown = 0;
				if (ImGui::Selectable("(empty)", a_choice < 0)) {
					a_choice = -1;
					changed = true;
				}
				for (const int i : order) {
					const auto& c = a_session.snapshot.candidates[static_cast<std::size_t>(i)];
					++shown;
					const bool usable = CanUse(a_session, i, a_choice);
					ImGui::PushID(i);
					ImGui::BeginDisabled(!usable);
					if (ImGui::Selectable(CandidateLabel(c).c_str(), a_choice == i)) {
						a_choice = i;
						changed = true;
					}
					ImGui::EndDisabled();
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
						ImGui::BeginTooltip();
						CandidateTooltip(a_session, c);
						ImGui::EndTooltip();
					}
					ImGui::SameLine();
					ElementChips(atelier::EffectiveElements(c.material, Cat()), a_session.focus);
					ImGui::SameLine();
					ImGui::TextDisabled("x%d", c.available - UsedCount(a_session, i) + (a_choice == i ? 1 : 0));
					ImGui::PopID();
				}
				if (shown == 0) {
					ImGui::TextDisabled("You have nothing that fits.");
				}
				ImGui::EndPopup();
			}
			return changed;
		}

		void DrawSlotButton(Session& a_session, const char* a_id, int& a_choice, const std::function<bool(const game::Candidate&)>& a_filter)
		{
			ImGui::PushID(a_id);
			std::string label = "- empty -";
			if (a_choice >= 0) {
				label = CandidateLabel(a_session.snapshot.candidates[static_cast<std::size_t>(a_choice)]);
			}
			if (ImGui::Button(std::format("{}##slot", label).c_str(), { -FLT_MIN, 0.0f })) {
				ImGui::OpenPopup("pick");
			}
			if (a_choice >= 0 && ImGui::IsItemHovered()) {
				ImGui::BeginTooltip();
				CandidateTooltip(a_session, a_session.snapshot.candidates[static_cast<std::size_t>(a_choice)]);
				ImGui::EndTooltip();
			}
			if (DrawCandidatePicker(a_session, "pick", a_choice, a_filter)) {
				PruneTraits(a_session);
			}
			ImGui::PopID();
		}

		void DrawElementBars(const atelier::ElementValues& a_values)
		{
			const int maxValue = std::max(12, *std::ranges::max_element(a_values));
			for (std::size_t i = 0; i < atelier::kElementCount; ++i) {
				const auto element = static_cast<atelier::Element>(i);
				ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ToVec4(ElementColor(element)));
				ImGui::ProgressBar(static_cast<float>(a_values[i]) / static_cast<float>(maxValue), { ImGui::GetContentRegionAvail().x * 0.6f, 0.0f },
					std::format("{} {}", atelier::ElementName(element), a_values[i]).c_str());
				ImGui::PopStyleColor();
			}
		}

		void DrawEffectLines(Session& a_session, const atelier::RecipeDef& a_recipe, const atelier::SynthesisOutcome& a_outcome)
		{
			const auto refs = game::EffectReferences(a_session.snapshot, a_recipe);
			for (std::size_t i = 0; i < a_recipe.lines.size(); ++i) {
				const auto& line = a_recipe.lines[i];
				const auto& state = a_outcome.lines[i];
				ImGui::PushID(static_cast<int>(i));
				ImGui::TextColored(ToVec4(ElementColor(line.element)), "%s", line.name.empty() ? atelier::ElementName(line.element) : line.name.c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("(%s %d)", atelier::ElementName(line.element), state.value);
				ImGui::SameLine();
				const bool focused = a_session.focus == line.element;
				if (ImGui::SmallButton(focused ? "[Find]" : "Find")) {
					if (focused) {
						a_session.focus.reset();
					} else {
						a_session.focus = line.element;
					}
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Sort ingredient choices by %s and highlight it, to raise this line.", atelier::ElementName(line.element));
				}

				const int maxThreshold = line.tiers.empty() ? 1 : std::max(line.tiers.back().threshold, 1);
				ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ToVec4(ElementColor(line.element)));
				ImGui::ProgressBar(std::min(1.0f, static_cast<float>(state.value) / static_cast<float>(maxThreshold)), { -FLT_MIN, 6.0f * Renderer::Scale() }, "");
				ImGui::PopStyleColor();

				for (std::size_t t = 0; t < line.tiers.size(); ++t) {
					const auto& tier = line.tiers[t];
					const bool  reached = static_cast<int>(t) <= state.reachedTier;
					ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(reached ? Colors::kGood : Colors::kMuted));
					ImGui::Text("  %s %2d  %s", reached ? "[x]" : "[ ]", tier.threshold, TierText(tier, refs, a_recipe.potency).c_str());
					ImGui::PopStyleColor();
				}
				ImGui::PopID();
			}
		}

		void DrawTraitSelection(Session& a_session, const atelier::SynthesisOutcome& a_outcome)
		{
			const auto available = AvailableTraits(a_session);
			ImGui::TextColored(ToVec4(Colors::kGold), "Traits  (%zu / %d slots)", a_outcome.finalTraits.size(), a_outcome.traitSlots);
			if (available.empty()) {
				ImGui::TextDisabled("None of the chosen materials carry traits.");
				return;
			}
			for (const auto& id : available) {
				bool checked = std::ranges::find(a_session.traits, id) != a_session.traits.end();
				if (ImGui::Checkbox(std::format("{}##trait_{}", TraitName(id), id).c_str(), &checked)) {
					if (checked) {
						a_session.traits.push_back(id);
					} else {
						std::erase(a_session.traits, id);
					}
				}
				if (ImGui::IsItemHovered()) {
					ImGui::BeginTooltip();
					TraitTooltip(id);
					ImGui::EndTooltip();
				}
			}
			if (!a_outcome.finalTraits.empty()) {
				std::string final;
				for (const auto& id : a_outcome.finalTraits) {
					final += (final.empty() ? "" : ", ") + TraitName(id);
				}
				ImGui::TextWrapped("Inherited: %s", final.c_str());
			}
		}

		float ToRadians(float a_degrees)
		{
			return (a_degrees - 90.0f) * std::numbers::pi_v<float> / 180.0f;
		}

		void Arc(ImDrawList* a_draw, ImVec2 a_center, float a_radius, float a_from, float a_to, ImU32 a_color, float a_thickness)
		{
			if (a_to <= a_from) {
				return;
			}
			a_draw->PathArcTo(a_center, a_radius, ToRadians(a_from), ToRadians(a_to), std::max(4, static_cast<int>((a_to - a_from) / 3.0f)));
			a_draw->PathStroke(a_color, ImDrawFlags_None, a_thickness);
		}

		// One cue per frame: the best result judged since the last frame. A miss that ends
		// the ring fails every later area at once, which must still sound like one miss.
		// Normal / Easy / Auto, saved back to the INI.
		void DrawRingModeSwitch()
		{
			using game::RingMode;
			static constexpr std::array<std::pair<RingMode, const char*>, 3> kModes{ {
				{ RingMode::kNormal, "Normal" },
				{ RingMode::kEasy, "Easy" },
				{ RingMode::kAuto, "Auto" },
			} };
			auto&       config = game::Config::Get();
			const char* current = "Normal";
			for (const auto& [mode, label] : kModes) {
				if (mode == config.ringMode) {
					current = label;
				}
			}
			ImGui::BeginGroup();
			ImGui::TextDisabled("Judgement Ring");
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.5f);
			if (ImGui::BeginCombo("##ring_mode", current)) {
				for (const auto& [mode, label] : kModes) {
					if (ImGui::Selectable(label, mode == config.ringMode) && mode != config.ringMode) {
						config.ringMode = mode;
						config.SaveRingMode();
					}
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("%s", mode == RingMode::kNormal ? "The full Judgement Ring: hits and strikes raise quality, a Perfect adds an item." :
						                        mode == RingMode::kEasy   ? "Slower needle, wider areas, and a miss doesn't end the ring." :
						                                                    "Every area is hit automatically: no miss, but no hit, strike or Perfect bonus.");
					}
				}
				ImGui::EndCombo();
			}
			ImGui::EndGroup();
		}

		// A small picture of the ring this synthesis will get, with the player's current widths.
		void DrawRingPreview(Session& a_session, const atelier::RecipeDef& a_recipe)
		{
			const auto   params = game::RingParamsFor(a_session.snapshot, a_recipe, a_session.slots, a_session.catalyst);
			std::mt19937 rng(0);  // jitter-free: the pattern as designed
			auto         layoutParams = params;
			const auto   areas = atelier::JudgementRing::Layout(layoutParams, rng);

			const float  size = ImGui::GetFrameHeight() * 2.2f;
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const ImVec2 center{ origin.x + size * 0.5f, origin.y + size * 0.5f };
			const float  radius = size * 0.38f;
			const float  thickness = size * 0.12f;
			auto*        draw = ImGui::GetWindowDrawList();
			draw->AddCircle(center, radius, Colors::kRingTrack, 48, thickness);
			for (const auto& area : areas) {
				Arc(draw, center, radius, area.start, area.strikeStart, Colors::kHitArea, thickness);
				Arc(draw, center, radius, area.strikeStart, area.end, Colors::kStrikeArea, thickness);
			}
			ImGui::Dummy({ size, size });
			if (ImGui::IsItemHovered()) {
				ImGui::BeginTooltip();
				ImGui::TextColored(ToVec4(Colors::kGold), "Judgement Ring: %s", a_recipe.ringPattern.empty() ? "standard" : a_recipe.ringPattern.c_str());
				ImGui::Text("%zu areas, needle %.0f deg/s", areas.size(), params.degreesPerSecond);
				ImGui::TextDisabled("Orange: hit area. Red: strike area. Areas grow with skill, catalysts and Steady traits.");
				ImGui::EndTooltip();
			}
		}

		void StartRing(Session& a_session, const atelier::RecipeDef& a_recipe)
		{
			a_session.ringParams = game::RingParamsFor(a_session.snapshot, a_recipe, a_session.slots, a_session.catalyst);
			a_session.ring = {};
			a_session.countdown = 1.2f;
			a_session.sinceFinished = 0.0f;
			a_session.screen = Screen::kRing;
			TakeRingPresses();
		}

		void DrawBench(Session& a_session)
		{
			const auto recipe = Cat().FindRecipe(a_session.recipeId);
			if (!recipe) {
				ImGui::TextDisabled("Choose a recipe from your recipe book.");
				ImGui::Spacing();
				ImGui::TextWrapped(
					"Every recipe asks for ingredients by name or by category. Their elements - Fire, Ice, Lightning and Wind - "
					"fill the recipe's effect lines and unlock stronger effects. Quality and traits are passed on to what you brew. "
					"Finish each synthesis by stopping the Judgement Ring inside its hit areas.");
				return;
			}

			ImGui::PushFont(Renderer::LargeFont());
			ImGui::TextColored(ToVec4(Colors::kGold), "%s", recipe->name.c_str());
			ImGui::PopFont();
			ImGui::SameLine();
			ImGui::TextColored(ToVec4(KindColor(recipe->kind)), "%s", KindLabel(recipe->kind));
			ImGui::SameLine();
			ImGui::TextDisabled("Level %d", recipe->level);
			if (!recipe->description.empty()) {
				ImGui::TextWrapped("%s", recipe->description.c_str());
			}
			if (recipe->kind == atelier::OutputKind::kMaterial) {
				FillsText(a_session, recipe->id, recipe->outputCategories);
			}
			ImGui::Separator();

			const auto input = game::BuildInput(a_session.snapshot, *recipe, a_session.slots, a_session.catalyst, a_session.traits);
			const bool allFilled = std::ranges::none_of(a_session.slots, [](int s) { return s < 0; });
			const auto outcome = atelier::Evaluate(input, Cat(), nullptr, game::Config::Current().baseTraitSlots);

			// Left: ingredients. Right: elements, lines, traits.
			if (ImGui::BeginTable("bench", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
				ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch, 1.1f);
				ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch, 1.0f);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

				ImGui::TextColored(ToVec4(Colors::kGold), "Ingredients");
				const auto slots = ExpandSlots(*recipe);
				for (std::size_t s = 0; s < slots.size(); ++s) {
					const auto* req = slots[s];
					ImGui::TextDisabled("%s", RequirementLabel(*req).c_str());
					DrawSlotButton(a_session, std::format("slot{}", s).c_str(), a_session.slots[s], [req](const game::Candidate& c) { return c.material.Satisfies(*req); });
				}
				ImGui::Spacing();
				ImGui::TextDisabled("Catalyst (optional)");
				DrawSlotButton(a_session, "catalyst", a_session.catalyst, [](const game::Candidate& c) { return c.isCatalyst; });
				if (a_session.catalyst >= 0) {
					const auto& c = a_session.snapshot.candidates[static_cast<std::size_t>(a_session.catalyst)];
					if (const auto def = Cat().FindCatalyst(c.material.itemKey); def && !def->description.empty()) {
						ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(Colors::kMaterial));
						ImGui::TextWrapped("%s", def->description.c_str());
						ImGui::PopStyleColor();
					}
				}

				ImGui::Spacing();
				if (ImGui::Button("Auto-fill")) {
					AutoFill(a_session, *recipe);
				}
				ImGui::SameLine();
				if (ImGui::Button("Clear")) {
					SelectRecipe(a_session, recipe->id);
				}

				ImGui::Spacing();
				ImGui::Separator();
				DrawTraitSelection(a_session, outcome);

				ImGui::TableSetColumnIndex(1);
				ImGui::TextColored(ToVec4(Colors::kGold), "Elements");
				DrawElementBars(outcome.elements);
				ImGui::Spacing();
				ImGui::TextColored(ToVec4(Colors::kGold), "Effect Lines");
				DrawEffectLines(a_session, *recipe, outcome);
				ImGui::EndTable();
			}

			ImGui::Separator();
			if (allFilled) {
				ImGui::Text("Expected quality ~%d [%s]   Yield %d   Value %d", outcome.quality, atelier::QualityRank(outcome.quality), outcome.yield, outcome.brew.value);
				for (std::size_t i = 0; i < outcome.brew.effects.size(); ++i) {
					const auto& e = outcome.brew.effects[i];
					if (i > 0) {
						ImGui::SameLine();
						ImGui::TextUnformatted(",");
						ImGui::SameLine();
					}
					ImGui::TextColored(ToVec4(Colors::kGood), "%s", EffectText(e.effectKey, e.magnitude, e.duration).c_str());
					EffectBreakdownTooltip(e);
				}
				if (!outcome.valid && !outcome.error.empty()) {
					ImGui::TextColored(ToVec4(Colors::kBad), "%s", outcome.error.c_str());
				}
			} else {
				ImGui::TextDisabled("Fill every ingredient slot to synthesize.");
			}

			ImGui::BeginDisabled(!allFilled || !outcome.valid);
			if (ImGui::Button("Synthesize!", { 220.0f * Renderer::Scale(), 0.0f })) {
				StartRing(a_session, *recipe);
			}
			ImGui::EndDisabled();
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 3.0f);
			DrawRingPreview(a_session, *recipe);
			ImGui::SameLine();
			DrawRingModeSwitch();
		}

		// Lays out short labels left to right, wrapping at the window edge.
		void FlowItem(const std::string& a_text, ImU32 a_color, bool& a_first)
		{
			const float width = ImGui::CalcTextSize(a_text.c_str()).x;
			if (!a_first) {
				ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
				if (ImGui::GetContentRegionAvail().x < width) {
					ImGui::NewLine();
				}
			}
			ImGui::TextColored(ToVec4(a_color), "%s", a_text.c_str());
			a_first = false;
		}

		void DrawCategories(Session& a_session)
		{
			struct Entry
			{
				struct Ingredient
				{
					std::string name;
					int         owned{ 0 };
					std::string plugin;
					bool        obtainable{ true };
				};
				std::vector<Ingredient>                  ingredients;
				std::vector<const atelier::RecipeDef*>   materials;    // known material recipes producing it
				bool                                     undiscovered{ false };
				std::vector<const atelier::RecipeDef*>   neededBy;
				int                                      hidden{ 0 };
			};

			const auto& snapshot = a_session.snapshot;
			std::map<std::string, int> owned;  // by item key
			for (const auto& c : snapshot.candidates) {
				owned[c.material.itemKey] += c.available;
			}

			std::map<std::string, Entry> categories;
			static bool showUnobtainable = false;
			for (const auto& [formID, profile] : Db().IngredientProfiles()) {
				const auto it = owned.find(profile.key);
				const int  n = it != owned.end() ? it->second : 0;
				for (const auto& category : profile.categories) {
					auto& entry = categories[category];
					// Anything the player carries is evidently obtainable.
					if (!profile.obtainable && n == 0 && !showUnobtainable) {
						++entry.hidden;
						continue;
					}
					entry.ingredients.push_back({ profile.name, n, profile.plugin, profile.obtainable || n > 0 });
				}
			}
			for (const auto& [id, recipe] : Cat().recipes) {
				if (!snapshot.known.contains(id)) {
					continue;
				}
				if (recipe.kind == atelier::OutputKind::kMaterial) {
					for (const auto& category : recipe.outputCategories) {
						categories[category].materials.push_back(&recipe);
					}
				}
				for (const auto& req : recipe.requirements) {
					if (req.isCategory) {
						categories[req.key].neededBy.push_back(&recipe);
					}
				}
			}
			// Unknown materials only show up as "?" in categories the player already sees,
			// so the tab doesn't spoil categories that exist only for undiscovered recipes.
			for (const auto& [id, recipe] : Cat().recipes) {
				if (recipe.kind != atelier::OutputKind::kMaterial || snapshot.known.contains(id)) {
					continue;
				}
				for (const auto& category : recipe.outputCategories) {
					if (const auto it = categories.find(category); it != categories.end()) {
						it->second.undiscovered = true;
					}
				}
			}

			auto&       filter = a_session.categorySearch;
			static bool ownedOnly = false;
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
			ImGui::InputTextWithHint("##filter", "Search categories or ingredients", filter.data(), filter.size());
			ImGui::SameLine();
			ImGui::Checkbox("Only what I carry", &ownedOnly);
			ImGui::SameLine();
			ImGui::Checkbox("Show unobtainable", &showUnobtainable);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Also list ingredients that no NPC, container, plant, crafting recipe or loot list in your load order provides,\n"
								  "e.g. from asset files of mods that aren't installed. Hover an ingredient to see its plugin.");
			}
			const auto needle = game::Lower(filter.data());
			const auto matches = [&](std::string_view a_text) { return needle.empty() || game::Lower(a_text).find(needle) != std::string::npos; };

			if (!ImGui::BeginChild("categories", { 0.0f, 0.0f }, ImGuiChildFlags_Borders)) {
				ImGui::EndChild();
				return;
			}
			for (auto& [name, entry] : categories) {
				std::ranges::sort(entry.ingredients, [](const auto& a, const auto& b) { return std::tie(b.owned, a.name) < std::tie(a.owned, b.name); });
				std::ranges::sort(entry.neededBy, [](auto* a, auto* b) { return a->name < b->name; });
				entry.neededBy.erase(std::unique(entry.neededBy.begin(), entry.neededBy.end()), entry.neededBy.end());

				const bool special = name == "Potions" || name == "Poisons";
				if (entry.ingredients.empty() && entry.materials.empty() && entry.neededBy.empty() && !special) {
					continue;  // only unobtainable ingredients
				}
				const bool nameHit = matches(name);
				const bool itemHit = std::ranges::any_of(entry.ingredients, [&](const auto& i) { return matches(i.name); }) ||
				                     std::ranges::any_of(entry.materials, [&](auto* r) { return matches(r->name); });
				if (!nameHit && !itemHit) {
					continue;
				}
				int ownedKinds = 0;
				for (const auto& ingredient : entry.ingredients) {
					ownedKinds += ingredient.owned > 0 ? 1 : 0;
				}
				for (const auto* material : entry.materials) {
					ownedKinds += owned.contains(material->id) ? 1 : 0;
				}
				if (ownedOnly && ownedKinds == 0 && !special) {
					continue;
				}

				const auto label = std::format("({})   {} of {} kinds carried###{}", name, ownedKinds, entry.ingredients.size() + entry.materials.size(), name);
				if (!needle.empty()) {
					ImGui::SetNextItemOpen(true);
				}
				if (!ImGui::CollapsingHeader(label.c_str())) {
					continue;
				}
				ImGui::Indent();
				if (special) {
					ImGui::TextWrapped("Any %s you have synthesized fits this slot.", name == "Potions" ? "potion" : "poison");
				}
				if (!entry.ingredients.empty()) {
					ImGui::TextColored(ToVec4(Colors::kGold), "Ingredients");
					bool first = true;
					for (const auto& ingredient : entry.ingredients) {
						if (ownedOnly && ingredient.owned == 0) {
							continue;
						}
						const auto text = ingredient.owned > 0 ? std::format("{} x{}", ingredient.name, ingredient.owned) :
						                  ingredient.obtainable ? ingredient.name : ingredient.name + " (unobtainable)";
						FlowItem(text, ingredient.owned > 0 ? Colors::kGood : ingredient.obtainable ? Colors::kMuted : Colors::kBad, first);
						if (ImGui::IsItemHovered()) {
							ImGui::SetTooltip("From %s%s", ingredient.plugin.empty() ? "an unknown plugin" : ingredient.plugin.c_str(),
								ingredient.obtainable ? "" : "\nNo NPC, container, plant, crafting recipe or loot list in your load order provides it.");
						}
					}
				}
				if (entry.hidden > 0 && !ownedOnly) {
					ImGui::TextDisabled("+%d unobtainable ingredient(s) hidden", entry.hidden);
				}
				if (!entry.materials.empty() || entry.undiscovered) {
					ImGui::TextColored(ToVec4(Colors::kGold), "Synthesized materials");
					bool first = true;
					for (const auto* material : entry.materials) {
						const auto it = owned.find(material->id);
						FlowItem(it != owned.end() ? std::format("{} x{}", material->name, it->second) : material->name,
							it != owned.end() ? Colors::kGood : Colors::kMaterial, first);
					}
					if (entry.undiscovered) {
						FlowItem("? (a recipe you haven't discovered yet)", Colors::kMuted, first);
					}
				}
				if (!entry.neededBy.empty()) {
					ImGui::TextColored(ToVec4(Colors::kGold), "Needed by");
					ImGui::TextWrapped("%s", RecipeNames(entry.neededBy).c_str());
				}
				ImGui::Unindent();
				ImGui::Spacing();
			}
			ImGui::EndChild();
		}

		void DrawWorkshop(Session& a_session)
		{
			if (!ImGui::BeginTabBar("workshop_tabs")) {
				return;
			}
			if (ImGui::BeginTabItem("Synthesis")) {
				const float listWidth = ImGui::GetContentRegionAvail().x * 0.28f;
				if (ImGui::BeginChild("recipes", { listWidth, 0.0f }, ImGuiChildFlags_Borders)) {
					DrawRecipeList(a_session);
				}
				ImGui::EndChild();
				ImGui::SameLine();
				if (ImGui::BeginChild("bench", { 0.0f, 0.0f }, ImGuiChildFlags_Borders)) {
					DrawBench(a_session);
				}
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Ingredient Categories")) {
				DrawCategories(a_session);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		// -----------------------------------------------------------------------------
		// Judgement Ring screen
		// -----------------------------------------------------------------------------

		// -----------------------------------------------------------------------------
		// Mandala backdrop of the Judgement Ring: rotation-symmetric layers and alchemical
		// symbols, drawn in faint gold. a_glow (0..1) brightens it briefly after a hit.
		// -----------------------------------------------------------------------------

		ImU32 Gold(float a_alpha)
		{
			return IM_COL32(232, 190, 110, static_cast<int>(std::clamp(a_alpha, 0.0f, 1.0f) * 255.0f));
		}

		ImVec2 Polar(ImVec2 a_center, float a_radius, float a_angle)
		{
			return { a_center.x + std::cos(a_angle) * a_radius, a_center.y + std::sin(a_angle) * a_radius };
		}

		void Triangle(ImDrawList* a_draw, ImVec2 a_c, float a_s, bool a_up, ImU32 a_col, float a_w)
		{
			const float dir = a_up ? -1.0f : 1.0f;
			const ImVec2 apex{ a_c.x, a_c.y + dir * a_s };
			const ImVec2 left{ a_c.x - a_s * 0.87f, a_c.y - dir * a_s * 0.5f };
			const ImVec2 right{ a_c.x + a_s * 0.87f, a_c.y - dir * a_s * 0.5f };
			a_draw->AddTriangle(apex, left, right, a_col, a_w);
		}

		// Classical alchemical glyphs, upright, centered on a_c, roughly a_s in radius.
		void Glyph(ImDrawList* a_draw, int a_index, ImVec2 a_c, float a_s, ImU32 a_col, float a_w)
		{
			switch (a_index % 8) {
			case 0:  // Fire: triangle pointing up
				Triangle(a_draw, a_c, a_s, true, a_col, a_w);
				break;
			case 1:  // Mercury: horns, circle, cross
				a_draw->AddCircle({ a_c.x, a_c.y - a_s * 0.1f }, a_s * 0.38f, a_col, 20, a_w);
				a_draw->PathArcTo({ a_c.x, a_c.y - a_s * 0.85f }, a_s * 0.38f, 0.25f, std::numbers::pi_v<float> - 0.25f, 12);
				a_draw->PathStroke(a_col, ImDrawFlags_None, a_w);
				a_draw->AddLine({ a_c.x, a_c.y + a_s * 0.28f }, { a_c.x, a_c.y + a_s }, a_col, a_w);
				a_draw->AddLine({ a_c.x - a_s * 0.32f, a_c.y + a_s * 0.66f }, { a_c.x + a_s * 0.32f, a_c.y + a_s * 0.66f }, a_col, a_w);
				break;
			case 2:  // Air: triangle up with a bar
				Triangle(a_draw, a_c, a_s, true, a_col, a_w);
				a_draw->AddLine({ a_c.x - a_s * 0.75f, a_c.y }, { a_c.x + a_s * 0.75f, a_c.y }, a_col, a_w);
				break;
			case 3:  // Sun / gold: circle with a point
				a_draw->AddCircle(a_c, a_s * 0.8f, a_col, 24, a_w);
				a_draw->AddCircleFilled(a_c, a_s * 0.16f, a_col, 10);
				break;
			case 4:  // Water: triangle pointing down
				Triangle(a_draw, a_c, a_s, false, a_col, a_w);
				break;
			case 5:  // Salt: circle with a horizontal line
				a_draw->AddCircle(a_c, a_s * 0.8f, a_col, 24, a_w);
				a_draw->AddLine({ a_c.x - a_s * 0.8f, a_c.y }, { a_c.x + a_s * 0.8f, a_c.y }, a_col, a_w);
				break;
			case 6:  // Earth: triangle down with a bar
				Triangle(a_draw, a_c, a_s, false, a_col, a_w);
				a_draw->AddLine({ a_c.x - a_s * 0.75f, a_c.y }, { a_c.x + a_s * 0.75f, a_c.y }, a_col, a_w);
				break;
			default:  // Moon / silver: crescent
				{
					const float pi = std::numbers::pi_v<float>;
					a_draw->PathArcTo(a_c, a_s * 0.8f, pi * 0.5f, pi * 1.5f, 20);
					a_draw->PathArcTo({ a_c.x - a_s * 0.3f, a_c.y }, a_s * 0.62f, pi * 1.38f, pi * 0.62f, 16);
					a_draw->PathStroke(a_col, ImDrawFlags_Closed, a_w);
				}
				break;
			}
		}

		void DrawMandala(ImDrawList* a_draw, ImVec2 a_center, float a_radius, float a_thickness, float a_outer, float a_glow)
		{
			const float pi = std::numbers::pi_v<float>;
			const float t = static_cast<float>(ImGui::GetTime());
			const float w = std::max(1.0f, Renderer::Scale());
			const float lift = 1.0f + a_glow * 1.6f;
			const float ringOuter = a_radius + a_thickness;

			// Outer band: 8 symbols with dots between them, turning slowly.
			const float band = (ringOuter + a_outer) * 0.5f;
			const float glyphSize = std::min((a_outer - ringOuter) * 0.26f, band * 0.09f);
			const float spinA = t * 0.06f;
			a_draw->AddCircle(a_center, a_outer, Gold(0.22f * lift), 128, w);
			// Petals from the ring out to the outer circle, behind the symbols, turning the
			// other way.
			const float spinB = -t * 0.04f;
			const float petalBase = ringOuter + a_thickness * 0.1f;
			const float petalTip = a_outer * 0.99f;
			for (int i = 0; i < 16; ++i) {
				const float a = spinB + static_cast<float>(i) * pi / 8.0f;
				const float half = pi / 16.0f;
				const ImVec2 p0 = Polar(a_center, petalBase, a - half);
				const ImVec2 p1 = Polar(a_center, petalBase, a + half);
				const ImVec2 tip = Polar(a_center, petalTip, a);
				a_draw->AddBezierQuadratic(p0, Polar(a_center, petalTip * 0.98f, a - half * 0.9f), tip, Gold(0.16f * lift), w);
				a_draw->AddBezierQuadratic(tip, Polar(a_center, petalTip * 0.98f, a + half * 0.9f), p1, Gold(0.16f * lift), w);
			}

			for (int i = 0; i < 8; ++i) {
				const float a = spinA + static_cast<float>(i) * pi / 4.0f;
				a_draw->AddCircleFilled(Polar(a_center, band, a), glyphSize * 1.3f, Colors::kRingBase, 20);  // keeps petals off the symbol
				a_draw->AddCircle(Polar(a_center, band, a), glyphSize * 1.3f, Gold(0.18f * lift), 20, w);
				Glyph(a_draw, i, Polar(a_center, band, a), glyphSize, Gold(0.38f * lift), w * 1.3f);
				a_draw->AddCircleFilled(Polar(a_center, band, a + pi / 8.0f), glyphSize * 0.14f, Gold(0.3f * lift), 8);
			}

			// Inside the ring: eight-pointed star of two squares, twelve spokes, circles.
			const float inner = a_radius - a_thickness * 0.6f;
			const float spinC = -t * 0.03f;
			for (int square = 0; square < 2; ++square) {
				ImVec2 corners[4];
				for (int k = 0; k < 4; ++k) {
					corners[k] = Polar(a_center, inner * 0.96f, spinC + static_cast<float>(square) * pi / 4.0f + static_cast<float>(k) * pi / 2.0f);
				}
				a_draw->AddPolyline(corners, 4, Gold(0.14f * lift), ImDrawFlags_Closed, w);
			}
			for (int i = 0; i < 12; ++i) {
				const float a = -spinC + static_cast<float>(i) * pi / 6.0f;
				a_draw->AddLine(Polar(a_center, a_radius * 0.46f, a), Polar(a_center, inner * 0.7f, a), Gold(0.1f * lift), w);
				a_draw->AddCircleFilled(Polar(a_center, inner * 0.7f, a), w * 1.6f, Gold(0.18f * lift), 6);
			}
			a_draw->AddCircle(a_center, inner * 0.7f, Gold(0.1f * lift), 96, w);
			a_draw->AddCircle(a_center, inner, Gold(0.12f * lift), 96, w);
		}

		// The squared circle behind the counter text: circle, square, triangle, circle.
		void DrawMandalaCenter(ImDrawList* a_draw, ImVec2 a_center, float a_r, float a_glow)
		{
			const float pi = std::numbers::pi_v<float>;
			const float w = std::max(1.0f, Renderer::Scale());
			const ImU32 col = Gold(0.13f * (1.0f + a_glow * 1.6f));
			const float square = a_r * 0.92f;
			ImVec2 corners[4];
			for (int k = 0; k < 4; ++k) {
				corners[k] = Polar(a_center, square, pi / 4.0f + static_cast<float>(k) * pi / 2.0f);
			}
			a_draw->AddPolyline(corners, 4, col, ImDrawFlags_Closed, w);
			const float tri = square * 0.707f;
			a_draw->AddTriangle(Polar(a_center, tri, -pi / 2.0f), Polar(a_center, tri, pi / 6.0f), Polar(a_center, tri, pi * 5.0f / 6.0f), col, w);
			a_draw->AddCircle(a_center, tri * 0.5f, col, 32, w);
		}

		void PlayJudgementCues(const std::vector<atelier::AreaResult>& a_before, const atelier::JudgementRing& a_ring)
		{
			const auto& after = a_ring.Results();
			auto        best = atelier::AreaResult::kPending;
			for (std::size_t i = 0; i < after.size() && i < a_before.size(); ++i) {
				if (a_before[i] == atelier::AreaResult::kPending && after[i] != atelier::AreaResult::kPending) {
					const auto rank = [](atelier::AreaResult r) { return r == atelier::AreaResult::kStrike ? 3 : r == atelier::AreaResult::kHit ? 2 : r == atelier::AreaResult::kMiss ? 1 : 0; };
					if (rank(after[i]) > rank(best)) {
						best = after[i];
					}
				}
			}
			switch (best) {
			case atelier::AreaResult::kStrike:
				Sound::Play(Sound::Cue::kStrike);
				break;
			case atelier::AreaResult::kHit:
				Sound::Play(Sound::Cue::kHit);
				break;
			case atelier::AreaResult::kMiss:
				Sound::Play(Sound::Cue::kMiss);
				break;
			case atelier::AreaResult::kPending:
				break;
			}
			if (best != atelier::AreaResult::kPending && a_ring.Finished() && a_ring.Summary().Perfect()) {
				Sound::Play(Sound::Cue::kPerfect);
			}
		}

		// Resolves the rest of the synthesis as Auto mode would: every area hit, no bonuses.
		void SkipRing(Session& a_session)
		{
			auto params = a_session.ringParams;
			params.autoHit = true;
			params.missEndsRing = false;
			a_session.countdown = 0.0f;
			a_session.ring.Start(params, 0);
			a_session.ring.Update(3600.0f);  // the needle runs the whole way at once
			a_session.lastRingUpdate = Clock::now();
			TakeRingPresses();
			Sound::Play(Sound::Cue::kHit);
		}

		void UpdateRing(Session& a_session)
		{
			const auto now = Clock::now();
			auto       presses = TakeRingPresses();
			const float dt = ImGui::GetIO().DeltaTime;

			if (a_session.countdown > 0.0f) {
				a_session.countdown -= dt;
				if (a_session.countdown <= 0.0f) {
					a_session.ring.Start(a_session.ringParams, static_cast<std::uint32_t>(now.time_since_epoch().count()));
					a_session.lastRingUpdate = now;
					Sound::Play(Sound::Cue::kRingStart);
				}
				return;
			}

			if (a_session.ring.Running()) {
				const auto before = a_session.ring.Results();
				// Replay presses at the moment they happened, not at frame time.
				const auto mouse = ImGui::GetIO().MousePos;
				const bool overSkip = mouse.x >= a_session.skipMin.x && mouse.x <= a_session.skipMax.x && mouse.y >= a_session.skipMin.y &&
				                      mouse.y <= a_session.skipMax.y;
				std::erase_if(presses, [&](const RingPress& a_press) { return a_press.mouse && overSkip; });
				std::ranges::sort(presses, {}, &RingPress::time);
				for (const auto& [t, fromMouse] : presses) {
					if (t > a_session.lastRingUpdate) {
						a_session.ring.Update(std::chrono::duration<float>(t - a_session.lastRingUpdate).count());
						a_session.lastRingUpdate = t;
					}
					a_session.ring.Press();
				}
				a_session.ring.Update(std::chrono::duration<float>(now - a_session.lastRingUpdate).count());
				a_session.lastRingUpdate = now;
				PlayJudgementCues(before, a_session.ring);
			} else {
				a_session.ring.Update(dt);
				a_session.sinceFinished += dt;
			}
		}

		void CommitSynthesis(Session& a_session)
		{
			game::CommitRequest request;
			request.recipeId = a_session.recipeId;
			request.materials = a_session.slots;
			request.catalyst = a_session.catalyst;
			request.traits = a_session.traits;
			request.ring = a_session.ring.Summary();
			a_session.screen = Screen::kBrewing;

			AddTask([request, snapshot = a_session.snapshot] {
				auto result = game::Commit(snapshot, request);
				auto fresh = game::TakeSnapshot();
				std::scoped_lock lock(g_lock);
				if (!g_open) {
					return;
				}
				const auto recipeId = g_session.recipeId;
				g_session.snapshot = std::move(fresh);
				SelectRecipe(g_session, recipeId);
				g_session.result = std::move(result);
				g_session.screen = Screen::kResult;
			});
		}

		void DrawRing(Session& a_session)
		{
			UpdateRing(a_session);
			const auto& ring = a_session.ring;
			const auto  recipe = Cat().FindRecipe(a_session.recipeId);

			ImGui::PushFont(Renderer::LargeFont());
			const char* title = "JUDGEMENT RING";
			ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(title).x) * 0.5f + ImGui::GetCursorPosX());
			ImGui::TextColored(ToVec4(Colors::kGold), "%s", title);
			ImGui::PopFont();
			if (recipe) {
				const auto sub = std::format("Synthesizing {}", recipe->name);
				ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(sub.c_str()).x) * 0.5f + ImGui::GetCursorPosX());
				ImGui::TextDisabled("%s", sub.c_str());
			}

			const ImVec2 avail = ImGui::GetContentRegionAvail();
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const float  footer = ImGui::GetTextLineHeightWithSpacing() * 4.0f;
			const float  extent = std::min(avail.x, avail.y - footer) * 0.5f;
			const float  radius = extent * 0.70f;  // leaves room for the mandala's symbol band
			const ImVec2 center{ origin.x + avail.x * 0.5f, origin.y + (avail.y - footer) * 0.5f };
			const float  thickness = radius * 0.16f;
			auto*        draw = ImGui::GetWindowDrawList();
			const float  glow = ring.LastJudgedArea() >= 0 && ring.SinceLastJudgement() >= 0.0f &&
			                           ring.Results()[static_cast<std::size_t>(ring.LastJudgedArea())] != atelier::AreaResult::kMiss ?
			                       std::max(0.0f, 1.0f - ring.SinceLastJudgement() / 0.5f) :
			                       0.0f;

			// Body and track, over the mandala.
			draw->AddCircleFilled(center, radius + thickness, Colors::kRingBase, 96);
			DrawMandala(draw, center, radius, thickness, extent * 0.97f, glow);
			draw->AddCircle(center, radius + thickness, Colors::kGold, 96, 2.0f);
			draw->AddCircle(center, radius, Colors::kRingTrack, 96, thickness);
			for (int deg = 0; deg < 360; deg += 30) {
				const float  a = ToRadians(static_cast<float>(deg));
				const ImVec2 dir{ std::cos(a), std::sin(a) };
				draw->AddLine({ center.x + dir.x * (radius - thickness * 0.5f), center.y + dir.y * (radius - thickness * 0.5f) },
					{ center.x + dir.x * (radius - thickness * 0.8f), center.y + dir.y * (radius - thickness * 0.8f) }, Colors::kMuted, 2.0f);
			}

			// Hit / strike areas.
			const auto& areas = ring.Running() || ring.Finished() ? ring.Areas() : std::vector<atelier::RingArea>{};
			const auto& results = ring.Results();
			const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 8.0f);
			for (std::size_t i = 0; i < areas.size(); ++i) {
				const auto& area = areas[i];
				ImU32       hit = Colors::kHitArea;
				ImU32       strike = Colors::kStrikeArea;
				switch (results[i]) {
				case atelier::AreaResult::kHit:
					hit = strike = Colors::kGood;
					break;
				case atelier::AreaResult::kStrike:
					hit = strike = Colors::kGold;
					break;
				case atelier::AreaResult::kMiss:
					hit = strike = IM_COL32(80, 70, 60, 255);
					break;
				case atelier::AreaResult::kPending:
					if (static_cast<int>(i) == ring.CurrentArea()) {
						const ImVec4 base = ToVec4(Colors::kHitArea);
						const float  t = pulse * 0.4f;
						hit = ImGui::GetColorU32({ base.x + (1.0f - base.x) * t, base.y + (0.85f - base.y) * t, base.z + (0.5f - base.z) * t, 1.0f });
					}
					break;
				}
				Arc(draw, center, radius, area.start, area.strikeStart, hit, thickness);
				Arc(draw, center, radius, area.strikeStart, area.end, strike, thickness);
			}

			// Needle.
			const float  needle = ring.Running() || ring.Finished() ? ring.Needle() : 0.0f;
			const float  na = ToRadians(needle);
			const ImVec2 tip{ center.x + std::cos(na) * (radius + thickness * 0.9f), center.y + std::sin(na) * (radius + thickness * 0.9f) };
			draw->AddLine(center, tip, IM_COL32(255, 230, 160, 90), thickness * 0.35f);
			draw->AddLine(center, tip, Colors::kNeedle, 3.0f * Renderer::Scale());
			draw->AddCircleFilled(center, radius * 0.42f, Colors::kRingBase, 64);
			DrawMandalaCenter(draw, center, radius * 0.42f, glow);
			draw->AddCircle(center, radius * 0.42f, Colors::kGold, 64, 2.0f);
			draw->AddCircleFilled(center, 6.0f * Renderer::Scale(), Colors::kGold);

			// Centre text.
			auto* large = Renderer::LargeFont();
			auto  centerText = [&](const std::string& a_text, ImU32 a_color, float a_dy) {
                const ImVec2 size = large->CalcTextSizeA(large->FontSize, FLT_MAX, 0.0f, a_text.c_str());
                draw->AddText(large, large->FontSize, { center.x - size.x * 0.5f, center.y - size.y * 0.5f + a_dy }, a_color, a_text.c_str());
			};

			if (a_session.countdown > 0.0f) {
				centerText("Ready...", Colors::kParchment, 0.0f);
			} else if (ring.LastJudgedArea() >= 0 && ring.SinceLastJudgement() < 0.6f) {
				const auto  result = results[static_cast<std::size_t>(ring.LastJudgedArea())];
				const float alpha = 1.0f - ring.SinceLastJudgement() / 0.6f;
				const char* word = result == atelier::AreaResult::kStrike ? "STRIKE!" : result == atelier::AreaResult::kHit ? "HIT" : "MISS";
				ImVec4      color = ToVec4(result == atelier::AreaResult::kStrike ? Colors::kGold : result == atelier::AreaResult::kHit ? Colors::kGood : Colors::kBad);
				color.w = alpha;
				centerText(word, ImGui::GetColorU32(color), 0.0f);
			} else if (ring.Finished()) {
				const auto summary = ring.Summary();
				if (summary.Perfect()) {
					centerText("PERFECT!", Colors::kGold, 0.0f);
				} else if (summary.firstAreaMissed) {
					centerText("Botched", Colors::kBad, 0.0f);
				} else if (summary.Clean()) {
					centerText("Success", Colors::kGood, 0.0f);
				} else {
					centerText(std::format("{} / {}", summary.hits, summary.areas), Colors::kParchment, 0.0f);
				}
			} else {
				centerText(std::format("{} / {}", ring.CurrentArea() + 1, static_cast<int>(areas.size())), Colors::kParchment, 0.0f);
			}

			ImGui::Dummy({ avail.x, avail.y - footer });

			if (!ring.Finished()) {
				ImGui::TextWrapped("Press [Space], [E], left mouse or (A) as the needle crosses each orange hit area. Hits raise quality a little (+2%%); "
								   "the red strike zone at its end is a critical (+6%%). Striking every area is a Perfect synthesis (+10%% more and +1 item). "
								   "A miss costs 15%% and ruins the remaining stages. Higher quality means stronger, longer-lasting effects.");
				const bool skip = ImGui::Button("Skip ring  [Tab] / (Y)") || ImGui::IsKeyPressed(ImGuiKey_Tab, false) ||
				                  ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false);
				a_session.skipMin = ImGui::GetItemRectMin();
				a_session.skipMax = ImGui::GetItemRectMax();
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Finish this synthesis as Auto mode would: every area is hit, no strike or Perfect bonus, no miss.");
				}
				if (skip) {
					SkipRing(a_session);
				}
				return;
			}
			a_session.skipMin = a_session.skipMax = { -1.0f, -1.0f };

			const auto summary = ring.Summary();
			const float factor = atelier::RingQualityFactor(summary);
			ImGui::Text("Hits %d / %d   Strikes %d   Quality", summary.hits, summary.areas, summary.strikes);
			ImGui::SameLine();
			ImGui::TextColored(ToVec4(factor >= 1.0f ? Colors::kGood : Colors::kBad), "x%.2f", factor);
			if (summary.firstAreaMissed) {
				ImGui::TextColored(ToVec4(Colors::kBad), "The mixture curdled at the first stage: quality and yield will suffer.");
			}
			ImGui::BeginDisabled(a_session.sinceFinished < 0.6f);
			if (ImGui::Button("Continue", { 220.0f * Renderer::Scale(), 0.0f })) {
				CommitSynthesis(a_session);
			}
			ImGui::EndDisabled();
			if (a_session.sinceFinished >= 0.6f && a_session.sinceFinished < 0.7f) {
				ImGui::SetKeyboardFocusHere(-1);
			}
		}

		// -----------------------------------------------------------------------------
		// Result screen
		// -----------------------------------------------------------------------------

		void DrawResult(Session& a_session)
		{
			if (!a_session.result) {
				a_session.screen = Screen::kWorkshop;
				return;
			}
			const auto& result = *a_session.result;
			if (!result.ok) {
				ImGui::TextColored(ToVec4(Colors::kBad), "The synthesis failed: %s", result.error.c_str());
				ImGui::TextDisabled("Nothing was consumed.");
			} else {
				const auto& brew = result.brew;
				ImGui::PushFont(Renderer::LargeFont());
				ImGui::TextColored(ToVec4(Colors::kGold), "%s  x%d", brew.name.c_str(), result.yield);
				ImGui::PopFont();
				if (!result.yieldBonus.empty()) {
					ImGui::TextColored(ToVec4(Colors::kGood), "Batch size: %s", result.yieldBonus.c_str());
				}
				ImGui::Text("Quality %d  [%s]", brew.quality, atelier::QualityRank(brew.quality));
				if (result.ring.Perfect()) {
					ImGui::SameLine();
					ImGui::TextColored(ToVec4(Colors::kGold), "  Perfect synthesis!");
				}
				ImGui::Separator();
				for (const auto& effect : brew.effects) {
					ImGui::BulletText("%s", EffectText(effect.effectKey, effect.magnitude, effect.duration).c_str());
					EffectBreakdownTooltip(effect);
				}
				if (brew.kind == atelier::OutputKind::kMaterial) {
					ImGui::Text("Elements:");
					ImGui::SameLine();
					ElementChips(brew.elements);
					std::string categories;
					for (const auto& c : brew.categories) {
						categories += (categories.empty() ? "" : ", ") + std::format("({})", c);
					}
					ImGui::TextDisabled("Counts as %s", categories.c_str());
				}
				if (!brew.traits.empty()) {
					ImGui::Text("Traits:");
					for (const auto& trait : brew.traits) {
						ImGui::SameLine();
						ImGui::TextColored(ToVec4(Colors::kMaterial), "<%s>", TraitName(trait).c_str());
						if (ImGui::IsItemHovered()) {
							ImGui::BeginTooltip();
							TraitTooltip(trait);
							ImGui::EndTooltip();
						}
					}
				}
				ImGui::TextDisabled("Value %d   Weight %.2f", brew.value, brew.weight);
				for (const auto& id : result.unlocked) {
					if (const auto recipe = Cat().FindRecipe(id)) {
						ImGui::Spacing();
						ImGui::TextColored(ToVec4(Colors::kGold), "Inspiration! You devised a new recipe: %s", recipe->name.c_str());
					}
				}
			}

			ImGui::Spacing();
			if (ImGui::Button("Back to the workbench")) {
				a_session.result.reset();
				a_session.screen = Screen::kWorkshop;
			}
			ImGui::SameLine();
			if (ImGui::Button("Close")) {
				RequestClose();
			}
		}

		// The game's Scaleform cursor is rendered before our overlay and ends up hidden
		// beneath the window, so paint a cursor at the same position as the last thing
		// in the frame.
		void DrawCursor()
		{
			const auto& io = ImGui::GetIO();
			if (!ImGui::IsMousePosValid(&io.MousePos)) {
				return;
			}
			const float  s = 1.4f * Renderer::Scale();
			const ImVec2 p = io.MousePos;
			auto         pt = [&](float x, float y) { return ImVec2{ p.x + x * s, p.y + y * s }; };
			const ImVec2 arrow[] = { pt(0, 0), pt(0, 17), pt(4.5f, 13), pt(7.5f, 20), pt(10.5f, 18.8f), pt(7.5f, 12), pt(13, 12) };

			auto* draw = ImGui::GetForegroundDrawList();
			draw->AddConcavePolyFilled(arrow, IM_ARRAYSIZE(arrow), IM_COL32(25, 18, 12, 255));
			// Inset fill so the dark outline stays visible on light and dark backgrounds.
			const ImVec2 inner[] = { pt(1.5f, 3.5f), pt(1.5f, 13.5f), pt(5, 10.5f), pt(8.2f, 17.6f), pt(9, 17.2f), pt(5.8f, 10.3f), pt(9.6f, 10.3f) };
			draw->AddConcavePolyFilled(inner, IM_ARRAYSIZE(inner), Colors::kGold);
		}

		void DrawHint()
		{
			const auto& io = ImGui::GetIO();
			ImGui::SetNextWindowPos({ io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.06f }, ImGuiCond_Always, { 0.5f, 0.0f });
			ImGui::SetNextWindowBgAlpha(0.75f);
			constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
			                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
			if (ImGui::Begin("##atelier_hint", nullptr, flags)) {
				ImGui::TextColored(ToVec4(Colors::kGold), "[%s]", KeyName(game::Config::Current().hotkeyKeyboard).c_str());
				ImGui::SameLine();
				ImGui::TextUnformatted("Atelier Synthesis");
			}
			ImGui::End();
		}
	}

	// ---------------------------------------------------------------------------------
	// Public API
	// ---------------------------------------------------------------------------------

	void Register()
	{
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->Register(kMenuName, &AtelierMenu::Create);
			ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuWatcher::Get());
			logger::info("Menu registered");
		}
	}

	void RequestToggle()
	{
		auto* queue = RE::UIMessageQueue::GetSingleton();
		auto* ui = RE::UI::GetSingleton();
		if (!queue || !ui) {
			return;
		}
		if (g_open) {
			queue->AddMessage(kMenuName, RE::UI_MESSAGE_TYPE::kHide, nullptr);
			return;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->Is3DLoaded()) {
			return;
		}
		const bool atLab = ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME) && game::IsUsingAlchemyLab();
		if (ui->GameIsPaused() && !atLab) {
			return;  // some other menu (or the console) is open
		}
		if (player->IsInCombat()) {
			Notify("You can't concentrate on alchemy in the middle of a fight.");
			return;
		}
		std::string reason;
		if (!game::CanSynthesizeHere(&reason)) {
			Notify(reason);
			return;
		}
		queue->AddMessage(kMenuName, RE::UI_MESSAGE_TYPE::kShow, nullptr);
	}

	bool IsOpen()
	{
		return g_open;
	}

	bool IsRingActive()
	{
		return g_open && g_ringActive;
	}

	bool ShouldRender()
	{
		return g_open || g_hintVisible;
	}

	void Draw()
	{
		if (!g_open) {
			if (g_hintVisible) {
				DrawHint();
			}
			return;
		}

		std::scoped_lock lock(g_lock);
		auto&            session = g_session;
		const auto&      io = ImGui::GetIO();

		ImGui::SetNextWindowPos({ io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f }, ImGuiCond_Always, { 0.5f, 0.5f });
		ImGui::SetNextWindowSize({ io.DisplaySize.x * 0.86f, io.DisplaySize.y * 0.86f }, ImGuiCond_Always);
		constexpr auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
		                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
		if (ImGui::Begin("Atelier Synthesis", nullptr, flags)) {
			// Header.
			ImGui::PushFont(Renderer::LargeFont());
			ImGui::TextColored(ToVec4(Colors::kGold), "Atelier Synthesis");
			ImGui::PopFont();
			ImGui::SameLine();
			ImGui::TextDisabled("   Alchemy %.0f   (hover for perk bonuses)", session.snapshot.skill);
			if (ImGui::IsItemHovered()) {
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
				ImGui::TextUnformatted("Perk and Fortify Alchemy bonuses, as the vanilla alchemy lab applies them:");
				for (const auto& line : session.snapshot.potencySummary) {
					ImGui::Bullet();
					ImGui::TextWrapped("%s", line.c_str());
				}
				if (session.snapshot.potencySummary.empty()) {
					ImGui::TextDisabled("Perks are disabled in the INI.");
				}
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
			const char* closeHint = "[Esc] Close";
			ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - ImGui::CalcTextSize(closeHint).x);
			ImGui::TextDisabled("%s", closeHint);
			if (!session.notice.empty()) {
				ImGui::TextColored(ToVec4(Colors::kGood), "%s", session.notice.c_str());
			}
			for (const auto& error : game::Database::Get().Errors()) {
				ImGui::TextColored(ToVec4(Colors::kBad), "Data error: %s", error.c_str());
			}
			ImGui::Separator();

			switch (session.screen) {
			case Screen::kWorkshop:
				DrawWorkshop(session);
				break;
			case Screen::kRing:
				DrawRing(session);
				break;
			case Screen::kBrewing:
				ImGui::TextDisabled("Brewing...");
				break;
			case Screen::kResult:
				DrawResult(session);
				break;
			}

			const bool anyPopup = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
			const bool back = ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
			// Escape in a text field only leaves the field (WantTextInput is last frame's state).
			if (back && !anyPopup && !io.WantTextInput) {
				if (session.screen == Screen::kResult) {
					session.result.reset();
					session.screen = Screen::kWorkshop;
				} else if (session.screen == Screen::kWorkshop) {
					RequestClose();
				}
			}
		}
		ImGui::End();
		DrawCursor();

		g_ringActive = session.screen == Screen::kRing && !session.ring.Finished();
	}
}
