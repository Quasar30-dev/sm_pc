#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace hig {
struct CollisionFace {
    std::array<uint16_t, 3> vertices{};
    uint16_t flags = 0;
    std::array<float, 16> planes{}; // face plane followed by three outward edge planes
    uint32_t surface = 0;
};
struct Collision {
    std::vector<std::array<float, 3>> vertices;
    std::vector<CollisionFace> faces;
    std::array<uint32_t, 3> dimensions{};
    std::array<float, 3> cell_size{5, 10, 5}, minimum{}, maximum{};
    std::array<float, 4> sphere{};
    std::vector<uint16_t> cell_counts, cell_faces; // X fastest, then Y, then Z
    std::vector<uint8_t> trailing;                 // preserve unknown padding on read/write
};
bool decode_collision(std::span<const uint8_t> bytes, Collision &out, std::string *error = nullptr);
std::vector<uint8_t> encode_collision(const Collision &collision, std::string *error = nullptr);
bool cook_collision(std::span<const std::array<float, 3>> vertices, std::span<const uint32_t> indices,
                    uint32_t surface, Collision &out, std::string *error = nullptr);
} // namespace hig
