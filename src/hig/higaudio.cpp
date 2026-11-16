#include "higaudio.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace hig {
namespace {
struct Read {
    std::span<const uint8_t> b;
    std::span<const uint8_t> span(size_t p, size_t n) const {
        if (p > b.size() || n > b.size() - p)
            throw std::runtime_error("Audio read exceeds asset");
        return b.subspan(p, n);
    }
    uint32_t u32(size_t p) const {
        uint32_t n;
        auto s = span(p, 4);
        std::memcpy(&n, s.data(), 4);
        return n;
    }
    uint16_t u16(size_t p) const {
        uint16_t n;
        auto s = span(p, 2);
        std::memcpy(&n, s.data(), 2);
        return n;
    }
    int8_t s8(size_t p) const { return int8_t(span(p, 1)[0]); }
    float f32(size_t p) const {
        auto n = u32(p);
        float f;
        std::memcpy(&f, &n, 4);
        if (!std::isfinite(f))
            throw std::runtime_error("Nonfinite audio value");
        return f;
    }
};
bool failed(std::string *error, const std::exception &e) {
    if (error)
        *error = e.what();
    return false;
}
uint32_t signature(std::string s) {
    uint32_t crc = 0xffffffff;
    for (unsigned char ch : s) {
        if (ch >= 'a' && ch <= 'z')
            ch -= 32;
        crc ^= ch;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
    return ~crc;
}
} // namespace
bool decode_ps_adpcm(std::span<const uint8_t> data, AudioSample &out, std::string *error) {
    out = {};
    try {
        if (data.empty() || data.size() % 16 || data.size() > 64 * 1024 * 1024)
            throw std::runtime_error("Invalid PS ADPCM extent");
        constexpr int coeff[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
        int h1 = 0, h2 = 0;
        bool loop_start = false;
        out.pcm.reserve(data.size() / 16 * 28);
        for (size_t p = 0; p < data.size(); p += 16) {
            unsigned filter = data[p] >> 4, shift = data[p] & 15, flags = data[p + 1];
            if (filter > 4 || shift > 12)
                throw std::runtime_error("Invalid PS ADPCM predictor/shift");
            if (flags & 4) {
                out.loop_start = out.pcm.size();
                loop_start = true;
            }
            for (unsigned k = 0; k < 28; k++) {
                int nibble = (data[p + 2 + k / 2] >> ((k & 1) * 4)) & 15;
                if (nibble >= 8)
                    nibble -= 16;
                int sample = ((nibble * 4096) >> shift) +
                             ((h1 * coeff[filter][0] + h2 * coeff[filter][1] + 32) >> 6);
                sample = std::clamp(sample, -32768, 32767);
                h2 = h1;
                h1 = sample;
                out.pcm.push_back(int16_t(sample));
            }
            if (flags & 1) {
                if ((flags & 2) && loop_start)
                    out.loop_end = out.pcm.size();
                break;
            }
        }
        if (error)
            error->clear();
        return true;
    } catch (const std::exception &e) {
        out = {};
        return failed(error, e);
    }
}
bool decode_vag(std::span<const uint8_t> data, AudioSample &out, std::string *error) {
    try {
        Read r{data};
        if (r.u32(0) != 0x70474156)
            throw std::runtime_error("Unsupported VAG magic");
        auto be = [&](size_t p) {
            auto s = r.span(p, 4);
            return uint32_t(s[0]) << 24 | uint32_t(s[1]) << 16 | uint32_t(s[2]) << 8 | s[3];
        };
        unsigned rate = be(0x10);
        if (rate < 4000 || rate > 192000)
            throw std::runtime_error("Invalid VAG sample rate");
        if (!decode_ps_adpcm(r.span(0x30, be(0xc)), out, error))
            return false;
        out.rate = rate;
        return true;
    } catch (const std::exception &e) {
        out = {};
        return failed(error, e);
    }
}
bool load_music(const IsoFs &disc, const std::string &name, AudioSample &out, std::string *error) {
    out = {};
    try {
        const auto *file = disc.find("/VAGS.WAD");
        if (!file)
            throw std::runtime_error("Missing streamed audio archive");
        auto header = disc.read_range(*file, 0, 4);
        unsigned n = Read{header}.u32(0);
        if (n > 100000 || uint64_t(n) * 12 + 4 > file->size)
            throw std::runtime_error("Invalid VAGS directory");
        auto directory = disc.read_range(*file, 4, n * 12);
        Read r{directory};
        AudioSample channels[2];
        for (unsigned c = 0; c < 2; c++) {
            unsigned hash = signature(name + (c ? "_R.VAG" : "_L.VAG"));
            bool found = false;
            for (unsigned i = 0; i < n; i++)
                if (r.u32(i * 12) == hash) {
                    auto size = r.u32(i * 12 + 4), off = r.u32(i * 12 + 8);
                    if (size > 64 * 1024 * 1024 || uint64_t(off) + size > file->size)
                        throw std::runtime_error("Stream exceeds archive");
                    if (!decode_vag(disc.read_range(*file, off, size), channels[c], error))
                        return false;
                    found = true;
                    break;
                }
            if (!found)
                throw std::runtime_error("Missing music channel: " + name);
        }
        if (channels[0].rate != channels[1].rate ||
            channels[0].pcm.size() != channels[1].pcm.size())
            throw std::runtime_error("Stereo music channel mismatch");
        out.rate = channels[0].rate;
        out.channels = 2;
        out.pcm.resize(channels[0].pcm.size() * 2);
        for (size_t i = 0; i < channels[0].pcm.size(); i++)
            for (unsigned c = 0; c < 2; c++)
                out.pcm[2 * i + c] = channels[c].pcm[i];
        // LevelMusic requests the stream loop flag even if VAG block loop bits are absent.
        out.loop_start = channels[0].loop_start;
        out.loop_end = channels[0].pcm.size();
        return true;
    } catch (const std::exception &e) {
        out = {};
        return failed(error, e);
    }
}
bool decode_sound_bank(std::span<const uint8_t> data, SoundBank &out, std::string *error) {
    out.sounds.clear();
    try {
        Read file{data};
        if (file.u32(0) != 3 || file.u32(4) != 2)
            throw std::runtime_error("Unsupported Scream bank container");
        Read r{file.span(file.u32(8), file.u32(12))};
        auto samples = file.span(file.u32(16), file.u32(20));
        if (r.u32(0) != 0x6b6c4253 || r.u32(4) != 3)
            throw std::runtime_error("Unsupported SBlk version");
        unsigned n = r.u16(0x16), grains = r.u16(0x18);
        size_t so = r.u32(0x1c), go = r.u32(0x20), gd = r.u32(0x34);
        r.span(so, size_t(n) * 12);
        r.span(go, size_t(grains) * 8);
        std::unordered_map<uint64_t, std::shared_ptr<AudioSample>> cache;
        out.sounds.resize(n);
        for (unsigned i = 0; i < n; i++) {
            size_t s = so + i * 12;
            unsigned count = uint8_t(r.s8(s + 4));
            size_t first = r.u32(s + 8);
            if (count && (first % 8 || first / 8 + count > grains))
                throw std::runtime_error("Scream grain run exceeds bank");
            auto &sound = out.sounds[i];
            sound.volume = std::clamp(int(r.s8(s)), 0, 127) / 127.f;
            for (unsigned j = 0; j < count; j++) {
                size_t p = go + first + j * 8;
                SoundGrain grain;
                grain.opcode = r.u32(p);
                grain.delay = int32_t(r.u32(p + 4));
                if (grain.delay < 0 || grain.delay > 240 * 60)
                    throw std::runtime_error("Unsupported Scream grain delay");
                if (grain.opcode >> 24 == 1) {
                    size_t t = gd + (grain.opcode & 0xffffff);
                    r.span(t, 24);
                    uint32_t off = r.u32(t + 16), size = r.u32(t + 20);
                    if (off > samples.size() || size > samples.size() - off)
                        throw std::runtime_error("Scream sample exceeds bank");
                    uint64_t key = uint64_t(off) << 32 | size;
                    if (!cache.contains(key)) {
                        auto sample = std::make_shared<AudioSample>();
                        if (!decode_ps_adpcm(samples.subspan(off, size), *sample, error))
                            return false;
                        cache.emplace(key, std::move(sample));
                    }
                    // Signed center note selects PS2 (negative) / PS1 sample clock.
                    int note = r.s8(t + 2), fine = r.s8(t + 3);
                    float pitch = std::exp2((60.f - std::abs(note) + fine / 128.f) / 12.f);
                    auto sample = std::make_shared<AudioSample>(*cache.at(key));
                    sample->rate = unsigned(std::round((note < 0 ? 48000.f : 44100.f) * pitch));
                    grain.tone.sample = std::move(sample);
                    grain.tone.volume = std::clamp(int(r.s8(t + 1)), 0, 127) / 127.f;
                    grain.tone.pan = float(int16_t(r.u16(t + 4)));
                    grain.tone.bend_low = float(r.s8(t + 8));
                    grain.tone.bend_high = float(r.s8(t + 9));
                }
                if (grain.opcode >> 24 == 5) {
                    // the record of a child sound: its volume, pan, register settings, and at +12 its index in
                    // this bank (then a name, empty in these banks)
                    size_t t = gd + (grain.opcode & 0xffffff);
                    r.span(t, 32);
                    const uint32_t child = r.u32(t + 12);
                    if (child < n) {
                        grain.child = int32_t(child);
                        grain.child_volume = std::clamp(int(r.u32(t) & 255), 0, 127) / 127.f;
                    }
                }
                if (grain.opcode >> 24 == 5 && std::getenv("SM_SOUND_DEBUG")) { // research: a child sound's record
                    size_t t = gd + (grain.opcode & 0xffffff);
                    r.span(t, 32);
                    std::printf("  child grain of sound %u at %zx:", i, size_t(grain.opcode & 0xffffff));
                    for (unsigned k = 0; k < 32; k += 4)
                        std::printf(" %08x", r.u32(t + k));
                    std::printf("\n");
                }
                sound.grains.push_back(std::move(grain));
            }
        }
        if (error)
            error->clear();
        return true;
    } catch (const std::exception &e) {
        out.sounds.clear();
        return failed(error, e);
    }
}
bool load_level_audio(const IsoFs &disc, const std::string &level, LevelAudio &out,
                      std::string *error) {
    out = {};
    try {
        auto slash = level.find_last_of('/'), dot = level.find_last_of('.');
        std::string id = level.substr(slash + 1, dot - slash - 1);
        for (const auto &path :
             std::vector<std::string>{"/GLOBAL.BWD", "/TR.BWD", "/SP.BWD", "/SND/" + id + ".BWD"}) {
            HigWad hdr;
            std::vector<uint8_t> b;
            std::vector<MediaAsset> assets;
            if (!unpack_hig_wad(disc.read(path), hdr, b, error) ||
                !media_assets(b, hdr, assets, error))
                return false;
            for (const auto &a : assets)
                if (a.size && a.name.ends_with(".bnk")) {
                    SoundBank bank;
                    bank.hash = a.hash;
                    bank.name = a.name;
                    if (!decode_sound_bank(std::span<const uint8_t>(b).subspan(a.offset, a.size),
                                           bank, error)) {
                        if (error)
                            *error = path + " / " + a.name + ": " + *error;
                        return false;
                    }
                    out.banks.insert_or_assign(a.hash, std::move(bank));
                }
        }
        for (const auto &path :
             std::vector<std::string>{"/GLOBAL.WAD", "/TR.WAD", "/SP.WAD", level}) {
            if (!disc.find(path))
                continue;
            HigWad hdr;
            std::vector<uint8_t> b;
            std::vector<MediaAsset> assets;
            if (!unpack_hig_wad(disc.read(path), hdr, b, error) ||
                !media_assets(b, hdr, assets, error))
                return false;
            Read r{b};
            for (const auto &a : assets) {
                if (a.size >= 8 && r.u32(a.offset) == 0xaabbccdd) {
                    unsigned n = r.u32(a.offset + 4);
                    if (n > (a.size - 8) / 4)
                        throw std::runtime_error("Sound signature table exceeds asset");
                    for (unsigned i = 0; i < n; i++)
                        out.bindings[r.u32(a.offset + 8 + i * 4)] = {a.hash + 1, i};
                }
                if (path == level && a.size >= 0x80 && r.u32(a.offset) == 0x20474f42) {
                    Read bog{r.span(a.offset, a.size)};
                    unsigned d = bog.u32(0x2c), n = bog.u32(0x30);
                    bog.span(d, size_t(n) * 68);
                    for (unsigned i = 0; i < n; i++)
                        if (bog.u32(d + i * 68 + 0x24) == 0xf2cdb47f &&
                            bog.u32(d + i * 68 + 0x30)) {
                            unsigned inst = bog.u32(d + i * 68 + 12);
                            unsigned prop = bog.u32(inst + 0x54);
                            int source = int32_t(bog.u32(prop + 0x10));
                            if (source < 0)
                                source = std::stoi(id);
                            unsigned variant = uint8_t(bog.s8(prop));
                            if (source < 0 || source > 99 || variant > 25)
                                throw std::runtime_error("Unsupported LevelMusic name");
                            char name[8];
                            std::snprintf(name, sizeof(name), "%02d%c", source, 'A' + variant);
                            out.music_name = name;
                            break;
                        }
                }
            }
            for (const auto &a : assets)
                if (a.size == 0x34 && out.bindings.contains(a.hash)) {
                    auto &binding = out.bindings.at(a.hash);
                    binding.name = a.name;
                    binding.volume = std::clamp(r.f32(a.offset + 0x18), 0.f, 1.f);
                    binding.loop = r.u32(a.offset + 0x14) != 0;
                    if (binding.index != r.u32(a.offset + 0x30))
                        throw std::runtime_error("GDE sound index/signature mismatch");
                }
        }
        if (!out.music_name.empty()) {
            out.music = std::make_shared<AudioSample>();
            if (!load_music(disc, out.music_name, *out.music, error))
                return false;
        }
        return true;
    } catch (const std::exception &e) {
        out = {};
        return failed(error, e);
    }
}
} // namespace hig
