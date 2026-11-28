#!/usr/bin/env python3
"""Generates the binary assets of Atelier Alchemy.

  data/AtelierAlchemy.esp              ESL-flagged plugin with the brew slot records
  data/Interface/AtelierAlchemy.swf    empty Scaleform movie backing the engine-side menu
  data/Sound/FX/AtelierAlchemy/*.wav   Judgement Ring cues (synthesized here)

The plugin only contains generic placeholder forms. The SKSE DLL rewrites their
name, effects, value, weight and model at runtime for every synthesized batch
(see src/game/State.cpp), so the record layout here must match:

  ALCH  0x800 .. 0x800+POTION_SLOTS-1    potions and poisons
  MISC  0xA00 .. 0xA00+MATERIAL_SLOTS-1  synthesized materials
  SNDR  0xB00 .. 0xB00+len(SOUNDS)-1     Judgement Ring cues, in SOUNDS order
  MGEF  0xC00 .. 0xC00+len(EFFECTS)-1    effects vanilla alchemy lacks (effects.json "form")
  KYWD  0xC40 .. 0xC40+len(EFFECTS)-1    their stacking keywords

Usage: python3 tools/build_assets.py
"""

from __future__ import annotations

import math
import random
import struct
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "data"

POTION_SLOTS = 256
MATERIAL_SLOTS = 64
POTION_BASE = 0x800
MATERIAL_BASE = 0xA00
SOUND_BASE = 0xB00
EFFECT_BASE = 0xC00
EFFECT_KEYWORD_BASE = 0xC40

FORM_VERSION = 44  # Skyrim Special Edition
MASTER_INDEX = 0x01  # our own records: one master (Skyrim.esm) precedes us

# Skyrim.esm "AlchRestoreHealth". Only a placeholder so the records are never
# effect-less; the DLL replaces the effect list on load.
ALCH_RESTORE_HEALTH = 0x0003EB15

RECORD_FLAG_ESL = 0x200

# Sound descriptors are set up like the vanilla menu sounds (UIMenuOKSD): standard
# descriptor, AudioCategoryUI (plays while menus pause the game, follows the master
# volume) and the SOMMono01400Player1st output model. File names are relative to
# Data\Sound\.
SNDR_TYPE_STANDARD = 0x1EEF540A
SNCT_AUDIO_CATEGORY_UI = 0x00064451
SOPM_MONO_PLAYER_1ST = 0x000B4058
SOUND_FOLDER = "fx\\AtelierAlchemy"

