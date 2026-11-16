#pragma once
#include "render_options.h"
#include <filesystem>
#include <string>

namespace sm {
// Optional PC play preferences. Original Size Matters presentation remains the
// default; these settings do not serialize campaign state or weapon ammunition.
struct PlayStyleSettings {
    bool uya_camera = false;
    bool strafe = true;
    bool uya_weapons = false;
    int uya_weapon = 39;
    // The wrench model in SM's own wrench moves: 0 Size Matters', or one of UYA's OmniWrenches by its entry of the
    // UYA disc's ARMOR global (118 silver, 122 blue, 126 dark / orange).
    int uya_wrench = 0;
    bool thruster_pack = false;
    bool hydro_pack = true;
    RenderOptions rendering;
};
std::filesystem::path play_style_settings_path();
PlayStyleSettings read_play_style(const std::filesystem::path&);
bool save_play_style(const std::filesystem::path&, const PlayStyleSettings&, std::string& error);
}
