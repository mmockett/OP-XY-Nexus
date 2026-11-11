#!/usr/bin/env python3
"""Writes every Nexus preset from the one table below:

  - definition.json's "presets" (the previews and the builder's list),
  - presets/<name>.json, the OP-XY preset files the builder installs as
    content/presets/nexus/<name>.json, with each preset's amp envelope and its
    Shift row (Tyrell is default.json, which replaces the generated init),
  - tests/presets.h, which tests/sound.cpp plays,
  - the preset table in ../README.md, between its preset markers.

Pure Python; run it after changing the table, a control or the layout:

    python3 tools/make_presets.py

The table holds the controls as the header shows them (0..100, octave and
ratio by their labels) and the amp envelope in seconds, which are turned into
stock's amp words by the OP-XY's traced curve (amp_curve.h, OS 1.2.1): attack
and decay larger-is-longer, release inverted (larger is shorter), sustain
linear. The words and their real times are printed.
"""
import json
import math
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent.parent          # the machine folder
README = HERE.parent / "README.md"
DEFINITION = HERE / "definition.json"
PRESETS = HERE / "presets"
TESTS = HERE / "tests" / "presets.h"

PAGE = ["shape", "detune", "bright", "reso", "chorus", "ring", "touch", "ribbon"]
SHIFT = ["balance", "octave", "lowcut", "noise", "spread", "ratio", "drift", "slide"]

