#include "capture.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <codecapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <optional>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <future>
#include <mutex>
#include <sstream>
#include <thread>
#include <variant>

using Microsoft::WRL::ComPtr;

namespace media {
namespace {

struct ComScope {
    bool owned = false;
    explicit ComScope(DWORD model = COINIT_MULTITHREADED) {
        const HRESULT hr = CoInitializeEx(nullptr, model);
        owned = SUCCEEDED(hr); // S_FALSE also needs a matching uninitialize
    }
    ~ComScope() {
        if (owned)
            CoUninitialize();
    }
};

std::string hresult_text(const char *what, HRESULT hr) {
    char code[16];
    std::snprintf(code, sizeof code, "%08lx", static_cast<unsigned long>(hr));
    return std::string(what) + " failed (0x" + code + ")";
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t'))
        ++start;
    return s.substr(start);
}

std::string utf8(const std::wstring &w) {
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring out(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
    return out;
}

// Area-weighted resample of one axis. Each output sample covers [i*ratio, (i+1)*ratio) of input.
struct AxisWeights {
    std::vector<uint32_t> first;
    std::vector<std::vector<uint32_t>> weights; // 16.16 fixed point, sums to 65536
};
AxisWeights axis_weights(unsigned source, unsigned target) {
    AxisWeights a;
    a.first.resize(target);
    a.weights.resize(target);
    const double ratio = double(source) / target;
    for (unsigned i = 0; i < target; ++i) {
        const double lo = i * ratio, hi = std::min(double(source), (i + 1) * ratio);
        const unsigned first = unsigned(lo), last = std::min(source - 1, unsigned(std::ceil(hi)) - 1);
        a.first[i] = first;
        std::vector<double> w;
        for (unsigned s = first; s <= last; ++s)
            w.push_back(std::max(0.0, std::min(hi, s + 1.0) - std::max(lo, double(s))));
        double sum = 0;
        for (double v : w)
            sum += v;
        uint32_t total = 0;
        for (size_t k = 0; k < w.size(); ++k) {
            const uint32_t q = k + 1 == w.size() ? 65536 - total : uint32_t(std::lround(w[k] / sum * 65536));
            a.weights[i].push_back(q);
            total += q;
        }
    }
    return a;
}

// RGBA (source size, top-down) -> BGRX (output size, top-down).
void convert_frame(const std::vector<uint8_t> &rgba, unsigned sw, unsigned sh, std::vector<uint8_t> &bgra,
                   unsigned dw, unsigned dh, const AxisWeights *xs, const AxisWeights *ys) {
    bgra.resize(size_t(dw) * dh * 4);
    if (!xs) { // same size (perhaps cropped by one pixel to stay even)
        for (unsigned y = 0; y < dh; ++y) {
            const uint8_t *s = rgba.data() + size_t(y) * sw * 4;
            uint8_t *d = bgra.data() + size_t(y) * dw * 4;
            for (unsigned x = 0; x < dw; ++x, s += 4, d += 4) {
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
                d[3] = 255;
            }
        }
        return;
    }
    // Horizontal pass into a 16-bit-per-channel intermediate, then vertical.
    std::vector<uint32_t> mid(size_t(dw) * sh * 3);
    for (unsigned y = 0; y < sh; ++y) {
        const uint8_t *row = rgba.data() + size_t(y) * sw * 4;
        uint32_t *out = mid.data() + size_t(y) * dw * 3;
        for (unsigned x = 0; x < dw; ++x) {
            uint64_t r = 0, g = 0, b = 0;
            const uint8_t *s = row + size_t(xs->first[x]) * 4;
            for (uint32_t w : xs->weights[x]) {
                r += uint64_t(s[0]) * w;
                g += uint64_t(s[1]) * w;
                b += uint64_t(s[2]) * w;
                s += 4;
            }
            out[x * 3 + 0] = uint32_t(r >> 8); // 8.8 fixed point
            out[x * 3 + 1] = uint32_t(g >> 8);
            out[x * 3 + 2] = uint32_t(b >> 8);
        }
    }
    for (unsigned y = 0; y < dh; ++y) {
        uint8_t *d = bgra.data() + size_t(y) * dw * 4;
        for (unsigned x = 0; x < dw; ++x) {
            uint64_t r = 0, g = 0, b = 0;
            unsigned sy = ys->first[y];
            for (uint32_t w : ys->weights[y]) {
                const uint32_t *s = mid.data() + (size_t(sy) * dw + x) * 3;
                r += uint64_t(s[0]) * w;
                g += uint64_t(s[1]) * w;
                b += uint64_t(s[2]) * w;
                ++sy;
            }
            d[x * 4 + 0] = uint8_t(std::min<uint64_t>(255, (b + (1u << 23)) >> 24));
            d[x * 4 + 1] = uint8_t(std::min<uint64_t>(255, (g + (1u << 23)) >> 24));
            d[x * 4 + 2] = uint8_t(std::min<uint64_t>(255, (r + (1u << 23)) >> 24));
            d[x * 4 + 3] = 255;
        }
    }
}

double bits_per_pixel(Quality q) {
    switch (q) {
    case Quality::low: return .045;
    case Quality::medium: return .075;
    case Quality::high: return .11;
    default: return .18;
    }
}

} // namespace

const char *quality_name(Quality q) {
    switch (q) {
    case Quality::low: return "low";
    case Quality::medium: return "medium";
    case Quality::high: return "high";
    default: return "ultra";
    }
}

std::filesystem::path default_capture_folder(std::string_view game) {
    PWSTR videos = nullptr;
    std::filesystem::path base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos, 0, nullptr, &videos)))
        base = videos;
    CoTaskMemFree(videos);
    if (base.empty())
        base = std::filesystem::current_path() / "captures";
    return base / std::filesystem::path(wide(std::string(game)));
}

