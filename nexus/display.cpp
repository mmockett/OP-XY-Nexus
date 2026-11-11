#include "display.h"

// Nexus's screen: a still isometric night city, the same every time, and a
// white flying car with its wingman that crosses it, level, left to right,
// while notes are held. They come in off the left, leave off the right and come
// round again after a short gap; released, they ease to a stop wherever they
// are. Small traffic crosses on three level lanes at different depths, a
// searchlight sweeps from one roof, and the mast lights blink, all only while
// the cars fly. Each lot's parts are a hash of its index (tools/make_panel.py
// mirrors this, to draw previews).
//
// What the controls do to it:
//   shape   the skyline: rolling for sine, ramps for saw, blocks for square, spikes for pulse
//   detune  the wingman (layer II) drifts off the leader's line
//   bright  the share of lit windows
//   reso    the neon strips brighten
//   chorus  the two cars weave apart and back, on the chorus's clock
//   ring    rings on the masts, faint to bright, one then two
//   touch   dark windows flare, one after another, while a note is held
//   ribbon  the cars' height
//
// The Shift row shows in the header while Shift is held; the city doesn't draw it.
//
// The device pays for every point of an outline that changes, so a frame changes
// at most `budget` points. While the cars fly they, the traffic and the searchlight
// take about half; the city restyles within the rest, a whole lot at a time,
// resuming where it left off. Nothing changes at rest. Only the towers that hide
// something behind them are filled.
namespace nexus {
namespace screen {
using namespace opxy::draw;
using opxy::i32;
using opxy::u32;

// Shows the frame rate and the slowest frame in the bottom-left corner, for testing on the device.
const bool show_fps = false;
const u32 fps_text = 16;

const u32 budget = 280;                 // points of outlines changed a frame, at most
const i32 cruise = 110 * unit;          // the cars' speed while held, px a second
const i32 speed_share = 596;            // 65536 / 110: speed as a share of cruise, Q10, by (speed * 596) >> 16
const i32 behind = 66, apart = 16;      // the wingman behind the leader, px, and how far detune takes it
// The lead car's course, px: from just off the left to where the wingman has left on the right, then a gap.
const i32 course_left = -city::lead_right;
const i32 course = 480 + behind + apart - city::wing_left + 70 - course_left;
const i32 home = 300;                   // where the lead car rests after a reset, px
const i32 car_y = 132, car_rise = 48;   // the lead car's height on screen at ribbon 0, and how far ribbon lifts it, px
const u32 beam_points = 4;

static inline u32 hash(i32 i, i32 j, u32 k) {
    u32 h = static_cast<u32>(i) * 374761393u + static_cast<u32>(j) * 668265263u + k * 1442695041u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static inline u32 r16(i32 i, i32 j, u32 k) { return hash(i, j, k) >> 16; }
static inline u32 q16(i32 control) { return control < 0 ? 0u : static_cast<u32>(control) >> 15; }
static inline u32 smallest(u32 a, u32 b) { return a < b ? a : b; }

// 3u^2 - 2u^3, Q16
static inline u32 smooth(u32 u) {
    const u32 u2 = (u * u) >> 16;
    return (u2 * (196608u - 2 * u)) >> 16;
}
// The skyline's wave at a phase, Q16: the oscillator's morph, drawn in towers.
static inline u32 wave(u32 ph, u32 s) {
    const u32 sine = ph < 32768u ? smooth(2 * ph) : smooth(131070u - 2 * ph);
    const u32 saw = ph, square = ph < 32768u ? 65535u : 0u;
    if (s < 21845u) return static_cast<u32>(static_cast<i32>(sine) + ((static_cast<i32>(saw) - static_cast<i32>(sine)) * static_cast<i32>(s * 3) >> 16));
    if (s < 43690u) return static_cast<u32>(static_cast<i32>(saw) + ((static_cast<i32>(square) - static_cast<i32>(saw)) * static_cast<i32>((s - 21845u) * 3) >> 16));
    const u32 width = 32768u - (((s - 43690u) * 3 * 24904u) >> 16);
    return ph < width ? 65535u : 0u;
}
// sin(2 pi x) for a Q16 turn, Q10: a parabola, close enough for a sway.
static inline i32 sway(u32 turn) {
    const i32 x = static_cast<i32>(static_cast<opxy::i16>(turn & 0xffffu));   // -32768..32767 is -pi..pi
    const i32 a = x < 0 ? -x : x;
    return (x * (32768 - a)) >> 18;                                         // 4 x (1 - |x|), Q10
}

struct Controls { u32 shape, detune, bright, reso, chorus, ring, touch, ribbon; };

// ------------------------------------------------------------------ nodes and what they cost
static inline Node pose(i32 x, i32 y, i32 scale, u32 shade, Paint paint) {
    Node n;
    n.x = clamp(x, node_left * unit, node_right * unit);
    n.y = clamp(y, node_top * unit, node_bottom * unit);
    n.scale = scale;
    n.cos = unit;
    n.sin = 0;
    n.shade = shade;
    n.paint = paint;
    return n;
}
static inline Node parked() { return pose(hidden_x * unit, hidden_y * unit, unit, 0, keep); }
static inline bool same(const Node& a, const Node& b) {
    return a.x == b.x && a.y == b.y && a.scale == b.scale && a.cos == b.cos && a.sin == b.sin &&
           a.shade == b.shade && a.paint == b.paint;
}
// Sets a node, adding its points to `spent` when it changes.
static inline void set(Frame& frame, u32 n, const Node& value, u32& spent) {
    if (same(frame.nodes[n], value)) return;
    frame.nodes[n] = value;
    spent += city::points[n];
}

// ------------------------------------------------------------------ the city
// What a lot shows: `form` packs its scale (bits 0-5), neon (6-8: 0 none, 1..4 dim to bright),
// two rings (9-10, 11-12: 0 none, 1..3 faint to bright), a lit sign (13), its mast light on (14)
// and bit 31, so that no look is 0; `lit` holds its windows, two bits each.
struct Look { u32 form, lit; };

static inline i32 scale_of(u32 form) { return 614 + static_cast<i32>(form & 63u) * 11; }   // Q10, 0.6 .. 1.28

static inline Look desired(u32 k, const Controls& c, bool held, i32 hold, u32 serial, i32 clock) {
    const city::Lot& lot = city::lots[k];
    const i32 i = static_cast<i32>(k);
    const u32 sky = wave((lot.column * 11916u) & 0xffffu, c.shape);
    const u32 mix = (sky * 39322u + r16(i, 0, 1) * 26214u) >> 16;
    Look look;
    look.form = 1u << 31 | ((mix * 63u) >> 16);
    look.lit = 0;
    const i32 rise = 154 + static_cast<i32>((c.touch * 973u) >> 16);   // the flare's spread, Q10 seconds
    for (u32 w = 0; w < lot.windows; ++w) {
        const i32 j = static_cast<i32>(w);
        u32 v = 0;
        if (r16(i, j, 20) < c.bright) v = 1;
        else if (held && c.touch && r16(i * 7 + j, static_cast<i32>(serial), 3) < (c.touch >> 1) &&
                 hold >= static_cast<i32>((r16(i, j, 40) * static_cast<u32>(rise)) >> 16)) v = 2;
        look.lit |= v << (2 * w);
    }
    if (lot.neon) look.form |= (1 + smallest(3u, (c.reso * 4) >> 16)) << 6;
    if (lot.mast) {
        const u32 g = c.ring;
        const u32 first = g == 0 ? 0 : 1 + smallest(2u, (g * 6) >> 16);
        const u32 second = g < 32768u ? 0 : 1 + smallest(2u, ((g - 32768u) * 6) >> 16);
        look.form |= first << 9 | second << 11;
        // the mast light: on for 0.27 s of every second, each mast at its own phase
        if (((static_cast<u32>(clock) + (r16(i, 0, 9) >> 6)) & 1023u) < 280u) look.form |= 1u << 14;
    }
    if (lot.sign && r16(i, 0, 8) < c.bright + 9830u) look.form |= 1u << 13;
    return look;
}

// One part of a lot: every part scales about the lot's pivot, so its parts stay together.
static inline Node part(const city::Lot& lot, u32 node, i32 s, u32 shade, Paint paint) {
    return pose(lot.x * unit + s * city::origin_x[node], lot.y * unit + s * city::origin_y[node], s, shade, paint);
}

const u32 most_parts = 1 + city::most_windows + 1 + 2 + 3;

// A lot's nodes for a look, in order from its first.
static inline u32 lot_parts(const city::Lot& lot, const Look& look, Node* out) {
    const i32 s = scale_of(look.form);
    u32 node = lot.first, n = 0;
    out[n++] = part(lot, node++, s, 0, keep);                                       // the body
    for (u32 w = 0; w < lot.windows; ++w, ++node) {
        const u32 v = (look.lit >> (2 * w)) & 3u;
        out[n++] = v ? part(lot, node, s, v == 2 ? 3 : 1 + ((lot.faces >> w) & 1u), fill_and_stroke) : parked();
    }
    if (lot.neon) out[n++] = part(lot, node++, s, 3 + ((look.form >> 6) & 7u), fill_and_stroke);
    if (lot.sign) {
        out[n++] = part(lot, node++, s, 0, keep);                                   // the blade
        out[n++] = (look.form >> 13 & 1u) ? part(lot, node, s, 11, fill_and_stroke) : parked();
        ++node;
    }
    if (lot.mast) {
        for (u32 r = 0; r < 2; ++r, ++node) {
            const u32 level = (look.form >> (9 + 2 * r)) & 3u;
            out[n++] = level ? part(lot, node, s, 7 + level, stroke) : parked();
        }
        out[n++] = part(lot, node, s, (look.form >> 14 & 1u) ? 13 : 12, fill_and_stroke);
    }
    return n;
}

// Brings lots to their looks, a whole lot at a time, within `left` points; resumes next
// frame at the first lot that didn't fit.
static inline void draw_city(DisplayState& state, Frame& frame, const Controls& c, bool held, u32 left, u32& spent) {
    u32 k = state.cursor;
    for (u32 count = 0; count < city::lot_count; ++count, k = k + 1 == city::lot_count ? 0 : k + 1) {
        const Look want = desired(k, c, held, state.hold, state.serial, state.clock);
        if (!state.fresh && want.form == state.form[k] && want.lit == state.lit[k]) continue;
        const city::Lot& lot = city::lots[k];
        Node parts[most_parts];
        const u32 n = lot_parts(lot, want, parts);
        u32 cost = 0;
        for (u32 p = 0; p < n; ++p)
            if (!same(frame.nodes[lot.first + p], parts[p])) cost += city::points[lot.first + p];
        if (!state.fresh && cost > left) break;
        for (u32 p = 0; p < n; ++p) frame.nodes[lot.first + p] = parts[p];
        left = cost > left ? 0 : left - cost;
        spent += cost;
        state.form[k] = want.form;
        state.lit[k] = want.lit;
    }
    state.cursor = k;
}

// The searchlight, on its lot's roof: it sweeps 30 degrees either side, once in about 7 s, as the cars fly.
static inline void draw_beam(const DisplayState& state, Frame& frame, u32& spent) {
    const city::Lot& lot = city::lots[city::beam_lot];
    const i32 s = scale_of(state.form[city::beam_lot]);
    Node beam = pose(lot.x * unit + s * city::beam_x, lot.y * unit + s * city::beam_y, 2 * unit, 0, keep);
    const Turn t = rotation((sway(static_cast<u32>(state.clock) * 9u) * 300) >> 10);
    beam.cos = t.cos;
    beam.sin = t.sin;
    set(frame, city::beam, beam, spent);
}

// The traffic: each lane's cars at their places along its loop, which runs 10 px past each side.
static inline void draw_traffic(const DisplayState& state, Frame& frame, u32& spent) {
    u32 car = 0;
    for (u32 l = 0; l < city::lane_count; ++l) {
        const city::Lane& lane = city::lanes[l];
        for (u32 n = 0; n < lane.cars; ++n, ++car) {
            i32 p = city::spots[car] * unit + state.lane[l];
            if (p >= city::lane_length * unit) p -= city::lane_length * unit;
            const i32 x = lane.rightward ? city::lane_left * unit + p : (city::lane_left + city::lane_length) * unit - p;
            set(frame, lane.first + n, pose(x, lane.y * unit, unit, 0, keep), spent);
        }
    }
}

// The cars: the leader, and the wingman (layer II) behind it, off its line by detune. They bob
// and bank gently as they fly, in step with the speed; off screen, they're parked.
static inline void draw_cars(const DisplayState& state, Frame& frame, const Controls& c, bool calm, u32& spent) {
    const u32 clock = static_cast<u32>(state.clock);
    const i32 share = calm ? 0 : clamp((state.speed * speed_share) >> 16, 0, unit);   // Q10
    const i32 bob = ((sway(clock * 31u) * 3 / 2) * share) >> 10;                 // 1.5 px at about 0.5 Hz
    const i32 bank = (((sway(clock * 31u + 16384u) * 15) >> 10) * share) >> 10;  // 1.5 degrees, nose up as it rises
    // chorus: the cars weave apart, up to 5 px each, on the chorus's 0.9 Hz clock
    const i32 weave = (static_cast<i32>(c.chorus >> 6) * 5 * sway(clock * 58u)) >> 10;
    const i32 x = course_left * unit + state.run;
    const i32 y = car_y * unit - static_cast<i32>((c.ribbon * static_cast<u32>(car_rise)) >> 6) + bob + weave;
    const Turn tilt = rotation(-bank);
    const bool lead = x + city::lead_right * unit > 0 && x + city::lead_left * unit < 480 * unit;
    for (u32 k = 0; k < city::lead_nodes; ++k) {
        Node v = lead ? pose(x, y, unit, 0, keep) : parked();
        if (lead) { v.cos = tilt.cos; v.sin = tilt.sin; }
        set(frame, city::car_first + k, v, spent);
    }
    const i32 off = static_cast<i32>((c.detune * static_cast<u32>(apart)) >> 6);   // Q10 px, 0..16
    const i32 wx = x - behind * unit - off, wy = y + 16 * unit + off * 3 / 5 - 2 * weave;
    const bool wing = c.detune > 1966u && wx + city::wing_right * unit > 0 && wx + city::wing_left * unit < 480 * unit;
    for (u32 k = 0; k < city::wing_nodes; ++k) {
        Node v = wing ? pose(wx, wy, unit, 0, keep) : parked();
        if (wing) { v.cos = tilt.cos; v.sin = tilt.sin; }
        set(frame, city::car_first + city::lead_nodes + k, v, spent);
    }
}

// The frame-rate readout, as BENCH counts it: frames until a second of elapsed time, and the
// slowest frame in that second. No division.
static inline char* digits(char* out, u32 value) {
    if (value > 9999) value = 9999;
    bool started = false;
    const u32 places[4] = {1000, 100, 10, 1};
    for (u32 p = 0; p < 4; ++p) {
        char digit = '0';
        while (value >= places[p]) { value -= places[p]; ++digit; }
        if (digit != '0' || started || p == 3) { *out++ = digit; started = true; }
    }
    *out = 0;
    return out;
}
static inline char* words(char* out, const char* tail) {
    while (*tail) *out++ = *tail++;
    *out = 0;
    return out;
}
static inline void readout(DisplayState& state, const Input& input, Frame& frame) {
    if (!show_fps) {
        text(frame, fps_text, hidden_x * unit, hidden_y * unit, unit, "");
        return;
    }
    const u32 dt = static_cast<u32>(clamp(input.elapsed, 0, 4096));
    ++state.frames;
    state.acc += dt;
    if (dt > state.worst) state.worst = dt;
    if (state.acc >= static_cast<u32>(unit)) {
        state.fps = state.frames;
        state.worst_shown = state.worst;
        state.frames = state.acc = state.worst = 0;
    }
    char line[text_capacity];
    char* at = digits(line, state.fps);
    at = words(at, " fps  ");
    at = digits(at, (state.worst_shown * 1000u) >> 10);
    words(at, " ms");
    text(frame, fps_text, 4 * unit, 216 * unit, unit, line);
}

// ------------------------------------------------------------------ the header's captions
static inline void number(char* out, i32 n) {
    char t[8];
    u32 at = 0;
    if (n < 0) { out[at++] = '-'; n = -n; }
    u32 k = 0;
    do { t[k++] = static_cast<char>('0' + n % 10); n /= 10; } while (n && k < 6);
    while (k) out[at++] = t[--k];
    out[at] = 0;
}
static inline void append(char* out, const char* tail) {
    u32 at = 0;
    while (out[at]) ++at;
    for (u32 k = 0; tail[k] && at + 1 < header_capacity; ++k) out[at++] = tail[k];
    out[at] = 0;
}
// 2^x for Q16 x in 0..65535, Q16 (1..2): a quadratic, within 0.2%.
static inline u32 exp2_fraction(u32 x) {
    return 65536u + ((x * 45426u) >> 16) + ((((x * x) >> 16) * 20110u) >> 16);
}
// d^1.5 for Q16 d, Q16: d times its square root (bisection, 16 steps).
static inline u32 power15(u32 d) {
    u32 root = 0;
    for (u32 bit = 1u << 15; bit; bit >>= 1) {
        const u32 trial = root | bit;
        if (((trial * trial) >> 16) <= d) root = trial;
    }
    return (d * root) >> 16;
}
// A frequency: 990, 1.0k, 16k.
static inline void hertz_caption(char* out, u32 hertz) {
    out[0] = 0;
    char n[8];
    if (hertz < 1000u) {
        number(out, static_cast<i32>(hertz));
    } else if (hertz < 9950u) {
        const u32 tenths = (hertz + 50u) / 100u;
        number(out, static_cast<i32>(tenths / 10u));
        append(out, ".");
        number(n, static_cast<i32>(tenths % 10u));
        append(out, n);
        append(out, "k");
    } else {
        number(out, static_cast<i32>((hertz + 500u) / 1000u));
        append(out, "k");
    }
}
// x 2^(octaves) for Q16 octaves >= 0.
static inline u32 times_exp2(u32 x, u32 octaves) {
    return ((x * exp2_fraction(octaves & 0xffffu)) << (octaves >> 16)) >> 16;
}
static inline void captions(char (&values)[8][header_capacity], const Controls& c, const i32* slots) {
    for (u32 i = 0; i < 8; ++i) header_amount(values[i], header_capacity, slots[i]);
    // detune: 0c .. 30c
    number(values[1], static_cast<i32>((power15(c.detune) * 30u + 32768u) >> 16));
    append(values[1], "c");
    // bright: 80 Hz .. 16 kHz, 7.64 octaves
    hertz_caption(values[2], times_exp2(80u, (c.bright * 7644u) / 1000u));
    // ribbon: -12st .. +12st
    const i32 st = (static_cast<i32>(c.ribbon) * 24 + 32768) / 65536 - 12;
    values[7][0] = 0;
    if (st > 0) append(values[7], "+");
    char n[8];
    number(n, st);
    append(values[7], n);
    append(values[7], "st");
}

// The Shift row: balance, octave, lowcut, noise; spread, ratio, drift, slide.
static inline u32 step_of(i32 value) { return (q16(value) * 100u + 32768u) >> 16; }
static inline void shift_captions(char (&values)[8][header_capacity], const i32* row) {
    for (u32 i = 0; i < 8; ++i) header_amount(values[i], header_capacity, row[i]);
    static const char* const footage[4] = {"16'", "8'", "4'", "2'"};
    copy(values[1], header_capacity, footage[(q16(row[1]) * 4u) >> 16]);
    // lowcut: off, then 20 Hz .. 2 kHz (6.64 octaves)
    const u32 cut = step_of(row[2]);
    if (cut) hertz_caption(values[2], times_exp2(20u, cut * 4354u));
    else copy(values[2], header_capacity, "off");
    static const char* const ratios[8] = {"0.5", "1", "1.5", "2", "2.6", "3", "3.5", "4"};
    copy(values[5], header_capacity, ratios[(q16(row[5]) * 8u) >> 16]);
    // slide: 0.02 s .. 1.5 s (6.23 octaves), in hundredths
    const u32 hundredths = (times_exp2(20u, step_of(row[7]) * 4082u) + 5u) / 10u;   // from ms
    char n[8];
    number(values[7], static_cast<i32>(hundredths / 100u));
    append(values[7], ".");
    if (hundredths % 100u < 10u) append(values[7], "0");
    number(n, static_cast<i32>(hundredths % 100u));
    append(values[7], n);
    append(values[7], "s");
}
}

inline void display_reset(DisplayState& state) {
    state.notes.reset();
    state.header.reset();
    state.fresh = 1;
    state.cursor = 0;
    state.run = (screen::home - screen::course_left) * opxy::draw::unit;
    state.speed = 0;
    state.clock = 0;
    state.hold = 0;
    state.serial = 0;
    for (opxy::u32 l = 0; l < city::lane_count; ++l) state.lane[l] = 0;
    state.frames = state.acc = state.worst = state.fps = state.worst_shown = 0;
    for (opxy::u32 k = 0; k < city::lot_count; ++k) state.form[k] = state.lit[k] = 0;
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace screen;
    state.notes.update(input);
    const bool held = input.held_notes != 0 && input.held_notes <= 128;
    const i32 dt = clamp(input.elapsed, 0, 102);
    Controls c;
    c.shape = q16(input.slots[0]); c.detune = q16(input.slots[1]); c.bright = q16(input.slots[2]);
    c.reso = q16(input.slots[3]); c.chorus = q16(input.slots[4]); c.ring = q16(input.slots[5]);
    c.touch = q16(input.slots[6]); c.ribbon = q16(input.slots[7]);

    // Touch: each note re-chooses the windows that flare, and they light one by one.
    if (!held) state.hold = 0;
    else if (input.note_serial != state.serial) { state.serial = input.note_serial; state.hold = 0; }
    else state.hold = clamp(state.hold + dt, 0, 8 * unit);

    // The cars: ease up to speed while notes are held, and down to a stop.
    const i32 target = held ? cruise : 0;
    if (input.reduced_motion) state.speed = target;
    else {
        // signed throughout: a slowing car's negative step must stay negative
        const i32 ease = dt * 6 < 1024 ? dt * 6 : 1024;
        state.speed += ((target - state.speed) >> 4) * ease >> 6;
    }
    if (!held && state.speed < unit) state.speed = 0;
    if (state.speed > 0) state.clock = (state.clock + dt) & 0xfffffff;
    state.run += (state.speed * dt) >> 10;
    if (state.run >= course * unit) state.run -= course * unit;
    // The traffic, each lane at its own share of the cars' speed.
    for (u32 l = 0; l < city::lane_count; ++l) {
        state.lane[l] += ((state.speed * city::lanes[l].share) >> 10) * dt >> 10;
        if (state.lane[l] >= city::lane_length * unit) state.lane[l] -= city::lane_length * unit;
    }

    // What moves first; then the city restyles within what's left, the searchlight's points kept back.
    u32 spent = 0;
    draw_cars(state, frame, c, input.reduced_motion != 0, spent);
    draw_traffic(state, frame, spent);
    const u32 left = spent + beam_points < budget ? budget - spent - beam_points : 0;
    draw_city(state, frame, c, held, left, spent);
    draw_beam(state, frame, spent);
    state.fresh = 0;
    readout(state, input, frame);

    const char* const titles[8] = {"shape", "detune", "bright", "reso", "chorus", "ring", "touch", "ribbon"};
    char values[8][header_capacity];
    captions(values, c, input.slots);
    const ShiftRow& row = shift_row(input);
    const char* const shift_titles[8] = {"balance", "octave", "lowcut", "noise", "spread", "ratio", "drift", "slide"};
    char shift_values[8][header_capacity];
    shift_captions(shift_values, row.values);
    page_header(state.header, input, frame, titles, values, 0, page_header_slots, row, shift_titles, shift_values);
}
}
