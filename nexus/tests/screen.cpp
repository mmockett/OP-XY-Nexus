// Nexus's screen checks, compiled for the host and run by python sdk.py test --machine <folder>:
// every frame valid and every outline within the device's drawing rules across every control's
// whole travel; nothing changing at rest, Shift held or not; the train running its loop and
// coming and going only where it can't be seen; how much changes a frame, in nodes and in the
// points of their outlines (the device redraws those, at a cost by their points), while notes
// are held and while controls turn; and how much of the screen is filled, which the device
// pays for every frame.
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

// The budgets: points of changed outlines a frame, and nodes (display.cpp's; the header's eight
// bars may change besides), and the filled area, header included, at most 1.6 times the picture
// below the header (480 x 182 px).
const u32 most_points = nexus::screen::budget;
const double most_fill = 1.6 * 480 * 182;

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

// The filled area this frame, px^2: the still artwork's and the header's (make_panel.py measures
// them), and each filled node shown, at its scale (a node partly off screen counted whole).
double filled() {
    double area = nexus::city::static_fill + nexus::city::header_fill;
    for (u32 k = 8; k < nexus::Frame::node_count; ++k) {
        const opxy::draw::Node& node = frame.nodes[k];
        if (hidden(node) || !outlines[k].filled) continue;
        const double s = node.scale / 1024.0;
        area += outlines[k].area / 16.0 * s * s;
    }
    return area;
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

double most_filled = 0;
Change step(const i32 (&slots)[8], u32 held, u32 serial) {
    std::memcpy(&before, &frame, sizeof frame);
    const opxy::draw::Input in = input_of(slots, held, serial);
    nexus::display(state, in, frame);
    const double f = filled();
    if (f > most_filled) most_filled = f;
    return changed();
}

void slots_of(const double (&setting)[8], i32 (&s)[8]) {
    for (int j = 0; j < 8; ++j) s[j] = setting[j] >= 1 ? 0x7fffffff : static_cast<i32>(setting[j] * 2147483647.0);
}

// The train: between two frames each of its nodes either moved a little down its track (right
// by at most 6 px and down half that), or came or went at the ends of its run, where make_panel.py
// found it wholly hidden: shown at its start, or last shown at its end.
u32 train_appeared = 0;
void check_train() {
    for (u32 k = nexus::city::restyle_end; k < nexus::Frame::node_count; ++k) {
        if (nexus::city::kind[k] != nexus::city::train) continue;
        const opxy::draw::Node &a = before.nodes[k], &b = frame.nodes[k];
        const i32 home_x = nexus::city::home_x[k] * unit;
        if (!hidden(a) && !hidden(b)) {
            const i32 dx = b.x - a.x, dy = b.y - a.y;
            expect(dx >= 0 && dx <= 6 * unit && dy >= (dx >> 1) - 2 && dy <= (dx >> 1) + 2, "the train jumped on screen");
        } else if (hidden(a) && !hidden(b)) {
            expect(b.x - home_x <= (nexus::city::train_start + 6) * unit, "the train appeared on screen");
            if (k == nexus::city::restyle_end || nexus::city::kind[k - 1] != nexus::city::train) ++train_appeared;
        } else if (!hidden(a) && hidden(b)) {
            expect(a.x - home_x >= (nexus::city::train_end - 6) * unit, "the train vanished on screen");
        }
    }
}

// At rest, once settled, every node that restyles shows what the controls ask of it.
bool settled(const i32 (&slots)[8]) {
    nexus::screen::Controls c;
    c.shape = nexus::screen::q16(slots[0]); c.detune = nexus::screen::q16(slots[1]);
    c.bright = nexus::screen::q16(slots[2]); c.reso = nexus::screen::q16(slots[3]);
    c.chorus = nexus::screen::q16(slots[4]); c.ring = nexus::screen::q16(slots[5]);
    c.swell = nexus::screen::q16(slots[6]); c.bite = nexus::screen::q16(slots[7]);
    for (u32 k = 8; k < nexus::city::restyle_end; ++k)
        if (!nexus::screen::same(frame.nodes[k], nexus::screen::restyled(k, state, c, false))) return false;
    return true;
}

// Rest, then check that nothing changes for two seconds.
bool still(const i32 (&slots)[8], u32 serial) {
    for (int f = 0; f < 150; ++f) step(slots, 0, serial);
    u32 changes = 0;
    for (int f = 0; f < 60; ++f) changes += anything(step(slots, 0, serial));
    return changes == 0;
}
}

