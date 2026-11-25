#!/usr/bin/env python3
"""Offline consistency check for Data/SKSE/Plugins/AtelierAlchemy/*.json.

Catches typos the plugin would otherwise only report in-game: unknown effects,
traits, categories, item keys and unlock targets. Usage:

    python3 tools/validate_data.py [data/SKSE/Plugins/AtelierAlchemy]
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

DEFAULT_DIR = Path(__file__).resolve().parent.parent / "data" / "SKSE" / "Plugins" / "AtelierAlchemy"
ELEMENTS = {"fire", "ice", "lightning", "wind"}
KINDS = {"potion", "poison", "material"}


def load(path: Path) -> dict:
    # The plugin accepts // comments (nlohmann::json ignore_comments); strip them here.
    text = "\n".join(line for line in path.read_text(encoding="utf-8").splitlines() if not line.lstrip().startswith("//"))
    return json.loads(text)


def slug(name: str) -> str:
    out = re.sub(r"[^a-z0-9]+", "_", name.lower().replace("'", ""))
    return out.strip("_")


def main() -> int:
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_DIR
    docs = {p.name: load(p) for p in sorted(folder.glob("*.json"))}
    merged: dict[str, list | dict] = {}
    for doc in docs.values():
        for key, value in doc.items():
            if isinstance(value, list):
                merged.setdefault(key, []).extend(value)
            else:
                merged.setdefault(key, {}).update(value)

    errors: list[str] = []
    effects = set(merged.get("effects", {}))
    traits = {t["id"] for t in merged.get("traits", [])}
    recipes = {r["id"]: r for r in merged.get("recipes", [])}
    ingredients = merged.get("ingredients", [])

    categories = {c for i in ingredients for c in i.get("categories", [])}
    categories |= {c for r in recipes.values() if r.get("kind") == "material" for c in r.get("categories", [])}
    categories |= {"Potions", "Poisons", "Misc"} | set(merged.get("categoryKeywords", {}))
    item_keys = {i.get("key") or slug(i["name"]) for i in ingredients if "name" in i} | set(recipes)

    for key, effect in merged.get("effects", {}).items():
        form = effect.get("form")
        if form is not None and not re.fullmatch(r"[^|]+\.(esm|esp|esl)\|0x[0-9A-Fa-f]{1,8}", form):
            errors.append(f"effect {key}: form '{form}' must look like 'Skyrim.esm|0x03EB15'")

    for trait in merged.get("traits", []):
        for element in trait.get("elements", {}):
            if element.lower() not in ELEMENTS:
                errors.append(f"trait {trait['id']}: unknown element '{element}'")

    for combo in merged.get("traitCombos", []):
        for t in combo:
            if t not in traits:
                errors.append(f"traitCombos: unknown trait '{t}'")
    for category, pool in merged.get("categoryTraitPools", {}).items():
        for t in pool:
            if t not in traits:
                errors.append(f"categoryTraitPools[{category}]: unknown trait '{t}'")
    for ingredient in ingredients:
        for t in ingredient.get("traits", []):
            if t not in traits:
                errors.append(f"ingredient {ingredient.get('name')}: unknown trait '{t}'")

    patterns = merged.get("ringPatterns", {})

    def check_shape(where: str, shape) -> None:
        if not isinstance(shape, list) or not shape:
            errors.append(f"{where}: ring pattern must be a non-empty list")
            return
        if len(shape) > 8:
            errors.append(f"{where}: at most 8 ring areas")
        for area in shape:
            if not 0.0 <= area.get("at", -1) <= 1.0:
                errors.append(f"{where}: ring area 'at' must be between 0 and 1")
            if area.get("width", 1.0) <= 0 or area.get("strike", 1.0) <= 0:
                errors.append(f"{where}: ring area sizes must be positive")

    for name, shape in patterns.items():
        check_shape(f"ring pattern {name}", shape)

    for rid, recipe in recipes.items():
        where = f"recipe {rid}"
        if recipe.get("potency", 1.0) <= 0:
            errors.append(f"{where}: potency must be positive")
        ring = recipe.get("ring")
        if isinstance(ring, str):
            if ring not in patterns:
                errors.append(f"{where}: unknown ring pattern '{ring}'")
        elif ring is not None:
            check_shape(where, ring)
        if recipe.get("kind", "potion") not in KINDS:
            errors.append(f"{where}: unknown kind {recipe.get('kind')}")
        for req in recipe.get("ingredients", []):
            if "category" in req and req["category"] not in categories:
                errors.append(f"{where}: no ingredient or material has category '{req['category']}'")
            if "item" in req and req["item"] not in item_keys:
                errors.append(f"{where}: unknown item '{req['item']}'")
        for line in recipe.get("lines", []):
            if line.get("element", "").lower() not in ELEMENTS:
                errors.append(f"{where}: line {line.get('name')} has unknown element")
            thresholds = [t["at"] for t in line.get("tiers", [])]
            if thresholds != sorted(thresholds):
                errors.append(f"{where}: line {line.get('name')} tiers are not sorted")
            for tier in line.get("tiers", []):
                if "effect" in tier and tier["effect"] not in effects:
                    errors.append(f"{where}: unknown effect '{tier['effect']}'")
                if "element" in tier and tier["element"].lower() not in ELEMENTS:
                    errors.append(f"{where}: unknown element '{tier['element']}'")
        for rule in recipe.get("unlocks", []):
            if rule.get("recipe") not in recipes:
                errors.append(f"{where}: unlocks unknown recipe '{rule.get('recipe')}'")
            if "item" in rule and rule["item"] not in item_keys:
                errors.append(f"{where}: unlock rule uses unknown item '{rule['item']}'")
            if "category" in rule and rule["category"] not in categories:
                errors.append(f"{where}: unlock rule uses unknown category '{rule['category']}'")
        if recipe.get("kind") != "material" and not any("effect" in t for l in recipe.get("lines", []) for t in l.get("tiers", [])):
            errors.append(f"{where}: a potion/poison needs at least one effect tier")

    for catalyst in merged.get("catalysts", []):
        if catalyst.get("item") not in item_keys:
            errors.append(f"catalyst '{catalyst.get('item')}' matches no ingredient or material")

    # Every recipe should be discoverable.
    reachable = {rid for rid, r in recipes.items() if r.get("known") or r.get("unlockSkill", -1) >= 0}
    reachable |= {rule["recipe"] for r in recipes.values() for rule in r.get("unlocks", [])}
    for rid in recipes:
        if rid not in reachable:
            errors.append(f"recipe {rid} can never be learned (no known / unlockSkill / unlock rule)")

    print(f"{len(recipes)} recipes, {len(patterns)} ring patterns, {len(traits)} traits, {len(effects)} effects, {len(ingredients)} ingredients, "
          f"{len(merged.get('catalysts', []))} catalysts")
    for e in errors:
        print("ERROR:", e)
    print("OK" if not errors else f"{len(errors)} error(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
