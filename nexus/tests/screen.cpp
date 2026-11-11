// Nexus's screen checks, compiled for the host and run by python sdk.py test --machine <folder>:
// every frame valid and every outline within the device's drawing rules across every control's
// whole travel; nothing changing at rest; the cars and the traffic wrapping cleanly; and how much
// changes a frame, in nodes and in the points of their outlines (the device redraws those, at a
// cost by their points), while notes are held and while controls turn.
#include "../display.cpp"
#include "outlines.h"
#include <cstdio>
#include <cstring>

// The Shift row the display reads (the OP-XY fills it for the selected track's synth).
opxy::draw::ShiftRow row;
extern "C" const opxy::draw::ShiftRow* opxy_synth_shift(opxy::u32) { return &row; }

namespace {
using opxy::i32;
using opxy::u32;
using opxy::draw::unit;

// The budget: points of changed outlines a frame (display.cpp's).
const u32 most_points = nexus::screen::budget;

nexus::DisplayState state;
nexus::Frame frame, before;

opxy::draw::Input input_of(const i32 (&slots)[8], u32 held, u32 serial) {
    opxy::draw::Input in;
    std::memset(&in, 0, sizeof in);
    for (int k = 0; k < 8; ++k) in.slots[k] = slots[k];
    for (int k = 0; k < 4; ++k) { in.parameters[k] = slots[k]; in.shown[k] = static_cast<opxy::u8>(k); }
    in.elapsed = 33;                       // about 31 frames a second, the device's rate
    in.track = 1;
    in.note_serial = serial;
    in.held_notes = held;
    in.note_off_serial = serial;
    in.amp.attack = in.amp.decay = in.amp.sustain = in.amp.release = opxy::draw::envelope_unavailable;
    return in;
}

bool hidden(const opxy::draw::Node& node) {
    return node.x == opxy::draw::hidden_x * unit && node.y == opxy::draw::hidden_y * unit;
}

// What changed since the previous frame: nodes, the points of their outlines, and texts and
// header values. The frame-rate readout (text 16) is left out: shown, it changes once a second.
struct Change { u32 nodes, points, texts; };
Change changed() {
    Change c = {0, 0, 0};
    for (u32 k = 0; k < nexus::Frame::node_count; ++k)
        if (std::memcmp(&frame.nodes[k], &before.nodes[k], sizeof frame.nodes[k])) {
            ++c.nodes;
            c.points += static_cast<u32>(outlines[k].points);
        }
    for (u32 k = 0; k < nexus::screen::fps_text; ++k)
        if (std::memcmp(&frame.texts[k], &before.texts[k], sizeof frame.texts[k])) ++c.texts;
    if (std::memcmp(frame.header, before.header, sizeof frame.header)) ++c.texts;
    return c;
}
u32 anything(const Change& c) { return c.nodes + c.texts; }

u32 on_screen() {
    u32 n = 0;
    for (u32 k = 0; k < nexus::Frame::node_count; ++k)
        if (!hidden(frame.nodes[k])) ++n;
    return n;
}

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
}

// The device's drawing rules, for every node on screen at its scale in this frame
// (OP-XY-MNMD docs/SCREEN.md; the SDK's docs/display.md, "Drawing on the device").
void check_outlines() {
    for (u32 k = 0; k < nexus::Frame::node_count; ++k) {
        const opxy::draw::Node& node = frame.nodes[k];
        if (hidden(node)) continue;
        const Outline& o = outlines[k];
        expect(o.points >= 3 && o.points <= 64, "an outline with fewer than 3 or more than 64 points");
        expect(o.area != 0, "an outline with no area");
        expect(o.turn >= 25, "an outline turns within 2.5 degrees of straight back");
        expect((o.gap * node.scale >> 10) >= unit / 8, "neighbouring points closer than 1/8 px on screen");
        expect((o.closing * node.scale >> 10) >= unit / 4, "a closing edge under 1/4 px on screen");
        expect(node.paint != opxy::draw::fill_and_stroke || o.filled, "a fill on a node whose artwork has none");
    }
}

