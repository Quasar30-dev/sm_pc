#pragma once
// Engine-side capture: hotkeys (F9 record, F11 settings, F12 screenshot), the ImGui settings
// window, frame timing and the window-title recording indicator. Shared by future_pc and sm_pc.
#include "capture.h"

#include <chrono>
#include <optional>

namespace media {

class CaptureController {
public:
    // game names the default folder (Videos\<game>); prefix starts every file name.
    CaptureController(std::string game, std::string prefix, std::filesystem::path settings_file);
    void set_window(void *hwnd);
    CaptureSettings &settings() { return settings_; }
    VideoRecorder &recorder() { return recorder_; }
    bool recording() const { return recorder_.active(); }
    // Engines hide their debug panels while this is true.
    bool hide_overlays() const { return recording() && settings_.hide_overlays; }
    bool window_open() const { return window_open_ && !hide_overlays(); }

    // Once per frame, between ImGui::NewFrame and ImGui::Render: hotkeys and the settings window.
    // has_audio says whether this engine can feed the recorder sound.
    void ui(bool has_audio);
    // Compact controls for a game's own pause menu (record, screenshot, 720p/1080p, 30/60 fps,
    // quality, audio). Call inside an open ImGui window, after ui() this frame.
    void pause_panel(bool heading = true);
    void toggle_recording();
    // ImGuiKey for start/stop (F9 by default; a game that binds F9 itself picks another).
    void set_record_key(int imgui_key) { record_key_ = imgui_key; }
    // After all drawing, before Present. fixed_seconds is the simulation time of this frame for
    // scripted runs, which then play back at real speed; live play uses the wall clock.
    void end_frame(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *back_buffer,
                   std::optional<double> fixed_seconds = {});

    // Scripted control: record to file (or a timestamped file in the folder) from the next frame.
    void request_recording(std::optional<std::filesystem::path> file = {});
    void request_screenshot(std::optional<std::filesystem::path> file = {});
    // Stops and finishes the MP4 (also called by the destructor's owner on exit).
    VideoRecorder::Stats stop(ID3D11DeviceContext *context);
    const std::string &last_message() const { return message_; }

private:
    void save();
    void update_title();

    std::string game_, prefix_;
    std::filesystem::path settings_file_;
    CaptureSettings settings_;
    VideoRecorder recorder_;
    D3D11FrameGrabber grabber_;
    void *hwnd_ = nullptr;
    std::wstring base_title_;
    bool window_open_ = false, has_audio_ = false;
    bool want_start_ = false, want_stop_ = false;
    int record_key_ = 0; // set in the constructor (ImGuiKey_F9)
    std::optional<std::filesystem::path> start_file_, shot_file_;
    bool want_shot_ = false;
    std::chrono::steady_clock::time_point started_{};
    std::optional<double> first_fixed_;
    std::string message_;
    char folder_text_[1024] = {};
    int last_title_second_ = -1;
};

} // namespace media