# Magic effects only these recipes make, set up like vanilla potion effects (copied from
# Skyrim.esm AlchFortifyHealth: self, fire and forget, the potion sounds, MagicAlchBeneficial
# so Benefactor applies). Like vanilla, each effect's own keyword is the Peak Value
# Modifier's associated item, so drinking a second potion replaces the first instead of
# stacking. Magnitudes are what the effect adds to the actor value; effects.json
# "magnitudeScale" converts from the percentages the recipes work in. Duration-based
# effects (potency lengthens them, like Invisibility) also carry MagicAlchDurationBased.
KYWD_MAGIC_ALCH_BENEFICIAL = 0x000F8A4E
KYWD_MAGIC_ALCH_DURATION_BASED = 0x000F8A4F
MGEF_ARCHETYPE_PEAK_VALUE_MODIFIER = 34
MGEF_ARCHETYPE_ETHEREALIZE = 41  # as VoiceMakeEthereal (Become Ethereal)
MGEF_RECOVER = 0x00000002
MGEF_NO_MAGNITUDE = 0x00000400
MGEF_NO_AREA = 0x00000800
MGEF_FX_PERSIST = 0x00001000
MGEF_POWER_AFFECTS_MAGNITUDE = 0x00200000
MGEF_POWER_AFFECTS_DURATION = 0x00400000
MGEF_MENU_DISPLAY_OBJECT = 0x000435A5
MGEF_HIT_SHADER = 0x0004E220           # the potion shader of AlchFortifyHealth
MGEF_HIT_SHADER_ETHEREAL = 0x00064D67  # VoiceMakeEthereal's
#   (editor id, name, description, archetype, actor value (-1 none), flags, base cost, hit shader)
EFFECTS = [
    ("AA_AlchFortifyArmorRating", "Fortify Armor Rating",
     "Armor rating is increased by <mag> points for <dur> seconds.",
     MGEF_ARCHETYPE_PEAK_VALUE_MODIFIER, 39,  # DamageResist
     MGEF_RECOVER | MGEF_NO_AREA | MGEF_POWER_AFFECTS_MAGNITUDE, 0.25, MGEF_HIT_SHADER),
    ("AA_AlchFortifyShoutRecovery", "Fortify Shout Recovery",
     "Shouts recharge faster for <dur> seconds.",
     MGEF_ARCHETYPE_PEAK_VALUE_MODIFIER, 86,  # ShoutRecoveryMult
     MGEF_RECOVER | MGEF_NO_AREA | MGEF_POWER_AFFECTS_MAGNITUDE, 0.6, MGEF_HIT_SHADER),
    # Like the Muffle spell (MuffleFFSelf): magnitude 1 silences movement completely.
    ("AA_AlchMuffle", "Muffle",
     "Your movements are silent for <dur> seconds.",
     MGEF_ARCHETYPE_PEAK_VALUE_MODIFIER, 92,  # MovementNoiseMult
     MGEF_RECOVER | MGEF_NO_AREA | MGEF_POWER_AFFECTS_DURATION, 3.0, MGEF_HIT_SHADER),
    ("AA_AlchEthereal", "Ethereal",
     "You can't be harmed, and can't attack, for <dur> seconds.",
     MGEF_ARCHETYPE_ETHEREALIZE, -1,
     MGEF_NO_MAGNITUDE | MGEF_NO_AREA | MGEF_FX_PERSIST | MGEF_POWER_AFFECTS_DURATION, 150.0, MGEF_HIT_SHADER_ETHEREAL),
]
# SNDD of AlchFortifyHealth: (type, descriptor) pairs - draw/sheathe, charge, ready, release.
MGEF_SOUNDS = bytes.fromhex("0000000057e9030001000000226605000200000063f303000300000061f30300")


def subrecord(tag: str, data: bytes) -> bytes:
    assert len(tag) == 4 and len(data) < 0x10000
    return tag.encode("ascii") + struct.pack("<H", len(data)) + data


def zstring(text: str) -> bytes:
    return text.encode("cp1252") + b"\0"


def record(tag: str, form_id: int, body: bytes, flags: int = 0) -> bytes:
    header = tag.encode("ascii") + struct.pack("<IIIIHH", len(body), flags, form_id, 0, FORM_VERSION, 0)
    return header + body


def group(label: str, records: list[bytes]) -> bytes:
    content = b"".join(records)
    header = b"GRUP" + struct.pack("<I", 24 + len(content)) + label.encode("ascii") + struct.pack("<iHHI", 0, 0, 0, 0)
    return header + content


def obnd() -> bytes:
    return subrecord("OBND", struct.pack("<6h", -4, -4, 0, 4, 4, 12))


def alch(index: int) -> bytes:
    local_id = POTION_BASE + index
    body = b"".join(
        [
            subrecord("EDID", zstring(f"AA_Brew{index:03d}")),
            obnd(),
            subrecord("FULL", zstring("Atelier Brew")),
            subrecord("DATA", struct.pack("<f", 0.5)),
            # value, flags (1 = manual cost), addiction, addiction chance, consume sound
            subrecord("ENIT", struct.pack("<iIIfI", 10, 0x1, 0, 0.0, 0)),
            subrecord("EFID", struct.pack("<I", ALCH_RESTORE_HEALTH)),
            subrecord("EFIT", struct.pack("<fII", 1.0, 0, 0)),
        ]
    )
    return record("ALCH", (MASTER_INDEX << 24) | local_id, body)


def misc(index: int) -> bytes:
    local_id = MATERIAL_BASE + index
    body = b"".join(
        [
            subrecord("EDID", zstring(f"AA_Material{index:03d}")),
            obnd(),
            subrecord("FULL", zstring("Atelier Material")),
            subrecord("DATA", struct.pack("<if", 5, 0.2)),
        ]
    )
    return record("MISC", (MASTER_INDEX << 24) | local_id, body)