int main() {
    i32 slots[8];
    const double tyrell[8] = {0, .22, .46, .28, .45, 0, .80, 0};
    slots_of(tyrell, slots);
    // The row at Tyrell: sine 0, octave 8', lowcut off, noise 6; balance 50, ratio 1.5, spread 40, drift 25.
    const i32 defaults[8] = {0, 0x2ffffffe, 0, 0x07ae147b, 0x40000000, 0x27fffffd, 0x33333333, 0x20000000};
    std::memset(&row, 0, sizeof row);
    for (int k = 0; k < 8; ++k) row.values[k] = defaults[k];
    nexus::display_reset(state);
    std::memset(&frame, 0, sizeof frame);
    step(slots, 0, 0);
    expect(opxy::draw::valid(frame), "the first frame is invalid");
    check_outlines();
    expect(state.fresh == 0, "the first frame didn't set every node");
    expect(settled(slots), "the first frame doesn't show the controls");
    std::printf("%u nodes; filled at rest at Tyrell: %.0f px^2, %.2f of the picture (header included)\n",
                static_cast<unsigned>(nexus::Frame::node_count), filled(), filled() / (480.0 * 182));

    // Every control through every value, with the others at the default, at rest and held:
    // valid frames, outlines within the drawing rules, and each frame within the budgets.
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
                expect(c.nodes <= nexus::screen::most_nodes + 8, "a control's turn changes more nodes in a frame than the budget");
                if (c.points > sweep_most) sweep_most = c.points;
            }
            expect(opxy::draw::valid(frame), "a frame is invalid");
        }
    }
    std::printf("turning each control: at most %u points changed a frame\n", static_cast<unsigned>(sweep_most));

    // Each control at 0 and at 100 shows: settled, the screen differs from the default.
    {
        nexus::Frame base;
        expect(still(slots, 7), "the screen changes at rest at the default");
        std::memcpy(&base, &frame, sizeof frame);
        const char* names[8] = {"shape", "detune", "bright", "reso", "chorus", "ring", "swell", "bite"};
        for (int k = 0; k < 8; ++k) {
            if (k == 4 || k == 6) continue;    // chorus and swell show only while a note is held
            for (int end = 0; end < 2; ++end) {
                i32 s[8];
                for (int j = 0; j < 8; ++j) s[j] = slots[j];
                s[k] = end ? 0x7fffffff : 0;
                if (s[k] == slots[k]) continue;   // shape, ring and bite are 0 at Tyrell
                expect(still(s, 7), "the screen changes at rest");
                expect(settled(s), "the city hasn't settled at rest");
                if (!std::memcmp(frame.nodes, base.nodes, sizeof frame.nodes)) {
                    std::printf("FAIL %s at %d doesn't change the screen\n", names[k], end ? 100 : 0);
                    ++failures;
                }
            }
        }
        expect(still(slots, 7), "the screen changes at rest at the default");
    }

    // The header names each control by its definition.json label and shows its caption: page 1,
    // page 2 and the Shift row under each page. Swell and bite are amounts, 0..100.
    {
        const char* const page_titles[2][4] = {{"shape", "detune", "bright", "reso"}, {"chorus", "ring", "swell", "bite"}};
        const char* const row_titles[2][4] = {{"sine", "octave", "lowcut", "noise"}, {"balance", "ratio", "spread", "drift"}};
        i32 s[8];
        for (int j = 0; j < 8; ++j) s[j] = slots[j];
        s[6] = 0x7fffffff;      // swell 100
        s[7] = 0x40000000;      // bite 50
        for (int page = 0; page < 2; ++page) {
            for (int held = 0; held < 2; ++held) {
                row.held = static_cast<u32>(held);
                row.line = static_cast<u32>(page);
                opxy::draw::Input in = input_of(s, 0, 7);
                for (int c = 0; c < 4; ++c) in.shown[c] = static_cast<opxy::u8>(4 * page + c);
                in.reduced_motion = 1;     // the header snaps to the page
                nexus::display(state, in, frame);
                for (int c = 0; c < 4; ++c) {
                    const char* want = held ? row_titles[page][c] : page_titles[page][c];
                    if (std::strcmp(frame.texts[c].chars, want)) {
                        std::printf("FAIL page %d%s column %d is titled %s, not %s\n", page + 1, held ? " with Shift" : "",
                                    c, frame.texts[c].chars, want);
                        ++failures;
                    }
                }
                if (page == 1 && !held) {
                    expect(!std::strcmp(frame.header[2], "100"), "swell's caption isn't its amount");
                    expect(!std::strcmp(frame.header[3], "50"), "bite's caption isn't its amount");
                }
                if (held) {
                    // Tyrell's row: sine 0, octave 8', lowcut off, noise 6; balance 50, ratio 1.5, spread 40, drift 25
                    const char* const shown[2][4] = {{"0", "8'", "off", "6"}, {"50", "1.5", "40", "25"}};
                    for (int c = 0; c < 4; ++c)
                        if (std::strcmp(frame.header[c], shown[page][c])) {
                            std::printf("FAIL the Shift row's %s shows %s, not %s\n", row_titles[page][c], frame.header[c],
                                        shown[page][c]);
                            ++failures;
                        }
                }
            }
        }
        row.held = 0;
        row.line = 0;
        step(slots, 0, 7);
    }

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
    expect(still(slots, 8), "the screen changes at rest with Shift held");
    row.held = 0;
    row.line = 0;

    // At rest nothing changes, once the train has eased to a stop and the city has settled.
    expect(still(slots, 9), "the screen changes at rest");
    expect(settled(slots), "the city hasn't settled at rest");

    // While notes are held: the train runs and comes round, the fans turn, the dish swings, the
    // craft bobs, the lights blink and windows flare. Measured at the default, at every control
    // up, at every control down, and at the most flaring (bright 0, swell 100), notes changing.
    const double settings[4][8] = {
        {0, .22, .46, .28, .45, 0, .80, 0},
        {1, 1, 1, 1, 1, 1, 1, 1},
        {0, 0, 0, 0, 0, 0, 0, 0},
        {0, .22, 0, .28, .45, 1, 1, 0},
    };
    for (int k = 0; k < 4; ++k) {
        i32 s[8];
        slots_of(settings[k], s);
        for (int f = 0; f < 150; ++f) step(s, 0, 20 + k);
        u32 points = 0, most = 0, nodes = 0, most_nodes = 0;
        train_appeared = 0;
        const int length = 2400;           // about 80 s: a new note every 2 s, held 1.6 s of it
        for (int f = 0; f < length; ++f) {
            const u32 serial = 30 + k * 100 + f / 60;
            const Change c = step(s, f % 60 < 48 ? 2u : 0u, serial);
            check_train();
            check_outlines();
            points += c.points;
            nodes += c.nodes;
            if (c.points > most) most = c.points;
            if (c.nodes > most_nodes) most_nodes = c.nodes;
            expect(opxy::draw::valid(frame), "a frame in flight is invalid");
        }
        std::printf("setting %d: %.1f points (%.1f nodes) changed a frame on average, at most %u points "
                    "(%u nodes); the train came round %u times\n", k, points / double(length), nodes / double(length),
                    static_cast<unsigned>(most), static_cast<unsigned>(most_nodes), static_cast<unsigned>(train_appeared));
        expect(most <= most_points, "a frame in flight changes more points than the budget");
        expect(most_nodes <= nexus::screen::most_nodes + 8, "a frame in flight changes more nodes than the budget");
        expect(train_appeared >= 1, "the train doesn't come round again");
    }

    // Turning controls while the train runs: every frame within the budget, and once released the
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
            check_train();
            check_outlines();
            if (c.points > most) most = c.points;
        }
        std::printf("turning controls while the train runs: at most %u points changed a frame\n", static_cast<unsigned>(most));
        expect(most <= most_points, "turning a control while the train runs changes more points than the budget");
        expect(still(s, 950), "the screen changes after the train stops");
        expect(settled(s), "the city hasn't settled after turning controls while the train ran");
    }

    // A long run, notes held throughout, at the slowest and the fastest: the train loops cleanly.
    for (int fast = 0; fast < 2; ++fast) {
        i32 s[8];
        for (int j = 0; j < 8; ++j) s[j] = slots[j];
        s[4] = fast ? 0x7fffffff : 0;
        train_appeared = 0;
        for (int f = 0; f < 6000; ++f) {
            step(s, 1, 990);
            check_train();
        }
        std::printf("held for %d s at chorus %d: the train came round %u times\n", 6000 * 33 / 1000, fast ? 100 : 0,
                    static_cast<unsigned>(train_appeared));
        expect(train_appeared >= (fast ? 10u : 2u), "the train doesn't keep coming round");
    }

    // Reduced motion: the train still runs, without easing; frames valid.
    for (int f = 0; f < 200; ++f) {
        std::memcpy(&before, &frame, sizeof frame);
        opxy::draw::Input in = input_of(slots, f < 150 ? 1u : 0u, 995);
        in.reduced_motion = 1;
        nexus::display(state, in, frame);
        expect(opxy::draw::valid(frame), "a reduced-motion frame is invalid");
        expect(changed().points <= most_points, "a reduced-motion frame changes more points than the budget");
    }

    // Released, everything eases to a stop and then nothing changes.
    expect(still(slots, 500), "the screen changes after the train stops");
    std::printf("filled at most %.0f px^2, %.2f of the picture (header included)\n", most_filled, most_filled / (480.0 * 182));
    expect(most_filled <= most_fill, "more of the screen is filled than the budget");
    if (!failures) std::puts("ok");
    return failures ? 1 : 0;
}
