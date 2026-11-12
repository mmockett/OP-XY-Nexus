#pragma once
#include <opxy/page_header.hpp>
#include <opxy/synth_shift.hpp>
#include "city.h"

namespace nexus {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
// Text slots 0..15 are the two-page header's; 16 is the frame-rate readout.
// Nodes 0..3 are reshaping outlines (the fans' blades and the advert's lines), of 9 points each. Every
// other node is a path but the ball's three, which are circles.
using Frame = opxy::draw::Frame<city::paths, city::path_points, city::node_count, 17, 16>;

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
    opxy::u32 envelope;       // 1 while the track's amp envelope is known: the windows glow with it
    opxy::i32 glow;           // the windows' glow, Q16, through the envelope
    opxy::u32 stage;          // its stage: 0 out, 1 attack, 2 decay and sustain, 3 release
    opxy::u32 glow_serial;    // the note that started it: which windows glow
    opxy::u32 frames, acc, worst, fps, worst_shown;   // the frame-rate readout (display.cpp's show_fps)
    opxy::i32 pace;           // the fans' and the road cars' pace, Q10: 1 while notes are held, easing to 0
    opxy::u32 spin[city::fans];   // each fan's turn, Q16: balance sets the two on the right's speeds
    opxy::u32 vane;           // the duct fans' turn, Q16: which pose of their blades shows
    opxy::i32 road[city::cars];   // each road car's way along its loop, Q10 px
    opxy::u32 traffic;        // the road cars shown, a bit each: spread's, taken up as each is out of sight
    opxy::i32 ball;           // the ball on its spire, Q10 px down from its rest: easing to octave's step
    opxy::i32 dish;           // where the dish points, tenths of a degree: easing to ratio's step
};
}
