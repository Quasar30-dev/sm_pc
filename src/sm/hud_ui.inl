struct RetailHud {
    hig::HudAssets assets;
    void *font = nullptr;
    std::vector<void *> icons;
    int debug_screen = -1; // --hud-screen N: draw this screen of MAINHUD.W3D alone (layout research)
    bool upload(sm::RenderDx11 &r, std::string &error) {
        font = r.upload_picture(assets.font, error);
        if (!font)
            return false;
        for (const auto &icon : assets.icons) {
            auto *v = r.upload_picture(icon, error);
            if (!v)
                return false;
            icons.push_back(v);
        }
        return true;
    }
    void draw(const sm::Gameplay &game, const sm::Ps2Weapons* weapons = nullptr) const {
        auto *draw = ImGui::GetBackgroundDrawList();
        auto size = ImGui::GetIO().DisplaySize;
        float scale = std::min(size.x / 480.f, size.y / 272.f),
              origin = (size.x - 480 * scale) * .5f;
        auto point = [&](float x, float y) { return ImVec2(origin + x * scale, y * scale); };
        auto screen = [&](unsigned index) {
            for (unsigned id : assets.layout.screens.at(index)) {
                if (id == 1179)
                    continue; // weapon XP fill is independently clipped by retail
                std::vector<hig::HudVertex> vertices;
                hig::hud_triangles(assets.layout, id, vertices);
                for (size_t i = 0; i + 2 < vertices.size(); i += 3) {
                    draw->PrimReserve(3, 3);
                    auto base = draw->_VtxCurrentIdx;
                    for (unsigned j = 0; j < 3; j++)
                        draw->PrimWriteIdx(ImDrawIdx(base + j));
                    for (unsigned j = 0; j < 3; j++) {
                        const auto &v = vertices[i + j];
                        draw->PrimWriteVtx(point(v.x, v.y), ImGui::GetIO().Fonts->TexUvWhitePixel,
                                           v.rgba);
                    }
                }
            }
        };
        auto text = [&](std::string value, float x, float y, bool right = false) {
            constexpr float s = .75f;
            if (right)
                for (unsigned char c : value)
                    x -= (assets.metrics[c][2] + 1) * s;
            for (unsigned char c : value) {
                float w = assets.metrics[c][2], off = int8_t(assets.metrics[c][3]);
                float u = float((c << 4) & 255), v = float(c & 240);
                draw->AddImage((ImTextureID)(intptr_t)font, point(x, y + (off - 15) * s),
                               point(x + w * s, y + off * s), ImVec2(u / 256, v / 256),
                               ImVec2((u + w) / 256, (v + 15) / 256));
                x += (w + 1) * s;
            }
        };
        auto icon = [&](unsigned i, float x, float y, float w, float h) {
            // b0994..b09f8 installs PAK entry zero at HUD icon slot one.
            if (i && i <= icons.size())
                draw->AddImage((ImTextureID)(intptr_t)icons[i - 1], point(x - w / 2, y - h / 2),
                               point(x + w / 2, y + h / 2));
        };
        if (debug_screen >= 0) {
            if (size_t(debug_screen) < assets.layout.screens.size())
                screen(unsigned(debug_screen));
            return;
        }
        screen(7);
        screen(17);
        screen(15);
        icon(7, 458, 18, 28, 22);
        const bool uya = weapons && weapons->enabled();
        if (!uya) icon(game.equipped_tier() ? game.equipped_tier()->icon : assets.weapon_icon, 22, 18, 22, 22);
        text(std::to_string(game.bolts), 432, 16, true);
        text(std::to_string(uya ? weapons->ammo() : int(game.ammo)) + "/" +
             std::to_string(uya ? weapons->max_ammo() : int(game.max_ammo)), 52, 16);
        if (uya) text(weapons->equipped() == 39 ? "SHOCK" : "NITRO", 12, 39);
        // b8020: original nanotech/XP bounds and RGBA gradients.
        auto bar = [&](unsigned id, float fraction, unsigned segments, ImU32 top, ImU32 bottom) {
            const auto &b = assets.layout.elements.at(id).bounds;
            draw->AddRectFilled(point(b[0] - 1, b[1] - 1), point(b[2] + 1, b[3] + 1), 0xa0202020);
            draw->AddRectFilled(point(b[0], b[1]), point(b[2], b[3]), 0xff000000);
            for (unsigned i = 0; i < segments; i++) {
                float a = float(i) / segments,
                      z = std::min(float(i + 1) / segments, std::clamp(fraction, 0.f, 1.f));
                if (z <= a)
                    break;
                float x = b[0] + (b[2] - b[0]) * a, end = b[0] + (b[2] - b[0]) * z;
                draw->AddRectFilledMultiColor(point(x, b[1]), point(std::max(x, end - 1), b[3]),
                                              top, top, bottom, bottom);
            }
        };
        // LEVEL_01.REL b826c..b82c4 constructs these RGBA words:
        // D0 A5 09 FF (upper edge), FF 35 1C FF (lower edge).
        // b82c8 submits one continuous gradient, without segment dividers.
        bar(72, float(game.health) / std::max(1u, game.max_health), 1, 0xff09a5d0, 0xff1c35ff);
        bar(71, 0, 1, 0xffffff80, 0xff90d000);
        auto health = std::to_string(game.health) + "/" + std::to_string(game.max_health);
        float width = 0;
        for (unsigned char c : health)
            width += (assets.metrics[c][2] + 1) * .75f;
        const auto &hb = assets.layout.elements.at(73).bounds;
        text(health, (hb[0] + hb[2] - width) * .5f, 18);
    }
};
