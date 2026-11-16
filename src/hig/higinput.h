#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace hig {
// Engine button identities recovered from LEVEL_17.REL 0x798ac..0x79a60.
enum PadButton : uint32_t {
    Select = 0x1,
    Start = 0x8,
    Up = 0x10,
    Right = 0x20,
    Down = 0x40,
    Left = 0x80,
    L1 = 0x100,
    R1 = 0x200,
    L2 = 0x400,
    R2 = 0x800,
    Triangle = 0x1000,
    Circle = 0x2000,
    Cross = 0x4000,
    Square = 0x8000,
    L3 = 0x40000,
    R3 = 0x80000
};
struct PadSample {
    uint32_t buttons = 0;
    std::array<uint8_t, 4> sticks{128, 128, 128, 128}; // left X/Y, right X/Y; down-positive Y
};
struct PadState {
    PadSample raw;
    std::array<float, 4> axes{};
    uint32_t pressed = 0, released = 0, held = 0, untouched = ~0u;
    uint32_t left_directions = 0, left_directions_pressed = 0;
};
// Native input semantics, not an emulated pad device. Axis transfer recovered
// from LEVEL_17.REL 0x7a120 / 0x7a140. Default dead-zone is 64/128 = 0.5.
float pad_axis(uint8_t value, uint8_t dead_zone = 64);
uint32_t pad_directions(float x, float y);
// Read the observed digital/analogue packet prefix for oracle fixtures.
// Does not handle device negotiation, pressure mode, masks or game remapping.
bool decode_pad_packet(std::span<const uint8_t> packet, PadSample &sample);
PadState advance_pad(const PadState &previous, const PadSample &next);
} // namespace hig
