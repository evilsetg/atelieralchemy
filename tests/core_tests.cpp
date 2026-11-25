// Host-side tests for the game-independent core (synthesis + Judgement Ring).
// Run with: ./scripts/test-core.sh

#include "core/JudgementRing.h"
#include "core/Synthesis.h"

#include <cstdio>
#include <cstdlib>

using namespace atelier;

static int g_failures = 0;

#define CHECK(cond)                                                          \
	do {                                                                     \
		if (!(cond)) {                                                       \
			std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
			++g_failures;                                                    \
		}                                                                    \
	} while (0)

static Catalog MakeCatalog()
{
	Catalog catalog;
	catalog.traits["potent"] = TraitDef{ .id = "potent", .name = "Potent", .magnitudePct = 15.0f };
	catalog.traits["lasting"] = TraitDef{ .id = "lasting", .name = "Lasting", .durationPct = 25.0f };
	catalog.traits["potent_lasting"] = TraitDef{ .id = "potent_lasting", .name = "Enduring Potency", .magnitudePct = 20.0f, .durationPct = 30.0f };
	catalog.traits["light"] = TraitDef{ .id = "light", .name = "Light", .weightPct = -50.0f };
	catalog.traits["venom"] = TraitDef{ .id = "venom", .name = "Venomous", .magnitudePct = 30.0f, .poisonsOnly = true };
	catalog.combos.push_back({ "potent", "lasting", "potent_lasting" });

	RecipeDef recipe;
	recipe.id = "healing";
	recipe.name = "Healing Salve";
	recipe.kind = OutputKind::kPotion;
	recipe.level = 10;
	recipe.baseYield = 2;
	recipe.baseValue = 40;
	recipe.requirements = { { true, "Plants", 2 }, { false, "salt", 1 } };
	EffectLine health{ "Healing", Element::kFire, {} };
	health.tiers.push_back({ .threshold = 2, .kind = EffectTier::Kind::kEffect, .effectKey = "RestoreHealth", .magnitude = 20.0f });
	health.tiers.push_back({ .threshold = 5, .kind = EffectTier::Kind::kEffect, .effectKey = "RestoreHealth", .magnitude = 50.0f });
	health.tiers.push_back({ .threshold = 8, .kind = EffectTier::Kind::kYield, .amount = 1 });
	EffectLine wind{ "Breath", Element::kWind, {} };
	wind.tiers.push_back({ .threshold = 3, .kind = EffectTier::Kind::kEffect, .effectKey = "RestoreStamina", .magnitude = 30.0f });
	recipe.lines = { health, wind };
	recipe.unlocks.push_back({ .recipe = "greater_healing", .useCategory = "Fungus" });
	catalog.recipes[recipe.id] = recipe;

	catalog.catalysts["fire_salts"] = CatalystDef{ .itemKey = "fire_salts", .name = "Fire Salts", .addElements = { 3, 0, 0, 0 }, .qualityBonus = 10 };
	return catalog;
}

static MaterialInput Plant(int quality, ElementValues elements, std::vector<std::string> traits = {})
{
	return MaterialInput{ "herb", "Herb", { "Plants" }, elements, quality, std::move(traits) };
}

static void TestFusion()
{
	std::puts("trait fusion");
	const auto catalog = MakeCatalog();
	auto       fused = FuseTraits(catalog, { "potent", "light", "lasting" });
	CHECK(fused.size() == 2);
	CHECK(fused[0] == "potent_lasting");
	CHECK(fused[1] == "light");
	CHECK(FuseTraits(catalog, { "light", "light" }).size() == 1);
}

static void TestEvaluate()
{
	std::puts("evaluate");
	const auto     catalog = MakeCatalog();
	SynthesisInput input;
	input.recipe = catalog.FindRecipe("healing");
	input.stats.skill = 0.0f;
	input.materials = { Plant(100, { 1, 0, 0, 1 }, { "potent" }), Plant(50, { 1, 0, 0, 1 }, { "lasting" }),
		MaterialInput{ "salt", "Salt", { "Minerals" }, { 0, 0, 0, 1 }, 60, { "light" } } };
	input.chosenTraits = { "potent", "lasting", "light", "venom" };

	auto out = Evaluate(input, catalog, nullptr);
	CHECK(out.valid);
	CHECK(out.elements[0] == 2);
	CHECK(out.elements[3] == 3);
	CHECK(out.lines[0].reachedTier == 0);
	CHECK(out.lines[1].reachedTier == 0);
	CHECK(out.quality == 70);  // (100 + 50 + 60) / 3
	CHECK(out.yield == 2);
	CHECK(out.finalTraits.size() == 2);  // fused + light; venom is poison-only and not on any material
	CHECK(out.brew.effects.size() == 2);
	CHECK(out.brew.weight < 0.5f);

	// Catalyst pushes Fire to 5 -> tier 2 effect, +10 quality.
	input.catalyst = MaterialInput{ "fire_salts", "Fire Salts", { "Minerals" }, {}, 70, {} };
	out = Evaluate(input, catalog, nullptr);
	CHECK(out.lines[0].reachedTier == 1);
	CHECK(out.quality == 80);  // (280 / 4) + 10
	const float expected = 50.0f * QualityMagnitudeMult(80) * 1.20f;
	CHECK(std::abs(out.brew.effects[0].magnitude - std::round(expected)) < 0.01f);

	// Wrong material in a slot is rejected.
	auto bad = input;
	bad.materials[2] = Plant(10, {});
	CHECK(!Evaluate(bad, catalog, nullptr).valid);
	bad.materials.pop_back();
	CHECK(!Evaluate(bad, catalog, nullptr).valid);
}

