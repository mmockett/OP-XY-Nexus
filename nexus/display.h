#pragma once
#include <opxy/page_header.hpp>
#include <opxy/synth_shift.hpp>
#include "city.h"

namespace nexus {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
// Text slots 0..15 are the two-page header's; 16 is the frame-rate readout.
using Frame = opxy::draw::Frame<0, 0, city::node_count, 17, 14>;

struct DisplayState {
    opxy::draw::NoteTracker notes;
    opxy::draw::PageHeader header;
    opxy::u32 fresh;          // 1 until the first frame after a reset has set every node
    opxy::u32 cursor;         // the lot the city's restyling resumes at
    opxy::i32 run;            // the lead car's way along its course, Q10 px
    opxy::i32 speed;          // its speed, Q10 px a second
    opxy::i32 clock;          // Q10 seconds flown: the bob, the weave, the searchlight and the mast lights
    opxy::i32 hold;           // Q10 seconds since the latest note started, while held
    opxy::u32 serial;         // that note's serial: which windows flare
    opxy::i32 lane[city::lane_count];   // each traffic lane's way along its loop, Q10 px
    opxy::u32 frames, acc, worst, fps, worst_shown;   // the frame-rate readout (display.cpp's show_fps)
    opxy::u32 form[city::lot_count];    // what each lot shows: its scale, neon, rings, sign and light
    opxy::u32 lit[city::lot_count];     // and its windows, two bits each: 0 dark, 1 lit, 2 flaring
};
}
