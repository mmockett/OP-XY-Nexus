#pragma once
#include <opxy/page_header.hpp>
#include "city.h"

namespace nexus {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<0, 0, city::node_count, 16, 12>;

struct DisplayState {
    opxy::draw::NoteTracker notes;
    opxy::draw::PageHeader header;
    opxy::u32 fresh;          // 1 until the first frame after a reset has set every node
    opxy::u32 page;           // the stretch of city on screen
    opxy::i32 wipe;           // the page turn's frame, 0..7, or -1
    opxy::i32 car;            // the car's way along the page, Q10 world px
    opxy::i32 speed;          // its speed, Q10 world px a second
    opxy::i32 clock;          // Q10 seconds flown, for the hover and the chorus's weave
    opxy::i32 hold;           // Q10 seconds since the latest note started, while held
    opxy::u32 serial;         // that note's serial: which windows flare
    opxy::u32 shown[city::lot_count];   // what each lot shows now (a packed state)
};
}
