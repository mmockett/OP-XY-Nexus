# Nexus

A two-layer polysynth for the OP-XY, after the Yamaha CS-80, built with the
[Open-XY SDK](https://github.com/etcetc000/open-xy). Every note is two
complete synths played together, a few cents apart, each with its own
resonant filter. Touch makes a held note swell like brass, ribbon slides
each note in, ring modulation turns it to bells, and the CS-80's own chorus
spreads it across the stereo field.

**Status: experimental.** Nexus has not yet been played on an OP-XY, and its
CPU figures are estimates.

## The sound

Each voice plays layer I and layer II, as the CS-80 plays its channels I and
II. Each layer is a band-limited oscillator that morphs from sine through saw
and square to a narrow pulse, into its own 12 dB resonant low-pass. The two
layers play the same wave a few cents apart, so they drift in and out of
phase; layer I carries a breath of noise, and layer II can be ring-modulated.
Every voice wanders a little in pitch and tone.

The OP-XY has no aftertouch or ribbon, so Nexus plays their parts itself.
**Touch** leans into each held note: the filter starts dark and swells over
about a second to nearly three times its resting brightness, the resonance
rising with it, and stays bright while the note sounds. A vibrato arrives a
moment later. Each voice does this a little differently, so a chord opens
unevenly, as it does under ten fingers. **Ribbon** slides each note in from up
to an octave below or above.

**Chorus** is the CS-80's stereo stage: the level sways between left and
right while a phase shift sweeps, like a rotating speaker, on one clock for
the whole chord. It also spreads the layers, layer I to the left and layer II
to the right, so their slow phasing moves across the field. At 0 Nexus is
mono, and both sides are the same voice.

**Page 1:** shape, detune, bright, reso<br>
**Page 2:** chorus, ring, touch, ribbon

| Control | 0 | 25 | 50 | 75 | 100 |
| --- | --- | --- | --- | --- | --- |
| shape | sine | sine into saw | saw into square | square, narrowing | 12% pulse |
| detune (layer II against I) | 0c | 4c | 11c | 19c | 30c |
| bright (both low-passes) | 80 Hz | 300 Hz | 1.1 kHz | 4.3 kHz | 16 kHz |
| reso | none | slight | a peak | strong | ringing |
| chorus (sway, sweep, spread) | mono | gentle | wide | deep | full rotation |
| ring (amount, ratio) | off | 25%, a fifth | 50%, 2.6 | 75%, 3.6 | full, 4.0 |
| touch (swell, rise) | none | 1.6x in 0.4 s | 2.2x in 0.6 s | 2.7x in 0.9 s | 3.3x in 1.1 s |
| ribbon (slide in from) | -12st | -6st | 0st | +6st | +12st |

Shift and a turn moves any control in fine steps.

## Presets

| Preset | Role | The idea |
| --- | --- | --- |
| Tyrell | brass | Saw in both layers 3 cents apart, a dark filter that swells over about a second, the chorus wide. The default |
| Long Strings | pad | Slow saws further apart, a softer swell, chorus |
| Glass Choir | pad | Near-sine layers with ring at a fifth, bright and still |
| Toy Bells | percussion | Sine, ring at 3.5, no swell: play it with a short envelope |
| Sixteen Feet | bass | A square, dark and resonant, mono |
| Rise Lead | lead | A saw that slides up seven semitones into each note |

Tyrell sounds as intended with a slow amp envelope (attack about 0.35 s,
release about 2 s), the OP-XY's filter (M3) and LFO (M4) off, and a long, dark
reverb send. Falls at the ends of phrases come from the OP-XY's pitch bend.

## The screen

An isometric night city: dark towers with light edges, lit windows, neon
strips and masts. While a note is held, a white flying car crosses it with
its wingman; at the edge of the screen the city turns the page and the car
flies on over the next stretch, which never repeats.

| Control | What it changes |
| --- | --- |
| shape | The skyline: rolling for sine, ramps for saw, blocks for square, spikes for pulse |
| detune | The wingman (layer II) drifts off the leader's line |
| bright | How many windows are lit |
| reso | The neon strips brighten |
| chorus | The two cars weave apart and back |
| ring | Rings on the masts, faint to bright, one then two |
| touch | Dark windows flare one by one while a note is held |
| ribbon | How high the cars fly |

## CPU

Estimated, not measured: counted from the Blackfin instructions in the
compiled sound loops, at 1.3 to 1.5 cycles an instruction, plus about 1.5% of
the block for the OP-XY's own share of a voice. Only a measurement on the
OP-XY gives a real figure.

| Setting | Per voice, % of the block |
| --- | --- |
| Tyrell with chorus 0 (mono) | 3.1 to 3.3 |
| Tyrell | 3.8 to 4.1 |
| Pulse, ring and chorus up | 4.8 to 5.3 |
| Heaviest: shape between sine and saw, ring and chorus up | 5.4 to 6.0 |

## Building

Nexus needs the Open-XY SDK at version 1.12 or later (stereo synth voices).

```sh
python open-xy/sdk.py test --machine nexus          # records, preview and the host checks
python open-xy/sdk.py compile --machine nexus --out <new folder>   # the Blackfin compile
```

`nexus/tools/make_panel.py` writes the screen's artwork and the tables
`display.cpp` reads; `nexus/tools/make_tables.py` writes the sound's tables.

## Licence

MIT. See [LICENSE](LICENSE).
