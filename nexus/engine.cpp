#include "core.h"
#include "tables.h"
#include <opxy/divide.hpp>
#include <opxy/q15.hpp>
#include <opxy/q31.hpp>

// Nexus: a two-layer polysynth after the Yamaha CS-80.
//
// Each voice plays two layers, as the CS-80 plays its channels I and II. Each
// layer is a band-limited oscillator that morphs sine, saw, square and narrow
// pulse, into its own 12 dB resonant state-variable low-pass. The layers play the same wave a few cents apart, so
// they phase slowly against each other; layer I breathes a little noise, and
// layer II, at its own footage, can be ring-modulated by a sine at a chosen ratio.
//
// Touch is the brass swell: each note's filter starts dark and blooms over
// about a second to near three times its resting cutoff, its resonance rising
// with it, and holds bright while the note sounds; a vibrato arrives a little
// later. Each voice does this slightly differently, so chords open unevenly.
// Ribbon slides each note in from up to an octave away. Every voice drifts a
// little in pitch and tone.
//
// Then the CS-80's stereo: chorus sways the level between left and right while
// a phase shift sweeps (two first-order allpasses a side), on one clock shared
// by every voice, and spread leans layer I to the left and layer II to the right
// (L = mono + side, R = mono - side). At chorus 0 and spread 0 both sides are
// the mono voice.
//
// The Shift row (SDK 1.13) sits under the eight controls, in parameters 8..15:
// balance, octave, lowcut, noise under page 1; spread, ratio, drift, slide under
// page 2. Each is read once a block, and the work behind lowcut, noise, spread,
// drift and a balance off centre is skipped where it isn't needed.
//
// Integer arithmetic only: oscillators in Q15 with 16 x 16 multiplies,
// filters in Q26 signals and Q31 coefficients. Divisions run once a block.
namespace nexus {
namespace engine {
using opxy::q31::multiply;
using opxy::q15::product;

// The Shift row's scales: noise 6 and drift 25 give the levels the voice always had.
const i32 breath_full = 19868;           // Q15 at noise 100: 0.35 of gaussian breath, as uniform noise of the same power
const i32 drift_reach = 1964;            // Q16 octaves at drift 100: the pitch wander's reach, 0.36 semitones
const i32 drift_sine = 220;              // Q16 octaves at drift 100: the slow pitch wobble, 0.04 semitones
const i32 tone_wander = 5676;            // Q16 octaves at drift 100: the cutoff's wobble, 6%
// Output: 0.2 of full scale per model unit, each layer at cos 45 degrees (balance
// 50), from Q26 with a shift of 3. A chord of eight peaks near 0.4 to 0.8.
const i32 output_gain = 1214800200;      // Q31: 0.2 x 0.7071 x 32 / 8
const i32 ceiling = 0x30000000 >> 3;     // the declared peak, 0.375 of full scale, before the shift
const u32 fastest = 0x40000000u;         // increments capped at a quarter of the sample rate
const u32 c4 = 25477629u;                // the increment of middle C (261.6 Hz), where the filter's keyboard tracking pivots
const u32 chorus_rate = 5609753u;        // the chorus clock's increment a block: 0.9 Hz
const i32 chorus_centre = 336153;        // its phase shift's corner, 700 Hz, Q16 octaves above 20 Hz

static inline i32 clamp(i32 x, i32 low, i32 high) { return x < low ? low : x > high ? high : x; }
static inline i32 q15(i32 control) { return control < 0 ? 0 : control >> 16; }
// A Shift row amount's step, 0..100, as the OP-XY turns it (it plays step / 100 of full scale).
static inline i32 step_of(i32 control) { return (q15(control) * 100 + 16384) >> 15; }
// A Shift row enum's choice, 0..count - 1.
static inline u32 choice_of(i32 control, u32 count) { return (static_cast<u32>(q15(control)) * count) >> 15; }
static inline u32 next(u32& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// sin(2 pi phase), Q15: a parabola refined once (0.775 p + 0.225 p|p|).
static inline i32 sine(u32 phase) {
    const i32 s = static_cast<i32>(phase) >> 16;
    const i32 a = s < 0 ? -s : s;
    const i32 p = (s * (32767 - a)) >> 13;
    const i32 pa = p < 0 ? -p : p;
    return p + ((((product(p, pa) >> 15) - p) * 7373) >> 15);
}
// 2^(f / 65536) for a Q16 fraction f in 0..65535, Q29 (1..2).
static inline i32 exp2_fraction(u32 f) {
    const u32 index = f >> 11;
    const i32 fraction = static_cast<i32>((f & 2047u) << 20);
    return tables::exp2[index] + multiply(tables::exp2[index + 1] - tables::exp2[index], fraction);
}
// 2^-x for Q16 x >= 0, Q15.
static inline i32 fall(i32 x) {
    if (x <= 0) return 32767;
    const u32 whole = static_cast<u32>(x) >> 16, f = static_cast<u32>(x) & 0xffffu;
    if (whole >= 15) return 0;
    const i32 v = f ? exp2_fraction(65536u - f) >> 1 : 1 << 29;    // 2^-f, Q29
    return clamp((v >> 14) >> whole, 0, 32767);
}
// log2 of x, Q16 (its whole part is the top bit's place): a quadratic within an octave, within 0.005.
static inline i32 log2_q16(u32 x) {
    if (!x) return 0;
    u32 m = x;
    i32 top = 0;
    if (m >> 16) { m >>= 16; top += 16; }
    if (m >> 8) { m >>= 8; top += 8; }
    if (m >> 4) { m >>= 4; top += 4; }
    if (m >> 2) { m >>= 2; top += 2; }
    if (m >> 1) top += 1;
    const u32 f = (top >= 16 ? x >> (top - 16) : x << (16 - top)) - 65536u;   // 0..65535
    return top * 65536 + static_cast<i32>(f + ((((f * (65536u - f)) >> 16) * 22713u) >> 16));
}
// A phase increment moved by Q16 octaves (-12..+12), capped at `fastest`.
static inline u32 transpose(u32 increment, i32 octaves) {
    const u32 shifted = static_cast<u32>(clamp(octaves, -12 * 65536, 12 * 65536) + 16 * 65536);
    const u32 whole = shifted >> 16;                                  // 16 is no shift
    const u32 base = static_cast<u32>(multiply(static_cast<i32>(increment >> 1), exp2_fraction(shifted & 0xffffu)));
    u32 result;
    if (whole >= 13) {
        const u32 up = whole - 13;
        result = up >= 8 || base > (fastest >> up) ? fastest : base << up;
    } else {
        result = base >> (13 - whole);
    }
    return result > fastest ? fastest : result;
}
// A table indexed in quarter octaves above 20 Hz, read at Q16 octaves.
static inline i32 at_quarter(const i32* table, i32 octaves) {
    const u32 position = static_cast<u32>(clamp(octaves, 0, 39 * 16384 - 1)) * 4;
    const u32 index = position >> 16;
    const i32 fraction = static_cast<i32>((position & 0xffffu) << 15);
    return table[index] + multiply(table[index + 1] - table[index], fraction);
}
// A 17-entry table over a Q15 control.
static inline i32 along(const i32* table, i32 q) {
    const u32 position = static_cast<u32>(clamp(q, 0, 32767)) << 4;   // 16 steps
    const u32 index = position >> 15;
    const i32 fraction = static_cast<i32>(position & 32767u);
    return table[index] + (((table[index + 1] - table[index]) * fraction) >> 15);
}
// 3u^2 - 2u^3 for Q15 u
static inline i32 smooth(i32 u) {
    const i32 u2 = (u * u) >> 15;
    return (u2 * (98304 - 2 * u)) >> 15;
}

// ------------------------------------------------------------------ one layer
// How the shape control mixes an oscillator, Q15: the saw, a second saw `width`
// ahead (subtracted, it makes a pulse) and a sine.
enum Mode { sine_only, sine_saw, saw_only, saw_pulse };
struct Wave { Mode mode; i32 saw, pulse, sine; u32 width; };
static inline Wave wave_of(i32 s) {
    Wave w;
    w.width = 0x80000000u;
    w.pulse = 0;
    if (s < 10923) {
        // sine to saw
        const i32 u = s * 3;
        w.saw = u;
        w.sine = 32767 - u;
        w.mode = u < 655 ? sine_only : w.sine < 655 ? saw_only : sine_saw;   // within 2%: one wave
        if (w.mode == sine_only) w.sine = 32767;
        if (w.mode == saw_only) w.saw = 32767;
    } else if (s < 21845) {
        // saw to square: (1 - u) saw + 0.9 u square, the square a saw less a saw half a cycle on
        const i32 u = (s - 10923) * 3;
        w.saw = 32767 - ((u * 3277) >> 15);
        w.pulse = (u * 29491) >> 15;
        w.sine = 0;
        w.mode = w.pulse ? saw_pulse : saw_only;
    } else {
        // the square narrows to a 12% pulse, at 0.9
        const i32 t = clamp((s - 21845) * 3, 0, 32767);
        w.width = static_cast<u32>(32768 - ((t * 12452) >> 15)) << 16;
        w.saw = w.pulse = 29491;
        w.sine = 0;
        w.mode = saw_pulse;
    }
    return w;
}

// The PolyBLEP correction (1 - x)^2, Q15, x the distance to the reset in increments.
static inline i32 blep(u32 distance, u32 reciprocal) {
    const i32 x = static_cast<i32>((distance * reciprocal) >> 17);
    const i32 e = 32768 - x;
    return (e * e) >> 15;
}
// A band-limited saw, Q15.
static inline i32 saw(u32 phase, u32 step, u32 reciprocal) {
    i32 s = static_cast<i32>(phase >> 16) - 32768;
    if (__builtin_expect(phase < step, 0)) s += blep(phase, reciprocal);
    else if (__builtin_expect(phase > 0u - step, 0)) s -= blep(0u - phase, reciprocal);
    return s;
}

struct Filter { i32 a1, g; };   // 1 / (1 + g (g + k)), and g / 4, Q31
struct Layer {
    u32 phase, step, reciprocal;
    u32 ring_phase, ring_step;
    i32 ring_dry, ring_wet;     // Q14: (1 - ring) and 1.4 ring
    u32 noise;
    i32 breath;                 // the noise's level, Q15
    i32 ic1, ic2;
};

// A layer over a block: oscillator, breath or ring, low-pass, into out (Q26).
template <Mode M, bool Noise, bool Ring>
__attribute__((noinline)) static void layer(Layer& l, const Wave& w, const Filter& f, i32* __restrict out) {
    u32 phase = l.phase, noise = l.noise, ring_phase = l.ring_phase;
    const u32 step = l.step, reciprocal = l.reciprocal, ring_step = l.ring_step, width = w.width;
    const i32 ws = w.saw, wp = w.pulse, wn = w.sine, dry = l.ring_dry, wet = l.ring_wet, breath = l.breath;
    const i32 a1 = f.a1, g = f.g;
    i32 ic1 = l.ic1, ic2 = l.ic2;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        phase += step;
        i32 o;
        if (M == sine_only) o = sine(phase);
        else if (M == sine_saw) o = (product(saw(phase, step, reciprocal), ws) + product(sine(phase), wn)) >> 15;
        else if (M == saw_only) o = saw(phase, step, reciprocal);
        else o = (product(saw(phase, step, reciprocal), ws) - product(saw(phase + width, step, reciprocal), wp)) >> 15;
        if (Noise) {
            noise = noise * 1664525u + 1013904223u;
            o += product(static_cast<i32>(noise) >> 16, breath) >> 15;
        }
        if (Ring) {
            ring_phase += ring_step;
            o = (o * (dry + (product(sine(ring_phase), wet) >> 15))) >> 14;
        }
        // into the filter at half level, Q26
        const i32 x = (Ring ? clamp(o, -65534, 65534) : o) * 1024;
        // 12 dB state-variable low-pass (trapezoidal), in three multiplies:
        // v1 = a1 (ic1 + g v3), v2 = ic2 + g v1
        const i32 v3 = x - ic2;
        const i32 v1 = multiply(a1, ic1 + multiply(g, v3) * 4);
        const i32 v2 = ic2 + multiply(g, v1) * 4;
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;
        out[i] = v2;
    }
    l.phase = phase;
    l.noise = noise;
    l.ring_phase = ring_phase;
    l.ic1 = ic1;
    l.ic2 = ic2;
}

template <bool Noise, bool Ring>
static inline void run_layer(Layer& l, const Wave& w, const Filter& f, i32* out) {
    switch (w.mode) {
    case sine_only: layer<sine_only, Noise, Ring>(l, w, f, out); break;
    case sine_saw: layer<sine_saw, Noise, Ring>(l, w, f, out); break;
    case saw_only: layer<saw_only, Noise, Ring>(l, w, f, out); break;
    default: layer<saw_pulse, Noise, Ring>(l, w, f, out); break;
    }
}

// Balance off centre and the low cut, on a layer's block in place: the level
// (Q10, 1024 is unity), then a one-pole high-pass. Skipped at balance 50, lowcut 0.
template <bool Level, bool Cut>
__attribute__((noinline)) static void trim(i32* __restrict out, i32 level, i32 cut, i32* state) {
    const i32 gain = level << 20;                                      // Q31, half the level
    i32 low = *state;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        i32 x = out[i];
        if (Level) x = multiply(x, gain) * 2;
        if (Cut) {
            low += multiply(cut, x - low);
            x -= low;
        }
        out[i] = x;
    }
    *state = low;
}
static inline void trim_layer(i32* out, i32 level, i32 cut, i32* state) {
    if (level != 1024) {
        if (cut) trim<true, true>(out, level, cut, state);
        else trim<true, false>(out, level, cut, state);
    } else if (cut) {
        trim<false, true>(out, level, cut, state);
    }
}

// The low-pass's coefficients for a cutoff (Q16 octaves above 20 Hz) and k = 2 - 1.94 reso (Q28).
static inline Filter filter_of(i32 octaves, i32 k) {
    const i32 g = at_quarter(tables::tangent, octaves);                // Q28
    const i32 d = (1 << 25) + multiply(g, g + k);                      // 1 + g (g + k), Q25
    u32 n = 0xffffffffu / static_cast<u32>(d >> 9);                    // 1 / d, Q16
    if (n > 65535u) n = 65535u;
    Filter f;
    f.a1 = static_cast<i32>(n << 15);                                  // Q31
    f.g = g * 2;                                                       // g / 4, Q31
    return f;
}

// ------------------------------------------------------------------ the stereo stage
struct Stereo { i32 coefficient[2], dry[2], wet[2]; };

// Mono: both sides the same, the layers summed.
__attribute__((noinline)) static void mono(const i32* __restrict a, const i32* __restrict b, opxy::Output out) {
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        const i32 y = clamp(multiply(a[i] + b[i], output_gain), -ceiling, ceiling) * 8;
        out[2 * i] = y;
        out[2 * i + 1] = y;
    }
}