static void TestElementTraits()
{
	std::puts("element traits");
	auto catalog = MakeCatalog();
	catalog.traits["charged"] = TraitDef{ .id = "charged", .name = "Charged", .elements = { 0, 0, 2, 0 } };
	SynthesisInput input;
	input.recipe = catalog.FindRecipe("healing");
	input.materials = { Plant(50, { 1, 0, 0, 1 }, { "charged" }), Plant(50, { 1, 0, 0, 1 }),
		MaterialInput{ "salt", "Salt", { "Minerals" }, { 0, 0, 0, 1 }, 50, {} } };
	// The trait counts whether or not the product inherits it.
	const auto out = Evaluate(input, catalog, nullptr);
	CHECK(out.elements[2] == 2);
	CHECK(EffectiveElements(input.materials[0], catalog)[2] == 2);
	CHECK(EffectiveElements(input.materials[1], catalog)[2] == 0);
}

static void TestQualityCurves()
{
	std::puts("quality curves");
	CHECK(std::abs(QualityMagnitudeMult(80) - 1.0f) < 1e-4f);
	CHECK(std::abs(QualityMagnitudeMult(200) - 1.6f) < 1e-4f);
	CHECK(std::abs(QualityMagnitudeMult(999) - 2.0f) < 1e-4f);
	CHECK(QualityMagnitudeMult(1) > 0.6f && QualityMagnitudeMult(1) < 0.61f);
	CHECK(std::abs(QualityDurationMult(80) - 1.0f) < 1e-4f);
	CHECK(std::abs(QualityDurationMult(200) - 1.3f) < 1e-4f);
	CHECK(std::abs(QualityDurationMult(999) - 1.5f) < 1e-4f);
}

static void TestVanillaReferences()
{
	std::puts("vanilla references");
	auto catalog = MakeCatalog();
	auto& recipe = catalog.recipes["healing"];
	recipe.lines[0].tiers[0].vanillaScale = 0.8f;
	recipe.lines[0].tiers[1].vanillaScale = 1.0f;
	recipe.lines[1].tiers[0].vanillaScale = 1.1f;

	SynthesisInput input;
	input.recipe = &recipe;
	input.stats.skill = 0.0f;
	input.stats.magnitudeMult = 9.0f;  // perks must not apply twice to vanilla-relative tiers
	input.materials = { Plant(100, { 1, 0, 0, 1 }), Plant(100, { 1, 0, 0, 1 }),
		MaterialInput{ "salt", "Salt", { "Minerals" }, { 0, 0, 0, 1 }, 100, {} } };
	// Fortify Enchanting at Alchemy 58 with 3 Alchemist ranks: 1 * 4 * 1.29 * 1.6 = 8.26 in vanilla.
	input.references["RestoreHealth"] = EffectReference{ .magnitude = 8.256f, .duration = 60.0f };
	// Invisibility-like: potency lengthens the effect.
	input.references["RestoreStamina"] = EffectReference{ .magnitude = 1.0f, .duration = 40.0f, .scalesDuration = true };

	const auto out = Evaluate(input, catalog, nullptr);
	CHECK(out.quality == 100);  // quality x1.1 magnitude, x1.05 duration
	CHECK(out.brew.effects.size() == 2);
	CHECK(std::abs(out.brew.effects[0].magnitude - 7.3f) < 0.01f);  // 8.256 * 0.8 * 1.1
	CHECK(out.brew.effects[0].duration == 63);                       // 60 * 1.05
	CHECK(std::abs(out.brew.effects[1].magnitude - 1.0f) < 0.01f);
	CHECK(out.brew.effects[1].duration == 48);  // 40 * 1.1 * 1.1

	// Trait bonuses are capped: +60% counts as +25%.
	catalog.traits["strong"] = TraitDef{ .id = "strong", .name = "Strong", .magnitudePct = 60.0f };
	auto strong = input;
	strong.materials[0].traits = { "strong" };
	strong.chosenTraits = { "strong" };
	const auto capped = Evaluate(strong, catalog, nullptr);
	CHECK(std::abs(capped.brew.effects[0].magnitude - 9.1f) < 0.01f);  // 8.256 * 0.8 * 1.1 * 1.25
	CHECK(!capped.brew.effects[0].breakdown.empty());

	// No reference (e.g. an add-on effect): absolute values with perks.
	input.references.clear();
	CHECK(std::abs(Evaluate(input, catalog, nullptr).brew.effects[0].magnitude - 20.0f * 1.1f * 9.0f) < 0.51f);
}

