#include "display.h"

// Nexus's screen: a dense isometric city in greys, after a pixel-art picture: an elevated
// monorail with a two-car train, the OP-XY advert tower, a sun billboard and a dot-matrix
// hourglass, a helipad with a flying car over it, masts, a dish, a ball tower, rooftop fans
// and a road with tiny cars. At rest it is still. While notes are held the train runs along
// its track, passing behind the nearer towers and coming round again; the cars drive both ways
// along the road, the fans turn, the dish swings, the flying car bobs and the lights blink.
// Released, it all eases to a stop where it is.
//
// What the controls do to it:
//   shape   the advert's pattern: ramps for the saw, a checker for the pulse and square, bars for the narrow pulse
//   detune  the hourglass's two halves of dark dots drift apart
//   bright  more windows and background lights are lit, in a scattered order, and a fog comes in: one plain
//           sheet of translucent white over the far city, under everything nearer, thicker as bright
//           rises; at 0 there is none, and the far city is dark
//   reso    the advert, the billboards and the neon edges brighten
//   chorus  the train's speed
//   ring    rings on the masts, round the ball and in the dish, faint to bright, one then two; each passes
//           behind its mast or the ball and in front of it
//   swell   how many dark windows glow with each note, and the pattern pulses while a note is held. The
//           glow follows the track's amp envelope: up over its attack, down to its sustain over its decay,
//           out over its release, through the greys between, each window a little after another
//   bite    how high the flying car hovers over the helipad: at 0 it has landed
//   sine    (the Shift row's) the two lines under the advert's pattern, short to long
//   lowcut  (the Shift row's) the sun poster's reflection on the water clears, stripe by stripe from the
//           bottom up; all of it shows at 0, none at 100
// The Shift row's values reach the display whether or not Shift is held.
//
// The Shift row shows in the header while Shift is held.
//
// The city's towers, track and roofs are still artwork, each face clipped to what is seen of
// it, so the screen is filled about once (tools/make_panel.py). What changes is a node, and the
// device pays for every point of a node that changes, so a frame changes at most `budget`
// points: what moves goes first, then the rest restyles within what's left, resuming where it
// left off. The fans' blades and the advert's lines are reshaping outlines (nodes 0..3), redrawn
// whole when they change. Nothing changes at rest.
namespace nexus {
namespace screen {
using namespace opxy::draw;
using opxy::i32;
using opxy::u32;

// Shows the frame rate and the slowest frame in the bottom-left corner, for testing on the device.
const bool show_fps = false;
const u32 fps_text = 16;

const u32 budget = 200;                 // points of outlines changed a frame, at most
const u32 most_nodes = 30;              // and nodes: many changed at once slow the device too
const u32 most_busy = 10;               // and restyled nodes while anything moves or glows: the glow's recolours
const i32 slow = 20, fast = 90;         // the train's speed at chorus 0 and 100, px a second along x
const i32 gap = 90;                     // px of its run the train spends out of sight before it comes round
const i32 rise = 30;                    // how high the craft hovers at bite 100, px (make_panel.py's CRAFT_RISE)
const i32 drift = 5;                    // how far each half of the hourglass moves at detune 100, px (SAND_DRIFT)
const i32 course = city::train_end - city::train_start + gap;   // the train's loop, px
const i32 road_gap = 30;                // px of its run a road car spends out of sight before it comes back
const i32 glow_floor = 1024;            // released, the glow is out below this (1/64, Q16)
const u32 fan_rate = 32768;             // the fans' turn while notes are held, Q16 turns a second: half a turn

static inline u32 hash(i32 i, i32 j, u32 k) {
    u32 h = static_cast<u32>(i) * 374761393u + static_cast<u32>(j) * 668265263u + k * 1442695041u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static inline u32 r16(i32 i, i32 j, u32 k) { return hash(i, j, k) >> 16; }
static inline u32 q16(i32 control) { return control < 0 ? 0u : static_cast<u32>(control) >> 15; }
static inline u32 smallest(u32 a, u32 b) { return a < b ? a : b; }

// sin(2 pi x) for a Q16 turn, Q10: a parabola, close enough for a sway.
static inline i32 sway(u32 turn) {
    const i32 x = static_cast<i32>(static_cast<opxy::i16>(turn & 0xffffu));   // -32768..32767 is -pi..pi
    const i32 a = x < 0 ? -x : x;
    return (x * (32768 - a)) >> 18;                                         // 4 x (1 - |x|), Q10
}

struct Controls { u32 shape, detune, bright, reso, chorus, ring, swell, bite, sine, lowcut; };

// sin(2 pi x) for a Q16 turn, Q14, from city.h's quarter wave.
static inline i32 sine_of(u32 turn) {
    const u32 t = turn & 0xffffu, f = t & 0x3fffu;
    const u32 at = (t & 0x4000u) ? 0x4000u - f : f;                        // 0..16384 along the quarter
    const u32 i = at >> 8, frac = at & 255u;
    const i32 a = city::quarter_sine[i], b = city::quarter_sine[i < 64 ? i + 1 : 64];
    const i32 v = a + (((b - a) * static_cast<i32>(frac)) >> 8);
    return (t & 0x8000u) ? -v : v;
}

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
// A node at home in a grey (0..15): the palette's nearest (city.h's slot).
static inline Node home(u32 k, u32 grey, Paint paint) {
    return pose(city::home_x[k] * unit, city::home_y[k] * unit, unit, city::slot[grey > 15 ? 15 : grey], paint);
}
static inline bool same(const Node& a, const Node& b) {
    return a.x == b.x && a.y == b.y && a.scale == b.scale && a.cos == b.cos && a.sin == b.sin &&
           a.shade == b.shade && a.paint == b.paint;
}

// A grey from low to high by a Q16 amount.
static inline u32 between(u32 low, u32 high, u32 amount) {
    const u32 v = low + ((amount * (high - low + 1)) >> 16);
    return v > high ? high : v;
}
// The fog sheet for bright: hidden at 0, then the palette's white at rising opacity, its fog donors in turn
// up to the fullest at 100.
static inline Node fog(u32 k, u32 bright) {
    if (!bright) return parked();
    return pose(city::home_x[k] * unit, city::home_y[k] * unit, unit, city::fog_donor + ((bright * city::fog_donors) >> 16),
                fill_and_stroke);
}
// The advert's pattern for a shape: 1 ramps (the saw, to 30), 2 a checker (saw and pulse into the square,
// to 85), 3 bars (the narrowing pulse). Pattern 0, circles, is drawn but not shown: shape has no sine now.
static inline u32 pattern_of(u32 shape) { return shape < 19661u ? 1 : shape < 55706u ? 2 : 3; }
// A ring's level, 0 (none) or 1..3 faint to bright: the first ring comes in, then the second.
static inline u32 ring_level(u32 g, u32 slot) {
    if (slot == 0) return g == 0 ? 0 : 1 + smallest(2u, (g * 6) >> 16);
    return g < 32768u ? 0 : 1 + smallest(2u, ((g - 32768u) * 6) >> 16);
}
// While a note is held the pattern pulses, up to two greys brighter at 1.5 Hz, by swell.
static inline u32 pulse(const DisplayState& s, const Controls& c, bool held) {
    if (!held) return 0;
    const u32 t = (static_cast<u32>(s.clock) * 3u / 2u) & 1023u;
    const u32 tri = t < 512u ? t : 1023u - t;
    return (tri * (c.swell >> 8) * 3u) >> 17;
}

// ------------------------------------------------------------------ the city
// What a node that restyles shows for the controls.
static inline Node restyled(u32 k, const DisplayState& s, const Controls& c, bool held) {
    const u32 low = city::low[k], high = city::high[k], p = city::param[k];
    switch (city::kind[k]) {
    case city::window: {
        if ((p << 8) < c.bright) return home(k, high, fill_and_stroke);
        if (s.envelope) {
            // swell: dark windows glow with the envelope, each note its own few (glow_share of them at swell
            // 100), each at its own point of the rise, from its dark grey through the greys to white
            if (!s.glow || r16(static_cast<i32>(k), static_cast<i32>(s.glow_serial), 3) >= ((c.swell * city::glow_share) >> 16))
                return home(k, low, fill_and_stroke);
            i32 lit = 2 * s.glow - static_cast<i32>(r16(static_cast<i32>(k), 0, 40));
            lit = lit < 0 ? 0 : lit > 65536 ? 65536 : lit;
            return home(k, low + (((15u - low) * static_cast<u32>(lit) + r16(static_cast<i32>(k), 0, 41)) >> 16),
                        fill_and_stroke);
        }
        // the envelope unknown: dark windows flare, each note its own few, one after another
        const i32 spread = 154 + static_cast<i32>((c.swell * 973u) >> 16);   // Q10 seconds
        if (held && c.swell && r16(static_cast<i32>(k), static_cast<i32>(s.serial), 3) < (c.swell >> 1) &&
            s.hold >= static_cast<i32>((r16(static_cast<i32>(k), 0, 40) * static_cast<u32>(spread)) >> 16))
            return home(k, 15, fill_and_stroke);
        return home(k, low, fill_and_stroke);
    }
    case city::glow: return home(k, (p << 8) < c.bright ? high : low, fill_and_stroke);
    case city::glyph:
    case city::panel:
    case city::neon: return home(k, between(low, high, c.reso), fill_and_stroke);
    case city::pattern:
        if (p != pattern_of(c.shape)) return parked();
        return home(k, smallest(15u, between(low, high, c.reso) + pulse(s, c, held)), fill_and_stroke);
    case city::sand: {
        // in half pixels, the top half up and the bottom half down
        const i32 d = static_cast<i32>((c.detune * static_cast<u32>(drift) * 2u) >> 16) * (unit / 2);
        return pose(city::home_x[k] * unit, city::home_y[k] * unit + (p ? d : -d), unit, 0, keep);
    }
    case city::ring: {
        // param: its ring (bit 0), and whether it is an outline (bit 1, the dish's) or a filled ribbon
        const u32 level = ring_level(c.ring, p & 1u);
        return level ? home(k, low + (level - 1) * (high - low) / 2, (p & 2u) ? stroke : fill_and_stroke) : parked();
    }
    case city::fog: return fog(k, c.bright);
    case city::stripe: {   // the sun's reflection, cleared from the bottom stripe up as lowcut's step rises
        const u32 step = (c.lowcut * 100u + 32768u) >> 16;
        return step * (city::sun_stripes + 1) <= (p + 1) * 100u ? home(k, 0, keep) : parked();
    }
    default: return home(k, 0, keep);
    }
}

// The windows' glow, Q16, through the track's amp envelope: on a note's start up at attack's rate to full,
// then down to sustain at decay's; released, out at release's, to nothing. Stock reads each word's top 7
// bits; release's runs the other way. Unknown, the glow is off and the windows flare as they did before.
static inline i32 fall(i32 level, i32 to, u32 index, i32 dt) {
    i32 k = (city::fall_rate[index] >> 4) * dt >> 6;                      // Q16 of the way there this frame
    k = k > 65536 ? 65536 : k;
    const i32 step = ((level - to) >> 4) * (k >> 4) >> 8;
    return level - (step ? step : level > to ? 1 : level < to ? -1 : 0);   // never stuck short of it
}
static inline void glow(DisplayState& s, const Input& in, bool held, i32 dt) {
    s.envelope = envelope_known(in.amp) ? 1u : 0u;
    if (!s.envelope) { s.glow = 0; s.stage = 0; return; }
    const u32 attack = static_cast<u32>(in.amp.attack < 0 ? 0 : in.amp.attack) >> 24;
    const u32 decay = static_cast<u32>(in.amp.decay < 0 ? 0 : in.amp.decay) >> 24;
    const u32 release = static_cast<u32>(in.amp.release < 0 ? 0 : in.amp.release) >> 24;
    const i32 sustain = (in.amp.sustain < 0 ? 0 : in.amp.sustain) >> 15;   // Q16
    if (held) {
        if (s.stage == 0 || s.stage == 3 || in.note_serial != s.glow_serial) { s.glow_serial = in.note_serial; s.stage = 1; }
        if (s.stage == 1) {
            s.glow += (city::attack_rate[attack] >> 6) * dt >> 4;
            if (s.glow >= 65536) { s.glow = 65536; s.stage = 2; }
        }
        if (s.stage == 2) s.glow = fall(s.glow, sustain, decay ? decay : 1, dt);
    } else if (s.stage) {
        s.stage = 3;
        s.glow = fall(s.glow, 0, 127 - release, dt);
        if (s.glow < glow_floor) { s.glow = 0; s.stage = 0; }
    }
}

// What a node that moves shows: on the clock, which runs only while the train does.
static inline Node moved(u32 k, const DisplayState& s, const Controls& c, bool held) {
    const u32 clock = static_cast<u32>(s.clock), p = city::param[k];
    switch (city::kind[k]) {
    case city::light:   // on for about a quarter of every second, each at its own phase
        return home(k, ((clock + p * 4u) & 1023u) < 230u ? city::high[k] : city::low[k], fill_and_stroke);
    case city::dish: {  // swings up to 8 degrees either way, a whole degree at a time
        Node n = home(k, 0, keep);
        const Turn t = rotation(((sway(clock * 5u) * 8) >> 10) * 10);
        n.cos = t.cos;
        n.sin = t.sin;
        return n;
    }
    case city::craft:
    case city::shadow: {
        const i32 up = static_cast<i32>((c.bite * static_cast<u32>(rise)) >> 16);   // whole px
        if (city::kind[k] == city::shadow)
            return pose(city::home_x[k] * unit, city::home_y[k] * unit, unit - up * 10, 0, keep);
        const i32 bob = held && up ? (sway(clock * 41u) + 512) >> 10 : 0;          // -1, 0 or 1 px
        Node n = pose(city::home_x[k] * unit, (city::home_y[k] - up + bob) * unit, unit, 0, keep);
        if (p) {   // its beacon
            n.shade = city::slot[((clock + 300u) & 1023u) < 120u ? city::high[k] : city::low[k]];
            n.paint = fill_and_stroke;
        }
        return n;
    }
    case city::train: {
        if (s.run >= (city::train_end - city::train_start) * unit) return parked();
        const i32 d = city::train_start * unit + s.run;
        return pose(city::home_x[k] * unit + d, city::home_y[k] * unit + (d >> 1), unit, 0, keep);
    }
    case city::car: {   // along the road, up it to the right or down it to the left, then out of sight a while
        const i32 run = s.road[p], length = (city::car_end[p] - city::car_start[p]) * unit;
        if (run > length) return parked();
        const i32 d = city::car_way[p] > 0 ? city::car_start[p] * unit + run : city::car_end[p] * unit - run;
        return pose(city::home_x[k] * unit + d, city::home_y[k] * unit - (d >> 1), unit, 0, keep);
    }
    default: return home(k, 0, keep);
    }
}

// Brings the nodes that restyle to what the controls ask, within `left` points and `count_left`
// nodes; resumes next frame at the first that didn't fit.
static inline void restyle(DisplayState& s, Frame& frame, const Controls& c, bool held, u32 left, u32 count_left,
                           u32& spent) {
    const u32 first = city::first, count = city::restyle_end - first;
    u32 k = s.cursor < first || s.cursor >= city::restyle_end ? first : s.cursor;
    for (u32 n = 0; n < count; ++n, k = k + 1 == city::restyle_end ? first : k + 1) {
        const Node want = restyled(k, s, c, held);
        if (same(frame.nodes[k], want)) continue;
        const u32 cost = city::points[k];
        if (!s.fresh && (cost > left || !count_left)) break;
        if (count_left) --count_left;
        frame.nodes[k] = want;
        left = cost > left ? 0 : left - cost;
        spent += cost;
    }
    s.cursor = k;
}

// The reshaping outlines. Each fan's three blades in its roof's plane, turned by `turn` (Q16): a
// blade's root near the hub and its two tips, (u, v) in the roof to (u - v, (u + v) / 2) on screen.
static inline void blades(Frame& frame, u32 f, u32 turn) {
    const u32 k = f;
    const i32 reach = city::fan_reach[f];
    u32 at = 0;
    for (u32 b = 0; b < 3; ++b) {
        const u32 a0 = turn + b * 21845u;
        const u32 angle[3] = {a0 - static_cast<u32>(city::fan_root), a0 - static_cast<u32>(city::fan_tip),
                              a0 + static_cast<u32>(city::fan_tip)};
        for (u32 j = 0; j < 3; ++j) {
            const i32 r = j ? reach : city::fan_hub;
            const i32 u = (r * sine_of(angle[j] + 16384u)) >> 14, v = (r * sine_of(angle[j])) >> 14;
            frame.paths[k].points[at].x = city::fan_x[f] + u - v;
            frame.paths[k].points[at].y = city::fan_y[f] + ((u + v) >> 1);
            ++at;
        }
    }
    frame.paths[k].key = (k << 16 | (turn & 0xffffu)) + 1u;
}
// The advert's two lines, a bracket: as long as sine's step (0..100) asks, in its face's units
// (u, w) to (u, u / 2 + w) on screen.
static inline void lines(Frame& frame, u32 step) {
    const u32 k = city::lines;
    const i32 l1 = city::line_length[0][0] + (city::line_length[0][1] - city::line_length[0][0]) * static_cast<i32>(step) / 100;
    const i32 l2 = city::line_length[1][0] + (city::line_length[1][1] - city::line_length[1][0]) * static_cast<i32>(step) / 100;
    const i32 g = city::line_gap, h = city::line_height, sp = city::line_spine;
    const i32 u[9] = {0, l1, l1, sp, sp, l2, l2, 0, 0};
    const i32 w[9] = {0, 0, h, h, g, g, g + h, g + h, (g + h) >> 1};
    for (u32 j = 0; j < 9; ++j) {
        frame.paths[k].points[j].x = city::line_x + u[j];
        frame.paths[k].points[j].y = city::line_y + (u[j] >> 1) + w[j];
    }
    frame.paths[k].key = (k << 16 | step) + 1u;
}
// Brings the reshaping outlines to the fans' turn and sine's step; returns the points redrawn and
// counts the outlines changed.
static inline u32 reshape(DisplayState& s, Frame& frame, const Controls& c, u32& count) {
    u32 spent = 0;
    for (u32 f = 0; f < city::fans; ++f) {
        const u32 turn = (s.spin + f * 9000u) & 0xffffu;   // each fan at its own angle
        const Node want = home(f, 9, stroke);
        if (!s.fresh && frame.paths[f].key == (f << 16 | turn) + 1u && same(frame.nodes[f], want)) continue;
        blades(frame, f, turn);
        frame.nodes[f] = want;
        spent += city::path_points;
        ++count;
    }
    const u32 step = (c.sine * 100u + 32768u) >> 16;
    const Node want = home(city::lines, between(city::low[city::lines], city::high[city::lines], c.reso), stroke);
    if (s.fresh || frame.paths[city::lines].key != ((city::lines << 16) | step) + 1u || !same(frame.nodes[city::lines], want)) {
        lines(frame, step);
        frame.nodes[city::lines] = want;
        spent += city::path_points;
        ++count;
    }
    return spent;
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
// The page's captions, by slot: detune in cents, bright in hertz; shape, reso, chorus, ring,
// swell and bite as amounts, 0..100.
static inline void captions(char (&values)[8][header_capacity], const Controls& c, const i32* slots) {
    for (u32 i = 0; i < 8; ++i) header_amount(values[i], header_capacity, slots[i]);
    // detune: 0c .. 30c
    number(values[1], static_cast<i32>((power15(c.detune) * 30u + 32768u) >> 16));
    append(values[1], "c");
    // bright: 80 Hz .. 16 kHz, 7.64 octaves
    hertz_caption(values[2], times_exp2(80u, (c.bright * 7644u) / 1000u));
}

// The Shift row's captions, by entry: sine, octave, lowcut, noise; balance, ratio, spread, drift.
// Octave in feet, lowcut in hertz, ratio as a ratio; the rest as amounts, 0..100.
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
}
}

inline void display_reset(DisplayState& state) {
    state.notes.reset();
    state.header.reset();
    state.fresh = 1;
    state.cursor = city::first;
    state.run = -city::train_start * opxy::draw::unit;   // the train at home
    state.speed = 0;
    state.clock = 0;
    state.hold = 0;
    state.serial = 0;
    state.glow = 0;
    state.stage = 0;
    state.glow_serial = 0;
    state.envelope = 0;
    state.frames = state.acc = state.worst = state.fps = state.worst_shown = 0;
    state.pace = 0;
    state.spin = 0;
    for (opxy::u32 k = 0; k < city::cars; ++k) state.road[k] = city::car_phase[k] * opxy::draw::unit;
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace screen;
    state.notes.update(input);
    const bool held = input.held_notes != 0 && input.held_notes <= 128;
    const i32 dt = clamp(input.elapsed, 0, 102);
    Controls c;
    c.shape = q16(input.slots[0]); c.detune = q16(input.slots[1]); c.bright = q16(input.slots[2]);
    c.reso = q16(input.slots[3]); c.chorus = q16(input.slots[4]); c.ring = q16(input.slots[5]);
    c.swell = q16(input.slots[6]); c.bite = q16(input.slots[7]);
    // The Shift row's values reach the display whether or not Shift is held (opxy/synth_shift.hpp).
    const ShiftRow& row = shift_row(input);
    c.sine = q16(row.values[0]);
    c.lowcut = q16(row.values[2]);

    // Swell: each note re-chooses the windows that flare, and they light one by one.
    if (!held) state.hold = 0;
    else if (input.note_serial != state.serial) { state.serial = input.note_serial; state.hold = 0; }
    else state.hold = clamp(state.hold + dt, 0, 8 * unit);
    glow(state, input, held, dt);

    // The train: eases up to chorus's speed while notes are held, and down to a stop.
    const i32 target = held ? (slow + static_cast<i32>((c.chorus * static_cast<u32>(fast - slow)) >> 16)) * unit : 0;
    if (input.reduced_motion) state.speed = target;
    else {
        // signed throughout: a slowing train's negative step must stay negative
        const i32 ease = dt * 6 < 1024 ? dt * 6 : 1024;
        state.speed += ((target - state.speed) >> 4) * ease >> 6;
    }
    if (!held && state.speed < unit) state.speed = 0;
    if (state.speed > 0) state.clock = (state.clock + dt) & 0xfffffff;
    state.run += (state.speed * dt) >> 10;
    if (state.run >= course * unit) state.run -= course * unit;

    // The fans and the road's cars: on one pace that eases to 1 while notes are held, and to 0.
    const i32 aim = held ? unit : 0;
    if (input.reduced_motion) state.pace = aim;
    else state.pace += (((aim - state.pace) >> 3) * (dt * 6 < 1024 ? dt * 6 : 1024)) >> 8;
    if (!held && state.pace < unit / 16) state.pace = 0;
    state.spin = (state.spin + ((fan_rate * static_cast<u32>(state.pace) * static_cast<u32>(dt)) >> 20)) & 0xffffu;
    for (u32 k = 0; k < city::cars; ++k) {
        state.road[k] += (city::car_speed[k] * state.pace * dt) >> 10;
        const i32 loop = (city::car_end[k] - city::car_start[k] + road_gap) * unit;
        if (state.road[k] >= loop) state.road[k] -= loop;
    }

    // What moves first, and the reshaping outlines; then the rest restyles within what's left.
    u32 spent = 0, count = 0;
    for (u32 k = city::restyle_end; k < city::node_count; ++k) {
        const Node want = moved(k, state, c, held);
        if (same(frame.nodes[k], want)) continue;
        frame.nodes[k] = want;
        spent += city::points[k];
        ++count;
    }
    spent += reshape(state, frame, c, count);
    // While anything moves or glows, the restyling (the glow's windows, mostly) takes at most most_busy nodes
    // a frame: the device pays for each changed node, on top of what moves.
    const u32 room = count < most_nodes ? most_nodes - count : 0;
    const bool busy = held || state.stage || state.speed || state.pace;
    restyle(state, frame, c, held, spent < budget ? budget - spent : 0, busy ? smallest(room, most_busy) : room, spent);
    state.fresh = 0;
    readout(state, input, frame);

    // The titles are definition.json's labels (city.h, from make_panel.py).
    char values[8][header_capacity];
    captions(values, c, input.slots);
    char shift_values[8][header_capacity];
    shift_captions(shift_values, row.values);
    page_header(state.header, input, frame, city::titles, values, city::bars, page_header_slots, row, city::shift_titles,
                shift_values);
}
}