// Spread alone, with the chorus at 0: L = mono + side, R = mono - side.
__attribute__((noinline)) static void spread(const i32* __restrict a, const i32* __restrict b, opxy::Output out,
                                             i32 half_spread) {
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        const i32 ai = a[i], bi = b[i];
        const i32 side = multiply(ai - bi, half_spread), sum = ai + bi;
        out[2 * i] = clamp(multiply(sum + side, output_gain), -ceiling, ceiling) * 8;
        out[2 * i + 1] = clamp(multiply(sum - side, output_gain), -ceiling, ceiling) * 8;
    }
}

// Spread and the CS-80's chorus on one side: L = mono + side, R = mono - side
// (Sign 1 and -1), then two first-order allpasses, mixed with the dry side and
// swayed. A side at a time, so its state stays in registers.
template <int Sign>
__attribute__((noinline)) static void chorus_side(const i32* __restrict a, const i32* __restrict b, i32* __restrict out,
                                                  i32 half_spread, i32 coefficient, i32 dry, i32 wet, i32* pass) {
    i32 x0 = pass[0], y1 = pass[1], y2 = pass[2];
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        const i32 ai = a[i], bi = b[i];
        const i32 side = multiply(ai - bi, half_spread);
        const i32 x = Sign > 0 ? ai + bi + side : ai + bi - side;
        const i32 s1 = multiply(coefficient, x - y1) + x0;
        const i32 s2 = multiply(coefficient, s1 - y2) + y1;
        x0 = x; y1 = s1; y2 = s2;
        out[2 * i] = clamp(multiply(x, dry) + multiply(s2, wet), -ceiling, ceiling) * 8;
    }
    pass[0] = x0; pass[1] = y1; pass[2] = y2;
}

