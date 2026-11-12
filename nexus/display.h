#pragma once
#include <opxy/page_header.hpp>
#include <opxy/synth_shift.hpp>
#include "city.h"

namespace nexus {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
// Text slots 0..15 are the two-page header's; 16 is the frame-rate readout.
using Frame = opxy::draw::Frame<0, 0, city::node_count, 17, 16>;

struct DisplayState {
    opxy::draw::NoteTracker notes;
    opxy::draw::PageHeader header;
    opxy::u32 fresh;          // 1 until the first frame after a reset has set every node
    opxy::u32 cursor;         // the node the restyling resumes at
    opxy::i32 run;            // the train's way along its loop, Q10 px along x
    opxy::i32 speed;          // its speed, Q10 px a second
    opxy::i32 clock;          // Q10 seconds run: the lights, fans, dish, craft and the pattern's pulse
    opxy::i32 hold;           // Q10 seconds since the latest note started, while held
    opxy::u32 serial;         // that note's serial: which windows flare
    opxy::u32 frames, acc, worst, fps, worst_shown;   // the frame-rate readout (display.cpp's show_fps)
};
}
