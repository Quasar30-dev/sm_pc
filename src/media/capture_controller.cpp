#include "capture_controller.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <cstring>
#include <thread>

namespace media {
namespace {

std::string utf8(const std::filesystem::path &p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

std::filesystem::path from_utf8(const char *s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s)));
}

std::string clock_text(double seconds) {
    const int total = int(seconds);
    char text[16];
    std::snprintf(text, sizeof text, "%02d:%02d", total / 60, total % 60);
    return text;
}

} // namespace

CaptureController::CaptureController(std::string game, std::string prefix, std::filesystem::path settings_file)
    : game_(std::move(game)), prefix_(std::move(prefix)), settings_file_(std::move(settings_file)) {
    settings_ = load_capture_settings(settings_file_, game_);
    record_key_ = ImGuiKey_F9;
    std::snprintf(folder_text_, sizeof folder_text_, "%s", utf8(settings_.folder).c_str());
}

void CaptureController::set_window(void *hwnd) {
    hwnd_ = hwnd;
    if (!hwnd_)
        return;
    wchar_t title[512] = {};
    GetWindowTextW(static_cast<HWND>(hwnd_), title, 512);
    base_title_ = title;
}

void CaptureController::save() {
    if (!save_capture_settings(settings_file_, settings_))
        message_ = "Could not save the capture settings to " + utf8(settings_file_);
}

void CaptureController::update_title() {
    if (!hwnd_)
        return;
    int second = -1;
    if (recording())
        second = int(recorder_.stats().seconds);
    if (second == last_title_second_)
        return;
    last_title_second_ = second;
    std::wstring title = base_title_;
    if (second >= 0) {
        const auto t = clock_text(second);
        title += L"  ● REC " + std::wstring(t.begin(), t.end());
    }
    SetWindowTextW(static_cast<HWND>(hwnd_), title.c_str());
}

void CaptureController::request_recording(std::optional<std::filesystem::path> file) {
    want_start_ = true;
    start_file_ = std::move(file);
}

void CaptureController::request_screenshot(std::optional<std::filesystem::path> file) {
    want_shot_ = true;
    shot_file_ = std::move(file);
}

void CaptureController::toggle_recording() {
    if (recording())
        want_stop_ = true;
    else
        request_recording();
}

