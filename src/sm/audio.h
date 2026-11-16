#pragma once
#include "hig/higaudio.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

namespace sm {
struct AudioEvent {
    uint32_t sound = 0;
    bool stop = false;
    // What the sound's program reads from the game's registers when it has a choice to make (a footstep's ground,
    // a gadgebot command's number).
    int param = 0;
};
class Audio {
  public:
    ~Audio();
    bool load(const hig::IsoFs &, const std::string &level, std::string &error);
    bool open_device(std::string &error);
    void events(const std::vector<AudioEvent> &);
    void pause(bool);
    void movie(std::shared_ptr<hig::AudioSample>); // null restores frozen level mix
    double movie_seconds();
    void volumes(float music, float effects);
    void render(std::span<int16_t> stereo); // same mixer for device and headless capture
    bool write_capture(const std::filesystem::path &, std::span<const int16_t>, std::string &);
    // Device-thread mix for recording (48 kHz stereo); silent() mutes the speakers after it.
    void capture(std::function<void(const int16_t *, size_t frames)>);
    void silent(bool s) { silent_ = s; }
    unsigned event_count() const { return event_count_; }
    // What became of the sound events asked for (a hidden run's report): how many were played, and the sounds
    // the level's banks do not have or whose program the mixer cannot play yet, each with its count.
    struct Tally {
        unsigned played = 0;
        std::map<uint32_t, unsigned> unbound, unsupported, heard;
    };
    Tally tally() {
        std::lock_guard lock(mutex_);
        return tally_;
    }
    const hig::LevelAudio &assets() const { return data_; }

  private:
    struct Voice {
        std::shared_ptr<hig::AudioSample> sample;
        double position = 0, step = 1;
        float gain = 1, left = 1, right = 1;
        uint32_t sound = 0;
        size_t delay = 0;
        bool loop = false;
    };
    hig::LevelAudio data_;
    Tally tally_;
    std::vector<Voice> voices_;
    std::mutex mutex_, capture_mutex_;
    std::function<void(const int16_t *, size_t)> capture_;
    std::atomic<bool> silent_{false};
    std::thread thread_;
    std::atomic<bool> finish_{false};
    void *device_ = nullptr;
    double music_position_ = 0;
    std::shared_ptr<hig::AudioSample> movie_;
    uint64_t movie_position_ = 0;
    bool paused_ = false;
    float music_volume_ = .65f, effects_volume_ = .85f;
    uint32_t random_ = 0x12345678;
    unsigned event_count_ = 0;
    uint32_t random();
};
} // namespace sm
