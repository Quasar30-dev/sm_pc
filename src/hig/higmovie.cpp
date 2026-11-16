#include "higmovie.h"
#include <cstring>
namespace hig {
bool movie_info(const IsoFs &disc, const std::string &path, MovieInfo &out, std::string &error) {
    auto *file = disc.find(path);
    if (!file) {
        error = "Movie is absent from ISO: " + path;
        return false;
    }
    auto b = disc.read_range(*file, 0, 64);
    auto u = [&](unsigned p) {
        uint32_t v = 0;
        if (p + 4 <= b.size())
            std::memcpy(&v, b.data() + p, 4);
        return v;
    };
    if (b.size() < 44 || std::memcmp(b.data(), "BIK", 3) || b[3] < 'f' || b[3] > 'i' ||
        uint64_t(u(4)) + 8 != file->size) {
        error = "Invalid original Bink movie header";
        return false;
    }
    MovieInfo m{file->path, u(20), u(24), u(8), u(28), u(32), u(40)};
    if (!m.width || m.width > 4096 || !m.height || m.height > 2160 || !m.frames ||
        m.frames > 200000 || !m.rate || !m.rate_divisor || double(m.rate) / m.rate_divisor > 120 ||
        m.duration() > 1800 || m.audio_tracks > 16 ||
        44ull + m.audio_tracks * 12ull + (m.frames + 1ull) * 4 > file->size) {
        error = "Unsupported or unbounded Bink dimensions/timing";
        return false;
    }
    out = m;
    error.clear();
    return true;
}
} // namespace hig
