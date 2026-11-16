#pragma once
#include "higlevel.h"
namespace hig {
struct FrontendImage {
    uint32_t file_offset = 0, byte_size = 0;
    std::string source_path;
    GimImage image;
};
// FRONTEND.REL contains embedded presentation DATA as well as original code.
// Enumerate bounded GIM containers; decoding does not execute the module.
bool decode_frontend_images(std::span<const uint8_t>, std::vector<FrontendImage> &,
                            std::string *error = nullptr);
} // namespace hig
