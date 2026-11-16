#include "render_options.h"
#include <algorithm>
#include <cmath>

namespace sm {
RenderOptions sanitize_render_options(RenderOptions o) {
    const auto bound=[](float value, float fallback, float lo, float hi) {
        return std::clamp(std::isfinite(value)?value:fallback,lo,hi);
    };
    o.color_scale=bound(o.color_scale,1,0,4);
    o.vertex_color_strength=bound(o.vertex_color_strength,1,0,1);
    o.saturation=bound(o.saturation,1,0,2);
    o.contrast=bound(o.contrast,1,0,2);
    o.gamma=bound(o.gamma,1,.25f,4);
    o.mip_bias=bound(o.mip_bias,0,-2,2);
    if(o.texture_filter!=TextureFilter::Linear&&o.texture_filter!=TextureFilter::Nearest&&
       o.texture_filter!=TextureFilter::Anisotropic)o.texture_filter=TextureFilter::Linear;
    for(size_t i=0;i<o.fog_color.size();++i)
        o.fog_color[i]=bound(o.fog_color[i],RenderOptions{}.fog_color[i],0,1);
    o.fog_near=bound(o.fog_near,50,0,9999);
    o.fog_far=bound(o.fog_far,200,o.fog_near+.01f,10000);
    o.fog_strength=bound(o.fog_strength,1,0,1);
    return o;
}

RenderOptions uya_render_options() {
    RenderOptions o;
    // UYA's GS MODULATE convention is 0x80=unity (ps2native/shading.h),
    // whereas this renderer receives SM's already normalized vertex colours.
    // Apply that full gain through the host's white-preserving shoulder. The
    // previous quarter-gain preset was barely visible. This adapts already
    // normalized SM colours; it does not substitute UYA's material pipeline.
    o.color_scale=255.f/128.f;
    return o;
}
} // namespace sm
