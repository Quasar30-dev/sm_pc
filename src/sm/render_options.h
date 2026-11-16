#pragma once
#include <array>

namespace sm {
enum class TextureFilter { Linear = 0, Nearest = 1, Anisotropic = 2 };

// Optional PC presentation controls. Defaults retain Size Matters' existing
// texture/vertex-color pass; these are not a port of UYA material or light data.
struct RenderOptions {
    float color_scale = 1, vertex_color_strength = 1;
    float saturation = 1, contrast = 1, gamma = 1, mip_bias = 0;
    TextureFilter texture_filter = TextureFilter::Linear;
    bool smooth_effects = true, fog = false;
    std::array<float, 3> fog_color{.25f, .3f, .4f};
    float fog_near = 50, fog_far = 200, fog_strength = 1;
    bool operator==(const RenderOptions&) const = default;
};
RenderOptions sanitize_render_options(RenderOptions);
RenderOptions uya_render_options();
} // namespace sm
