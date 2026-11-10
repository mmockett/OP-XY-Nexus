// Temporary: the voice before the Shift row (tests/old, main at 51c60b7) against the voice now,
// preset by preset, with the row set as each preset sets it. Tyrell and Sixteen Feet must match
// to the bit; the others print how far apart they are.
#define nexus nexus_old
#include "old/engine.cpp"
#undef nexus
#include "../engine.cpp"
#include <cstdio>

namespace {
using opxy::i32;
using opxy::u32;
const unsigned frames = opxy::block_frames;
i32 word(double v) { return v <= 0 ? 0 : v >= 1 ? 0x7fffffff : static_cast<i32>(v * 2147483647.0); }
i32 amount(double v) {
    const int step = v <= 0 ? 0 : v >= 1 ? 100 : static_cast<int>(v * 100 + 0.5);
    return step == 100 ? 0x7fffffff : step * 21474836 + (step * 47 + 50) / 100;
}
u32 increment(int semitones) {
    double f = 440.0;
    const double up = 1.0594630943592953;
    for (int k = 0; k < semitones; ++k) f *= up;
    for (int k = 0; k > semitones; --k) f /= up;
    return static_cast<u32>(f / 44100.0 * 4294967296.0);
}
struct Gap { long differing, total; double largest, peak; };
Gap compare(const double (&c)[16], const int* notes, int count, int blocks) {
    static nexus_old::Voice old_voices[8];
    static nexus_old::Shared old_shared;
    static nexus::Voice voices[8];
    static nexus::Shared shared;
    i32 p_old[8], p[16];
    for (int k = 0; k < 16; ++k) p[k] = k < 8 || k == 9 || k == 13 ? word(c[k]) : amount(c[k]);
    for (int k = 0; k < 8; ++k) p_old[k] = p[k];
    u32 pitch[8][frames];
    old_shared.cycle = old_shared.phase = 0; old_shared.sway = 0;
    shared.cycle = shared.phase = 0; shared.sway = 0;
    for (int v = 0; v < count; ++v) {
        for (unsigned i = 0; i < frames; ++i) pitch[v][i] = increment(notes[v]);
        nexus_old::initialize(old_voices[v], old_shared, static_cast<u32>(v + 1));
        nexus_old::start(old_voices[v], old_shared, static_cast<u32>(v + 1));
        nexus::initialize(voices[v], shared, static_cast<u32>(v + 1));
        nexus::start(voices[v], shared, static_cast<u32>(v + 1));
    }
    Gap g = {0, 0, 0, 0};
    i32 a[2 * frames], b[2 * frames];
    for (int k = 0; k < blocks; ++k)
        for (int v = 0; v < count; ++v) {
            nexus_old::render(old_voices[v], old_shared, static_cast<u32>(v + 1), a, p_old, pitch[v]);
            nexus::render(voices[v], shared, static_cast<u32>(v + 1), b, p, pitch[v]);
            for (unsigned i = 0; i < 2 * frames; ++i) {
                const double d = (static_cast<double>(a[i]) - b[i]) / 2147483648.0, x = a[i] / 2147483648.0;
                g.differing += a[i] != b[i];
                ++g.total;
                if (d > g.largest) g.largest = d;
                if (-d > g.largest) g.largest = -d;
                if (x > g.peak) g.peak = x;
                if (-x > g.peak) g.peak = -x;
            }
        }
    return g;
}
}

int main() {
    const char* const names[7] = {"Tyrell", "Long Strings", "Glass Choir", "Toy Bells", "Sixteen Feet", "Rise Lead", "Tyrell, chorus 0, spread 0"};
    const double presets[7][16] = {
        {.33, .22, .46, .28, .45, 0, .80, .5, .5, .375, 0, .06, .40, .3125, .25, .30},
        {.34, .60, .42, .10, .40, 0, .35, .5, .5, .375, 0, .06, .36, .3125, .25, .30},
        {.10, .30, .62, .30, .30, .25, .20, .5, .5, .375, 0, .06, .27, .3125, .25, .30},
        {0, .20, .70, .20, 0, .70, 0, .5, .5, .375, 0, .06, 0, .8125, .25, .30},
        {.66, .20, .30, .30, 0, 0, .40, .5, .5, .375, 0, .06, 0, .3125, .25, .30},
        {.34, .15, .58, .30, .20, 0, .50, .79, .5, .375, 0, .06, .18, .3125, .25, .30},
        {.33, .22, .46, .28, 0, 0, .80, .5, .5, .375, 0, .06, 0, .3125, .25, .30},
    };
    const bool exact[7] = {true, false, false, false, true, false, true};
    const int chord[8] = {-21, -17, -14, -10, -9, -5, -2, 2};
    int failures = 0;
    for (int k = 0; k < 7; ++k) {
        const Gap g = compare(presets[k], chord, 8, 2100);
        std::printf("%-28s %ld of %ld samples differ, by at most %.3g (peak %.3g, %.1f dB below it)\n", names[k],
                    g.differing, g.total, g.largest, g.peak, g.largest > 0 ? 20 * __builtin_log10(g.peak / g.largest) : 999.0);
        if (exact[k] && g.differing) { std::printf("FAIL %s is not bit-exact\n", names[k]); ++failures; }
    }
    if (!failures) std::puts("ok");
    return failures ? 1 : 0;
}
