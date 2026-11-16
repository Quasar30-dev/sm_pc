#include "audio.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <mmsystem.h>

namespace sm {
Audio::~Audio() {
    finish_ = true;
    if (thread_.joinable())
        thread_.join();
    if (device_)
        waveOutClose(static_cast<HWAVEOUT>(device_));
}
uint32_t Audio::random() {
    random_ ^= random_ << 13;
    random_ ^= random_ >> 17;
    random_ ^= random_ << 5;
    return random_;
}
bool Audio::load(const hig::IsoFs &disc, const std::string &level, std::string &error) {
    // Stop previous level before doing ISO I/O; the output thread owns no disc reader.
    {
        std::lock_guard lock(mutex_);
        voices_.clear();
        data_ = {};
        music_position_ = 0;
    }
    hig::LevelAudio next;
    if (!hig::load_level_audio(disc, level, next, &error))
        return false;
    std::printf("Retail audio: %zu banks, %zu sound bindings; music %s (%zu frames)\n",
                next.banks.size(), next.bindings.size(), next.music_name.c_str(),
                next.music ? next.music->pcm.size() / 2 : 0);
    std::lock_guard lock(mutex_);
    data_ = std::move(next);
    event_count_ = 0;
    random_ = 0x12345678;
    return true;
}
void Audio::pause(bool p) {
    std::lock_guard lock(mutex_);
    paused_ = p;
}
void Audio::movie(std::shared_ptr<hig::AudioSample> movie) {
    std::lock_guard lock(mutex_);
    movie_ = std::move(movie);
    movie_position_ = 0;
}
double Audio::movie_seconds() {
    std::lock_guard lock(mutex_);
    return double(movie_position_) / 48000;
}
void Audio::volumes(float music, float effects) {
    std::lock_guard lock(mutex_);
    music_volume_ = std::clamp(music, 0.f, 1.f);
    effects_volume_ = std::clamp(effects, 0.f, 1.f);
}
void Audio::events(const std::vector<AudioEvent> &events) {
    std::lock_guard lock(mutex_);
    for (const auto &event : events) {
        if (event.stop)
            std::erase_if(voices_, [&](auto &v) { return v.sound == event.sound; });
        auto binding = data_.bindings.find(event.sound);
        if (event.stop && binding == data_.bindings.end())
            continue;
        if (binding == data_.bindings.end()) {
            tally_.unbound[event.sound]++;
            continue;
        }
        const auto &b = binding->second;
        auto bank = data_.banks.find(b.bank);
        if (bank == data_.banks.end() || b.index >= bank->second.sounds.size())
            continue;
        const auto &sounds = bank->second.sounds;
        // A program is played only when the mixer knows every grain it can reach (its child sounds included);
        // otherwise it is left out as a whole, never with every branch at once. Scream's grain types here: 1 a
        // tone, 4 an LFO's settings (not applied), 5 a child sound, 20 nothing, 21 / 22 a loop (sixteen turns of it
        // are laid out), 24 stop, 25 one of several runs at random, 26 a random wait, 27 / 28 a pitch bend, 30 to
        // 33 and 40 a register set, set at random, raised, lowered, added to, 34 a test of a register (the grain
        // after it is skipped when it fails), 35 a marker, 36 on to a marker, 38 wait for the voices, 41 key the
        // voices off, 43 what follows is for when the sound is stopped.
        const std::function<bool(size_t, int)> known = [&](size_t index, int depth) {
            if (depth > 4 || index >= sounds.size())
                return false;
            for (const auto &g : sounds[index].grains) {
                const unsigned type = g.opcode >> 24;
                if (type == 5) {
                    if (g.child < 0 || !known(size_t(g.child), depth + 1))
                        return false;
                } else if (type != 0 && type != 1 && type != 4 && type != 20 && type != 21 && type != 22 && type != 24 && type != 25 &&
                           type != 26 && type != 27 && type != 28 && !(type >= 30 && type <= 36) && type != 38 && type != 40 &&
                           type != 41 && type != 43)
                    return false;
            }
            return true;
        };
        if (!known(b.index, 0)) {
            if (!event.stop)
                tally_.unsupported[event.sound]++;
            continue;
        }
        // where a stopped sound's own ending starts (after its grain of type 43), if it has one
        size_t ending = 0;
        for (size_t k = 0; k < sounds[b.index].grains.size(); k++)
            if (sounds[b.index].grains[k].opcode >> 24 == 43)
                ending = k + 1;
        if (event.stop && !ending)
            continue;
        if (!event.stop) {
            tally_.played++;
            tally_.heard[event.sound]++;
        }
        if (b.loop)
            std::erase_if(voices_, [&](auto &v) { return v.sound == event.sound; });
        const std::function<void(size_t, size_t, size_t, float, int)> run = [&](size_t index, size_t first, size_t delay, float volume,
                                                                                int depth) {
            const auto &sound = sounds[index];
            size_t skip_after = 0, remaining = 0, loop_from = 0;
            float bend = 0;
            unsigned jumps = 0, turns = 0;
            int registers[4]{};
            const auto reg = [&](int number) -> int & { return registers[size_t(number) & 3]; };
            for (size_t i = first; i < sound.grains.size(); i++) {
                const auto &g = sound.grains[i];
                const unsigned type = g.opcode >> 24;
                delay += size_t(g.delay) * 200; // 48000 / Scream's 240 Hz sequencer
                if (type == 24 || type == 43)
                    break;
                if (type == 21) {
                    loop_from = i;
                    continue;
                }
                if (type == 22) { // round again: laid out sixteen times, a stop event ends it sooner
                    if (++turns >= 16)
                        break;
                    i = loop_from;
                    continue;
                }
                if (type >= 30 && type <= 33 || type == 40) {
                    const int number = int8_t(g.opcode & 255), a = int8_t((g.opcode >> 8) & 255), c = int8_t((g.opcode >> 16) & 255);
                    if (number >= 0) {
                        if (type == 30)
                            reg(number) = a;
                        else if (type == 31)
                            reg(number) = c > a ? a + int(random() % unsigned(c - a + 1)) : a;
                        else if (type == 32)
                            reg(number)++;
                        else if (type == 33)
                            reg(number)--;
                        else
                            reg(number) += a;
                    }
                    continue;
                }
                if (type == 25) {
                    unsigned options = g.opcode & 255, count = (g.opcode >> 8) & 255;
                    if (!options || !count || i + size_t(options) * count >= sound.grains.size())
                        break;
                    unsigned choice = random() % options;
                    i += choice * count;
                    remaining = count;
                    skip_after = (options - choice - 1) * count;
                    continue;
                }
                if (type == 34) {
                    // the register (a negative number is one of the game's own, which the event's `param` stands
                    // for), how to compare (0 less, 1 equal, 2 more) and with what
                    const int number = int8_t(g.opcode & 255), how = (g.opcode >> 8) & 255, with = int8_t((g.opcode >> 16) & 255);
                    const int value = number < 0 ? event.param : reg(number);
                    const bool pass = how == 0 ? value < with : how == 1 ? value == with : value > with;
                    if (!pass)
                        i++; // the next grain is not done
                    continue;
                }
                if (type == 36) {
                    if (++jumps > 8)
                        break;
                    size_t marker = sound.grains.size();
                    for (size_t k = 0; k < sound.grains.size(); k++)
                        if (sound.grains[k].opcode >> 24 == 35 && (sound.grains[k].opcode & 0xffffff) == (g.opcode & 0xffffff))
                            marker = k;
                    if (marker == sound.grains.size())
                        break;
                    i = marker;
                    continue;
                }
                if (type == 5 && g.child >= 0 && depth < 4)
                    run(size_t(g.child), 0, delay, volume * g.child_volume, depth + 1);
                if (type == 26 && (g.opcode & 0xffffff))
                    delay += size_t(random() % (g.opcode & 0xffffff)) * 200;
                if (type == 27)
                    bend = (int(random() % 65536) - 32768) / 32768.f * int8_t(g.opcode & 255) / 100.f;
                if (type == 28)
                    bend = int8_t(g.opcode & 255) / 127.f;
                if (type == 1 && g.tone.sample && !g.tone.sample->pcm.empty()) {
                    const auto &t = g.tone;
                    float semitone = bend * (bend < 0 ? t.bend_low : t.bend_high);
                    float pan = std::sin(t.pan * 3.14159265f / 180.f);
                    Voice v;
                    v.sample = t.sample;
                    v.step = t.sample->rate / 48000. * std::exp2(semitone / 12.f);
                    v.gain = sound.volume * t.volume * b.volume * volume;
                    v.sound = event.sound;
                    v.delay = delay;
                    v.left = std::sqrt((1 - pan) * .5f);
                    v.right = std::sqrt((1 + pan) * .5f);
                    v.loop = b.loop;
                    if (voices_.size() >= 64)
                        voices_.erase(voices_.begin());
                    voices_.push_back(std::move(v));
                }
                if (remaining && !--remaining) {
                    i += skip_after;
                    skip_after = 0;
                }
            }
        };
        run(b.index, event.stop ? ending : 0, 0, 1, 0);
        event_count_++;
    }
}
void Audio::render(std::span<int16_t> output) {
    std::lock_guard lock(mutex_);
    std::fill(output.begin(), output.end(), 0);
    if (paused_)
        return;
    if (movie_) {
        for (size_t i = 0; i + 1 < output.size(); i += 2, ++movie_position_)
            for (unsigned c = 0; c < 2; c++)
                if (movie_position_ * 2 + c < movie_->pcm.size())
                    output[i + c] = int16_t(movie_->pcm[movie_position_ * 2 + c] * effects_volume_);
        return;
    }
    auto fetch = [](const hig::AudioSample &s, double &position, double step, bool loop,
                    unsigned channel) {
        size_t frames = s.pcm.size() / s.channels;
        size_t end = loop && s.loop_end ? s.loop_end : frames;
        if (position >= end && loop && end > s.loop_start)
            position =
                s.loop_start + std::fmod(position - s.loop_start, double(end - s.loop_start));
        if (position >= end || !end)
            return 0.f;
        size_t i = size_t(position), j = i + 1;
        if (j >= end)
            j = loop ? s.loop_start : i;
        float fraction = float(position - i);
        float sample = (s.pcm[i * s.channels + channel] * (1 - fraction) +
                        s.pcm[j * s.channels + channel] * fraction) /
                       32768.f;
        if (channel + 1 == s.channels)
            position += step;
        return sample;
    };
    for (size_t i = 0; i + 1 < output.size(); i += 2) {
        float left = 0, right = 0;
        if (data_.music) {
            auto &m = *data_.music;
            left = fetch(m, music_position_, m.rate / 48000., true, 0) * music_volume_;
            right = fetch(m, music_position_, m.rate / 48000., true, 1) * music_volume_;
        }
        for (auto &v : voices_) {
            if (v.delay) {
                v.delay--;
                continue;
            }
            float a = fetch(*v.sample, v.position, v.step, v.loop, 0) * v.gain * effects_volume_;
            left += a * v.left;
            right += a * v.right;
        }
        output[i] = int16_t(std::clamp(std::lround(left * 32767), -32768l, 32767l));
        output[i + 1] = int16_t(std::clamp(std::lround(right * 32767), -32768l, 32767l));
    }
    std::erase_if(
        voices_, [](auto &v) { return !v.loop && !v.delay && v.position >= v.sample->pcm.size(); });
}
void Audio::capture(std::function<void(const int16_t *, size_t)> sink) {
    std::lock_guard lock(capture_mutex_);
    capture_ = std::move(sink);
}
bool Audio::open_device(std::string &error) {
    if (device_)
        return true;
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = 192000;
    HWAVEOUT device = nullptr;
    MMRESULT result = waveOutOpen(&device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) {
        error = "Windows audio output unavailable (" + std::to_string(result) + ")";
        return false;
    }
    device_ = device;
    finish_ = false;
    thread_ = std::thread([this, device] {
        std::array<std::array<int16_t, 1024>, 4> buffers{};
        std::array<WAVEHDR, 4> headers{};
        for (size_t i = 0; i < 4; i++) {
            headers[i].lpData = reinterpret_cast<char *>(buffers[i].data());
            headers[i].dwBufferLength = sizeof(buffers[i]);
            waveOutPrepareHeader(device, &headers[i], sizeof(WAVEHDR));
        }
        while (!finish_) {
            for (size_t i = 0; i < 4; i++)
                if (!(headers[i].dwFlags & WHDR_INQUEUE)) {
                    render(buffers[i]);
                    {
                        std::lock_guard lock(capture_mutex_);
                        if (capture_)
                            capture_(buffers[i].data(), buffers[i].size() / 2);
                    }
                    if (silent_)
                        buffers[i].fill(0);
                    waveOutWrite(device, &headers[i], sizeof(WAVEHDR));
                }
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
        waveOutReset(device);
        for (auto &h : headers)
            waveOutUnprepareHeader(device, &h, sizeof(h));
    });
    error.clear();
    return true;
}
bool Audio::write_capture(const std::filesystem::path &path, std::span<const int16_t> data,
                          std::string &error) {
    if (std::filesystem::exists(path) || data.size() > 0x7fffffc0u) {
        error = "Audio capture must be a new, bounded WAV";
        return false;
    }
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t n) { f.write(reinterpret_cast<const char *>(&n), 4); };
    auto u16 = [&](uint16_t n) { f.write(reinterpret_cast<const char *>(&n), 2); };
    f.write("RIFF", 4);
    u32(36 + uint32_t(data.size() * 2));
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(2);
    u32(48000);
    u32(192000);
    u16(4);
    u16(16);
    f.write("data", 4);
    u32(uint32_t(data.size() * 2));
    f.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size() * 2));
    if (!f) {
        error = "Audio capture write failed";
        return false;
    }
    return true;
}
} // namespace sm
