// The pause menu's presentation, in the original's own shapes: the button plates, selected fills, value boxes, title
// bar and hint bar of /HUD/MAINHUD.W3D, placed where the game's menu tables put them, with the retail font.
//
// The HUD's space is 480 x 272, but the settings pages reach above and below it (their title bar is centred on
// y = -11, their hint bar on 274 or 282): the PS2's 4:3 frame shows that much more of it. The layout here spans
// y -27..299 and is scaled uniformly to fit the window and centred, so every page keeps the same scale and nothing
// is stretched or cut at any resolution or aspect ratio. Included by main.cpp after hud_ui.inl.
void draw_pause(const sm::PauseMenu &menu, const sm::Gameplay &game, const hig::Localization &text,
                const RetailHud &hud, bool controller) {
    auto *draw = ImGui::GetBackgroundDrawList();
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    constexpr float canvas_top = -27, canvas_height = 326;
    const float scale = std::min(size.x / 480.f, size.y / canvas_height);
    const float ox = (size.x - 480 * scale) * .5f, oy = (size.y - canvas_height * scale) * .5f - canvas_top * scale;
    auto at = [&](float x, float y) { return ImVec2(ox + x * scale, oy + y * scale); };
    // the menu's two text colours (LEVEL_02 0x215a30: d0 a5 09 and white) and a grey for what is not built yet
    const ImU32 gold = IM_COL32(0xd0, 0xa5, 0x09, 255), white = IM_COL32(255, 255, 255, 255),
                grey = IM_COL32(140, 150, 158, 170);
    draw->AddRectFilled({0, 0}, size, IM_COL32(0, 0, 0, 150));
    const hig::HudLayout *layout = menu.layout();
    // HUD shapes
    auto triangles = [&](const std::vector<hig::HudVertex> &vertices, float dx, float dy) {
        for (size_t i = 0; i + 2 < vertices.size(); i += 3) {
            draw->PrimReserve(3, 3);
            const auto base = draw->_VtxCurrentIdx;
            for (unsigned j = 0; j < 3; j++)
                draw->PrimWriteIdx(ImDrawIdx(base + j));
            for (unsigned j = 0; j < 3; j++) {
                const auto &v = vertices[i + j];
                draw->PrimWriteVtx(at(v.x + dx, v.y + dy), ImGui::GetIO().Fonts->TexUvWhitePixel, v.rgba);
            }
        }
    };
    auto shape = [&](const sm::PauseShape &s) {
        if (!layout)
            return;
        std::vector<hig::HudVertex> vertices;
        if (s.element && hig::hud_triangles(*layout, s.element, vertices))
            triangles(vertices, 0, 0);
        else if (s.tmpl && hig::hud_template(*layout, s.tmpl, vertices))
            triangles(vertices, s.x, s.y);
    };
    // text in the retail font; `k` scales the 15-pixel glyph cell; y is the top of the line
    auto width = [&](std::string_view s, float k) {
        float w = 0;
        for (unsigned char c : s)
            w += (hud.assets.metrics[c][2] + 1) * k;
        return w;
    };
    // The glyphs sit low in their 15-pixel cell: about 3.4 pixels of a cell's top are empty for capitals, so a
    // line is raised by that much to centre the letters, not the cell, in its place.
    auto put = [&](std::string_view s, float x, float y, float k, ImU32 color) {
        y -= 3.4f * k;
        for (unsigned char c : s) {
            const float w = hud.assets.metrics[c][2], off = int8_t(hud.assets.metrics[c][3]);
            const float u = float((c << 4) & 255), v = float(c & 240);
            draw->AddImage((ImTextureID)(intptr_t)hud.font, at(x, y + off * k), at(x + w * k, y + (off + 15) * k),
                           ImVec2(u / 256, v / 256), ImVec2((u + w) / 256, (v + 15) / 256), color);
            x += (w + 1) * k;
        }
    };
    // centred in a box, narrowed when it is wider than the box
    auto boxed = [&](std::string_view s, const std::array<float, 4> &box, float k, ImU32 color) {
        const float room = box[2] - box[0] - 4, w = width(s, k);
        if (w > room && w > 0)
            k *= room / w;
        put(s, (box[0] + box[2] - width(s, k)) * .5f, (box[1] + box[3] - 15 * k) * .5f, k, color);
    };
    // Text in a box, wrapped at whole words, from its line `skip` on; returns the lines the whole text makes.
    auto wrapped = [&](const std::string &body, float x0, float y0, float x1, float y1, float k, int skip = 0) {
        const float line_height = 15 * k + 2;
        float y = y0;
        int line = 0;
        size_t start = 0;
        while (start <= body.size()) {
            size_t end = body.find('\n', start);
            if (end == std::string::npos)
                end = body.size();
            std::string_view paragraph(body.data() + start, end - start);
            if (paragraph.empty() && line >= skip)
                y += line_height * .5f;
            while (!paragraph.empty()) {
                size_t take = paragraph.size();
                if (width(paragraph, k) > x1 - x0) { // the longest run of whole words that fits
                    take = 0;
                    while (true) {
                        const size_t space = paragraph.find(' ', take + 1);
                        const size_t candidate = space == std::string_view::npos ? paragraph.size() : space;
                        if (width(paragraph.substr(0, candidate), k) > x1 - x0)
                            break;
                        take = candidate;
                        if (space == std::string_view::npos)
                            break;
                    }
                    if (!take) // one word wider than the panel: let it run
                        take = paragraph.find(' ') == std::string_view::npos ? paragraph.size() : paragraph.find(' ');
                }
                if (line >= skip) {
                    if (y + line_height <= y1)
                        put(paragraph.substr(0, take), x0, y, k, white);
                    y += line_height;
                }
                line++;
                paragraph.remove_prefix(std::min(paragraph.size(), take + (take < paragraph.size() ? 1 : 0)));
            }
            start = end + 1;
        }
        return line;
    };
    const auto rows = menu.rows(game, text);
    const std::string title = menu.title(text), note = menu.detail(game, text);
    const bool settings = menu.settings_page(), split = menu.has_detail();
    // the settings pages' title bar and hint bar (templates 769 and 1323; the camera page keeps its own heights)
    const float title_y = menu.page() == sm::PausePage::CameraControls ? -4.f : -11.f;
    const float hints_y = menu.page() == sm::PausePage::CameraControls ? 282.f : 274.f;
    if (settings || split) {
        shape({0, 769, 240, title_y});
        boxed(title, {13, title_y - 6, 469, title_y + 6}, .9f, white);
        shape({0, 1323, 240, hints_y});
        const std::string back = controller ? "B  Back" : "Esc  Back";
        const std::string act = split ? (menu.page() == sm::PausePage::Weapons ? (controller ? "A  Equip" : "Enter  Equip") : "")
                                      : (controller ? "A  Toggle" : "Enter  Toggle");
        boxed(back, {12, hints_y - 6, 100, hints_y + 6}, .6f, white);
        if (!act.empty())
            boxed(act, {380, hints_y - 6, 468, hints_y + 6}, .6f, white);
        if (settings)
            boxed(controller ? "D-pad  Select and change" : "Arrows  Select and change", {105, hints_y - 6, 375, hints_y + 6},
                  .6f, white);
    }
    if (menu.panel_page() && layout && layout->elements.size() > 1391) {
        // CHEATS (HUD screen 45): a list panel over a panel of text. SKINS (screen 65): three panels beside a tall
        // one. Both under the title bar at y 19 and over the hint bar at y 259. What stands in the panels is ours:
        // the original's cheats are not rebuilt, and its skins page shows the hero where the list is here.
        const bool skins = menu.page() == sm::PausePage::Skins;
        static constexpr unsigned cheat_screen[] = {982, 983, 1329, 1330}, skin_screen[] = {1080, 1081, 1388, 1389, 1390, 1391};
        for (unsigned element : skins ? std::span<const unsigned>(skin_screen) : std::span<const unsigned>(cheat_screen))
            shape({element, 0, 0, 0});
        boxed(title, {13, 13, 469, 25}, .9f, white);
        boxed(controller ? "B  Back" : "Esc  Back", {12, 253, 100, 265}, .6f, white);
        const bool wear = skins && menu.selection() + 1 < rows.size(); // the page's last row is a switch
        const bool sets = menu.page() == sm::PausePage::ArmorSets;
        boxed(sets   ? (controller ? "A  Equip" : "Enter  Equip")
              : wear ? (controller ? "A  Wear" : "Enter  Wear")
                     : (controller ? "A  Toggle" : "Enter  Toggle"),
              {380, 253, 468, 265}, .6f, white);
        auto right = [&](std::string_view s, float x1, float y, float k, ImU32 color) { put(s, x1 - width(s, k), y, k, color); };
        const unsigned selection = menu.selection();
        if (skins) {
            const float pitch = 17.5f, top = 46;
            for (unsigned i = 0; i < rows.size(); i++) {
                const auto &row = rows[i];
                const float y = top + pitch * float(i);
                const ImU32 color = i == selection ? gold : white;
                if (row.marked) // the skin he wears
                    draw->AddRectFilled(at(249, y + 3), at(254, y + 8), gold);
                put(row.label, 259, y, .7f, color);
                if (!row.value.empty())
                    right(row.value, 468, y, .7f, color);
            }
            if (selection < rows.size()) {
                const auto &row = rows[selection];
                boxed(row.label, {8, 46, 233, 58}, .8f, gold);
                const bool option = selection + 1 == rows.size(); // the imported Ratchets' gameplay switch
                boxed(option ? "<  " + row.value + "  >" : !row.value.empty() ? "Armor  <  " + row.value + "  >"
                                                         : row.marked     ? std::string("Wearing")
                                                                          : std::string("Not worn"),
                      {8, 91, 233, 103}, .8f, white);
            }
            if (!note.empty())
                wrapped(note, 12, 135, 230, 238, .6f);
        } else {
            constexpr unsigned visible = 6;
            const unsigned first = selection >= visible ? selection - visible + 1 : 0;
            for (unsigned i = first; i < rows.size() && i < first + visible; i++) {
                const auto &row = rows[i];
                const float y = 44 + 15.5f * float(i - first);
                const ImU32 color = !row.enabled ? grey : i == selection ? gold : white;
                if (row.marked) // the set he wears
                    draw->AddRectFilled(at(122, y + 3), at(127, y + 8), gold);
                put(row.label, 131, y, .75f, color);
                right(row.value, 344, y, .75f, color);
            }
            if (first > 0)
                boxed("^", {340, 40, 352, 48}, .5f, gold);
            if (first + visible < rows.size())
                boxed("v", {340, 130, 352, 138}, .5f, gold);
            if (!note.empty()) {
                // a text longer than the panel is shown a panel at a time (the original scrolls its own, 0xb1e10)
                constexpr float k = .65f;
                const int fits = int((238 - 172) / (15 * k + 2));
                const int lines = wrapped(note, 14, 400, 466, 400, k); // measured, not drawn
                const int pages = lines > fits ? (lines + fits - 2) / (fits - 1) : 1;
                const int page = pages > 1 ? int(ImGui::GetTime() / 4) % pages : 0;
                wrapped(note, 14, 172, 466, 238, k, page * (fits - 1));
                if (pages > 1)
                    boxed(std::to_string(page + 1) + " / " + std::to_string(pages), {430, 228, 466, 238}, .5f, gold);
            }
        }
    } else if (split) {
        // a column of plates beside a panel of text (the original's own WEAPONS / GADGETS / ARMOR screens are not
        // rebuilt yet: this page is ours, in the original's plates)
        constexpr unsigned visible = 8;
        const unsigned first = menu.selection() >= visible ? menu.selection() - visible + 1 : 0;
        for (unsigned i = first; i < rows.size() && i < first + visible; i++) {
            const auto &row = rows[i];
            const float y = 30 + 30 * float(i - first);
            const bool selected = i == menu.selection();
            shape({0, 764, 127, y});
            if (selected)
                shape({0, 765, 128, y});
            float left = 27;
            if (row.icon && row.icon <= hud.icons.size()) {
                draw->AddImage((ImTextureID)(intptr_t)hud.icons[row.icon - 1], at(20, y - 9), at(38, y + 9));
                left = 42;
            }
            const ImU32 color = !row.enabled ? grey : selected ? gold : white;
            if (row.value.empty())
                boxed(row.label, {left, y - 6, 232, y + 6}, .8f, color);
            else {
                boxed(row.label, {left, y - 6, 150, y + 6}, .8f, color);
                boxed(row.value, {150, y - 6, 232, y + 6}, .8f, color);
            }
            if (row.marked) // the weapon in hand
                draw->AddRectFilled(at(236, y - 3), at(240, y + 3), gold);
        }
        if (first > 0)
            boxed("^", {12, 9, 243, 17}, .6f, gold);
        if (first + visible < rows.size())
            boxed("v", {12, 252, 243, 260}, .6f, gold);
        // the text panel, in the plates' own colours
        draw->AddRectFilledMultiColor(at(252, 16), at(468, 256), IM_COL32(0x4a, 0x6a, 0x7d, 0xbf), IM_COL32(0x4a, 0x6a, 0x7d, 0xbf),
                                      IM_COL32(0x2e, 0x41, 0x4d, 0xbf), IM_COL32(0x2e, 0x41, 0x4d, 0xbf));
        draw->AddRect(at(252, 16), at(468, 256), IM_COL32(0xcc, 0xc0, 0x00, 255), 0, 0, std::max(1.f, scale));
        if (!note.empty())
            wrapped(note, 260, 24, 460, 250, .6f);
    } else {
        for (unsigned i = 0; i < rows.size(); i++) {
            const auto &row = rows[i];
            const bool selected = i == menu.selection();
            const ImU32 color = !row.enabled ? grey : selected ? gold : white;
            if (!row.plate.set()) { // no HUD data: a plain centred list
                put(row.label, 240 - width(row.label, .9f) * .5f, 60 + 22 * float(i), .9f, color);
                continue;
            }
            shape(row.plate);
            if (selected)
                shape(row.selected);
            boxed(row.label, row.label_box, .9f, color);
            if (row.value_plate.set())
                shape(row.value_plate);
            const auto &box = row.value_box;
            if (row.slider && box[2] > box[0]) {
                const float yc = (box[1] + box[3]) * .5f;
                draw->AddRectFilled(at(box[0], yc - 3), at(box[2], yc + 3), IM_COL32(0, 0, 0, 200));
                draw->AddRectFilled(at(box[0] + 1, yc - 2), at(box[0] + 1 + (box[2] - box[0] - 2) * std::clamp(row.fraction, 0.f, 1.f), yc + 2),
                                    selected ? gold : white);
            } else if (!row.value.empty() && box[2] > box[0])
                boxed(row.value, box, .9f, color);
        }
        if (!note.empty()) // a question above its answers (the quit page)
            boxed(note, {20, 92, 460, 108}, .9f, white);
        if (!settings)
            boxed(controller ? "A  Select      B  Back      Start  Resume" : "Enter  Select      Esc  Back", {40, 280, 440, 292},
                  .6f, white);
    }
    if (!menu.notice.empty())
        boxed(menu.notice, {40, 250, 440, 262}, .6f, gold);
}
