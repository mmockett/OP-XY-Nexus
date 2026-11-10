// Nexus's sound checks, compiled for the host and run by python sdk.py test --machine <folder>.
#include "../engine.cpp"
#include <cstdio>

namespace {
using opxy::i32;
using opxy::u32;
const unsigned frames = opxy::block_frames;
const i32 peak = 0x30000000;   // definition.json's outputPeakQ31

i32 word(double v) { return v <= 0 ? 0 : v >= 1 ? 0x7fffffff : static_cast<i32>(v * 2147483647.0); }
// 440 Hz x 2^(semitones / 12) as a phase increment, by repeated factors (no libm).
u32 increment(int semitones) {
    double f = 440.0;
    const double up = 1.0594630943592953;
    for (int k = 0; k < semitones; ++k) f *= up;
    for (int k = 0; k > semitones; --k) f /= up;
    return static_cast<u32>(f / 44100.0 * 4294967296.0);
}

double trace[2][64 * 700];   // the left side of the last two single notes, for comparing them

struct Result { double peak_left, peak_right, energy, difference, crossings; bool bounded, mono; };

// One note (or a chord) of `blocks` blocks at the given controls (0..1).
Result play(const double (&c)[8], const int* notes, int count, int blocks, int slot = -1) {
    static nexus::Voice voices[8];
    static nexus::Shared shared;
    i32 p[8];
    for (int k = 0; k < 8; ++k) p[k] = word(c[k]);
    u32 pitch[8][frames];
    for (int v = 0; v < count; ++v) {
        for (unsigned i = 0; i < frames; ++i) pitch[v][i] = increment(notes[v]);
        nexus::initialize(voices[v], shared, static_cast<u32>(v + 1));
        nexus::start(voices[v], shared, static_cast<u32>(v + 1));
    }
    Result r = {0, 0, 0, 0, 0, true, true};
    i32 out[2 * frames];
    double last = 0;
    for (int b = 0; b < blocks; ++b) {
        double sum[2 * frames] = {};
        for (int v = 0; v < count; ++v) {
            nexus::render(voices[v], shared, static_cast<u32>(v + 1), out, p, pitch[v]);
            for (unsigned i = 0; i < 2 * frames; ++i) {
                r.bounded = r.bounded && out[i] >= -peak && out[i] <= peak;
                if (i % 2 == 0) r.mono = r.mono && out[i] == out[i + 1];
                sum[i] += out[i] / 2147483648.0;
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
            last = l;
        }
    }
    return r;
}

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
}
}

int main() {
    //                    shape detune bright reso chorus ring touch ribbon
    const double tyrell[8] = {.33, .22, .46, .28, .45, 0, .80, .5};
    const double presets[6][8] = {
        {.33, .22, .46, .28, .45, 0, .80, .5},       // Tyrell
        {.34, .60, .42, .10, .40, 0, .35, .5},       // Long Strings
        {.10, .30, .62, .30, .30, .25, .20, .5},     // Glass Choir
        {0, .20, .70, .20, 0, .70, 0, .5},           // Toy Bells
        {.66, .20, .30, .30, 0, 0, .40, .5},         // Sixteen Feet
        {.34, .15, .58, .30, .20, 0, .50, .79},      // Rise Lead
    };
    const int chord[8] = {-21, -17, -14, -10, -9, -5, -2, 2};
    const int one[1] = {-9};

    // Every preset sounds, stays under the declared peak, and an eight-note chord sits near Hexagon's levels.
    for (int k = 0; k < 6; ++k) {
        const Result r = play(presets[k], chord, 8, 1400);
        std::printf("preset %d: chord of 8 peaks at %.2f / %.2f of full scale\n", k, r.peak_left, r.peak_right);
        expect(r.energy > 0, "a preset is silent");
        expect(r.bounded, "a voice above the declared peak");
        expect(r.peak_left < 1.0 && r.peak_right < 1.0, "a chord clips");
        expect(r.peak_left > 0.2, "a chord is far quieter than the suite");
    }
    // The extremes stay bounded.
    for (int corner = 0; corner < 256; corner += 17) {
        double c[8];
        for (int k = 0; k < 8; ++k) c[k] = (corner >> k) & 1 ? 1.0 : 0.0;
        expect(play(c, chord, 8, 300).bounded, "a corner of the controls above the declared peak");
    }
    // Mono at chorus 0: both sides are the same voice.
    double dry[8] = {.33, .22, .46, .28, 0, 0, .80, .5};
    expect(play(dry, one, 1, 400).mono, "chorus 0 is not mono");
    expect(play(dry, chord, 8, 200).mono, "chorus 0 is not mono in a chord");
    const Result wide = play(tyrell, one, 1, 700);
    expect(!wide.mono && wide.difference > wide.energy * 0.002, "chorus makes no stereo");

    // In tune: a sine at 440 Hz crosses zero 880 times a second.
    const double sine[8] = {0, 0, 1, 0, 0, 0, 0, .5};
    const int a4[1] = {0};
    const Result tune = play(sine, a4, 1, 689);
    std::printf("sine at A4: %.0f zero crossings in a second\n", tune.crossings);
    expect(tune.crossings > 860 && tune.crossings < 900, "a sine at A4 is out of tune");

    // Every control changes the sound between 0 and 100.
    const Result base = play(dry, one, 1, 500);
    for (int k = 0; k < 8; ++k) {
        double lo[8], hi[8];
        for (int j = 0; j < 8; ++j) lo[j] = hi[j] = dry[j];
        lo[k] = 0;
        hi[k] = 1;
        const Result a = play(lo, one, 1, 500, 0), b = play(hi, one, 1, 500, 1);
        double apart = 0, size = 0;
        for (unsigned i = 0; i < 500 * frames; ++i) {
            apart += (trace[0][i] - trace[1][i]) * (trace[0][i] - trace[1][i]);
            size += trace[0][i] * trace[0][i] + trace[1][i] * trace[1][i];
        }
        const bool moved = apart > 0.01 * size || a.difference != b.difference;
        std::printf("control %d: energy %.3g -> %.3g, crossings %.0f -> %.0f, change %.3f\n", k, a.energy, b.energy,
                    a.crossings, b.crossings, apart / (size + 1e-12));
        expect(moved, "a control changes nothing");
    }
    (void)base;
    if (!failures) std::puts("ok");
    return failures ? 1 : 0;
}