Change step(const i32 (&slots)[8], u32 held, u32 serial) {
    std::memcpy(&before, &frame, sizeof frame);
    const opxy::draw::Input in = input_of(slots, held, serial);
    nexus::display(state, in, frame);
    return changed();
}

void slots_of(const double (&setting)[8], i32 (&s)[8]) {
    for (int j = 0; j < 8; ++j) s[j] = setting[j] >= 1 ? 0x7fffffff : static_cast<i32>(setting[j] * 2147483647.0);
}

// A node's reach on screen, left and right edges, Q10 px.
i32 left_edge(const opxy::draw::Node& n, u32 k) { return n.x + (outlines[k].left * n.scale >> 10); }
i32 right_edge(const opxy::draw::Node& n, u32 k) { return n.x + (outlines[k].right * n.scale >> 10); }

// Between two frames, a moving node (a car's or the traffic's) either moved a little the way it
// flies, or came or went where it can't be seen: on screen in both, it moved less than `most` px
// in x, the right way; shown in only one, it's off screen in that one at the other's x, so it
// slipped in or out past an edge; and a jump of the loop happens off screen at both ends.
void check_travel(u32 k, bool rightward, i32 most) {
    const opxy::draw::Node &a = before.nodes[k], &b = frame.nodes[k];
    const i32 margin = 3 * unit;           // a bank of 1.5 degrees reaches a little past the box
    if (!hidden(a) && !hidden(b)) {
        const i32 d = rightward ? b.x - a.x : a.x - b.x;
        if (d >= 0 && d <= most * unit) return;
        expect((right_edge(a, k) <= margin || left_edge(a, k) >= 480 * unit - margin) &&
               (right_edge(b, k) <= margin || left_edge(b, k) >= 480 * unit - margin),
               "a moving part jumped on screen");
    } else if (hidden(a) && !hidden(b)) {
        expect(rightward ? right_edge(b, k) <= most * unit + margin : left_edge(b, k) >= (480 - most) * unit - margin,
               "a moving part appeared on screen");
    } else if (!hidden(a) && hidden(b)) {
        expect(rightward ? left_edge(a, k) >= (480 - most) * unit - margin : right_edge(a, k) <= most * unit + margin,
               "a moving part vanished on screen");
    }
}
// (The wingman also leaves where it is when detune goes to 0: `wing` leaves it out.)
void check_moving(bool wing = true) {
    for (u32 k = 0; k < nexus::city::lead_nodes + (wing ? nexus::city::wing_nodes : 0); ++k)
        check_travel(nexus::city::car_first + k, true, 6);
    for (u32 l = 0; l < nexus::city::lane_count; ++l)
        for (u32 n = 0; n < nexus::city::lanes[l].cars; ++n)
            check_travel(nexus::city::lanes[l].first + n, nexus::city::lanes[l].rightward != 0, 3);
}

// At rest, once settled, every lot shows what the controls ask of it.
bool settled(const i32 (&slots)[8]) {
    nexus::screen::Controls c;
    c.shape = nexus::screen::q16(slots[0]); c.detune = nexus::screen::q16(slots[1]);
    c.bright = nexus::screen::q16(slots[2]); c.reso = nexus::screen::q16(slots[3]);
    c.chorus = nexus::screen::q16(slots[4]); c.ring = nexus::screen::q16(slots[5]);
    c.touch = nexus::screen::q16(slots[6]); c.ribbon = nexus::screen::q16(slots[7]);
    for (u32 k = 0; k < nexus::city::lot_count; ++k) {
        const nexus::screen::Look want = nexus::screen::desired(k, c, false, 0, state.serial, state.clock);
        if (want.form != state.form[k] || want.lit != state.lit[k]) return false;
    }
    return true;
}
}

