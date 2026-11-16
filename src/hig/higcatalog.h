#pragma once
#include "higactor.h"

namespace hig {
struct ActorCatalogEntry {
    std::string name, wad, model_path, animation_path;
    uint32_t hash = 0;
    unsigned clips = 0, bones = 0;
};
// Enumerate compatible model/bank pairs from fileinfo data. This does not
// infer animation identity from a gameplay class or truncate the clip list.
// Shared by Theia and photo mode; source WADs remain immutable.
std::vector<ActorCatalogEntry> catalog_actors(std::span<const uint8_t> wad,
                                              const std::string &source);
} // namespace hig
