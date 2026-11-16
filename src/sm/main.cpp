#include "frontend.h"
#include "gameplay.h"
#include "ps2_ratchet.h"
#include "ps2_weapons.h"
#include "ps2_packs.h"
#include "play_style.h"
#include "camera.h"
#include "hig/higfrontend.h"
#include "hig/highud.h"
#include "hig/higlocalization.h"
#include "hig/higmenu.h"
#include "level_loop.h"
#include "pause_menu.h"
#include "movie.h"
#include "photo.h"
#include "pilot.h"
#include <fstream>
#include <map>
#include "extras.h"
#include "media/capture_controller.h"
#include <windows.h>
#include "platform_win32.h"
#include "render_dx11.h"
#include <DirectXMath.h>
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <d3d11.h>
#include <imgui.h>
#include <future>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <mmsystem.h>
#include <sstream>
#include <stdexcept>
#include <thread>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
using namespace DirectX;
namespace {
LRESULT CALLBACK window_proc(HWND w, UINT message, WPARAM wp, LPARAM lp) {
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(w, message, wp, lp))
        return 1;
    if (message == WM_CLOSE) {
        DestroyWindow(w);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, message, wp, lp);
}
struct Camera {
    XMFLOAT3 eye{};
    float yaw = 0, pitch = -.5f, speed = 20, fov = 45, roll = 0;
    // Set, the camera looks along `look` with `up` for its up, whatever its yaw and pitch (the Magneboots).
    bool framed = false;
    XMFLOAT3 look{0, 0, 1}, up{0, 1, 0};
    void frame(const sm::Scene &s) {
        float extent = 0;
        XMFLOAT3 center;
        float *c = &center.x;
        for (int k = 0; k < 3; k++) {
            c[k] = (s.minimum[k] + s.maximum[k]) * .5f;
            extent = std::max(extent, s.maximum[k] - s.minimum[k]);
        }
        extent = std::max(extent, 10.f);
        eye = {center.x + extent * .4f, center.y + extent * .6f, center.z - extent * .6f};
        speed = std::max(10.f, extent * .2f);
        yaw = std::atan2(center.x - eye.x, center.z - eye.z);
        pitch = std::atan2(center.y - eye.y, std::hypot(center.x - eye.x, center.z - eye.z));
    }
    XMVECTOR direction() const {
        return XMVectorSet(std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                           std::cos(yaw) * std::cos(pitch), 0);
    }
    std::array<float, 16> matrix(unsigned w, unsigned h) const {
        XMFLOAT4X4 m;
        // SCUS_976.15 LookAt (0x1ff90) uses eye minus target, and
        // Perspective (0x20570) produces clip W = -view Z. Using LH here
        // mirrors the retail world, including Ratchet's right-hand weapon.
        if (framed)
            XMStoreFloat4x4(&m, XMMatrixLookToRH(XMLoadFloat3(&eye), XMLoadFloat3(&look), XMLoadFloat3(&up)) *
                                    XMMatrixPerspectiveFovRH(fov * XM_PI / 180, float(w) / h, .1f, 20000.f));
        else
        XMStoreFloat4x4(&m,
                        XMMatrixLookToRH(XMLoadFloat3(&eye), direction(),
                                         XMVector3Rotate(XMVectorSet(0, 1, 0, 0),
                                                         XMQuaternionRotationAxis(direction(), roll * XM_PI / 180))) *
                            XMMatrixPerspectiveFovRH(fov * XM_PI / 180, float(w) / h, .1f, 20000.f));
        std::array<float, 16> out;
        std::copy_n(&m._11, 16, out.begin());
        return out;
    }
    void move(float dt, const hig::PadState &pad, bool mouse, bool keyboard, const ImGuiIO &io) {
        auto key = [&](int vk) { return keyboard && (GetAsyncKeyState(vk) & 0x8000) != 0; };
        if (mouse) {
            yaw -= io.MouseDelta.x * .004f;
            pitch -= io.MouseDelta.y * .004f;
        }
        yaw -= pad.axes[2] * dt * 2;
        pitch -= pad.axes[3] * dt * 2;
        pitch = std::clamp(pitch, -1.5f, 1.5f);
        float forward = float(key('W')) - float(key('S')) - pad.axes[1];
        float right = float(key('D')) - float(key('A')) + pad.axes[0];
        auto delta = direction() * forward +
                     XMVectorSet(-std::cos(yaw), 0, std::sin(yaw), 0) * right +
                     XMVectorSet(0,
                                 float(key('E')) - float(key('Q')) +
                                     float((pad.raw.buttons & hig::R1) != 0) -
                                     float((pad.raw.buttons & hig::L1) != 0),
                                 0, 0);
        if (XMVectorGetX(XMVector3LengthSq(delta)) > 1)
            delta = XMVector3Normalize(delta);
        XMStoreFloat3(&eye,
                      XMLoadFloat3(&eye) + delta * (dt * speed * (key(VK_SHIFT) ? 4.f : 1.f)));
    }
};
// This ImGui revision renders at DisplaySize. Scale its generated geometry
// from client coordinates to the selected render resolution, keeping input
// coordinates and layout in client pixels even when the backbuffer is 4K.
void scale_ui(ImDrawData *data, unsigned w, unsigned h) {
    if (data->DisplaySize.x <= 0 || data->DisplaySize.y <= 0)
        return;
    ImVec2 scale(float(w) / data->DisplaySize.x, float(h) / data->DisplaySize.y);
    data->ScaleClipRects(scale);
    for (auto *list : data->CmdLists)
        for (auto &v : list->VtxBuffer) {
            v.pos.x *= scale.x;
            v.pos.y *= scale.y;
        }
    data->DisplaySize = ImVec2(float(w), float(h));
}
// The swap chain stretches the picture, drawn at the display resolution, over the window's client area. So the
// window takes the resolution's shape: its client area is the resolution itself, or the largest size of that shape
// the monitor's work area holds. A maximised window is left as it is.
void fit_window(HWND window, unsigned width, unsigned height) {
    MONITORINFO monitor{};
    monitor.cbSize = sizeof monitor;
    if (!width || !height || IsZoomed(window) ||
        !GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
        return;
    RECT frame{0, 0, 0, 0};
    AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE);
    const int frame_w = frame.right - frame.left, frame_h = frame.bottom - frame.top;
    const int work_w = monitor.rcWork.right - monitor.rcWork.left, work_h = monitor.rcWork.bottom - monitor.rcWork.top;
    const float k = std::min({1.f, float(work_w - frame_w) / float(width), float(work_h - frame_h) / float(height)});
    const int w = std::max(1, int(std::lround(float(width) * k))) + frame_w,
              h = std::max(1, int(std::lround(float(height) * k))) + frame_h;
    SetWindowPos(window, nullptr, monitor.rcWork.left + (work_w - w) / 2, monitor.rcWork.top + (work_h - h) / 2, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}
#include "frontend_ui.inl"
#include "hud_ui.inl"
#include "level_ui.inl"
#include "pause_ui.inl"
#include "photo_ui.inl"
int run(int argc, wchar_t **argv) {
    if (argc < 2) {
        std::puts(
            "sm_pc <Size Matters USA.iso> [--level /WAD/01.WAD] [--inspect] "
            "[--width 1920 --height 1080 "
            "--fps 60] [--frames N --capture NEW.bmp --debug --hidden "
            "--display-cycle]\n"
            "  --menu-script up,down,accept | --flow-script accept,accept,load,wait:60,start\n"
            "  --gameplay-script idle:30,run:15,fire:5 [--camera-yaw degrees] "
            "[--player-position x,y,z]\n"
            "  --movie /VID/L01/01A.BIK [--movie-frame 120 | --movie-skip-after 1]\n"
            "  Gameplay script also supports interact,help,accept,back,wrench,runwrench,throw,hyper,runfire,dive,swim,ascend\n"
            "  --photo [--photo-model name --photo-clip N --photo-time seconds --photo-hide-ui]\n"
            "  --photo-shot NEW.bmp [--photo-resolution 1080|2160]\n"
            "  --pilot combat:40,tour:20  (engine-aware demo driver)\n"
#ifdef SM_PC_STANDALONE
            "  [--show-options] [--play-style-settings INI]; strafe: Z / L2 (XInput LT)\n"
#else
            "  --ps2-style [--uya-weapon 39|119] [--show-options] [--play-style-settings INI]\n"
            "  --uya-camera | --sm-weapons; strafe: Z / L2 (XInput LT)\n"
#endif
            "  --record NEW.mp4 [--record-start F --record-quality low|medium|high|ultra --record-fps 30|60\n"
            "   --record-height 720|1080|1440 --record-no-audio] [--capture-folder DIR --capture-settings INI]\n"
            "  In game: F9 record, F11 capture settings, F12 screenshot");
        return 2;
    }
    auto prefs = sm::display_settings_path();
    auto display = sm::read_display(prefs);
    auto play_style_path = sm::play_style_settings_path();
    auto play_style = sm::read_play_style(play_style_path);
    bool show_options = false;
    std::wstring iso = argv[1];
    std::string level;
    std::string menu_script, flow_script, gameplay_script;
    std::string movie_path;
    int movie_frame = -1;
    double movie_skip_after = -1;
    bool photo_start = false, photo_hide_ui = false;
    std::vector<std::string> photo_models;
    int photo_clip = -1, photo_resolution = 0;
    float photo_time = 0;
    std::filesystem::path photo_shot;
    bool menu_has_save = false;
    std::filesystem::path capture, audio_capture, record_path, capture_folder, capture_settings_path, export_font;
    unsigned record_start = 1;
    std::optional<unsigned> record_fps, record_height;
    std::string record_quality, pilot_plan;
    bool record_no_audio = false;
    bool mute = false;
    bool inspect = false, hidden = false, debug = false, cycle = false;
    bool all_weapons = false; // --all-weapons: the hero owns every weapon and gadget from the start
    bool all_armor = false;   // --all-armor: every armour piece
    int armor_set_option = 0; // --armor-set N: wears set N (1..7 whole, 8..13 mixed) from the start
    int hud_screen = -1;      // --hud-screen N: draw one retail HUD screen alone
    unsigned frame_limit = 0;
    float camera_yaw_offset = 0;
    std::vector<std::array<float, 2>> waypoints;
    std::vector<std::array<float, 3>> waypoints3; // testing: the places mgo:N walks to on the Magneboots
    bool relocate_player = false;
    hig::Vec3 player_position{};
    // Optional PS2 Ratchet override (ps2_ratchet.h); --ratchet* options apply for this run only.
    sm::Ps2RatchetSettings ratchet_settings = sm::read_ps2_ratchet_settings(sm::ps2_ratchet_settings_path());
    for (int i = 2; i < argc; i++) {
        std::wstring arg = argv[i];
        auto value = [&]() {
            if (++i >= argc)
                throw std::runtime_error("Missing option value");
            return std::wstring(argv[i]);
        };
        if (arg == L"--photo")
            photo_start = true;
        else if (arg == L"--photo-hide-ui")
            photo_hide_ui = true;
        else if (arg == L"--photo-model") {
            auto v = value();
            photo_models.emplace_back(v.begin(), v.end());
            photo_start = true;
        } else if (arg == L"--photo-clip")
            photo_clip = std::stoi(value());
        else if (arg == L"--photo-time")
            photo_time = std::stof(value());
        else if (arg == L"--photo-resolution")
            photo_resolution = std::stoi(value());
        else if (arg == L"--photo-shot") {
            photo_shot = value();
            photo_start = true;
        } else if (arg == L"--movie") {
            auto v = value();
            movie_path.assign(v.begin(), v.end());
        } else if (arg == L"--movie-frame")
            movie_frame = std::stoi(value());
        else if (arg == L"--movie-skip-after")
            movie_skip_after = std::stod(value());
        else if (arg == L"--level") {
            auto v = value();
            level.assign(v.begin(), v.end());
        } else if (arg == L"--menu-script") {
            auto v = value();
            menu_script.assign(v.begin(), v.end());
        } else if (arg == L"--flow-script") {
            auto v = value();
            flow_script.assign(v.begin(), v.end());
        } else if (arg == L"--gameplay-script") {
            auto v = value();
            gameplay_script.assign(v.begin(), v.end());
        } else if (arg == L"--hud-screen") {
            hud_screen = std::stoi(value());
        } else if (arg == L"--all-weapons") {
            all_weapons = true;
        } else if (arg == L"--player-position") {
            auto v = value();
            std::wistringstream coordinates(v);
            wchar_t comma1, comma2;
            if (!(coordinates >> player_position[0] >> comma1 >> player_position[1] >> comma2 >>
                  player_position[2]) ||
                comma1 != L',' || comma2 != L',' || !(coordinates >> std::ws).eof() ||
                !std::all_of(player_position.begin(), player_position.end(),
                             [](float n) { return std::isfinite(n); }))
                throw std::runtime_error("Player position needs finite x,y,z coordinates");
            relocate_player = true;
        } else if (arg == L"--menu-has-save")
            menu_has_save = true;
        else if (arg == L"--width")
            display.width = std::stoul(value());
        else if (arg == L"--height")
            display.height = std::stoul(value());
        else if (arg == L"--fps")
            display.fps = std::stoul(value());
        else if (arg == L"--frames")
            frame_limit = std::stoul(value());
        else if (arg == L"--capture")
            capture = value();
        else if (arg == L"--audio-capture")
            audio_capture = value();
        else if (arg == L"--mute")
            mute = true;
        else if (arg == L"--record")
            record_path = value();
        else if (arg == L"--record-start")
            record_start = std::stoul(value());
        else if (arg == L"--record-quality")
            record_quality = std::filesystem::path(value()).string();
        else if (arg == L"--record-fps")
            record_fps = std::stoul(value());
        else if (arg == L"--record-height")
            record_height = std::stoul(value());
        else if (arg == L"--record-no-audio")
            record_no_audio = true;
        else if (arg == L"--capture-folder")
            capture_folder = value();
        else if (arg == L"--capture-settings")
            capture_settings_path = value();
        else if (arg == L"--pilot")
            pilot_plan = std::filesystem::path(value()).string();
        else if (arg == L"--waypoints") { // testing: "x,z;x,z;..." places the gameplay script's go:N walks to
            std::istringstream list(std::filesystem::path(value()).string());
            std::string item;
            while (std::getline(list, item, ';')) {
                float x = 0, z = 0;
                char comma = 0;
                std::istringstream pair(item);
                if (!(pair >> x >> comma >> z) || comma != ',')
                    throw std::runtime_error("Waypoints are x,z;x,z;...");
                waypoints.push_back({x, z});
            }
        }
        else if (arg == L"--waypoints3") { // testing: "x,y,z;x,y,z;..." places the gameplay script's mgo:N walks to
            std::istringstream list(std::filesystem::path(value()).string());
            std::string item;
            while (std::getline(list, item, ';')) {
                float x = 0, y = 0, z = 0;
                char a = 0, b = 0;
                std::istringstream triple(item);
                if (!(triple >> x >> a >> y >> b >> z) || a != ',' || b != ',')
                    throw std::runtime_error("Waypoints3 are x,y,z;x,y,z;...");
                waypoints3.push_back({x, y, z});
            }
        }
        else if (arg == L"--export-font")
            export_font = value();
        else if (arg == L"--camera-yaw") {
            camera_yaw_offset = std::stof(value()) * XM_PI / 180.f;
            if (!std::isfinite(camera_yaw_offset))
                throw std::runtime_error("Invalid camera yaw");
        } else if (arg == L"--settings") {
            prefs = value();
            display = sm::read_display(prefs);
        } else if (arg == L"--play-style-settings") {
            play_style_path = value();
            play_style = sm::read_play_style(play_style_path);
        } else if (arg == L"--ps2-style") {
            play_style.uya_camera = play_style.strafe = true;
            play_style.rendering = sm::uya_render_options();
        } else if (arg == L"--uya-camera") {
            play_style.uya_camera = true;
        } else if (arg == L"--uya-weapon") {
            play_style.uya_weapon = std::stoi(value());
            if (play_style.uya_weapon != 39 && play_style.uya_weapon != 119)
                throw std::runtime_error("Supported UYA weapons are 39 (Shock Blaster) and 119 (Nitro Launcher)");
            play_style.uya_weapons = true;
        } else if (arg == L"--sm-weapons") {
            play_style.uya_weapons = false;
        } else if (arg == L"--show-options") {
            show_options = true;
        } else if (arg == L"--ratchet") {
            ratchet_settings.model = std::filesystem::path(value()).string();
        } else if (arg == L"--hero-skin") { // one of Size Matters' own skins (0..6), for this run only
            ratchet_settings.model = "sm";
            ratchet_settings.skin = std::stoi(value());
            if (ratchet_settings.skin < 0 || ratchet_settings.skin >= int(sm::hero_skin_count))
                throw std::runtime_error("--hero-skin is 0..6");
        } else if (arg == L"--all-armor") {
            all_armor = true;
        } else if (arg == L"--armor-set") {
            armor_set_option = std::stoi(value());
            if (armor_set_option < 1 || armor_set_option > 13)
                throw std::runtime_error("--armor-set is 1..13");
            all_armor = true;
        } else if (arg == L"--uya-wrench") { // 118 silver, 122 blue, 126 dark / orange; for this run
            play_style.uya_wrench = std::stoi(value());
            if (play_style.uya_wrench != 118 && play_style.uya_wrench != 122 && play_style.uya_wrench != 126)
                throw std::runtime_error("--uya-wrench is 118, 122 or 126");
        } else if (arg == L"--ratchet-controller") { // the imported Ratchet's own game's controller, for this run
            ratchet_settings.own_controller = true;
        } else if (arg == L"--ratchet-armor") {
            ratchet_settings.armor = std::stoi(value());
        } else if (arg == L"--ratchet-anims") {
            ratchet_settings.animations = std::filesystem::path(value()).string();
        } else if (arg == L"--inspect")
            inspect = true;
        else if (arg == L"--hidden")
            hidden = true;
        else if (arg == L"--debug")
            debug = true;
        else if (arg == L"--display-cycle")
            cycle = true;
        else
            throw std::runtime_error("Unknown native engine option");
    }
    if (!sm::valid_display(display))
        throw std::runtime_error("Invalid display dimensions or frame cap");
    if (cycle && frame_limit < 100)
        throw std::runtime_error("Display audit needs --frames at least 100");
    if (!capture.empty() && !frame_limit)
        throw std::runtime_error("A capture needs a bounded --frames run");
    sm::Runtime runtime;
    sm::Scene scene, actors;
    sm::Gameplay game;
    sm::PlayerAssets player_assets;
    sm::UyaFollowCamera uya_camera;
    if (!sm::extras_built) // a build without the borrowed play options (extras.h): none of them can be on
        play_style.uya_camera = play_style.uya_weapons = play_style.thruster_pack = false;
    std::string weapon_message;
    std::string pack_message;
    auto apply_packs = [&]() {
        if (player_assets.ratchet.bones.empty()) return false;
        if (!player_assets.ps2_packs) player_assets.ps2_packs = std::make_shared<sm::Ps2Packs>();
        if ((play_style.thruster_pack || play_style.hydro_pack) && !player_assets.ps2_packs->loaded() &&
            !player_assets.ps2_packs->load(ratchet_settings.iso_uya, player_assets.ratchet,
                                          player_assets.textures, pack_message)) return false;
        game.air_pack = play_style.thruster_pack && player_assets.ps2_packs->loaded()
                            ? ps2game::HeroPack::Thrusterpack : ps2game::HeroPack::Helipack;
        game.hydro_pack_enabled = play_style.hydro_pack && player_assets.ps2_packs->loaded();
        pack_message.clear();
        return true;
    };
    auto apply_weapons = [&]() {
        if (!play_style.uya_weapons) {
            if (player_assets.ps2_weapons) player_assets.ps2_weapons->set_enabled(false);
            return true;
        }
        if (player_assets.ratchet.bones.empty()) {
            weapon_message = "Load a playable level before enabling UYA weapons.";
            return false;
        }
        if (!player_assets.ps2_weapons) player_assets.ps2_weapons = std::make_shared<sm::Ps2Weapons>();
        auto& weapons = *player_assets.ps2_weapons;
        if ((!weapons.loaded() && !weapons.load(ratchet_settings.iso_uya, player_assets.ratchet,
                                               player_assets.textures, weapon_message)) ||
            !weapons.equip(play_style.uya_weapon, weapon_message)) {
            weapons.set_enabled(false);
            return false;
        }
        weapons.set_enabled(true);
        weapon_message.clear();
        return true;
    };
    // An OmniWrench of UYA's in SM's wrench moves (play_style.uya_wrench). The UYA assets are read for it even when
    // the UYA guns are off.
    auto apply_wrench = [&]() {
        if (!play_style.uya_wrench) {
            if (player_assets.ps2_weapons)
                player_assets.ps2_weapons->set_wrench(0, player_assets.wrench, player_assets.textures, weapon_message);
            return true;
        }
        if (player_assets.ratchet.bones.empty()) {
            weapon_message = "Load a playable level before choosing a UYA wrench.";
            return false;
        }
        if (!player_assets.ps2_weapons) player_assets.ps2_weapons = std::make_shared<sm::Ps2Weapons>();
        auto& weapons = *player_assets.ps2_weapons;
        if (!weapons.loaded() && !weapons.load(ratchet_settings.iso_uya, player_assets.ratchet,
                                              player_assets.textures, weapon_message))
            return false;
        return weapons.set_wrench(play_style.uya_wrench, player_assets.wrench, player_assets.textures, weapon_message);
    };
    bool ratchet_tried = false; // the startup override is attempted once (it reads a PS2 disc)
    auto apply_ratchet = [&]() {
        ratchet_tried = true;
        player_assets.ps2.reset();
        if (!ratchet_settings.active() || player_assets.ratchet.bones.empty())
            return;
        auto ps2 = std::make_shared<sm::Ps2Ratchet>();
        std::string why;
        // load() reuses the texture slots of a previous PS2 Ratchet, so encounter texture indices never move.
        if (ps2->load(ratchet_settings, player_assets.ratchet, player_assets.textures, why)) {
            player_assets.ps2 = std::move(ps2);
            std::printf("%s\n", player_assets.ps2->description().c_str());
        } else
            std::fprintf(stderr, "PS2 Ratchet unavailable: %s\n", why.c_str());
    };
    // One 30 Hz gameplay tick; while the PS2 hoverboots or a charge-boot dash own movement SM's controller gets a
    // neutral pad (SM still runs enemies and effects).
    auto tick_player = [&](const hig::PadState &pad, float yaw) {
        if (play_style.uya_camera && uya_camera.initialized()) yaw = uya_camera.pose().yaw;
        game.strafe_enabled = play_style.strafe;
        game.air_pack = play_style.thruster_pack && player_assets.ps2_packs && player_assets.ps2_packs->loaded()
                            ? ps2game::HeroPack::Thrusterpack : ps2game::HeroPack::Helipack;
        game.hydro_pack_enabled = play_style.hydro_pack && player_assets.ps2_packs && player_assets.ps2_packs->loaded();
        const bool owned = player_assets.ps2 && player_assets.ps2->owns_movement();
        // an imported Ratchet's own controller moves him on foot; SM then gets the pad without its movement
        const bool controlled = player_assets.ps2 && player_assets.ps2->controller_begin(game);
        game.tick(owned ? hig::PadState{} : controlled ? sm::Ps2Ratchet::controller_pad(pad) : pad, yaw, 1.f / 30, true);
        if (player_assets.ps2)
            player_assets.ps2->tick(game, player_assets, pad, yaw, (pad.pressed & hig::L3) != 0);
        return owned;
    };
    RetailHud hud;
    hud.debug_screen = hud_screen;
    std::string error;
    sm::Audio audio;
    sm::Movie movie;
    sm::LevelLoop level_loop;
    unsigned pending_level = 0;
    std::string pending_wad;
    // The pause menu (pause_menu.h): the retail pages with the PC's settings; its options persist beside the
    // display settings.
    sm::PauseMenu pause;
    const auto pause_prefs = prefs.parent_path() / "sm_pause_options.ini";
    pause.options = sm::read_pause_options(pause_prefs);
    sm::PauseOptions saved_pause_options = pause.options;
    bool pause_open = false;
    unsigned fitted_width = 0, fitted_height = 0; // the resolution the window was last shaped to
    sm::PauseAction scripted_pause = sm::PauseAction::None; // a flow script's CHEATS / SKINS choice, applied once the renderer is up
    // The pause menu's CHEATS and SKINS pages show the play options and the hero's skin as they are.
    auto pause_state = [&]() {
        pause.goodies.uya_camera = play_style.uya_camera;
        pause.goodies.thruster_pack = play_style.thruster_pack;
        pause.goodies.hydro_pack = play_style.hydro_pack;
        pause.goodies.uya_weapons = player_assets.ps2_weapons && player_assets.ps2_weapons->enabled();
        pause.goodies.uya_look = play_style.rendering == sm::uya_render_options();
        pause.goodies.charge_boots = ratchet_settings.charge_boots;
        pause.goodies.all_weapons = all_weapons;
        pause.goodies.all_armor = all_armor;
        pause.goodies.wrench = play_style.uya_wrench == 118 ? 1 : play_style.uya_wrench == 122 ? 2 : play_style.uya_wrench == 126 ? 3 : 0;
        pause.wearing = {};
        if (player_assets.ps2 && player_assets.ps2->loaded()) {
            pause.wearing.skin = ratchet_settings.model == "gc"     ? unsigned(sm::PauseSkin::GoingCommando)
                                 : ratchet_settings.model == "rac1" ? unsigned(sm::PauseSkin::RatchetAndClank)
                                                                    : unsigned(sm::PauseSkin::UpYourArsenal);
            pause.wearing.armor = ratchet_settings.model == "rac1" ? 0 : ratchet_settings.armor;
        } else
            pause.wearing.skin = player_assets.hero_skin;
        pause.wearing.own_gameplay = ratchet_settings.own_controller;
    };
    int applied_music = -1, applied_effects = -1; // the volumes last given to the mixer
    bool movie_active = false, movie_ready = false;
    double movie_seconds = 0;
    void *movie_picture = nullptr;
    hig::GimImage movie_image;
    auto begin_movie = [&](const std::string &path) {
        audio.movie(nullptr);
        movie.stop();
        movie_picture = nullptr;
        movie_ready = false;
        movie_seconds = 0;
        movie_active = movie.open(runtime.disc(), path, error);
        if (movie_active)
            std::printf("Movie: %s %ux%u, %.3f seconds\n", path.c_str(), movie.info().width,
                        movie.info().height, movie.info().duration());
        else
            std::fprintf(stderr, "Movie unavailable: %s\n", error.c_str());
        return movie_active;
    };
    std::vector<int16_t> recorded_audio;
    hig::LevelData render_level;
    bool player_ready = false;
    bool camera_submerged = false;
    PhotoUi photo;
    if (photo_start && level.empty())
        level = "/WAD/01.WAD";
    if (!runtime.mount(iso, error))
        throw std::runtime_error(error);
    if (!hig::load_hud(runtime.disc(), hud.assets, &error))
        throw std::runtime_error(error);
    if (!export_font.empty()) { // retail HUD font sheet (16x16 cells) and its width/offset metrics
        std::filesystem::create_directories(export_font);
        if (!media::save_image(export_font / "font.png", hud.assets.font.rgba.data(), hud.assets.font.width,
                               hud.assets.font.height, size_t(hud.assets.font.width) * 4, "png", 100, error))
            throw std::runtime_error(error);
        std::ofstream json(export_font / "metrics.json");
        json << "{\"game\":\"sm\",\"cell\":16,\"height\":15,\"metrics\":[";
        for (unsigned c = 0; c < 256; c++)
            json << (c ? "," : "") << "[" << unsigned(hud.assets.metrics[c][2]) << "," << int(int8_t(hud.assets.metrics[c][3])) << "]";
        json << "]}\n";
        std::printf("Exported the retail HUD font to %s\n", export_font.string().c_str());
        return 0;
    }
    std::printf("Retail HUD: %zu screens, %zu elements, %zu icons; weapon icon %u, ammo %u\n",
                hud.assets.layout.screens.size(), hud.assets.layout.elements.size(),
                hud.assets.icons.size(), hud.assets.weapon_icon, hud.assets.max_ammo);
    if (!audio_capture.empty() &&
        (gameplay_script.empty() || std::filesystem::exists(audio_capture)))
        throw std::runtime_error("Audio capture requires a gameplay script and a new WAV path");
    auto frontend_bytes = runtime.disc().read("/LVL/FRONTEND.REL");
    std::vector<hig::FrontendImage> frontend;
    if (!hig::decode_frontend_images(frontend_bytes, frontend, &error))
        throw std::runtime_error(error);
    hig::Localization localization;
    hig::MenuData retail_menu;
    auto loc = runtime.disc().read("/HUD/LOCALDAT.BIN");
    if (!hig::decode_localization(loc, localization, &error) ||
        !hig::decode_frontend_menu(frontend_bytes, retail_menu, &error))
        throw std::runtime_error(error);
    for (auto &item : retail_menu.items) {
        if (!localization.find(item.text_id))
            throw std::runtime_error("Frontend menu label is absent from localization");
        if (item.action == 99)
            item.enabled = menu_has_save;
    }
    hig::navigate_menu(retail_menu, 0);
    auto menu_label = [&](const hig::MenuItem &item) {
        return hig::inspect_localized_text(*localization.find(item.text_id));
    };
    if (!menu_script.empty()) {
        std::printf("Native retail title menu: %zu items; save availability is a "
                    "fixture, no progress file is written\n",
                    retail_menu.items.size());
        std::istringstream script(menu_script);
        std::string word;
        auto report = [&](const char *event, uint32_t action) {
            std::printf("%s: selection=%u label=%s action=%u\n", event, retail_menu.selection,
                        menu_label(retail_menu.items[retail_menu.selection]).c_str(), action);
        };
        report("initial", 0);
        while (std::getline(script, word, ',')) {
            uint32_t pulse = word == "up"       ? hig::Up
                             : word == "down"   ? hig::Down
                             : word == "accept" ? hig::Cross
                                                : 0;
            if (!pulse && word != "idle")
                throw std::runtime_error("Menu script supports up,down,accept,idle");
            report(word.c_str(), hig::advance_menu(retail_menu, pulse));
        }
        return 0;
    }
    hig::NewGameBootstrap bootstrap;
    if (!hig::decode_new_game_bootstrap(frontend_bytes, bootstrap, &error))
        throw std::runtime_error(error);
    char first_wad[32];
    std::snprintf(first_wad, sizeof(first_wad), "/WAD/%02u.WAD", bootstrap.level);
    sm::Frontend flow(retail_menu);
    std::printf("Frontend data: %zu embedded images; original code is never executed\n",
                frontend.size());
    for (const auto &image : frontend)
        std::printf("  %08x %ux%u %s\n", image.file_offset, image.image.width, image.image.height,
                    image.source_path.c_str());
    auto load = [&](const std::string &wad) {
        camera_submerged = false;
        if (!runtime.load(wad, error) || !sm::build_scene(runtime.world().level, scene, error))
            return false;
        photo.reset();
        level_loop = sm::LevelLoop{};
        if (player_assets.animations.clips.empty() &&
            !sm::load_player_assets(runtime.disc(), player_assets, error))
            return false;
        // the skin chosen on the pause menu's SKINS page: one of Size Matters' own, or an imported PS2 Ratchet
        if (!ratchet_settings.active() && unsigned(ratchet_settings.skin) != player_assets.hero_skin &&
            !sm::load_hero_skin(runtime.disc(), unsigned(ratchet_settings.skin), player_assets, error)) {
            std::fprintf(stderr, "Hero skin %d unavailable: %s\n", ratchet_settings.skin, error.c_str());
            ratchet_settings.skin = 0;
        }
        if (!ratchet_tried && ratchet_settings.active())
            apply_ratchet();
        if (player_assets.ps2_weapons && player_assets.ps2_weapons->enabled())
            play_style.uya_weapon = player_assets.ps2_weapons->equipped();
        if (play_style.uya_weapons && !apply_weapons())
            std::fprintf(stderr, "UYA weapons unavailable: %s\n", weapon_message.c_str());
        if (play_style.uya_wrench && !apply_wrench()) {
            std::fprintf(stderr, "UYA wrench unavailable: %s\n", weapon_message.c_str());
            play_style.uya_wrench = 0;
        }
        if (!apply_packs())
            std::fprintf(stderr, "UYA packs unavailable: %s\n", pack_message.c_str());
        if (!sm::load_encounter_assets(runtime.disc(), wad, player_assets.encounters,
                                       player_assets.textures, error))
            return false;
        if(!sm::load_mechanism_assets(runtime.disc(),runtime.world(),player_assets.encounters,player_assets.textures,error))return false;
        if (!sm::load_object_assets(runtime.disc(), runtime.world(), player_assets.encounters.objects,
                                    player_assets.textures, error))
            return false;
        sm::add_generic_enemies(runtime.world(), player_assets.encounters);
        player_ready = game.start(runtime.world(), error, true);
        if (!player_ready && wad == "/WAD/01.WAD")
            return false;
        game.ammo = game.max_ammo = hud.assets.max_ammo;
        {
            // the pause mode's menu tables are in every level module
            std::vector<hig::HudMenu> menus;
            if (runtime.world().disc_path.size() >= 7)
                hig::decode_hud_menus(runtime.disc().read("/LVL/LEVEL_" + runtime.world().disc_path.substr(5, 2) + ".REL"),
                                      hud.assets.layout, menus);
            pause.set_retail(&hud.assets.layout, std::move(menus));
        }
        if (player_assets.encounters.objects.has_weapons)
            game.configure_weapons(&player_assets.encounters.objects.weapons, player_assets.encounters.objects.level,
                                   all_weapons);
        game.configure_armor(player_assets.encounters.objects.has_armor ? &player_assets.encounters.objects.armor : nullptr,
                             all_armor);
        if (armor_set_option && game.armor_tables()) {
            // the set's recipe: the helmet's, the body armor's, the gloves' and the boots' sets
            const auto &recipe = game.armor_tables()->sets[size_t(armor_set_option)].recipe;
            game.wear_armor(recipe[0], 1);
            game.wear_armor(recipe[1], 0);
            game.wear_armor(recipe[2], 2);
            game.wear_armor(recipe[3], 4);
            armor_set_option = 0; // once: later levels keep what he wears
        }
        if (player_ready) {
            game.configure_player(player_assets);
            game.strafe_enabled = play_style.strafe;
            if (player_assets.ps2_weapons) player_assets.ps2_weapons->reset_transients();
            if (player_assets.ps2_packs) player_assets.ps2_packs->reset_transients();
            game.start_enemies(runtime.world(), player_assets.encounters);
            if (!level_loop.start(runtime.disc(), runtime.world(), error, &player_assets.encounters))
                return false;
            level_loop.start_objects(game, player_assets.encounters.objects);
            if (player_assets.encounters.objects.has_weapons) { // the vendor names what it sells in the game's own words
                const auto &table = player_assets.encounters.objects.weapons.weapons;
                for (size_t i = 0; i < table.size() && i < level_loop.item_names.size(); i++)
                    level_loop.item_names[i] = sm::pause_text(localization, table[i].tier(0).name_text, table[i].name.c_str());
            }
            {
                size_t bodies = 0, crates = 0;
                for (const auto &o : level_loop.objects.objects) {
                    bodies += o.visible;
                    crates += o.kind == sm::ObjectKind::Crate || o.kind == sm::ObjectKind::CrateAmmo ||
                              o.kind == sm::ObjectKind::CrateHealth || o.kind == sm::ObjectKind::CrateExplosive;
                }
                std::printf("Objects: %zu instances, %zu with a body, %zu crates; %zu models, %zu class records, "
                            "%zu not decoded\n",
                            level_loop.objects.objects.size(), bodies, crates,
                            player_assets.encounters.objects.models.size(),
                            player_assets.encounters.objects.classes.size(),
                            player_assets.encounters.objects.warnings.size());
            }
            if (relocate_player) {
                game.relocate(player_position);
                relocate_player = false;
            }
            if (const char *line = getenv("SM_FACES")) { // research: "x,y,z,r": the level collision's faces about a point
                float v[4]{};
                if (std::sscanf(line, "%f,%f,%f,%f", v, v + 1, v + 2, v + 3) == 4) {
                    const auto &collision = runtime.world().terrain_collision;
                    std::map<uint64_t, unsigned> kinds;
                    for (const auto &face : collision.faces) {
                        float c[3]{};
                        for (int k = 0; k < 3; k++)
                            for (int a = 0; a < 3; a++)
                                c[a] += collision.vertices[face.vertices[k]][a] / 3;
                        if (std::abs(c[0] - v[0]) > v[3] || std::abs(c[1] - v[1]) > v[3] || std::abs(c[2] - v[2]) > v[3])
                            continue;
                        const int facing = std::abs(face.planes[1]) > .7f ? 0 : std::abs(face.planes[2]) > .7f ? 2 : 1;
                        if (v[3] <= 3 && facing != 0) {
                            std::printf("  face %zu normal %.2f %.2f %.2f:", size_t(&face - collision.faces.data()), face.planes[0], face.planes[1],
                                        face.planes[2]);
                            for (int k = 0; k < 3; k++)
                                std::printf(" (%.2f %.2f %.2f)", collision.vertices[face.vertices[k]][0], collision.vertices[face.vertices[k]][1],
                                            collision.vertices[face.vertices[k]][2]);
                            std::printf("\n");
                        }
                        kinds[(uint64_t(face.flags) << 34) | (uint64_t(face.surface) << 2) | uint64_t(facing)]++;
                    }
                    for (const auto &[key, count] : kinds)
                        std::printf("Faces: flags %04x surface %08x %s x%u\n", unsigned(key >> 34), unsigned(key >> 2),
                                    (key & 3) == 0 ? "floor" : (key & 3) == 2 ? "z-wall" : "x-wall", count);
                }
            }
            if (const char *line = getenv("SM_CURVE")) { // research: "every,hex,hex...": the points of BOG curves
                unsigned every = 1;
                int used = 0;
                if (std::sscanf(line, "%u%n", &every, &used) == 1 && every) {
                    for (const char *at = line + used; *at == ',';) {
                        char *end = nullptr;
                        const uint32_t source = std::strtoul(at + 1, &end, 16);
                        at = end;
                        const hig::BogCurve *c = nullptr;
                        for (const auto &curve : runtime.world().level.curves)
                            if (curve.source_offset == source)
                                c = &curve;
                        std::printf("Curve %x: %zu points\n", source, c ? c->points.size() : size_t(0));
                        for (size_t k = 0; c && k < c->points.size(); k += every)
                            std::printf("  %zu: %.2f,%.2f,%.2f\n", k, c->points[k][0], c->points[k][1], c->points[k][2]);
                    }
                }
            }
            if (const char *line = getenv("SM_FACE_DUMP")) { // research: "x,y,z,r,surface(hex)": every such face as three corners
                float v[4]{};
                unsigned wanted = 0;
                if (std::sscanf(line, "%f,%f,%f,%f,%x", v, v + 1, v + 2, v + 3, &wanted) == 5) {
                    const auto &collision = runtime.world().terrain_collision;
                    for (const auto &face : collision.faces) {
                        if (face.surface != wanted)
                            continue;
                        const auto &a = collision.vertices[face.vertices[0]], &b = collision.vertices[face.vertices[1]],
                                   &c = collision.vertices[face.vertices[2]];
                        const float cx = (a[0] + b[0] + c[0]) / 3 - v[0], cy = (a[1] + b[1] + c[1]) / 3 - v[1], cz = (a[2] + b[2] + c[2]) / 3 - v[2];
                        if (cx * cx + cy * cy + cz * cz > v[3] * v[3])
                            continue;
                        std::printf("Tri %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n", a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]);
                    }
                }
            }
            if (getenv("SM_SURFACES")) { // research: the level's surface types (hash, flags, footstep sound)
                std::printf("Surface types: %zu\n", runtime.world().surface_types.size());
                for (const auto &s : runtime.world().surface_types)
                    std::printf("  surface %08x flags %04x sound %08x\n", s.hash, unsigned(s.flags), s.sound);
            }
            if (const char *line = getenv("SM_RAY")) { // research: "x0,y0,z0,x1,y1,z1": what a line through the level meets first
                float v[6]{};
                if (std::sscanf(line, "%f,%f,%f,%f,%f,%f", v, v + 1, v + 2, v + 3, v + 4, v + 5) == 6) {
                    const auto hit = game.terrain().ray({v[0], v[1], v[2]}, {v[3], v[4], v[5]});
                    std::printf("Ray: hit=%d at %.2f,%.2f,%.2f normal %.2f,%.2f,%.2f\n", int(hit.hit), hit.point[0], hit.point[1], hit.point[2],
                                hit.normal[0], hit.normal[1], hit.normal[2]);
                }
            }
            if (const char *map = getenv("SM_GROUND_MAP")) { // research: "x0,z0,x1,z1,step,ytop,ybottom"
                float v[7]{};
                if (std::sscanf(map, "%f,%f,%f,%f,%f,%f,%f", v, v + 1, v + 2, v + 3, v + 4, v + 5, v + 6) == 7 && v[4] > 0) {
                    std::printf("Ground map x %.1f..%.1f step %.2f, first floor below y %.1f\n      ", v[0], v[2], v[4], v[5]);
                    for (float x = v[0]; x <= v[2] + .001f; x += v[4])
                        std::printf("%6.1f", x);
                    for (float z = v[1]; z <= v[3] + .001f; z += v[4]) {
                        std::printf("\n%6.1f", z);
                        for (float x = v[0]; x <= v[2] + .001f; x += v[4]) {
                            const auto hit = game.terrain().ray({x, v[5], z}, {x, v[6], z});
                            if (hit.hit)
                                std::printf("%6.1f", hit.point[1]);
                            else
                                std::printf("     .");
                        }
                    }
                    std::printf("\n");
                }
            }
            level_loop.tick(game);
            uya_camera.reset(game, game.heading + camera_yaw_offset);
            std::printf("Encounters: %zu original actor types, %zu authored placements\n",
                        player_assets.encounters.models.size(), game.enemies.size());
        }
        if (!audio.load(runtime.disc(), wad, error)) {
            std::fprintf(stderr, "Audio unavailable: %s\n", error.c_str());
            error.clear();
        }
        if (const char *wanted = getenv("SM_SOUND_PROGRAM")) { // research: "hash,hash,...": those sounds' programs, grain by grain
            std::istringstream list(wanted);
            std::string item;
            while (std::getline(list, item, ',')) {
                const uint32_t sound = uint32_t(std::stoul(item, nullptr, 16));
                const auto binding = audio.assets().bindings.find(sound);
                if (binding == audio.assets().bindings.end())
                    continue;
                const auto bank = audio.assets().banks.find(binding->second.bank);
                if (bank == audio.assets().banks.end() || binding->second.index >= bank->second.sounds.size())
                    continue;
                std::printf("Program %08x (%s, loop %d):", sound, binding->second.name.c_str(), int(binding->second.loop));
                for (const auto &grain : bank->second.sounds[binding->second.index].grains)
                    std::printf(" %u/%06x+%d", grain.opcode >> 24, grain.opcode & 0xffffff, grain.delay);
                std::printf("\n");
            }
        }
        render_level.textures = runtime.world().level.textures;
        render_level.textures.insert(render_level.textures.end(), player_assets.textures.begin(),
                                     player_assets.textures.end());
        std::printf("Native DATA: %s, %zu triangles, %zu placed models, %zu COL faces\n",
                    runtime.world().level.name.c_str(), scene.indices.size() / 3, scene.instances,
                    runtime.world().terrain_collision.faces.size());
        for (const auto &p : runtime.world().pending)
            std::printf("Pending: %s\n", p.c_str());
        return true;
    };
    if (!level.empty() && !load(level))
        throw std::runtime_error(error);
    if (!level.empty())
        flow.inspect_level();
    uint64_t level_ticks = 0;
    if (!flow_script.empty()) {
        sm::InputLatch input;
        sm::MenuRepeat repeat;
        std::printf("Native frontend flow; 30 Hz level bootstrap; gameplay actors "
                    "and progress saves pending\n");
        auto report = [&](const char *event) {
            std::printf("%s: screen=%s selection=%u level_ticks=%llu\n", event,
                        sm::screen_name(flow.screen()), flow.selection(),
                        (unsigned long long)level_ticks);
        };
        report("initial");
        std::istringstream script(flow_script);
        std::string word;
        while (std::getline(script, word, ',')) {
            if (word == "load") {
                if (!flow.loading())
                    throw std::runtime_error("load requires a pending New Game request");
                bool ok = load(first_wad);
                flow.loaded(ok, error);
                if (!ok)
                    throw std::runtime_error(error);
                level = first_wad;
                level_ticks = 0;
            } else {
                unsigned ticks = 1;
                hig::PadSample sample;
                if (word.starts_with("wait:"))
                    ticks = std::stoul(word.substr(5));
                else if (word == "up")
                    sample.buttons = hig::Up;
                else if (word == "down")
                    sample.buttons = hig::Down;
                else if (word == "accept")
                    sample.buttons = hig::Cross;
                else if (word == "back")
                    sample.buttons = hig::Circle;
                else if (word == "left")
                    sample.buttons = hig::Left;
                else if (word == "right")
                    sample.buttons = hig::Right;
                else if (word == "start")
                    sample.buttons = hig::Start;
                else if (word != "idle")
                    throw std::runtime_error("Flow script supports "
                                             "up,down,accept,back,start,idle,load,wait:N");
                if (!ticks || ticks > 100000)
                    throw std::runtime_error("Headless wait must be 1..100000 ticks");
                for (unsigned tick = 0; tick < ticks; tick++) {
                    input.sample(sample);
                    auto p = input.consume();
                    const bool was_paused = flow.screen() == sm::Screen::Pause;
                    if (was_paused) {
                        const auto action = pause.pulse(repeat.tick(p), game);
                        if (action == sm::PauseAction::Resume)
                            flow.choose(0);
                        else if (action == sm::PauseAction::QuitToTitle)
                            flow.choose(2);
                        else if (action == sm::PauseAction::ApplyGoodies || action == sm::PauseAction::ApplySkin)
                            scripted_pause = action;
                    } else
                        flow.pulse(repeat.tick(p));
                    if (!was_paused && flow.screen() == sm::Screen::Pause) {
                        pause.open();
                        pause.graphics = {display.width, display.height, display.fps, display.vsync};
                        pause_state();
                        pause_open = true; // the render loop keeps the scripted page
                    }
                    if (flow.level_active())
                        level_ticks++;
                }
                // Script commands are taps, including release between commands.
                input.sample({});
                input.consume();
                repeat.clear();
            }
            report(word.c_str());
        }
        // Default is CPU-only. A bounded frame run can render the final scripted
        // state for unattended UI captures without requiring physical input.
        if (!frame_limit)
            return 0;
    }
    if (!gameplay_script.empty()) {
        if (level.empty()) {
            if (!load(first_wad))
                throw std::runtime_error(error);
            level = first_wad;
            flow.inspect_level();
        }
        if (!player_ready)
            throw std::runtime_error("Gameplay script requires a Ratchet spawn");
        sm::InputLatch script_input;
        std::istringstream script(gameplay_script);
        std::string word;
        while (std::getline(script, word, ',')) {
            auto colon = word.find(':');
            std::string command = word.substr(0, colon);
            unsigned ticks = colon == std::string::npos ? 1 : std::stoul(word.substr(colon + 1));
            if (command == "clank") { // testing: the controlled character is Clank (1) or Ratchet (2)
                game.clank = ticks == 1;
                command = "idle";
                ticks = 1;
            }
            if (command == "bots") { // testing: this many gadgebots stand waiting in a row ahead of the hero
                unsigned placed = 0;
                for (auto &o : level_loop.objects.objects)
                    if ((o.name == "clankbot" || o.name == "ClankBot") && !o.pooled && o.state == 0 && !o.n[3] && placed < ticks) {
                        const float ahead = 2.f + .8f * float(placed++);
                        o.matrix[12] = game.position[0] + std::sin(game.heading) * ahead;
                        o.matrix[14] = game.position[2] + std::cos(game.heading) * ahead;
                        const auto ground = game.terrain().ray({o.matrix[12], game.position[1] + 1, o.matrix[14]},
                                                               {o.matrix[12], game.position[1] - 3, o.matrix[14]});
                        o.matrix[13] = ground.hit ? ground.point[1] : game.position[1];
                        o.visible = true;
                        o.state = 3;
                    }
                command = "idle";
                ticks = 1;
            }
            if (command == "hp") { // testing: this much health, and as much at most (a run that is not about the fights)
                game.health = game.max_health = std::max(1u, ticks);
                command = "idle";
                ticks = 1;
            }
            if (command == "bolts") { // testing: at least this many bolts (a run that breaks no crates)
                game.bolts = std::max(game.bolts, ticks);
                command = "idle";
                ticks = 1;
            }
            if (command == "shaft") { // testing: Kalidon's shaft as it is once its platform has gone (state 7: the console)
                for (auto &o : level_loop.objects.objects)
                    if (o.name == "Lvl3Platform" && !o.pooled) {
                        o.state = uint8_t(ticks);
                        o.visible = o.solid = false;
                        o.matrix[13] = -109.27f;
                    }
                command = "idle";
                ticks = 1;
            }
            if (command == "foe") { // testing: the level's boss has at most this much health left (a run that is not about the fight)
                for (auto &o : level_loop.objects.objects)
                    if (o.name == "Mungo" && !o.pooled)
                        o.f[0] = std::min(o.f[0], float(ticks));
                command = "idle";
                ticks = 1;
            }
            if (command == "equip") { // testing: give and take in hand a weapon or gadget by its table index
                game.grant_weapon(ticks, true);
                command = "idle";
                ticks = 1;
            }
            if (command == "turn") { // testing: face a compass bearing (degrees; 0 is +z, 90 is +x), then one idle tick
                game.heading = float(ticks % 360) * XM_PI / 180.f;
                command = "idle";
                ticks = 1;
            }
            // testing: wait (at most 3000 ticks) until his height is at or below -N/10 ("ydown"), at or above -N/10
            // ("yup"), or at or above +N/10 ("yover"): a lift's or a sinking platform's ride
            int height_wait = 0;
            float height_goal = 0;
            if (command == "ydown" || command == "yup" || command == "yover") {
                height_wait = command == "ydown" ? -1 : 1;
                height_goal = (command == "yover" ? 1.f : -1.f) * float(ticks) / 10;
                command = "idle";
                ticks = 3000;
            }
            // testing: "clear:R" fights with the wrench (towards the nearest enemy, Square tapped) until no enemy is
            // left within R metres, at most 3000 ticks
            float clear_radius = 0;
            if (command == "clear") {
                clear_radius = float(ticks);
                command = "seekwrench";
                ticks = 3000;
            }
            float rail_goal = -1;
            if (command == "rail") { // testing: wait until he is this many metres along his rail (at most 3000 ticks)
                rail_goal = float(ticks);
                command = "idle";
                ticks = 3000;
            }
            const std::array<float, 2> *goal = nullptr;
            // testing: walk to a waypoint (at most 900 ticks); "gojump" with Cross held, "gohop" with a double jump
            // (Cross, let go from the eighth tick, and held again from the twelfth)
            const bool hop = command == "gohop";
            if (hop)
                command = "gojump";
            if (command == "go" || command == "gojump") {
                if (ticks >= waypoints.size())
                    throw std::runtime_error("Gameplay command names a waypoint that was not given");
                goal = &waypoints[ticks];
                ticks = 900;
            }
            const std::array<float, 3> *goal3 = nullptr;
            const bool magne_jump = command == "mgojump";
            if (command == "mgo" || magne_jump) { // testing: on the Magneboots, along the surface to a place
                if (ticks >= waypoints3.size())
                    throw std::runtime_error("Gameplay command names a waypoint3 that was not given");
                goal3 = &waypoints3[ticks];
                ticks = 900;
                command = "idle";
            }
            if (!ticks || ticks > 10000)
                throw std::runtime_error("Gameplay command needs 1..10000 ticks");
            hig::PadSample sample;
            if (command == "gojump")
                sample.buttons |= hig::Cross;
            if (command == "run" || command == "jump" || command == "glide" || command == "runfire" || command == "runwrench" || command == "swim")
                sample.sticks[1] = 0;
            if (command == "jump" || command == "glide" || command == "ascend" || command == "seekjump")
                sample.buttons |= hig::Cross;
            if (command == "crouch" || command == "highjump" || command == "longjump") sample.buttons |= hig::R1;
            if (command == "highjump" || command == "longjump") sample.buttons |= hig::Cross;
            if (command == "longjump") sample.sticks[1] = 0;
            else if (command == "fire" || command == "runfire" || command == "seekfire")
                sample.buttons |= hig::Circle;
            else if (command == "wrench" || command == "hyper" || command == "runwrench" || command == "dive" || command == "seekwrench")
                sample.buttons |= hig::Square;
            else if (command == "seek" || command == "seekjump") // testing: towards the nearest enemy (the stick is set each tick below)
                ;
            else if (command == "dup") // the D-pad (the gadgebots' commands)
                sample.buttons |= hig::Up;
            else if (command == "ddown")
                sample.buttons |= hig::Down;
            else if (command == "dleft")
                sample.buttons |= hig::Left;
            else if (command == "dright")
                sample.buttons |= hig::Right;
            else if (command == "seekthrow")
                sample.buttons |= hig::L2 | hig::Square;
            else if (command == "throw")
                sample.buttons |= hig::L2 | hig::Square;
            else if (command == "weapon")
                sample.buttons |= hig::Triangle;
            else if (command == "hover") // PS2 Ratchet override: equip/remove hoverboots
                sample.buttons |= hig::L3;
            else if (command == "hoverrun" || command == "hoverthrust") {
                sample.sticks[1] = 0;
                if (command == "hoverthrust")
                    sample.buttons |= hig::R1;
            } else if (command == "r1") // PS2 Ratchet override: charge boots are a double tap of R1
                sample.buttons |= hig::R1;
            else if (command == "runr1") {
                sample.sticks[1] = 0;
                sample.buttons |= hig::R1;
            }
            else if (command == "interact")
                sample.buttons |= hig::Triangle;
            else if (command == "help")
                sample.buttons |= hig::Select;
            else if (command == "accept")
                sample.buttons |= hig::Cross;
            else if (command == "back")
                sample.buttons |= hig::Circle;
            else if (command == "pull") // the stick back (a BotFlinger's force down)
                sample.sticks[1] = 255;
            else if (command == "left")
                sample.sticks[0] = 0;
            else if (command == "right")
                sample.sticks[0] = 255;
            else if (command == "hopleft" || command == "hopright") { // the stick to a side with Cross (a rail to a rail)
                sample.sticks[0] = command == "hopleft" ? 0 : 255;
                sample.buttons |= hig::Cross;
            }
            else if (command == "strafeleft" || command == "straferight" || command == "strafeback" || command == "strafefire") {
                sample.buttons = hig::L2;
                if (command == "strafeback") sample.sticks[1] = 255;
                else sample.sticks[0] = command == "strafeleft" ? 0 : 255;
                if (command == "strafefire") sample.buttons |= hig::Circle;
            }
            else if (command != "idle" && command != "run" && command != "swim" && !goal &&
                     command != "jump" && command != "glide" && command != "ascend" &&
                     command != "crouch" && command != "highjump")
                throw std::runtime_error("Unknown gameplay command");
            for (unsigned tick = 0; tick < ticks; tick++) {
                if (rail_goal >= 0 && (!game.grinding() || game.grind().distance >= rail_goal))
                    break;
                if (clear_radius > 0) {
                    bool any = false;
                    for (const auto &e : game.enemies)
                        any |= !e.hidden && e.state != sm::EnemyState::Dead &&
                               std::hypot(e.position[0] - game.position[0], e.position[2] - game.position[2]) < clear_radius &&
                               std::fabs(e.position[1] - game.position[1]) < 4;
                    if (!any)
                        break;
                    sample.buttons = tick % 7 < 4 ? hig::Square : 0;
                }
                if (height_wait && (height_wait < 0 ? game.position[1] <= height_goal : game.position[1] >= height_goal))
                    break;
                if (command.rfind("seek", 0) == 0) {
                    const sm::Enemy *nearest = nullptr;
                    float best = 1e9f;
                    for (const auto &e : game.enemies) {
                        const float d = std::hypot(e.position[0] - game.position[0], e.position[2] - game.position[2]);
                        if (!e.hidden && e.state != sm::EnemyState::Dead && d < best)
                            best = d, nearest = &e;
                    }
                    sample.sticks[0] = sample.sticks[1] = 128;
                    // "seek" stops beside it, "seekfire" six metres off (it shoots from there), the others close in
                    if (nearest && (command == "seekfire" ? best > 6 : best > 1.1f || command != "seek")) {
                        const float yaw = play_style.uya_camera ? uya_camera.pose().yaw : camera_yaw_offset;
                        const float dx = (nearest->position[0] - game.position[0]) / best, dz = (nearest->position[2] - game.position[2]) / best;
                        const float x = -dx * std::cos(yaw) + dz * std::sin(yaw), z = dx * std::sin(yaw) + dz * std::cos(yaw);
                        sample.sticks[0] = uint8_t(std::clamp(128.f + x * 127.f, 0.f, 255.f));
                        sample.sticks[1] = uint8_t(std::clamp(128.f - z * 127.f, 0.f, 255.f));
                    }
                }
                if (goal3) {
                    if (!game.on_magneboots() && tick > 2)
                        break;
                    const hig::Vec3 up = game.magne_up(), view = game.magne_view();
                    const hig::Vec3 side{view[1] * up[2] - view[2] * up[1], view[2] * up[0] - view[0] * up[2], view[0] * up[1] - view[1] * up[0]};
                    hig::Vec3 to{(*goal3)[0] - game.position[0], (*goal3)[1] - game.position[1], (*goal3)[2] - game.position[2]};
                    if (std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]) < .7f)
                        break;
                    const float along = to[0] * view[0] + to[1] * view[1] + to[2] * view[2], across = to[0] * side[0] + to[1] * side[1] + to[2] * side[2];
                    const float length = std::max(1e-4f, std::hypot(along, across));
                    // the stick's byte for an axis value, through the pad's dead zone (hig::pad_axis: 64 of 128)
                    auto stick = [](float wanted) {
                        if (std::fabs(wanted) < .02f)
                            return uint8_t(128);
                        const float raw = (wanted > 0 ? 1.f : -1.f) * (.5f + .5f * std::min(1.f, std::fabs(wanted)));
                        return uint8_t(std::clamp((raw + 1) * 127.5f + .5f, 0.f, 255.f));
                    };
                    sample.sticks[0] = stick(across / length);
                    sample.sticks[1] = stick(-along / length);
                    sample.buttons = magne_jump && tick == 0 ? hig::Cross : 0;
                }
                if (goal) {
                    const float dx = (*goal)[0] - game.position[0], dz = (*goal)[1] - game.position[2], left = std::hypot(dx, dz);
                    if (left < .35f)
                        break;
                    if (hop)
                        sample.buttons = tick < 8 || tick >= 12 ? hig::Cross : 0;
                    const float yaw = play_style.uya_camera ? uya_camera.pose().yaw : camera_yaw_offset;
                    const float x = -dx / left * std::cos(yaw) + dz / left * std::sin(yaw), z = dx / left * std::sin(yaw) + dz / left * std::cos(yaw);
                    sample.sticks[0] = uint8_t(std::clamp(128.f + x * 127.f, 0.f, 255.f));
                    sample.sticks[1] = uint8_t(std::clamp(128.f - z * 127.f, 0.f, 255.f));
                }
                script_input.sample(sample);
                auto pad = script_input.consume();
                if (level_loop.input(pad, game)) {
                    const bool movement_owned = tick_player(pad, play_style.uya_camera ? uya_camera.pose().yaw : camera_yaw_offset);
                    level_loop.tick(game);
                    game.tick_weapons(movement_owned ? hig::PadState{} : pad, 1.f / 30);
                    if (play_style.uya_camera) uya_camera.tick(game, pad, 1.f / 30,
                        player_assets.ps2 && player_assets.ps2->charge_dashing());
                    { // on a lock's rails the camera stays behind him, in a scripted run too (the offset is from his facing)
                        static float offset_kept = 0;
                        static bool kept = false;
                        if (game.grinding() && !kept) {
                            offset_kept = camera_yaw_offset;
                            kept = true;
                            camera_yaw_offset = 0;
                        } else if (!game.grinding() && kept) {
                            camera_yaw_offset = offset_kept;
                            kept = false;
                        }
                    }
                    audio.events(game.audio_events);
                    level_ticks++;
                }
                if (!level_loop.movie_request.empty()) { // a scripted run takes a level's movie as seen
                    std::printf("Movie: %s\n", level_loop.movie_request.c_str());
                    level_loop.movie_request.clear();
                }
                if (!level_loop.travel_request.empty()) { // and says where a level change would take him (it is not made)
                    std::printf("Travel: %s\n", level_loop.travel_request.c_str());
                    level_loop.travel_request.clear();
                }
                std::array<int16_t, 3200> samples{};
                audio.pause(level_loop.panel != sm::LevelPanel::None);
                audio.render(samples);
                if (!audio_capture.empty()) {
                    if (recorded_audio.size() > 48000ull * 2 * 600)
                        throw std::runtime_error("Audio capture exceeds ten minutes");
                    recorded_audio.insert(recorded_audio.end(), samples.begin(), samples.end());
                }
            }
            script_input.sample({});
            script_input.consume();
            std::printf(
                "%s: position=%.3f,%.3f,%.3f grounded=%d clip=%s shots=%u impacts=%u "
                "health=%u defeated=%u swimming=%d underwater=%d swim_pitch=%.3f combo_swings=%u panel=%u checkpoint=%d\n",
                word.c_str(), game.position[0], game.position[1], game.position[2], game.grounded,
                game.animation_name(player_assets), game.shots, game.impacts, game.health,
                game.defeated, game.swimming, game.underwater, game.swim_pitch, game.wrench_swings, unsigned(level_loop.panel),
                level_loop.checkpoint);
            if (level_loop.panel == sm::LevelPanel::Vendor || level_loop.purchases) { // the vendor: what it has, at what price
                std::string stock;
                for (unsigned item : game.vendor_stock())
                    stock += " " + std::to_string(item) + ":" + std::to_string(game.price(item));
                std::printf("Vendor: open=%d row=%u stock=[%s ] bolts=%u purchases=%u hypershot=%d\n",
                            int(level_loop.panel == sm::LevelPanel::Vendor), level_loop.selection, stock.c_str(), game.bolts,
                            level_loop.purchases, int(game.arsenal.owned[hig::GadgetHypershot]));
            }
            {
                // the nearest living enemy, for scripted combat checks
                float best = 1e30f;
                const sm::Enemy *nearest = nullptr;
                for (const auto &e : game.enemies) {
                    if (e.hidden || e.state == sm::EnemyState::Dead)
                        continue;
                    const float dx = e.position[0] - game.position[0], dy = e.position[1] - game.position[1],
                                dz = e.position[2] - game.position[2];
                    const float d = dx * dx + dy * dy + dz * dz;
                    if (d < best) {
                        best = d;
                        nearest = &e;
                    }
                }
                if (nearest)
                    std::printf("Nearest enemy: %s %.2f m at %.2f,%.2f,%.2f state=%s health=%.1f burning=%.1f poisoned=%.1f frozen=%.1f\n",
                                player_assets.encounters.models[nearest->model].name.c_str(), std::sqrt(best),
                                nearest->position[0], nearest->position[1], nearest->position[2],
                                sm::enemy_state_name(nearest->state), nearest->health, nearest->burn_time, nearest->poison_time, nearest->frozen);
            }
            std::printf("Weapon: %s level=%u xp=%.0f ammo=%u/%u level_ups=%u bolts=%u in_flight=%zu explosions=%u "
                        "shield=%.1f\n",
                        game.equipped_name(), unsigned(game.arsenal.level[game.arsenal.equipped]) + 1,
                        game.arsenal.xp[game.arsenal.equipped], game.ammo, game.max_ammo, game.level_ups, game.bolts,
                        game.weapon_shots.size(), game.explosions, game.shield_time());
            {
                size_t shown = 0;
                for (const auto &e : game.enemies)
                    shown += !e.hidden && e.state != sm::EnemyState::Dead;
                std::printf("Armour: worn=%u,%u,%u,%u,%u,%u set=%u protection=%.2f blocked=%u powers=%u charge=%.2f charged=%d cloaked=%d shots=%zu pieces=%d,%d,%d,%d,%d,%d\n",
                            game.armor.worn[0], game.armor.worn[1], game.armor.worn[2], game.armor.worn[3], game.armor.worn[4],
                            game.armor.worn[5], game.armor_set(), game.armor_protection(), game.armor_blocked, game.armor_powers,
                            game.armor_charge, game.armor_charged, game.armor_cloaked(), game.weapon_shots.size(),
                            player_assets.encounters.objects.armor_models[0][game.armor.worn[0] & 7],
                            player_assets.encounters.objects.armor_models[1][game.armor.worn[1] & 7],
                            player_assets.encounters.objects.armor_models[2][game.armor.worn[2] & 7],
                            player_assets.encounters.objects.armor_models[3][game.armor.worn[3] & 7],
                            player_assets.encounters.objects.armor_models[4][game.armor.worn[4] & 7],
                            player_assets.encounters.objects.armor_models[5][game.armor.worn[5] & 7]);
                if (getenv("SM_HERO_DEBUG")) { // the models of what the sets' powers leave in the world
                    for (uint32_t hash : {0x3fdd057bu, 0x850fa0b4u, 0x702127e9u, 0x3448a16bu, 0x3795cb33u, 0xd0f68f21u, 0xea0fa0a2u}) {
                        const int index = player_assets.encounters.objects.class_model(hash);
                        if (index < 0) {
                            std::printf("  power class %08x: no model\n", hash);
                            continue;
                        }
                        const auto &m = player_assets.encounters.objects.models[size_t(index)];
                        std::printf("  power class %08x: %s skinned=%d clips=%zu box %.2f %.2f %.2f .. %.2f %.2f %.2f\n", hash,
                                    m.name.c_str(), m.skinned, m.animations.clips.size(), m.minimum[0], m.minimum[1], m.minimum[2],
                                    m.maximum[0], m.maximum[1], m.maximum[2]);
                    }
                }
                if (getenv("SM_HERO_DEBUG")) // the worn pieces' models
                    for (unsigned place = 0; place < 6; place++) {
                        const int index = player_assets.encounters.objects.armor_models[place][game.armor.worn[place] & 7];
                        if (index < 0)
                            continue;
                        const auto &m = player_assets.encounters.objects.models[size_t(index)];
                        std::printf("  piece %u: %s skinned=%d bones=%zu meshes=%zu clips=%zu box %.2f %.2f %.2f .. %.2f %.2f %.2f\n",
                                    place, m.name.c_str(), m.skinned, m.actor.bones.size(), m.actor.meshes.size(),
                                    m.animations.clips.size(), m.minimum[0], m.minimum[1], m.minimum[2], m.maximum[0],
                                    m.maximum[1], m.maximum[2]);
                        std::printf("    ids:");
                        for (size_t b = 0; b < m.actor.bones.size(); b++)
                            std::printf(" %u<%d", m.actor.bones[b].id, m.actor.bones[b].parent == 255 ? -1 : int(m.actor.bones[b].parent));
                        std::printf(" | parts:");
                        {
                            std::array<unsigned, 8> parts{};
                            for (const auto &mesh : m.actor.meshes)
                                parts[mesh.part & 7]++;
                            for (unsigned k = 0; k < 8; k++)
                                std::printf(" %u", parts[k]);
                        }
                        std::puts("");
                        if (false)
                            for (size_t b = 0; b < m.actor.bones.size() && b < 24; b++)
                                std::printf("    bone %zu id %u scale %.3f ib %.2f %.2f %.2f | hero bone %u scale %.3f t %.3f %.3f %.3f ib %.2f %.2f %.2f | parent %u bind t %.3f %.3f %.3f\n", b,
                                            m.actor.bones[b].id, m.actor.bones[b].bind.scale[0], m.actor.bones[b].inverse_bind[12],
                                            m.actor.bones[b].inverse_bind[13], m.actor.bones[b].inverse_bind[14],
                                            unsigned(m.actor.bones[b].id) < 112 ? unsigned(m.actor.bones[b].id) : 0u,
                                            player_assets.ratchet.bones[m.actor.bones[b].id < 112 ? m.actor.bones[b].id : 0].bind.scale[0],
                                            player_assets.ratchet.bones[m.actor.bones[b].id < 112 ? m.actor.bones[b].id : 0].bind.translation[0],
                                            player_assets.ratchet.bones[m.actor.bones[b].id < 112 ? m.actor.bones[b].id : 0].bind.translation[1],
                                            player_assets.ratchet.bones[m.actor.bones[b].id < 112 ? m.actor.bones[b].id : 0].bind.translation[2],
                                            player_assets.ratchet.bones[m.actor.bones[b].id < 112 ? m.actor.bones[b].id : 0].inverse_bind[12],
                                            player_assets.ratchet.bones[m.actor.bones[b].id < 112 ? m.actor.bones[b].id : 0].inverse_bind[13],
                                            player_assets.ratchet.bones[m.actor.bones[b].id < 112 ? m.actor.bones[b].id : 0].inverse_bind[14],
                                            m.actor.bones[b].parent,
                                            m.actor.bones[b].bind.translation[0], m.actor.bones[b].bind.translation[1],
                                            m.actor.bones[b].bind.translation[2]);
                    }
                std::printf("Objects: crates_broken=%u pickups=%zu bolts=%u bolts_collected=%u explosions=%u "
                            "titanium=%u armor=%u targets=%zu waves=%u enemies_in_level=%zu teleports=%u deaths=%u "
                            "death_by=%x prompt=%s\n",
                            level_loop.objects.crates_broken, level_loop.objects.pickups.size(), game.bolts,
                            level_loop.objects.bolts_collected, level_loop.objects.explosions,
                            level_loop.objects.titanium_bolts, level_loop.objects.armor_pickups,
                            game.targets.size(), level_loop.objects.waves_started, shown,
                            level_loop.objects.teleports, level_loop.objects.deaths, level_loop.objects.death_source,
                            level_loop.objects.prompt.empty() ? "-" : level_loop.objects.prompt.c_str());
                if (game.clank || !level_loop.movie_request.empty())
                    std::printf("Character: %s movie=%s\n", game.clank ? "Clank" : "Ratchet",
                                level_loop.movie_request.empty() ? "-" : level_loop.movie_request.c_str());
                if (game.clank) { // the gadgebots and what they open
                    std::string bots, doors, buttons;
                    char cell[96];
                    int command = -1, awake = 0;
                    for (const auto &o : level_loop.objects.objects) {
                        if (o.pooled || !o.alive)
                            continue;
                        if (o.name == "clankbot" || o.name == "ClankBot") {
                            std::snprintf(cell, sizeof(cell), " %u@%.1f,%.1f,%.1f", unsigned(o.state), o.matrix[12], o.matrix[13], o.matrix[14]);
                            bots += cell;
                        } else if (o.name == "ClankBotController") {
                            command = o.n[0];
                            awake = o.state;
                        } else if (o.name == "Level02TriggeredDoor") {
                            std::snprintf(cell, sizeof(cell), " %u", unsigned(o.state));
                            doors += cell;
                        } else if (o.name == "Level02Button") {
                            std::snprintf(cell, sizeof(cell), " %u", unsigned(o.state));
                            buttons += cell;
                            if (getenv("SM_HERO_DEBUG"))
                                std::printf("  button %x at %.2f,%.2f,%.2f axis %.2f %.2f %.2f right %.2f %.2f %.2f\n", o.source, o.base[12],
                                            o.base[13], o.base[14], o.base[8], o.base[9], o.base[10], o.base[0], o.base[1], o.base[2]);
                        }
                    }
                    std::printf("Gadgebots: controller=%d command=%d bots=[%s ] buttons=[%s ] doors=[%s ]\n", awake, command, bots.c_str(),
                                buttons.c_str(), doors.c_str());
                    std::string flingers, ports, others;
                    unsigned houses = 0;
                    for (const auto &o : level_loop.objects.objects) {
                        if (o.pooled)
                            continue;
                        if (o.name == "ClankBotHouse")
                            houses += o.alive;
                        if (!o.alive)
                            continue;
                        if (o.name == "BOTFLINGER" || o.name == "BotFlinger") {
                            std::snprintf(cell, sizeof(cell), " %u:facing=%.0f,force=%.2f,aim=%d@%.1f,%.1f,%.1f,throws=%d", unsigned(o.state),
                                          o.f[0] * 180 / XM_PI, o.f[1], o.n[1], o.f[2], o.f[3], o.f[4], o.n[2]);
                            const float dx = o.matrix[12] - game.position[0], dz = o.matrix[14] - game.position[2];
                            if (o.state || dx * dx + dz * dz < 100)
                                flingers += cell;
                        } else if (o.name == "ClankBotPort") {
                            std::snprintf(cell, sizeof(cell), " %d", o.n[0]);
                            ports += cell;
                            if (getenv("SM_HERO_DEBUG"))
                                std::printf("  port %x at %.2f,%.2f,%.2f axis %.2f %.2f %.2f\n", o.source, o.base[12], o.base[13], o.base[14],
                                            o.base[8], o.base[9], o.base[10]);
                        } else if (o.name == "Level02Column") {
                            std::snprintf(cell, sizeof(cell), " column=%u@%.2f", unsigned(o.state), o.matrix[13]);
                            others += cell;
                        } else if (o.name == "Level02ExpandableBridge") {
                            std::snprintf(cell, sizeof(cell), " bridge=%u@%.2f", unsigned(o.state), o.f[0]);
                            others += cell;
                        } else if (o.name == "Level02Statue") {
                            std::snprintf(cell, sizeof(cell), " statue=%u@%.2f", unsigned(o.state), o.f[1]);
                            others += cell;
                        }
                    }
                    std::printf("Temple: houses=%u flinger=[%s ] ports_need=[%s ]%s\n", houses, flingers.c_str(), ports.c_str(), others.c_str());
                }
                {
                    std::string locks, gates, doors;
                    unsigned levers = 0, on = 0, bombs = 0;
                    for (const auto &o : level_loop.objects.objects) {
                        if (o.pooled)
                            continue;
                        char text[96];
                        if (o.name == "GrindrailLock") {
                            std::snprintf(text, sizeof text, " %x:%u,locked=%d,tries=%d", o.source, unsigned(o.state), o.n[0], o.n[1]);
                            locks += text;
                        } else if (o.name == "GrindrailGate") {
                            std::snprintf(text, sizeof text, " %x:%u", o.source, unsigned(o.state));
                            gates += text;
                        } else if (o.name == "GrindrailSwitch") {
                            levers++;
                            on += o.state == 1;
                        } else if (o.name == "GrindrailBomb")
                            bombs += o.alive;
                        else if (o.name.rfind("Door") != std::string::npos && o.name.rfind("Lvl", 0) == 0) {
                            std::snprintf(text, sizeof text, " %x:%u", o.source, unsigned(o.state));
                            doors += text;
                        }
                    }
                    if (!locks.empty()) {
                        const auto &g = game.grind();
                        std::printf("Grind: locks=[%s ] riding=%d phase=%d rail=%d along=%.1f height=%.2f jumps=%u crossings=%u swings=%u knocks=%u "
                                    "gates=[%s ] switches_on=%u/%u bombs=%u\n",
                                    locks.c_str(), int(game.grinding()), g.phase, g.rail, g.distance, g.height, g.jumps, g.crossings, g.swings,
                                    g.knocks, gates.c_str(), on, levers, bombs);
                    }
                    if (!doors.empty())
                        std::printf("Doors:%s\n", doors.c_str());
                    for (const auto &o : level_loop.objects.objects)
                        if (o.name == "Lvl3BoltCranker" && !o.pooled) { // Kalidon's fluid lake
                            std::string plates;
                            for (unsigned k = 0; k < 10; k++)
                                if (const auto *m = level_loop.objects.find(o.props.u32("platforms", k))) {
                                    char text[48];
                                    std::snprintf(text, sizeof text, " %s%u@%.1f", m->name == "Hookshot" ? "h" : "p", unsigned(m->state), m->matrix[13]);
                                    plates += (m->name == "Hookshot" ? !m->n[3] : m->visible) ? text : " -";
                                }
                            std::printf("Lake: cranker=%u progress=%.2f platforms=[%s ]\n", unsigned(o.state), o.f[0], plates.c_str());
                        }
                    for (const auto &o : level_loop.objects.objects)
                        if (o.name == "Level04Crane" && !o.pooled) // Metalis's crane
                            std::printf("Crane: state=%u angle=%.3f frames=%.0f end=%.2f,%.2f,%.2f\n", unsigned(o.state), o.f[1], o.f[0],
                                        o.base[12] + std::sin(o.f[1] + .069f) * 27.1f, o.base[13] + 1.81f, o.base[14] + std::cos(o.f[1] + .069f) * 27.1f);
                    for (const auto &o : level_loop.objects.objects)
                        if (o.name == "Mungo" && !o.pooled) // Kalidon's boss
                            std::printf("Mungo: state=%u health=%.1f/%.1f phase=%d entries=%d at %.2f,%.2f,%.2f clip=%d time=%.2f repos=%.2f\n", unsigned(o.state),
                                        o.f[0], o.f[2], o.n[0], o.n[1], o.matrix[12], o.matrix[13], o.matrix[14], o.clip, o.time, o.f[7]);
                    for (const auto &o : level_loop.objects.objects)
                        if (o.name == "ConveyerJunkController" && !o.pooled) { // Kalidon's belts
                            std::string items;
                            for (uint32_t source : o.members)
                                if (const auto *m = level_loop.objects.find(source)) {
                                    char text[40];
                                    std::snprintf(text, sizeof text, " %d@%.1f%s", m->n[0], m->matrix[14], m->model < 0 ? "(no model)" : m->visible ? "" : "(hidden)");
                                    items += text;
                                }
                            std::printf("Belt: %x state=%u run=%.2f items=[%s ]\n", o.source, unsigned(o.state), o.f[0], items.c_str());
                        }
                    for (const auto &o : level_loop.objects.objects)
                        if (o.name == "Lvl3Platform" && !o.pooled) { // Kalidon's shaft
                            std::string lifts, huts, waves;
                            unsigned ledges = 0, bombs = 0;
                            float fluid = 0;
                            for (const auto &m : level_loop.objects.objects) {
                                char text[64];
                                if (m.name == "EnemyWave" && std::fabs(m.base[12] - o.base[12]) < 1 && std::fabs(m.base[14] - o.base[14]) < 2) {
                                    std::snprintf(text, sizeof text, " %u%s:%d/%zu", unsigned(m.state), m.alive ? "" : "x", m.n[0], m.members.size());
                                    waves += text;
                                } else if (m.name == "Lvl3Elevator") {
                                    std::snprintf(text, sizeof text, " %u@%.1f%s%s", unsigned(m.state), m.matrix[13], m.collision ? "" : "(no collision)",
                                                  m.solid ? "s" : "");
                                    lifts += text;
                                } else if (m.name == "Lvl3StaticPlatform")
                                    ledges += m.visible;
                                else if (m.name == "PODBomb")
                                    bombs += m.state >= 1 && m.state <= 3;
                                else if (m.name == "HutDoor" && m.state) {
                                    std::snprintf(text, sizeof text, " %x:%u", m.source, unsigned(m.state));
                                    huts += text;
                                } else if (m.source == o.props.u32("fluidMoby"))
                                    fluid = m.matrix[13];
                            }
                            std::printf("Shaft: state=%u y=%.2f stop=%d wave=%d bombset=%d bomb=%d bombs_live=%u fluid=%.2f lifts=[%s ] ledges=%u "
                                        "huts_open=[%s ] waves=[%s ]\n",
                                        unsigned(o.state), o.matrix[13], o.n[0], o.n[1], o.n[2], o.n[3], bombs, fluid, lifts.c_str(), ledges,
                                        huts.c_str(), waves.c_str());
                        }
                }
                if (game.on_magneboots())
                    std::printf("Magneboots: normal=%.2f,%.2f,%.2f up=%.2f,%.2f,%.2f forward=%.2f,%.2f,%.2f air=%d\n", game.magne_normal()[0],
                                game.magne_normal()[1], game.magne_normal()[2], game.magne_up()[0], game.magne_up()[1], game.magne_up()[2],
                                game.magne_forward()[0], game.magne_forward()[1], game.magne_forward()[2], int(game.magne_in_air()));
                if (const char *wanted = getenv("SM_OBJECT_CLIPS"); wanted && *wanted) { // research: a class's model and clips
                    const std::string name = wanted;
                    _putenv("SM_OBJECT_CLIPS=");
                    for (const auto &o : level_loop.objects.objects)
                        if (o.name == name) {
                            std::printf("ObjectModel %s@%x: model=%d at %.2f,%.2f,%.2f\n", name.c_str(), o.source, o.model, o.matrix[12],
                                        o.matrix[13], o.matrix[14]);
                            if (o.model >= 0) {
                                const auto &m = player_assets.encounters.objects.models[size_t(o.model)];
                                std::printf("  skinned=%d bones=%zu bounds=%.2f,%.2f,%.2f..%.2f,%.2f,%.2f collision=%d\n", int(m.skinned),
                                            m.actor.bones.size(), m.minimum[0], m.minimum[1], m.minimum[2], m.maximum[0], m.maximum[1],
                                            m.maximum[2], int(o.collision != nullptr));
                                for (size_t k = 0; k < m.animations.clips.size(); k++) {
                                    std::printf("  clip %zu (0x%zx): %s %.3f s", k, k, m.animations.clips[k].name.c_str(),
                                                m.animations.clips[k].duration);
                                    for (const auto &event : m.animations.clips[k].events)
                                        std::printf(" [%s@%.2f]", event.name.c_str(), event.time);
                                    std::printf("\n");
                                }
                            }
                            break;
                        }
                }
                if (const char *list = getenv("SM_ENEMY_CLIPS"); list && *list == '1') { // research: the enemy classes' clips
                    _putenv("SM_ENEMY_CLIPS=0");
                    for (const auto &m : player_assets.encounters.models) {
                        std::printf("EnemyModel %s: speed=%.2f reach=%.2f health=%u damage=%u idle=%u alert=%u run=%u attack=%u hurt=%u death=%u\n",
                                    m.name.c_str(), m.speed, m.attack_distance, m.health, m.damage, m.idle, m.alert, m.run, m.attack, m.hurt,
                                    m.death);
                        for (size_t k = 0; k < m.animations.clips.size(); k++) {
                            std::printf("  clip %zu: %s %.3f s", k, m.animations.clips[k].name.c_str(), m.animations.clips[k].duration);
                            for (const auto &event : m.animations.clips[k].events)
                                std::printf(" [%s@%.2f]", event.name.c_str(), event.time);
                            std::printf("\n");
                        }
                    }
                }
                if (const char *list = getenv("SM_CLIPS"); list && *list == '1') { // research: the hero's clips by index
                    _putenv("SM_CLIPS=0");
                    for (size_t k = 0; k < player_assets.animations.clips.size(); k++)
                        std::printf("Clip %zu (0x%zx): %s %.3f s\n", k, k, player_assets.animations.clips[k].name.c_str(),
                                    player_assets.animations.clips[k].duration);
                }
                if (!game.hooks.empty())
                    std::printf("Hypershot: hooks=%zu in_reach=%d phase=%d grapples=%u\n", game.hooks.size(), int(game.hook_in_reach() != nullptr),
                                game.grapple_phase(), game.grapples);
                { // the Sprout-O-Matic's world, where the level has one
                    std::string sprouts, soils, plants;
                    char cell[96];
                    unsigned walls = 0;
                    for (const auto &o : level_loop.objects.objects) {
                        if (o.kind == sm::ObjectKind::Logic || !o.alive)
                            continue;
                        walls += (o.name == "Level02WallPiece" || o.name == "Level02WallChunk") && o.solid;
                        if ((o.name == "ElectroSoil" || o.name == "CannonSprout") && getenv("SM_HERO_DEBUG"))
                            std::printf("  %s %x at %.1f,%.1f,%.1f right %.2f %.2f %.2f forward %.2f %.2f %.2f\n", o.name.c_str(), o.source,
                                        o.matrix[12], o.matrix[13], o.matrix[14], o.matrix[0], o.matrix[1], o.matrix[2], o.matrix[8],
                                        o.matrix[9], o.matrix[10]);
                        if (o.name == "LadderSprout" && o.collision && getenv("SM_HERO_DEBUG")) {
                            std::map<uint32_t, unsigned> kinds;
                            for (const auto &face : o.collision->faces)
                                kinds[(uint32_t(face.flags) << 16) ^ face.surface]++;
                            std::printf("  ladder collision: %zu faces, box %.2f %.2f %.2f .. %.2f %.2f %.2f; flags^surface:", o.collision->faces.size(),
                                        o.collision->minimum[0], o.collision->minimum[1], o.collision->minimum[2], o.collision->maximum[0],
                                        o.collision->maximum[1], o.collision->maximum[2]);
                            for (const auto &[surface, count] : kinds)
                                std::printf(" %08x x%u", surface, count);
                            if (!o.collision->faces.empty())
                                std::printf("; first face flags %04x surface %08x normal %.2f %.2f %.2f", o.collision->faces[0].flags,
                                            o.collision->faces[0].surface, o.collision->faces[0].planes[0], o.collision->faces[0].planes[1],
                                            o.collision->faces[0].planes[2]);
                            std::printf("; forward %.2f %.2f %.2f\n", o.matrix[8], o.matrix[9], o.matrix[10]);
                            // its level faces by height (model space): where one can stand on it
                            std::map<int, std::array<float, 4>> steps;
                            for (const auto &face : o.collision->faces) {
                                if (face.planes[1] < .7f)
                                    continue;
                                float low[3]{1e9f, 1e9f, 1e9f}, high[3]{-1e9f, -1e9f, -1e9f};
                                for (int k = 0; k < 3; k++)
                                    for (int a = 0; a < 3; a++) {
                                        low[a] = std::min(low[a], o.collision->vertices[face.vertices[k]][a]);
                                        high[a] = std::max(high[a], o.collision->vertices[face.vertices[k]][a]);
                                    }
                                auto [it, fresh] = steps.try_emplace(int(std::lround(high[1] * 10)), std::array<float, 4>{low[0], high[0], low[2], high[2]});
                                if (!fresh) {
                                    it->second[0] = std::min(it->second[0], low[0]), it->second[1] = std::max(it->second[1], high[0]);
                                    it->second[2] = std::min(it->second[2], low[2]), it->second[3] = std::max(it->second[3], high[2]);
                                }
                            }
                            for (const auto &[height, box] : steps)
                                std::printf("    step y %.1f x %.2f..%.2f z %.2f..%.2f\n", height * .1f, box[0], box[1], box[2], box[3]);
                        }
                        if (o.name == "Level02WallPiece" && getenv("SM_HERO_DEBUG"))
                            std::printf("  wall piece %x at %.1f,%.1f,%.1f model=%d collision=%d solid=%d visible=%d state=%u\n", o.source,
                                        o.matrix[12], o.matrix[13], o.matrix[14], o.model, int(o.collision != nullptr), int(o.solid),
                                        int(o.visible), unsigned(o.state));
                        if (o.name == "SPROUT" || o.name == "Sprout") {
                            std::snprintf(cell, sizeof(cell), " %u@%.1f,%.1f,%.1f", unsigned(o.state), o.matrix[12], o.matrix[13], o.matrix[14]);
                            sprouts += cell;
                        } else if (o.name == "ElectroSoil" || o.name == "Electrosoil") {
                            std::snprintf(cell, sizeof(cell), " %u/%d", unsigned(o.state), o.n[0]);
                            soils += cell;
                        } else if (o.name == "LadderSprout" || o.name == "CannonSprout" || o.name == "ExplodeSprout") {
                            std::snprintf(cell, sizeof(cell), " %s:%u@%.1f,%.1f,%.1f%s", o.name.c_str(), unsigned(o.state), o.matrix[12],
                                          o.matrix[13], o.matrix[14], o.solid ? " solid" : "");
                            plants += cell;
                        }
                    }
                    if (!sprouts.empty() || !soils.empty())
                        std::printf("Sprouts: gadget=%d spray=%d at %.1f,%.1f,%.1f follower=%x sprouts=[%s ] soils=[%s ] plants=[%s ] launched=%d wall_pieces=%u\n",
                                    int(game.arsenal.owned[hig::GadgetSproutOMatic]), int(game.sprout_spray.on),
                                    game.sprout_spray.point[0], game.sprout_spray.point[1], game.sprout_spray.point[2],
                                    level_loop.objects.sprout_follower, sprouts.c_str(), soils.c_str(), plants.c_str(),
                                    int(game.launched()), walls);
                    for (const auto &o : level_loop.objects.objects) // the gates that go when their enemies are gone
                        if (o.name == "MovedObject" && !o.pooled)
                            std::printf("Gate: %x state=%u at %.1f,%.2f,%.1f solid=%d\n", o.source, unsigned(o.state), o.matrix[12], o.matrix[13],
                                        o.matrix[14], int(o.solid));
                }
            }
            if (player_assets.ps2_weapons && player_assets.ps2_weapons->enabled())
                std::printf("UYA weapon: %s\n", player_assets.ps2_weapons->status().c_str());
            if (player_assets.ps2_packs && player_assets.ps2_packs->loaded()) {
                std::printf("UYA packs: %s\n", player_assets.ps2_packs->status().c_str());
                const auto stats = player_assets.ps2_packs->stats();
                std::printf("Pack state: equipped=%d desired=%d sequence=%d clank=%d particles=%zu ticks=%u boost=%d native_heli=%d\n",
                            stats.equipped, stats.desired, stats.pack_sequence, stats.clank_sequence,
                            stats.particles, stats.ticks, game.swim_boosting(), game.native_heli_clip());
            }
            if(player_assets.ps2 && player_assets.ps2->loaded()) {
                const auto fx=player_assets.ps2->hover_effects_stats();
                std::printf("Hover sole FX: loaded=%d active=%d ticks=%llu live=%zu thrust=%d state=%s\n",
                    fx.loaded,fx.active,static_cast<unsigned long long>(fx.ticks),fx.particles,fx.thrusting,
                    player_assets.ps2->state_name().c_str());
            }
        }
        if (!audio_capture.empty()) {
            if (!audio.write_capture(audio_capture, recorded_audio, error))
                throw std::runtime_error(error);
            std::printf("Audio capture: %zu stereo frames, %u mapped action events\n",
                        recorded_audio.size() / 2, audio.event_count());
        }
        { // what became of the run's sound events
            const auto tally = audio.tally();
            std::string heard, unbound, unsupported;
            char cell[32];
            for (const auto &[sound, count] : tally.heard)
                std::snprintf(cell, sizeof(cell), " %08x:%u", sound, count), heard += cell;
            for (const auto &[sound, count] : tally.unbound)
                std::snprintf(cell, sizeof(cell), " %08x:%u", sound, count), unbound += cell;
            for (const auto &[sound, count] : tally.unsupported)
                std::snprintf(cell, sizeof(cell), " %08x:%u", sound, count), unsupported += cell;
            std::printf("Sounds: played=%u heard=[%s ] not_in_the_banks=[%s ] program_not_played=[%s ]\n", tally.played, heard.c_str(),
                        unbound.c_str(), unsupported.c_str());
            if (getenv("SM_HERO_DEBUG")) // research: the programs the mixer does not play, grain by grain
                for (const auto &[sound, count] : tally.unsupported) {
                    const auto binding = audio.assets().bindings.find(sound);
                    if (binding == audio.assets().bindings.end())
                        continue;
                    const auto bank = audio.assets().banks.find(binding->second.bank);
                    if (bank == audio.assets().banks.end() || binding->second.index >= bank->second.sounds.size())
                        continue;
                    std::printf("  program %08x:", sound);
                    for (const auto &grain : bank->second.sounds[binding->second.index].grains)
                        std::printf(" %u/%06x+%u", grain.opcode >> 24, grain.opcode & 0xffffff, unsigned(grain.delay));
                    std::printf("\n");
                }
        }
        if (!frame_limit)
            return 0;
    }
    if (inspect)
        return 0;
    if (!movie_path.empty()) {
        if (!begin_movie(movie_path))
            throw std::runtime_error(error);
        if (movie_frame >= int(movie.info().frames) || movie_frame < -1 ||
            !std::isfinite(movie_skip_after))
            throw std::runtime_error("Invalid movie preview frame/timing");
    }
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.lpszClassName = L"TheiaSizeMattersNative";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        throw std::runtime_error("Register native window failed");
    RECT rect{0, 0, 1280, 720};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND window =
        CreateWindowW(wc.lpszClassName, L"Size Matters - Native engine development",
                      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
                      rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
    if (!window)
        throw std::runtime_error("Create native window failed");
    sm::RenderDx11 renderer;
    if (!renderer.init(window, display.width, display.height, debug, error))
        throw std::runtime_error(error);
    if (!hud.upload(renderer, error))
        throw std::runtime_error(error);
    // The device thread is also the recording clock, so it runs muted when only recording needs it.
    bool sound_output = (!hidden || !record_path.empty()) && audio.open_device(error);
    audio.silent(mute || hidden);
    if (!hidden && !mute && !sound_output)
        std::fprintf(stderr, "%s\n", error.c_str());
    if (!level.empty() && !renderer.upload(scene, render_level, error))
        throw std::runtime_error(error);
    std::vector<void *> background;
    for (const auto &image : frontend)
        if (image.source_path.find("hiresfrontend.gim") != std::string::npos) {
            auto *view = renderer.upload_picture(image.image, error);
            if (!view)
                throw std::runtime_error(error);
            background.push_back(view);
        }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init((ID3D11Device *)renderer.device(),
                        (ID3D11DeviceContext *)renderer.context());
    if (!hidden)
        ShowWindow(window, SW_SHOWNORMAL);
    std::filesystem::path exe_dir;
    std::string executable_updated = std::string(__DATE__) + " " + __TIME__;
    {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        exe_dir = std::filesystem::path(exe).parent_path();
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        FILETIME local_time{};
        SYSTEMTIME updated{};
        if (GetFileAttributesExW(exe, GetFileExInfoStandard, &attributes) &&
            FileTimeToLocalFileTime(&attributes.ftLastWriteTime, &local_time) &&
            FileTimeToSystemTime(&local_time, &updated)) {
            char label[32]{};
            std::snprintf(label, sizeof label, "%04u-%02u-%02u %02u:%02u:%02u",
                updated.wYear, updated.wMonth, updated.wDay, updated.wHour, updated.wMinute, updated.wSecond);
            executable_updated = label;
        }
    }
    media::CaptureController capture_ctl("Size Matters", "sm",
                                         capture_settings_path.empty() ? exe_dir / "sm_pc.capture.ini" : capture_settings_path);
    capture_ctl.set_window(window);
    {
        auto &cs = capture_ctl.settings();
        if (!capture_folder.empty()) cs.folder = capture_folder;
        if (record_fps) cs.fps = *record_fps == 30 ? 30 : 60;
        if (record_height) cs.height = *record_height;
        if (record_no_audio) cs.audio = false;
        if (!record_quality.empty())
            cs.quality = record_quality == "low" ? media::Quality::low : record_quality == "medium" ? media::Quality::medium
                       : record_quality == "ultra" ? media::Quality::ultra : media::Quality::high;
    }
    if (!record_path.empty() && !frame_limit)
        throw std::runtime_error("--record needs a bounded --frames run; use F9 to record live play");
    audio.capture([&capture_ctl](const int16_t *stereo, size_t count) { capture_ctl.recorder().audio(stereo, count); });
    std::unique_ptr<sm::Pilot> pilot;
    if (!pilot_plan.empty())
        pilot = std::make_unique<sm::Pilot>(pilot_plan);
    sm::FixedClock clock;
    hig::PadState pad;
    Camera camera;
    if (!level.empty()) {
        camera.yaw = game.heading + camera_yaw_offset;
        camera.pitch = -.25f;
    }
    auto start = std::chrono::steady_clock::now(), previous = start;
    auto pending = display;
    bool alive = true, browser = false, shell = true, ocean = true, overlay = show_options,
         free_camera = false;
    float environment_seconds = float(level_ticks) / 30.f;
    std::string selection = level.empty() ? first_wad : level, message;
    unsigned frames = 0;
    float fps = 0;
    bool last_pad = false;
    float music_volume = float(pause.options.music) / 10, effects_volume = float(pause.options.effects) / 10;
    sm::InputLatch input;
    sm::MenuRepeat repeat;
    bool wait_input_release = false;
    // After the player's assets gained textures (a skin, a pack, a weapon set read from another disc): the renderer
    // takes the list again, and the time the read took is not played back as gameplay ticks.
    auto player_textures_changed = [&]() {
        render_level.textures = runtime.world().level.textures;
        render_level.textures.insert(render_level.textures.end(), player_assets.textures.begin(), player_assets.textures.end());
        std::string problem;
        if (!renderer.upload(scene, render_level, problem))
            pause.notice = problem;
        previous = std::chrono::steady_clock::now();
        input.clear();
        repeat.clear();
        wait_input_release = true;
    };
    // SPECIAL > CHEATS changed one of the play options.
    auto apply_goodies = [&]() {
        const auto wanted = pause.goodies;
        const auto textures = player_assets.textures.size();
        pause.notice.clear();
        if (wanted.uya_camera != play_style.uya_camera) {
            play_style.uya_camera = wanted.uya_camera;
            if (player_ready)
                uya_camera.reset(game, camera.yaw, camera.pitch);
        }
        if (wanted.thruster_pack != play_style.thruster_pack || wanted.hydro_pack != play_style.hydro_pack) {
            play_style.thruster_pack = wanted.thruster_pack;
            play_style.hydro_pack = wanted.hydro_pack;
            if (!apply_packs()) {
                play_style.thruster_pack = false;
                pause.notice = pack_message;
            }
        }
        if (wanted.uya_weapons != (player_assets.ps2_weapons && player_assets.ps2_weapons->enabled())) {
            play_style.uya_weapons = wanted.uya_weapons;
            if (!apply_weapons()) {
                play_style.uya_weapons = false;
                pause.notice = weapon_message;
            }
        }
        bool wrench_changed = false;
        if (const int word = wanted.wrench == 1 ? 118 : wanted.wrench == 2 ? 122 : wanted.wrench == 3 ? 126 : 0;
            word != play_style.uya_wrench) {
            play_style.uya_wrench = word;
            if (!apply_wrench()) {
                play_style.uya_wrench = 0;
                apply_wrench();
                pause.notice = weapon_message;
            }
            wrench_changed = true; // its textures took the wrench's slots: the renderer needs them again
        }
        if (wanted.uya_look != (play_style.rendering == sm::uya_render_options()))
            play_style.rendering = wanted.uya_look ? sm::uya_render_options() : sm::RenderOptions{};
        if (wanted.charge_boots != ratchet_settings.charge_boots) {
            ratchet_settings.charge_boots = wanted.charge_boots;
            if (ratchet_settings.active())
                apply_ratchet();
            std::string why;
            if (!hidden)
                sm::save_ps2_ratchet_settings(sm::ps2_ratchet_settings_path(), ratchet_settings, why);
        }
        if (wanted.all_armor && !all_armor && player_assets.encounters.objects.has_armor) {
            all_armor = true;
            game.configure_armor(&player_assets.encounters.objects.armor, true);
        }
        if (wanted.all_weapons && !all_weapons && player_assets.encounters.objects.has_weapons) {
            all_weapons = true;
            game.configure_weapons(&player_assets.encounters.objects.weapons, player_assets.encounters.objects.level, true);
        }
        std::string why;
        if (!hidden && !sm::save_play_style(play_style_path, play_style, why) && pause.notice.empty())
            pause.notice = why;
        if (player_assets.textures.size() != textures || wrench_changed)
            player_textures_changed();
        pause_state();
    };
    // SPECIAL > SKINS chose a skin: one of Size Matters' own, or a Ratchet imported from a PS2 disc.
    auto apply_skin = [&]() {
        const auto wanted = pause.skin;
        const auto textures = player_assets.textures.size();
        pause.notice.clear();
        std::string why;
        const bool own_before = ratchet_settings.own_controller;
        ratchet_settings.own_controller = wanted.own_gameplay;
        if (wanted.skin < sm::hero_skin_count) {
            if (!sm::load_hero_skin(runtime.disc(), wanted.skin, player_assets, why))
                pause.notice = why;
            else {
                ratchet_settings.model = "sm";
                ratchet_settings.skin = int(wanted.skin);
                player_assets.ps2.reset();
            }
        } else {
            const auto before = ratchet_settings;
            ratchet_settings.model = wanted.skin == sm::PauseSkin::GoingCommando     ? "gc"
                                     : wanted.skin == sm::PauseSkin::RatchetAndClank ? "rac1"
                                                                                     : "uya";
            ratchet_settings.animations = ratchet_settings.model;
            ratchet_settings.armor = wanted.armor;
            apply_ratchet();
            if (player_assets.ps2 && ratchet_settings.own_controller && !own_before) {
                // his own gameplay comes with his own game's camera (CHEATS can still switch it)
                play_style.uya_camera = true;
                if (player_ready)
                    uya_camera.reset(game, camera.yaw, camera.pitch);
            }
            if (!player_assets.ps2) { // its disc is not here: he keeps what he wore
                ratchet_settings = before;
                if (ratchet_settings.active())
                    apply_ratchet();
                pause.notice = "That Ratchet needs the original disc of its game.";
            }
        }
        if (!hidden && pause.notice.empty() &&
            !sm::save_ps2_ratchet_settings(sm::ps2_ratchet_settings_path(), ratchet_settings, why))
            pause.notice = why;
        if (player_assets.textures.size() != textures)
            player_textures_changed();
        pause_state();
    };
    if (scripted_pause == sm::PauseAction::ApplyGoodies)
        apply_goodies();
    else if (scripted_pause == sm::PauseAction::ApplySkin)
        apply_skin();
    auto end_movie = [&](bool skipped) {
        level_loop.mechanisms.movie_finished();
        std::printf("Movie %s at %.3f seconds; gameplay resumed\n",
                    skipped ? "skipped" : "finished", movie_seconds);
        audio.movie(nullptr);
        movie.stop();
        movie_active = false;
        movie_ready = false;
        input.clear();
        repeat.clear();
        wait_input_release = true;
        previous = std::chrono::steady_clock::now();
    };
    if (photo_start) {
        if (!player_ready)
            throw std::runtime_error("Photo mode requires a loaded level");
        photo.seed(runtime.disc(), player_assets);
        photo.enter(camera, game, 1);
        if (!photo_models.empty()) {
            for (const auto &source : photo.sources) {
                std::vector<hig::ActorCatalogEntry> entries;
                if (!sm::read_photo_catalog(runtime.disc(), source, entries, error))
                    throw std::runtime_error(error);
                photo.session.catalog.insert(photo.session.catalog.end(), entries.begin(), entries.end());
            }
            photo.scanned = photo.sources.size();
            for (const auto &name : photo_models) {
                auto entry = std::find_if(photo.session.catalog.begin(), photo.session.catalog.end(),
                                         [&](const auto &c) { return c.name.find(name) != std::string::npos; });
                if (entry == photo.session.catalog.end())
                    throw std::runtime_error("Photo character not found: " + name);
                size_t asset;
                if (!photo.session.load(runtime.disc(), size_t(entry - photo.session.catalog.begin()), asset, error))
                    throw std::runtime_error(error);
                auto at = game.position;
                at[0] += float(photo.session.actors.size()) * 1.5f;
                photo.session.spawn(asset, at, game.heading);
            }
        }
        auto *actor = photo.session.find(photo.session.selected);
        if (photo_clip >= 0) {
            if (size_t(photo_clip) >= photo.session.assets[actor->asset].animations.clips.size())
                throw std::runtime_error("Photo clip index out of range");
            actor->clip = unsigned(photo_clip);
            photo.requested_tab = 1;
        }
        if (!std::isfinite(photo_time) || photo_time < 0)
            throw std::runtime_error("Photo time must be a finite nonnegative number");
        actor->time = std::min(photo_time, photo.session.clip(*actor).duration);
        photo.frame(camera, *actor);
        photo.saved_camera = camera;
        photo.visible = !photo_hide_ui;
        photo.resolution = photo_resolution == 2160 ? 2 : photo_resolution == 1080 ? 1 : 0;
        photo.capture_requested = !photo_shot.empty();
        std::printf("Photo mode: %zu catalog entries, %zu staged actors, clip %u at %.3fs\n",
                    photo.session.catalog.size(), photo.session.actors.size(), actor->clip, actor->time);
    }
    // Esc is Start in play and Circle in menus. The choice is latched at key-down: re-evaluating it while the key is
    // held turned one press into Start (pause opens) then Circle (pause closes) then Start..., a flickering pause menu.
    uint32_t escape_button = 0;
    timeBeginPeriod(1);
    while (alive) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                alive = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!alive)
            break;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = now - previous;
        previous = now;
        float dt = std::min(.1f, std::chrono::duration<float>(elapsed).count());
        if (dt > 0)
            fps = fps * .95f + .05f / dt;
        bool focused = GetForegroundWindow() == window;
        auto device = sm::poll_controller(focused);
        if (fitted_width != display.width || fitted_height != display.height) {
            fitted_width = display.width;
            fitted_height = display.height;
            fit_window(window, display.width, display.height);
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        // The interface is laid out in the window's client pixels and scaled to the picture (scale_ui). A hidden
        // run has no window to look at: its interface is laid out on the picture itself, so a capture is what a
        // window of exactly that resolution shows.
        RECT client{};
        GetClientRect(window, &client);
        if (hidden || client.right <= 0 || client.bottom <= 0)
            io.DisplaySize = ImVec2(float(display.width), float(display.height));
        const unsigned shape_width = hidden || client.right <= 0 ? display.width : unsigned(client.right),
                       shape_height = hidden || client.bottom <= 0 ? display.height : unsigned(client.bottom);
        ImGui::NewFrame();
        capture_ctl.ui(sound_output);
        if (!record_path.empty() && frames + 1 == record_start)
            capture_ctl.request_recording(record_path);
        auto sample = device.sample;
        if (focused) {
            auto key = [&](int vk, uint32_t button) {
                if (GetAsyncKeyState(vk) & 0x8000)
                    sample.buttons |= button;
            };
            key(VK_UP, hig::Up);
            key(VK_DOWN, hig::Down);
            key(VK_RETURN, hig::Cross);
            if (movie_active)
                key(VK_SPACE, hig::Cross);
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                if (!escape_button)
                    escape_button = flow.level_active() ? hig::Start : hig::Circle;
                sample.buttons |= escape_button;
            } else
                escape_button = 0;
            if (flow.level_active() && !movie_active) {
                key('H', hig::Select);
                if (level_loop.nearby_vendor >= 0 || level_loop.mechanisms.nearby >= 0 ||
                    level_loop.mechanisms.active >= 0 || level_loop.panel == sm::LevelPanel::Help)
                    key('R', hig::Triangle);
                key(VK_LEFT, hig::Left);
                key(VK_RIGHT, hig::Right);
            }
            if (flow.level_active() && !movie_active && !photo.active && !free_camera && !io.WantCaptureKeyboard) {
                auto held = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
                if (held('A') || held('D'))
                    sample.sticks[0] = held('D') ? 255 : 0;
                if (held('W') || held('S'))
                    sample.sticks[1] = held('S') ? 255 : 0;
                key(VK_SPACE, hig::Cross);
                key('E', hig::Circle);
                key('F', hig::Square);
                key('B', hig::L3);           // PS2 Ratchet override: hoverboots on/off
                key(VK_LSHIFT, hig::R1);     // hoverboot thrust
                key(VK_LCONTROL, hig::R2);   // hoverboot brake
                key('C', hig::R1);          // crouch / high jump / long jump
                key('Z', hig::L2);
                if (held('G'))
                    sample.buttons |= hig::L2 | hig::Square;
                key(VK_TAB, hig::Triangle);
                if (!io.WantCaptureMouse && held(VK_LBUTTON))
                    sample.buttons |= hig::Circle;
            }
        } else {
            input.clear();
            repeat.clear();
            escape_button = 0;
        }
        if (pilot) { // the pilot plays: skip movies, then drive Ratchet from live state
            if (movie_active)
                sample = {(frames / 6) % 2 ? uint32_t(hig::Cross) : 0u, {128, 128, 128, 128}};
            else if (flow.level_active() && player_ready && !photo.active && level_loop.panel == sm::LevelPanel::None)
                sample = pilot->sample(game, camera.yaw, dt);
        }
        if (device.sample.buttons ||
            std::any_of(device.sample.sticks.begin(), device.sample.sticks.end(),
                        [](uint8_t n) { return n < 64 || n > 191; }))
            last_pad = true;
        if (ImGui::IsMouseClicked(0) || sample.buttons != device.sample.buttons)
            last_pad = false;
        if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
            if (photo.active)
                photo.visible = !photo.visible;
            else
                overlay = !overlay;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
            if (photo.active)
                photo.exit_requested = true;
            else if (player_ready && flow.level_visible() && !movie_active &&
                     !browser && level_loop.panel == sm::LevelPanel::None)
                photo.enter_requested = true;
        }
        if (photo.enter_requested) {
            photo.seed(runtime.disc(), player_assets);
            photo.enter(camera, game, float(clock.alpha()));
            input.clear();
            repeat.clear();
            elapsed = std::chrono::steady_clock::duration::zero();
        }
        if (photo.active && ImGui::IsKeyPressed(ImGuiKey_F12, false))
            photo.capture_requested = true;
        if (photo.active && ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
            photo.exit_requested = true;
        if (photo.exit_requested) {
            camera = photo.saved_camera;
            photo.active = photo.exit_requested = false;
            if (play_style.uya_camera) uya_camera.reset(game, camera.yaw, camera.pitch);
            input.clear();
            repeat.clear();
            wait_input_release = true;
            elapsed = std::chrono::steady_clock::duration::zero();
        }
        if (flow.loading() && !movie_active) {
            bool ok = load(first_wad) && renderer.upload(scene, render_level, error);
            flow.loaded(ok, error);
            if (ok) {
                level = first_wad;
                {
                    camera.yaw = game.heading;
                    camera.pitch = -.25f;
                }
                level_ticks = 0;
                begin_movie("/VID/L01/01A.BIK");
            }
            // Synchronous asset residency must not become catch-up game ticks.
            previous = std::chrono::steady_clock::now();
            elapsed = std::chrono::steady_clock::duration::zero();
            input.clear();
            repeat.clear();
        }
        if (play_style.uya_camera && player_ready && flow.level_active() && !movie_active &&
            !photo.active && !free_camera && !browser && focused &&
            level_loop.panel == sm::LevelPanel::None && ImGui::IsMouseDown(1) && !io.WantCaptureMouse)
            uya_camera.add_look(-io.MouseDelta.x * .004f, -io.MouseDelta.y * .004f);
        input.sample(sample);
        auto old_screen = flow.screen();
        auto steps = clock.advance(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed));
        for (unsigned i = 0; i < steps; i++) {
            pad = input.consume();
            if (photo.active) {
                if (pad.pressed & hig::Start)
                    photo.exit_requested = true;
                continue;
            }
            if (movie_active) {
                if (pad.pressed & (hig::Cross | hig::Circle | hig::Start))
                    end_movie(true);
                continue;
            }
            if (wait_input_release) {
                if (!pad.raw.buttons)
                    wait_input_release = false;
                continue;
            }
            if (flow.level_active() && !browser && player_ready && !free_camera &&
                gameplay_script.empty() && !level_loop.input(pad, game))
                continue;
            auto pulse = repeat.tick(pad);
            // ImGui owns interaction within the PC graphics widgets.
            if (flow.screen() == sm::Screen::Display) {
                pulse &= hig::Circle;
                // Esc/B first closes an open graphics dropdown.
                if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
                    pulse = 0;
            }
            bool was_playing = flow.level_active();
            if (!browser && flow.screen() == sm::Screen::Pause) {
                // the pause menu owns the pad while it is up
                switch (pause.pulse(pulse, game)) {
                case sm::PauseAction::Resume:
                    flow.choose(0);
                    break;
                case sm::PauseAction::QuitToTitle:
                    flow.choose(2);
                    break;
                case sm::PauseAction::RestartLevel:
                    pending_wad = runtime.world().disc_path;
                    flow.choose(0);
                    break;
                case sm::PauseAction::ApplyGraphics: {
                    sm::DisplaySettings wanted = display;
                    wanted.width = pause.graphics.width;
                    wanted.height = pause.graphics.height;
                    wanted.fps = pause.graphics.fps;
                    wanted.vsync = pause.graphics.vsync;
                    std::string problem;
                    if (renderer.resize(wanted.width, wanted.height, problem)) {
                        display = wanted;
                        pause.notice = sm::save_display(prefs, display, problem) ? "Graphics settings saved." : problem;
                    } else
                        pause.notice = problem;
                    break;
                }
                case sm::PauseAction::ApplyGoodies:
                    apply_goodies();
                    break;
                case sm::PauseAction::ApplySkin:
                    apply_skin();
                    break;
                default:
                    break;
                }
            } else if (!browser)
                flow.pulse(was_playing ? pulse & hig::Start : pulse);
            if (!was_playing && flow.level_active()) {
                wait_input_release = true;
                continue;
            }
            if (was_playing && flow.level_active() && gameplay_script.empty()) {
                level_ticks++;
                if (player_ready && !free_camera && gameplay_script.empty() && (focused || pilot) &&
                    !browser) {
                    const bool movement_owned = tick_player(pad, camera.yaw);
                    level_loop.tick(game);
                    if (level_loop.objects.aim_camera) // he aims a device (a BotFlinger): the camera stays behind where it faces
                        camera.yaw += std::remainder(level_loop.objects.aim_yaw - camera.yaw, XM_2PI) * .25f;
                    game.tick_weapons(movement_owned ? hig::PadState{} : pad, 1.f / 30);
                    if (play_style.uya_camera) uya_camera.tick(game, pad, 1.f / 30,
                        player_assets.ps2 && player_assets.ps2->charge_dashing());
                    audio.events(game.audio_events);
                }
            }
        }
        if (!movie_active && !level_loop.movie_request.empty()) {
            pending_level = level_loop.next_level;
            auto requested = std::move(level_loop.movie_request);
            level_loop.movie_request.clear();
            begin_movie(requested);
        }
        audio.pause(
            (!movie_active && (!flow.level_active() || level_loop.panel != sm::LevelPanel::None)) ||
            (!focused && !hidden) || browser || photo.active);
        if (movie_active) {
            if (movie_ready && (focused || hidden))
                movie_seconds = sound_output ? audio.movie_seconds() : movie_seconds + dt;
            double presentation =
                movie_frame >= 0
                    ? double(movie_frame) * movie.info().rate_divisor / movie.info().rate
                    : movie_seconds;
            if (movie.frame(presentation, movie_image)) {
                movie_picture = renderer.update_movie(movie_image, error);
                if (!movie_picture)
                    throw std::runtime_error(error);
                if (!movie_ready) {
                    movie_ready = true;
                    audio.movie(movie.soundtrack());
                }
            }
            auto failure = movie.error();
            if (!failure.empty()) {
                end_movie(true);
                if (!movie_path.empty())
                    throw std::runtime_error(failure);
                message = failure;
            } else if (movie_ready && movie_frame < 0 && movie_seconds >= movie.info().duration())
                end_movie(false);
            else if (movie_skip_after >= 0 && movie_seconds >= movie_skip_after)
                end_movie(true);
        }
        if(!level_loop.travel_request.empty() && !movie_active)pending_wad=std::move(level_loop.travel_request);
        if ((pending_level || !pending_wad.empty()) && !movie_active) {
            unsigned destination = pending_level;
            pending_level = 0;
            unsigned bolts = game.bolts, ammo = game.ammo;
            char wad[32];
            std::snprintf(wad, sizeof(wad), "/WAD/%02u.WAD", destination);
            if(pending_wad.empty())pending_wad=wad;
            std::string destination_wad=std::move(pending_wad);pending_wad.clear();
            if (!load(destination_wad) || !renderer.upload(scene, render_level, error))
                throw std::runtime_error(error);
            level = destination_wad;
            game.bolts = bolts;
            game.ammo = std::min(ammo, game.max_ammo);
            level_ticks = 0;
            std::printf("Level transition: %s\n", destination_wad.c_str());
            previous = std::chrono::steady_clock::now();
            input.clear();
            repeat.clear();
            wait_input_release = true;
        }
        if (flow.screen() != old_screen) {
            repeat.clear();
            if (flow.screen() == sm::Screen::Display) {
                pending = display;
                message.clear();
            }
        }
        float actor_alpha = gameplay_script.empty() && flow.level_active() && !movie_active &&
                                    level_loop.panel == sm::LevelPanel::None
                                ? float(clock.alpha())
                                : 1.f;
        if (photo.active) {
            actor_alpha = photo.frozen_alpha;
            if (!hidden)
                photo.scan(iso);
            photo.session.tick((focused || hidden) ? dt : 0);
            if (focused)
                camera.move(dt, hig::advance_pad({}, device.sample),
                            ImGui::IsMouseDown(1) && !io.WantCaptureMouse, !io.WantCaptureKeyboard, io);
            photo.draw(camera, runtime.disc(), player_assets, game);
        }
        if (!photo.active && flow.level_active() && !movie_active && !browser && focused && free_camera)
            camera.move(dt, pad, ImGui::IsMouseDown(1) && !io.WantCaptureMouse,
                        !io.WantCaptureKeyboard, io);
        if (!photo.active && player_ready && flow.level_visible() && !free_camera) {
            if (play_style.uya_camera) {
                if (!uya_camera.initialized()) uya_camera.reset(game, camera.yaw, camera.pitch);
                const auto pose = uya_camera.pose(actor_alpha);
                camera.eye = {pose.eye[0], pose.eye[1], pose.eye[2]};
                camera.yaw = pose.yaw;
                camera.pitch = pose.pitch;
                camera.fov = pose.fov;
            } else {
            camera.fov = 45;
            if (flow.level_active() && !movie_active && focused) {
                bool modal = level_loop.panel != sm::LevelPanel::None;
                if (!modal && ImGui::IsMouseDown(1) && !io.WantCaptureMouse) {
                    camera.yaw -= io.MouseDelta.x * .004f;
                    camera.pitch -= io.MouseDelta.y * .004f;
                }
                camera.yaw -= device.connected && !modal ? pad.axes[2] * dt * 2 * (pause.options.invert_x ? -1 : 1) : 0;
                camera.pitch -= device.connected && !modal ? pad.axes[3] * dt * 2 * (pause.options.invert_y ? -1 : 1) : 0;
            }
            if (pilot && flow.level_active() && !movie_active)
                pilot->steer_camera(camera.yaw, camera.pitch, game, dt);
            camera.pitch = std::clamp(camera.pitch, -.85f, .35f);
            auto p = game.interpolated(actor_alpha);
            hig::Vec3 target{p[0], p[1] + (game.swimming ? .2f : .8f), p[2]};
            XMFLOAT3 direction;
            XMStoreFloat3(&direction, camera.direction());
            hig::Vec3 wanted{target[0] - direction.x * 4.5f, target[1] - direction.y * 4.5f,
                             target[2] - direction.z * 4.5f};
            auto hit = game.terrain().ray(target, wanted);
            float fraction = hit.hit ? std::max(.1f, hit.fraction - .06f) : 1.f;
            camera.eye = {target[0] + (wanted[0] - target[0]) * fraction,
                          target[1] + (wanted[1] - target[1]) * fraction,
                          target[2] + (wanted[2] - target[2]) * fraction};
            }
            // On the Magneboots the camera takes his up for its own and sits 5 m behind him along the surface
            // (0x53000). NATIVE: its height over the surface (the pitch the player keeps) and its eased up.
            camera.framed = false;
            static hig::Vec3 eased{0, 1, 0};
            static bool was = false;
            if (!game.on_magneboots())
                was = false;
            else {
                const hig::Vec3 up = game.magne_up(), view = game.magne_view();
                if (!was)
                    eased = up;
                for (int k = 0; k < 3; k++)
                    eased[k] += (up[k] - eased[k]) * std::min(1.f, dt * 6);
                const float size = std::sqrt(eased[0] * eased[0] + eased[1] * eased[1] + eased[2] * eased[2]);
                if (size > 1e-4f)
                    for (float &v : eased)
                        v /= size;
                was = true;
                if (flow.level_active() && !movie_active && focused && device.connected && level_loop.panel == sm::LevelPanel::None)
                    game.magne_turn_view(-pad.axes[2] * dt * 2 * (pause.options.invert_x ? -1 : 1));
                const float pitch = std::clamp(camera.pitch, -.85f, .1f), c = std::cos(pitch), s = std::sin(pitch);
                auto p = game.interpolated(actor_alpha);
                const hig::Vec3 target{p[0] + eased[0] * .8f, p[1] + eased[1] * .8f, p[2] + eased[2] * .8f};
                const hig::Vec3 look{view[0] * c + eased[0] * s, view[1] * c + eased[1] * s, view[2] * c + eased[2] * s};
                const hig::Vec3 wanted{target[0] - look[0] * 5, target[1] - look[1] * 5, target[2] - look[2] * 5};
                auto hit = game.terrain().ray(target, wanted);
                const float fraction = hit.hit ? std::max(.1f, hit.fraction - .06f) : 1.f;
                camera.eye = {target[0] + (wanted[0] - target[0]) * fraction, target[1] + (wanted[1] - target[1]) * fraction,
                              target[2] + (wanted[2] - target[2]) * fraction};
                camera.framed = true;
                camera.look = {look[0], look[1], look[2]};
                camera.up = {eased[0], eased[1], eased[2]};
                camera.fov = 45;
                if (std::hypot(look[0], look[2]) > .3f) // the camera he has again when he steps off
                    camera.yaw = std::atan2(look[0], look[2]);
            }
        }
        if (!flow.level_visible()) {
            auto *draw = ImGui::GetBackgroundDrawList();
            // Retail image tiles; responsive PC layout until original HUD
            // placement, fonts and transitions have also been reconstructed.
            if (background.size() == 2) {
                // The 480-wide art occupies 256 + 224 columns. The last 32
                // columns of tile two are black padding in the original asset.
                float split = io.DisplaySize.x * (256.f / 480.f);
                draw->AddImage((ImTextureID)(intptr_t)background[0], ImVec2(0, 0),
                               ImVec2(split, io.DisplaySize.y));
                draw->AddImage((ImTextureID)(intptr_t)background[1], ImVec2(split, 0),
                               io.DisplaySize, ImVec2(0, 0), ImVec2(224.f / 256.f, 1));
            }
            draw->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 70));
        }
        // entering the pause menu opens it at its first page with the live settings
        if ((flow.screen() == sm::Screen::Pause) != pause_open) {
            pause_open = !pause_open;
            if (pause_open) {
                pause.open();
                pause.graphics = {display.width, display.height, display.fps, display.vsync};
                pause_state();
            }
        }
        if (!(pause.options == saved_pause_options)) {
            saved_pause_options = pause.options;
            if (!hidden) // a hidden run is a test: it leaves the player's saved choices alone
                sm::save_pause_options(pause_prefs, pause.options);
        }
        if (pause.options.music != applied_music || pause.options.effects != applied_effects) {
            applied_music = pause.options.music;
            applied_effects = pause.options.effects;
            music_volume = float(applied_music) / 10;
            effects_volume = float(applied_effects) / 10;
            audio.volumes(music_volume, effects_volume);
        }
        game.strafe_enabled = play_style.strafe && pause.options.strafe;
        if (player_assets.ps2_weapons) // the imported guns' own sounds follow the effects volume
            player_assets.ps2_weapons->set_volume(mute || hidden ? 0.f : effects_volume);
        auto before_ui = flow.screen();
        if (!movie_active && !photo.active && flow.screen() != sm::Screen::Pause)
            draw_frontend(flow, localization, renderer, display, pending, prefs, last_pad, message,
                          photo.enter_requested);
        if (flow.screen() != before_ui) {
            repeat.clear();
            if (flow.screen() == sm::Screen::Display) {
                pending = display;
                message.clear();
            }
        }
        if (!photo.active && player_ready && flow.level_visible() && !movie_active &&
            flow.screen() != sm::Screen::Pause)
            hud.draw(game, player_assets.ps2_weapons.get());
        if (!photo.active && !movie_active && flow.screen() == sm::Screen::Pause) {
            pause.settle(game);
            draw_pause(pause, game, localization, hud, last_pad);
        }
        if (!photo.active && player_ready && flow.level_active() && !movie_active)
            draw_level_loop(level_loop, game, last_pad);
        if (!photo.active && overlay && flow.screen() == sm::Screen::Level) {
            ImGui::SetNextWindowPos(ImVec2(20, io.DisplaySize.y - 150), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(.75f);
            ImGui::Begin("##level_status", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoInputs);
            ImGui::Text("%s | %s", runtime.world().level.name.c_str(),
                        player_ready ? game.animation_name(player_assets) : "Inspection");
            ImGui::TextUnformatted("WASD: move | Space: jump / double jump / hold to glide");
            ImGui::TextUnformatted("C + Space: high jump | C + direction + Space: long jump");
            if (game.swimming)
                ImGui::TextUnformatted(game.underwater ? "WASD: swim | F: dive | Space: ascend"
                                                      : "WASD: swim | F: dive | Space: jump out of water");
            ImGui::TextUnformatted("Mouse / E: fire | F: wrench | G: throw | Z / L2: strafe | F2: photo mode");
            if (player_ready)
                ImGui::Text("%s | shots %u | hits %u", player_assets.ps2_weapons && player_assets.ps2_weapons->enabled()
                              ? (player_assets.ps2_weapons->equipped() == 39 ? "UYA Shock Blaster" : "UYA Nitro Launcher")
                              : game.weapon ? "Lacerator" : "Blaster",
                            game.shots, game.impacts);
            ImGui::End();
        }
        if (!photo.active && overlay && !capture_ctl.hide_overlays()) {
            ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 440, 24), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(std::min(440.f, io.DisplaySize.x - 32), std::max(200.f, io.DisplaySize.y - 48)), ImGuiCond_FirstUseEver);
            ImGui::Begin("Size Matters options (F1)", &overlay);
            ImGui::TextDisabled("Executable updated: %s", executable_updated.c_str());
            #include "play_style_ui.inl"
            ImGui::Text("State: %s | %llu active level ticks", sm::screen_name(flow.screen()),
                        (unsigned long long)level_ticks);
            ImGui::Text("%.1f present FPS | %llu clock ticks at 30 Hz", fps,
                        (unsigned long long)clock.ticks());
            ImGui::Text("XInput: %s | buttons %08X",
                        device.connected ? "connected" : "disconnected", pad.raw.buttons);
            if (ImGui::SliderFloat("Music", &music_volume, 0, 1) |
                ImGui::SliderFloat("Effects", &effects_volume, 0, 1))
                audio.volumes(music_volume, effects_volume);
            ImGui::Text("Retail HUD | %u audio events", audio.event_count());
            if (player_ready)
                ImGui::Text("Enemies %zu | defeated %u | health %u | %s", game.enemies.size(),
                            game.defeated, game.health,
                            game.underwater ? "Underwater"
                            : game.swimming ? "Swimming"
                            : game.wading ? "Wading"
                                          : "On land");
            if (ImGui::Button("Inspect a level"))
                browser = !browser;
            if (browser) {
                if (ImGui::BeginCombo("Level WAD", selection.c_str())) {
                    for (const auto &file : runtime.disc().files())
                        if (file.path.starts_with("/WAD/") && file.path.ends_with(".WAD"))
                            if (ImGui::Selectable(file.path.c_str(), file.path == selection))
                                selection = file.path;
                    ImGui::EndCombo();
                }
                if (ImGui::Button("Load selected data")) {
                    if (load(selection) && renderer.upload(scene, render_level, error)) {
                        level = selection;
                        {
                            camera.yaw = game.heading;
                            camera.pitch = -.25f;
                        }
                        flow.inspect_level();
                        browser = false;
                        message.clear();
                        level_ticks = 0;
                        previous = std::chrono::steady_clock::now();
                        input.clear();
                        repeat.clear();
                    } else
                        message = error;
                }
            }
            if (flow.level_visible()) {
                if (ImGui::Checkbox("Free camera", &free_camera) && !free_camera)
                    uya_camera.reset(game, camera.yaw, camera.pitch);
                if (ImGui::Button("Frame terrain")) {
                    free_camera = true;
                    camera.frame(scene);
                }
                ImGui::SameLine();
                ImGui::Checkbox("Sky", &shell);
                ImGui::SameLine();
                ImGui::Checkbox("Ocean", &ocean);
                ImGui::Text("%zu triangles | %zu static placements", scene.indices.size() / 3,
                            scene.instances);
            }
            if (sm::extras_built && player_ready && ImGui::CollapsingHeader("Ratchet model")) {
                // PS2 Ratchet override (src/sm/ps2_ratchet.h): armour + rig + animation set from a PS2 disc.
                static const char *model_ids[] = {"sm", "uya", "gc", "rac1"};
                static const char *model_names[] = {"Size Matters default", "PS2 - Up Your Arsenal",
                                                    "PS2 - Going Commando", "PS2 - Ratchet & Clank"};
                int model_index = 0, anim_index = 1;
                for (int i = 0; i < 4; i++) {
                    if (ratchet_settings.model == model_ids[i])
                        model_index = i;
                    if (ratchet_settings.animations == model_ids[i])
                        anim_index = i;
                }
                if (ImGui::Combo("Model", &model_index, model_names, 4))
                    ratchet_settings.model = model_ids[model_index];
                if (ratchet_settings.active()) {
                    int choice = anim_index - 1;
                    if (ImGui::Combo("Animations", &choice, model_names + 1, 3))
                        ratchet_settings.animations = model_ids[choice + 1];
                    if (ratchet_settings.model != "rac1" &&
                        ImGui::InputInt("Armour", &ratchet_settings.armor))
                        ratchet_settings.armor = std::clamp(ratchet_settings.armor, 0, 15);
                    ImGui::Checkbox("Charge boots (double-tap R1 / Shift)", &ratchet_settings.charge_boots);
                }
                if (ImGui::Button("Apply")) {
                    apply_ratchet();
                    render_level.textures = runtime.world().level.textures;
                    render_level.textures.insert(render_level.textures.end(), player_assets.textures.begin(),
                                                 player_assets.textures.end());
                    if (!renderer.upload(scene, render_level, error))
                        message = error;
                    else if (ratchet_settings.active() && !player_assets.ps2)
                        message = "PS2 Ratchet unavailable (see the console)";
                    else
                        message.clear();
                }
                ImGui::SameLine();
                if (ImGui::Button("Save as default")) {
                    std::string why;
                    message = sm::save_ps2_ratchet_settings(sm::ps2_ratchet_settings_path(), ratchet_settings, why)
                                  ? "Saved " + sm::ps2_ratchet_settings_path().string()
                                  : why;
                }
                if (player_assets.ps2) {
                    ImGui::TextWrapped("%s | %s", player_assets.ps2->description().c_str(),
                                       player_assets.ps2->state_name().c_str());
                    ImGui::TextWrapped("Hoverboots: B / L3 on-off, Shift / R1 thrust, Ctrl / R2 brake");
                }
            }
            if (!message.empty())
                ImGui::TextWrapped("%s", message.c_str());
            if (ImGui::Button("Exit"))
                alive = false;
            ImGui::End();
        }
        // Exercise real swapchain recreation without modifying saved settings.
        if (cycle && (frames == 20 || frames == 40 || frames == 60 || frames == 80)) {
            display.width = frames == 40 ? 3840 : 1920;
            display.height = frames == 40 ? 2160 : 1080;
            display.fps = frames == 60 ? 60 : 120;
            if (!renderer.resize(display.width, display.height, error))
                throw std::runtime_error(error);
            std::printf("Display audit: frame %u -> %ux%u @ %u\n", frames, display.width,
                        display.height, display.fps);
        }
        if (player_ready && flow.level_visible()) {
            game.render(player_assets, actor_alpha, actors,
                        !photo.active || photo.session.show_player,
                        !photo.active || photo.session.show_enemies);
            level_loop.render(player_assets.encounters, actor_alpha, actors);
            level_loop.objects.render(player_assets, actor_alpha, {camera.eye.x, camera.eye.y, camera.eye.z}, actors);
            game.render_effects(player_assets,actor_alpha,{camera.eye.x,camera.eye.y,camera.eye.z},
                                actors,!photo.active || photo.session.show_player);
            if (player_assets.ps2 && (!photo.active || photo.session.show_player))
                player_assets.ps2->render_effects(game, actor_alpha,
                    {camera.eye.x,camera.eye.y,camera.eye.z}, actors);
            if (photo.active)
                photo.session.render(player_assets, actors, int(player_assets.textures.size()));
            for (auto &batch : actors.batches)
                if (batch.texture >= 0)
                    batch.texture += int(runtime.world().level.textures.size());
        } else
            actors = {};
        if (photo.textures_dirty) {
            render_level.textures = runtime.world().level.textures;
            render_level.textures.insert(render_level.textures.end(), player_assets.textures.begin(), player_assets.textures.end());
            photo.session.append_textures(render_level.textures);
            if (!renderer.upload(scene, render_level, error))
                throw std::runtime_error(error);
            photo.textures_dirty = false;
            previous = std::chrono::steady_clock::now();
        }
        if (!renderer.update_actors(actors, error))
            throw std::runtime_error(error);
        if (!photo.active && flow.level_active() && !movie_active && level_loop.panel == sm::LevelPanel::None)
            environment_seconds =
                (float(level_ticks) + (gameplay_script.empty() ? float(clock.alpha()) : 0.f)) /
                30.f;
        sm::SceneView view{{camera.eye.x, camera.eye.y, camera.eye.z},
                           player_ready ? game.interpolated(actor_alpha)
                                        : hig::Vec3{camera.eye.x, camera.eye.y, camera.eye.z},
                           environment_seconds,
                           shell,
                           ocean};
        view.render_options = play_style.rendering;
        camera_submerged = player_ready && flow.level_visible() && !movie_active &&
            game.camera_underwater(view.eye, camera_submerged);
        view.underwater = camera_submerged;
        unsigned render_width = display.width, render_height = display.height;
        if (photo.active && photo.capture_requested && photo.resolution) {
            render_width = photo.resolution == 2 ? 3840 : 1920;
            render_height = photo.resolution == 2 ? 2160 : 1080;
            if (!renderer.resize(render_width, render_height, error))
                throw std::runtime_error(error);
        }
        // The projection takes the shape the picture is shown in (the window's client area, should the user have
        // pulled it to another shape), so nothing is drawn stretched; a photograph keeps its own resolution's.
        const bool photograph = render_width != display.width || render_height != display.height;
        if (!renderer.draw(camera.matrix(photograph ? render_width : shape_width, photograph ? render_height : shape_height),
                           view, error))
            throw std::runtime_error(error);
        if (photo.active && photo.capture_requested) {
            std::filesystem::path path = photo_shot;
            if (path.empty()) {
                auto directory = std::filesystem::current_path() / "screenshots" / "SizeMatters";
                std::filesystem::create_directories(directory);
                auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
                path = directory / ("Photo_" + std::to_string(stamp) + ".bmp");
            }
            if (renderer.capture(path, error)) {
                photo.message = "Saved " + path.string();
                std::printf("Photo saved: %s (%u x %u)\n", path.string().c_str(), render_width, render_height);
            } else {
                photo.message = error;
                if (!photo_shot.empty())
                    throw std::runtime_error(error);
            }
            photo_shot.clear();
            photo.capture_requested = false;
            if (render_width != display.width || render_height != display.height) {
                if (!renderer.resize(display.width, display.height, error) ||
                    !renderer.draw(camera.matrix(shape_width, shape_height), view, error))
                    throw std::runtime_error(error);
            }
        }
        if (movie_active) {
            auto *draw = ImGui::GetForegroundDrawList();
            draw->AddRectFilled({0, 0}, io.DisplaySize, IM_COL32(0, 0, 0, 255));
            if (movie_picture) {
                // PS2 512x448 storage is displayed in its original 4:3 screen aspect.
                float w = std::min(io.DisplaySize.x, io.DisplaySize.y * 4.f / 3.f),
                      h = w * 3.f / 4.f;
                ImVec2 at{(io.DisplaySize.x - w) / 2, (io.DisplaySize.y - h) / 2};
                draw->AddImage((ImTextureID)(intptr_t)movie_picture, at, {at.x + w, at.y + h});
            } else
                draw->AddText({30, 30}, IM_COL32_WHITE, "Loading original movie...");
            draw->AddText({24, io.DisplaySize.y - 30}, IM_COL32_WHITE,
                          "Esc / Enter / A / B: skip cutscene");
        }
        ImGui::Render();
        scale_ui(ImGui::GetDrawData(), display.width, display.height);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if (!movie_active ||
            (movie_ready && (movie_frame < 0 || movie.displayed_frame() >= movie_frame)))
            frames++;
        if (frame_limit && frames == frame_limit && !capture.empty() &&
            !renderer.capture(capture, error))
            throw std::runtime_error(error);
        capture_ctl.end_frame((ID3D11Device *)renderer.device(), (ID3D11DeviceContext *)renderer.context(),
                              (ID3D11Texture2D *)renderer.back_buffer()); // sm_pc runs in real time, so the wall clock is exact
        if (!renderer.present(display.vsync, error))
            throw std::runtime_error(error);
        if (frame_limit && frames >= frame_limit)
            alive = false;
        if (IsIconic(window)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        std::this_thread::sleep_until(now + std::chrono::nanoseconds(1000000000ull / display.fps));
    }
    timeEndPeriod(1);
    if (pilot) {
        const auto &ps = pilot->stats();
        std::printf("Pilot: %s | grid %zu nodes, travelled %.0f m, engagements %u, fire frames %u, wrench %u, dodges %u, stuck %u, routes %u\n",
                    pilot->status().c_str(), ps.nav_nodes, double(ps.travelled), ps.engagements, ps.fire_frames, ps.melee, ps.dodges, ps.stuck, ps.routes);
    }
    if (capture_ctl.recording()) {
        const auto saved = capture_ctl.stop((ID3D11DeviceContext *)renderer.context());
        std::printf("Recording: %s\n", capture_ctl.last_message().c_str());
        if (!saved.error.empty())
            throw std::runtime_error("Recording failed: " + saved.error);
    } else if (!capture_ctl.last_message().empty())
        std::printf("Capture: %s\n", capture_ctl.last_message().c_str());
    size_t errors = renderer.validation_errors();
    auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("Native host: %u frames in %.3fs, %llu simulation-clock ticks, "
                "%llu ns catch-up "
                "discarded; %zu DX11 errors\n",
                frames, seconds, (unsigned long long)clock.ticks(),
                (unsigned long long)clock.dropped_nanoseconds(), errors);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (IsWindow(window))
        DestroyWindow(window);
    return errors ? 1 : 0;
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        return run(argc, argv);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "Native engine: %s\n", e.what());
        return 1;
    }
}