# ---------------------------------------------------------------------------------- the table
# controls: the eight page controls, then the Shift row, as the header shows them.
# amp: attack s (to full level), decay s (to the sustain level), sustain 0..1, release s (to silence),
#      or "words" to keep four words as they are. sends: reverb and delay, 0..100: the player's, not
#      the preset's (an OP-XY synth preset doesn't keep the track's FX I and FX II sends).
TABLE = [
    dict(name="Tyrell", file="default", role="brass",
         idea="Saw in both layers 3 cents apart, a dark filter that swells over about a second, the chorus and "
              "spread wide. The default",
         controls=dict(shape=33, detune=22, bright=46, reso=28, chorus=45, ring=0, touch=80, ribbon=50,
                       balance=50, octave="8ft", lowcut=0, noise=6, spread=40, ratio="1.5", drift=25, slide=30),
         amp="words", sends=(60, 0)),
    dict(name="Long Strings", role="pad",
         idea="Slow saws further apart, a softer swell, more drift, chorus and spread",
         controls=dict(shape=34, detune=60, bright=42, reso=10, chorus=40, ring=0, touch=35, ribbon=50,
                       balance=50, octave="8ft", lowcut=0, noise=6, spread=36, ratio="1.5", drift=35, slide=30),
         amp=(0.9, 0.5, 0.9, 3.8), sends=(60, 0)),
    dict(name="Glass Choir", role="pad",
         idea="Near-sine layers, the second an octave up, with ring at a fifth, bright and still",
         controls=dict(shape=10, detune=30, bright=62, reso=30, chorus=30, ring=25, touch=20, ribbon=50,
                       balance=50, octave="4ft", lowcut=0, noise=6, spread=27, ratio="1.5", drift=25, slide=30),
         amp=(0.5, 0.5, 0.8, 3.3), sends=(70, 0)),
    dict(name="Toy Bells", role="percussion",
         idea="Sine, ring at a ratio of 3.5, no swell",
         controls=dict(shape=0, detune=20, bright=70, reso=20, chorus=0, ring=70, touch=0, ribbon=50,
                       balance=50, octave="8ft", lowcut=0, noise=6, spread=0, ratio="3.5", drift=25, slide=30),
         amp=(0.0, 1.4, 0.0, 1.8), sends=(50, 20)),
    dict(name="Sixteen Feet", role="bass",
         idea="A square with its second layer an octave down, dark and resonant, mono",
         controls=dict(shape=66, detune=20, bright=30, reso=30, chorus=0, ring=0, touch=40, ribbon=50,
                       balance=40, octave="16ft", lowcut=0, noise=6, spread=0, ratio="1.5", drift=25, slide=30),
         amp=(0.01, 0.06, 0.7, 0.45), sends=(15, 0)),
    dict(name="Rise Lead", role="lead",
         idea="A saw that slides up seven semitones into each note, over about 0.4 s",
         controls=dict(shape=34, detune=15, bright=58, reso=30, chorus=20, ring=0, touch=50, ribbon=79,
                       balance=50, octave="8ft", lowcut=0, noise=6, spread=18, ratio="1.5", drift=20, slide=69),
         amp=(0.05, 0.1, 0.9, 1.2), sends=(50, 20)),
    # ---- the pads
    dict(name="Neon Rain", role="pad",
         idea="Saws 8 cents apart with the second layer an octave down, a slow swell, the chorus and spread "
              "wide, a breath of noise and the lowest lows cut",
         controls=dict(shape=33, detune=40, bright=48, reso=16, chorus=55, ring=0, touch=45, ribbon=50,
                       balance=44, octave="16ft", lowcut=20, noise=10, spread=55, ratio="1.5", drift=35, slide=30),
         amp=(1.2, 0.6, 0.85, 6.0), sends=(60, 15)),
    dict(name="Sodium Haze", role="pad",
         idea="Dark saws far apart under a hiss of breath, swelling slowly open, wide and drifting",
         controls=dict(shape=33, detune=58, bright=30, reso=24, chorus=40, ring=0, touch=70, ribbon=50,
                       balance=50, octave="8ft", lowcut=24, noise=24, spread=65, ratio="1.5", drift=45, slide=30),
         amp=(1.8, 0.6, 0.9, 7.0), sends=(70, 0)),
    dict(name="High Rise", role="pad",
         idea="A saw with a second layer an octave up and a little quieter, bright and open, for chords "
              "that shimmer over the band",
         controls=dict(shape=33, detune=34, bright=54, reso=14, chorus=50, ring=0, touch=50, ribbon=50,
                       balance=40, octave="4ft", lowcut=30, noise=8, spread=50, ratio="1.5", drift=30, slide=30),
         amp=(0.6, 0.6, 0.85, 4.5), sends=(65, 25)),
    dict(name="Harbor Fog", role="pad",
         idea="A dark, low pad: the second layer an octave down and leading, a deep swell, wide and slow",
         controls=dict(shape=33, detune=46, bright=32, reso=20, chorus=45, ring=0, touch=62, ribbon=50,
                       balance=60, octave="16ft", lowcut=14, noise=14, spread=45, ratio="1.5", drift=40, slide=30),
         amp=(1.5, 0.6, 0.9, 6.5), sends=(55, 0)),
    dict(name="Ember", role="pad",
         idea="Hollow squares 6 cents apart, the CS-80's chorus deep, a soft swell: an organ-like string pad",
         controls=dict(shape=56, detune=32, bright=36, reso=22, chorus=62, ring=0, touch=40, ribbon=50,
                       balance=50, octave="8ft", lowcut=20, noise=5, spread=40, ratio="1.5", drift=30, slide=30),
         amp=(0.8, 0.6, 0.85, 5.0), sends=(55, 10)),
    dict(name="Afterglow", role="swell",
         idea="Each note slides up a semitone into place over most of a second while the filter blooms its "
              "fullest: saws wide apart, far apart in the field, a long attack",
         controls=dict(shape=33, detune=66, bright=36, reso=30, chorus=65, ring=0, touch=100, ribbon=46,
                       balance=50, octave="8ft", lowcut=22, noise=12, spread=75, ratio="1.5", drift=50, slide=86),
         amp=(2.6, 0.6, 0.9, 8.0), sends=(75, 20)),
    dict(name="Vapor Choir", role="choir",
         idea="Resonant saws with the second layer an octave up, a vibrato that arrives late, breath and the "
              "widest chorus and spread: a choir across the whole field",
         controls=dict(shape=33, detune=30, bright=44, reso=44, chorus=80, ring=0, touch=55, ribbon=50,
                       balance=42, octave="4ft", lowcut=36, noise=20, spread=85, ratio="1.5", drift=40, slide=30),
         amp=(0.9, 0.6, 0.85, 5.0), sends=(75, 0)),
    # ---- the machine's range
    dict(name="Ice Tower", role="glass",
         idea="Sine layers, the second two octaves up and ringing at 2.6, struck and fading, spread wide: "
              "glass bells",
         controls=dict(shape=0, detune=26, bright=74, reso=18, chorus=0, ring=40, touch=0, ribbon=50,
                       balance=38, octave="2ft", lowcut=30, noise=0, spread=70, ratio="2.6", drift=15, slide=30),
         amp=(0.0, 2.6, 0.18, 4.5), sends=(65, 30)),
    dict(name="Undercity", role="bass",
         idea="A saw with an octave under it, mono, a resonant filter that growls open as the note is held",
         controls=dict(shape=33, detune=14, bright=26, reso=46, chorus=0, ring=0, touch=55, ribbon=50,
                       balance=58, octave="16ft", lowcut=0, noise=0, spread=0, ratio="1.5", drift=10, slide=30),
         amp=(0.005, 0.35, 0.65, 0.5), sends=(10, 0)),
    dict(name="Ion Rings", role="texture",
         idea="Saws rung at 3.5 against the second layer, swelling and drifting: metallic, unsettled, wide",
         controls=dict(shape=33, detune=40, bright=48, reso=36, chorus=50, ring=55, touch=60, ribbon=50,
                       balance=50, octave="8ft", lowcut=0, noise=0, spread=55, ratio="3.5", drift=45, slide=30),
         amp=(0.7, 0.6, 0.8, 5.0), sends=(65, 30)),
]

