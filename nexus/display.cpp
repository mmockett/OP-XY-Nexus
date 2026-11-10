#include "display.h"

// Nexus's screen: an isometric night city, and a white flying car with its
// wingman that crosses it while notes are held. At the screen's edge the city
// turns the page: the next stretch wipes in, lot by lot, and the car comes
// back to the left over it. Each page is a hash of each lot's place in the
// endless city (tools/make_panel.py mirrors this, to draw previews).
//
// What the controls do to it:
//   shape   the skyline: rolling for sine, ramps for saw, blocks for square, spikes for pulse
//   detune  the wingman (layer II) drifts off the leader's line
//   bright  the share of lit windows
//   reso    the neon strips brighten
//   chorus  the two cars weave apart and back, on the chorus's clock
//   ring    rings on the masts, faint to bright, one then two
//   touch   dark windows flare, one after another, while a note is held
//   ribbon  the car's height
//
// The Shift row shows in the header while Shift is held; the city doesn't draw it.
//
// Nothing changes at rest. While flying, the cars' eleven nodes move each
// frame; a page turn redraws at most four lots a frame.
namespace nexus {
namespace screen {
using namespace opxy::draw;
using opxy::i32;
using opxy::u32;

const i32 c30 = 887;                    // cos 30 degrees, Q10
const i32 cruise = 120 * unit;          // the car's speed while held, world px a second
const i32 page_length = 256 * unit;     // a page: four lots along x
const u32 most_lots = 4;                // lots redrawn in one frame, besides the first
const u32 wipe_frames = 8;

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

struct Controls { u32 shape, detune, bright, reso, chorus, ring, touch, ribbon; };

// What a lot shows on a page, packed: its scale (bits 0-5), tier (6), four
// windows (7-14: 0 dark, 1 lit, 2 flaring), neon (15-17), mast (18), rings
// (19-22), sign (23) and the sign's light (24).
static inline u32 desired(const city::Lot& lot, u32 page, const Controls& c, bool held, i32 hold, u32 serial) {
    const i32 wi = lot.i + 4 * static_cast<i32>(page), wj = lot.j;
    const u32 d = r16((wi + 1000) / 5 - 200, (wj + 1000) / 4 - 250, 91);
    const u32 district = d < 32768u ? 0 : d < 52429u ? 1 : 2;
    const u32 sky = wave(static_cast<u32>(wi * 9362 + wj * 5041) & 0xffffu, c.shape);
    const u32 mix = (sky * 39322u + r16(wi, wj, 1) * 26214u) >> 16;
    const u32 code = district == 0 ? 24 + ((mix * 18) >> 16) : district == 1 ? 20 + ((mix * 11) >> 16) : 18 + ((mix * 8) >> 16);
    const u32 r2 = r16(wi, wj, 2);
    const u32 tier = (district == 0 && r2 < 39322u) || (district == 1 && r2 < 22938u);
    u32 state = code | tier << 6;
    const i32 rise = 154 + static_cast<i32>((c.touch * 973u) >> 16);   // the bloom's rise, Q10 seconds
    for (u32 k = 0; k < 4; ++k) {
        u32 w = 0;
        if (r16(wi, wj, 20 + k) < c.bright) w = 1;
        else if (held && c.touch && r16(wi * 7 + static_cast<i32>(k), wj, serial) < (c.touch >> 2) &&
                 hold >= static_cast<i32>((r16(wi, wj, 40 + k) * static_cast<u32>(rise)) >> 16)) w = 2;
        state |= w << (7 + 2 * k);
    }
    if (r16(wi, wj, 6) > 36045u) state |= smallest(4u, 1 + ((c.reso * 4) >> 16)) << 15;
    if (lot.mast && tier) {
        const u32 g = c.ring;
        const u32 first = g == 0 ? 0 : 1 + smallest(2u, (g * 6) >> 16);
        const u32 second = g < 32768u ? 0 : 1 + smallest(2u, ((g - 32768u) * 6) >> 16);
        state |= 1u << 18 | first << 19 | second << 21;
    }
    if (lot.sign && r16(wi, wj, 7) > 26214u) {
        state |= 1u << 23;
        if (r16(wi, wj, 8) < c.bright + 9830u) state |= 1u << 24;
    }
    return state;
}

template <class F>
static inline void put(F& frame, u32 node, i32 x, i32 y, i32 scale, u32 shade, Paint paint) {
    const i32 nx = clamp(x, node_left * unit, node_right * unit), ny = clamp(y, node_top * unit, node_bottom * unit);
    place(frame, node, nx, ny, scale, shade, paint);
}

// One node of a lot: every node scales about the lot's pivot, so its parts stay together.
static inline void show(Frame& frame, u32 node, const city::Lot& lot, i32 s, u32 shade, Paint paint) {
    put(frame, node, lot.x * unit + s * city::origin_x[node], lot.y * unit + s * city::origin_y[node], s, shade, paint);
}

// Sets a lot's nodes to a packed state.
static inline void draw_lot(Frame& frame, const city::Lot& lot, u32 state) {
    const i32 s = static_cast<i32>(state & 63u) * 32;               // Q10 scale, 0.56 .. 1.28
    u32 node = lot.first;
    show(frame, node++, lot, s, 0, keep);                           // body
    show(frame, node++, lot, s, 0, keep);                           // roof
    for (u32 k = 0; k < 4; ++k, ++node) {
        const u32 w = (state >> (7 + 2 * k)) & 3u;
        if (w) show(frame, node, lot, s, w == 2 ? 3 : (k & 1u) ? 2 : 1, fill_and_stroke);
        else hide(frame, node);
    }
    const u32 neon = (state >> 15) & 7u;
    if (neon) show(frame, node, lot, s, 3 + neon, fill_and_stroke); else hide(frame, node);
    ++node;
    if (lot.sign) {
        if (state >> 23 & 1u) show(frame, node, lot, s, 0, keep); else hide(frame, node);
        ++node;
        if (state >> 24 & 1u) show(frame, node, lot, s, 11, fill_and_stroke); else hide(frame, node);
        ++node;
    }
    const bool tier = state >> 6 & 1u;
    for (u32 k = 0; k < 2; ++k, ++node) {
        if (tier) show(frame, node, lot, s, 0, keep); else hide(frame, node);
    }
    if (lot.mast) {
        if (state >> 18 & 1u) show(frame, node, lot, s, 0, keep); else hide(frame, node);
        ++node;
        for (u32 k = 0; k < 2; ++k, ++node) {
            const u32 ring = (state >> (19 + 2 * k)) & 3u;
            if (ring) show(frame, node, lot, s, 7 + ring, stroke); else hide(frame, node);
        }
    }
}

// sin(2 pi x) for a Q16 turn, Q10: a parabola, close enough for a sway.
static inline i32 sway(u32 turn) {
    const i32 x = static_cast<i32>(static_cast<opxy::i16>(turn & 0xffffu));   // -32768..32767 is -pi..pi
    const i32 a = x < 0 ? -x : x;
    return (x * (32768 - a)) >> 18;                                         // 4 x (1 - |x|), Q10
}

// The cars: the leader, and the wingman (layer II) off its line by detune.
static inline void draw_cars(Frame& frame, const DisplayState& state, const Controls& c, bool flying) {
    const i32 wx = 150 * unit + state.car;                              // world x, Q10
    const i32 height = 170 * unit + static_cast<i32>((c.ribbon * 30u) >> 6);  // world z, Q10 (ribbon: 0..30 px up)
    const i32 hover = flying ? (sway(static_cast<u32>(state.clock) * 31u) * 4) / 5 : 0;   // 0.8 px at about 0.5 Hz
    // chorus: the cars weave apart, up to 10 world px, on the chorus's 0.9 Hz clock
    const i32 weave = (static_cast<i32>(c.chorus >> 6) * 10 * sway(static_cast<u32>(state.clock) * 58u)) >> 10;
    const i32 ly = 300 * unit + weave;
    const i32 sx = 240 * unit + (((wx - ly) >> 4) * c30 >> 6);
    const i32 sy = 40 * unit + ((wx + ly) >> 1) - height - hover;
    for (u32 k = 0; k < city::lead_nodes; ++k) {
        const u32 n = city::car_first + k;
        put(frame, n, sx + city::lead_x * unit, sy + city::lead_y * unit, unit, 0, keep);
    }
    const i32 apart = static_cast<i32>((c.detune * 24u) >> 6);          // Q10 world px, 0..24
    const bool wing = c.detune > 1966u;
    const i32 wx2 = wx - 50 * unit, wy2 = 320 * unit + apart - weave;
    const i32 wz = height - 10 * unit + apart * 2 / 5;
    const i32 tx = 240 * unit + (((wx2 - wy2) >> 4) * c30 >> 6);
    const i32 ty = 40 * unit + ((wx2 + wy2) >> 1) - wz + hover;
    for (u32 k = 0; k < city::wing_nodes; ++k) {
        const u32 n = city::car_first + city::lead_nodes + k;
        if (wing) put(frame, n, tx + city::wing_x * unit, ty + city::wing_y * unit, unit, 0, keep);
        else hide(frame, n);
    }
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
    state.page = 0;
    state.wipe = -1;
    state.car = 0;
    state.speed = 0;
    state.clock = 0;
    state.hold = 0;
    state.serial = 0;
    for (opxy::u32 k = 0; k < city::lot_count; ++k) state.shown[k] = 0;
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

    // The car: eases up to speed while notes are held, and down to a stop.
    const i32 target = held ? cruise : 0;
    if (input.reduced_motion) state.speed = target;
    else {
        // signed throughout: a slowing car's negative step must stay negative
        const i32 ease = dt * 6 < 1024 ? dt * 6 : 1024;
        state.speed += ((target - state.speed) >> 4) * ease >> 6;
    }
    if (!held && state.speed < unit) state.speed = 0;
    const bool flying = state.speed > 0;
    if (flying) state.clock = (state.clock + dt) & 0xfffffff;
    if (state.wipe < 0) {
        state.car += (state.speed >> 5) * dt >> 5;
        if (state.car >= page_length) state.wipe = 0;
    }

    // The city: each lot shows its page; during a page turn, lots left of the edge show the next.
    const i32 edge = state.wipe >= 0 ? -60 + (state.wipe + 1) * 75 : -1000;
    u32 budget = state.fresh ? city::lot_count : most_lots;
    for (u32 k = 0; k < city::lot_count && budget; ++k) {
        const city::Lot& lot = city::lots[k];
        const u32 page = state.page + (lot.x < edge ? 1u : 0u);
        const u32 want = desired(lot, page, c, held, state.hold, state.serial);
        if (want == state.shown[k]) continue;
        draw_lot(frame, lot, want);
        state.shown[k] = want;
        --budget;
    }
    if (state.wipe >= 0 && ++state.wipe >= static_cast<i32>(wipe_frames)) {
        state.wipe = -1;
        state.page += 1;
        state.car -= page_length;
    }
    draw_cars(frame, state, c, flying && !input.reduced_motion);
    state.fresh = 0;

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
