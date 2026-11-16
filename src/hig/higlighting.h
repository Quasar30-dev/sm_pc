#pragma once
#include "higlevel.h"
#include <algorithm>
#include <cmath>

namespace hig {
struct ActorLighting {
    std::array<float,3> ambient{};
    std::array<std::array<float,3>,3> directions{},colors{};
    size_t count=0;
};
// Original model 2, default merge flag 0: first two directional lights, then
// point candidates inside radius*5, maximum 16 candidates, highest three.
// LEVEL_01.REL 8dc44..8de84 / 8e05c..8e6dc. Script-created dynamic lights and
// the optional direction-clustering mode are separate, unsupported inputs.
inline ActorLighting actor_lighting(const LevelLighting& level,std::array<float,3> position) {
    ActorLighting out;
    for(int k=0;k<3;++k)out.ambient[k]=float(level.ambient[k]>>1)/128.f;
    struct Candidate { std::array<float,3> direction;std::array<uint8_t,4> color;float strength; };
    std::array<Candidate,16> candidates{};size_t count=0;
    for(size_t i=0;i<std::min(size_t(2),level.directional.size());++i) {
        const auto& light=level.directional[i];candidates[count++]={light.direction,light.rgba,1000000.f};
    }
    for(const auto& light:level.points) {
        if(count==candidates.size())break;
        std::array<float,3> delta{};float square=0;
        for(int k=0;k<3;++k){delta[k]=light.position[k]-position[k];square+=delta[k]*delta[k];}
        if(square>=light.radius*light.radius*25.f)continue;
        const float denominator=std::sqrt(square)+.1f;
        for(auto& v:delta)v/=denominator;
        candidates[count++]={delta,light.rgba,light.radius/(2*denominator)};
    }
    std::stable_sort(candidates.begin(),candidates.begin()+count,[](const auto& a,const auto& b){return a.strength>b.strength;});
    out.count=std::min(size_t(3),count);
    for(size_t i=0;i<out.count;++i) {
        out.directions[i]=candidates[i].direction;
        for(int k=0;k<3;++k)out.colors[i][k]=float(uint32_t(float(candidates[i].color[k])*std::min(1.f,candidates[i].strength)))/128.f;
    }
    return out;
}
inline std::array<float,3> actor_light_rgb(const ActorLighting& light,std::array<float,3> normal) {
    auto out=light.ambient;
    for(size_t i=0;i<light.count;++i) {
        float dot=0;for(int k=0;k<3;++k)dot+=normal[k]*light.directions[i][k];
        for(int k=0;k<3;++k)out[k]+=std::max(0.f,dot)*light.colors[i][k];
    }
    return out;
}
// LIGHTING_MODEL 1 in the original SM LEVEL_01.REL 8d5a8..8d848:
// packed signed world direction is negated and divided by 127; ambient and
// each of at most three light colours are shifted right once into GS units.
// The shrub VU pass clamps each dot product to zero before its colour MAC.
inline std::array<float,3> node_light_rgb(const LevelModel::InstanceLighting& receiver,
                                         std::array<float,3> world_normal) {
    std::array<float,3> result{};
    for(int k=0;k<3;++k)result[k]=float(receiver.ambient[k]>>1)/128.f;
    for(size_t i=0;i<std::min(size_t(3),receiver.lights.size());++i) {
        const auto& light=receiver.lights[i];
        float dot=0;
        for(int k=0;k<3;++k)dot-=world_normal[k]*float(light.direction[k])/127.f;
        dot=std::max(0.f,dot);
        for(int k=0;k<3;++k)result[k]+=dot*float(light.rgba[k]>>1)/128.f;
    }
    return result;
}
// Host normal transform for the decoded metre-space mesh. The source light
// directions remain world-space; an inverse transpose also handles the BOG
// instances whose authored scale is not uniform. Zero normals stay unlit.
inline std::array<float,3> lighting_normal(std::array<float,3> n,
                                          const std::array<float,16>& m) {
    const std::array<float,3> a{m[0],m[1],m[2]},b{m[4],m[5],m[6]},c{m[8],m[9],m[10]};
    const auto cross=[](auto x,auto y){return std::array<float,3>{x[1]*y[2]-x[2]*y[1],x[2]*y[0]-x[0]*y[2],x[0]*y[1]-x[1]*y[0]};};
    const auto x=cross(b,c),y=cross(c,a),z=cross(a,b);
    const float det=a[0]*x[0]+a[1]*x[1]+a[2]*x[2];
    if(std::abs(det)<1e-12f)return {};
    std::array<float,3> out{};
    for(int k=0;k<3;++k)out[k]=(n[0]*x[k]+n[1]*y[k]+n[2]*z[k])/det;
    const float length=std::sqrt(out[0]*out[0]+out[1]*out[1]+out[2]*out[2]);
    if(length>1e-8f)for(auto& v:out)v/=length;
    return out;
}
} // namespace hig
