# Atelier Alchemy

## Description

Atelier Alchemy is an alchemy overhaul inspired by the synthesis system of the *Atelier* series. Combine ingredients and crafted intermediates into potions and poisons whose strength depends on what you put in. Every brew is finished with a *Shadow Hearts*-style **Judgement Ring**. The vanilla alchemy lab still works as before; open the synthesis window with **K** at any alchemy lab.

## Installation instructions

1. Install the requirements below.
2. Install the archive with a mod manager (MO2 or Vortex), or copy its contents into `Skyrim Special Edition/Data`.
3. Enable `AtelierAlchemy.esp`. It's ESL-flagged, so it doesn't take a load order slot.
4. Optional: adjust the hotkey, ring mode, volume and balance in `SKSE/Plugins/AtelierAlchemy.ini`.

It's safe to add to an existing save.

## Main features

- **Quality and traits:** every ingredient you pick up gets a quality (1–999) and may carry traits such as *Potent*, *Lingering* or *Charged*. You choose which traits your product inherits, and pairs fuse into stronger ones.
- **Elements and effect lines:** ingredients carry Fire, Ice, Lightning and Wind. Raising a recipe's elements climbs its effect lines toward stronger effects, more quality, extra yield or more trait slots.
- **Crafting chains:** 42 recipes, including 11 intermediate materials (extracts, tinctures, oils, essences, Philosopher's Salt, Quintessence…) that pass quality and traits up the chain. New recipes unlock with skill or through "ideas".
- **The Judgement Ring:** stop the needle in each area to finish a synthesis. Hits and strikes raise quality, a Perfect gives an extra item, and a miss ends the ring early. Every recipe has its own ring pattern, drawn over a turning alchemical mandala. Easy and Auto modes and a Skip button are available.
- **Balanced against vanilla:** effect strength is measured against what vanilla alchemy could brew for you right now. Higher quality and harder tiers take you beyond it.
- **Signature brews:** high-level recipes like Titan's Draught and Dragonblood Tonic last longer and make effects vanilla alchemy can't: Fortify Armor Rating, faster shout recovery, Muffle and Ethereal.
- **Rewarding batches:** a potion or poison synthesis makes two items, materials two or three. Perfect rings and some effect-line tiers add more.
- **Perk support without patches:** perks are read straight from their records, so vanilla, Ordinator and other perk overhauls apply as at a vanilla lab. That includes Fortify Alchemy, and Ordinator's Double Toil and Trouble doubles your potion and poison batches.
- **Real items:** products are real potions and poisons with real magic effects. They sell to merchants, grant Alchemy XP and are saved in the SKSE co-save.
- **Moddable data:** all recipes, traits and ring patterns are plain JSON. Add-ons can drop in their own file.

## Requirements

- Skyrim Special Edition 1.5.97 or Anniversary Edition 1.6.x
- [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

## Source code

The plugin is licensed under the GPL-3.0. The full source code is available at https://github.com/evilsetg/atelieralchemy .

## Shout outs

- **Inspiration:** Gust's *Atelier* series for the synthesis system, and Sacnoth's *Shadow Hearts* for the Judgement Ring. No assets, names or text from those games are used.
- **Libraries:** built on [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR), [Dear ImGui](https://github.com/ocornut/imgui), [spdlog](https://github.com/gabime/spdlog) and [nlohmann/json](https://github.com/nlohmann/json).
- **Font:** the bundled font is [Alegreya Sans](https://github.com/huertatipografica/Alegreya-Sans) by Huerta Tipográfica (SIL Open Font License).
