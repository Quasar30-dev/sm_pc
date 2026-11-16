#include "platform_win32.h"
#include <Windows.h>
#include <Xinput.h>
#include <fstream>
#include <sstream>
namespace sm {
bool valid_display(const DisplaySettings &s) {
    return s.width >= 320 && s.width <= 7680 && s.height >= 200 && s.height <= 4320 &&
           (s.fps == 30 || s.fps == 60 || s.fps == 120 || s.fps == 144 || s.fps == 240);
}
std::filesystem::path display_settings_path() {
    wchar_t dir[32768];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", dir, 32768);
    return n && n < 32768 ? std::filesystem::path(dir) / L"Theia" / L"SizeMatters" / L"display.ini"
                          : std::filesystem::path(L"sm-display.ini");
}
DisplaySettings read_display(const std::filesystem::path &path) {
    DisplaySettings s;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream in(line);
        std::string key;
        unsigned value;
        if (!std::getline(in, key, '=') || !(in >> value))
            continue;
        if (key == "width")
            s.width = value;
        else if (key == "height")
            s.height = value;
        else if (key == "fps")
            s.fps = value;
        else if (key == "vsync" && value <= 1)
            s.vsync = value != 0;
    }
    return valid_display(s) ? s : DisplaySettings{};
}
bool save_display(const std::filesystem::path &path, const DisplaySettings &s, std::string &error) {
    if (!valid_display(s)) {
        error = "Invalid PC display settings";
        return false;
    }
    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "Cannot create display settings directory";
        return false;
    }
    auto temp = path;
    temp += L".tmp-" + std::to_wstring(GetCurrentProcessId());
    {
        std::ofstream f(temp, std::ios::binary | std::ios::trunc);
        f << "version=1\nwidth=" << s.width << "\nheight=" << s.height << "\nfps=" << s.fps
          << "\nvsync=" << unsigned(s.vsync) << '\n';
        f.flush();
        if (!f) {
            error = "Cannot write display settings";
            return false;
        }
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temp, ec);
        error = "Cannot publish display settings";
        return false;
    }
    error.clear();
    return true;
}
hig::PadSample convert_xinput(uint16_t buttons, int16_t lx, int16_t ly, int16_t rx, int16_t ry,
                              uint8_t lt, uint8_t rt) {
    hig::PadSample sample;
    const std::pair<uint16_t, uint32_t> map[] = {
        {XINPUT_GAMEPAD_BACK, hig::Select},      {XINPUT_GAMEPAD_START, hig::Start},
        {XINPUT_GAMEPAD_DPAD_UP, hig::Up},       {XINPUT_GAMEPAD_DPAD_RIGHT, hig::Right},
        {XINPUT_GAMEPAD_DPAD_DOWN, hig::Down},   {XINPUT_GAMEPAD_DPAD_LEFT, hig::Left},
        {XINPUT_GAMEPAD_LEFT_SHOULDER, hig::L1}, {XINPUT_GAMEPAD_RIGHT_SHOULDER, hig::R1},
        {XINPUT_GAMEPAD_A, hig::Cross},          {XINPUT_GAMEPAD_B, hig::Circle},
        {XINPUT_GAMEPAD_X, hig::Square},         {XINPUT_GAMEPAD_Y, hig::Triangle},
        {XINPUT_GAMEPAD_LEFT_THUMB, hig::L3},    {XINPUT_GAMEPAD_RIGHT_THUMB, hig::R3}};
    for (auto [source, target] : map)
        if (buttons & source)
            sample.buttons |= target;
    // PC mapping policy for analogue triggers to the PS2 digital buttons.
    if (lt > 30)
        sample.buttons |= hig::L2;
    if (rt > 30)
        sample.buttons |= hig::R2;
    auto byte = [](int value) { return uint8_t(((value + 32768) * 255 + 32767) / 65535); };
    sample.sticks = {byte(lx), byte(-1 - int(ly)), byte(rx), byte(-1 - int(ry))};
    return sample;
}
DevicePad poll_controller(bool focused) {
    DevicePad result;
    if (!focused)
        return result;
    for (DWORD i = 0; i < XUSER_MAX_COUNT; i++) {
        XINPUT_STATE state{};
        if (XInputGetState(i, &state) != ERROR_SUCCESS)
            continue;
        const auto &g = state.Gamepad;
        result.connected = true;
        result.index = i;
        result.sample = convert_xinput(g.wButtons, g.sThumbLX, g.sThumbLY, g.sThumbRX, g.sThumbRY,
                                       g.bLeftTrigger, g.bRightTrigger);
        break;
    }
    return result;
}
} // namespace sm
