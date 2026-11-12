// Nexus's screen checks, compiled for the host and run by python sdk.py test --machine <folder>:
// every frame valid and every outline within the device's drawing rules across every control's
// whole travel, the reshaping outlines (the fans' blades, the advert's lines) at every pose;
// nothing changing at rest, Shift held or not; the train and the road's cars running their loops
// and coming and going only where they can't be seen; the fans turning a little every frame while
// notes are held; bright bringing in one fog sheet over the far city, from none at 0 to its
// fullest at 100 (12/32, what bright 34% showed before), and lighting the windows, which run down every face, in a scattered order; the windows' glow following the amp
// envelope through attack, decay, sustain and release, and stopping; sine setting the advert's lines
// with Shift held or not; the rest of the Shift row: octave sliding the ball along its spire (its rings with
// it), balance setting the right-hand fans' speeds, drift wobbling the craft, noise flickering the neon sign,
// spread bringing in the road's cars and ratio turning the dish, each only when it changes or while notes are
// held; the aircraft warning lights blinking slowly and the duct fans stepping through their poses while
// held, a pose at a time, about one a frame; the round parts as SVG ellipses and the ball as bound circles; how much changes a frame, in nodes and
// in the points of their outlines (the device redraws those, at a cost by their points), while notes are held
// and while controls turn; and how much of the screen is filled, which the device pays for every frame.
#include "../display.cpp"
#include "outlines.h"
#include <cmath>
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
// bars may change besides), and the opaque filled area, header included, at most 1.52 times the picture
// below the header (480 x 182 px: metro-6 held 30 frames a second at full bright with 1.51, and the far
// city filling every gap since is paid for by the clouds taken out and by thin bands that now hide the
// faces under them); the fog at most 0.15 of it, and 2.0 times
// with the fog counted four times: the device blends each of its pixels with what is under it, which costs
// several opaque fills (metro-5's 0.24 of a picture of fog took the device from about 30 to about 20 frames
// a second). The still artwork's fill counts its circles whole: the device fills all of each.
const u32 most_points = nexus::screen::budget;
const double most_fill = 1.52 * 480 * 182;
const double fog_weight = 4;
const double most_fog_fill = 0.15 * 480 * 182;
const double most_weighted_fill = 2.0 * 480 * 182;     // and with the fog counted fog_weight times

// The track's amp envelope the display is given: unknown unless a check sets it.
opxy::draw::Envelope amp = {opxy::draw::envelope_unavailable, opxy::draw::envelope_unavailable,
                            opxy::draw::envelope_unavailable, opxy::draw::envelope_unavailable};

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
    in.amp = amp;
    return in;
}