# ---------------------------------------------------------------------------------- the amp envelope
# OS 1.2.1's amp envelope, from amp_curve.h (the traced curve, closed-form fits): stock reads the top 7
# bits of each word, i = word >> 8 of the 15-bit preset word.
SR = 44100.0
BLOCK_S = 64 / SR


def blocks(s):
    return math.ceil(s / BLOCK_S - 1e-9) * BLOCK_S


def tau(j):
    """One-pole time constant of coefficient index j, seconds."""
    return 0.000763 if j == 0 else 0.00566 * j + 0.00026 * 2 ** (j / 8.7)


def attack_s(word):
    i = word >> 8
    return BLOCK_S if i < 2 else blocks(2 ** (7.8945 + 0.1261 * i) / SR)


def decay_s(word, sustain):
    """Peak to sustain + 1%."""
    level = sustain / 32767 + 0.01
    return 0.0 if level >= 1 else blocks(tau(max(1, word >> 8)) * -math.log(level))


def release_s(word):
    """Full level to stock's -66 dB cut-off. Inverted: a larger word is a shorter release."""
    return blocks(tau(127 - (word >> 8)) * math.log(2000))


def nearest(times, target):
    """The index 0..127 whose time is closest to target (in ratio), and its word (mid-step)."""
    i = min(range(128), key=lambda k: abs(math.log((times(k) + 1e-4) / (target + 1e-4))))
    return min(32767, (i << 8) + 128)


def amp_words(amp, defaults):
    if amp == "words":
        return dict(defaults)
    a, d, s, r = amp
    sustain = round(s * 32767)
    return dict(attack=nearest(lambda i: attack_s(i << 8), a),
                decay=nearest(lambda i: decay_s(i << 8, sustain), d) if sustain < 32440 else 16384,
                sustain=sustain,
                release=nearest(lambda i: release_s(i << 8), r))


def amp_seconds(words):
    return (attack_s(words["attack"]), decay_s(words["decay"], words["sustain"]), words["sustain"] / 32767,
            release_s(words["release"]))