void CaptureController::ui(bool has_audio) {
    has_audio_ = has_audio;
    if (ImGui::IsKeyPressed(ImGuiKey(record_key_), false))
        toggle_recording();
    if (ImGui::IsKeyPressed(ImGuiKey_F11, false))
        window_open_ = !window_open_;
    if (ImGui::IsKeyPressed(ImGuiKey_F12, false))
        request_screenshot();
    if (!window_open_ || hide_overlays())
        return;

    ImGui::SetNextWindowSize(ImVec2(470, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Capture (F11)", &window_open_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    if (recording()) {
        const auto s = recorder_.stats();
        ImGui::TextColored(ImVec4(1, .35f, .3f, 1), "Recording %s", clock_text(s.seconds).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%llu frames, %llu dropped", (unsigned long long)s.frames, (unsigned long long)s.dropped);
        if (ImGui::Button((std::string("Stop recording (") + ImGui::GetKeyName(ImGuiKey(record_key_)) + ")").c_str()))
            want_stop_ = true;
    } else if (ImGui::Button((std::string("Start recording (") + ImGui::GetKeyName(ImGuiKey(record_key_)) + ")").c_str()))
        request_recording();
    ImGui::SameLine();
    if (ImGui::Button("Screenshot (F12)"))
        request_screenshot();
    ImGui::SameLine();
    if (ImGui::Button("Open folder"))
        open_folder(settings_.folder);

    ImGui::SeparatorText("Where");
    ImGui::SetNextItemWidth(-80);
    if (ImGui::InputText("##folder", folder_text_, sizeof folder_text_, ImGuiInputTextFlags_EnterReturnsTrue) ||
        ImGui::IsItemDeactivatedAfterEdit()) {
        settings_.folder = from_utf8(folder_text_);
        save();
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        if (auto chosen = pick_folder(hwnd_, settings_.folder); !chosen.empty()) {
            settings_.folder = chosen;
            std::snprintf(folder_text_, sizeof folder_text_, "%s", utf8(chosen).c_str());
            save();
        }
    }

    ImGui::BeginDisabled(recording());
    ImGui::SeparatorText("Video");
    bool changed = false;
    int quality = int(settings_.quality);
    const char *qualities[] = {"Low", "Medium", "High", "Ultra"};
    if (ImGui::Combo("Quality", &quality, qualities, 4)) {
        settings_.quality = Quality(quality);
        changed = true;
    }
    int fps = settings_.fps == 30 ? 0 : 1;
    const char *rates[] = {"30 fps", "60 fps"};
    if (ImGui::Combo("Frame rate", &fps, rates, 2)) {
        settings_.fps = fps ? 60 : 30;
        changed = true;
    }
    const unsigned heights[] = {0, 1440, 1080, 720};
    const char *sizes[] = {"Window size", "1440p", "1080p", "720p"};
    int size = 0;
    for (int i = 0; i < 4; ++i)
        if (heights[i] == settings_.height)
            size = i;
    if (ImGui::Combo("Resolution", &size, sizes, 4)) {
        settings_.height = heights[size];
        changed = true;
    }
    ImGui::BeginDisabled(!has_audio);
    changed |= ImGui::Checkbox(has_audio ? "Record game audio" : "Record game audio (not available here)", &settings_.audio);
    ImGui::EndDisabled();
    changed |= ImGui::Checkbox("Hide debug panels while recording", &settings_.hide_overlays);
    ImGui::EndDisabled();

    ImGui::SeparatorText("Screenshots");
    int format = settings_.screenshot_format == "jpg" ? 1 : 0;
    const char *formats[] = {"PNG (lossless)", "JPEG"};
    if (ImGui::Combo("Format", &format, formats, 2)) {
        settings_.screenshot_format = format ? "jpg" : "png";
        changed = true;
    }
    if (format) {
        int q = int(settings_.jpeg_quality);
        if (ImGui::SliderInt("JPEG quality", &q, 50, 100)) {
            settings_.jpeg_quality = unsigned(q);
            changed = true;
        }
    }
    if (changed)
        save();
    if (!message_.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", message_.c_str());
    }
    ImGui::End();
}

void CaptureController::pause_panel(bool heading) {
    ImGui::PushID("capture_pause_panel");
    if (heading)
        ImGui::SeparatorText("Capture");
    const std::string key = ImGui::GetKeyName(ImGuiKey(record_key_));
    if (recording()) {
        const auto s = recorder_.stats();
        ImGui::TextColored(ImVec4(1, .35f, .3f, 1), "REC %s", clock_text(s.seconds).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%llu frames, %llu dropped", (unsigned long long)s.frames, (unsigned long long)s.dropped);
        if (ImGui::Button(("Stop recording (" + key + ")").c_str()))
            want_stop_ = true;
    } else if (ImGui::Button(("Start recording (" + key + ")").c_str()))
        request_recording();
    ImGui::SameLine();
    if (ImGui::Button("Screenshot (F12)"))
        request_screenshot();
    ImGui::SameLine();
    if (ImGui::Button("Open folder"))
        open_folder(settings_.folder);

    ImGui::BeginDisabled(recording());
    bool changed = false;
    const float field = std::max(90.f, ImGui::GetContentRegionAvail().x * .3f);
    // 1080p and 720p are the targets; 1440p is only offered in the F11 window.
    const char *label = settings_.height == 1080 ? "1080p" : settings_.height == 720 ? "720p" : settings_.height ? "1440p" : "Window size";
    ImGui::SetNextItemWidth(field);
    if (ImGui::BeginCombo("Resolution", label)) {
        for (unsigned h : {1080u, 720u, 0u})
            if (ImGui::Selectable(h == 1080 ? "1080p" : h == 720 ? "720p" : "Window size", settings_.height == h)) {
                settings_.height = h;
                changed = true;
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    int fps = settings_.fps == 30 ? 0 : 1;
    ImGui::SetNextItemWidth(field * .6f);
    if (ImGui::Combo("Frame rate", &fps, "30 fps\0" "60 fps\0")) {
        settings_.fps = fps ? 60 : 30;
        changed = true;
    }
    int quality = int(settings_.quality);
    ImGui::SetNextItemWidth(field);
    if (ImGui::Combo("Quality", &quality, "Low\0Medium\0High\0Ultra\0")) {
        settings_.quality = Quality(quality);
        changed = true;
    }
    ImGui::BeginDisabled(!has_audio_);
    changed |= ImGui::Checkbox("Game audio", &settings_.audio);
    ImGui::EndDisabled();
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Hide debug panels while recording", &settings_.hide_overlays);
    ImGui::EndDisabled();
    if (changed)
        save();
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s record, F12 screenshot, F11 more options. Saved to %s", key.c_str(), utf8(settings_.folder).c_str());
    if (!message_.empty())
        ImGui::TextUnformatted(message_.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopID();
}

void CaptureController::end_frame(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *back_buffer,
                                  std::optional<double> fixed_seconds) {
    if (!device || !context || !back_buffer)
        return;
    if (want_shot_) {
        want_shot_ = false;
        std::vector<uint8_t> rgba;
        unsigned w = 0, h = 0;
        std::string error;
        const auto ext = settings_.screenshot_format == "jpg" ? "jpg" : "png";
        const auto file = shot_file_.value_or(timestamped_path(settings_.folder, prefix_, ext));
        shot_file_.reset();
        if (read_texture_rgba(device, context, back_buffer, rgba, w, h, error) &&
            save_image(file, rgba.data(), w, h, size_t(w) * 4, file.extension() == ".jpg" ? "jpg" : "png",
                       settings_.jpeg_quality, error))
            message_ = "Screenshot saved: " + utf8(file);
        else
            message_ = "Screenshot failed: " + error;
    }
    if (want_stop_) {
        want_stop_ = false;
        stop(context);
    }
    if (want_start_ && !recording()) {
        want_start_ = false;
        D3D11_TEXTURE2D_DESC d{};
        back_buffer->GetDesc(&d);
        const auto file = start_file_.value_or(timestamped_path(settings_.folder, prefix_, "mp4"));
        start_file_.reset();
        std::string error;
        grabber_.reset();
        first_fixed_.reset();
        // Preferred: scale and convert on the GPU to NV12 at the chosen height (720p/1080p...),
        // so the main thread reads back ~3 MB per 1080p frame instead of the full RGBA window.
        bool started = false;
        if (grabber_.nv12_supported(device)) {
            const unsigned out_h = (settings_.height ? settings_.height : d.Height) & ~1u;
            const unsigned out_w = unsigned(std::lround(double(d.Width) * out_h / d.Height)) & ~1u;
            started = recorder_.start_nv12(file, settings_, out_w, out_h, settings_.audio && has_audio_, error);
        }
        if (!started) {
            error.clear();
            started = recorder_.start(file, settings_, d.Width, d.Height, settings_.audio && has_audio_, error);
        }
        if (started) {
            started_ = std::chrono::steady_clock::now();
            message_ = "Recording to " + utf8(file);
        } else
            message_ = "Could not start recording: " + error;
    }
    if (recording()) {
        double seconds;
        if (fixed_seconds) {
            if (!first_fixed_)
                first_fixed_ = *fixed_seconds;
            seconds = *fixed_seconds - *first_fixed_;
            // Scripted runs simulate faster than real time; keep pace so audio stays in sync.
            if (settings_.audio && has_audio_)
                std::this_thread::sleep_until(started_ + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                             std::chrono::duration<double>(seconds)));
        } else
            seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
        if (!recorder_.nv12_input())
            grabber_.grab(device, context, back_buffer, seconds, recorder_);
        else if (!grabber_.grab_nv12(device, context, back_buffer, seconds, recorder_)) {
            message_ = "Recording stopped: the GPU frame converter failed";
            stop(context);
        }
        if (const auto s = recorder_.stats(); !s.error.empty()) {
            message_ = s.error;
            stop(context);
        }
    }
    update_title();
}

VideoRecorder::Stats CaptureController::stop(ID3D11DeviceContext *context) {
    if (!recording())
        return recorder_.stats();
    if (context)
        grabber_.flush(context, recorder_);
    const auto s = recorder_.stop();
    if (s.error.empty()) {
        char text[96];
        std::snprintf(text, sizeof text, " (%s, %llu frames, %llu dropped)", clock_text(s.seconds).c_str(),
                      (unsigned long long)s.frames, (unsigned long long)s.dropped);
        message_ = "Saved: " + utf8(s.file) + text;
    } else
        message_ = "Recording failed: " + s.error;
    update_title();
    return s;
}

} // namespace media
