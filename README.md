# Atelier Alchemy

An alchemy overhaul for Skyrim Special Edition / Anniversary Edition, inspired by the
synthesis systems of the *Atelier* series. Every synthesis ends with a *Shadow Hearts*-style
**Judgement Ring** minigame.

## Features

- **Quality and traits on every ingredient.** When you pick up an ingredient it rolls a
  quality from 1 to 999, based on its rarity and your Alchemy skill, and it may carry traits
  such as *Potent*, *Lingering*, *Precious* or *Steady Hand*. Skyrim stacks ingredients, so
  the plugin keeps a ledger per ingredient and reconciles it with your real inventory.
- **Elements.** Every ingredient has Fire, Ice, Lightning and Wind values. These are derived
  from its own alchemical effects (Health → Fire, Magicka → Ice, Stamina → Lightning,
  stealth/utility → Wind), so ingredients added by other mods work too.
- **Recipes with effect lines.** A recipe asks for ingredients by name or by category, such as
  *(Plants)*, *(Fungus)* or *(Water)*. Each recipe has several effect lines tied to one
  element. As the element total of your ingredients rises, a line climbs through tiers that
  give stronger effects, extra quality, extra yield or extra trait slots.
- **Synthesized materials.** Eleven intermediates feed into other recipes and carry quality
  and traits up the chain. Each has its own effect lines that can raise its elements,
  quality or trait slots before you use it:
  - Basics: *Distilled Water* (Water/Solvent), *Alchemist's Ash* (Ash), *Herbal Extract*
    (Extract), *Beast Tallow* (Oil), *Fungal Paste* (Paste), *Floral Tincture* (Tincture)
    and *Concentrated Venom* (Toxin).
  - Advanced: *Spirit Essence*, *Philosopher's Salt*, *Aether Crystal* and *Quintessence*.

  The starter recipes take raw ingredients. Mid- and high-level potions and poisons need
  these intermediates, for example a Hunter's Draught takes an Extract, an Oil and an insect,
  and a Grand Elixir needs Quintessence.
- **Minor elements and element traits.** Every basic material also has two lines for the
  elements it lacks (Floral Tincture's *Pollen Charge* for Lightning, Distilled Water's
  *Warm Spring* for Fire...). Like every line, they need that element to progress, so feed
  in an ingredient, catalyst or trait that carries it and the material amplifies it for the
  next step. Element traits such as
  *Kindled*, *Frosted*, *Charged* and *Windborne* add +2 of their element to every synthesis
  they're used in; pairs fuse into dual-element traits and finally *Prismatic*.
- **Finding the right ingredient.** Press *Find* on an effect line (or a sort button in the
  ingredient picker) to sort every choice by that line's element and highlight it.
- **Catalysts.** An optional extra slot that adds elements, adds quality or bends the ring.
  For example, Frost Salts slow the needle and a Daedra Heart adds an extra hit area.
- **Trait inheritance and fusion.** You pick which traits the product inherits (3 slots by
  default). Some pairs fuse into stronger traits, for example *Potent* + *Lingering* →
  *Enduring Potency*.