CaptureSettings load_capture_settings(const std::filesystem::path &file, std::string_view game) {
    CaptureSettings s;
    s.folder = default_capture_folder(game);
    std::ifstream in(file);
    std::string line;
    while (in && std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos || line.starts_with('#'))
            continue;
        const auto key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        try {
            if (key == "folder" && !value.empty())
                s.folder = std::filesystem::path(wide(value));
            else if (key == "quality")
                s.quality = value == "low" ? Quality::low : value == "medium" ? Quality::medium : value == "ultra" ? Quality::ultra : Quality::high;
            else if (key == "fps")
                s.fps = std::stoul(value) == 30 ? 30u : 60u;
            else if (key == "height") {
                const unsigned h = unsigned(std::stoul(value));
                s.height = h == 720 || h == 1080 || h == 1440 ? h : 0;
            } else if (key == "audio")
                s.audio = value != "0";
            else if (key == "hide_overlays")
                s.hide_overlays = value != "0";
            else if (key == "screenshot_format")
                s.screenshot_format = value == "jpg" ? "jpg" : "png";
            else if (key == "jpeg_quality")
                s.jpeg_quality = std::clamp(unsigned(std::stoul(value)), 50u, 100u);
        } catch (const std::exception &) { // keep the default for a malformed value
        }
    }
    return s;
}

bool save_capture_settings(const std::filesystem::path &file, const CaptureSettings &s) {
    std::ofstream out(file, std::ios::trunc);
    out << "# Capture settings (F9 record, F11 settings, F12 screenshot)\n"
        << "folder=" << utf8(s.folder.wstring()) << "\n"
        << "quality=" << quality_name(s.quality) << "\n"
        << "fps=" << s.fps << "\n"
        << "height=" << s.height << "\n"
        << "audio=" << (s.audio ? 1 : 0) << "\n"
        << "hide_overlays=" << (s.hide_overlays ? 1 : 0) << "\n"
        << "screenshot_format=" << s.screenshot_format << "\n"
        << "jpeg_quality=" << s.jpeg_quality << "\n";
    return bool(out);
}

std::filesystem::path timestamped_path(const std::filesystem::path &folder, std::string_view prefix,
                                       std::string_view extension) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", &local);
    const std::string base = std::string(prefix) + "_" + stamp;
    auto path = folder / (base + "." + std::string(extension));
    for (unsigned n = 2; std::filesystem::exists(path); ++n)
        path = folder / (base + "_" + std::to_string(n) + "." + std::string(extension));
    return path;
}

bool save_image(const std::filesystem::path &file, const uint8_t *rgba, unsigned width, unsigned height,
                size_t pitch, std::string_view format, unsigned jpeg_quality, std::string &error) {
    ComScope com;
    std::error_code ec;
    if (file.has_parent_path())
        std::filesystem::create_directories(file.parent_path(), ec);
    const bool jpeg = format == "jpg";
    std::vector<uint8_t> bgra(size_t(width) * height * 4);
    for (unsigned y = 0; y < height; ++y) {
        const uint8_t *s = rgba + y * pitch;
        uint8_t *d = bgra.data() + size_t(y) * width * 4;
        for (unsigned x = 0; x < width; ++x, s += 4, d += 4) {
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
            d[3] = s[3]; // PNG keeps alpha (font atlases store coverage there); JPEG drops it
        }
    }
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = factory->CreateEncoder(jpeg ? GUID_ContainerFormatJpeg : GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &options);
    if (SUCCEEDED(hr) && jpeg) {
        PROPBAG2 name{};
        name.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_R4;
        value.fltVal = float(std::clamp(jpeg_quality, 50u, 100u)) / 100.f;
        hr = options->Write(1, &name, &value);
    }
    if (SUCCEEDED(hr)) hr = frame->Initialize(options.Get());
    if (SUCCEEDED(hr)) hr = frame->SetSize(width, height);
    WICPixelFormatGUID pixel = jpeg ? GUID_WICPixelFormat24bppBGR : GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&pixel);
    if (SUCCEEDED(hr)) {
        if (pixel == GUID_WICPixelFormat24bppBGR) {
            std::vector<uint8_t> bgr(size_t(width) * height * 3);
            for (size_t i = 0, j = 0; i < bgra.size(); i += 4, j += 3) {
                bgr[j] = bgra[i];
                bgr[j + 1] = bgra[i + 1];
                bgr[j + 2] = bgra[i + 2];
            }
            hr = frame->WritePixels(height, width * 3, UINT(bgr.size()), bgr.data());
        } else if (pixel == GUID_WICPixelFormat32bppBGRA)
            hr = frame->WritePixels(height, width * 4, UINT(bgra.size()), bgra.data());
        else
            hr = E_UNEXPECTED;
    }
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    if (FAILED(hr)) {
        error = hresult_text(jpeg ? "Writing the JPEG screenshot" : "Writing the PNG screenshot", hr);
        return false;
    }
    return true;
}

void open_folder(const std::filesystem::path &folder) {
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::filesystem::path pick_folder(void *owner_window, const std::filesystem::path &start) {
    // The common dialog needs a single-threaded apartment; the engines' threads may not be one.
    std::filesystem::path chosen;
    std::thread([&] {
        ComScope com(COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        ComPtr<IFileOpenDialog> dialog;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
            return;
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        dialog->SetTitle(L"Choose the capture folder");
        ComPtr<IShellItem> folder;
        std::error_code ec;
        std::filesystem::create_directories(start, ec);
        if (SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(&folder))))
            dialog->SetFolder(folder.Get());
        if (FAILED(dialog->Show(static_cast<HWND>(owner_window))))
            return;
        ComPtr<IShellItem> result;
        PWSTR path = nullptr;
        if (SUCCEEDED(dialog->GetResult(&result)) && SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            chosen = path;
            CoTaskMemFree(path);
        }
    }).join();
    return chosen;
}

