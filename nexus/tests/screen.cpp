// Nexus's screen checks, compiled for the host and run by python sdk.py test --machine <folder>:
// every frame valid and every outline within the device's drawing rules across every control's
// whole travel, the reshaping outlines (the fans' blades, the advert's lines) at every pose;
// nothing changing at rest, Shift held or not; the train and the road's cars running their loops
// and coming and going only where they can't be seen; the fans turning a little every frame while
// notes are held; bright darkening the sky; sine setting the advert's lines with Shift held or
// not; how much changes a frame, in nodes and in the points of their outlines (the device redraws
// those, at a cost by their points), while notes are held and while controls turn; and how much
// of the screen is filled, which the device pays for every frame.
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
namespace city = nexus::city;

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
    for (u32 k = 0; k < nexus::Frame::node_count; ++k) {
        const bool reshaped = k < city::paths && frame.paths[k].key != before.paths[k].key;
        if (reshaped || std::memcmp(&frame.nodes[k], &before.nodes[k], sizeof frame.nodes[k])) {
            ++c.nodes;
            c.points += static_cast<u32>(outlines[k].points);
        }
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
    for (u32 k = city::first; k < nexus::Frame::node_count; ++k) {
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
// A reshaping outline as the frame has it, against the same rules: points 1/8 px apart, a closing edge
// of 1/4 px, no turn within 2.5 degrees of straight back, some area, within 64 px of its node.
void check_path(u32 p) {
    const opxy::draw::Point* q = frame.paths[p].points;
    const u32 n = city::path_points;
    double area = 0;
    for (u32 j = 0; j < n; ++j) {
        const opxy::draw::Point &a = q[(j + n - 1) % n], &b = q[j], &c = q[(j + 1) % n];
        const double ux = a.x - b.x, uy = a.y - b.y, vx = c.x - b.x, vy = c.y - b.y;
        const double gap2 = vx * vx + vy * vy, back2 = ux * ux + uy * uy;
        expect(gap2 >= (unit / 8.0) * (unit / 8.0), "a reshaping outline's points closer than 1/8 px");
        // the turn at b: within 2.5 degrees of straight back when the two edges nearly coincide
        const double dot = ux * vx + uy * vy, cross = ux * vy - uy * vx;
        expect(!(dot > 0 && cross * cross < 0.0019 * dot * dot), "a reshaping outline turns straight back");
        expect(b.x >= -64 * unit && b.x <= 64 * unit && b.y >= -64 * unit && b.y <= 64 * unit,
               "a reshaping outline's point past 64 px from its node");
        area += static_cast<double>(b.x) * c.y - static_cast<double>(c.x) * b.y;
        (void)back2;
    }
    const double cx = q[0].x - q[n - 1].x, cy = q[0].y - q[n - 1].y;
    expect(cx * cx + cy * cy >= (unit / 4.0) * (unit / 4.0), "a reshaping outline's closing edge under 1/4 px");
    expect(area != 0, "a reshaping outline with no area");
    expect(frame.paths[p].key != 0, "a reshaping outline without a key");
}

void check_outlines() {
    for (u32 p = 0; p < city::paths; ++p) check_path(p);
    for (u32 k = city::paths; k < nexus::Frame::node_count; ++k) {
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

// The road's cars: between two frames each either moved a little along the road (at most a pixel
// across, half that up or down, the way it drives), or came or went at the ends of its run, where
// make_panel.py found it wholly hidden.
u32 cars_appeared = 0;
void check_cars() {
    for (u32 k = city::restyle_end; k < nexus::Frame::node_count; ++k) {
        if (city::kind[k] != city::car) continue;
        const u32 p = city::param[k];
        const opxy::draw::Node &a = before.nodes[k], &b = frame.nodes[k];
        const i32 home_x = city::home_x[k] * unit;
        if (!hidden(a) && !hidden(b)) {
            const i32 dx = b.x - a.x, dy = b.y - a.y;
            expect(dx * city::car_way[p] >= 0 && (dx < 0 ? -dx : dx) <= unit && dy >= -(dx >> 1) - 2 &&
                       dy <= -(dx >> 1) + 2, "a road car jumped");
        } else if (hidden(a) && !hidden(b)) {
            const i32 at = b.x - home_x;
            expect(city::car_way[p] > 0 ? at <= (city::car_start[p] + 2) * unit : at >= (city::car_end[p] - 2) * unit,
                   "a road car appeared on screen");
            ++cars_appeared;
        } else if (!hidden(a) && hidden(b)) {
            const i32 at = a.x - home_x;
            expect(city::car_way[p] > 0 ? at >= (city::car_end[p] - 2) * unit : at <= (city::car_start[p] + 2) * unit,
                   "a road car vanished on screen");
        }
    }
}

// The fans: while notes are held and the pace is up, every frame turns each fan's blades a little,
// smoothly (at most 12 degrees a frame); released, they ease to a stop.
u32 fan_turns = 0, fan_frames = 0;
void check_fans() {
    for (u32 f = 0; f < city::fans; ++f) {
        const u32 a = (before.paths[f].key - 1) & 0xffffu, b = (frame.paths[f].key - 1) & 0xffffu;
        const u32 step = (b - a) & 0xffffu;
        expect(step <= 2185u, "a fan's blades jumped");
        if (step) ++fan_turns;
    }
    ++fan_frames;
}

// At rest, once settled, every node that restyles shows what the controls ask of it.
bool settled(const i32 (&slots)[8]) {
    nexus::screen::Controls c;
    c.shape = nexus::screen::q16(slots[0]); c.detune = nexus::screen::q16(slots[1]);
    c.bright = nexus::screen::q16(slots[2]); c.reso = nexus::screen::q16(slots[3]);
    c.chorus = nexus::screen::q16(slots[4]); c.ring = nexus::screen::q16(slots[5]);
    c.swell = nexus::screen::q16(slots[6]); c.bite = nexus::screen::q16(slots[7]);
    c.sine = nexus::screen::q16(row.values[0]);
    for (u32 k = city::first; k < nexus::city::restyle_end; ++k)
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
    for (u32 p = 0; p < city::paths; ++p) expect(frame.paths[p].key != 0, "the first frame didn't set every outline");
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
    const double settings[6][8] = {
        {0, .22, .46, .28, .45, 0, .80, 0},
        {1, 1, 1, 1, 1, 1, 1, 1},
        {0, 0, 0, 0, 0, 0, 0, 0},
        {0, .22, 0, .28, .45, 1, 1, 0},
        {0, .22, 1, .28, 1, 1, .8, .6},      // night, the craft up and bobbing, every ring
        {.5, .5, .5, .5, .5, .5, .5, .5},
    };
    u32 flight_points = 0, flight_nodes = 0;
    for (int k = 0; k < 6; ++k) {
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
            check_cars();
            check_fans();
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
        if (most > flight_points) flight_points = most;
        if (most_nodes > flight_nodes) flight_nodes = most_nodes;
    }
    std::printf("in flight: at most %u points and %u nodes changed a frame (the header's bars included)\n",
                static_cast<unsigned>(flight_points), static_cast<unsigned>(flight_nodes));

    // Turning controls while the train runs: every frame within the budget, and once released the
    // city settles to the controls and nothing changes.
    {
        i32 s[8];
        slots_of(tyrell, s);
        u32 most = 0, most_turning_nodes = 0;
        for (int f = 0; f < 675; ++f) {
            const int k = (f / 75) % 9;                         // each control in turn, and sine, swept up and down
            const int v = f % 75 < 38 ? (f % 75) * 56 : (74 - f % 75) * 56;
            if (k < 8) s[k] = v > 2047 ? 0x7fffffff : v << 20;
            else row.values[0] = v > 2047 ? 0x7fffffff : v << 20;
            const Change c = step(s, 2, 900 + f / 50);
            check_train();
            check_cars();
            check_outlines();
            if (c.points > most) most = c.points;
            if (c.nodes > most_turning_nodes) most_turning_nodes = c.nodes;
        }
        row.values[0] = defaults[0];
        std::printf("turning controls and sine while the train runs: at most %u points and %u nodes changed a frame\n",
                    static_cast<unsigned>(most), static_cast<unsigned>(most_turning_nodes));
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
        cars_appeared = 0;
        fan_turns = fan_frames = 0;
        for (int f = 0; f < 6000; ++f) {
            step(s, 1, 990);
            check_train();
            check_cars();
            check_fans();
        }
        std::printf("held for %d s at chorus %d: the train came round %u times, the road's cars %u times; "
                    "the fans turned in %u of %u fan-frames\n", 6000 * 33 / 1000, fast ? 100 : 0,
                    static_cast<unsigned>(train_appeared), static_cast<unsigned>(cars_appeared),
                    static_cast<unsigned>(fan_turns), static_cast<unsigned>(fan_frames * city::fans));
        expect(train_appeared >= (fast ? 10u : 2u), "the train doesn't keep coming round");
        expect(cars_appeared >= 4 * city::cars, "the road's cars don't keep coming back");
        // eased up within a second, the fans turn every frame after: smooth, not stepped
        expect(fan_turns + 31 * city::fans >= fan_frames * city::fans, "the fans don't turn every frame while held");
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

    // Bright darkens the sky and the clouds, from a light grey at 0 to near black at 100, never
    // brightening as it rises; at every setting the lit windows stand out from the sky.
    {
        i32 s[8];
        slots_of(tyrell, s);
        u32 sky = 0, cloud = 0;
        for (u32 k = city::first; k < city::restyle_end; ++k)
            if (city::kind[k] == city::sky) (city::low[k] == 12 ? sky : cloud) = k;
        expect(sky && cloud, "no sky or no clouds");
        u32 last = 99;
        for (int v = 0; v < 128; ++v) {
            s[2] = v == 127 ? 0x7fffffff : v << 24;
            expect(still(s, 1000), "the screen changes at rest while bright is set");
            expect(settled(s), "the sky hasn't settled");
            const u32 shade = frame.nodes[sky].shade;
            expect(shade <= last, "the sky brightens as bright rises");
            expect(frame.nodes[cloud].shade >= shade && frame.nodes[cloud].shade <= shade + 2, "the clouds part from the sky");
            expect(shade + 2 <= 14, "the lit windows don't stand out from the sky");
            for (u32 k = city::first; k < city::restyle_end; ++k)
                if (city::kind[k] == city::sky && city::low[k] == 12) expect(frame.nodes[k].shade == shade, "the sky in two greys");
            last = shade;
            if (v == 0) expect(shade == 12, "the sky at bright 0 isn't light grey");
            if (v == 127) expect(shade <= 1, "the sky at bright 100 isn't near black");
        }
        std::printf("bright 0 to 100: the sky from grey 12 to grey %u\n", static_cast<unsigned>(last));
        slots_of(tyrell, s);
        expect(still(s, 1001), "the screen changes at rest");
    }

    // Sine sets the advert's lines, Shift held or not: longer as it rises, and still at rest.
    for (int held = 0; held < 2; ++held) {
        row.held = static_cast<u32>(held);
        i32 last = 0;
        for (int v = 0; v <= 100; ++v) {
            row.values[0] = v == 100 ? 0x7fffffff : v * 21474836 + (v * 47 + 50) / 100;
            expect(still(slots, 1100), "the screen changes at rest while sine is set");
            const i32 end = frame.paths[city::lines].points[1].x;
            expect(v == 0 || end > last, "the advert's lines don't lengthen with sine");
            expect(((frame.paths[city::lines].key - 1) & 0xffffu) == static_cast<u32>(v), "the lines aren't at sine's step");
            last = end;
        }
        std::printf("sine 0 to 100%s: the advert's first line ends %.1f px further right\n", held ? " (Shift held)" : "",
                    (last - (frame.paths[city::lines].points[0].x + city::line_length[0][0])) / 1024.0);
    }
    row.held = 0;
    row.values[0] = defaults[0];

    // The fans: held, they ease up and then turn every frame; released, they ease to a stop.
    {
        fan_turns = fan_frames = 0;
        for (int f = 0; f < 31; ++f) step(slots, 1, 1200);
        for (int f = 0; f < 300; ++f) {
            step(slots, 1, 1200);
            check_fans();
        }
        expect(fan_turns == fan_frames * city::fans, "the fans miss a frame while held");
        expect(still(slots, 1200), "the fans don't stop once released");
    }

    // Released, everything eases to a stop and then nothing changes.
    expect(still(slots, 500), "the screen changes after the train stops");
    std::printf("filled at most %.0f px^2, %.2f of the picture (header included)\n", most_filled, most_filled / (480.0 * 182));
    expect(most_filled <= most_fill, "more of the screen is filled than the budget");
    if (!failures) std::puts("ok");
    return failures ? 1 : 0;
}
