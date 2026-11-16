#include "render_dx11.h"
#include "hig/higenvironment.h"
#ifndef SM_PC_STANDALONE // the dive is Up Your Arsenal's (extras.h)
#include "ps2game/water.h"
#endif
#include <algorithm>
#include <cstring>
#include <cctype>
#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <fstream>
#include <numeric>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace sm {
namespace {
bool checked(HRESULT hr, std::string &error, const char *action) {
    if (SUCCEEDED(hr))
        return true;
    char code[32];
    sprintf_s(code, " (HRESULT %08X)", unsigned(hr));
    error = std::string(action) + code;
    return false;
}
const char *shader = R"(
cbuffer Draw : register(b0) {
    row_major float4x4 view_projection; row_major float4x4 model; float4 material;
    float4 presentation; // color gain, vertex-colour strength, saturation, contrast
    float4 presentation_flags; // inverse gamma, fog enabled, smooth effects, RGB controls enabled
    float4 fog_color; float4 fog_range; float4 camera_eye;
};
struct Input {float3 position:POSITION;float2 uv:TEXCOORD0;float4 color:COLOR0;};
struct Pixel {float4 position:SV_POSITION;float2 uv:TEXCOORD0;float2 bounds:TEXCOORD1;float3 world:TEXCOORD2;float4 color:COLOR0;};
Pixel vs_main(Input i) {Pixel o;float4 world=mul(float4(i.position,1),model);o.position=mul(world,view_projection);o.world=world.xyz;o.uv=i.uv+material.zw;o.bounds=i.uv;o.color=i.color;return o;}
Texture2D albedo:register(t0);SamplerState repeat_sampler:register(s0);
float4 ps_effect(Pixel i):SV_TARGET {
    // Smooth cubic B-spline reconstruction: four bilinear taps, no ringing.
    // SampleGrad retains the actual footprint for distant/subpixel particles.
    uint w,h;albedo.GetDimensions(w,h);float2 size=float2(w,h);
    float2 p=i.uv*size-.5,base=floor(p),f=p-base,g=1-f;
    float2 w0=g*g*g/6,w1=(3*f*f*f-6*f*f+4)/6;
    float2 w2=(-3*f*f*f+3*f*f+3*f+1)/6,w3=f*f*f/6;
    float2 a=w0+w1,b=w2+w3;
    float2 lo=(base-1+w1/a+.5)/size,hi=(base+1+w3/b+.5)/size;
    float2 dx=ddx(i.uv),dy=ddy(i.uv);
    float4 c=albedo.SampleGrad(repeat_sampler,lo,dx,dy)*a.x*a.y
      +albedo.SampleGrad(repeat_sampler,float2(hi.x,lo.y),dx,dy)*b.x*a.y
      +albedo.SampleGrad(repeat_sampler,float2(lo.x,hi.y),dx,dy)*a.x*b.y
      +albedo.SampleGrad(repeat_sampler,hi,dx,dy)*b.x*b.y;
    if(presentation_flags.z<.5)c=albedo.Sample(repeat_sampler,i.uv);
    // Feather geometric edges independently of the repeating texture phase.
    float2 edge=smoothstep(0,max(float2(.12,.12),fwidth(i.bounds)*1.5),min(i.bounds,1-i.bounds));
    c.a*=edge.x*edge.y;
    return c*i.color;
}
float4 ps_main(Pixel i):SV_TARGET {
    float4 texel=albedo.Sample(repeat_sampler,i.uv),c=texel*i.color;
    clip(c.a-material.x);c.a*=material.y;
    if(presentation_flags.w>.5) {
        float3 rgb=texel.rgb*lerp(float3(1,1,1),i.color.rgb,presentation.y);
        // Above-unity gain lifts midtones while keeping white at white. SM's
        // normalized material colours must not be doubled and clipped as GS bytes.
        rgb=presentation.x>1?rgb*presentation.x/(1+(presentation.x-1)*rgb):rgb*presentation.x;
        float luminance=dot(rgb,float3(.2126,.7152,.0722));
        rgb=lerp(luminance.xxx,rgb,presentation.z);
        rgb=saturate((rgb-.5)*presentation.w+.5);
        if(presentation_flags.x!=1)rgb=pow(rgb,presentation_flags.x);
        c.rgb=rgb;
    }
    if(presentation_flags.y>.5) {
        float fog=saturate((length(i.world-camera_eye.xyz)-fog_range.x)/max(.01,fog_range.y-fog_range.x))*fog_color.a;
        c.rgb=lerp(c.rgb,fog_color.rgb,fog);
    }
    return c;
}
float4 vs_overlay(uint id:SV_VertexID):SV_POSITION {
    return float4(id==2?3:-1,id==1?3:-1,0,1);
}
float4 ps_overlay():SV_TARGET {return fog_color;}
)";
struct Constants {
    std::array<float, 16> matrix;
    hig::Matrix model;
    float material[4]{};
    float presentation[4]{}, presentation_flags[4]{};
    float fog_color[4]{}, fog_range[4]{}, camera_eye[4]{};
};
} // namespace
struct RenderDx11::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swap;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11DepthStencilView> depth;
    ComPtr<ID3D11VertexShader> vs, overlay_vs;
    ComPtr<ID3D11PixelShader> ps, effect_ps, overlay_ps;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> vertices, indices, constants;
    ComPtr<ID3D11Buffer> actor_vertices, actor_indices;
    std::vector<Batch> actor_batches;
    size_t actor_vertex_bytes = 0, actor_index_bytes = 0;
    ComPtr<ID3D11SamplerState> sampler, effect_sampler;
    ComPtr<ID3D11SamplerState> presentation_sampler;
    TextureFilter presentation_filter = TextureFilter::Linear;
    float presentation_bias = 0;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11BlendState> opaque, transparent, additive, overlay_blend;
    ComPtr<ID3D11DepthStencilState> depth_write, depth_read, depth_off;
    hig::LevelEnvironment environment;
    ComPtr<ID3D11InfoQueue> validation;
    std::vector<ComPtr<ID3D11ShaderResourceView>> textures;
    std::vector<ComPtr<ID3D11ShaderResourceView>> filtered_textures;
    ComPtr<ID3D11ShaderResourceView> white;
    std::vector<ComPtr<ID3D11ShaderResourceView>> pictures;
    std::vector<Batch> batches;
    unsigned width = 0, height = 0;
    ComPtr<ID3D11ShaderResourceView> movie_view;
    unsigned movie_width=0,movie_height=0;
    bool texture(const hig::GimImage &im, ComPtr<ID3D11ShaderResourceView> &view,
                 std::string &error,bool mutable_picture=false,bool mipmaps=false) {
        if (!im.width || !im.height || im.width > 16384 || im.height > 16384 ||
            im.rgba.size() != uint64_t(im.width) * im.height * 4) {
            error = "Invalid decoded GIM upload";
            return false;
        }
        D3D11_TEXTURE2D_DESC d{};
        d.Width = im.width;
        d.Height = im.height;
        d.MipLevels = mipmaps ? 0 : 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = mutable_picture || mipmaps ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if(mipmaps){d.BindFlags|=D3D11_BIND_RENDER_TARGET;d.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;}
        D3D11_SUBRESOURCE_DATA data{im.rgba.data(), im.width * 4, 0};
        ComPtr<ID3D11Texture2D> t;
        if(!checked(device->CreateTexture2D(&d, mipmaps?nullptr:&data, &t), error, "Create GIM texture") ||
           !checked(device->CreateShaderResourceView(t.Get(), nullptr, &view), error,"Create GIM view"))return false;
        if(mipmaps){context->UpdateSubresource(t.Get(),0,nullptr,im.rgba.data(),im.width*4,0);context->GenerateMips(view.Get());}
        return true;
    }
};
RenderDx11::RenderDx11() : p_(std::make_unique<Impl>()) {}
RenderDx11::~RenderDx11() {
    if (p_->context)
        p_->context->ClearState();
}
void *RenderDx11::device() const { return p_->device.Get(); }
void *RenderDx11::context() const { return p_->context.Get(); }
unsigned RenderDx11::width() const { return p_->width; }
unsigned RenderDx11::height() const { return p_->height; }
bool RenderDx11::init(void *window, unsigned width, unsigned height, bool debug,
                      std::string &error) {
    auto &p = *p_;
    DXGI_SWAP_CHAIN_DESC d{};
    d.BufferDesc.Width = width;
    d.BufferDesc.Height = height;
    d.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = 2;
    d.OutputWindow = HWND(window);
    d.Windowed = TRUE;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    D3D_FEATURE_LEVEL required = D3D_FEATURE_LEVEL_11_0;
    if (!checked(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                               debug ? D3D11_CREATE_DEVICE_DEBUG : 0, &required, 1,
                                               D3D11_SDK_VERSION, &d, &p.swap, &p.device, nullptr,
                                               &p.context),
                 error, "Create native DX11 device"))
        return false;
    if (debug)
        p.device.As(&p.validation);
    ComPtr<IDXGIFactory> factory;
    if (SUCCEEDED(p.swap->GetParent(IID_PPV_ARGS(&factory))))
        factory->MakeWindowAssociation(HWND(window), DXGI_MWA_NO_ALT_ENTER);
    ComPtr<ID3DBlob> vs, ps, effect_ps, overlay_vs, overlay_ps, messages;
    auto compile = [&](const char *entry, const char *profile, ComPtr<ID3DBlob> &blob) {
        HRESULT hr = D3DCompile(shader, std::strlen(shader), "sm_native_primary", nullptr, nullptr,
                                entry, profile, D3DCOMPILE_ENABLE_STRICTNESS, 0, &blob, &messages);
        if (FAILED(hr)) {
            error = messages ? std::string((char *)messages->GetBufferPointer(),
                                           messages->GetBufferSize())
                             : "Native shader compilation failed";
            return false;
        }
        return true;
    };
    if (!compile("vs_main", "vs_5_0", vs) || !compile("ps_main", "ps_5_0", ps) ||
        !compile("ps_effect", "ps_5_0", effect_ps) ||
        !compile("vs_overlay", "vs_5_0", overlay_vs) || !compile("ps_overlay", "ps_5_0", overlay_ps))
        return false;
    if(!checked(p.device->CreateVertexShader(overlay_vs->GetBufferPointer(),overlay_vs->GetBufferSize(),
                                            nullptr,&p.overlay_vs),error,"Create underwater vertex shader") ||
       !checked(p.device->CreatePixelShader(overlay_ps->GetBufferPointer(),overlay_ps->GetBufferSize(),
                                            nullptr,&p.overlay_ps),error,"Create underwater pixel shader"))return false;
    if(!checked(p.device->CreatePixelShader(effect_ps->GetBufferPointer(),effect_ps->GetBufferSize(),
                                            nullptr,&p.effect_ps),error,"Create effect shader"))return false;
    if (!checked(p.device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr,
                                              &p.vs),
                 error, "Create vertex shader") ||
        !checked(p.device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr,
                                             &p.ps),
                 error, "Create pixel shader"))
        return false;
    D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    if (!checked(p.device->CreateInputLayout(elements, 3, vs->GetBufferPointer(),
                                             vs->GetBufferSize(), &p.layout),
                 error, "Create native vertex layout"))
        return false;
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(Constants);
    cb.Usage = D3D11_USAGE_DEFAULT;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (!checked(p.device->CreateBuffer(&cb, nullptr, &p.constants), error,
                 "Create draw constants"))
        return false;
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (!checked(p.device->CreateSamplerState(&sampler, &p.sampler), error,
                 "Create texture sampler"))
        return false;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    if(!checked(p.device->CreateSamplerState(&sampler,&p.effect_sampler),error,"Create effect sampler"))return false;
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    if (!checked(p.device->CreateRasterizerState(&raster, &p.raster), error, "Create raster state"))
        return false;
    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (!checked(p.device->CreateBlendState(&blend, &p.opaque), error, "Create opaque state"))
        return false;
    auto &b = blend.RenderTarget[0];
    b.BlendEnable = TRUE;
    b.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    b.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    b.BlendOp = D3D11_BLEND_OP_ADD;
    b.SrcBlendAlpha = D3D11_BLEND_ONE;
    b.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    b.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    if (!checked(p.device->CreateBlendState(&blend, &p.transparent), error, "Create alpha state"))
        return false;
    b.SrcBlendAlpha=D3D11_BLEND_ZERO;b.DestBlendAlpha=D3D11_BLEND_ONE;
    if(!checked(p.device->CreateBlendState(&blend,&p.overlay_blend),error,"Create underwater blend state"))return false;
    b.DestBlend=D3D11_BLEND_ONE;
    if(!checked(p.device->CreateBlendState(&blend,&p.additive),error,"Create additive state"))return false;
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    if (!checked(p.device->CreateDepthStencilState(&depth, &p.depth_write), error,
                 "Create depth state"))
        return false;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    if (!checked(p.device->CreateDepthStencilState(&depth, &p.depth_read), error,
                 "Create alpha depth state"))
        return false;
    depth.DepthEnable = FALSE;
    if (!checked(p.device->CreateDepthStencilState(&depth, &p.depth_off), error,
                 "Create sky depth state"))
        return false;
    hig::GimImage white{1, 1, {255, 255, 255, 255}};
    return p.texture(white, p.white, error) && resize(width, height, error);
}
bool RenderDx11::resize(unsigned width, unsigned height, std::string &error) {
    if (width < 320 || height < 200 || width > 7680 || height > 4320) {
        error = "Render dimensions are outside 320x200 to 7680x4320";
        return false;
    }
    auto &p = *p_;
    p.context->OMSetRenderTargets(0, nullptr, nullptr);
    p.target.Reset();
    p.depth.Reset();
    if (!checked(p.swap->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), error,
                 "Resize native backbuffer"))
        return false;
    ComPtr<ID3D11Texture2D> back, depth;
    if (!checked(p.swap->GetBuffer(0, IID_PPV_ARGS(&back)), error, "Get native backbuffer") ||
        !checked(p.device->CreateRenderTargetView(back.Get(), nullptr, &p.target), error,
                 "Create render target"))
        return false;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = width;
    d.Height = height;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    d.SampleDesc.Count = 1;
    d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (!checked(p.device->CreateTexture2D(&d, nullptr, &depth), error, "Create depth buffer") ||
        !checked(p.device->CreateDepthStencilView(depth.Get(), nullptr, &p.depth), error,
                 "Create depth view"))
        return false;
    p.width = width;
    p.height = height;
    return true;
}
bool RenderDx11::upload(const Scene &scene, const hig::LevelData &level, std::string &error) {
    auto &p = *p_;
    ComPtr<ID3D11Buffer> vertices, indices;
    auto buffer = [&](const void *data, size_t size, unsigned bind, ComPtr<ID3D11Buffer> &result) {
        if (!size || size > UINT32_MAX) {
            error = "Native GPU buffer size is invalid";
            return false;
        }
        D3D11_BUFFER_DESC d{};
        d.ByteWidth = unsigned(size);
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = bind;
        D3D11_SUBRESOURCE_DATA sub{data, 0, 0};
        return checked(p.device->CreateBuffer(&d, &sub, &result), error, "Upload native geometry");
    };
    if (!buffer(scene.vertices.data(), scene.vertices.size() * sizeof(Vertex),
                D3D11_BIND_VERTEX_BUFFER, vertices) ||
        !buffer(scene.indices.data(), scene.indices.size() * 4, D3D11_BIND_INDEX_BUFFER, indices))
        return false;
    std::vector<ComPtr<ID3D11ShaderResourceView>> textures(level.textures.size()),filtered_textures(level.textures.size());
    for (size_t i = 0; i < textures.size(); i++) {
        std::string path=level.textures[i].name;
        std::transform(path.begin(),path.end(),path.begin(),[](unsigned char c){return char(std::tolower(c));});
        bool fx=path.find("\\sfx\\")!=std::string::npos;
        if (!level.textures[i].image.rgba.empty()) {
            // Keep the old single-level view for ordinary default draws. Optional
            // filtering/bias can use the prebuilt chain without a runtime reload.
            if(!p.texture(level.textures[i].image,filtered_textures[i],error,false,true))return false;
            if(fx)textures[i]=filtered_textures[i]; // effects already used generated mips
            else {
                ComPtr<ID3D11Resource> resource;filtered_textures[i]->GetResource(&resource);
                D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
                view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;
                if(!checked(p.device->CreateShaderResourceView(resource.Get(),&view,&textures[i]),error,"Create original level-zero texture view"))return false;
            }
        }
    }
    p.vertices = vertices;
    p.indices = indices;
    p.textures = std::move(textures);
    p.filtered_textures = std::move(filtered_textures);
    p.batches = scene.batches;
    p.environment = scene.environment;
    return true;
}
bool RenderDx11::update_actors(const Scene &scene, std::string &error) {
    auto &p = *p_;
    if (scene.vertices.size() > 1000000 || scene.indices.size() > 3000000) {
        error = "Excessive actor geometry";
        return false;
    }
    if (scene.vertices.empty() || scene.indices.empty()) {
        p.actor_batches.clear();
        return true;
    }
    for (auto i : scene.indices)
        if (i >= scene.vertices.size()) {
            error = "Actor index exceeds vertices";
            return false;
        }
    auto update = [&](const void *data, size_t size, unsigned bind, ComPtr<ID3D11Buffer> &buffer,
                      size_t &capacity) {
        if (size > capacity) {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = unsigned((size + 65535) & ~size_t(65535));
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = bind;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            ComPtr<ID3D11Buffer> replacement;
            if (!checked(p.device->CreateBuffer(&desc, nullptr, &replacement), error,
                         "Create actor buffer"))
                return false;
            buffer = replacement;
            capacity = desc.ByteWidth;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (!checked(p.context->Map(buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), error,
                     "Map actor geometry"))
            return false;
        std::memcpy(mapped.pData, data, size);
        p.context->Unmap(buffer.Get(), 0);
        return true;
    };
    if (!update(scene.vertices.data(), scene.vertices.size() * sizeof(Vertex),
                D3D11_BIND_VERTEX_BUFFER, p.actor_vertices, p.actor_vertex_bytes) ||
        !update(scene.indices.data(), scene.indices.size() * 4, D3D11_BIND_INDEX_BUFFER,
                p.actor_indices, p.actor_index_bytes))
        return false;
    p.actor_batches = scene.batches;
    return true;
}
bool RenderDx11::draw(const std::array<float, 16> &matrix, const SceneView &view,
                      std::string &error) {
    auto &p = *p_;
    const auto options=sanitize_render_options(view.render_options);
    if (!p.target) {
        error = "Native backbuffer is unavailable";
        return false;
    }
    if((options.texture_filter!=TextureFilter::Linear||options.mip_bias!=0)&&
       (!p.presentation_sampler||p.presentation_filter!=options.texture_filter||p.presentation_bias!=options.mip_bias)) {
        D3D11_SAMPLER_DESC descriptor{};
        descriptor.Filter=options.texture_filter==TextureFilter::Nearest?D3D11_FILTER_MIN_MAG_MIP_POINT:
                          options.texture_filter==TextureFilter::Anisotropic?D3D11_FILTER_ANISOTROPIC:D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        descriptor.AddressU=descriptor.AddressV=descriptor.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
        descriptor.MaxAnisotropy=16;descriptor.MaxLOD=D3D11_FLOAT32_MAX;descriptor.MipLODBias=options.mip_bias;
        ComPtr<ID3D11SamplerState> replacement;
        if(!checked(p.device->CreateSamplerState(&descriptor,&replacement),error,"Create presentation sampler"))return false;
        p.presentation_sampler=std::move(replacement);
        p.presentation_filter=options.texture_filter;p.presentation_bias=options.mip_bias;
    }
    auto *c = p.context.Get();
    float clear[] = {.025f, .035f, .06f, 1};
    c->ClearRenderTargetView(p.target.Get(), clear);
    c->ClearDepthStencilView(p.depth.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1, 0);
    c->OMSetRenderTargets(1, p.target.GetAddressOf(), p.depth.Get());
    if (!p.vertices)
        return true;
    D3D11_VIEWPORT vp{0, 0, float(p.width), float(p.height), 0, 1};
    c->RSSetViewports(1, &vp);
    c->RSSetState(p.raster.Get());
    UINT stride = sizeof(Vertex), offset = 0;
    c->IASetVertexBuffers(0, 1, p.vertices.GetAddressOf(), &stride, &offset);
    c->IASetIndexBuffer(p.indices.Get(), DXGI_FORMAT_R32_UINT, 0);
    c->IASetInputLayout(p.layout.Get());
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(p.vs.Get(), nullptr, 0);
    c->PSSetShader(p.ps.Get(), nullptr, 0);
    c->GSSetShader(nullptr, nullptr, 0);
    c->VSSetConstantBuffers(0, 1, p.constants.GetAddressOf());
    c->PSSetConstantBuffers(0, 1, p.constants.GetAddressOf());
    c->PSSetSamplers(0, 1, p.sampler.GetAddressOf());
    const hig::Matrix identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    struct DrawItem {
        const Batch *batch;
        hig::Matrix model;
        bool actor;
        float distance;
    };
    std::vector<DrawItem> sky, solid, alpha;
    auto tiles = hig::ocean_tiles(p.environment, view.anchor, view.seconds);
    auto add = [&](const Batch &b, const hig::Matrix &model, bool actor) {
        auto center = hig::transform_point(b.center, model);
        float distance = 0;
        for (int k = 0; k < 3; k++)
            distance += (center[k] - view.eye[k]) * (center[k] - view.eye[k]);
        (b.shell                ? sky
         : (b.blend || b.ocean) ? alpha
                                : solid)
            .push_back({&b, model, actor, distance});
    };
    for (const auto &b : p.batches) {
        if (b.shell) {
            if (view.sky)
                add(b, hig::sky_transform(p.environment, b.model, view.eye, view.seconds), false);
        } else if (b.ocean) {
            if (view.ocean)
                for (const auto &tile : tiles)
                    if (tile.distant == b.ocean_distant)
                        add(b, tile.transform, false);
        } else
            add(b, identity, false);
    }
    for (const auto &b : p.actor_batches)
        add(b, identity, true);
    // Sky layers retain their authored order and never write terrain depth.
    // Water and other translucent surfaces composite after opaque actors.
    std::stable_sort(alpha.begin(), alpha.end(),
                     [](const auto &a, const auto &b) { return a.distance > b.distance; });
    auto uv = hig::ocean_uv_offset(view.seconds);
    auto draw_items = [&](const std::vector<DrawItem> &items) {
        for (const auto &item : items) {
            const auto &b = *item.batch;
            const bool style_surface=!b.shell&&!b.effect&&!b.additive;
            const bool filtered=style_surface&&(options.texture_filter!=TextureFilter::Linear||options.mip_bias!=0);
            // Imported weapon/boot effects do not use SM's "\\sfx\\" names.
            // Their SampleGrad reconstruction still needs the full mip chain.
            const auto& texture_views=filtered||b.effect?p.filtered_textures:p.textures;
            auto *texture =
                b.texture >= 0 && b.texture < int(texture_views.size()) && texture_views[b.texture]
                    ? texture_views[b.texture].Get()
                    : p.white.Get();
            c->PSSetShaderResources(0, 1, &texture);
            c->PSSetShader(b.effect?p.effect_ps.Get():p.ps.Get(),nullptr,0);
            auto* sample=b.effect&&!b.effect_wrap?p.effect_sampler.Get():
                filtered?p.presentation_sampler.Get():p.sampler.Get();
            c->PSSetSamplers(0,1,&sample);
            Constants constants{
                matrix,
                item.model,
                {b.alpha_test, b.ocean ? .5f : 1.f, b.ocean ? uv[0] : b.uv_offset[0], b.ocean ? uv[1] : b.uv_offset[1]}};
            const bool rgb=style_surface&&(options.color_scale!=1||options.vertex_color_strength!=1||
                options.saturation!=1||options.contrast!=1||options.gamma!=1);
            constants.presentation[0]=options.color_scale;constants.presentation[1]=options.vertex_color_strength;
            constants.presentation[2]=options.saturation;constants.presentation[3]=options.contrast;
            constants.presentation_flags[0]=1/options.gamma;constants.presentation_flags[1]=style_surface&&options.fog?1.f:0.f;
            constants.presentation_flags[2]=options.smooth_effects?1.f:0.f;constants.presentation_flags[3]=rgb?1.f:0.f;
            for(size_t k=0;k<3;++k){constants.fog_color[k]=options.fog_color[k];constants.camera_eye[k]=view.eye[k];}
            constants.fog_color[3]=options.fog_strength;constants.fog_range[0]=options.fog_near;constants.fog_range[1]=options.fog_far;
            c->UpdateSubresource(p.constants.Get(), 0, nullptr, &constants, 0, 0);
            bool blend = b.blend || b.ocean;
            c->OMSetBlendState(b.additive ? p.additive.Get() : blend ? p.transparent.Get() : p.opaque.Get(), nullptr, UINT_MAX);
            c->OMSetDepthStencilState(b.shell ? p.depth_off.Get()
                                      : blend ? p.depth_read.Get()
                                              : p.depth_write.Get(),
                                      0);
            c->IASetVertexBuffers(
                0, 1, item.actor ? p.actor_vertices.GetAddressOf() : p.vertices.GetAddressOf(),
                &stride, &offset);
            c->IASetIndexBuffer(item.actor ? p.actor_indices.Get() : p.indices.Get(),
                                DXGI_FORMAT_R32_UINT, 0);
            c->DrawIndexed(b.count, b.first, 0);
        }
    };
    draw_items(sky);
    draw_items(solid);
    draw_items(alpha);
    if(view.underwater) {
        // Camera immersion is supplied by gameplay's authored ocean plane. The
        // tint is UYA WaterProfile's original GS colour, adapted to the SM scene.
        // Composite once after geometry/effects and before the separate HUD pass.
#ifdef SM_PC_STANDALONE
        constexpr uint32_t rgba=0; // (never under water in this build)
#else
        constexpr uint32_t rgba=ps2game::WaterProfile{}.tint_rgba;
#endif
        Constants tint{};
        for(int k=0;k<3;++k)tint.fog_color[k]=float((rgba>>(8*k))&255)/255.f;
        tint.fog_color[3]=float(rgba>>24)/128.f;
        c->UpdateSubresource(p.constants.Get(),0,nullptr,&tint,0,0);
        c->IASetInputLayout(nullptr);
        c->VSSetShader(p.overlay_vs.Get(),nullptr,0);
        c->PSSetShader(p.overlay_ps.Get(),nullptr,0);
        c->OMSetBlendState(p.overlay_blend.Get(),nullptr,UINT_MAX);
        c->OMSetDepthStencilState(p.depth_off.Get(),0);
        c->Draw(3,0);
    }
    return true;
}
void *RenderDx11::back_buffer() const {
    if (!p_->target)
        return nullptr;
    ComPtr<ID3D11Resource> resource;
    p_->target->GetResource(&resource);
    return resource.Get(); // the swap chain keeps it alive
}
bool RenderDx11::present(bool vsync, std::string &error) {
    return checked(p_->swap->Present(vsync ? 1 : 0, 0), error, "Present native frame");
}
bool RenderDx11::capture(const std::filesystem::path &path, std::string &error) {
    auto &p = *p_;
    if (std::filesystem::exists(path)) {
        error = "Capture file already exists";
        return false;
    }
    ComPtr<ID3D11Texture2D> back, staging;
    if (!checked(p.swap->GetBuffer(0, IID_PPV_ARGS(&back)), error, "Read capture backbuffer"))
        return false;
    D3D11_TEXTURE2D_DESC d{};
    back->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    if (!checked(p.device->CreateTexture2D(&d, nullptr, &staging), error, "Create capture staging"))
        return false;
    p.context->CopyResource(staging.Get(), back.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    if (!checked(p.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map), error,
                 "Map frame capture"))
        return false;
    std::vector<uint8_t> bgra(size_t(p.width) * p.height * 4);
    for (unsigned y = 0; y < p.height; y++)
        for (unsigned x = 0; x < p.width; x++) {
            auto *src = (const uint8_t *)map.pData + y * map.RowPitch + x * 4;
            auto *dst = bgra.data() + (size_t(y) * p.width + x) * 4;
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = 255;
        }
    p.context->Unmap(staging.Get(), 0);
    BITMAPFILEHEADER fh{};
    fh.bfType = 0x4d42;
    fh.bfOffBits = sizeof(fh) + sizeof(BITMAPINFOHEADER);
    fh.bfSize = fh.bfOffBits + DWORD(bgra.size());
    BITMAPINFOHEADER ih{};
    ih.biSize = sizeof(ih);
    ih.biWidth = LONG(p.width);
    ih.biHeight = -LONG(p.height);
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biSizeImage = DWORD(bgra.size());
    std::ofstream file(path, std::ios::binary);
    file.write((const char *)&fh, sizeof fh);
    file.write((const char *)&ih, sizeof ih);
    file.write((const char *)bgra.data(), bgra.size());
    if (!file) {
        error = "Cannot write native frame capture";
        return false;
    }
    return true;
}
size_t RenderDx11::validation_errors() const {
    if (!p_->validation)
        return 0;
    size_t count = 0;
    for (UINT64 i = 0; i < p_->validation->GetNumStoredMessages(); i++) {
        SIZE_T size = 0;
        p_->validation->GetMessage(i, nullptr, &size);
        std::vector<uint8_t> storage(size);
        auto *msg = (D3D11_MESSAGE *)storage.data();
        if (SUCCEEDED(p_->validation->GetMessage(i, msg, &size)) &&
            msg->Severity <= D3D11_MESSAGE_SEVERITY_ERROR)
            count++;
    }
    return count;
}
void *RenderDx11::upload_picture(const hig::GimImage &image, std::string &error) {
    ComPtr<ID3D11ShaderResourceView> view;
    if (!p_->texture(image, view, error))
        return nullptr;
    p_->pictures.push_back(view);
    return view.Get();
}
void *RenderDx11::update_movie(const hig::GimImage &image,std::string &error) {
    if(!image.width||!image.height||image.rgba.size()!=uint64_t(image.width)*image.height*4){error="Invalid movie frame";return nullptr;}
    if(!p_->movie_view||p_->movie_width!=image.width||p_->movie_height!=image.height) {
        p_->movie_view.Reset();if(!p_->texture(image,p_->movie_view,error,true))return nullptr;
        p_->movie_width=image.width;p_->movie_height=image.height;
    } else {
        ComPtr<ID3D11Resource> resource;p_->movie_view->GetResource(&resource);
        p_->context->UpdateSubresource(resource.Get(),0,nullptr,image.rgba.data(),image.width*4,0);
    }
    return p_->movie_view.Get();
}
} // namespace sm