// ---------------------------------------------------------------- recorder

struct VideoRecorder::Impl {
    struct Video {
        std::vector<uint8_t> rgba;
        int64_t slot = 0;
    };
    struct Audio {
        std::vector<int16_t> pcm;
    };
    using Job = std::variant<Video, Audio>;

    CaptureSettings settings;
    unsigned source_w = 0, source_h = 0, out_w = 0, out_h = 0;
    bool with_audio = false, nv12 = false;
    std::unique_ptr<AxisWeights> xs, ys;

    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> jobs;
    size_t queued_video = 0;
    bool stopping = false;
    std::thread worker;
    std::atomic<bool> running{false};

    // Producer-side frame placement (main thread) and audio resampling (audio thread).
    int64_t last_slot = -1;
    double resample_position = 0;
    float resample_last[2] = {};
    // Stats.
    mutable std::mutex stats_mutex;
    Stats stats;

    // Worker-side state.
    ComPtr<IMFSinkWriter> writer;
    DWORD video_stream = 0, audio_stream = 0;
    std::optional<Video> held;
    std::vector<uint8_t> converted;
    uint64_t audio_frames_written = 0;

    void fail(const std::string &message) {
        std::lock_guard lock(stats_mutex);
        if (stats.error.empty())
            stats.error = message;
    }

    HRESULT open(const std::filesystem::path &file) {
        ComPtr<IMFAttributes> attributes;
        HRESULT hr = MFCreateAttributes(&attributes, 4);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
        if (SUCCEEDED(hr)) hr = attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
        if (SUCCEEDED(hr)) hr = MFCreateSinkWriterFromURL(file.c_str(), nullptr, attributes.Get(), &writer);
        if (FAILED(hr)) return hr;

        const UINT32 bitrate = UINT32(std::clamp(double(out_w) * out_h * settings.fps * bits_per_pixel(settings.quality), 1.5e6, 80e6));
        ComPtr<IMFMediaType> video_out, video_in;
        hr = MFCreateMediaType(&video_out);
        if (SUCCEEDED(hr)) hr = video_out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = video_out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        if (SUCCEEDED(hr)) hr = video_out->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
        if (SUCCEEDED(hr)) hr = video_out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (SUCCEEDED(hr)) hr = video_out->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
        if (SUCCEEDED(hr) && nv12) hr = video_out->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
        if (SUCCEEDED(hr) && nv12) hr = video_out->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
        if (SUCCEEDED(hr)) hr = MFSetAttributeSize(video_out.Get(), MF_MT_FRAME_SIZE, out_w, out_h);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(video_out.Get(), MF_MT_FRAME_RATE, settings.fps, 1);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(video_out.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (SUCCEEDED(hr)) hr = writer->AddStream(video_out.Get(), &video_stream);
        if (SUCCEEDED(hr)) hr = MFCreateMediaType(&video_in);
        if (SUCCEEDED(hr)) hr = video_in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = video_in->SetGUID(MF_MT_SUBTYPE, nv12 ? MFVideoFormat_NV12 : MFVideoFormat_RGB32);
        if (SUCCEEDED(hr)) hr = video_in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (SUCCEEDED(hr)) hr = video_in->SetUINT32(MF_MT_DEFAULT_STRIDE, nv12 ? out_w : out_w * 4); // positive: top-down rows
        if (SUCCEEDED(hr) && nv12) hr = video_in->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
        if (SUCCEEDED(hr) && nv12) hr = video_in->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
        if (SUCCEEDED(hr)) hr = MFSetAttributeSize(video_in.Get(), MF_MT_FRAME_SIZE, out_w, out_h);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(video_in.Get(), MF_MT_FRAME_RATE, settings.fps, 1);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(video_in.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(video_stream, video_in.Get(), nullptr);
        if (FAILED(hr)) return hr;

        if (with_audio) {
            ComPtr<IMFMediaType> audio_out, audio_in;
            hr = MFCreateMediaType(&audio_out);
            if (SUCCEEDED(hr)) hr = audio_out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            if (SUCCEEDED(hr)) hr = audio_out->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
            if (SUCCEEDED(hr)) hr = audio_out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            if (SUCCEEDED(hr)) hr = audio_out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
            if (SUCCEEDED(hr)) hr = audio_out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
            if (SUCCEEDED(hr)) hr = audio_out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000); // 192 kbps
            if (SUCCEEDED(hr)) hr = writer->AddStream(audio_out.Get(), &audio_stream);
            if (SUCCEEDED(hr)) hr = MFCreateMediaType(&audio_in);
            if (SUCCEEDED(hr)) hr = audio_in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            if (SUCCEEDED(hr)) hr = audio_in->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
            if (SUCCEEDED(hr)) hr = audio_in->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            if (SUCCEEDED(hr)) hr = audio_in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
            if (SUCCEEDED(hr)) hr = audio_in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
            if (SUCCEEDED(hr)) hr = audio_in->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
            if (SUCCEEDED(hr)) hr = audio_in->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 48000 * 4);
            if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(audio_stream, audio_in.Get(), nullptr);
            if (FAILED(hr)) return hr;
        }
        return writer->BeginWriting();
    }

