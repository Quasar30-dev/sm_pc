// PC presentation of the shared, headlessly runnable frontend state machine.
void draw_frontend(sm::Frontend &flow, const hig::Localization &text, sm::RenderDx11 &renderer,
                   sm::DisplaySettings &display, sm::DisplaySettings &pending,
                   const std::filesystem::path &prefs, bool controller, std::string &message,
                   bool &photo_request) {
    const auto screen = flow.screen();
    static sm::Screen previous = sm::Screen::Level;
    bool entered = screen != previous;
    previous = screen;
    if (screen == sm::Screen::Level)
        return;
    auto label = [&](uint32_t id) {
        auto *entry = text.find(id);
        return entry ? hig::inspect_localized_text(*entry) : std::to_string(id);
    };
    auto &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(40, std::max(24.f, io.DisplaySize.y * .18f)), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(std::min(510.f, io.DisplaySize.x - 80), 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(.94f);
    auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_AlwaysAutoResize;
    if (screen != sm::Screen::Display)
        flags |= ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus;
    ImGui::Begin("##native_frontend", nullptr, flags);
    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextUnformatted("RATCHET & CLANK");
    ImGui::TextColored(ImVec4(.5f, .85f, 1, 1), "SIZE MATTERS");
    ImGui::Separator();
    auto item = [&](unsigned row, const std::string &caption, bool enabled = true) {
        bool selected = flow.selection() == row;
        ImGui::BeginDisabled(!enabled);
        ImGui::PushID(int(row));
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.13f, .4f, .57f, 1));
        bool clicked =
            ImGui::Button(((selected ? ">  " : "   ") + caption).c_str(), ImVec2(-1, 38));
        if (selected)
            ImGui::PopStyleColor();
        ImGui::PopID();
        ImGui::EndDisabled();
        if (clicked)
            flow.choose(row);
    };
    switch (screen) {
    case sm::Screen::Title:
        for (unsigned row = 0; row < flow.title().items.size(); row++) {
            const auto &entry = flow.title().items[row];
            item(row, label(entry.text_id), entry.enabled);
        }
        ImGui::TextWrapped("Native engine development build. Game progress loading "
                           "is not available yet.");
        break;
    case sm::Screen::NewGame:
        ImGui::TextWrapped("%s", label(591).c_str());
        ImGui::Spacing();
        ImGui::TextWrapped("This build loads Pokitaru's scenery and collision. Player gameplay "
                           "and progress saving are still in development.");
        item(0, "Start");
        item(1, "Cancel");
        break;
    case sm::Screen::Loading:
        ImGui::TextUnformatted("Loading Pokitaru...");
        break;
    case sm::Screen::Pause:
        ImGui::TextUnformatted("PAUSED");
        item(0, "Resume");
        item(1, label(452));
        item(2, "Return to title");
        ImGui::Spacing();
        if (ImGui::Button("Photo mode (F2)"))
            photo_request = true;
        break;
    case sm::Screen::Options:
        ImGui::TextUnformatted(label(452).c_str());
        item(0, "Graphics");
        item(1, label(712));
        item(2, "Back");
        break;
    case sm::Screen::Controls:
        ImGui::TextUnformatted(label(712).c_str());
        ImGui::TextWrapped("Menus: Arrow keys / D-pad or left stick. Enter / A "
                           "selects; Esc / B goes back.");
        ImGui::Spacing();
        ImGui::TextWrapped("Ratchet: WASD / left stick moves. Space / A jumps; hold it in "
                          "the air to glide. E or left mouse / B fires. Tap F / X for the "
                          "wrench combo. G or C + F / LT + X throws the wrench. F / X in the air performs a hyper strike. R / Y opens a nearby vendor; H / Back opens Help Desk.");
        ImGui::Spacing();
        ImGui::TextWrapped("Level inspection: hold right mouse and use WASD to move, Q / E "
                           "vertically. Controller sticks move/look; bumpers move vertically. Esc "
                           "or Start pauses. F1 opens the development tools. F2 opens photo mode.");
        item(0, "Back");
        break;
    case sm::Screen::Notice:
        ImGui::TextWrapped("%s", flow.message().c_str());
        item(0, "Back");
        break;
    case sm::Screen::Display:
        ImGui::TextUnformatted("GRAPHICS");
        if (entered) {
            ImGui::SetWindowFocus();
            ImGui::SetKeyboardFocusHere();
        }
        if (ImGui::BeginCombo(
                "Resolution",
                (std::to_string(pending.width) + " x " + std::to_string(pending.height)).c_str())) {
            for (auto resolution :
                 {std::pair{1280u, 720u}, {1920u, 1080u}, {2560u, 1440u}, {3840u, 2160u}}) {
                auto name =
                    std::to_string(resolution.first) + " x " + std::to_string(resolution.second);
                if (ImGui::Selectable(name.c_str(), pending.width == resolution.first &&
                                                        pending.height == resolution.second)) {
                    pending.width = resolution.first;
                    pending.height = resolution.second;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::BeginCombo("Frame cap", std::to_string(pending.fps).c_str())) {
            for (unsigned rate : {30, 60, 120, 144, 240})
                if (ImGui::Selectable(std::to_string(rate).c_str(), pending.fps == rate))
                    pending.fps = rate;
            ImGui::EndCombo();
        }
        ImGui::Checkbox("V-sync", &pending.vsync);
        ImGui::TextWrapped("V-sync can limit the frame rate to your monitor's refresh rate.");
        if (ImGui::Button("Apply and save", ImVec2(-1, 38))) {
            std::string error;
            if (renderer.resize(pending.width, pending.height, error)) {
                display = pending;
                message =
                    sm::save_display(prefs, display, error) ? "Graphics settings saved." : error;
            } else
                message = error;
        }
        if (!message.empty())
            ImGui::TextWrapped("%s", message.c_str());
        if (ImGui::Button("Back", ImVec2(-1, 38)))
            flow.pulse(hig::Circle);
        break;
    default:
        break;
    }
    ImGui::Separator();
    ImGui::TextUnformatted(controller ? "A: Select    B: Back" : "Enter: Select    Esc: Back");
    ImGui::End();
}
