// Included after Camera. UI and camera policy are native PC features; asset
// discovery/playback lives in the headless core and shared HIG reader.
struct PhotoUi {
    sm::PhotoSession session;
    bool active = false, visible = true, enter_requested = false, exit_requested = false;
    bool textures_dirty = false, capture_requested = false;
    float frozen_alpha = 1;
    Camera saved_camera;
    int resolution = 0, spawn_at = 0, catalog_selection = 0, requested_tab = -1;
    std::string message;
    ImGuiTextFilter character_filter, clip_filter;
    std::vector<std::string> sources;
    size_t scanned = 0;
    struct Scan {
        std::vector<hig::ActorCatalogEntry> entries;
        std::string error;
    };
    std::future<Scan> scan_job;
    void reset() {
        if (scan_job.valid())
            scan_job.get();
        session = {};
        sources.clear();
        scanned = 0;
        active = enter_requested = exit_requested = textures_dirty = false;
        catalog_selection = 0;
        message.clear();
    }
    void seed(const hig::IsoFs &disc, const sm::PlayerAssets &assets) {
        if (!session.catalog.empty())
            return;
        session.seed(assets);
        sources = {"/GLOBAL.WAD", "/SP.WAD"};
        for (const auto &file : disc.files())
            if (file.path.starts_with("/WAD/") && file.path.ends_with(".WAD"))
                sources.push_back(file.path);
        textures_dirty = true;
    }
    void scan(const std::wstring &iso) {
        if (scan_job.valid()) {
            if (scan_job.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return;
            auto result = scan_job.get();
            session.catalog.insert(session.catalog.end(), result.entries.begin(),
                                   result.entries.end());
            if (!result.error.empty())
                message = sources[scanned] + ": " + result.error;
            scanned++;
        }
        if (scanned < sources.size()) {
            auto path = sources[scanned];
            // IsoFs uses a seekable FILE; a private handle keeps background
            // catalog reads independent of music, movies and gameplay loading.
            scan_job = std::async(std::launch::async, [iso, path] {
                Scan result;
                hig::IsoFs disc;
                if (disc.open(iso, &result.error))
                    sm::read_photo_catalog(disc, path, result.entries, result.error);
                return result;
            });
        }
    }
    void enter(Camera &camera, const sm::Gameplay &game, float alpha) {
        saved_camera = camera;
        frozen_alpha = alpha;
        camera.speed = 3;
        active = visible = true;
        enter_requested = false;
        if (session.actors.empty()) {
            auto id = session.spawn(0, game.interpolated(alpha), game.heading);
            auto *actor = session.find(id);
            actor->clip = game.clip_index();
            actor->time = std::min(session.clip(*actor).duration,
                                   game.animation_time + alpha / 30 * game.animation_rate());
            actor->weapon = game.motion == sm::Motion::Wrench ||
                                    game.motion == sm::Motion::HyperStrike ||
                                    game.motion == sm::Motion::WrenchThrow
                                ? 1
                                : 2;
            session.show_player = false;
        }
    }
    void frame(Camera &camera, const sm::PhotoActor &actor) {
        auto [lo, hi] = session.bounds(actor);
        hig::Vec3 center;
        float extent = 0;
        for (int k = 0; k < 3; k++) {
            center[k] = (lo[k] + hi[k]) * .5f;
            extent += (hi[k] - lo[k]) * (hi[k] - lo[k]);
        }
        float distance =
            std::max(.25f, std::sqrt(extent) * .55f) / std::sin(camera.fov * XM_PI / 360);
        camera.yaw = -3 * XM_PI / 4;
        camera.pitch = -.2f;
        XMFLOAT3 direction;
        XMStoreFloat3(&direction, camera.direction());
        camera.eye = {center[0] - direction.x * distance, center[1] - direction.y * distance,
                      center[2] - direction.z * distance};
    }
    void draw(Camera &camera, const hig::IsoFs &disc, const sm::PlayerAssets &assets,
              const sm::Gameplay &game) {
        if (!visible)
            return;
        const auto &io = ImGui::GetIO();
        float width = std::min(370.f, io.DisplaySize.x - 32);
        ImGui::SetNextWindowPos({io.DisplaySize.x - width - 16, 16}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({width, io.DisplaySize.y - 32}, ImGuiCond_Always);
        ImGui::Begin("Photo mode", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoCollapse);
        ImGui::TextDisabled("Gameplay frozen | F2 / Esc: return");
        if (ImGui::Button("Take photo (F12)"))
            capture_requested = true;
        ImGui::SameLine();
        if (ImGui::Button("Hide UI (F1)"))
            visible = false;
        const char *sizes[] = {"Current render size", "1920 x 1080", "3840 x 2160"};
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##photo_resolution", &resolution, sizes, 3);
        ImGui::Separator();
        if (ImGui::BeginTabBar("##photo_tabs")) {
            if (ImGui::BeginTabItem("Characters", nullptr,
                                    requested_tab == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
                ImGui::Checkbox("Show gameplay Ratchet", &session.show_player);
                ImGui::Checkbox("Show level enemies", &session.show_enemies);
                ImGui::Text("Photo characters (%zu / 64)", session.actors.size());
                if (ImGui::BeginListBox("##actors", {-1, 125})) {
                    for (const auto &a : session.actors) {
                        auto label = std::to_string(a.id) + "  " + a.name;
                        if (ImGui::Selectable(label.c_str(), session.selected == a.id))
                            session.selected = a.id;
                    }
                    ImGui::EndListBox();
                }
                if (ImGui::Button("Duplicate"))
                    session.duplicate(session.selected);
                ImGui::SameLine();
                if (ImGui::Button("Remove"))
                    session.remove(session.selected);
                ImGui::SameLine();
                if (ImGui::Button("Pose selected"))
                    requested_tab = 1;
                ImGui::Separator();
                ImGui::Text("Disc library: %zu models", session.catalog.size());
                if (scanned < sources.size())
                    ImGui::TextDisabled("Reading %zu / %zu archives...", scanned + 1,
                                        sources.size());
                ImGui::TextUnformatted("Search characters");
                character_filter.Draw("##character_search", -1);
                if (ImGui::BeginListBox("##catalog", {-1, 150})) {
                    for (size_t i = 0; i < session.catalog.size(); i++) {
                        const auto &c = session.catalog[i];
                        auto label = c.name + "  [" + c.wad + "]";
                        if (character_filter.PassFilter(label.c_str()) &&
                            ImGui::Selectable((label + "##" + std::to_string(i)).c_str(),
                                              int(i) == catalog_selection))
                            catalog_selection = int(i);
                    }
                    ImGui::EndListBox();
                }
                if (!session.catalog.empty()) {
                    const auto &entry = session.catalog.at(catalog_selection);
                    ImGui::TextDisabled("%u clips | %u bones", entry.clips, entry.bones);
                }
                const char *places[] = {"Beside Ratchet", "In front of camera",
                                        "At selected character"};
                ImGui::SetNextItemWidth(-1);
                ImGui::Combo("##spawn_at", &spawn_at, places, 3);
                if (ImGui::Button("Spawn character", {-1, 0})) {
                    size_t asset;
                    size_t before = session.assets.size();
                    if (session.load(disc, size_t(catalog_selection), asset, message)) {
                        hig::Vec3 at = game.interpolated(frozen_alpha);
                        at[0] += 1;
                        if (spawn_at == 1) {
                            XMFLOAT3 d;
                            XMStoreFloat3(&d, camera.direction());
                            at = {camera.eye.x + d.x * 3, camera.eye.y + d.y * 3,
                                  camera.eye.z + d.z * 3};
                        } else if (spawn_at == 2)
                            if (const auto *a = session.find(session.selected))
                                at = a->position;
                        if (!session.spawn(asset, at, game.heading))
                            message = "The scene already contains 64 photo characters.";
                        else
                            requested_tab = 1;
                        textures_dirty |= before != session.assets.size();
                    }
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Pose", nullptr,
                                    requested_tab == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
                if (requested_tab == 1)
                    requested_tab = -1;
                auto *a = session.find(session.selected);
                if (!a)
                    ImGui::TextWrapped("Spawn or select a photo character in Characters.");
                else {
                    ImGui::TextWrapped("%llu  %s", (unsigned long long)a->id, a->name.c_str());
                    ImGui::Checkbox("Visible", &a->visible);
                    ImGui::SameLine();
                    if (ImGui::Button("Frame selected"))
                        frame(camera, *a);
                    ImGui::DragFloat3("Position", a->position.data(), .01f);
                    hig::Vec3 degrees = a->rotation;
                    for (auto &v : degrees)
                        v *= 180 / XM_PI;
                    if (ImGui::DragFloat3("Rotation", degrees.data(), .5f))
                        for (unsigned i = 0; i < 3; i++)
                            a->rotation[i] = degrees[i] * XM_PI / 180;
                    ImGui::DragFloat("Scale", &a->scale, .01f, .01f, 100, "%.2f",
                                     ImGuiSliderFlags_AlwaysClamp);
                    if (ImGui::Button("Snap to ground")) {
                        auto from = a->position, to = a->position;
                        from[1] += 2;
                        to[1] -= 100;
                        auto hit = game.terrain().ray(from, to);
                        if (hit.hit)
                            a->position[1] = hit.point[1] + .005f;
                    }
                    ImGui::Separator();
                    ImGui::TextUnformatted("Find animation");
                    clip_filter.Draw("##animation_search", -1);
                    const auto &bank = session.assets[a->asset].animations;
                    if (ImGui::BeginListBox("##clips", {-1, 150})) {
                        for (size_t i = 0; i < bank.clips.size(); i++) {
                            auto label = std::to_string(i) + "  " + bank.clips[i].name;
                            if (clip_filter.PassFilter(label.c_str()) &&
                                ImGui::Selectable(label.c_str(), a->clip == i)) {
                                a->clip = unsigned(i);
                                a->time = 0;
                            }
                        }
                        ImGui::EndListBox();
                    }
                    const auto &clip = session.clip(*a);
                    ImGui::Text("%u / %zu   %s", a->clip + 1, bank.clips.size(), clip.name.c_str());
                    if (ImGui::Button(a->playing ? "Pause" : "Play")) {
                        if (!a->playing && a->time >= clip.duration)
                            a->time = 0;
                        a->playing = !a->playing;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("< frame")) {
                        a->playing = false;
                        a->time = std::max(0.f, a->time - 1.f / 30);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("frame >")) {
                        a->playing = false;
                        a->time = std::min(clip.duration, a->time + 1.f / 30);
                    }
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::SliderFloat("##time", &a->time, 0, clip.duration, "%.3f seconds"))
                        a->playing = false;
                    ImGui::Checkbox("Loop", &a->loop);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(140);
                    ImGui::SliderFloat("Speed", &a->speed, .05f, 4, "%.2fx");
                    if (session.assets[a->asset].ratchet &&
                        ImGui::CollapsingHeader("Attachments")) {
                        ImGui::Checkbox("Clank", &a->clank);
                        ImGui::Checkbox("Follow hero animation", &a->auto_attachments);
                        int clank = int(a->clank_clip), weapon = int(a->weapon),
                            clip_index = int(a->weapon_clip);
                        ImGui::SliderInt("Clank clip", &clank, 0,
                                         int(assets.clank_animations.clips.size()) - 1);
                        a->clank_clip = unsigned(clank);
                        if (ImGui::Combo("Weapon", &weapon, "None\0Wrench\0Blaster\0")) {
                            a->weapon = unsigned(weapon);
                            a->weapon_clip = clip_index = 0;
                        }
                        if (weapon) {
                            const auto &clips = weapon == 1 ? assets.wrench_animations.clips
                                                            : assets.blaster_animations.clips;
                            ImGui::SliderInt("Weapon clip", &clip_index, 0, int(clips.size()) - 1);
                            a->weapon_clip = unsigned(clip_index);
                        }
                        ImGui::SliderFloat("Attachment time", &a->attachment_time, 0, 2, "%.3f s");
                    }
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Camera", nullptr,
                                    requested_tab == 2 ? ImGuiTabItemFlags_SetSelected : 0)) {
                ImGui::TextWrapped("Hold right mouse to look. WASD moves; Q / E lowers or raises "
                                   "the camera. Hold Shift for faster movement.");
                ImGui::TextWrapped("Controller: sticks move/look, bumpers lower/raise. Start "
                                   "returns to gameplay.");
                ImGui::SliderFloat("Field of view", &camera.fov, 10, 120, "%.0f degrees");
                ImGui::SliderFloat("Roll", &camera.roll, -180, 180, "%.1f degrees");
                ImGui::SliderFloat("Move speed", &camera.speed, .1f, 30, "%.1f");
                ImGui::DragFloat3("Camera position", &camera.eye.x, .02f);
                if (ImGui::Button("Frame selected"))
                    if (const auto *a = session.find(session.selected))
                        frame(camera, *a);
                ImGui::SameLine();
                if (ImGui::Button("Reset camera")) {
                    camera = saved_camera;
                    camera.speed = 3;
                }
                ImGui::Spacing();
                ImGui::TextWrapped("Photos are saved without menus or HUD. F12 also works with the "
                                   "interface hidden. F1 brings the interface back.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        if (!message.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", message.c_str());
        }
        ImGui::End();
    }
};
