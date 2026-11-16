#pragma once
#include "higwad.h"
namespace hig {
struct MovieInfo {
    std::string path;
    uint32_t width = 0, height = 0, frames = 0, rate = 0, rate_divisor = 0, audio_tracks = 0;
    double duration() const { return rate ? double(frames) * rate_divisor / rate : 0; }
};
bool movie_info(const IsoFs &, const std::string &, MovieInfo &, std::string &error);
} // namespace hig