    HRESULT write_video(const Video &v, int64_t next_slot) {
        // NV12 frames arrive encoder-ready from the GPU; RGBA frames are converted here.
        if (!nv12) {
            if (xs)
                convert_frame(v.rgba, source_w, source_h, converted, out_w, out_h, xs.get(), ys.get());
            else
                convert_frame(v.rgba, source_w, source_h, converted, out_w, out_h, nullptr, nullptr);
        }
        const std::vector<uint8_t> &pixels = nv12 ? v.rgba : converted;
        ComPtr<IMFMediaBuffer> buffer;
        ComPtr<IMFSample> sample;
        HRESULT hr = MFCreateMemoryBuffer(DWORD(pixels.size()), &buffer);
        BYTE *data = nullptr;
        if (SUCCEEDED(hr)) hr = buffer->Lock(&data, nullptr, nullptr);
        if (SUCCEEDED(hr)) {
            std::memcpy(data, pixels.data(), pixels.size());
            buffer->Unlock();
            hr = buffer->SetCurrentLength(DWORD(pixels.size()));
        }
        if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
        if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.Get());
        const LONGLONG start = v.slot * 10000000LL / settings.fps, end = next_slot * 10000000LL / settings.fps;
        if (SUCCEEDED(hr)) hr = sample->SetSampleTime(start);
        if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(std::max<LONGLONG>(1, end - start));
        if (SUCCEEDED(hr)) hr = writer->WriteSample(video_stream, sample.Get());
        if (SUCCEEDED(hr)) {
            std::lock_guard lock(stats_mutex);
            stats.frames += uint64_t(next_slot - v.slot);
            stats.seconds = double(next_slot) / settings.fps;
        }
        return hr;
    }

    HRESULT write_audio(const Audio &a) {
        const DWORD bytes = DWORD(a.pcm.size() * sizeof(int16_t));
        ComPtr<IMFMediaBuffer> buffer;
        ComPtr<IMFSample> sample;
        HRESULT hr = MFCreateMemoryBuffer(bytes, &buffer);
        BYTE *data = nullptr;
        if (SUCCEEDED(hr)) hr = buffer->Lock(&data, nullptr, nullptr);
        if (SUCCEEDED(hr)) {
            std::memcpy(data, a.pcm.data(), bytes);
            buffer->Unlock();
            hr = buffer->SetCurrentLength(bytes);
        }
        if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
        if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.Get());
        const uint64_t frames = a.pcm.size() / 2;
        if (SUCCEEDED(hr)) hr = sample->SetSampleTime(LONGLONG(audio_frames_written * 10000000ULL / 48000));
        if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(LONGLONG(frames * 10000000ULL / 48000));
        if (SUCCEEDED(hr)) hr = writer->WriteSample(audio_stream, sample.Get());
        audio_frames_written += frames;
        return hr;
    }

    void run(const std::filesystem::path &file, std::promise<std::string> ready) {
        ComScope com;
        HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        if (FAILED(hr)) {
            ready.set_value(hresult_text("Starting Media Foundation", hr));
            return;
        }
        hr = open(file);
        if (FAILED(hr)) {
            writer.Reset();
            MFShutdown();
            ready.set_value(hresult_text("Opening the H.264 recorder", hr));
            return;
        }
        ready.set_value({});
        bool ok = true;
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stopping || !jobs.empty(); });
                if (jobs.empty())
                    break;
                job = std::move(jobs.front());
                jobs.pop_front();
                if (std::holds_alternative<Video>(job))
                    --queued_video;
            }
            if (!ok)
                continue;
            HRESULT r = S_OK;
            if (auto *video = std::get_if<Video>(&job)) {
                if (held)
                    r = write_video(*held, video->slot);
                held = std::move(*video);
            } else
                r = write_audio(std::get<Audio>(job));
            if (FAILED(r)) {
                fail(hresult_text("Encoding the recording", r));
                ok = false;
            }
        }
        if (ok && held)
            if (const HRESULT r = write_video(*held, held->slot + 1); FAILED(r))
                fail(hresult_text("Encoding the last frame", r));
        held.reset();
        if (writer)
            if (const HRESULT r = writer->Finalize(); FAILED(r))
                fail(hresult_text("Finishing the MP4", r));
        writer.Reset();
        MFShutdown();
    }
};

VideoRecorder::VideoRecorder() : p_(std::make_unique<Impl>()) {}
VideoRecorder::~VideoRecorder() {
    if (active())
        stop();
}

bool VideoRecorder::start(const std::filesystem::path &file, const CaptureSettings &settings, unsigned width,
                          unsigned height, bool with_audio, std::string &error) {
    return begin(file, settings, width, height, with_audio, false, error);
}

