// Nexus's sound checks, compiled for the host and run by python sdk.py test --machine <folder>.
#include "../engine.cpp"
#include "presets.h"
#include <cstdio>

namespace {
using opxy::i32;
using opxy::u32;
const unsigned frames = opxy::block_frames;
const unsigned controls = 16;  // two pages of four and the Shift row under them
const i32 peak = 0x30000000;   // definition.json's outputPeakQ31

i32 word(double v) { return v <= 0 ? 0 : v >= 1 ? 0x7fffffff : static_cast<i32>(v * 2147483647.0); }
// A Shift row amount's word, as the OP-XY plays it: whole steps 0..100, step / 100 of full scale.
i32 amount(double v) {
    const int step = v <= 0 ? 0 : v >= 1 ? 100 : static_cast<int>(v * 100 + 0.5);
    return step == 100 ? 0x7fffffff : step * 21474836 + (step * 47 + 50) / 100;
}
// The sixteen words render receives: the eight controls, then the Shift row (octave
// and ratio are enums, read by choice; the rest are amounts).
void words(const double (&c)[16], i32 (&p)[16]) {
    for (unsigned k = 0; k < controls; ++k) p[k] = k < 8 || k == 9 || k == 13 ? word(c[k]) : amount(c[k]);
}
// 440 Hz x 2^(semitones / 12) as a phase increment, by repeated factors (no libm).
u32 increment(int semitones) {
    double f = 440.0;
    const double up = 1.0594630943592953;
    for (int k = 0; k < semitones; ++k) f *= up;
    for (int k = 0; k > semitones; --k) f /= up;
    return static_cast<u32>(f / 44100.0 * 4294967296.0);
}

double trace[2][64 * 700];   // the left side of the last two single notes, for comparing them

struct Result {
    double peak_left, peak_right, energy, difference, crossings, voice_peak;
    double early, late, early_slope, late_slope;   // the left side's energy and its first difference's, before and after `release_at`
    bool bounded, mono;
};

// One note (or a chord) of `blocks` blocks at the given controls (0..1). Released at
// block `release_at` when that is at least 0; `slot` keeps a single note's trace.
Result play(const double (&c)[16], const int* notes, int count, int blocks, int slot = -1, int release_at = -1) {
    static nexus::Voice voices[8];
    static nexus::Shared shared;
    i32 p[16];
    words(c, p);
    u32 pitch[8][frames];
    for (int v = 0; v < count; ++v) {
        for (unsigned i = 0; i < frames; ++i) pitch[v][i] = increment(notes[v]);
        nexus::initialize(voices[v], shared, static_cast<u32>(v + 1));
        nexus::start(voices[v], shared, static_cast<u32>(v + 1));
    }
    Result r = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, true, true};
    i32 out[2 * frames];
    double last = 0;
    const int split = release_at >= 0 ? release_at : blocks / 2;
    for (int b = 0; b < blocks; ++b) {
        if (b == release_at)
            for (int v = 0; v < count; ++v) nexus::release(voices[v], shared, static_cast<u32>(v + 1));
        double sum[2 * frames] = {};
        for (int v = 0; v < count; ++v) {
            nexus::render(voices[v], shared, static_cast<u32>(v + 1), out, p, pitch[v]);
            for (unsigned i = 0; i < 2 * frames; ++i) {
                r.bounded = r.bounded && out[i] >= -peak && out[i] <= peak;
                if (i % 2 == 0) r.mono = r.mono && out[i] == out[i + 1];
                const double y = out[i] / 2147483648.0;
                if (y > r.voice_peak) r.voice_peak = y;
                if (-y > r.voice_peak) r.voice_peak = -y;
                sum[i] += y;
            }
        }
        for (unsigned i = 0; i < frames; ++i) {
            const double l = sum[2 * i], rr = sum[2 * i + 1];
            if (l > r.peak_left) r.peak_left = l;
            if (-l > r.peak_left) r.peak_left = -l;
            if (rr > r.peak_right) r.peak_right = rr;
            if (-rr > r.peak_right) r.peak_right = -rr;
            r.energy += l * l + rr * rr;
            r.difference += (l - rr) * (l - rr);
            if (slot >= 0 && b < 700) trace[slot][b * frames + i] = l;
            if ((l > 0) != (last > 0)) r.crossings += 1;
            // after the first 100 blocks: the level and the brightness (first difference), either side of the split
            if (b >= 100) {
                const double slope = (l - last) * (l - last);
                if (b < split) { r.early += l * l; r.early_slope += slope; }
                else if (b >= split + 100) { r.late += l * l; r.late_slope += slope; }
            }
            last = l;
        }
    }
    return r;
}

