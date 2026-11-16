#pragma once
#include "hig/higinput.h"
#include <filesystem>
#include <string>
namespace sm {
struct DisplaySettings {
    unsigned width = 1920, height = 1080, fps = 60;
    bool vsync = false;
};
bool valid_display(const DisplaySettings &);
std::filesystem::path display_settings_path();
DisplaySettings read_display(const std::filesystem::path &);
bool save_display(const std::filesystem::path &, const DisplaySettings &, std::string &error);
// The XInput adapter supplies the retail 8-bit input range. It does not add
// XInput's default stick dead zones on top of the recovered HIG transfer.
hig::PadSample convert_xinput(uint16_t buttons, int16_t lx, int16_t ly, int16_t rx, int16_t ry,
                              uint8_t lt, uint8_t rt);
struct DevicePad {
    bool connected = false;
    unsigned index = 0;
    hig::PadSample sample;
};
DevicePad poll_controller(bool focused);
} // namespace sm
