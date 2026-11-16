#include "scene.h"
#include "hig/higlighting.h"
#include <cmath>
#include <cstring>
#include <limits>
namespace sm {
bool build_scene(const hig::LevelData &level, Scene &out, std::string &error) {
    Scene s;
    s.environment = level.environment;
    s.minimum.fill(std::numeric_limits<float>::max());
    s.maximum.fill(-std::numeric_limits<float>::max());
    const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    bool have_bounds = false;
    for (size_t mi = 0; mi < level.models.size(); mi++) {
        const auto &model = level.models[mi];
        bool ocean = int(mi) == level.environment.ocean_model;
        if (!model.terrain && model.instances.empty()) {
            s.unplaced_models++;
            continue;
        }
        auto instances = model.instances;
        if (model.terrain || ocean)
            instances = {identity};
        if (!model.terrain && !ocean)
            s.instances += instances.size();
        std::vector<std::pair<const hig::LevelMaterial *, bool>> materials;
        for (const auto &m : model.materials)
            materials.push_back({&m, false});
        if (ocean)
            for (const auto &m : model.distant_materials)
                materials.push_back({&m, true});
        for (const auto &[source, distant] : materials) {
            const auto &material = *source;
            if (material.indices.empty())
                continue;
            size_t count = material.positions.size() / 3;
            if (material.positions.size() % 3 || material.uvs.size() != count * 2 ||
                material.rgba.size() != count * 4 || material.indices.size() % 3 ||
                s.vertices.size() + count * instances.size() > 16000000 ||
                s.indices.size() + material.indices.size() * instances.size() > 48000000) {
                error = "Malformed or excessive native scene geometry";
                return false;
            }
            for (auto i : material.indices)
                if (i >= count) {
                    error = "Native mesh index exceeds vertices";
                    return false;
                }
            Batch batch;
            batch.first = uint32_t(s.indices.size());
            batch.texture = material.texture;
            batch.alpha_test = material.alpha_test;
            bool has_alpha = false;
            for (size_t i = 3; i < material.rgba.size(); i += 4)
                has_alpha |= material.rgba[i] < 255;
            if (material.texture >= 0 && material.texture < int(level.textures.size())) {
                const auto &pixels = level.textures[material.texture].image.rgba;
                for (size_t i = 3; i < pixels.size(); i += 4)
                    has_alpha |= pixels[i] < 255;
            }
            batch.blend = material.alpha_blend && has_alpha && material.alpha_test < .5f;
            batch.shell = model.kind == "shell";
            batch.ocean = ocean;
            batch.ocean_distant = distant;
            batch.model = int(mi);
            auto first_vertex = s.vertices.size();
            for (size_t instance=0;instance<instances.size();++instance) {
                const auto& matrix=instances[instance];
                uint32_t base = uint32_t(s.vertices.size());
                for (size_t i = 0; i < count; i++) {
                    Vertex v{};
                    auto p = &material.positions[i * 3];
                    for (int k = 0; k < 3; k++) {
                        v.position[k] = p[0] * matrix[k] + p[1] * matrix[4 + k] +
                                        p[2] * matrix[8 + k] + matrix[12 + k];
                        if (!std::isfinite(v.position[k])) {
                            error = "Non-finite native scene transform";
                            return false;
                        }
                        if (model.kind == "terrain") {
                            s.minimum[k] = std::min(s.minimum[k], v.position[k]);
                            s.maximum[k] = std::max(s.maximum[k], v.position[k]);
                            have_bounds = true;
                        }
                        batch.center[k] += v.position[k];
                    }
                    std::memcpy(v.uv, &material.uvs[i * 2], sizeof v.uv);
                    std::memcpy(v.color, &material.rgba[i * 4], sizeof v.color);
                    if(!model.terrain&&!ocean&&model.kind!="shell"&&
                       instance<model.instance_lighting.size()&&material.normals.size()==count*3) {
                        const auto normal=hig::lighting_normal({material.normals[i*3],material.normals[i*3+1],
                                                               material.normals[i*3+2]},matrix);
                        if(normal[0]!=0||normal[1]!=0||normal[2]!=0) {
                            const auto light=hig::node_light_rgb(model.instance_lighting[instance],normal);
                            for(int k=0;k<3;++k)v.color[k]=uint8_t(std::clamp(float(v.color[k])*light[k],0.f,255.f));
                        }
                    }
                    s.vertices.push_back(v);
                }
                for (auto i : material.indices)
                    s.indices.push_back(base + i);
            }
            batch.count = uint32_t(s.indices.size()) - batch.first;
            for (auto &c : batch.center)
                c /= float(s.vertices.size() - first_vertex);
            s.batches.push_back(batch);
        }
    }
    if (!have_bounds || s.indices.empty()) {
        error = "No decoded playable terrain in this WAD";
        return false;
    }
    out = std::move(s);
    error.clear();
    return true;
}
} // namespace sm