def sndr(index: int, file_name: str) -> bytes:
    local_id = SOUND_BASE + index
    body = b"".join(
        [
            subrecord("EDID", zstring(f"AA_{Path(file_name).stem.title().replace('_', '')}SD")),
            subrecord("CNAM", struct.pack("<I", SNDR_TYPE_STANDARD)),
            subrecord("GNAM", struct.pack("<I", SNCT_AUDIO_CATEGORY_UI)),
            subrecord("ANAM", zstring(f"{SOUND_FOLDER}\\{file_name}")),
            subrecord("ONAM", struct.pack("<I", SOPM_MONO_PLAYER_1ST)),
            subrecord("LNAM", struct.pack("<4B", 2, 0, 0, 0)),
            # frequency shift, frequency variance, priority, dB variance, static attenuation
            subrecord("BNAM", struct.pack("<bbBBH", 0, 0, 0x80, 0, 0)),
        ]
    )
    return record("SNDR", (MASTER_INDEX << 24) | local_id, body)


def mgef(index: int, editor_id: str, name: str, description: str, archetype: int, actor_value: int, flags: int,
         cost: float, hit_shader: int) -> bytes:
    local_id = EFFECT_BASE + index
    keyword = (MASTER_INDEX << 24) | (EFFECT_KEYWORD_BASE + index)
    keywords = [keyword, KYWD_MAGIC_ALCH_BENEFICIAL]
    if flags & MGEF_POWER_AFFECTS_DURATION:
        keywords.append(KYWD_MAGIC_ALCH_DURATION_BASED)
    data = [0] * 38  # 152 bytes, all 4-byte fields
    data[0] = flags
    data[1] = struct.unpack("<I", struct.pack("<f", cost))[0]
    if archetype == MGEF_ARCHETYPE_PEAK_VALUE_MODIFIER:
        data[2] = keyword  # associated item
    data[3] = 0xFFFFFFFF  # magic skill: none
    data[4] = 0xFFFFFFFF  # resist value: none
    data[8] = hit_shader
    data[16] = archetype
    data[17] = actor_value & 0xFFFFFFFF
    data[20] = 1  # casting type: fire and forget
    data[22] = 0xFFFFFFFF  # second actor value: none
    data[28] = struct.unpack("<I", struct.pack("<f", 1.0))[0]  # dual cast scale
    body = b"".join(
        [
            subrecord("EDID", zstring(editor_id)),
            subrecord("FULL", zstring(name)),
            subrecord("MDOB", struct.pack("<I", MGEF_MENU_DISPLAY_OBJECT)),
            subrecord("KSIZ", struct.pack("<I", len(keywords))),
            subrecord("KWDA", struct.pack(f"<{len(keywords)}I", *keywords)),
            subrecord("DATA", struct.pack("<38I", *data)),
            subrecord("SNDD", MGEF_SOUNDS),
            subrecord("DNAM", zstring(description)),
        ]
    )
    return record("MGEF", (MASTER_INDEX << 24) | local_id, body)


def kywd(index: int, editor_id: str) -> bytes:
    body = subrecord("EDID", zstring(editor_id.replace("AA_Alch", "AA_MagicAlch")))
    return record("KYWD", (MASTER_INDEX << 24) | (EFFECT_KEYWORD_BASE + index), body)


def build_esp() -> bytes:
    potions = [alch(i) for i in range(POTION_SLOTS)]
    materials = [misc(i) for i in range(MATERIAL_SLOTS)]
    sounds = [sndr(i, name) for i, name in enumerate(SOUNDS)]
    effects = [mgef(i, *effect) for i, effect in enumerate(EFFECTS)]
    keywords = [kywd(i, effect[0]) for i, effect in enumerate(EFFECTS)]
    # Top level groups follow the engine's canonical order (KYWD, MGEF, MISC, ALCH, ..., SNDR).
    groups = [group("KYWD", keywords), group("MGEF", effects), group("MISC", materials), group("ALCH", potions), group("SNDR", sounds)]
    num_records = len(potions) + len(materials) + len(sounds) + len(effects) + len(keywords) + len(groups)

    header_body = b"".join(
        [
            subrecord("HEDR", struct.pack("<fiI", 1.70, num_records, EFFECT_KEYWORD_BASE + len(EFFECTS))),
            subrecord("CNAM", zstring("Atelier Alchemy")),
            subrecord("SNAM", zstring("Atelier-style synthesis and the Judgement Ring. Requires the AtelierAlchemy SKSE plugin.")),
            subrecord("MAST", zstring("Skyrim.esm")),
            subrecord("DATA", struct.pack("<Q", 0)),
        ]
    )
    tes4 = record("TES4", 0, header_body, flags=RECORD_FLAG_ESL)
    return tes4 + b"".join(groups)


