#pragma once
#include "highud.h"
#include <memory>
#include <unordered_map>

namespace hig {
struct AudioSample {
    unsigned rate = 48000, channels = 1;
    std::vector<int16_t> pcm;
    size_t loop_start = 0, loop_end = 0; // frames, exclusive end; zero means one-shot
};
bool decode_ps_adpcm(std::span<const uint8_t>, AudioSample &, std::string *error = nullptr);
bool decode_vag(std::span<const uint8_t>, AudioSample &, std::string *error = nullptr);
bool load_music(const IsoFs &, const std::string &name, AudioSample &,
                std::string *error = nullptr);
struct SoundTone {
    std::shared_ptr<AudioSample> sample;
    float volume = 1, pan = 0, bend_low = 0, bend_high = 0;
};
struct SoundGrain {
    uint32_t opcode = 0;
    int32_t delay = 0; // 240 Hz Scream tick
    SoundTone tone;
    // A grain that starts another sound of the same bank (type 5): its index there, and the volume it is given.
    int32_t child = -1;
    float child_volume = 1;
};
struct BankSound {
    float volume = 1;
    std::vector<SoundGrain> grains;
};
struct SoundBank {
    uint32_t hash = 0;
    std::string name;
    std::vector<BankSound> sounds;
};
bool decode_sound_bank(std::span<const uint8_t>, SoundBank &, std::string *error = nullptr);
struct SoundBinding {
    uint32_t bank = 0, index = 0;
    float volume = 1;
    bool loop = false;
    std::string name;
};
struct LevelAudio {
    std::unordered_map<uint32_t, SoundBank> banks;
    std::unordered_map<uint32_t, SoundBinding> bindings;
    std::shared_ptr<AudioSample> music;
    std::string music_name;
};
bool load_level_audio(const IsoFs &, const std::string &level_wad, LevelAudio &,
                      std::string *error = nullptr);
} // namespace hig