// The fundamental's power at A4 in the last 0.1 s of the trace (a whole 44 cycles), by Goertzel.
double fundamental(const double* x, int length) {
    const double c = 1.99607132886337;     // 2 cos(2 pi 440 / 44100)
    double s1 = 0, s2 = 0;
    for (int i = length - 4410; i < length; ++i) {
        const double s = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
}
}

// The Shift row at its defaults: sine 0, octave 8', lowcut off, noise 6; balance 50, ratio 1.5, spread 40, drift 25.
#define ROW 0, .375, 0, .06, .5, .3125, .40, .25

int main() {
    //                         shape detune bright reso chorus ring swell bite | sine octave lowcut noise balance ratio spread drift
    const double tyrell[16] = {0, .22, .46, .28, .45, 0, .80, 0, ROW};
    const int chord[8] = {-21, -17, -14, -10, -9, -5, -2, 2};
    const int one[1] = {-9};
    const int a4[1] = {0};

    // Every preset sounds, stays under the declared peak, and an eight-note chord sits near Hexagon's levels.
    for (unsigned k = 0; k < preset_count; ++k) {
        const Result r = play(presets[k].c, chord, 8, 1400);
        std::printf("%s: chord of 8 peaks at %.2f / %.2f of full scale, a voice at %.3f\n", presets[k].name,
                    r.peak_left, r.peak_right, r.voice_peak);
        expect(r.energy > 0, "a preset is silent");
        expect(r.bounded, "a voice above the declared peak");
        expect(r.voice_peak < 0.375, "a preset's voice reaches the ceiling");
        expect(r.peak_left < 1.0 && r.peak_right < 1.0, "a chord clips");
        expect(r.peak_left > 0.2, "a chord is far quieter than the suite");
    }
    // The extremes stay bounded, the Shift row's with them.
    for (int corner = 0; corner < 65536; corner += 4369) {
        double c[16];
        for (unsigned k = 0; k < controls; ++k) c[k] = (corner >> k) & 1 ? 1.0 : 0.0;
        expect(play(c, chord, 8, 300).bounded, "a corner of the controls above the declared peak");
        expect(play(c, chord, 8, 300, -1, 150).bounded, "a released corner above the declared peak");
    }
    for (unsigned k = 0; k < controls; ++k) {
        double c[16];
        for (unsigned j = 0; j < controls; ++j) c[j] = 1.0;
        c[k] = 0;
        expect(play(c, chord, 8, 200).bounded, "every control up but one, above the declared peak");
    }
    // Mono at chorus 0 and spread 0: both sides are the same voice.
    double dry[16] = {0, .22, .46, .28, 0, 0, .80, 0, ROW};
    dry[14] = 0;
    expect(play(dry, one, 1, 400).mono, "chorus 0, spread 0 is not mono");
    expect(play(dry, chord, 8, 200).mono, "chorus 0, spread 0 is not mono in a chord");
    double busy[16] = {.5, .4, .5, .6, 0, .5, .8, .6, .4, .375, .3, .2, .3, .3125, 0, .5};
    expect(play(busy, chord, 8, 300, -1, 150).mono, "chorus 0, spread 0 is not mono with every stage on");
    const Result wide = play(tyrell, one, 1, 700);
    expect(!wide.mono && wide.difference > wide.energy * 0.002, "chorus makes no stereo");
    double spread[16] = {0, .22, .46, .28, 0, 0, .80, 0, ROW};
    const Result apart = play(spread, one, 1, 700);
    expect(!apart.mono && apart.difference > apart.energy * 0.002, "spread alone makes no stereo");

    // In tune: sine 100, the sine alone, at 440 Hz crosses zero 880 times a second.
    double pure[16] = {0, 0, 1, 0, 0, 0, 0, 0, ROW};
    pure[8] = 1;
    pure[14] = 0;
    pure[15] = 0;
    const Result tune = play(pure, a4, 1, 689);
    std::printf("sine at A4: %.0f zero crossings in a second\n", tune.crossings);
    expect(tune.crossings > 860 && tune.crossings < 900, "a sine at A4 is out of tune");

    // The sine joins in phase with the saw's fundamental: sine 50 raises the fundamental.
    double open[16] = {0, 0, 1, 0, 0, 0, 0, 0, 0, .375, 0, 0, .5, .3125, 0, 0};
    play(open, a4, 1, 300, 0);
    open[8] = .5;
    play(open, a4, 1, 300, 1);
    const double without = fundamental(trace[0], 300 * frames), with = fundamental(trace[1], 300 * frames);
    std::printf("the fundamental at sine 0 and 50: %.3g, %.3g\n", without, with);
    expect(with > 2 * without, "the sine cancels the saw's fundamental");

    // On release the filter closes towards the swell's dark start: the note darkens.
    // At swell 0 and bite 0 there is nothing to close towards, so nothing changes.
    const Result held = play(dry, one, 1, 800);
    const Result closed = play(dry, one, 1, 800, -1, 400);
    const double held_bright = held.late_slope / held.late, closed_bright = closed.late_slope / closed.late;
    std::printf("brightness after 0.6 s held and released: %.4g, %.4g\n", held_bright, closed_bright);
    expect(closed_bright < 0.7 * held_bright, "the release does not darken the note");
    double flat[16] = {0, .22, .46, .28, 0, 0, 0, 0, ROW};
    flat[14] = 0;
    const Result kept = play(flat, one, 1, 600, 0), let_go = play(flat, one, 1, 600, 1, 300);
    double moved_by = 0;
    for (unsigned i = 0; i < 600 * frames; ++i) moved_by += (trace[0][i] - trace[1][i]) * (trace[0][i] - trace[1][i]);
    expect(moved_by == 0 && kept.energy == let_go.energy, "the release changes a note at swell 0 and bite 0");

    // Bite starts the note brighter than without it, then falls to the swell's dark start.
    double bitten[16] = {0, .22, .46, .28, 0, 0, .80, 0, ROW};
    bitten[14] = 0;
    play(bitten, one, 1, 200, 0);
    bitten[7] = 1;
    play(bitten, one, 1, 200, 1);
    double bright_at[2][2] = {};   // [bite 0, 100][first 0.06 s, 0.15 to 0.29 s]: the first difference over the level
    for (int s = 0; s < 2; ++s) {
        for (int part = 0; part < 2; ++part) {
            double level = 0, slope = 0;
            const unsigned from = part ? 100 * frames : 1, to = part ? 200 * frames : 40 * frames;
            for (unsigned i = from; i < to; ++i) {
                level += trace[s][i] * trace[s][i];
                slope += (trace[s][i] - trace[s][i - 1]) * (trace[s][i] - trace[s][i - 1]);
            }
            bright_at[s][part] = slope / (level + 1e-30);
        }
    }
    std::printf("brightness at bite 0 and 100: first 0.06 s %.4g, %.4g; 0.15 to 0.29 s %.4g, %.4g\n", bright_at[0][0],
                bright_at[1][0], bright_at[0][1], bright_at[1][1]);
    expect(bright_at[1][0] > 2 * bright_at[0][0], "bite does not start the note brighter");
    expect(bright_at[1][1] < 1.5 * bright_at[0][1], "bite does not fall back to the swell");

    // Every control changes the sound between its ends, the Shift row's too; ratio with ring up,
    // noise with the filter open.
    for (unsigned k = 0; k < controls; ++k) {
        double lo[16], hi[16];
        for (unsigned j = 0; j < controls; ++j) lo[j] = hi[j] = dry[j];
        if (k == 13) lo[5] = hi[5] = .5;     // ratio with the ring on
        if (k == 11) lo[2] = hi[2] = 1;      // noise with the filter open
        lo[k] = 0;
        hi[k] = 1;
        const Result a = play(lo, one, 1, 500, 0), b = play(hi, one, 1, 500, 1);
        double gap = 0, size = 0;
        for (unsigned i = 0; i < 500 * frames; ++i) {
            gap += (trace[0][i] - trace[1][i]) * (trace[0][i] - trace[1][i]);
            size += trace[0][i] * trace[0][i] + trace[1][i] * trace[1][i];
        }
        const bool moved = gap > 0.01 * size || a.difference != b.difference;
        std::printf("control %u: energy %.3g -> %.3g, crossings %.0f -> %.0f, change %.3f\n", k, a.energy, b.energy,
                    a.crossings, b.crossings, gap / (size + 1e-12));
        expect(moved, "a control changes nothing");
    }
    if (!failures) std::puts("ok");
    return 1;   // diagnostic: fail on purpose, so CI prints the measurements
}
