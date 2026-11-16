// The hero's saved look and the choices of the "Ratchet model" window: where they are kept, reading and saving
// them. Apart from ps2_ratchet.cpp because a build without the borrowed play options (extras.h) still keeps which
// of Size Matters' own skins he wears.
#include "ps2_ratchet.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdlib>
#include <fstream>

namespace sm {

// ---- settings -------------------------------------------------------------------------------------------
std::filesystem::path ps2_ratchet_settings_path() {
    wchar_t dir[32768];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", dir, 32768);
    return n && n < 32768 ? std::filesystem::path(dir) / L"Theia" / L"SizeMatters" / L"ratchet.ini" : std::filesystem::path(L"sm-ratchet.ini");
}

Ps2RatchetSettings read_ps2_ratchet_settings(const std::filesystem::path &path) {
    Ps2RatchetSettings s;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "model") s.model = v;
        else if (k == "animations") s.animations = v;
        else if (k == "armor") s.armor = std::atoi(v.c_str());
        else if (k == "iso_uya") s.iso_uya = v;
        else if (k == "iso_gc") s.iso_gc = v;
        else if (k == "iso_rac1") s.iso_rac1 = v;
        else if (k == "hoverboots") s.hoverboots = v;
        else if (k == "charge_boots") s.charge_boots = v != "0";
        else if (k == "skin") s.skin = std::atoi(v.c_str());
        else if (k == "own_controller") s.own_controller = v != "0";
    }
    auto valid = [](const std::string &g) { return g == "uya" || g == "gc" || g == "rac1"; };
    if (s.model != "sm" && !valid(s.model)) s.model = "sm";
    if (!valid(s.animations) && s.animations != "sm") s.animations = "uya";
    if (s.armor < 0 || s.armor > 15) s.armor = 0;
    if (s.skin < 0 || s.skin >= int(hero_skin_count)) s.skin = 0;
    return s;
}

bool save_ps2_ratchet_settings(const std::filesystem::path &path, const Ps2RatchetSettings &s, std::string &error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto tmp = path.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        f << "model=" << s.model << "\nanimations=" << s.animations << "\narmor=" << s.armor << "\niso_uya=" << s.iso_uya << "\niso_gc=" << s.iso_gc
          << "\niso_rac1=" << s.iso_rac1 << "\nhoverboots=" << s.hoverboots << "\ncharge_boots=" << (s.charge_boots ? 1 : 0) << "\nskin=" << s.skin << "\nown_controller=" << (s.own_controller ? 1 : 0) << "\n";
        if (!f) return error = "cannot write " + tmp, false;
    }
    if (!MoveFileExW(std::filesystem::path(tmp).wstring().c_str(), path.wstring().c_str(), MOVEFILE_REPLACE_EXISTING))
        return error = "cannot replace " + path.string(), false;
    return true;
}

} // namespace sm