class BitWriter:
    def __init__(self) -> None:
        self.bits: list[int] = []

    def write(self, value: int, count: int) -> None:
        if value < 0:
            value &= (1 << count) - 1
        for i in reversed(range(count)):
            self.bits.append((value >> i) & 1)

    def bytes(self) -> bytes:
        bits = self.bits + [0] * (-len(self.bits) % 8)
        return bytes(int("".join(map(str, bits[i : i + 8])), 2) for i in range(0, len(bits), 8))


def swf_tag(code: int, data: bytes = b"") -> bytes:
    assert len(data) < 0x3F
    return struct.pack("<H", (code << 6) | len(data)) + data


def build_swf() -> bytes:
    rect = BitWriter()
    rect.write(16, 5)
    for value in (0, 1280 * 20, 0, 720 * 20):  # twips
        rect.write(value, 16)
    body = rect.bytes() + struct.pack("<HH", 30 << 8, 1)  # 30 fps, 1 frame
    body += swf_tag(69, struct.pack("<I", 0))  # FileAttributes: AS2, no network
    body += swf_tag(1)  # ShowFrame
    body += swf_tag(0)  # End
    return b"FWS" + bytes([8]) + struct.pack("<I", 8 + len(body)) + body


# ---------------------------------------------------------------------------
# Judgement Ring sound cues: small additive synthesizer, standard library only.
# ---------------------------------------------------------------------------

RATE = 44100


def silence(seconds: float) -> list[float]:
    return [0.0] * int(seconds * RATE)


def mix(target: list[float], source: list[float], at: float = 0.0, gain: float = 1.0) -> None:
    offset = int(at * RATE)
    if len(target) < offset + len(source):
        target.extend([0.0] * (offset + len(source) - len(target)))
    for i, v in enumerate(source):
        target[offset + i] += v * gain


def noise(seconds: float, decay: float, seed: int, lowpass: float = 1.0, highpass: float = 0.0) -> list[float]:
    """Decaying noise burst through one-pole low/high-pass filters (coefficients 0-1)."""
    rng = random.Random(seed)
    out, lp, hp_state = [], 0.0, 0.0
    for n in range(int(seconds * RATE)):
        x = rng.uniform(-1.0, 1.0) * math.exp(-decay * n / RATE)
        lp += lowpass * (x - lp)
        hp_state += highpass * (lp - hp_state)
        out.append(lp - hp_state if highpass else lp)
    return out


def lowpass(samples: list[float], coeff: float) -> list[float]:
    out, y = [], 0.0
    for x in samples:
        y += coeff * (x - y)
        out.append(y)
    return out


def reverb(samples: list[float], wet: float, tail: float = 0.6, room: float = 0.78) -> list[float]:
    """Schroeder reverb: parallel damped combs into two allpasses."""
    dry = samples + [0.0] * int(tail * RATE)
    combs = [1116, 1188, 1277, 1356]
    acc = [0.0] * len(dry)
    for delay in combs:
        buf, damp = [0.0] * len(dry), 0.0
        for n, x in enumerate(dry):
            y = buf[n - delay] if n >= delay else 0.0
            damp += 0.4 * (y - damp)
            buf[n] = x + room * damp
            acc[n] += y / len(combs)
    for delay in (556, 441):
        out = [0.0] * len(acc)
        for n, x in enumerate(acc):
            delayed = out[n - delay] if n >= delay else 0.0
            xd = acc[n - delay] if n >= delay else 0.0
            out[n] = -0.5 * x + xd + 0.5 * delayed
        acc = out
    return [d + wet * w for d, w in zip(dry, acc)]