bool VideoRecorder::begin(const std::filesystem::path &file, const CaptureSettings &settings, unsigned width,
                          unsigned height, bool with_audio, bool nv12, std::string &error) {
    if (active()) {
        error = "Already recording";
        return false;
    }
    p_->nv12 = nv12;
    if (width < 16 || height < 16) {
        error = "The window is too small to record";
        return false;
    }
    // Reset in place: the audio thread may read p_ at any time.
    auto &p = *p_;
    if (p.worker.joinable())
        p.worker.join();
    p.jobs.clear();
    p.queued_video = 0;
    p.stopping = false;
    p.last_slot = -1;
    p.xs.reset();
    p.ys.reset();
    p.held.reset();
    p.audio_frames_written = 0;
    p.resample_position = 0;
    p.resample_last[0] = p.resample_last[1] = 0;
    {
        std::lock_guard lock(p.stats_mutex);
        p.stats = {};
    }
    p.settings = settings;
    p.settings.fps = settings.fps == 30 ? 30 : 60;
    p.source_w = width;
    p.source_h = height;
    p.with_audio = with_audio;
    if (p.nv12) {
        p.out_w = width & ~1u;
        p.out_h = height & ~1u;
    } else if (settings.height && settings.height < height) {
        p.out_h = settings.height & ~1u;
        p.out_w = unsigned(std::lround(double(width) * p.out_h / height)) & ~1u;
        p.xs = std::make_unique<AxisWeights>(axis_weights(width, p.out_w));
        p.ys = std::make_unique<AxisWeights>(axis_weights(height, p.out_h));
    } else {
        p.out_w = width & ~1u; // H.264 needs even dimensions
        p.out_h = height & ~1u;
    }
    std::error_code ec;
    if (file.has_parent_path())
        std::filesystem::create_directories(file.parent_path(), ec);
    {
        std::lock_guard lock(p.stats_mutex);
        p.stats.file = file;
    }
    std::promise<std::string> ready;
    auto result = ready.get_future();
    p.worker = std::thread([&p, file, ready = std::move(ready)]() mutable { p.run(file, std::move(ready)); });
    error = result.get();
    if (!error.empty()) {
        p.worker.join();
        return false;
    }
    p.running = true;
    return true;
}

void VideoRecorder::frame(const uint8_t *rgba, size_t pitch, double seconds) {
    auto &p = *p_;
    if (!p.running)
        return;
    // The first frame anchors the video at zero; later frames take the slot of their time.
    const int64_t slot = p.last_slot < 0 ? 0 : std::max<int64_t>(0, std::llround(seconds * p.settings.fps));
    if (slot <= p.last_slot)
        return; // two frames in one slot: keep the first
    Impl::Video v;
    v.slot = slot;
    v.rgba.resize(size_t(p.source_w) * p.source_h * 4);
    for (unsigned y = 0; y < p.source_h; ++y)
        std::memcpy(v.rgba.data() + size_t(y) * p.source_w * 4, rgba + y * pitch, size_t(p.source_w) * 4);
    {
        std::lock_guard lock(p.mutex);
        if (p.queued_video >= 8) { // the encoder is behind: the previous frame stays on screen longer
            std::lock_guard stats(p.stats_mutex);
            ++p.stats.dropped;
            return;
        }
        p.jobs.push_back(std::move(v));
        ++p.queued_video;
    }
    p.last_slot = slot;
    p.wake.notify_one();
}

void VideoRecorder::audio(const int16_t *stereo, size_t frames) {
    auto &p = *p_;
    if (!p.running || !p.with_audio || !frames)
        return;
    Impl::Audio a;
    a.pcm.assign(stereo, stereo + frames * 2);
    {
        std::lock_guard lock(p.mutex);
        p.jobs.push_back(std::move(a));
    }
    p.wake.notify_one();
}

void VideoRecorder::audio_float(const float *samples, size_t frames, unsigned channels, unsigned rate) {
    auto &p = *p_;
    if (!p.running || !p.with_audio || !frames || !channels || !rate)
        return;
    auto to16 = [](float v) { return int16_t(std::lround(std::clamp(v, -1.f, 1.f) * 32767.f)); };
    auto fold = [&](size_t i, float &l, float &r) {
        const float *f = samples + i * channels;
        l = f[0];
        r = channels > 1 ? f[1] : f[0];
        if (channels >= 6) { // 5.1/7.1: fold centre and surrounds into the front pair
            l += .707f * f[2] + .707f * f[4];
            r += .707f * f[2] + .707f * f[5];
        }
    };
    std::vector<int16_t> stereo;
    if (rate == 48000) {
        stereo.resize(frames * 2);
        for (size_t i = 0; i < frames; ++i) {
            float l, r;
            fold(i, l, r);
            stereo[i * 2] = to16(l);
            stereo[i * 2 + 1] = to16(r);
        }
    } else { // linear resampling, continuous across calls
        const double step = double(rate) / 48000;
        for (;;) {
            const double at = p.resample_position;
            if (at >= double(frames))
                break;
            const size_t i = size_t(at);
            const float t = float(at - double(i));
            float l1, r1;
            fold(i, l1, r1);
            const float l0 = i ? 0 : p.resample_last[0], r0 = i ? 0 : p.resample_last[1];
            float la = l0, ra = r0;
            if (i) fold(i - 1, la, ra);
            stereo.push_back(to16(la + (l1 - la) * t));
            stereo.push_back(to16(ra + (r1 - ra) * t));
            p.resample_position += step;
        }
        p.resample_position -= double(frames);
        fold(frames - 1, p.resample_last[0], p.resample_last[1]);
    }
    audio(stereo.data(), stereo.size() / 2);
}

bool VideoRecorder::start_nv12(const std::filesystem::path &file, const CaptureSettings &settings, unsigned width,
                               unsigned height, bool with_audio, std::string &error) {
    return begin(file, settings, width & ~1u, height & ~1u, with_audio, true, error);
}

bool VideoRecorder::nv12_input() const { return p_->nv12; }

