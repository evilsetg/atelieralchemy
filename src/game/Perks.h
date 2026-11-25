#pragma once

namespace game::Perks
{
	// Writes every perk in the load order that touches alchemy (potency, purity, poison
	// doses, potions created, harvesting) to the log, with entry functions, values and
	// conditions, plus the Alchemy perk tree. Reference data for perk overhaul support.
	void DumpAlchemyPerks();

	// Logs which of those perks the player currently has.
	void LogPlayerAlchemyPerks();

	struct Potency
	{
		float       multiplier{ 1.0f };
		std::string breakdown;  // "Alchemist x2.00, Benefactor x1.25"
	};

	// Evaluates the player's "Mod Alchemy Effectiveness" perk entries for one magic effect,
	// the way the vanilla alchemy menu would: vanilla perks, Ordinator and any other perk
	// mod alike, including Fortify Alchemy (applied by perks too). Main thread only.
	Potency AlchemyEffectiveness(const RE::EffectSetting* a_effect, bool a_makingPoison);

	// "Mod Potions Created" entries (Ordinator's Double Toil and Trouble, ...): how many
	// potions one vanilla brew becomes. Random conditions are rolled, so call once per
	// synthesis. Main thread only.
	Potency PotionsCreated(bool a_makingPoison);
}
