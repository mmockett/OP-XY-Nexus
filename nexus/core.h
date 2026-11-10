#pragma once
#include <opxy/audio.hpp>

namespace nexus {
using opxy::i32;
using opxy::u32;

// One note: two layers, each an oscillator into its own resonant low-pass,
// then the CS-80's stereo stage.
struct Voice {
    u32 cycle;          // the chorus clock's block cycle this voice last rendered in
    u32 random;         // xorshift state for this voice's block-rate choices
    u32 noise;          // xorshift state for the breath in layer I
    u32 blocks;         // blocks since the note started
    u32 phase[3];       // layer I, layer II, the ring modulator's sine
    i32 low[4];         // the two low-passes' states: I's ic1, ic2, then II's
    i32 pass[6];        // the chorus's allpasses, left then right: last input, stage 1's and stage 2's last outputs
    i32 slide;          // the ribbon's slide still to go, Q15 (1 at the note's start)
    u32 vibrato;        // the vibrato's phase
    u32 vibrato_rate;   // its increment a block: about 5.6 to 6.2 Hz, per note
    i32 vibrato_delay;  // blocks before it arrives: about 0.55 to 0.8 s, per note
    i32 rise_extra;     // blocks added to the bloom's rise, per note (up to 0.08 s)
    u32 wander_phase;   // the slow pitch wobble's phase and increment, per note
    u32 wander_rate;
    u32 tone_phase;     // the cutoff's slow wobble
    i32 drift_from;     // the pitch drift's last and next targets, Q16 octaves
    i32 drift_to;
};

// One clock for every voice: the CS-80's chorus stage moves the whole chord as one.
struct Shared {
    u32 cycle;          // the block cycle being rendered
    u32 phase;          // the chorus's 0.9 Hz phase
    i32 sway;           // its sine this block, Q15
};
}