// The chorus's settings this block, for depth d (Q15) and the shared clock's sine.
static inline Stereo stereo_of(i32 d, i32 sway) {
    Stereo st;
    const i32 swing = (sway * d) >> 15;                                // lfo x depth, Q15
    const i32 shift = (swing * 3277) >> 10;                            // 1.6 octaves x swing, Q16
    for (unsigned c = 0; c < 2; ++c) {
        const i32 sign = c ? -1 : 1;
        st.coefficient[c] = at_quarter(tables::allpass, chorus_centre + sign * shift);
        // the level sways between the sides: 1 +- 0.35 lfo x depth
        const i32 tremolo = 32768 + sign * ((swing * 11469) >> 15);  // Q15
        const i32 dry = ((32768 - (d >> 1)) * tremolo) >> 15;          // (1 - d/2) x tremolo, Q15
        const i32 wet = ((d >> 1) * tremolo) >> 15;                    // d/2 x tremolo, Q15
        st.dry[c] = multiply(output_gain, clamp(dry, 0, 32767) << 16);
        st.wet[c] = multiply(output_gain, wet << 16);
    }
    return st;
}
}

// Callbacks run serially per voice; owner is a stable nonzero voice ID.
inline void initialize(Voice& voice, Shared& shared, u32 owner) {
    u32 seed = owner * 0x9e3779b9u + 0x7f4a7c15u;
    voice.random = engine::next(seed) | 1u;
    voice.noise = engine::next(voice.random) | 1u;
    for (unsigned k = 0; k < 3; ++k) voice.phase[k] = engine::next(voice.random);   // free-running, starting apart
    for (unsigned k = 0; k < 4; ++k) voice.low[k] = 0;
    for (unsigned k = 0; k < 6; ++k) voice.pass[k] = 0;
    voice.cut[0] = voice.cut[1] = 0;
    voice.blocks = 0;
    voice.slide = 0;
    voice.vibrato = voice.vibrato_rate = 0;
    voice.vibrato_delay = voice.rise_extra = 0;
    voice.wander_phase = voice.wander_rate = 0;
    voice.tone_phase = engine::next(voice.random);
    voice.drift_from = voice.drift_to = 0;
    voice.cycle = shared.cycle - 1;   // not yet counted in this cycle
}