# ---------------------------------------------------------------------------------- CPU
def cpu(controls):
    """Per voice, % of the block, estimated (not measured): README's CPU table taken apart by the work
    each setting adds to the sound loops (engine.cpp's layer, trim and stereo stages)."""
    c = controls
    est = 3.35                                     # Tyrell with chorus and spread 0: saw, breath, drift
    w = math.floor(c["shape"] / 100 * 32767 + 0.5)  # engine.cpp wave_of: within 2% of sine or saw, one wave
    if w < 10923:
        wave = "sine" if w * 3 < 655 else "saw" if 32767 - w * 3 < 655 else "sine+saw"
    else:
        wave = "pulse" if w >= 21845 or ((w - 10923) * 3 * 29491) >> 15 else "saw"
    est += {"sine": 0.38, "saw": 0.0, "sine+saw": 1.06, "pulse": 0.6}[wave]
    est += 0.49 if c["ring"] > 0 else 0              # the ring's sine on layer II
    est += 0.7 if c["chorus"] > 0 else 0.15 if c["spread"] > 0 else 0
    est -= 0.15 if c["noise"] == 0 else 0
    level, cut = c["balance"] != 50, c["lowcut"] > 0
    est += 0.25 if level and cut else 0.2 if cut else 0.15 if level else 0
    return est, wave


# ---------------------------------------------------------------------------------- values
def normalized(definition, preset):
    """The sixteen controls as definition.json stores them, 0..1 (an enum at its choice's middle)."""
    by_id = {p["id"]: p for p in definition["parameters"] + definition["shift"]}
    values = {}
    for key in PAGE + SHIFT:
        p, v = by_id[key], preset["controls"][key]
        if p["mapping"]["kind"] == "enum":
            labels = p["mapping"]["labels"]
            values[key] = (labels.index(v) + 0.5) / len(labels)
        else:
            if not (isinstance(v, (int, float)) and 0 <= v <= 100):
                sys.exit(f"{preset['name']}: {key} must be 0..100")
            values[key] = round(v / 100, 4)
    return values


def shift_params(definition, values):
    """The four words of the region keeping the Shift row, as tooling.definitions.shift_preset_params
    packs them: eight 7-bit fields (an amount's step 0..100, an enum's choice), first lowest, 15 bits a word."""
    fields = []
    for p in definition["shift"]:
        v = values[p["id"]]
        if p["mapping"]["kind"] == "enum":
            n = len(p["mapping"]["labels"])
            fields.append(min(n - 1, ((math.floor(v * 0x7fffffff + 0.5) >> 16) * n) >> 15))
        else:
            fields.append(min(100, math.floor(v * 100 + 0.5)))
    fields += [0] * (8 - len(fields))
    bits = sum(f << (7 * i) for i, f in enumerate(fields))
    return [bits >> (15 * w) & 0x7FFF for w in range(4)]


def preset_file(definition, values, amp):
    """A version 4 OP-XY preset, as the SDK's template (tooling/default_preset.json) writes the init,
    with the eight controls, the amp envelope and the Shift row in one sample-less region. The OP-XY's
    filter (M3) and LFO (M4) are off, as in the init."""
    return {
        "engine": {"bendrange": 8191, "highpass": 0,
                   "modulation": {s: {"amount": 16384, "target": 0}
                                  for s in ("aftertouch", "modwheel", "pitchbend", "velocity")},
                   "params": [math.floor(values[k] * 32767 + 0.5) for k in PAGE],
                   "playmode": "poly", "portamento.amount": 0, "portamento.type": 0, "transpose": 0,
                   "tuning.root": 0, "tuning.scale": 0, "velocity.sensitivity": 0, "volume": 16384, "width": 0},
        "envelope": {"amp": {k: amp[k] for k in ("attack", "decay", "release", "sustain")},
                     "filter": {"attack": 0, "decay": 0, "release": 0, "sustain": 32767}},
        "fx": {"active": False, "params": [0, 0, 0, 0, 0, 32767, 0, 0], "type": "svf"},
        "lfo": {"active": False, "params": [0] * 8, "type": "value"},
        "octave": 0, "platform": "OP-XY",
        "regions": [{"hikey": 53, "model": "fm slap", "params": shift_params(definition, values)}],
        "type": definition["id"], "version": 4}


