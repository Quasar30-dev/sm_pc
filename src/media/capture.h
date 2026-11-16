#pragma once
// Built-in video and screenshot capture shared by future_pc and sm_pc.
//
// VideoRecorder encodes RGBA frames and 48 kHz stereo PCM to an H.264/AAC MP4 with Media
// Foundation on a worker thread. D3D11FrameGrabber copies the back buffer into a ring of staging
// textures and maps them a few frames later, so recording never waits on the GPU.
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

namespace media {

enum class Quality { low, medium, high, ultra };
const char *quality_name(Quality);

struct CaptureSettings {
    std::filesystem::path folder;  // created on first capture
    Quality quality = Quality::high;
    unsigned fps = 60;             // 30 or 60
    unsigned height = 0;           // 0 keeps the window size; otherwise downscale to this height
    bool audio = true;
    bool hide_overlays = true;     // engines hide debug and capture panels while recording
    std::string screenshot_format = "png"; // png or jpg
    unsigned jpeg_quality = 92;
};

// %USERPROFILE%\Videos\<game>
std::filesystem::path default_capture_folder(std::string_view game);
// A small key=value file; missing or unreadable values keep their defaults.
CaptureSettings load_capture_settings(const std::filesystem::path &file, std::string_view game);
bool save_capture_settings(const std::filesystem::path &file, const CaptureSettings &);
// <folder>/<prefix>_2026-10-05_15-40-12.<extension>, with a counter if that name exists.
std::filesystem::path timestamped_path(const std::filesystem::path &folder, std::string_view prefix,
                                       std::string_view extension);
// Writes top-down RGBA rows as PNG or JPEG (format "png" or "jpg").
bool save_image(const std::filesystem::path &file, const uint8_t *rgba, unsigned width,
                unsigned height, size_t pitch, std::string_view format, unsigned jpeg_quality,
                std::string &error);
void open_folder(const std::filesystem::path &folder);
// Shows the Windows folder picker; returns an empty path if cancelled.
std::filesystem::path pick_folder(void *owner_window, const std::filesystem::path &start);

class VideoRecorder {
public:
    struct Stats {
        double seconds = 0;
        uint64_t frames = 0, dropped = 0;
        std::filesystem::path file;
        std::string error;
    };
    VideoRecorder();
    ~VideoRecorder();
    VideoRecorder(const VideoRecorder &) = delete;
    VideoRecorder &operator=(const VideoRecorder &) = delete;

    // width/height are the source frame size; the output follows settings.height and fps.
    bool start(const std::filesystem::path &file, const CaptureSettings &settings, unsigned width,
               unsigned height, bool with_audio, std::string &error);
    // Encoder-ready input: frames arrive already scaled to width x height (even) as NV12
    // (BT.709, 16-235), e.g. from D3D11FrameGrabber::grab_nv12. settings.height is ignored.
    bool start_nv12(const std::filesystem::path &file, const CaptureSettings &settings, unsigned width,
                    unsigned height, bool with_audio, std::string &error);
    // height rows of luma then height/2 rows of interleaved CbCr, each width bytes, rows pitch apart.
    void frame_nv12(const uint8_t *nv12, size_t pitch, double seconds);
    bool nv12_input() const;
    // Top-down RGBA rows at the source size. seconds is the frame's time since start(); frames are
    // placed on the fps grid (a late frame extends the previous one, an early one is skipped).
    void frame(const uint8_t *rgba, size_t pitch, double seconds);
    // Interleaved stereo at 48 kHz, in order. Thread-safe.
    void audio(const int16_t *stereo, size_t frames);
    // Float mix from any channel count and rate; downmixed to stereo and resampled to 48 kHz.
    // Call from one thread at a time.
    void audio_float(const float *samples, size_t frames, unsigned channels, unsigned rate = 48000);
    Stats stop();
    bool active() const;
    Stats stats() const;
    unsigned source_width() const;
    unsigned source_height() const;

private:
    bool begin(const std::filesystem::path &file, const CaptureSettings &settings, unsigned width,
               unsigned height, bool with_audio, bool nv12, std::string &error);
    struct Impl;
    std::unique_ptr<Impl> p_;
};

class D3D11FrameGrabber {
public:
    D3D11FrameGrabber();
    ~D3D11FrameGrabber();
    // Queue a copy of source (an R8G8B8A8 or B8G8R8A8 texture) stamped with seconds, then hand any
    // finished copies to the recorder.
    void grab(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *source,
              double seconds, VideoRecorder &recorder);
    // GPU path: area-filter source to the recorder's output size and convert to NV12 on the GPU,
    // so only width*height*1.5 bytes are read back. Returns false if the GPU path is unavailable
    // on this device (the caller then records with grab()). Leaves the pipeline state as found.
    bool grab_nv12(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *source,
                   double seconds, VideoRecorder &recorder);
    // Whether grab_nv12 can run on this device (compiles its shaders once).
    bool nv12_supported(ID3D11Device *device);
    // Hand every queued copy to the recorder, waiting for the GPU. Call before VideoRecorder::stop.
    void flush(ID3D11DeviceContext *context, VideoRecorder &recorder);
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

// Reads source synchronously into top-down RGBA (for screenshots).
bool read_texture_rgba(ID3D11Device *device, ID3D11DeviceContext *context,
                       ID3D11Texture2D *source, std::vector<uint8_t> &rgba, unsigned &width,
                       unsigned &height, std::string &error);

} // namespace media