def finish(samples: list[float], peak: float, fade: float = 0.02) -> list[float]:
    top = max(abs(v) for v in samples) or 1.0
    out = [v * peak / top for v in samples]
    fade_n = int(fade * RATE)
    for i in range(min(fade_n, len(out))):
        out[-1 - i] *= i / fade_n
    return out


def write_wav(path: Path, samples: list[float]) -> None:
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", max(-32767, min(32767, round(v * 32767)))) for v in samples))


def cutoff(hz: float) -> float:
    """One-pole filter coefficient for a cutoff frequency."""
    return 1.0 - math.exp(-2.0 * math.pi * hz / RATE)


def thump(f_start: float, f_end: float, seconds: float, drop: float, decay: float) -> list[float]:
    """Drum body: a sine whose pitch falls quickly from f_start towards f_end."""
    out, phase = [], 0.0
    for n in range(int(seconds * RATE)):
        t = n / RATE
        phase += (f_end + (f_start - f_end) * math.exp(-drop * t)) / RATE
        out.append(math.sin(2 * math.pi * phase) * math.exp(-decay * t) * min(1.0, t / 0.0015))
    return out


def tone(freq: float, seconds: float, decay: float, shape: str = "sine", duty: float = 0.5,
         glide: float = 1.0, attack: float = 0.002) -> list[float]:
    """A pitched note; glide is the frequency ratio reached at the end."""
    out, phase = [], 0.0
    for n in range(int(seconds * RATE)):
        t = n / RATE
        phase += freq * glide ** (t / seconds) / RATE
        p = phase % 1.0
        if shape == "square":
            v = 1.0 if p < duty else -1.0
        elif shape == "triangle":
            v = 4 * abs(p - 0.5) - 1
        else:
            v = math.sin(2 * math.pi * p)
        out.append(v * math.exp(-decay * t) * min(1.0, t / attack))
    return out


def shards(seconds: float, count: int, seed: int, low: float, high: float) -> list[float]:
    """Breaking glass: many tiny, bright, inharmonic pings scattered over time, thinning out."""
    rng = random.Random(seed)
    out = [0.0] * int(seconds * RATE)
    for _ in range(count):
        start = seconds * 0.85 * rng.random() ** 2.2  # dense at the break, sparse later
        freq = rng.uniform(low, high)
        dur = rng.uniform(0.02, 0.07)
        amp = rng.uniform(0.3, 1.0) * (1.0 - start / seconds)
        ping = tone(freq, dur, decay=rng.uniform(60.0, 120.0), attack=0.0005)
        mix(ping, tone(freq * rng.uniform(1.4, 2.3), dur, decay=140.0, attack=0.0005), gain=0.5)
        mix(out, ping, at=start, gain=amp)
    return out[: int(seconds * RATE)]


def drive(samples: list[float], amount: float) -> list[float]:
    """Gentle saturation to glue layers together."""
    norm = math.tanh(amount)
    return [math.tanh(amount * v) / norm for v in samples]


def punch(seed: int, weight: float = 1.0) -> list[float]:
    """A punch: low body thump, a fleshy slap and a short pitched thwack."""
    out = thump(150.0 * weight, 62.0 * weight, 0.2, drop=40.0, decay=20.0)
    mix(out, noise(0.05, 70.0, seed=seed, lowpass=cutoff(3500), highpass=cutoff(700)), gain=0.55)
    mix(out, tone(330.0 * weight, 0.07, decay=45.0, glide=0.6), gain=0.3)
    return out


def cue_hit() -> list[float]:
    out = punch(seed=11)
    # The "sound" half: a short, warm rising blip on top of the punch.
    mix(out, tone(880.0, 0.12, decay=28.0, shape="triangle", glide=1.12), at=0.006, gain=0.22)
    return finish(reverb(drive(out, 1.1), wet=0.1, tail=0.15, room=0.6), peak=0.65)


