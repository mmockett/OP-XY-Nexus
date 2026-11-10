// Nexus's screen checks, compiled for the host and run by python sdk.py test --machine <folder>:
// every frame valid across every control's whole travel, nothing changing at rest, and the
// number of nodes that change a frame while notes are held (the device redraws only those).
#include "../display.cpp"
#include <cstdio>
#include <cstring>

// The Shift row the display reads (the OP-XY fills it for the selected track's synth).
opxy::draw::ShiftRow row;
extern "C" const opxy::draw::ShiftRow* opxy_synth_shift(opxy::u32) { return &row; }

namespace {
using opxy::i32;
using opxy::u32;

nexus::DisplayState state;
nexus::Frame frame, before;

opxy::draw::Input input_of(const i32 (&slots)[8], u32 held, u32 serial) {
    opxy::draw::Input in;
    std::memset(&in, 0, sizeof in);
    for (int k = 0; k < 8; ++k) in.slots[k] = slots[k];
    for (int k = 0; k < 4; ++k) { in.parameters[k] = slots[k]; in.shown[k] = static_cast<opxy::u8>(k); }
    in.elapsed = 34;                       // about 30 frames a second
    in.track = 1;
    in.note_serial = serial;
    in.held_notes = held;
    in.note_off_serial = serial;
    in.amp.attack = in.amp.decay = in.amp.sustain = in.amp.release = opxy::draw::envelope_unavailable;
    return in;
}

u32 changed() {
    u32 n = 0;
    for (u32 k = 0; k < nexus::Frame::node_count; ++k)
        if (std::memcmp(&frame.nodes[k], &before.nodes[k], sizeof frame.nodes[k])) ++n;
    return n;
}

u32 step(const i32 (&slots)[8], u32 held, u32 serial) {
    std::memcpy(&before, &frame, sizeof frame);
    const opxy::draw::Input in = input_of(slots, held, serial);
    nexus::display(state, in, frame);
    return changed();
}

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
}
}

int main() {
    i32 slots[8];
    const double tyrell[8] = {.33, .22, .46, .28, .45, 0, .80, .5};
    for (int k = 0; k < 8; ++k) slots[k] = static_cast<i32>(tyrell[k] * 2147483647.0);
    // The row at its defaults: balance 50, octave 8', lowcut off, noise 6; spread 40, ratio 1.5, drift 25, slide 30.
    const i32 defaults[8] = {0x40000000, 0x2ffffffe, 0, 0x07ae147b, 0x33333333, 0x27fffffd, 0x20000000, 0x26666666};
    std::memset(&row, 0, sizeof row);
    for (int k = 0; k < 8; ++k) row.values[k] = defaults[k];
    nexus::display_reset(state);
    std::memset(&frame, 0, sizeof frame);
    step(slots, 0, 0);
    expect(opxy::draw::valid(frame), "the first frame is invalid");

    // Every control through every value, with the others at the default, at rest and held.
    for (int k = 0; k < 8; ++k) {
        for (int v = 0; v < 128; ++v) {
            i32 s[8];
            for (int j = 0; j < 8; ++j) s[j] = slots[j];
            s[k] = v == 127 ? 0x7fffffff : v << 24;
            for (int f = 0; f < 6; ++f) step(s, v & 1, 1 + v / 16);
            expect(opxy::draw::valid(frame), "a frame is invalid");
        }
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
    for (int f = 0; f < 30; ++f) step(slots, 0, 8);
    u32 held_still = 0;
    for (int f = 0; f < 60; ++f) held_still += step(slots, 0, 8);
    expect(held_still == 0, "the screen changes at rest with Shift held");
    row.held = 0;
    row.line = 0;

    // At rest nothing changes.
    for (int f = 0; f < 120; ++f) step(slots, 0, 9);
    u32 still = 0;
    for (int f = 0; f < 60; ++f) still += step(slots, 0, 9);
    expect(still == 0, "the screen changes at rest");

    // While notes are held: the cars fly, windows flare, pages turn. Measured at the
    // default, at every control up, and at the most flaring (bright 0, touch 100).
    const double settings[3][8] = {
        {.33, .22, .46, .28, .45, 0, .80, .5},
        {1, 1, 1, 1, 1, 1, 1, 1},
        {.33, .22, 0, .28, .45, 0, 1, .5},
    };
    for (int k = 0; k < 3; ++k) {
        i32 s[8];
        for (int j = 0; j < 8; ++j) s[j] = settings[k][j] >= 1 ? 0x7fffffff : static_cast<i32>(settings[k][j] * 2147483647.0);
        for (int f = 0; f < 60; ++f) step(s, 0, 20 + k);
        u32 total = 0, most = 0;
        const int length = 900;            // 30 s: a new note every 2 s, held 1.6 s of it
        for (int f = 0; f < length; ++f) {
            const u32 serial = 30 + k * 100 + f / 60;
            const u32 n = step(s, f % 60 < 48 ? 2u : 0u, serial);
            total += n;
            if (n > most) most = n;
            expect(opxy::draw::valid(frame), "a frame in flight is invalid");
        }
        std::printf("setting %d: %.1f nodes changed a frame on average, at most %u\n", k, total / double(length), static_cast<unsigned>(most));
        expect(total <= 20u * length, "more than 20 changed nodes a frame on average");
        expect(most <= 64u, "a frame changes more than 64 nodes");
    }
    if (!failures) std::puts("ok");
    return failures ? 1 : 0;
}