def slug(preset):
    return preset.get("file") or re.sub(r"[^a-z0-9]+", "-", preset["name"].lower()).strip("-")


# ---------------------------------------------------------------------------------- the README table
def fmt_s(s):
    return "0" if s < 0.0015 else f"{s:.2f}".rstrip("0").rstrip(".") if s < 1 else f"{s:.1f}".rstrip("0").rstrip(".")


def readme_section(rows):
    lines = ["| Preset | Role | The idea | Amp: attack / decay / sustain / release | Sends: reverb, delay | "
             "CPU a voice, est. |", "| --- | --- | --- | --- | --- | --- |"]
    for preset, seconds in rows:
        a, d, s, r = seconds
        sends = preset["sends"]
        lines.append(f"| {preset['name']} | {preset['role']} | {preset['idea']} | "
                     f"{fmt_s(a)} s / {fmt_s(d)} s / {round(s * 100)}% / {fmt_s(r)} s | "
                     f"{sends[0]}, {sends[1]} | {cpu(preset['controls'])[0]:.1f}% |")
    return "\n".join(lines)


def main():
    text = DEFINITION.read_text()
    definition = json.loads(text)
    machine = json.loads((HERE / "machine.json").read_text())
    defaults = machine["presetDefaults"]["amp"]
    names = [p["name"] for p in TABLE]
    if len(TABLE) > 16 or len(set(names)) != len(names) or len({slug(p) for p in TABLE}) != len(TABLE):
        sys.exit("At most 16 presets, each with its own name")

    presets, files, rows = {}, {}, []
    print(f"{'preset':14} {'attack':>14} {'decay':>14} {'sustain':>14} {'release':>14}   (seconds: word)")
    for preset in TABLE:
        values = normalized(definition, preset)
        presets[preset["name"]] = values
        amp = amp_words(preset["amp"], defaults)
        seconds = amp_seconds(amp)
        files[slug(preset)] = preset_file(definition, values, amp)
        rows.append((preset, seconds))
        a, d, s, r = seconds
        print(f"{preset['name']:14} {a:7.3f}: {amp['attack']:5d} {d:7.3f}: {amp['decay']:5d} "
              f"{s:7.2f}: {amp['sustain']:5d} {r:7.2f}: {amp['release']:5d}")

    definition["presets"] = presets
    DEFINITION.write_text(json.dumps(definition, indent=2) + "\n")

    PRESETS.mkdir(exist_ok=True)
    for old in PRESETS.glob("*.json"):
        if old.stem not in files:
            old.unlink()
    for name, value in files.items():
        data = json.dumps(value, indent=2) + "\n"
        if len(data.encode()) > 16384:
            sys.exit(f"{name}: a preset is at most 16 KiB")
        (PRESETS / f"{name}.json").write_text(data)

    head = ("// Generated by tools/make_presets.py from its table. Edit that, not this file.\n#pragma once\n\n"
            "// The presets: the eight controls, then the Shift row, 0..1 as definition.json has them:\n// "
            + ", ".join(PAGE + SHIFT) + ".\n"
            "struct Preset { const char* name; double c[16]; };\nconst Preset presets[] = {\n")
    body = "".join(f'    {{"{n}", {{{", ".join(repr(float(v[k])) for k in PAGE + SHIFT)}}}}},\n'
                   for n, v in presets.items())
    TESTS.write_text(head + body + f"}};\nconst unsigned preset_count = {len(presets)};\n")

    readme = README.read_text()
    start, end = "<!-- presets: written by nexus/tools/make_presets.py -->", "<!-- /presets -->"
    if start not in readme or end not in readme:
        sys.exit("README.md has no preset markers")
    before, rest = readme.split(start, 1)
    after = rest.split(end, 1)[1]
    README.write_text(before + start + "\n" + readme_section(rows) + "\n" + end + after)
    print(f"{len(TABLE)} presets: definition.json, presets/, tests/presets.h, README.md")


if __name__ == "__main__":
    main()
