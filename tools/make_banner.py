#!/usr/bin/env python3
"""Renders Nexus Mods artwork in the style of the in-game Judgement Ring.

    nix-shell -p "python3.withPackages (ps: [ ps.pillow ])" --run "python3 tools/make_banner.py"

Writes dist/nexus/banner.png (1920x480 header) and dist/nexus/cover.png (1280x720).
The ring, mandala and colours mirror src/ui/Menu.cpp (DrawRing / DrawMandala) and
src/ui/Style.h; the title uses the bundled Alegreya Sans.
"""

from __future__ import annotations

import math
import random
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
FONT = ROOT / "data" / "SKSE" / "Plugins" / "AtelierAlchemy" / "fonts" / "AlegreyaSans-Regular.ttf"
OUT = ROOT / "dist" / "nexus"
SS = 3  # supersampling

GOLD = (232, 190, 110)
PARCHMENT = (240, 228, 205)
MUTED = (170, 150, 125)
RING_BASE = (35, 28, 24)
RING_TRACK = (95, 78, 60)
HIT = (235, 160, 60)
STRIKE = (220, 50, 45)
GOOD = (140, 215, 120)
NEEDLE = (255, 245, 225)


def rgba(color, alpha):
    return (*color, int(max(0.0, min(1.0, alpha)) * 255))


class Canvas:
    """RGBA canvas; every translucent group is drawn on its own layer and composited."""

    def __init__(self, width, height):
        self.w, self.h = width * SS, height * SS
        self.img = Image.new("RGBA", (self.w, self.h))

    def layer(self):
        layer = Image.new("RGBA", (self.w, self.h), (0, 0, 0, 0))
        return layer, ImageDraw.Draw(layer)

    def add(self, layer, blur=0.0):
        if blur:
            layer = layer.filter(ImageFilter.GaussianBlur(blur * SS))
        self.img = Image.alpha_composite(self.img, layer)

    def save(self, path, size):
        path.parent.mkdir(parents=True, exist_ok=True)
        self.img.resize(size, Image.LANCZOS).convert("RGB").save(path, optimize=True)
        print("wrote", path)


def polar(cx, cy, r, angle):
    return cx + math.cos(angle) * r, cy + math.sin(angle) * r


def ring_angle(deg):
    """Ring degrees (0 = 12 o'clock, clockwise) to radians."""
    return math.radians(deg - 90.0)


def arc(draw, cx, cy, r, start_deg, end_deg, color, width):
    box = (cx - r - width / 2, cy - r - width / 2, cx + r + width / 2, cy + r + width / 2)
    draw.arc(box, start_deg - 90.0, end_deg - 90.0, fill=color, width=int(width))


def circle(draw, cx, cy, r, outline=None, fill=None, width=1):
    draw.ellipse((cx - r, cy - r, cx + r, cy + r), outline=outline, fill=fill, width=int(max(1, width)))


def triangle(draw, cx, cy, s, up, color, width):
    d = -1 if up else 1
    pts = [(cx, cy + d * s), (cx - s * 0.87, cy - d * s * 0.5), (cx + s * 0.87, cy - d * s * 0.5)]
    draw.line(pts + [pts[0]], fill=color, width=int(width), joint="curve")