void VideoRecorder::frame_nv12(const uint8_t *nv12, size_t pitch, double seconds) {
    auto &p = *p_;
    if (!p.running || !p.nv12)
        return;
    const int64_t slot = p.last_slot < 0 ? 0 : std::max<int64_t>(0, std::llround(seconds * p.settings.fps));
    if (slot <= p.last_slot)
        return; // two frames in one slot: keep the first
    Impl::Video v;
    v.slot = slot;
    const unsigned rows = p.out_h + p.out_h / 2;
    v.rgba.resize(size_t(p.out_w) * rows);
    for (unsigned y = 0; y < rows; ++y)
        std::memcpy(v.rgba.data() + size_t(y) * p.out_w, nv12 + y * pitch, p.out_w);
    {
        std::lock_guard lock(p.mutex);
        if (p.queued_video >= 8) { // the encoder is behind: the previous frame stays on screen longer
            std::lock_guard stats(p.stats_mutex);
            ++p.stats.dropped;
            return;
        }
        p.jobs.push_back(std::move(v));
        ++p.queued_video;
    }
    p.last_slot = slot;
    p.wake.notify_one();
}

VideoRecorder::Stats VideoRecorder::stop() {
    auto &p = *p_;
    if (!p.worker.joinable())
        return stats();
    p.running = false;
    {
        std::lock_guard lock(p.mutex);
        p.stopping = true;
    }
    p.wake.notify_one();
    p.worker.join();
    return stats();
}

bool VideoRecorder::active() const { return p_ && p_->running; }
VideoRecorder::Stats VideoRecorder::stats() const {
    std::lock_guard lock(p_->stats_mutex);
    return p_->stats;
}
unsigned VideoRecorder::source_width() const { return p_->source_w; }
unsigned VideoRecorder::source_height() const { return p_->source_h; }

// ---------------------------------------------------------------- D3D11 readback


// GPU scaler + RGB -> NV12 (BT.709, 16-235) into one R8 target: rows [0, out_h) are luma and
// rows [out_h, out_h*3/2) are interleaved Cb/Cr, byte-for-byte the NV12 layout the encoder takes.
// Each output sample averages a grid of bilinear taps spanning its source footprint (box filter).
constexpr char nv12_shader[] = R"(
Texture2D<float4> source : register(t0);
SamplerState linear_clamp : register(s0);
cbuffer Frame : register(b0) { float2 source_size; float2 out_size; float2 ratio; float luma_taps; float chroma_taps; };
float4 vs(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
}
float3 box(float2 centre, float2 extent, float taps) {
    float2 c = centre * ratio, e = extent * ratio;
    int n = int(taps);
    float3 sum = 0;
    [loop] for (int j = 0; j < n; ++j)
        [loop] for (int i = 0; i < n; ++i)
            sum += source.SampleLevel(linear_clamp, (c - e * 0.5 + e * (float2(i, j) + 0.5) / n) / source_size, 0).rgb;
    return sum / (n * n);
}
float ps(float4 position : SV_Position) : SV_Target {
    const float3 k = float3(0.2126, 0.7152, 0.0722);
    float2 p = floor(position.xy);
    if (p.y < out_size.y)
        return (16.0 + 219.0 * dot(saturate(box(p + 0.5, float2(1, 1), luma_taps)), k)) / 255.0;
    float pair = floor(p.x * 0.5);
    float3 c = saturate(box(float2(pair * 2 + 1, (p.y - out_size.y) * 2 + 1), float2(2, 2), chroma_taps));
    float y = dot(c, k);
    float v = (p.x - pair * 2) >= 1 ? (c.r - y) / 1.5748 : (c.b - y) / 1.8556;
    return (128.0 + 224.0 * v) / 255.0;
}
)";

struct D3D11FrameGrabber::Impl {
    struct Slot {
        ComPtr<ID3D11Texture2D> staging;
        double seconds = 0;
        bool pending = false;
    };
    std::array<Slot, 3> ring;
    unsigned next = 0, width = 0, height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::vector<uint8_t> swizzled;
    // NV12 path: the ring holds R8 staging copies of `target` (out_w x out_h*3/2).
    bool nv12 = false;
    int nv12_state = 0; // 0 untried, 1 ready, -1 unavailable
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11Texture2D> copy, target;
    ComPtr<ID3D11ShaderResourceView> copy_view;
    ComPtr<ID3D11RenderTargetView> target_view;
    unsigned out_w = 0, out_h = 0;

