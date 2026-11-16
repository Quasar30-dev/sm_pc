void draw_level_loop(sm::LevelLoop &loop, sm::Gameplay &game, bool controller) {
    static sm::LevelPanel previous_panel = sm::LevelPanel::None;
    static unsigned previous_selection = ~0u;
    const bool reveal_selection =
        previous_panel != loop.panel || previous_selection != loop.selection;
    previous_panel = loop.panel;
    previous_selection = loop.selection;
    auto size = ImGui::GetIO().DisplaySize;
    if (loop.panel == sm::LevelPanel::None) {
        std::string text = loop.mechanisms.prompt(controller);
        if (text.empty() && !loop.objects.prompt.empty())
            text = (controller ? "Y  " : "R  ") + loop.objects.prompt;
        if (text.empty() && loop.nearby_vendor >= 0)
            text = controller ? "Y  Open vendor" : "R  Open vendor";
        else if (text.empty() && loop.notice_time > 0)
            text = loop.notice;
        if (!text.empty()) {
            ImGui::SetNextWindowPos({size.x * .5f, size.y - 65}, ImGuiCond_Always, {.5f, .5f});
            ImGui::SetNextWindowBgAlpha(.85f);
            ImGui::Begin("##level_prompt", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoInputs);
            ImGui::SetWindowFontScale(1.3f);
            ImGui::TextUnformatted(text.c_str());
            ImGui::End();
        }
        return;
    }
    ImGui::GetBackgroundDrawList()->AddRectFilled({0, 0}, size, IM_COL32(0, 0, 0, 130));
    ImGui::SetNextWindowPos({size.x * .5f, size.y * .5f}, ImGuiCond_Always, {.5f, .5f});
    ImGui::SetNextWindowSize({std::min(640.f, size.x - 40), 0}, ImGuiCond_Always);
    ImGui::Begin("##level_panel", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::SetWindowFontScale(1.3f);
    if (loop.panel == sm::LevelPanel::Vendor) {
        ImGui::TextUnformatted("WEAPON VENDOR");
        ImGui::Separator();
        ImGui::Text("Bolts: %u", game.bolts);
        ImGui::Text("Ammo: %u / %u", game.ammo, game.max_ammo);
        ImGui::Spacing();
        std::string offer = "Refill ammo  -  " + std::to_string(loop.refill_cost(game)) + " bolts";
        if (ImGui::Selectable(offer.c_str(), loop.selection == 0))
            loop.activate(0, game);
        const auto stock = game.vendor_stock();
        for (unsigned k = 0; k < stock.size(); k++) {
            const unsigned item = stock[k];
            const std::string name = item < loop.item_names.size() && !loop.item_names[item].empty() ? loop.item_names[item]
                                                                                                    : "Item " + std::to_string(item);
            const std::string row = name + "  -  " + std::to_string(game.price(item)) + " bolts##item" + std::to_string(item);
            if (ImGui::Selectable(row.c_str(), loop.selection == k + 1))
                loop.activate(k + 1, game);
        }
        if (ImGui::Selectable("Leave vendor", loop.selection == stock.size() + 1))
            loop.activate(unsigned(stock.size()) + 1, game);
        if (!loop.notice.empty()) {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", loop.notice.c_str());
        }
        ImGui::Separator();
        ImGui::TextUnformatted(controller ? "A: buy    B: leave" : "Enter: buy    Esc: leave");
    } else if (loop.panel == sm::LevelPanel::Planets) {
        ImGui::TextUnformatted("PLANET SELECT");
        ImGui::TextUnformatted("All destinations unlocked");
        ImGui::Separator();
        ImGui::BeginChild("destinations", {0, std::min(470.f, size.y - 220)}, true);
        for (unsigned i = 0; i < loop.destinations.size(); i++) {
            auto &d = loop.destinations[i];
            std::string label = d.name + "##" + d.wad;
            if (ImGui::Selectable(label.c_str(), loop.selection == i))
                loop.activate(i, game);
            if (loop.selection == i && reveal_selection)
                ImGui::SetScrollHereY(.5f);
        }
        ImGui::EndChild();
        ImGui::TextWrapped("Campaign worlds, races and bonus areas. Multiplayer and Giant Clank "
                           "areas currently use Ratchet for exploration.");
        ImGui::Separator();
        ImGui::TextUnformatted(controller ? "D-pad: choose    A: travel    B: back"
                                          : "Arrow keys: choose    Enter: travel    Esc: back");
        if (ImGui::Button("Back"))
            loop.close();
    } else {
        ImGui::TextUnformatted("HELP DESK");
        ImGui::Separator();
        ImGui::TextUnformatted("WASD / left stick: move   Space / A: jump, hold to glide");
        ImGui::TextUnformatted("E / B: fire   F / X: wrench / crank   R / Y: interact");
        ImGui::TextUnformatted(
            "G / LT + X: wrench throw   F / X in air: hyper strike   F2: photo mode");
        ImGui::Spacing();
        unsigned page = std::min(loop.selection, loop.help_count() - 1);
        ImGui::Text("Original game manual  |  %u / %u", page + 1, loop.help_count());
        ImGui::TextWrapped("%s", loop.help_text(page, controller).c_str());
        ImGui::Spacing();
        if (ImGui::Button("Previous"))
            loop.selection = (loop.selection + loop.help_count() - 1) % loop.help_count();
        ImGui::SameLine();
        if (ImGui::Button("Next"))
            loop.selection = (loop.selection + 1) % loop.help_count();
        ImGui::SameLine();
        if (ImGui::Button("Close"))
            loop.close();
        if (ImGui::Selectable("Planet select — all destinations",
                              loop.selection == loop.help_count()))
            loop.open_planets();
        ImGui::Separator();
        ImGui::TextUnformatted(controller ? "D-pad: browse    Y: planet select    B: close"
                                          : "Arrow keys: browse    R: planet select    Esc: close");
    }
    ImGui::End();
}