def glyph(draw, index, cx, cy, s, color, width):
    w = int(width)
    if index == 0:  # Fire
        triangle(draw, cx, cy, s, True, color, w)
    elif index == 1:  # Mercury
        circle(draw, cx, cy - s * 0.1, s * 0.38, outline=color, width=w)
        r = s * 0.38
        draw.arc((cx - r, cy - s * 0.85 - r, cx + r, cy - s * 0.85 + r), 15, 165, fill=color, width=w)
        draw.line([(cx, cy + s * 0.28), (cx, cy + s)], fill=color, width=w)
        draw.line([(cx - s * 0.32, cy + s * 0.66), (cx + s * 0.32, cy + s * 0.66)], fill=color, width=w)
    elif index == 2:  # Air
        triangle(draw, cx, cy, s, True, color, w)
        draw.line([(cx - s * 0.75, cy), (cx + s * 0.75, cy)], fill=color, width=w)
    elif index == 3:  # Sun
        circle(draw, cx, cy, s * 0.8, outline=color, width=w)
        circle(draw, cx, cy, s * 0.16, fill=color)
    elif index == 4:  # Water
        triangle(draw, cx, cy, s, False, color, w)
    elif index == 5:  # Salt
        circle(draw, cx, cy, s * 0.8, outline=color, width=w)
        draw.line([(cx - s * 0.8, cy), (cx + s * 0.8, cy)], fill=color, width=w)
    elif index == 6:  # Earth
        triangle(draw, cx, cy, s, False, color, w)
        draw.line([(cx - s * 0.75, cy), (cx + s * 0.75, cy)], fill=color, width=w)
    else:  # Moon
        r = s * 0.8
        draw.arc((cx - r, cy - r, cx + r, cy + r), 90, 270, fill=color, width=w)
        r2 = s * 0.62
        ox = cx - s * 0.3
        draw.arc((ox - r2, cy - r2, ox + r2, cy + r2), 112, 248, fill=color, width=w)


def bezier(p0, p1, p2, steps=24):
    return [((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0],
             (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1]) for t in (i / steps for i in range(steps + 1))]


def mandala(canvas, cx, cy, radius, thickness, outer, rotation=0.0, strength=1.0):
    """The DrawMandala layers: symbol band, petals, inner star and spokes."""
    w = 1.6 * SS
    ring_outer = radius + thickness
    band = (ring_outer + outer) / 2
    gsize = min((outer - ring_outer) * 0.26, band * 0.09)

    layer, d = canvas.layer()
    circle(d, cx, cy, outer, outline=rgba(GOLD, 0.30 * strength), width=w)
    for i in range(16):  # petals
        a = -rotation * 0.7 + i * math.pi / 8
        half = math.pi / 16
        p0 = polar(cx, cy, ring_outer + thickness * 0.1, a - half)
        p1 = polar(cx, cy, ring_outer + thickness * 0.1, a + half)
        tip = polar(cx, cy, outer * 0.99, a)
        pts = bezier(p0, polar(cx, cy, outer * 0.97, a - half * 0.9), tip) + bezier(tip, polar(cx, cy, outer * 0.97, a + half * 0.9), p1)[1:]
        d.line(pts, fill=rgba(GOLD, 0.22 * strength), width=int(w), joint="curve")
    canvas.add(layer)

    layer, d = canvas.layer()
    for i in range(8):  # symbols on dark discs, dots between
        a = rotation + i * math.pi / 4
        gx, gy = polar(cx, cy, band, a)
        circle(d, gx, gy, gsize * 1.3, fill=rgba(RING_BASE, min(1.0, strength * 1.5)), outline=rgba(GOLD, 0.30 * strength), width=w)
        glyph(d, i, gx, gy, gsize, rgba(GOLD, 0.62 * strength), w * 1.3)
        dx, dy = polar(cx, cy, band, a + math.pi / 8)
        circle(d, dx, dy, gsize * 0.14, fill=rgba(GOLD, 0.5 * strength))
    canvas.add(layer)