    bool prepare_shaders(ID3D11Device *device) {
        if (nv12_state)
            return nv12_state > 0;
        nv12_state = -1;
        ComPtr<ID3DBlob> vsb, psb, err;
        if (FAILED(D3DCompile(nv12_shader, sizeof nv12_shader - 1, "capture_nv12", nullptr, nullptr, "vs", "vs_4_0", 0, 0, &vsb, &err)) ||
            FAILED(D3DCompile(nv12_shader, sizeof nv12_shader - 1, "capture_nv12", nullptr, nullptr, "ps", "ps_4_0", 0, 0, &psb, &err)))
            return false;
        if (FAILED(device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs)) ||
            FAILED(device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps)))
            return false;
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(&sd, &sampler)))
            return false;
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 32;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&bd, nullptr, &constants)))
            return false;
        nv12_state = 1;
        return true;
    }

    // (Re)creates the source copy, the NV12 target and its staging ring for these sizes.
    bool prepare_targets(ID3D11Device *device, const D3D11_TEXTURE2D_DESC &d, unsigned w, unsigned h) {
        if (nv12 && copy && d.Width == width && d.Height == height && d.Format == format && w == out_w && h == out_h)
            return true;
        D3D11_TEXTURE2D_DESC cd = d;
        cd.Usage = D3D11_USAGE_DEFAULT;
        cd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        cd.CPUAccessFlags = 0;
        cd.MiscFlags = 0;
        cd.MipLevels = cd.ArraySize = 1;
        cd.SampleDesc = {1, 0};
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h + h / 2;
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8_UNORM;
        td.SampleDesc = {1, 0};
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        copy.Reset();
        copy_view.Reset();
        target.Reset();
        target_view.Reset();
        if (FAILED(device->CreateTexture2D(&cd, nullptr, &copy)) || FAILED(device->CreateShaderResourceView(copy.Get(), nullptr, &copy_view)) ||
            FAILED(device->CreateTexture2D(&td, nullptr, &target)) || FAILED(device->CreateRenderTargetView(target.Get(), nullptr, &target_view)))
            return false;
        D3D11_TEXTURE2D_DESC sd = td;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (auto &slot : ring) {
            slot = {};
            if (FAILED(device->CreateTexture2D(&sd, nullptr, &slot.staging)))
                return false;
        }
        nv12 = true;
        width = d.Width;
        height = d.Height;
        format = d.Format;
        out_w = w;
        out_h = h;
        next = 0;
        return true;
    }

    // Draws the NV12 conversion into `target`, saving and restoring the state it touches.
    void convert(ID3D11DeviceContext *context) {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(context->Map(constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            const float rx = float(width) / out_w, ry = float(height) / out_h;
            const float luma = std::clamp(std::ceil(std::max(rx, ry)), 1.f, 4.f);
            const float chroma = std::clamp(std::ceil(2 * std::max(rx, ry)), 1.f, 8.f);
            const float values[8] = {float(width), float(height), float(out_w), float(out_h), rx, ry, luma, chroma};
            std::memcpy(m.pData, values, sizeof values);
            context->Unmap(constants.Get(), 0);
        }
        // Saved state.
        D3D11_PRIMITIVE_TOPOLOGY topology{};
        ComPtr<ID3D11InputLayout> layout;
        ComPtr<ID3D11VertexShader> old_vs;
        ComPtr<ID3D11PixelShader> old_ps;
        ComPtr<ID3D11ShaderResourceView> old_srv;
        ComPtr<ID3D11SamplerState> old_sampler;
        ComPtr<ID3D11Buffer> old_cb;
        ComPtr<ID3D11RasterizerState> old_rs;
        ComPtr<ID3D11BlendState> old_blend;
        float blend_factor[4]{};
        UINT sample_mask = 0;
        ComPtr<ID3D11DepthStencilState> old_ds;
        UINT stencil_ref = 0;
        ComPtr<ID3D11RenderTargetView> old_rtv;
        ComPtr<ID3D11DepthStencilView> old_dsv;
        UINT viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        context->IAGetPrimitiveTopology(&topology);
        context->IAGetInputLayout(&layout);
        context->VSGetShader(&old_vs, nullptr, nullptr);
        context->PSGetShader(&old_ps, nullptr, nullptr);
        context->PSGetShaderResources(0, 1, &old_srv);
        context->PSGetSamplers(0, 1, &old_sampler);
        context->PSGetConstantBuffers(0, 1, &old_cb);
        context->RSGetState(&old_rs);
        context->RSGetViewports(&viewport_count, viewports);
        context->OMGetBlendState(&old_blend, blend_factor, &sample_mask);
        context->OMGetDepthStencilState(&old_ds, &stencil_ref);
        context->OMGetRenderTargets(1, &old_rtv, &old_dsv);

        ID3D11ShaderResourceView *none = nullptr;
        context->PSSetShaderResources(0, 1, &none);
        ID3D11RenderTargetView *rtv = target_view.Get();
        context->OMSetRenderTargets(1, &rtv, nullptr);
        context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        context->OMSetDepthStencilState(nullptr, 0);
        context->RSSetState(nullptr);
        const D3D11_VIEWPORT viewport{0, 0, float(out_w), float(out_h + out_h / 2), 0, 1};
        context->RSSetViewports(1, &viewport);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->IASetInputLayout(nullptr);
        context->VSSetShader(vs.Get(), nullptr, 0);
        context->PSSetShader(ps.Get(), nullptr, 0);
        ID3D11ShaderResourceView *view = copy_view.Get();
        context->PSSetShaderResources(0, 1, &view);
        ID3D11SamplerState *s = sampler.Get();
        context->PSSetSamplers(0, 1, &s);
        ID3D11Buffer *cb = constants.Get();
        context->PSSetConstantBuffers(0, 1, &cb);
        context->Draw(3, 0);

        context->PSSetShaderResources(0, 1, &none);
        context->IASetPrimitiveTopology(topology);
        context->IASetInputLayout(layout.Get());
        context->VSSetShader(old_vs.Get(), nullptr, 0);
        context->PSSetShader(old_ps.Get(), nullptr, 0);
        ID3D11ShaderResourceView *srv = old_srv.Get();
        context->PSSetShaderResources(0, 1, &srv);
        ID3D11SamplerState *smp = old_sampler.Get();
        context->PSSetSamplers(0, 1, &smp);
        ID3D11Buffer *ocb = old_cb.Get();
        context->PSSetConstantBuffers(0, 1, &ocb);
        context->RSSetState(old_rs.Get());
        context->RSSetViewports(viewport_count, viewports);
        context->OMSetBlendState(old_blend.Get(), blend_factor, sample_mask);
        context->OMSetDepthStencilState(old_ds.Get(), stencil_ref);
        ID3D11RenderTargetView *ortv = old_rtv.Get();
        context->OMSetRenderTargets(1, &ortv, old_dsv.Get());
    }

    bool deliver(ID3D11DeviceContext *context, Slot &slot, bool wait, VideoRecorder &recorder) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT hr = context->Map(slot.staging.Get(), 0, D3D11_MAP_READ, wait ? 0 : D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING)
            return false;
        slot.pending = false;
        if (FAILED(hr))
            return true;
        if (nv12) {
            recorder.frame_nv12(static_cast<const uint8_t *>(mapped.pData), mapped.RowPitch, slot.seconds);
            context->Unmap(slot.staging.Get(), 0);
            return true;
        }
        const bool bgra = format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        if (bgra) {
            swizzled.resize(size_t(width) * height * 4);
            for (unsigned y = 0; y < height; ++y) {
                const uint8_t *s = static_cast<const uint8_t *>(mapped.pData) + size_t(y) * mapped.RowPitch;
                uint8_t *d = swizzled.data() + size_t(y) * width * 4;
                for (unsigned x = 0; x < width; ++x, s += 4, d += 4) {
                    d[0] = s[2];
                    d[1] = s[1];
                    d[2] = s[0];
                    d[3] = s[3];
                }
            }
            recorder.frame(swizzled.data(), size_t(width) * 4, slot.seconds);
        } else
            recorder.frame(static_cast<const uint8_t *>(mapped.pData), mapped.RowPitch, slot.seconds);
        context->Unmap(slot.staging.Get(), 0);
        return true;
    }

    // Deliver finished copies oldest first; stop at the first still on the GPU.
    void drain(ID3D11DeviceContext *context, VideoRecorder &recorder, bool wait) {
        for (unsigned k = 0; k < ring.size(); ++k) {
            auto &slot = ring[(next + k) % ring.size()];
            if (slot.pending && !deliver(context, slot, wait, recorder))
                return;
        }
    }
};

