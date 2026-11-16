#pragma once
#include <bit>
#include <cstdint>

namespace hig {
// USA LEVEL_01.REL: rocket selectors 3fb58/3fbe0, defaults 38308..38328,
// vertical update 44984..449c4, forward displacement 44a70..44a88.
// Values in the executable are per 60 Hz movement pass (two per SM tick).
inline constexpr float rocket_high_launch = std::bit_cast<float>(uint32_t(0x3e9882ba)) * 60;
inline constexpr float rocket_high_gravity = -std::bit_cast<float>(uint32_t(0xbc4fac8f)) * 3600;
inline constexpr float rocket_long_gravity = -std::bit_cast<float>(uint32_t(0xbb03126f)) * 3600;
inline constexpr float rocket_long_launch = rocket_long_gravity / 60 * std::bit_cast<float>(uint32_t(0x41a00001));
inline constexpr float rocket_long_speed = std::bit_cast<float>(uint32_t(0x3e666665)) * 60;
inline constexpr float rocket_terminal_speed = .38f * 60;
inline constexpr float rocket_high_start_time = .3f;
inline constexpr float rocket_long_start_time = .233333333f;
}
