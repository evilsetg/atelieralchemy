#!/usr/bin/env python3
"""Rough offline check: can each potion/poison effect line reach its top tier?

Materials count with their best case, plus one element trait and the strongest catalyst
for the element. Materials count with their best case (base elements plus every element tier, including
conversions). Raw ingredients are guessed at ING_GUESS per element, since their real
elements come from the load order - the plugin's own audit in the SKSE log is exact.

    python3 tools/audit_lines.py
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from validate_data import DEFAULT_DIR, load  # noqa: E402

ING_GUESS = 3
TRAIT_BONUS = 2  # one element trait on one input


def main() -> int:
    recipes = {r["id"]: r for r in load(DEFAULT_DIR / "recipes.json")["recipes"]}
    catalysts = load(DEFAULT_DIR / "catalysts.json")["catalysts"]
    best_catalyst = {e: max((c.get("elements", {}).get(e, 0) for c in catalysts), default=0) for e in ("fire", "ice", "lightning", "wind")}
    materials = {k: r for k, r in recipes.items() if r.get("kind") == "material"}
    by_category: dict[str, list[dict]] = {}
    for m in materials.values():
        for c in m.get("categories", []):
            by_category.setdefault(c, []).append(m)

    def potential(m: dict, element: str) -> int:
        total = m.get("elements", {}).get(element, 0)
        for line in m["lines"]:
            for tier in line["tiers"]:
                if tier.get("element", "").lower() == element:
                    total += tier.get("amount", 1)
        return total

    def slot_best(req: dict, element: str) -> int:
        if "item" in req:
            return potential(materials[req["item"]], element) if req["item"] in materials else ING_GUESS
        best = max((potential(m, element) for m in by_category.get(req["category"], [])), default=0)
        exclusive = req["category"] in by_category and all(req["category"] in m["categories"] for m in by_category[req["category"]])
        is_material_only = exclusive and req["category"] not in {"Plants", "Flowers", "Fungus", "Spirit", "Minerals", "Salts"}
        return best if is_material_only else max(best, ING_GUESS)

    flagged = 0
    for rid, r in recipes.items():
        if r.get("kind") == "material":
            continue
        for line in r["lines"]:
            effects = [t for t in line["tiers"] if "effect" in t]
            if not effects:
                continue
            e = line["element"].lower()
            best = sum(slot_best(q, e) * q.get("count", 1) for q in r["ingredients"]) + TRAIT_BONUS
            with_catalyst = best + best_catalyst[e]
            top = max(t["at"] for t in effects)
            if with_catalyst < top:
                flagged += 1
                print(f"{rid:20s} {line['name']:16s} {e:9s} best ~{best:2d} (+catalyst {with_catalyst:2d}) < top tier {top}")
    print(f"{flagged} line(s) whose top effect tier looks out of reach")
    return 0


if __name__ == "__main__":
    sys.exit(main())