def ring(canvas, cx, cy, extent, rotation):
    radius = extent * 0.70
    thickness = radius * 0.16
    # Soft warm glow behind everything.
    layer, d = canvas.layer()
    circle(d, cx, cy, extent * 1.02, fill=rgba(GOLD, 0.10))
    canvas.add(layer, blur=40)

    layer, d = canvas.layer()
    circle(d, cx, cy, radius + thickness, fill=rgba(RING_BASE, 1.0))
    canvas.add(layer)
    mandala(canvas, cx, cy, radius, thickness, extent * 0.97, rotation)

    layer, d = canvas.layer()
    inner = radius - thickness * 0.6
    for sq in range(2):  # eight-pointed star
        pts = [polar(cx, cy, inner * 0.96, -rotation * 0.5 + sq * math.pi / 4 + k * math.pi / 2) for k in range(4)]
        d.line(pts + [pts[0]], fill=rgba(GOLD, 0.2), width=int(1.6 * SS))
    for i in range(12):
        a = rotation * 0.5 + i * math.pi / 6
        d.line([polar(cx, cy, radius * 0.46, a), polar(cx, cy, inner * 0.7, a)], fill=rgba(GOLD, 0.16), width=int(1.4 * SS))
        x, y = polar(cx, cy, inner * 0.7, a)
        circle(d, x, y, 2.4 * SS, fill=rgba(GOLD, 0.3))
    circle(d, cx, cy, inner * 0.7, outline=rgba(GOLD, 0.16), width=1.4 * SS)
    circle(d, cx, cy, inner, outline=rgba(GOLD, 0.18), width=1.4 * SS)
    canvas.add(layer)

    layer, d = canvas.layer()
    circle(d, cx, cy, radius + thickness, outline=rgba(GOLD, 1.0), width=2.2 * SS)
    arc(d, cx, cy, radius, 0, 360, rgba(RING_TRACK, 1.0), thickness)
    for deg in range(0, 360, 30):
        a = ring_angle(deg)
        d.line([polar(cx, cy, radius - thickness * 0.5, a), polar(cx, cy, radius - thickness * 0.8, a)], fill=rgba(MUTED, 1.0), width=int(2 * SS))
    # A "gentle" pattern mid-synthesis: first area struck (gold), second hit (green), third pending.
    areas = [(30, 80, 70, GOLD, GOLD), (140, 186, 177, GOOD, GOOD), (262, 286, 281, HIT, STRIKE)]
    for start, end, strike_start, hit_col, strike_col in areas:
        arc(d, cx, cy, radius, start, strike_start, rgba(hit_col, 1.0), thickness)
        arc(d, cx, cy, radius, strike_start, end, rgba(strike_col, 1.0), thickness)
    canvas.add(layer)

    # Needle sweeping towards the last area, with its glow.
    needle = ring_angle(238)
    tip = polar(cx, cy, radius + thickness * 0.9, needle)
    layer, d = canvas.layer()
    d.line([(cx, cy), tip], fill=rgba((255, 230, 160), 0.45), width=int(thickness * 0.35))
    canvas.add(layer, blur=3)
    layer, d = canvas.layer()
    d.line([(cx, cy), tip], fill=rgba(NEEDLE, 1.0), width=int(3 * SS))
    circle(d, cx, cy, radius * 0.42, fill=rgba(RING_BASE, 1.0))
    canvas.add(layer)

    layer, d = canvas.layer()  # squared circle in the hub
    r = radius * 0.42
    sq = [polar(cx, cy, r * 0.92, math.pi / 4 + k * math.pi / 2) for k in range(4)]
    d.line(sq + [sq[0]], fill=rgba(GOLD, 0.24), width=int(1.6 * SS))
    tri = r * 0.92 * 0.707
    tp = [polar(cx, cy, tri, -math.pi / 2), polar(cx, cy, tri, math.pi / 6), polar(cx, cy, tri, math.pi * 5 / 6)]
    d.line(tp + [tp[0]], fill=rgba(GOLD, 0.24), width=int(1.6 * SS))
    circle(d, cx, cy, tri * 0.5, outline=rgba(GOLD, 0.24), width=1.6 * SS)
    circle(d, cx, cy, r, outline=rgba(GOLD, 1.0), width=2.2 * SS)
    circle(d, cx, cy, 7 * SS, fill=rgba(GOLD, 1.0))
    canvas.add(layer)