int main() {
    i32 slots[8];
    const double tyrell[8] = {.33, .22, .46, .28, .45, 0, .80, .5};
    slots_of(tyrell, slots);
    // The row at its defaults: balance 50, octave 8', lowcut off, noise 6; spread 40, ratio 1.5, drift 25, slide 30.
    const i32 defaults[8] = {0x40000000, 0x2ffffffe, 0, 0x07ae147b, 0x33333333, 0x27fffffd, 0x20000000, 0x26666666};
    std::memset(&row, 0, sizeof row);
    for (int k = 0; k < 8; ++k) row.values[k] = defaults[k];
    nexus::display_reset(state);
    std::memset(&frame, 0, sizeof frame);
    step(slots, 0, 0);
    expect(opxy::draw::valid(frame), "the first frame is invalid");
    check_outlines();
    std::printf("%u nodes, %u on screen at rest at the default\n", static_cast<unsigned>(nexus::Frame::node_count),
                static_cast<unsigned>(on_screen()));

    // Every control through every value, with the others at the default, at rest and held:
    // valid frames, outlines within the drawing rules, and each frame within the budget.
    u32 sweep_most = 0;
    for (int k = 0; k < 8; ++k) {
        for (int v = 0; v < 128; ++v) {
            i32 s[8];
            for (int j = 0; j < 8; ++j) s[j] = slots[j];
            s[k] = v == 127 ? 0x7fffffff : v << 24;
            for (int f = 0; f < 6; ++f) {
                const Change c = step(s, v & 1, 1 + v / 16);
                check_outlines();
                expect(c.points <= most_points, "a control's turn changes more points in a frame than the budget");
                if (c.points > sweep_most) sweep_most = c.points;
            }
            expect(opxy::draw::valid(frame), "a frame is invalid");
        }
    }
    std::printf("turning each control: at most %u points changed a frame\n", static_cast<unsigned>(sweep_most));

    // The Shift row: held on either page, every entry through every value, with valid captions.
    for (int line = 0; line < 2; ++line) {
        row.held = 1;
        row.line = static_cast<u32>(line);
        for (int k = 0; k < 8; ++k) {
            for (int v = 0; v < 128; ++v) {
                row.values[k] = v == 127 ? 0x7fffffff : v << 24;
                i32 s[8];
                for (int j = 0; j < 8; ++j) s[j] = slots[j];
                opxy::draw::Input in = input_of(s, v & 1, 1 + v / 16);
                if (line) for (int c = 0; c < 4; ++c) in.shown[c] = static_cast<opxy::u8>(4 + c);
                std::memcpy(&before, &frame, sizeof frame);
                nexus::display(state, in, frame);
                expect(opxy::draw::valid(frame), "a frame with Shift held is invalid");
                for (int c = 0; c < 4; ++c) expect(frame.header[c][0] != 0, "a Shift caption is empty");
            }
            row.values[k] = defaults[k];
        }
    }
    // Holding Shift at rest: the header changes rows, then nothing changes.
    for (int f = 0; f < 90; ++f) step(slots, 0, 8);
    u32 held_still = 0;
    for (int f = 0; f < 60; ++f) held_still += anything(step(slots, 0, 8));
    expect(held_still == 0, "the screen changes at rest with Shift held");
    row.held = 0;
    row.line = 0;

    // At rest nothing changes, once the cars have eased to a stop and the city has settled.
    for (int f = 0; f < 120; ++f) step(slots, 0, 9);
    u32 still = 0;
    for (int f = 0; f < 60; ++f) still += anything(step(slots, 0, 9));
    expect(still == 0, "the screen changes at rest");
    expect(settled(slots), "the city hasn't settled at rest");

    // While notes are held: the cars cross and come round, the traffic and the searchlight move,
    // the mast lights blink and windows flare. Measured at the default, at every control up, at
    // every control down, and at the most flaring (bright 0, touch 100), with notes changing.
    const double settings[4][8] = {
        {.33, .22, .46, .28, .45, 0, .80, .5},
        {1, 1, 1, 1, 1, 1, 1, 1},
        {0, 0, 0, 0, 0, 0, 0, 0},
        {.33, .22, 0, .28, .45, 1, 1, .5},
    };
    for (int k = 0; k < 4; ++k) {
        i32 s[8];
        slots_of(settings[k], s);
        for (int f = 0; f < 120; ++f) step(s, 0, 20 + k);
        u32 points = 0, most = 0, nodes = 0, most_nodes = 0, shown = 0, appeared = 0;
        const int length = 1800;           // about a minute: a new note every 2 s, held 1.6 s of it
        for (int f = 0; f < length; ++f) {
            const u32 serial = 30 + k * 100 + f / 60;
            const bool lead_was = !hidden(frame.nodes[nexus::city::car_first]);
            const Change c = step(s, f % 60 < 48 ? 2u : 0u, serial);
            if (!lead_was && !hidden(frame.nodes[nexus::city::car_first])) ++appeared;
            check_moving();
            check_outlines();
            points += c.points;
            nodes += c.nodes;
            if (c.points > most) most = c.points;
            if (c.nodes > most_nodes) most_nodes = c.nodes;
            if (on_screen() > shown) shown = on_screen();
            expect(opxy::draw::valid(frame), "a frame in flight is invalid");
        }
        std::printf("setting %d: %.1f points (%.1f nodes) changed a frame on average, at most %u points "
                    "(%u nodes); at most %u nodes on screen; the cars came round %u times\n", k,
                    points / double(length), nodes / double(length), static_cast<unsigned>(most),
                    static_cast<unsigned>(most_nodes), static_cast<unsigned>(shown), static_cast<unsigned>(appeared));
        expect(most <= most_points, "a frame in flight changes more points than the budget");
        expect(appeared >= 3, "the cars don't come round again");
    }

    // Turning controls while the cars fly: every frame within the budget, and once released the
    // city settles to the controls and nothing changes.
    {
        i32 s[8];
        slots_of(tyrell, s);
        u32 most = 0;
        for (int f = 0; f < 600; ++f) {
            const int k = (f / 75) & 7;                         // each control in turn, swept up and down
            const int v = f % 75 < 38 ? (f % 75) * 56 : (74 - f % 75) * 56;
            s[k] = v > 2047 ? 0x7fffffff : v << 20;
            const Change c = step(s, 2, 900 + f / 50);
            check_moving(false);
            check_outlines();
            if (c.points > most) most = c.points;
        }
        std::printf("turning controls in flight: at most %u points changed a frame\n", static_cast<unsigned>(most));
        expect(most <= most_points, "turning a control in flight changes more points than the budget");
        for (int f = 0; f < 120; ++f) step(s, 0, 950);
        u32 quiet = 0;
        for (int f = 0; f < 60; ++f) quiet += anything(step(s, 0, 950));
        expect(quiet == 0, "the screen changes after the cars stop");
        expect(settled(s), "the city hasn't settled after turning controls in flight");
    }

    // A long flight, notes held throughout: the cars cross many times, wrapping cleanly. (Detune
    // comes back from 0 on its first frame, and the wingman with it, wherever it is.)
    {
        step(slots, 1, 990);
        u32 appeared = 0, vanished = 0;
        for (int f = 0; f < 3000; ++f) {
            const bool was = !hidden(frame.nodes[nexus::city::car_first + nexus::city::lead_nodes]);
            step(slots, 1, 990);
            const bool is = !hidden(frame.nodes[nexus::city::car_first + nexus::city::lead_nodes]);
            appeared += !was && is;
            vanished += was && !is;
            check_moving();
        }
        expect(appeared >= 10 && vanished >= 10, "the wingman doesn't come and go");
    }

    // Reduced motion: the cars still cross, without bob, bank or easing; frames valid.
    for (int f = 0; f < 200; ++f) {
        std::memcpy(&before, &frame, sizeof frame);
        opxy::draw::Input in = input_of(slots, f < 150 ? 1u : 0u, 995);
        in.reduced_motion = 1;
        nexus::display(state, in, frame);
        expect(opxy::draw::valid(frame), "a reduced-motion frame is invalid");
        expect(changed().points <= most_points, "a reduced-motion frame changes more points than the budget");
    }

    // Released, everything eases to a stop and then nothing changes.
    for (int f = 0; f < 120; ++f) step(slots, 0, 500);
    still = 0;
    for (int f = 0; f < 60; ++f) still += anything(step(slots, 0, 500));
    expect(still == 0, "the screen changes after the cars stop");
    if (!failures) std::puts("ok");
    return failures ? 1 : 0;
}
