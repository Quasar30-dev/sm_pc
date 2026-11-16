#include "higinput.h"
#include <cmath>
namespace hig {
float pad_axis(uint8_t value, uint8_t dead_zone) {
    float x = (float(value) / 255.f) * 2.f - 1.f;
    float zone = float(dead_zone) * (1.f / 128.f);
    if (zone >= 1 || std::abs(x) <= zone)
        return 0;
    return (x > 0 ? x - zone : x + zone) / (1.f - zone);
}
uint32_t pad_directions(float x, float y) {
    return (x > 0 ? Right : x < 0 ? Left : 0u) | (y > 0 ? Down : y < 0 ? Up : 0u);
}
bool decode_pad_packet(std::span<const uint8_t> packet, PadSample &out) {
    if (packet.size() < 4 || packet[0] != 0)
        return false;
    unsigned payload = (packet[1] & 15) * 2;
    if (payload < 2 || packet.size() < payload + 2)
        return false;
    PadSample s;
    uint8_t a = uint8_t(~packet[2]), b = uint8_t(~packet[3]);
    s.buttons = (a & 0xf9u) | ((a & 2u) ? L3 : 0u) | ((a & 4u) ? R3 : 0u) | ((b & 3u) << 10) |
                ((b & 12u) << 6) | ((b & 0xf0u) << 8);
    if (payload >= 6)
        s.sticks = {packet[6], packet[7], packet[4], packet[5]};
    out = s;
    return true;
}
PadState advance_pad(const PadState &previous, const PadSample &next) {
    PadState s;
    s.raw = next;
    uint32_t changed = previous.raw.buttons ^ next.buttons;
    s.pressed = changed & next.buttons;
    s.released = changed & previous.raw.buttons;
    s.held = previous.raw.buttons & next.buttons;
    s.untouched = ~(previous.raw.buttons | next.buttons);
    for (int k = 0; k < 4; k++)
        s.axes[k] = pad_axis(next.sticks[k]);
    s.left_directions = pad_directions(s.axes[0], s.axes[1]);
    s.left_directions_pressed = s.left_directions & ~previous.left_directions;
    return s;
}
} // namespace hig