def cue_strike() -> list[float]:
    # Something shattering: a crack, then glass shards falling away, over a heavy punch.
    out = punch(seed=21, weight=0.85)
    mix(out, noise(0.012, 300.0, seed=22, lowpass=cutoff(12000), highpass=cutoff(2500)), gain=0.9)  # crack
    mix(out, noise(0.25, 16.0, seed=23, lowpass=cutoff(9000), highpass=cutoff(3000)), at=0.005, gain=0.22)  # crumble
    mix(out, shards(0.55, 70, seed=24, low=2400.0, high=7000.0), at=0.004, gain=0.28)
    # Tonal layer: a bright two-note sting so a strike reads as a reward.
    mix(out, tone(1318.5, 0.35, decay=9.0, shape="triangle"), at=0.01, gain=0.22)
    mix(out, tone(1975.5, 0.35, decay=10.0, shape="triangle"), at=0.05, gain=0.2)
    return finish(reverb(drive(out, 1.3), wet=0.2, tail=0.35, room=0.7), peak=0.9, fade=0.06)


def cue_miss() -> list[float]:
    # Retro menu-cancel blip: two quick descending square-wave notes.
    out = []
    for i, (f, length) in enumerate([(587.3, 0.07), (392.0, 0.13)]):  # D5 -> G4
        note = tone(f, length, decay=10.0, shape="square", duty=0.25, glide=0.97, attack=0.001)
        mix(out, note, at=0.075 * i)
    return finish(lowpass(out, cutoff(5000)), peak=0.4, fade=0.03)


def cue_perfect() -> list[float]:
    # Rising retro arpeggio over a final punch and a shower of sparkles; starts just
    # after the final strike so the two layer into one flourish.
    out = silence(0.12)
    for i, f in enumerate([1046.5, 1318.5, 1568.0, 2093.0]):
        mix(out, tone(f, 0.22, decay=12.0, shape="square", duty=0.25, attack=0.001), at=0.12 + 0.06 * i, gain=0.16)
        mix(out, tone(f, 0.5, decay=6.0, shape="triangle"), at=0.12 + 0.06 * i, gain=0.18)
    final = 0.12 + 0.06 * 4
    mix(out, punch(seed=41, weight=0.8), at=final, gain=0.8)
    mix(out, tone(2093.0, 0.9, decay=3.5, shape="triangle"), at=final, gain=0.22)
    mix(out, tone(3136.0, 0.9, decay=4.0, shape="triangle"), at=final, gain=0.12)
    mix(out, shards(0.9, 45, seed=42, low=3000.0, high=8000.0), at=final, gain=0.15)
    return finish(reverb(out, wet=0.25, tail=0.6), peak=0.85, fade=0.15)


def cue_ring_start() -> list[float]:
    # Two light woodblock knocks: "ready, go".
    out = []
    for i, f in enumerate([700.0, 940.0]):
        knock = thump(f * 1.4, f, 0.08, drop=120.0, decay=55.0)
        mix(knock, noise(0.003, 900.0, seed=50 + i, lowpass=1.0, highpass=cutoff(4000)), gain=0.25)
        mix(out, knock, at=0.13 * i)
    return finish(reverb(out, wet=0.12, tail=0.15, room=0.6), peak=0.35)


SOUNDS = {
    "ring_start.wav": cue_ring_start,
    "hit.wav": cue_hit,
    "strike.wav": cue_strike,
    "miss.wav": cue_miss,
    "perfect.wav": cue_perfect,
}


def build_sounds() -> None:
    folder = DATA / "Sound" / "FX" / "AtelierAlchemy"
    folder.mkdir(parents=True, exist_ok=True)
    for name, make in SOUNDS.items():
        write_wav(folder / name, make())
    print(f"wrote {len(SOUNDS)} sound cues to {folder}")


def main() -> None:
    esp = build_esp()
    (DATA / "AtelierAlchemy.esp").write_bytes(esp)
    swf_path = DATA / "Interface" / "AtelierAlchemy.swf"
    swf_path.parent.mkdir(parents=True, exist_ok=True)
    swf_path.write_bytes(build_swf())
    print(f"wrote {DATA / 'AtelierAlchemy.esp'} ({len(esp)} bytes, {POTION_SLOTS} potion + {MATERIAL_SLOTS} material slots)")
    print(f"wrote {swf_path}")
    build_sounds()


if __name__ == "__main__":
    main()
