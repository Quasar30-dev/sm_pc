// Included in the existing F1 window; runtime options apply immediately.
if (ImGui::CollapsingHeader(sm::extras_built ? "PS2 gameplay options" : "Gameplay options", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (sm::extras_built) {
    ImGui::TextWrapped("Choose the camera, presentation and weapon set independently.");
    if (ImGui::Button("UYA camera + presentation")) {
        play_style.uya_camera = play_style.strafe = true;
        play_style.rendering = sm::uya_render_options();
        if (player_ready) uya_camera.reset(game, camera.yaw, camera.pitch);
    }
    if (ImGui::Checkbox("UYA follow camera", &play_style.uya_camera) && player_ready)
        uya_camera.reset(game, camera.yaw, camera.pitch);
    }
    ImGui::Checkbox("Strafe with Z / L2 (XInput LT)", &play_style.strafe);
    ImGui::TextWrapped("Hold strafe to keep facing the camera direction while moving sideways or backwards. G still throws the wrench.");
    if (sm::extras_built) {
    int air_pack = play_style.thruster_pack ? 1 : 0;
    const char* air_packs[] = {"Size Matters Heli-Pack", "UYA Thruster-Pack"};
    bool pack_changed = ImGui::Combo("Air pack", &air_pack, air_packs, 2);
    if (pack_changed) play_style.thruster_pack = air_pack == 1;
    pack_changed |= ImGui::Checkbox("Hydro-Pack underwater", &play_style.hydro_pack);
    if (pack_changed) {
        const auto texture_count = player_assets.textures.size();
        if (!apply_packs()) play_style.thruster_pack = false;
        if (player_assets.textures.size() != texture_count) {
            render_level.textures = runtime.world().level.textures;
            render_level.textures.insert(render_level.textures.end(), player_assets.textures.begin(), player_assets.textures.end());
            if (!renderer.upload(scene, render_level, error)) message = error;
        }
        previous = std::chrono::steady_clock::now();
        input.clear(); repeat.clear(); wait_input_release = true;
    }
    ImGui::TextWrapped("Hold C / R1 and press Space / Cross: high jump while stationary, long jump with a direction. F dives; Space ascends. Hold C / R1 underwater for Hydro-Pack boost. Imported packs require the original UYA ISO.");
    if (!pack_message.empty()) ImGui::TextWrapped("%s", pack_message.c_str());

    ImGui::SeparatorText("Weapons");
    bool using_uya = player_assets.ps2_weapons && player_assets.ps2_weapons->enabled();
    int weapon_set = using_uya ? 1 : 0;
    const char* weapon_sets[] = {"Size Matters weapons", "UYA weapons"};
    if (ImGui::Combo("Weapon set", &weapon_set, weapon_sets, 2)) {
        play_style.uya_weapons = weapon_set == 1;
        const auto texture_count = player_assets.textures.size();
        if (!apply_weapons()) play_style.uya_weapons = false;
        if (player_assets.textures.size() != texture_count) {
            render_level.textures = runtime.world().level.textures;
            render_level.textures.insert(render_level.textures.end(), player_assets.textures.begin(), player_assets.textures.end());
            if (!renderer.upload(scene, render_level, error)) message = error;
        }
        // Reading another disc must not turn into catch-up gameplay ticks.
        previous = std::chrono::steady_clock::now();
        input.clear();
        repeat.clear();
        wait_input_release = true;
    }
    using_uya = player_assets.ps2_weapons && player_assets.ps2_weapons->enabled();
    if (using_uya) {
        auto& weapons = *player_assets.ps2_weapons;
        play_style.uya_weapon = weapons.equipped();
        const char* equipped_name = "UYA weapon";
        for (const auto& choice : weapons.choices())
            if (choice.id == weapons.equipped()) equipped_name = choice.name.c_str();
        if (ImGui::BeginCombo("Equipped", equipped_name)) {
            for (const auto& choice : weapons.choices()) {
                if (ImGui::Selectable(choice.name.c_str(), choice.id == weapons.equipped()) &&
                    weapons.equip(choice.id, weapon_message))
                    play_style.uya_weapon = choice.id;
            }
            ImGui::EndCombo();
        }
        ImGui::Text("Ammo: %d / %d", weapons.ammo(), weapons.max_ammo());
        ImGui::TextWrapped("Tab / Triangle cycles these weapons. Size Matters weapons and their ammunition are kept separately.");
    }
    ImGui::TextWrapped("Supported UYA weapons: Shock Blaster and Nitro Launcher. Requires your original UYA ISO. Wrench remains available.");
    if (!weapon_message.empty()) ImGui::TextWrapped("%s", weapon_message.c_str());
    } // extras_built

    if (ImGui::TreeNode("Scene shaders and filtering")) {
        if (ImGui::Button("Size Matters presentation")) play_style.rendering = {};
        ImGui::SameLine();
        if (ImGui::Button("UYA-inspired")) play_style.rendering = sm::uya_render_options();
        auto& r = play_style.rendering;
        ImGui::SliderFloat("Colour gain", &r.color_scale, .5f, 2.f, "%.2f");
        ImGui::SliderFloat("Vertex colours", &r.vertex_color_strength, 0, 1, "%.2f");
        ImGui::SliderFloat("Saturation", &r.saturation, 0, 2, "%.2f");
        ImGui::SliderFloat("Contrast", &r.contrast, .5f, 1.5f, "%.2f");
        ImGui::SliderFloat("Gamma", &r.gamma, .5f, 2.f, "%.2f");
        int filter = int(r.texture_filter);
        const char* filters[] = {"Linear", "Nearest", "Anisotropic"};
        if (ImGui::Combo("Texture filtering", &filter, filters, 3)) r.texture_filter = sm::TextureFilter(filter);
        ImGui::SliderFloat("Mip bias", &r.mip_bias, -2, 2, "%.2f");
        ImGui::Checkbox("Smooth effect edges", &r.smooth_effects);
        ImGui::Checkbox("Distance fog", &r.fog);
        if (r.fog) {
            ImGui::ColorEdit3("Fog colour", r.fog_color.data());
            ImGui::SliderFloat("Fog starts", &r.fog_near, 0, 500, "%.1f");
            ImGui::SliderFloat("Fog ends", &r.fog_far, 1, 1000, "%.1f");
            ImGui::SliderFloat("Fog strength", &r.fog_strength, 0, 1, "%.2f");
        }
        r = sm::sanitize_render_options(r);
        ImGui::TextWrapped("The UYA-inspired preset adapts colour modulation to Size Matters assets. HUD and movie colours are preserved.");
        ImGui::TreePop();
    }
    if (sm::extras_built)
        ImGui::TextWrapped("Charge boots: choose a PS2 Ratchet model below and enable charge boots, then double-tap Shift / R1.");
    if (ImGui::Button("Save gameplay preferences")) {
        std::string why;
        message = sm::save_play_style(play_style_path, play_style, why) ? "Saved " + play_style_path.string() : why;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset options")) {
        play_style = {};
        apply_weapons();
        apply_packs();
        weapon_message.clear();
        if (player_ready) uya_camera.reset(game, camera.yaw, camera.pitch);
    }
}
