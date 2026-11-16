#include "movie.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>

namespace sm {
struct Movie::Impl {
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    bool cancel = false, done = false;
    HANDLE child = nullptr;
    int displayed = -1;
    struct Frame {
        unsigned index;
        hig::GimImage image;
    };
    std::deque<Frame> frames;
    std::shared_ptr<hig::AudioSample> audio;
    std::filesystem::path temporary;
    std::wstring ffmpeg;
    std::string failure;
    hig::MovieInfo metadata;

    bool pipe(const std::wstring &options, const std::function<bool(HANDLE)> &consume) {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE reader = nullptr, writer = nullptr;
        if (!CreatePipe(&reader, &writer, &sa, 0))
            return false;
        SetHandleInformation(reader, HANDLE_FLAG_INHERIT, 0);
        HANDLE nul =
            CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        &sa, OPEN_EXISTING, 0, nullptr);
        STARTUPINFOEXW si{};
        si.StartupInfo.cb = sizeof(si);
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = si.StartupInfo.hStdError = nul;
        si.StartupInfo.hStdOutput = writer;
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        std::vector<uint8_t> storage(bytes);
        si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        HANDLE inherited[] = {writer, nul};
        bool attrs = InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &bytes) != FALSE;
        if (!attrs ||
            !UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                       inherited, sizeof(inherited), nullptr, nullptr)) {
            if (attrs)
                DeleteProcThreadAttributeList(si.lpAttributeList);
            CloseHandle(reader);
            CloseHandle(writer);
            CloseHandle(nul);
            return false;
        }
        std::wstring command = L"\"" + ffmpeg + L"\" -nostdin -v error -threads 2 -i \"" +
                               temporary.wstring() + L"\" " + options + L" pipe:1";
        PROCESS_INFORMATION pi{};
        bool launched = CreateProcessW(ffmpeg.c_str(), command.data(), nullptr, nullptr, TRUE,
                                       CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                                       nullptr, &si.StartupInfo, &pi) != FALSE;
        DeleteProcThreadAttributeList(si.lpAttributeList);
        CloseHandle(writer);
        CloseHandle(nul);
        if (!launched) {
            CloseHandle(reader);
            return false;
        }
        CloseHandle(pi.hThread);
        {
            std::lock_guard lock(mutex);
            child = pi.hProcess;
            if (cancel)
                TerminateProcess(child, 1);
        }
        bool ok = consume(reader);
        CloseHandle(reader);
        if (!ok)
            TerminateProcess(pi.hProcess, 1);
        if (WaitForSingleObject(pi.hProcess, 5000) != WAIT_OBJECT_0) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 1000);
            ok = false;
        }
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        {
            std::lock_guard lock(mutex);
            child = nullptr;
            CloseHandle(pi.hProcess);
        }
        return ok && code == 0;
    }
    void decode() {
        auto pcm = std::make_shared<hig::AudioSample>();
        pcm->channels = 2;
        pcm->rate = 48000;
        bool ok = true;
        if (metadata.audio_tracks)
            ok = pipe(L"-map 0:a:0 -vn -ac 2 -ar 48000 -f s16le", [&](HANDLE input) {
                std::array<uint8_t, 16384> chunk{};
                std::vector<uint8_t> bytes;
                DWORD count;
                while (ReadFile(input, chunk.data(), sizeof(chunk), &count, nullptr) && count) {
                    if (bytes.size() + count > size_t((metadata.duration() + 2) * 192000))
                        return false;
                    bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + count);
                    std::lock_guard lock(mutex);
                    if (cancel)
                        return false;
                }
                if (bytes.empty() || bytes.size() % 4)
                    return false;
                pcm->pcm.resize(bytes.size() / 2);
                std::memcpy(pcm->pcm.data(), bytes.data(), bytes.size());
                return true;
            });
        if (ok) {
            {
                std::lock_guard lock(mutex);
                audio = pcm;
            }
            ok = pipe(L"-map 0:v:0 -an -fps_mode passthrough -pix_fmt rgba -f rawvideo",
                      [&](HANDLE input) {
                          for (unsigned index = 0; index < metadata.frames; index++) {
                              hig::GimImage image{metadata.width, metadata.height, {}};
                              image.rgba.resize(size_t(metadata.width) * metadata.height * 4);
                              size_t offset = 0;
                              DWORD count = 0;
                              while (offset < image.rgba.size()) {
                                  if (!ReadFile(input, image.rgba.data() + offset,
                                                DWORD(image.rgba.size() - offset), &count,
                                                nullptr) ||
                                      !count)
                                      return false;
                                  offset += count;
                              }
                              std::unique_lock lock(mutex);
                              wake.wait(lock, [&] { return cancel || frames.size() < 4; });
                              if (cancel)
                                  return false;
                              frames.push_back({index, std::move(image)});
                          }
                          return true;
                      });
        }
        std::lock_guard lock(mutex);
        done = true;
        if (!ok && !cancel)
            failure = "The original movie could not be decoded by FFmpeg.";
    }
};
Movie::Movie() : p_(std::make_unique<Impl>()) {}
Movie::~Movie() { stop(); }
void Movie::stop() {
    {
        std::lock_guard lock(p_->mutex);
        p_->cancel = true;
        if (p_->child)
            TerminateProcess(p_->child, 1);
    }
    p_->wake.notify_all();
    if (p_->worker.joinable())
        p_->worker.join();
    {
        std::lock_guard lock(p_->mutex);
        p_->frames.clear();
        p_->audio.reset();
    }
    if (!p_->temporary.empty()) {
        std::error_code ec;
        std::filesystem::remove(p_->temporary, ec);
        p_->temporary.clear();
    }
}
bool Movie::open(const hig::IsoFs &disc, const std::string &path, std::string &error) {
    stop();
    p_ = std::make_unique<Impl>();
    if (!hig::movie_info(disc, path, p_->metadata, error))
        return false;
    wchar_t binary[32768];
    DWORD length = SearchPathW(nullptr, L"ffmpeg.exe", nullptr, 32768, binary, nullptr);
    if (!length || length >= 32768) {
        error = "Movie playback needs FFmpeg on PATH.";
        return false;
    }
    p_->ffmpeg.assign(binary, length);
    wchar_t folder[MAX_PATH], temporary[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, folder) || !GetTempFileNameW(folder, L"smv", 0, temporary)) {
        error = "Cannot create movie cache";
        return false;
    }
    p_->temporary = temporary;
    auto data = disc.read(path);
    std::ofstream stream(p_->temporary, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char *>(data.data()), data.size());
    stream.close();
    if (data.empty() || !stream) {
        error = "Cannot cache original ISO movie";
        stop();
        return false;
    }
    p_->worker = std::thread([this] { p_->decode(); });
    error.clear();
    return true;
}
bool Movie::frame(double seconds, hig::GimImage &out) {
    std::lock_guard lock(p_->mutex);
    if (!std::isfinite(seconds) || !p_->metadata.rate_divisor)
        return false;
    bool changed = false;
    unsigned wanted = unsigned(std::clamp(seconds * p_->metadata.rate / p_->metadata.rate_divisor,
                                          0., double(p_->metadata.frames)));
    while (!p_->frames.empty() && p_->frames.front().index <= wanted) {
        p_->displayed = int(p_->frames.front().index);
        out = std::move(p_->frames.front().image);
        p_->frames.pop_front();
        changed = true;
    }
    if (changed)
        p_->wake.notify_all();
    return changed;
}
std::shared_ptr<hig::AudioSample> Movie::soundtrack() const {
    std::lock_guard lock(p_->mutex);
    return p_->audio;
}
std::string Movie::error() const {
    std::lock_guard lock(p_->mutex);
    return p_->failure;
}
const hig::MovieInfo &Movie::info() const { return p_->metadata; }
bool Movie::decoding_finished() const {
    std::lock_guard lock(p_->mutex);
    return p_->done;
}
int Movie::displayed_frame() const {
    std::lock_guard lock(p_->mutex);
    return p_->displayed;
}
} // namespace sm
