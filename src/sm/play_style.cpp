#include "play_style.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>

namespace sm {
std::filesystem::path play_style_settings_path() {
    wchar_t directory[32768]{};
    const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", directory, 32768);
    return count && count < 32768 ? std::filesystem::path(directory) / L"Theia" / L"SizeMatters" / L"play-style.ini"
                                  : std::filesystem::path(L"sm-play-style.ini");
}

PlayStyleSettings read_play_style(const std::filesystem::path& path) {
    PlayStyleSettings result;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        const auto key = line.substr(0, separator), value = line.substr(separator + 1);
        auto boolean = [&](bool& target) { if (value == "0" || value == "1") target = value == "1"; };
        auto number = [&](float& target) {
            char* end = nullptr;
            const float parsed = std::strtof(value.c_str(), &end);
            if (end != value.c_str() && *end == '\0' && std::isfinite(parsed)) target = parsed;
        };
        auto integer = [&](int& target) {
            int parsed = 0;
            const auto conversion = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (conversion.ec == std::errc{} && conversion.ptr == value.data() + value.size()) target = parsed;
        };
        auto& r = result.rendering;
        if (key == "uya_camera") boolean(result.uya_camera);
        else if (key == "strafe") boolean(result.strafe);
        else if (key == "uya_weapons") boolean(result.uya_weapons);
        else if (key == "uya_weapon") integer(result.uya_weapon);
        else if (key == "uya_wrench") integer(result.uya_wrench);
        else if (key == "thruster_pack") boolean(result.thruster_pack);
        else if (key == "hydro_pack") boolean(result.hydro_pack);
        else if (key == "color_scale") number(r.color_scale);
        else if (key == "vertex_color_strength") number(r.vertex_color_strength);
        else if (key == "saturation") number(r.saturation);
        else if (key == "contrast") number(r.contrast);
        else if (key == "gamma") number(r.gamma);
        else if (key == "mip_bias") number(r.mip_bias);
        else if (key == "texture_filter") { int filter = int(r.texture_filter); integer(filter); if (filter >= 0 && filter <= 2) r.texture_filter = TextureFilter(filter); }
        else if (key == "smooth_effects") boolean(r.smooth_effects);
        else if (key == "fog") boolean(r.fog);
        else if (key == "fog_r") number(r.fog_color[0]);
        else if (key == "fog_g") number(r.fog_color[1]);
        else if (key == "fog_b") number(r.fog_color[2]);
        else if (key == "fog_near") number(r.fog_near);
        else if (key == "fog_far") number(r.fog_far);
        else if (key == "fog_strength") number(r.fog_strength);
    }
    if (result.uya_weapon != 39 && result.uya_weapon != 119) result.uya_weapon = 39;
    if (result.uya_wrench != 118 && result.uya_wrench != 122 && result.uya_wrench != 126) result.uya_wrench = 0;
    result.rendering = sanitize_render_options(result.rendering);
    return result;
}

bool save_play_style(const std::filesystem::path& path, const PlayStyleSettings& settings, std::string& error) {
    const auto r = sanitize_render_options(settings.rendering);
    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) { error = "Cannot create play-style preferences directory: " + ec.message(); return false; }
    auto temporary = path;
    temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId());
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << std::setprecision(9)
             << "version=1\nuya_camera=" << settings.uya_camera << "\nstrafe=" << settings.strafe
             << "\nuya_weapons=" << settings.uya_weapons << "\nuya_weapon=" << settings.uya_weapon << "\nuya_wrench=" << settings.uya_wrench
             << "\nthruster_pack=" << settings.thruster_pack << "\nhydro_pack=" << settings.hydro_pack
             << "\ncolor_scale=" << r.color_scale << "\nvertex_color_strength=" << r.vertex_color_strength
             << "\nsaturation=" << r.saturation << "\ncontrast=" << r.contrast << "\ngamma=" << r.gamma
             << "\nmip_bias=" << r.mip_bias << "\ntexture_filter=" << int(r.texture_filter)
             << "\nsmooth_effects=" << r.smooth_effects << "\nfog=" << r.fog
             << "\nfog_r=" << r.fog_color[0] << "\nfog_g=" << r.fog_color[1] << "\nfog_b=" << r.fog_color[2]
             << "\nfog_near=" << r.fog_near << "\nfog_far=" << r.fog_far << "\nfog_strength=" << r.fog_strength << '\n';
        file.flush();
        if (!file) { error = "Cannot write play-style preferences"; return false; }
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, ec);
        error = "Cannot publish play-style preferences";
        return false;
    }
    error.clear();
    return true;
}
}