D3D11FrameGrabber::D3D11FrameGrabber() : p_(std::make_unique<Impl>()) {}
D3D11FrameGrabber::~D3D11FrameGrabber() = default;

void D3D11FrameGrabber::grab(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *source,
                             double seconds, VideoRecorder &recorder) {
    auto &p = *p_;
    D3D11_TEXTURE2D_DESC d{};
    source->GetDesc(&d);
    if (p.nv12 || d.Width != p.width || d.Height != p.height || d.Format != p.format) {
        flush(context, recorder);
        p.nv12 = false;
        p.width = d.Width;
        p.height = d.Height;
        p.format = d.Format;
        for (auto &slot : p.ring) {
            D3D11_TEXTURE2D_DESC s = d;
            s.Usage = D3D11_USAGE_STAGING;
            s.BindFlags = 0;
            s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            s.MiscFlags = 0;
            s.MipLevels = 1;
            s.ArraySize = 1;
            s.SampleDesc = {1, 0};
            slot = {};
            if (FAILED(device->CreateTexture2D(&s, nullptr, &slot.staging)))
                return;
        }
    }
    if (d.Width != recorder.source_width() || d.Height != recorder.source_height())
        return; // the window was resized; the recording keeps its starting size
    auto &slot = p.ring[p.next];
    if (slot.pending) // the ring is full: this copy has to finish before it is reused
        p.deliver(context, slot, true, recorder);
    context->CopyResource(slot.staging.Get(), source);
    slot.seconds = seconds;
    slot.pending = true;
    p.next = (p.next + 1) % p.ring.size();
    p.drain(context, recorder, false);
}

bool D3D11FrameGrabber::nv12_supported(ID3D11Device *device) { return device && p_->prepare_shaders(device); }

bool D3D11FrameGrabber::grab_nv12(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *source,
                                  double seconds, VideoRecorder &recorder) {
    auto &p = *p_;
    if (!recorder.nv12_input() || !p.prepare_shaders(device))
        return false;
    D3D11_TEXTURE2D_DESC d{};
    source->GetDesc(&d);
    const unsigned w = recorder.source_width(), h = recorder.source_height();
    if (!p.prepare_targets(device, d, w, h)) {
        flush(context, recorder);
        return false;
    }
    auto &slot = p.ring[p.next];
    if (slot.pending) // the ring is full: this copy has to finish before it is reused
        p.deliver(context, slot, true, recorder);
    context->CopyResource(p.copy.Get(), source);
    p.convert(context);
    context->CopyResource(slot.staging.Get(), p.target.Get());
    slot.seconds = seconds;
    slot.pending = true;
    p.next = (p.next + 1) % p.ring.size();
    p.drain(context, recorder, false);
    return true;
}

void D3D11FrameGrabber::flush(ID3D11DeviceContext *context, VideoRecorder &recorder) {
    p_->drain(context, recorder, true);
}

void D3D11FrameGrabber::reset() { p_ = std::make_unique<Impl>(); }

bool read_texture_rgba(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *source,
                       std::vector<uint8_t> &rgba, unsigned &width, unsigned &height, std::string &error) {
    D3D11_TEXTURE2D_DESC d{};
    source->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.SampleDesc = {1, 0};
    ComPtr<ID3D11Texture2D> staging;
    HRESULT hr = device->CreateTexture2D(&d, nullptr, &staging);
    if (FAILED(hr)) {
        error = hresult_text("Creating the screenshot buffer", hr);
        return false;
    }
    context->CopyResource(staging.Get(), source);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        error = hresult_text("Reading the screenshot", hr);
        return false;
    }
    width = d.Width;
    height = d.Height;
    rgba.resize(size_t(width) * height * 4);
    const bool bgra = d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    for (unsigned y = 0; y < height; ++y) {
        const uint8_t *s = static_cast<const uint8_t *>(mapped.pData) + size_t(y) * mapped.RowPitch;
        uint8_t *out = rgba.data() + size_t(y) * width * 4;
        std::memcpy(out, s, size_t(width) * 4);
        if (bgra)
            for (unsigned x = 0; x < width; ++x)
                std::swap(out[x * 4], out[x * 4 + 2]);
    }
    context->Unmap(staging.Get(), 0);
    return true;
}

} // namespace media