static void TestRingModifiers()
{
	std::puts("ring modifiers");
	const auto     catalog = MakeCatalog();
	SynthesisInput input;
	input.recipe = catalog.FindRecipe("healing");
	input.stats.skill = 0.0f;
	input.materials = { Plant(100, { 1, 0, 0, 1 }), Plant(100, { 1, 0, 0, 1 }),
		MaterialInput{ "salt", "Salt", { "Minerals" }, { 0, 0, 0, 1 }, 100, {} } };

	RingSummary perfect{ .hits = 3, .strikes = 3, .misses = 0, .areas = 3 };
	auto        out = Evaluate(input, catalog, &perfect);
	CHECK(out.yield == 3);
	CHECK(out.quality == static_cast<int>(std::lround(100.0f * std::pow(1.06f, 3.0f) * 1.1f)));

	// Plain hits earn a little; a clean ring beats no ring (or Auto mode).
	RingSummary clean{ .hits = 3, .strikes = 0, .misses = 0, .areas = 3 };
	out = Evaluate(input, catalog, &clean);
	CHECK(out.yield == 2);
	CHECK(out.quality == static_cast<int>(std::lround(100.0f * std::pow(1.02f, 3.0f))));
	CHECK(out.quality > Evaluate(input, catalog, nullptr).quality);
	RingSummary automatic = clean;
	automatic.automatic = true;
	CHECK(Evaluate(input, catalog, &automatic).quality == 100);

	RingSummary botched{ .hits = 0, .strikes = 0, .misses = 3, .areas = 3, .firstAreaMissed = true };
	out = Evaluate(input, catalog, &botched);
	CHECK(out.yield == 1);
	CHECK(out.quality < 40);

	auto unlocks = CheckUnlocks(*input.recipe, input, out);
	CHECK(unlocks.empty());
	input.materials[0].categories.push_back("Fungus");
	unlocks = CheckUnlocks(*input.recipe, input, out);
	CHECK(unlocks.size() == 1);
}

static void TestRing()
{
	std::puts("judgement ring");
	RingParams params;
	params.areaCount = 3;
	params.hitWidthDeg = 40.0f;
	params.strikeWidthDeg = 8.0f;
	params.degreesPerSecond = 100.0f;

	std::mt19937 rng(1234);
	const auto   areas = JudgementRing::Layout(params, rng);
	CHECK(areas.size() == 3);
	for (std::size_t i = 0; i < areas.size(); ++i) {
		CHECK(areas[i].start >= 25.0f);
		CHECK(areas[i].end <= 345.0f);
		CHECK(areas[i].end - areas[i].start == 40.0f);
		if (i > 0) {
			CHECK(areas[i].start > areas[i - 1].end);
		}
	}

	// Press inside each strike area -> perfect.
	JudgementRing ring;
	ring.Start(params, 99);
	for (const auto& area : ring.Areas()) {
		while (ring.Needle() < area.strikeStart + 1.0f) {
			ring.Update(0.005f);
		}
		ring.Press();
	}
	CHECK(ring.Finished());
	CHECK(ring.Summary().Perfect());

	// Press in the hit area but before the strike area -> hit.
	ring.Start(params, 99);
	const auto first = ring.Areas()[0];
	while (ring.Needle() < first.start + 2.0f) {
		ring.Update(0.005f);
	}
	ring.Press();
	CHECK(ring.Results()[0] == AreaResult::kHit);

	// Letting the needle pass the second area ends the ring.
	while (ring.Running()) {
		ring.Update(0.01f);
	}
	auto summary = ring.Summary();
	CHECK(summary.hits == 1);
	CHECK(summary.misses == 2);
	CHECK(!summary.firstAreaMissed);

	// Pressing too early fails the first area immediately.
	ring.Start(params, 5);
	ring.Update(0.01f);
	ring.Press();
	CHECK(ring.Finished());
	CHECK(ring.Summary().firstAreaMissed);

	// Auto mode hits everything.
	params.autoHit = true;
	ring.Start(params, 7);
	while (ring.Running()) {
		ring.Update(0.01f);
	}
	CHECK(ring.Summary().Clean());
	CHECK(ring.Summary().strikes == 0);

	// Params scale with skill and difficulty.
	const auto novice = MakeRingParams(4, 15.0f, 50, 0.0f, 1.0f);
	const auto master = MakeRingParams(4, 100.0f, 50, 0.0f, 1.0f);
	CHECK(master.hitWidthDeg > novice.hitWidthDeg);
	CHECK(master.degreesPerSecond < novice.degreesPerSecond);
	CHECK(MakeRingParams(8, 100.0f, 1, 200.0f, 1.0f).hitWidthDeg * 8 < 330.0f);
}

int main()
{
	TestFusion();
	TestEvaluate();
	TestElementTraits();
	TestQualityCurves();
	TestVanillaReferences();
	TestRingModifiers();
	TestRing();
	if (g_failures) {
		std::printf("%d check(s) failed\n", g_failures);
		return EXIT_FAILURE;
	}
	std::puts("all core tests passed");
	return EXIT_SUCCESS;
}
