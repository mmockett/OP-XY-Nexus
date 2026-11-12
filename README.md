# Nexus

A two-layer polysynth for the OP-XY, after the Yamaha CS-80, built with the
[Open-XY SDK](https://github.com/etcetc000/open-xy). Every note is two
complete synths played together, a few cents apart, each with its own pair
of resonant filters. Swell makes a held note bloom like brass, bite gives it
the CS-80's bright blip, ring modulation turns it to bells, and the CS-80's
own chorus moves it across the stereo field.

**Status: experimental.** Nexus has not yet been played on an OP-XY, and its
CPU figures are estimates.

## The sound

Each voice plays layer I and layer II, as the CS-80 plays its channels I and
II. In each layer a band-limited saw and pulse sound together, mixed by
shape, and the pulse's width moves slowly, each layer at its own rate. The
layer runs through a 12 dB resonant high-pass (lowcut) and a 12 dB resonant
low-pass (bright) in series. Reso sets both resonances, the high-pass's a
little lower, so with the two cutoffs close together the layer has two peaks,
like a voice. After both filters a sine joins at the layer's pitch (sine),
in phase with the saw's fundamental, for weight under a dark filter or, alone,
for bells. The two layers play a few cents apart, so they drift in and out of
phase; layer I carries a breath of noise, and layer II, at its own footage,
can be ring-modulated after its filters.

The OP-XY has no aftertouch, so Nexus plays the CS-80's filter envelope
itself, on both filters at once. **Swell** leans into each held note: the
filter starts dark and blooms over about a second to nearly three times its
resting brightness, the resonance rising with it, and stays bright while the
note sounds. A vibrato arrives a moment later. Each voice does this a little
differently, so a chord opens unevenly, as it does under ten fingers.
**Bite** starts each note bright, up to three octaves over the resting
cutoff, and drops it to the swell's dark start in a fifth of a second: the
CS-80's brass blip. When a key is let go the filter closes towards the dark
start, so notes darken as they fade.

**Chorus** is the CS-80's stereo stage: the level sways between left and
right while a phase shift sweeps, like a rotating speaker, on one clock for
the whole chord. **Spread** leans layer I to the left and layer II to the
right, so their slow phasing moves across the field; left and right still sum
to the mono voice. With both at 0 Nexus is mono, and both sides are the same
voice.

**Page 1:** shape, detune, bright, reso<br>
**Page 2:** chorus, ring, swell, bite<br>
**Shift:** sine, octave, lowcut, noise under page 1; balance, ratio, spread,
drift under page 2. Hold Shift and turn the encoder above.

| Control | 0 | 25 | 50 | 75 | 100 |
| --- | --- | --- | --- | --- | --- |
| shape | saw | saw and pulse | pulse, saw fading | square, narrowing | 12% pulse |
| detune (layer II against I) | 0c | 4c | 11c | 19c | 30c |
| bright (both low-passes) | 80 Hz | 300 Hz | 1.1 kHz | 4.3 kHz | 16 kHz |
| reso (both filters) | none | slight | a peak | strong | ringing |
| chorus (sway, sweep, spread) | mono | gentle | wide | deep | full rotation |
| ring (amount) | off | 25% | 50% | 75% | full |
| swell (start, crescendo, rise) | none | 0.8x to 1.6x in 0.4 s | 0.6x to 2.2x in 0.6 s | 0.4x to 2.7x in 0.9 s | 0.15x to 3.3x in 1.1 s |
| bite (start against rest, at swell 80) | none | -0.5 octave | +0.7 octave | +1.8 octaves | +3 octaves |

| Shift | 0 | 25 | 50 | 75 | 100 |
| --- | --- | --- | --- | --- | --- |
| sine (under shape, after the filters) | none | 25% | 50% | 75%, filters at half | the sine alone |
| lowcut (under bright, both high-passes) | off | 63 Hz | 200 Hz | 630 Hz | 2 kHz |
| noise (under reso, breath in layer I) | none | breathy | airy | hiss | strong hiss |
| balance (under chorus) | layer I | II 8 dB down | even | I 8 dB down | layer II |
| spread (under swell, I left, II right) | mono | 2 dB apart | 4 dB | 7 dB | 10 dB |
| drift (under bite, pitch and tone) | steady | 0.09st | 0.18st | 0.27st | 0.36st |

Swell's start and crescendo are times the resting cutoff. Bite's start lies
between the swell's dark start (1.6 octaves under rest at swell 80) and three
octaves over rest; it falls back in 0.07 s plus 0.15 s times swell. With
swell at 0 there is no dark start, and bite falls to rest in 0.07 s. Both
cutoffs follow the key, the swell and bite together, so lowcut moves with
them. **Octave** (under detune) sets layer II's footage: 16', 8', 4' or 2',
so the second layer can sit an octave under the first or above it.
**Ratio** (under ring) sets the ring modulator's pitch against layer II: 0.5,
1, 1.5 (a fifth), 2, 2.6, 3, 3.5 or 4. Whole numbers stay in tune; 2.6 and
3.5 ring like bells. The defaults are sine 0, octave 8', lowcut off, noise 6,
balance 50, ratio 1.5, spread 40 and drift 25.

## Presets

Each preset carries its amp envelope and its Shift row; Tyrell is the default,
in place of an init. Times are the OP-XY's own: attack to full level, decay to
the sustain level, release to silence. The sends belong to the track, not the
preset, so set them as the table gives: reverb on one send, delay on the other.
The OP-XY's filter (M3) and LFO (M4) are off in every preset.
`nexus/tools/make_presets.py` writes them all, and this table, from one list.

<!-- presets: written by nexus/tools/make_presets.py -->
| Preset | Role | The idea | Amp: attack / decay / sustain / release | Sends: reverb, delay | CPU a voice, est. |
| --- | --- | --- | --- | --- | --- |
| Tyrell | brass | Saw in both layers 3 cents apart, a dark filter that swells over about a second and closes as the note is let go, the chorus and spread wide. The default | 0.36 s / 0.68 s / 90% / 1.9 s | 60, 0 | 4.0% |
| Afterglow | swell | The second layer an octave up and rung at 1, every note opening with the full blip, then a slow swell and a long tail that darkens as it fades | 2.7 s / 0.59 s / 90% / 7.9 s | 75, 20 | 4.4% |
| Long Strings | pad | Slow saws further apart, a softer swell, more drift, chorus and spread | 0.94 s / 0.51 s / 90% / 3.8 s | 60, 0 | 4.0% |
| Glass Choir | pad | Sines over a quiet saw, the second layer an octave up and rung at a fifth, bright and still | 0.51 s / 0.51 s / 80% / 3.3 s | 70, 0 | 5.2% |
| Toy Bells | percussion | Sines alone, rung at a ratio of 3.5, no swell | 0 s / 1.4 s / 0% / 1.8 s | 50, 20 | 2.7% |
| Sixteen Feet | bass | A square with its second layer an octave down, dark and resonant, mono | 0.01 s / 0.06 s / 70% / 0.44 s | 15, 0 | 4.1% |
| Searchlight | lead | A bright lead that bites: each note starts two octaves open and drops back in a tenth of a second, then a short swell, both filters resonant | 0.01 s / 0.1 s / 90% / 0.91 s | 50, 25 | 4.5% |
| Neon Rain | pad | Saws 8 cents apart with the second layer an octave down, a slow swell, the chorus and spread wide, a breath of noise and the lowest lows cut | 1.2 s / 0.62 s / 85% / 6 s | 60, 15 | 4.7% |
| Sodium Haze | pad | Dark saws far apart under a hiss of breath and over a sine, swelling slowly open, wide and drifting | 1.7 s / 0.59 s / 90% / 7 s | 70, 0 | 4.7% |
| High Rise | pad | A saw with a second layer an octave up and a little quieter, bright and open, a sine underneath for body: chords that shimmer over the band | 0.61 s / 0.62 s / 85% / 4.5 s | 65, 25 | 4.8% |
| Vowels | choir | Both filters resonant and close together, two peaks like a voice's, the vowel opening as the swell rises. Turn bright to change the vowel | 0.51 s / 0.59 s / 90% / 3 s | 60, 10 | 4.5% |
| Ember | pad | Saw and square together, the square's width moving slowly, the CS-80's chorus deep, a soft swell: an organ-like string pad | 0.79 s / 0.62 s / 85% / 5 s | 55, 10 | 4.8% |
| Vapor Choir | choir | Resonant saws with the second layer an octave up, a vibrato that arrives late, breath and the widest chorus and spread: a choir across the whole field | 0.94 s / 0.62 s / 85% / 5 s | 75, 0 | 4.7% |
| Ice Tower | glass | Sines alone, the second two octaves up and ringing at 2.6, struck and fading, spread wide: glass bells | 0 s / 2.6 s / 18% / 4.5 s | 65, 30 | 3.0% |
| Undercity | bass | A saw with an octave under it, mono, a resonant filter that growls open as the note is held | 0.01 s / 0.35 s / 65% / 0.48 s | 10, 0 | 3.0% |
| Blip Brass | brass | Brass stabs with the CS-80's blip: every note bright for an instant, a dip, then a short swell, over a resonant low end | 0.01 s / 0.51 s / 85% / 0.3 s | 35, 0 | 4.5% |
<!-- /presets -->

Falls at the ends of phrases and slides into notes come from the OP-XY: its
pitch bend, and the sequencer's slides. The CPU column is estimated, as under
CPU below. Pads play many voices, so most stay near Tyrell's cost. Those over
4.5% a voice run lowcut or sine on both layers (Neon Rain, Sodium Haze, High
Rise, Vapor Choir), saw and pulse together (Ember), or the sine past 50 with
ring (Glass Choir).

## The screen

A dense isometric city in greys, drawn like pixel art: an elevated monorail
crossing it on a long diagonal, a road on a viaduct below, the OP-XY advert
tower, a poster of the sun rising over water, a dot-matrix hourglass, a helipad
with a flying car, masts with lights, a dish, a ball tower, rooftop extractor
fans, plant rooms and windows down every face, tall ones two floors high and,
on the helipad and the towers round its foot, small ones. Looking down on it
there is no sky: behind the near city, far towers, some stepped to a spire and
some rising out of the picture, fill every gap, over the dark streets of the
city below. Tops are light, sides mid and dark.

The windows light a few at a time, in a scattered order, never a column at
once. Behind the near city hangs one sheet of fog, over the far towers and
streets. As it thickens the far city pales almost to white, while the near city
stays sharp in front of it.

At rest the city is still. While a note is held, a two-car train runs along
the monorail, passing behind the nearer towers and coming round again; tiny
cars drive both ways along the road below, the fans turn smoothly, the dish
swings, the flying car bobs and the lights blink. When the note ends,
everything eases to a stop where it is.

| Control | What it changes |
| --- | --- |
| shape | The advert's pattern: ramps for the saw, a checker for the pulse and square, thin bars for the narrow pulse |
| detune | The hourglass's two halves of dark dots drift apart |
| bright | One fog sheet over the far city, and the window lights. At 0 there is no fog: the far city is dark. As it rises, the fog thickens to nearly white, and more windows and background lights come on |
| reso | The advert, the billboards and the neon edges brighten |
| chorus | The train's speed |
| ring | Rings on the masts, round the ball tower and in the dish, faint to bright, one then two; each passes behind its mast or the ball and in front of it |
| swell | How many dark windows glow with each note, and the advert's pattern pulses while it's held. The glow follows the track's amp envelope: up over its attack, down to its sustain, out over its release, through the greys between |
| bite | How high the flying car hovers over the helipad: at 0 it has landed |
| sine (Shift) | The two lines under the advert's pattern, short to long |
| lowcut (Shift) | The sun poster's reflection on the water clears stripe by stripe from the bottom up |

With a note, each glowing window brightens a little after the one before:
dark at the note's start, rising through the greys during the attack, white
at the peak, settling to the sustain's level and fading back to dark over the
release. When the OP-XY can't report the envelope, the windows flare one by
one while the note is held, as before.

While Shift is held, the header's bright row shows the Shift controls. Sine's
lines and lowcut's reflection follow them whether or not Shift is held.

## CPU

Estimated, not measured: counted from the Blackfin instructions in the
compiled sound loops, at 1.3 to 1.5 cycles an instruction, plus about 1.5% of
the block for the OP-XY's own share of a voice. Only a measurement on the
OP-XY gives a real figure.

| Setting | Instructions a sample | Per voice, % of the block |
| --- | --- | --- |
| Tyrell with chorus 0 and spread 0 (mono) | 88 | 3.1 to 3.3 |
| Tyrell | 132 | 3.9 to 4.1 |
| Tyrell with lowcut on | 160 | 4.3 to 4.7 |
| Sine alone (Toy Bells) | 66 | 2.7 to 2.8 |
| Saw and pulse, lowcut, sine up to 50, chorus | 239 | 5.8 to 6.3 |
| Heaviest: that with sine past 50, ring and balance off centre | 273 | 6.4 to 7.0 |

Swell, bite, the release and the pulse's movement cost nothing a sample.
Lowcut, sine, noise, ring, spread and drift cost nothing at 0, nor balance at
50; sine at 100 skips both filters and the saw.

## Building

Nexus needs the Open-XY SDK at version 1.13 or later (stereo synth voices and
a synth's Shift row). A firmware with a Shift-row synth carries the SDK's
preset and step-lock services, about 86 KiB, once for the whole build.

```sh
python open-xy/sdk.py test --machine nexus          # records, preview and the host checks
python open-xy/sdk.py compile --machine nexus --out <new folder>   # the Blackfin compile
```

`nexus/tools/make_panel.py` writes the screen's artwork and the tables
`display.cpp` reads (it needs shapely 2.2.0); `nexus/tools/make_tables.py`
writes the sound's tables.

## Licence

MIT. See [LICENSE](LICENSE).