- **Ideas (recipe discovery).** New recipes unlock as your Alchemy skill rises (from 20 up
  to 90; a recipe's level is the skill it unlocks at), or when you synthesize a recipe with a
  particular ingredient. Learning a recipe also teaches any intermediate material it needs.
  The recipe book shows hints.
- **Judgement Ring.** A needle sweeps once around a ring. Press when it crosses each orange
  hit area; the red strike area at its end is a critical. Each hit raises quality by 2% and
  each strike by 6%. Striking every area is a **Perfect** synthesis: another +10% and +1 item.
  A miss costs 15% and, as in Shadow Hearts, ends the ring. Missing the first area botches the
  batch (×0.6 quality, −1 item). The width and speed of the areas depend on your skill, the
  recipe's level, your catalyst and *Steady Hand* traits. Every recipe has its own ring
  pattern (for example *Gentle*: big, big, small; *Needle*: four slim areas with wide
  strikes; *Finale*: four small areas and a big last one), previewed next to the Synthesize
  button. Patterns live in `ring_patterns.json`. The ring sits on a slowly turning mandala of
  alchemical symbols (the four elements, Sun, Moon, Mercury and Salt) that glows on a hit. *Easy* and *Auto* (no bonuses) modes are
  available for accessibility.
  Each judgement has its own sound: a hit lands like a punch, a strike shatters like glass,
  a miss gives a retro menu-cancel blip, and a Perfect ends in a rising arpeggio. The WAVs in
  `SKSE/Plugins/AtelierAlchemy/sounds/` can be replaced with your own 16-bit PCM files.
- **42 recipes** (31 potions and poisons, 11 materials) covering every vanilla alchemy effect: restoration, regeneration, resistances,
  every skill Fortify, waterbreathing, invisibility, carry weight, and poisons from Damage,
  Ravage and Lingering Damage to regen drain, Weakness, Fear, Frenzy, Slow and Paralysis.
- **Balanced against vanilla.** Effect strength is derived from what vanilla alchemy could
  brew for you right now: the most common ingredient values for that effect, multiplied by the vanilla
  potency formula (skill, the game settings behind it, and every perk you own that changes
  alchemy effectiveness, which also covers Fortify Alchemy). A line's first, second and third tier give 80%, 100% and
  110% of that. For effects such as Invisibility, where potency lengthens the effect, the
  share applies to duration. Masterwork recipes multiply those shares: Elixir of Vitality and
  Panacea ×1.25, Grand Elixir ×1.5.
- **Batch sizes.** A potion or poison synthesis makes two items, as a reward for the extra
  work over a vanilla brew; materials come in batches of two or three. A Perfect ring, some effect-line tiers and "potions
  created" perks add more.
- **Quality matters.** On top of that, magnitude scales linearly with quality: Q1 = 0.6×,
  Q80 = 1.0× (a typical mid-game brew), Q200 = 1.6×, capped at 2.0× from Q280. Duration gets
  half the slope (Q200 = 1.3×, capped at 1.5×). Value grows with quality too. Traits add at
  most +25% magnitude and +50% duration in total. Hover an effect in the synthesis window to
  see how its value was calculated.
- **Vanilla integration.** Products are real potions and poisons with real magic effects.
  Alchemy perks apply exactly as at a vanilla lab. The plugin reads the perk records
  themselves, so perk overhauls such as Ordinator (Specialized Healer, Advanced Lab, Magnum
  Opus...) and potency perks from other mods work without a patch. "Potions created" perks
  such as Double Toil and Trouble multiply the batch of a potion or poison, synthesis grants
  Alchemy XP, and merchants buy your brews.

## Requirements

- Skyrim SE 1.5.97 or AE 1.6.x
- [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

## Installation

Install `dist/AtelierAlchemy-<version>.zip` with a mod manager, or copy the contents of
`dist/AtelierAlchemy/` into `Skyrim Special Edition/Data/`. Then enable `AtelierAlchemy.esp`.
It is ESL-flagged, so it doesn't use a load order slot.

## How to play

1. Go to an alchemy lab and press **K**. You can also press K while seated at the lab, where a
   hint is shown. The key, a gamepad button and the lab requirement are set in
   `SKSE/Plugins/AtelierAlchemy.ini`.
2. Choose a recipe in the **Recipe Book**. You start with *Distilled Water*, *Herbal Extract*,
   *Alchemist's Ash*, *Healing Salve*, *Mana Tonic*, *Stamina Draught* and *Viper Venom*.
   The **Ingredient Categories** tab lists every category with the ingredients and materials
   that belong to it (what you carry is highlighted) and the recipes that need it. Hovering
   a material shows which slots it fills; hovering an ingredient shows its plugin.
   Ingredients that nothing in your load order provides (no NPC, container, plant, crafting
   recipe or loot list - typically asset files of mods that aren't installed) are hidden
   unless you tick *Show unobtainable*. Vanilla, DLC and Creation Club ingredients always
   count as obtainable.
3. Fill each ingredient slot (or use **Auto-fill**), optionally add a catalyst, and watch the
   element bars and effect lines update. Tick the traits you want the product to inherit.
4. Press **Synthesize!** and play the Judgement Ring with **Space / E / left mouse /
   gamepad A**. Next to the button you can switch the ring between *Normal*, *Easy* and
   *Auto*; during a ring, **Skip** (or Tab / gamepad Y) finishes that synthesis as Auto would.
5. Collect your batch. Its name shows the rank and quality, for example
   `Healing Salve (B Q112)`. Drink it, sell it, or use it as an ingredient in a later
   synthesis.

A first chain to try: make *Distilled Water* (any plant + Salt Pile), then a *Healing Salve*
(two plants + the water). Adding Fire Salts as a catalyst pushes *Mending* to its second
tier. A longer one: Distilled Water → *Herbal Extract* (two plants + the water) → a
*Purifying Draught* (the extract + a salt + more water).

## Configuration and modding

- `SKSE/Plugins/AtelierAlchemy.ini`: hotkeys, the lab requirement, ring mode and speed,
  sound volume, balance multipliers and font.
- `SKSE/Plugins/AtelierAlchemy/*.json`: all game data. **Every** `.json` in that folder is
  loaded in alphabetical order, and any file may contain any section. An add-on can
  therefore ship its own `zz_mymod.json` with extra `recipes`, `traits`, `traitCombos`,
  `catalysts`, `ingredients` or `effects`. The format is documented in the comment headers of
  the bundled files.
  - `effects.json`: effects are matched by actor value, archetype and flags against the
    magic effects your load order actually uses, so no FormIDs are hard-coded. A few
    (Lingering Damage) also filter by the English effect name. With a translated game,
    adjust their `nameContains` / `nameExcludes` words.
  - `ingredients.json`: ingredient categories and signature traits, matched by English name
    or by `"form": "Plugin.esp|0x123456"`. Unlisted ingredients are categorized by keywords
    in their name and model path.
- Check your edits offline with `python3 tools/validate_data.py`, and
  `python3 tools/audit_lines.py` for effect lines whose top tier looks out of reach. The
  plugin repeats that audit with your load order's real ingredients at startup
  (`Recipe audit:` in the SKSE log). In game, data errors are
  listed at the top of the synthesis window and in the SKSE log.

## Building (NixOS / Linux, cross-compiled to Windows)

The flake provides `clang-cl`, `lld-link`, CMake, Ninja and `xwin`.

```sh
nix develop
./scripts/setup-xwin.sh      # once: downloads the MSVC CRT + Windows SDK (accepts Microsoft's license)
./scripts/build.sh           # assets, data validation, core tests, DLL -> dist/AtelierAlchemy
./scripts/package.sh         # dist/AtelierAlchemy-<version>.zip and ...-source.zip
```

`package.sh` also writes `AtelierAlchemy-<version>-source.zip`, the source code for the GPL:
everything git tracks or would track, uncommitted edits included. With `--with-deps` it also
bundles the dependency sources CMake fetched (run a build first) in `third_party/`, and a
build from that archive uses them instead of downloading anything. CommonLib's OpenVR
submodule is left out except for its headers and license, since the mod doesn't build for
Skyrim VR.

The Nexus page text lives in `nexus/description.md`. `package.sh` copies it to `dist/nexus/`
along with a BBCode version (`description.bbcode`) to paste into the Nexus editor.
`tools/make_banner.py` renders the banner and cover images into the same folder.

The first build compiles CommonLibSSE-NG and takes about 15 minutes; later builds take
seconds. Dependencies (CommonLibSSE-NG, spdlog, Dear ImGui, nlohmann/json, SimpleMath) are
fetched by CMake. On Windows, use the `windows-msvc` preset from a VS 2022 developer prompt.

The game-independent core (`src/core`: synthesis math and the Judgement Ring) has host unit
tests: `./scripts/test-core.sh`.

### Layout

| Path | Purpose |
| --- | --- |
| `src/core/` | Pure C++ synthesis rules, trait fusion and the Judgement Ring state machine (unit tested) |
| `src/game/` | Skyrim side: JSON database, effect resolution, ingredient ledger, brew slots, co-save, synthesis commit |
| `src/ui/` | `IDXGISwapChain::Present` vtable hook, Dear ImGui menu, input routing |
| `tools/build_assets.py` | Generates `AtelierAlchemy.esp` (brew slot records), the empty `Interface/AtelierAlchemy.swf` and the synthesized ring sound cues |
| `tools/validate_data.py` | Offline data checker |
| `data/` | Everything that ends up in `Data/` |

## How it works (technical notes)

- **Brew slots.** Each synthesis batch has its own effects, name, value and weight. The ESP
  ships 256 generic `ALCH` forms (potions and poisons) and 64 `MISC` forms (materials). Every
  batch is written into a free slot at runtime, and the slot table is stored in the SKSE
  co-save and reapplied on load, so brews are real, save-persistent items. When all slots are
  in use, the oldest brew the player no longer carries is recycled. *Copies of that brew sold
  to a merchant or left in a chest then turn into the new brew.*
- **Ingredient ledger.** Per-ingredient lists of quality and traits are kept in the co-save.
  If ingredients vanish outside the mod (eaten, sold, used at a vanilla lab), the
  lowest-quality entries are dropped.
- **Rendering and input.** ImGui is drawn from a hook on the swap chain's `Present` vtable
  slot, which needs no version-specific addresses. Input comes from the game's own input
  event sink. A modal, game-pausing `IMenu`, backed by an empty SWF, blocks gameplay controls
  while the window is open.
- **Sound.** The ring cues play through a separate XAudio2 device, loaded at runtime,
  because the game is paused while the menu is open. Their volume is the INI volume times
  the game's master volume.
- **Vanilla alchemy is left alone.** The vanilla alchemy lab still works as before.

## Credits and license

- Built on [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR) (GPL-3.0),
  [Dear ImGui](https://github.com/ocornut/imgui), [spdlog](https://github.com/gabime/spdlog),
  [nlohmann/json](https://github.com/nlohmann/json) and DirectXTK SimpleMath.
- Bundled font: [Alegreya Sans](https://github.com/huertatipografica/Alegreya-Sans), SIL Open
  Font License (`fonts/AlegreyaSans-OFL.txt`).
- The mechanics are inspired by Gust's *Atelier* series and Sacnoth's *Shadow Hearts*. No
  assets, names or text from those games are used.
- Licensed under the GNU GPL v3 (see `LICENSE`), because CommonLibSSE-NG is statically linked.