i32 glow_floor_of() { return nexus::screen::glow_floor; }

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
// them), and each opaque filled node shown, at its scale (a node partly off screen counted whole); and the
// fog's, translucent, which the device blends at several times the cost of an opaque fill.
double fog_area = 0;
double filled() {
    double area = nexus::city::static_fill + nexus::city::header_fill;
    fog_area = 0;
    for (u32 k = city::first; k < nexus::Frame::node_count; ++k) {
        const opxy::draw::Node& node = frame.nodes[k];
        if (hidden(node) || !outlines[k].filled) continue;
        const double s = node.scale / 1024.0;
        (outlines[k].filled == 2 ? fog_area : area) += outlines[k].area / 16.0 * s * s;
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

double most_filled = 0, most_fog = 0, most_weighted = 0;
Change step(const i32 (&slots)[8], u32 held, u32 serial) {
    std::memcpy(&before, &frame, sizeof frame);
    const opxy::draw::Input in = input_of(slots, held, serial);
    nexus::display(state, in, frame);
    const double f = filled();
    if (f > most_filled) most_filled = f;
    if (fog_area > most_fog) most_fog = fog_area;
    if (f + fog_weight * fog_area > most_weighted) most_weighted = f + fog_weight * fog_area;
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

// The road's cars: between two frames each either moved a little along the road (a pixel at a time across,
// two when a busy frame made it wait, half that up or down, the way it drives), or came or went at the ends
// of its run, where make_panel.py found it wholly hidden.
u32 cars_appeared = 0;
void check_cars() {
    for (u32 k = city::restyle_end; k < nexus::Frame::node_count; ++k) {
        if (city::kind[k] != city::car) continue;
        const u32 p = city::param[k];
        const opxy::draw::Node &a = before.nodes[k], &b = frame.nodes[k];
        const i32 home_x = city::home_x[k] * unit;
        if (!hidden(a) && !hidden(b)) {
            const i32 dx = b.x - a.x, dy = b.y - a.y;
            expect(dx * city::car_way[p] >= 0 && (dx < 0 ? -dx : dx) <= 2 * unit && dy >= -(dx >> 1) - 2 &&
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
    c.sine = nexus::screen::q16(row.values[0]); c.octave = nexus::screen::q16(row.values[1]);
    c.lowcut = nexus::screen::q16(row.values[2]); c.noise = nexus::screen::q16(row.values[3]);
    c.balance = nexus::screen::q16(row.values[4]); c.ratio = nexus::screen::q16(row.values[5]);
    c.spread = nexus::screen::q16(row.values[6]); c.drift = nexus::screen::q16(row.values[7]);
    for (u32 k = city::first; k < nexus::city::restyle_end; ++k)
        if (!nexus::screen::same(frame.nodes[k], nexus::screen::restyled(k, state, c, false))) return false;
    return true;
}

// The first moving node of a kind (of a param, for those that read one), or 0.
u32 first_of(u32 kind, i32 param = -1) {
    for (u32 k = city::restyle_end; k < city::node_count; ++k)
        if (city::kind[k] == kind && (param < 0 || city::param[k] == static_cast<u32>(param))) return k;
    return 0;
}
// A row entry's value in the middle of step s of n.
i32 middle(u32 s, u32 n) { return static_cast<i32>((2u * s + 1u) * (0x80000000u / (2u * n))); }

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
    const double settings[7][8] = {
        {0, .22, .46, .28, .45, 0, .80, 0},
        {1, 1, 1, 1, 1, 1, 1, 1},
        {0, 0, 0, 0, 0, 0, 0, 0},
        {0, .22, 0, .28, .45, 1, 1, 0},
        {0, .22, 1, .28, 1, 1, .8, .6},      // night, the craft up and bobbing, every ring
        {.5, .5, .5, .5, .5, .5, .5, .5},
        {0, .22, .46, .28, .45, 1, .8, .6},  // with the Shift row at 100: every car, the sign flickering, the craft wobbling
    };
    // Each setting twice: the envelope unknown (the windows flare), then known, Tyrell's (they glow with it).
    u32 flight_points = 0, flight_nodes = 0;
    for (int k = 0; k < 14; ++k) {
        if (k == 7) { amp.attack = 12416 << 16; amp.decay = 32640 << 16; amp.sustain = 29490 << 16; amp.release = 21632 << 16; }
        for (int j = 0; j < 8; ++j) row.values[j] = k % 7 == 6 ? 0x7fffffff : defaults[j];
        i32 s[8];
        slots_of(settings[k % 7], s);
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
        std::printf("setting %d%s: %.1f points (%.1f nodes) changed a frame on average, at most %u points "
                    "(%u nodes); the train came round %u times\n", k % 7, k < 7 ? "" : " (the envelope known)",
                    points / double(length), nodes / double(length),
                    static_cast<unsigned>(most), static_cast<unsigned>(most_nodes), static_cast<unsigned>(train_appeared));
        expect(most <= most_points, "a frame in flight changes more points than the budget");
        expect(most_nodes <= nexus::screen::most_nodes + 8, "a frame in flight changes more nodes than the budget");
        expect(train_appeared >= 1, "the train doesn't come round again");
        if (most > flight_points) flight_points = most;
        if (most_nodes > flight_nodes) flight_nodes = most_nodes;
    }
    for (int j = 0; j < 8; ++j) row.values[j] = defaults[j];
    amp.attack = amp.decay = amp.sustain = amp.release = opxy::draw::envelope_unavailable;
    std::printf("in flight: at most %u points and %u nodes changed a frame (the header's bars included)\n",
                static_cast<unsigned>(flight_points), static_cast<unsigned>(flight_nodes));

    // Turning controls while the train runs: every frame within the budget, and once released the
    // city settles to the controls and nothing changes.
    {
        i32 s[8];
        slots_of(tyrell, s);
        u32 most = 0, most_turning_nodes = 0;
        for (int f = 0; f < 1200; ++f) {
            const int k = (f / 75) % 16;                        // each control in turn, and the Shift row's, swept up and down
            const int v = f % 75 < 38 ? (f % 75) * 56 : (74 - f % 75) * 56;
            if (k < 8) s[k] = v > 2047 ? 0x7fffffff : v << 20;
            else row.values[k - 8] = v > 2047 ? 0x7fffffff : v << 20;
            const Change c = step(s, 2, 900 + f / 50);
            check_train();
            check_cars();
            check_outlines();
            if (c.points > most) most = c.points;
            if (c.nodes > most_turning_nodes) most_turning_nodes = c.nodes;
        }
        for (int j = 0; j < 8; ++j) row.values[j] = defaults[j];
        std::printf("turning controls and the Shift row while the train runs: at most %u points and %u nodes changed a frame\n",
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

    // Bright brings in the fog: one plain sheet over the far city, hidden at 0 (the far city dark) and up to
    // the first donor's place (city.h's fog_from); as bright rises, painted from its donors (white at their
    // opacity, city.h's fog_alpha, 32nds), never thinning, through every donor in turn, and at its fullest
    // at 100: 12/32, the opacity bright 34% (488 Hz) showed when the fullest was 24/32.
    {
        i32 s[8];
        slots_of(tyrell, s);
        u32 fogs = 0, sheet = 0;
        for (u32 k = city::first; k < city::restyle_end; ++k)
            if (city::kind[k] == city::fog) { ++fogs; sheet = k; }
        std::printf("the fog: %u node, %d points, %.0f px^2 (%.3f of the picture)\n", static_cast<unsigned>(fogs),
                    outlines[sheet].points, outlines[sheet].area / 16.0, outlines[sheet].area / 16.0 / (480.0 * 182));
        expect(fogs == 1, "the fog isn't one node");
        expect(outlines[sheet].filled == 2, "the fog's artwork has no translucent fill");
        expect(outlines[sheet].area / 16.0 <= most_fog_fill, "the fog sheet is bigger than its budget");
        u32 last = 0, steps = 0;
        for (int v = 0; v < 128; ++v) {
            s[2] = v == 127 ? 0x7fffffff : v << 24;
            expect(still(s, 1000), "the screen changes at rest while bright is set");
            expect(settled(s), "the fog hasn't settled");
            const opxy::draw::Node& n = frame.nodes[sheet];
            if (v == 0) expect(hidden(n), "the fog shows at bright 0");
            if (nexus::screen::q16(s[2]) < city::fog_from[0]) {
                expect(hidden(n) && last == 0, "the fog shows before its first donor's place");
                continue;
            }
            const bool donor = n.shade >= city::fog_donor && n.shade < city::fog_donor + city::fog_donors;
            expect(!hidden(n) && donor && n.paint == opxy::draw::fill_and_stroke, "the fog isn't painted from its donors");
            if (!donor) continue;
            const u32 alpha = city::fog_alpha[n.shade - city::fog_donor];
            expect(alpha >= last, "the fog thins as bright rises");
            if (alpha > last) ++steps;
            last = alpha;
            if (v == 32 || v == 64 || v == 96 || v == 127)
                std::printf("bright %d: the fog at %u/32\n", v == 127 ? 100 : (v * 100 + 64) / 127, static_cast<unsigned>(alpha));
        }
        expect(steps == city::fog_donors, "the fog skips a donor as bright rises");
        expect(last == city::fog_alpha[city::fog_donors - 1], "the fog isn't at its fullest at bright 100");
        expect(last == 12, "the fog's fullest isn't 12/32");
        expect(city::fog_from[0] > 0, "the fog shows at bright 0");
        for (u32 d = 1; d < city::fog_donors; ++d)
            expect(city::fog_alpha[d] > city::fog_alpha[d - 1], "the fog's donors don't rise in opacity");
        // Bright jumped from 0 to 100 at rest: the windows, background lights and fog restyle over a few
        // frames, each within the budgets, and settle within a second.
        slots_of(tyrell, s);
        s[2] = 0;
        expect(still(s, 1002), "the screen changes at rest");
        s[2] = 0x7fffffff;
        int frames = 0;
        u32 most = 0, most_count = 0;
        while (!settled(s) && frames < 100) {
            const Change c = step(s, 0, 1002);
            if (c.points > most) most = c.points;
            if (c.nodes > most_count) most_count = c.nodes;
            ++frames;
        }
        std::printf("bright 0 to 100 at once: settled in %d frames (%d ms), at most %u points and %u nodes a frame\n",
                    frames, frames * 33, static_cast<unsigned>(most), static_cast<unsigned>(most_count));
        expect(frames <= 31, "bright takes more than a second to settle");
        expect(most <= most_points, "bright's restyle changes more points in a frame than the budget");
        slots_of(tyrell, s);
        expect(still(s, 1001), "the screen changes at rest");
    }

    // The windows run down every face as far as it is seen (make_panel.py measures it), and light a few at a
    // time: each node is a short segment of a column, at most 48 px tall, lit by its own rank (param), so
    // bright lights them in a scattered order, not a column at a time: few segments next to each other in
    // a column have ranks within 8 of each other.
    {
        u32 windows = 0, close = 0;
        i32 tallest = 0;
        for (u32 k = city::first; k < city::restyle_end; ++k) {
            if (city::kind[k] != city::window) continue;
            ++windows;
            const i32 tall = outlines[k].bottom - outlines[k].top;
            if (tall > tallest) tallest = tall;
            if (k + 1 < city::restyle_end && city::kind[k + 1] == city::window) {
                const i32 d = static_cast<i32>(city::param[k]) - static_cast<i32>(city::param[k + 1]);
                if (d < 8 && d > -8) ++close;
            }
        }
        std::printf("windows: %u shown, covering %.1f%% of each column's seen height, in %u nodes at most %.1f px "
                    "tall; %u next to each other light together\n", static_cast<unsigned>(city::windows_shown),
                    city::window_cover / 10.0, static_cast<unsigned>(windows), tallest / 1024.0,
                    static_cast<unsigned>(close));
        expect(city::window_cover >= 990, "the windows stop short of where their faces are seen");
        expect(city::windows_shown >= 250, "too few windows show");
        expect(tallest <= 48 * 1024, "a window node lights more than a few windows at once");
        expect(close * 10 <= windows, "the windows light a column at a time");
    }

    // The windows' glow follows the track's amp envelope: up over its attack, down toward its sustain over
    // its decay while the note is held, out over its release, each glowing window stepping through the
    // greys between; every frame within the budgets; once out, nothing changes. At Tyrell's, Afterglow's,
    // Blip Brass's and Toy Bells' envelopes (their presets' words), at bright 0 and swell 100.
    {
        struct Words { const char* name; i32 attack, decay, sustain, release; };
        const Words envelopes[4] = {{"Tyrell", 12416, 32640, 29490, 21632}, {"Afterglow", 18304, 32128, 29490, 8320},
                                    {"Blip Brass", 640, 29824, 27852, 30848}, {"Toy Bells", 128, 13184, 0, 22144}};
        const double frame_s = 33 / 1024.0;
        i32 s[8];
        slots_of(tyrell, s);
        s[2] = 0;
        s[6] = 0x7fffffff;
        for (int e = 0; e < 4; ++e) {
            const Words& w = envelopes[e];
            amp.attack = w.attack << 16;
            amp.decay = w.decay << 16;
            amp.sustain = w.sustain << 16;
            amp.release = w.release << 16;
            expect(still(s, 1300 + e), "the screen changes at rest with the envelope known");
            const u32 ai = static_cast<u32>(amp.attack) >> 24, di = static_cast<u32>(amp.decay) >> 24,
                      ri = static_cast<u32>(amp.release) >> 24;
            const double attack_s = 65536.0 / city::attack_rate[ai];
            const double tau_d = 65536.0 / city::fall_rate[di ? di : 1], tau_r = 65536.0 / city::fall_rate[127 - ri];
            const double sustain = (amp.sustain >> 15) / 65536.0;
            const int held = 150;
            int peak = -1, out = -1;
            i32 last = 0, at_release = 0;
            u32 was = 0;
            u32 most = 0, most_count = 0, glowing = 0, most_glow = 0;
            u32 seen[nexus::Frame::node_count];
            for (u32 k = 0; k < nexus::Frame::node_count; ++k) seen[k] = 0;
            for (int f = 0; f < held + 400; ++f) {
                const Change c = step(s, f < held ? 1u : 0u, 1400 + e);
                check_outlines();
                if (c.points > most) most = c.points;
                if (c.nodes > most_count) most_count = c.nodes;
                const i32 level = state.glow;
                if (f < held) {
                    if (state.stage == 1) expect(level >= last, "the glow falls during the attack");
                    if (state.stage == 2 && peak < 0) peak = f + 1;
                    if (state.stage == 2 && was == 2) expect(level <= last && level >= (amp.sustain >> 15),
                                                             "the glow doesn't decay toward the sustain");
                    at_release = level;
                } else {
                    expect(level <= last, "the glow rises after release");
                    if (level == 0 && out < 0) out = f - held + 1;
                }
                last = level;
                was = state.stage;
                u32 now = 0, recoloured = 0;
                for (u32 k = city::first; k < city::restyle_end; ++k)
                    if (std::memcmp(&frame.nodes[k], &before.nodes[k], sizeof frame.nodes[k])) ++recoloured;
                if (recoloured > most_glow) most_glow = recoloured;
                for (u32 k = city::first; k < city::restyle_end; ++k) {
                    if (city::kind[k] != city::window) continue;
                    if (frame.nodes[k].shade != city::slot[city::low[k]]) ++now;
                    if (peak < 0 || f >= held) seen[k] |= 1u << frame.nodes[k].shade;   // through the attack and release
                }
                if (now > glowing) glowing = now;
            }
            u32 windows = 0, greys = 0;
            for (u32 k = city::first; k < city::restyle_end; ++k) {
                if (city::kind[k] != city::window || seen[k] == (1u << city::slot[city::low[k]])) continue;
                ++windows;
                for (u32 g = seen[k]; g; g &= g - 1) ++greys;
            }
            const double expected_out = at_release > glow_floor_of() ? tau_r * std::log(at_release / 1024.0) : 0;
            const double expected_level = sustain + (1 - sustain) * std::exp(-(held - peak) * frame_s / tau_d);
            std::printf("%s's envelope: at its peak in %.0f ms (attack %.0f ms); %.2f at the release (%.2f by its "
                        "decay); out in %.0f ms (%.0f ms by its release); %u windows glowed, through %.1f greys each; "
                        "at most %u points and %u nodes a frame, %u of them restyled\n", w.name, peak * frame_s * 1000, attack_s * 1000,
                        at_release / 65536.0, expected_level, out * frame_s * 1000, expected_out * 1000,
                        static_cast<unsigned>(glowing), windows ? greys / double(windows) : 0.0,
                        static_cast<unsigned>(most), static_cast<unsigned>(most_count), static_cast<unsigned>(most_glow));
            expect(peak > 0 && std::fabs(peak * frame_s - attack_s) <= frame_s + 0.05 * attack_s,
                   "the glow doesn't peak at the end of the attack");
            expect(std::fabs(at_release / 65536.0 - expected_level) <= 0.03, "the glow doesn't follow the decay");
            expect(out >= 0 && std::fabs(out * frame_s - expected_out) <= 2 * frame_s + 0.15 * expected_out,
                   "the glow doesn't go out over the release");
            expect(glowing >= 16 && glowing <= 48, "too few or too many windows glow");
            if (attack_s >= 0.2) expect(windows && greys >= 3 * windows, "the glow doesn't step through the greys");
            expect(most <= most_points, "the glow changes more points in a frame than the budget");
            expect(most_count <= nexus::screen::most_nodes + 8, "the glow changes more nodes in a frame than the budget");
            expect(most_glow <= nexus::screen::most_busy, "the glow restyles more nodes in a frame than most_busy");
            expect(still(s, 1400 + e), "the screen changes after the glow goes out");
        }
        amp.attack = amp.decay = amp.sustain = amp.release = opxy::draw::envelope_unavailable;
        expect(still(slots, 1450), "the screen changes at rest once the envelope is unknown again");
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

    // Lowcut clears the sun poster's reflection from the bottom stripe up, Shift held or not: all of it at
    // lowcut 0, none at 100, never one more as it rises, and still at rest.
    for (int held = 0; held < 2; ++held) {
        row.held = static_cast<u32>(held);
        u32 last = 99;
        for (int v = 0; v <= 100; ++v) {
            row.values[2] = v == 100 ? 0x7fffffff : v * 21474836 + (v * 47 + 50) / 100;
            expect(still(slots, 1150), "the screen changes at rest while lowcut is set");
            expect(settled(slots), "the poster hasn't settled");
            u32 shown = 0, lowest = 99;
            for (u32 k = city::first; k < city::restyle_end; ++k)
                if (city::kind[k] == city::stripe && !hidden(frame.nodes[k])) {
                    ++shown;
                    if (city::param[k] < lowest) lowest = city::param[k];
                }
            expect(shown <= last, "a reflection stripe comes back as lowcut rises");
            expect(!shown || lowest == city::sun_stripes - shown, "the reflection doesn't clear from the bottom up");
            if (v == 0) expect(shown == city::sun_stripes, "the reflection isn't whole at lowcut 0");
            if (v == 100) expect(shown == 0, "the reflection isn't gone at lowcut 100");
            last = shown;
        }
    }
    row.held = 0;
    row.values[2] = defaults[2];

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

    // The round parts: the roof's (the helipad's ring, each fan's rim, well and hub) SVG ellipses, or outlines
    // of points in make_panel.py's --round paths fallback; the ball, its sheen and its glint bound circles.
    {
        std::printf("round parts: %u ellipses, %u still circles, %u bound circles\n", static_cast<unsigned>(city::ellipses),
                    static_cast<unsigned>(city::circles), static_cast<unsigned>(city::bound_circles));
        expect(city::ellipses == 0 || city::ellipses == 3 * (city::fans + city::ducts) + 1,
               "the roof's round parts aren't each an ellipse");
        expect(city::bound_circles == 3, "the ball isn't three bound circles");
    }

    // Octave slides the ball along its spire, its rings with it: each of its four steps a place of its own, higher
    // as octave rises; there within half a second, a half pixel at a time, then still. The ball's circles only
    // move: never turned or scaled.
    {
        i32 s[8];
        for (int j = 0; j < 8; ++j) s[j] = slots[j];
        s[5] = 0x7fffffff;                                 // every ring shown
        const u32 ball = first_of(city::orb, 0);
        expect(ball != 0, "no ball");
        i32 last = 1 << 30;
        for (u32 step_ = 0; step_ < 4; ++step_) {
            row.values[1] = middle(step_, 4);
            const i32 want = (city::home_y[ball] + city::ball_step[step_]) * unit;
            int frames = 0;
            while (frame.nodes[ball].y != want && frames < 60) {
                const Change c = step(s, 0, 1500);
                expect(c.points <= most_points && c.nodes <= nexus::screen::most_nodes + 8, "the ball's move is over the budget");
                for (u32 k = city::restyle_end; k < city::node_count; ++k)
                    if (city::kind[k] == city::orb && !hidden(frame.nodes[k]) && !hidden(before.nodes[k]))
                        expect(frame.nodes[k].y - before.nodes[k].y <= 3 * unit && before.nodes[k].y - frame.nodes[k].y <= 3 * unit,
                               "the ball jumps");
                ++frames;
            }
            expect(frames <= 16, "the ball takes more than half a second to reach octave's step");
            expect(still(s, 1500), "the screen changes at rest while octave is set");
            const i32 y = frame.nodes[ball].y;
            expect(y == want, "the ball isn't at octave's step");
            expect(y < last, "the ball doesn't rise as octave does");
            for (u32 k = city::restyle_end; k < city::node_count; ++k) {
                if (city::kind[k] != city::orb) continue;
                const opxy::draw::Node& n = frame.nodes[k];
                expect(!hidden(n), "a part of the ball is hidden");
                expect(n.y - city::home_y[k] * unit == y - city::home_y[ball] * unit, "a part of the ball or a ring is left behind");
                expect(n.scale == unit && n.cos == unit && n.sin == 0, "the ball's circles turn or scale");
            }
            std::printf("octave step %u: the ball %d px from its rest, settled in %d frames\n", static_cast<unsigned>(step_),
                        static_cast<int>((y - city::home_y[ball] * unit) / unit), frames);
            last = y;
        }
        row.values[1] = defaults[1];
        expect(still(slots, 1501), "the screen changes at rest");
    }

    // Ratio turns the dish and its rings as one: each of its eight steps a way of its own, clockwise as ratio
    // rises; there within half a second, then still.
    {
        const u32 dish = first_of(city::dish);
        i32 last = -unit - 1;
        for (u32 step_ = 0; step_ < 8; ++step_) {
            row.values[5] = middle(step_, 8);
            expect(still(slots, 1550), "the screen changes at rest while ratio is set");
            const opxy::draw::Node& n = frame.nodes[dish];
            for (u32 k = dish; k < city::node_count && city::kind[k] == city::dish; ++k)
                if (!hidden(frame.nodes[k])) expect(frame.nodes[k].sin == n.sin && frame.nodes[k].cos == n.cos, "the dish's parts turn apart");
            expect(n.sin > last, "the dish doesn't turn clockwise as ratio rises");
            std::printf("ratio step %u: the dish at %.1f degrees\n", static_cast<unsigned>(step_), std::atan2(n.sin, n.cos) * 57.29578);
            last = n.sin;
        }
        // a step's turn, eased: within half a second, each frame within the budgets
        row.values[5] = middle(0, 8);
        int frames = 0;
        while (!(frames > 0 && anything(changed()) == 0) && frames < 60) {
            const Change c = step(slots, 0, 1551);
            expect(c.points <= most_points, "the dish's turn changes more points than the budget");
            ++frames;
        }
        std::printf("ratio from its top step to its bottom: the dish turned in %d frames\n", frames - 1);
        expect(frames <= 17, "the dish takes more than half a second to turn to ratio's step");
        row.values[5] = defaults[5];
        expect(still(slots, 1552), "the screen changes at rest");
    }

    // Spread brings in the road's cars: two at 0, more as it rises, all eight at 100, each where it shows at rest
    // (make_panel.py places them so); still at rest.
    {
        nexus::display_reset(state);
        step(slots, 0, 1600);
        u32 last = 0;
        for (int v = 0; v <= 100; v += 10) {
            row.values[6] = v == 100 ? 0x7fffffff : v * 21474836;
            expect(still(slots, 1600), "the screen changes at rest while spread is set");
            u32 shown = 0;
            for (u32 k = city::restyle_end; k < city::node_count; ++k)
                if (city::kind[k] == city::car && !hidden(frame.nodes[k])) ++shown;
            expect(shown >= last, "a car goes as spread rises");
            if (v == 0) expect(shown == 2, "spread 0 doesn't show two cars");
            if (v == 100) expect(shown == city::cars, "spread 100 doesn't show every car");
            if (v % 50 == 0) std::printf("spread %d: %u cars\n", v, static_cast<unsigned>(shown));
            last = shown;
        }
        row.values[6] = defaults[6];
        expect(still(slots, 1601), "the screen changes at rest");
    }

    // Noise, balance and drift change nothing at rest: they show only while notes are held.
    {
        expect(still(slots, 1650), "the screen changes at rest");
        nexus::Frame base;
        std::memcpy(&base, &frame, sizeof frame);
        for (int e = 3; e <= 7; e += (e == 4 ? 3 : 1)) {   // noise, balance, drift
            for (int end = 0; end < 2; ++end) {
                row.values[e] = end ? 0x7fffffff : 0;
                expect(still(slots, 1650), "the screen changes at rest");
                expect(!std::memcmp(frame.nodes, base.nodes, sizeof frame.nodes), "noise, balance or drift changes the screen at rest");
            }
            row.values[e] = defaults[e];
        }
    }

    // Balance sets the right-hand block's fans' speeds while notes are held, layer I's (fan 1) and layer II's (fan 2):
    // equal at 50; toward 0 layer II's slows almost to a stop, toward 100 layer I's.
    for (int b = 0; b < 3; ++b) {
        row.values[4] = b == 0 ? 0 : b == 1 ? 0x40000000 : 0x7fffffff;
        for (int f = 0; f < 40; ++f) step(slots, 1, 1700);
        u32 turned[city::fans] = {0, 0, 0};
        for (int f = 0; f < 100; ++f) {
            step(slots, 1, 1700);
            for (u32 k = 0; k < city::fans; ++k) turned[k] += ((frame.paths[k].key - 1) - (before.paths[k].key - 1)) & 0xffffu;
        }
        std::printf("balance %d: the fans turned %.2f, %.2f and %.2f turns in 100 frames\n", b * 50, turned[0] / 65536.0,
                    turned[1] / 65536.0, turned[2] / 65536.0);
        if (b == 1) expect(turned[1] == turned[2], "the right-hand fans differ at balance 50");
        if (b == 0) expect(turned[2] * 8 < turned[1], "layer II's fan doesn't slow almost to a stop at balance 0");
        if (b == 2) expect(turned[1] * 8 < turned[2], "layer I's fan doesn't slow almost to a stop at balance 100");
        expect(turned[1] && turned[2], "a fan stops while notes are held");
    }
    row.values[4] = defaults[4];
    expect(still(slots, 1701), "the fans don't stop once released");

    // Drift wobbles the craft while it hovers and notes are held: steady at 0; at 100 it bobs, and drifts across
    // the roof's plane (as the city's lines run, a pixel across and a half down), slowly, a pixel at a time,
    // never tilting; steady again once released.
    {
        i32 s[8];
        for (int j = 0; j < 8; ++j) s[j] = slots[j];
        s[7] = 0x4ccccccc;                                 // bite 60: hovering
        const u32 craft = first_of(city::craft);
        for (int d = 0; d < 2; ++d) {
            row.values[7] = d ? 0x7fffffff : 0;
            for (int f = 0; f < 60; ++f) step(s, 1, 1750);
            u32 moves = 0;
            i32 low = 1 << 30, high = -(1 << 30), left = 1 << 30, right = -(1 << 30);
            u32 tilted = 0;
            for (int f = 0; f < 400; ++f) {
                step(s, 1, 1750);
                const opxy::draw::Node &a = before.nodes[craft], &n = frame.nodes[craft];
                if (a.x != n.x || a.y != n.y || a.sin != n.sin) {
                    ++moves;
                    expect(n.x - a.x <= unit && a.x - n.x <= unit && n.y - a.y <= unit && a.y - n.y <= unit,
                           "the craft wobbles in jumps");
                }
                if (n.y < low) low = n.y;
                if (n.y > high) high = n.y;
                if (n.x < left) left = n.x;
                if (n.x > right) right = n.x;
                if (n.sin != 0 || n.cos != unit) ++tilted;
                // across the roof's plane: each pixel across with half a pixel down the other way, bob aside
                if (a.x != n.x) expect(n.x - a.x == unit || a.x - n.x == unit, "the craft drifts across in jumps");
            }
            std::printf("drift %d: the craft moved in %u of 400 frames, %.1f px up and down, %.1f across\n",
                        d * 100, static_cast<unsigned>(moves), (high - low) / 1024.0, (right - left) / 1024.0);
            expect(tilted == 0, "the craft tilts");
            if (!d) expect(moves == 0, "the craft wobbles at drift 0");
            if (d) {
                expect(moves >= 20 && moves <= 300, "the craft doesn't wobble slowly at drift 100");
                expect(high - low >= 3 * unit && right - left >= 2 * unit, "the craft doesn't bob and drift at drift 100");
            }
        }
        row.values[7] = defaults[7];
        expect(still(s, 1751), "the craft doesn't settle once released");
    }

    // Noise flickers the neon sign while notes are held: steady at 0, now and then at Tyrell's 6, often at 100;
    // steady at rest.
    {
        u32 flickers[3] = {0, 0, 0};
        const i32 noise[3] = {0, defaults[3], 0x7fffffff};
        for (int v = 0; v < 3; ++v) {
            row.values[3] = noise[v];
            for (int f = 0; f < 60; ++f) step(slots, 1, 1800);
            for (int f = 0; f < 300; ++f) {
                step(slots, 1, 1800);
                bool flicker = false;
                for (u32 k = city::restyle_end; k < city::node_count; ++k)
                    if (city::kind[k] == city::sign && frame.nodes[k].shade != before.nodes[k].shade) flicker = true;
                if (flicker) ++flickers[v];
            }
            expect(still(slots, 1801), "the sign doesn't settle once released");
        }
        std::printf("noise 0, 6 and 100: the sign flickered in %u, %u and %u of 300 frames\n", static_cast<unsigned>(flickers[0]),
                    static_cast<unsigned>(flickers[1]), static_cast<unsigned>(flickers[2]));
        expect(flickers[0] == 0, "the sign flickers at noise 0");
        expect(flickers[1] > 0 && flickers[1] * 3 < flickers[2], "the sign doesn't flicker more as noise rises");
        expect(flickers[2] >= 60, "the sign doesn't flicker often at noise 100");
        row.values[3] = defaults[3];
    }

    // The aircraft warning lights blink slowly while notes are held, each on for about a quarter of a second in
    // two; the duct fans step through their blade poses (those drawn, then the same turned half round), one of
    // each shown at a time and each a step on from the last, about a step a frame; both still at rest.
    {
        const u32 warning = first_of(city::warning), duct = first_of(city::vane);
        expect(warning && duct, "no warning lights or duct fans");
        for (int f = 0; f < 60; ++f) step(slots, 1, 1850);
        u32 blinks = 0, lit = 0, steps[8] = {0}, shown_at[8];
        for (u32 j = 0; j < city::ducts; ++j) shown_at[j] = 99;
        const int length = 620;                            // 20 s
        for (int f = 0; f < length; ++f) {
            step(slots, 1, 1850);
            const opxy::draw::Node &a = before.nodes[warning], &n = frame.nodes[warning];
            if (n.shade == city::slot[city::high[warning]]) ++lit;
            if (a.shade == city::slot[city::high[warning]] && n.shade != a.shade) ++blinks;
            for (u32 j = 0; j < city::ducts; ++j) {
                u32 shown = 0, pose = 0;
                for (u32 k = duct; k < city::node_count && city::kind[k] == city::vane; ++k)
                    if (city::param[k] / city::vane_shapes == j && !hidden(frame.nodes[k])) {
                        ++shown;
                        pose = city::param[k] % city::vane_shapes + (frame.nodes[k].cos < 0 ? city::vane_shapes : 0);
                        expect(frame.nodes[k].sin == 0 && (frame.nodes[k].cos == unit || frame.nodes[k].cos == -unit),
                               "a duct fan's blades turned other than half round");
                    }
                expect(shown == 1, "a duct fan shows other than one pose");
                if (shown_at[j] != 99 && pose != shown_at[j]) {
                    expect(pose == (shown_at[j] + 1) % city::vane_poses, "a duct fan's blades skip a pose");
                    ++steps[j];
                }
                shown_at[j] = pose;
            }
        }
        std::printf("held 20 s: a warning light blinked %u times, lit %.0f%% of the time; the duct fans stepped %u, %u and %u "
                    "times\n", static_cast<unsigned>(blinks), 100.0 * lit / length, static_cast<unsigned>(steps[0]),
                    static_cast<unsigned>(steps[1]), static_cast<unsigned>(steps[2]));
        expect(blinks >= 8 && blinks <= 12, "the warning lights don't blink slowly");
        expect(lit * 100 >= 8u * length && lit * 100 <= 20u * length, "the warning lights aren't on a quarter of a second in two");
        for (u32 j = 0; j < city::ducts; ++j) expect(steps[j] * 2 >= static_cast<u32>(length), "a duct fan's blades don't step about a frame at a time while held");
        expect(still(slots, 1851), "the warning lights or duct fans don't stop once released");
    }

    // Released, everything eases to a stop and then nothing changes.
    expect(still(slots, 500), "the screen changes after the train stops");
    std::printf("filled at most %.0f px^2, %.2f of the picture (header included); fog at most %.2f of it, %.2f "
                "with the fog counted four times (blended)\n", most_filled, most_filled / (480.0 * 182), most_fog / (480.0 * 182),
                most_weighted / (480.0 * 182));
    expect(most_filled <= most_fill, "more of the screen is filled than the budget");
    expect(most_fog <= most_fog_fill, "more fog than its budget");
    expect(most_weighted <= most_weighted_fill, "the fill and the blended fog together are over the budget");
    if (!failures) std::puts("ok");
    return failures ? 1 : 0;
}