def background(canvas, seed):
    w, h = canvas.w, canvas.h
    # Radial dark-brown vignette.
    base = Image.new("RGBA", (w // 8, h // 8))
    px = base.load()
    for y in range(base.height):
        for x in range(base.width):
            dx, dy = (x - base.width * 0.3) / base.width, (y - base.height * 0.5) / base.height
            t = min(1.0, math.hypot(dx * 1.2, dy * 1.6))
            px[x, y] = tuple(int(a + (b - a) * t) for a, b in zip((48, 38, 31), (14, 11, 9))) + (255,)
    canvas.img = base.resize((w, h), Image.BICUBIC)
    # Faint gold dust.
    rng = random.Random(seed)
    layer, d = canvas.layer()
    for _ in range(int(w * h / (SS * SS) / 900)):
        x, y = rng.uniform(0, w), rng.uniform(0, h)
        circle(d, x, y, rng.uniform(0.6, 1.6) * SS, fill=rgba(GOLD, rng.uniform(0.05, 0.25)))
    canvas.add(layer)


def text(canvas, xy, string, size, color, anchor="ls", shadow=True, spacing=0.0):
    font = ImageFont.truetype(str(FONT), int(size * SS))
    if shadow:
        layer, d = canvas.layer()
        d.text((xy[0] + 3 * SS, xy[1] + 4 * SS), string, font=font, fill=(0, 0, 0, 170), anchor=anchor)
        canvas.add(layer, blur=4)
    layer, d = canvas.layer()
    d.text(xy, string, font=font, fill=color, anchor=anchor)
    canvas.add(layer)
    return d.textbbox(xy, string, font=font, anchor=anchor)


def divider(canvas, x0, x1, y):
    layer, d = canvas.layer()
    d.line([(x0, y), (x1, y)], fill=rgba(GOLD, 0.8), width=int(1.6 * SS))
    mid = (x0 + x1) / 2
    s = 9 * SS
    d.polygon([(mid, y - s), (mid + s, y), (mid, y + s), (mid - s, y)], fill=rgba(RING_BASE, 1.0), outline=rgba(GOLD, 1.0))
    for x in (x0, x1):
        circle(d, x, y, 3 * SS, fill=rgba(GOLD, 0.9))
    canvas.add(layer)


def banner():
    W, H = 1920, 480
    c = Canvas(W, H)
    background(c, 1)
    # A large, faint mandala echo on the right for depth.
    mandala(c, (W + 90) * SS, H * 0.5 * SS, 260 * SS, 42 * SS, 430 * SS, rotation=0.62, strength=0.3)
    ring(c, 300 * SS, H * 0.5 * SS, 215 * SS, rotation=0.2)

    x = 620 * SS
    text(c, (x, 238 * SS), "Atelier Alchemy", 150, rgba(GOLD, 1.0))
    divider(c, x + 6 * SS, x + 940 * SS, 278 * SS)
    text(c, (x, 340 * SS), "Synthesis and the Judgement Ring", 48, rgba(PARCHMENT, 1.0))
    text(c, (x, 395 * SS), "Quality  ·  Traits  ·  Elements  ·  Intermediate materials  ·  Perk-aware balance", 28, rgba(MUTED, 1.0), shadow=False)
    c.save(OUT / "banner.png", (W, H))


def cover():
    W, H = 1280, 720
    c = Canvas(W, H)
    background(c, 2)
    ring(c, W * 0.5 * SS, 300 * SS, 255 * SS, rotation=0.1)
    text(c, (W * 0.5 * SS, 640 * SS), "Atelier Alchemy", 104, rgba(GOLD, 1.0), anchor="ms")
    text(c, (W * 0.5 * SS, 690 * SS), "Synthesis and the Judgement Ring", 36, rgba(PARCHMENT, 1.0), anchor="ms")
    c.save(OUT / "cover.png", (W, H))


if __name__ == "__main__":
    banner()
    cover()