inline void start(Voice& voice, Shared&, u32) {
    // each note blooms, wobbles and drifts a little differently
    const u32 r = engine::next(voice.random);
    voice.blocks = 0;
    voice.slide = 32767;
    voice.vibrato = 0;
    voice.vibrato_rate = 34905130u + (r >> 16) * 57u;              // 5.6 Hz + 0.6 Hz x r
    voice.vibrato_delay = 379 + static_cast<i32>(((r & 0xffffu) * 172u) >> 16);  // 0.55 s + 0.25 s x r
    const u32 q = engine::next(voice.random);
    voice.rise_extra = static_cast<i32>(((q >> 16) * 55u) >> 16);              // + 0.08 s x r
    voice.wander_rate = 810297u + (((q & 0xffffu) * 19478u) >> 12);         // 0.13 Hz + 0.05 Hz x r
    voice.wander_phase = engine::next(voice.random);
    voice.drift_from = voice.drift_to;
}

inline void destroy(Voice&, Shared&, u32) {}

inline void render(Voice& voice, Shared& shared, u32, opxy::Output output,
                   opxy::Parameters p, opxy::Pitch pitch) {
    using namespace engine;
    // The census: the first voice to render twice in a cycle moves the chorus clock on.
    if (voice.cycle == shared.cycle) {
        shared.phase += chorus_rate;
        shared.sway = sine(shared.phase);
        ++shared.cycle;
    }
    voice.cycle = shared.cycle;
    const i32 t = static_cast<i32>(voice.blocks < (1u << 20) ? voice.blocks : (1u << 20));   // up to 25 minutes
    ++voice.blocks;

    const i32 shape = q15(p[0]), detune = q15(p[1]), bright = q15(p[2]), reso = q15(p[3]);
    const i32 depth = q15(p[4]), ring = q15(p[5]), touch = q15(p[6]), ribbon = q15(p[7]);
    const i32 tc = touch * 2;                                          // Q16
    // The Shift row: balance, octave (16', 8', 4', 2'), lowcut, noise; spread, ratio, drift, slide.
    const i32 balance = step_of(p[8]), lowcut = step_of(p[10]);
    const i32 footage = (static_cast<i32>(choice_of(p[9], 4)) - 1) * 65536;   // layer II, Q16 octaves from 8'
    const i32 breath = (q15(p[11]) * breath_full) >> 15;
    // Half the spread, Q31: about 0.5 at 100, and at 40 exactly what chorus 45 spread before
    // the row (0.889 x chorus), so Tyrell is unchanged.
    const i32 spread_step = step_of(p[12]);
    const i32 half_spread = spread_step * 10738046 + (spread_step >> 2);
    const u32 ratio = choice_of(p[13], 8);
    const i32 drift = q15(p[14]);

    // ---- pitch, Q16 octaves: the ribbon's slide, touch's late vibrato, drift
    i32 common = ((ribbon - 16384) * 4 * (voice.slide >> 3)) >> 12;   // +-1 octave x slide
    voice.slide = (voice.slide * tables::slide[step_of(p[15])]) >> 15;
    voice.vibrato += voice.vibrato_rate;
    const i32 arrival = clamp((t - voice.vibrato_delay) * 106, 0, 65536);   // over 0.9 s
    const i32 vibrato_depth = ((((arrival >> 1) * touch) >> 14) * 710) >> 16;        // 0.13 semitones at full
    common += (vibrato_depth * sine(voice.vibrato)) >> 15;
    const i32 reach = (drift * drift_reach) >> 15;
    if ((t & 511) == 0) {                                              // a new drift target every 0.74 s
        voice.drift_from = voice.drift_to;
        voice.drift_to = static_cast<i32>((next(voice.random) >> 16) * static_cast<u32>(2 * reach) >> 16) - reach;
    }
    if (voice.drift_from | voice.drift_to) {
        const i32 along_drift = smooth((t & 511) << 6);                // Q15
        common += voice.drift_from + (((voice.drift_to - voice.drift_from) * along_drift) >> 15);
    }
    i32 tone = 0;                                                      // the cutoff's wobble, Q16 octaves
    if (drift) {
        voice.wander_phase += voice.wander_rate;
        common += (((drift * drift_sine) >> 15) * sine(voice.wander_phase)) >> 15;
        voice.tone_phase += 560964u;                                   // 0.09 Hz
        tone = (((drift * tone_wander) >> 15) * sine(voice.tone_phase)) >> 15;
    }
    const i32 apart = along(tables::detune, detune);
    const u32 base = pitch[0];

    Layer one, two;
    one.step = transpose(base, common - apart);
    two.step = transpose(base, common + apart + footage);
    one.reciprocal = 0xffffffffu / (one.step | 1u);
    two.reciprocal = 0xffffffffu / (two.step | 1u);
    one.phase = voice.phase[0];
    two.phase = voice.phase[1];
    one.noise = voice.noise;
    two.noise = 0;
    one.breath = breath;
    two.breath = 0;
    one.ring_phase = one.ring_step = 0;
    one.ring_dry = one.ring_wet = 0;
    two.ring_phase = voice.phase[2];
    two.ring_step = ring ? transpose(base, common + footage + tables::ratio[ratio]) : 0;
    two.ring_dry = (32767 - ring) >> 1;                                // Q14
    two.ring_wet = (ring * 22938) >> 15;                               // 1.4 ring, Q14
    one.ic1 = voice.low[0]; one.ic2 = voice.low[1];
    two.ic1 = voice.low[2]; two.ic2 = voice.low[3];

    // ---- touch: the bloom. The cutoff swells from dark along a slow S to its
    // crescendo, then settles to a held brightness; resonance rises with it.
    i32 bloom = 65536, swell = 0;                                      // Q16 linear, Q15
    if (tc) {
        const i32 rise = 103 + ((tc * 655) >> 16) + voice.rise_extra;  // 0.15 + 0.95 touch (+ 0.08 r) s, in blocks
        const i32 span = (tc * 3150) / 1000;                           // crescendo less start: 3.15 touch, Q16
        if (t < rise) {
            const i32 s = smooth(static_cast<i32>(opxy::divide::quotient16(static_cast<u32>(t) << 15, static_cast<u32>(rise))));
            bloom = 65536 - ((touch * 55706) >> 15) + (((span >> 4) * s) >> 11);
            swell = s;
        } else {
            const i32 e = fall((t - rise) * 86);                       // e^-(t / 1.6 s)
            bloom = 65536 + (tc * 1725) / 1000 + ((((tc * 575) / 1000) * e) >> 15);
            swell = 26788 + ((e * 5980) >> 15);
        }
    }
    const i32 resonance = clamp(reso + ((((touch * 10486) >> 15) * swell) >> 15), 0, 31785);
    const i32 k = 536870912 - resonance * 15892;                       // 2 - 1.94 resonance, Q28
    const i32 key = ((log2_q16(base) - log2_q16(c4)) >> 4) * 29491 >> 12;   // 0.45 octave an octave
    const i32 cutoff = 2 * 65536 + ((bright * 15655) >> 10) + key + (log2_q16(static_cast<u32>(bloom)) - 16 * 65536)
                     + tone;
    const Filter f1 = filter_of(cutoff, k), f2 = filter_of(cutoff + 3708, k);   // layer II 4% higher

    // ---- the layers
    const Wave w = wave_of(shape);
    i32 a[opxy::block_frames], b[opxy::block_frames];
    if (breath) run_layer<true, false>(one, w, f1, a);
    else run_layer<false, false>(one, w, f1, a);
    if (ring) run_layer<false, true>(two, w, f2, b);
    else run_layer<false, false>(two, w, f2, b);
    voice.phase[0] = one.phase;
    voice.phase[1] = two.phase;
    voice.phase[2] = two.ring_phase;
    voice.noise = one.noise;
    voice.low[0] = one.ic1; voice.low[1] = one.ic2;
    voice.low[2] = two.ic1; voice.low[3] = two.ic2;
    if (balance != 50 || lowcut) {
        const i32 cut = tables::lowcut[lowcut];
        trim_layer(a, tables::balance[balance], cut, &voice.cut[0]);
        trim_layer(b, tables::balance[100 - balance], cut, &voice.cut[1]);
    }
    if (!lowcut) voice.cut[0] = voice.cut[1] = 0;

    // ---- the stereo stage: mono at chorus 0 and spread 0
    if (depth) {
        const Stereo st = stereo_of(depth, shared.sway);
        chorus_side<1>(a, b, output, half_spread, st.coefficient[0], st.dry[0], st.wet[0], voice.pass);
        chorus_side<-1>(a, b, output + 1, half_spread, st.coefficient[1], st.dry[1], st.wet[1], voice.pass + 3);
    }
    else {
        if (half_spread) spread(a, b, output, half_spread);
        else mono(a, b, output);
        for (unsigned k2 = 0; k2 < 6; ++k2) voice.pass[k2] = 0;
    }
}
}
