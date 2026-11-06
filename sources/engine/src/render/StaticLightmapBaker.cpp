#include "engine/render/StaticLightmapBaker.hpp"
#include "engine/math/ColorSpace.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace kb::render {
namespace {
using V = kb::math::Vec3;
V Add(V a,V b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
V Sub(V a,V b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
V Mul(V a,float s) { return {a.x*s,a.y*s,a.z*s}; }
V Unit(V a) { return Mul(a,1.0F/std::sqrt(std::max(Dot(a,a),1.0e-20F))); }
bool Finite(V a) { return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z); }
bool Hit(V p,V d,float limit,const LightmapTriangle& t) {
    const V e1=Sub(t[1].position,t[0].position), e2=Sub(t[2].position,t[0].position);
    const V h=Cross(d,e2); const float determinant=Dot(e1,h);
    if (std::abs(determinant)<1.0e-8F) return false;
    const float inverse=1.0F/determinant; const V s=Sub(p,t[0].position);
    const float u=Dot(s,h)*inverse; if(u<0||u>1) return false;
    const V q=Cross(s,e1); const float v=Dot(d,q)*inverse; if(v<0||u+v>1) return false;
    const float distance=Dot(e2,q)*inverse; return distance>0.00001F&&distance<limit;
}
}

StaticLightmapResult BakeStaticLightmap(std::span<const LightmapTriangle> receiver,
    std::span<const LightmapTriangle> occluders,std::span<const LightmapLight> lights,const StaticLightmapSettings& settings) {
    StaticLightmapResult result;
    const auto fail=[&](const char* error) { result.error=error; result.rgba.clear(); return result; };
    if(receiver.empty()||settings.resolution<4||settings.resolution>1024||!std::isfinite(settings.ambient)||settings.ambient<0||
        !std::isfinite(settings.rayBias)||settings.rayBias<=0||settings.rayBias>1) return fail("invalid lightmap settings or empty receiver");
    const auto size=static_cast<std::size_t>(settings.resolution);
    const double work=static_cast<double>(size*size)*static_cast<double>(std::max<std::size_t>(lights.size(),1))*
        static_cast<double>(std::max<std::size_t>(occluders.size(),1));
    if(receiver.size()>100000||lights.size()>256||work>200000000.0) return fail("lightmap work budget exceeded; reduce resolution, lights or occluders");
    for(const auto& collection : {receiver,occluders}) for(const auto& t:collection) for(const auto& v:t)
        if(!Finite(v.position)||!Finite(v.normal)||!std::isfinite(v.u)||!std::isfinite(v.v)) return fail("non-finite lightmap geometry");
    for(const auto& t:receiver) for(const auto& v:t) if(v.u<0||v.u>1||v.v<0||v.v>1||Dot(v.normal,v.normal)<1.0e-12F)
        return fail("receiver requires normals and UV0 in [0,1]");
    for(const auto& light:lights) if(!Finite(light.position)||!Finite(light.direction)||!Finite(light.color)||
        !std::isfinite(light.intensity)||light.intensity<0||!std::isfinite(light.range)||light.range<=0||
        light.color.x<0||light.color.y<0||light.color.z<0||(light.directional&&Dot(light.direction,light.direction)<1.0e-12F))
        return fail("invalid lightmap light");
    result.rgba.resize(size*size*4,0);
    std::vector<int> owners(size*size,-1);
    for(std::size_t triangle=0;triangle<receiver.size();++triangle) {
        const auto& t=receiver[triangle];
        const float determinant=(t[1].u-t[0].u)*(t[2].v-t[0].v)-(t[2].u-t[0].u)*(t[1].v-t[0].v);
        if(std::abs(determinant)<1.0e-10F) return fail("receiver contains degenerate UV triangles");
        const int minX=std::max(0,static_cast<int>(std::floor(std::min({t[0].u,t[1].u,t[2].u})*size)));
        const int maxX=std::min(static_cast<int>(size)-1,static_cast<int>(std::ceil(std::max({t[0].u,t[1].u,t[2].u})*size)));
        const int minY=std::max(0,static_cast<int>(std::floor(std::min({t[0].v,t[1].v,t[2].v})*size)));
        const int maxY=std::min(static_cast<int>(size)-1,static_cast<int>(std::ceil(std::max({t[0].v,t[1].v,t[2].v})*size)));
        for(int y=minY;y<=maxY;++y) for(int x=minX;x<=maxX;++x) {
            const float u=(x+0.5F)/static_cast<float>(size),v=(y+0.5F)/static_cast<float>(size);
            const float b=((u-t[0].u)*(t[2].v-t[0].v)-(v-t[0].v)*(t[2].u-t[0].u))/determinant;
            const float c=((t[1].u-t[0].u)*(v-t[0].v)-(t[1].v-t[0].v)*(u-t[0].u))/determinant,a=1-b-c;
            if(std::min({a,b,c}) < -0.00001F) continue;
            const auto pixel=static_cast<std::size_t>(y)*size+x;
            if(owners[pixel]>=0) {
                // Adjacent atlas triangles may share their mathematical edge, never their interior.
                const auto& previous=receiver[static_cast<std::size_t>(owners[pixel])];
                const float pd=(previous[1].u-previous[0].u)*(previous[2].v-previous[0].v)-(previous[2].u-previous[0].u)*(previous[1].v-previous[0].v);
                const float pb=((u-previous[0].u)*(previous[2].v-previous[0].v)-(v-previous[0].v)*(previous[2].u-previous[0].u))/pd;
                const float pc=((previous[1].u-previous[0].u)*(v-previous[0].v)-(previous[1].v-previous[0].v)*(u-previous[0].u))/pd;
                if(std::min({a,b,c})>0.00001F||std::min({1-pb-pc,pb,pc})>0.00001F) return fail("receiver UV0 overlaps; provide a unique lightmap atlas");
                continue;
            }
            owners[pixel]=static_cast<int>(triangle); ++result.coveredTexels;
            const V p=Add(Add(Mul(t[0].position,a),Mul(t[1].position,b)),Mul(t[2].position,c));
            const V n=Unit(Add(Add(Mul(t[0].normal,a),Mul(t[1].normal,b)),Mul(t[2].normal,c)));
            V irradiance{settings.ambient,settings.ambient,settings.ambient};
            for(const auto& light:lights) {
                const V delta=light.directional?Mul(Unit(light.direction),-1):Sub(light.position,p);
                const float distance=std::sqrt(Dot(delta,delta)); const V direction=Unit(delta);
                float strength=std::max(Dot(n,direction),0.0F)*light.intensity;
                if(!light.directional) { const float fade=std::max(1-distance/light.range,0.0F);strength*=fade*fade/std::max(distance*distance,1.0F); }
                if(strength<=0) continue;
                if(light.castsShadow&&std::any_of(occluders.begin(),occluders.end(),[&](const auto& blocker) {
                    return Hit(Add(p,Mul(n,settings.rayBias)),direction,light.directional?1.0e20F:distance-settings.rayBias,blocker);
                })) { ++result.shadowedSamples;continue; }
                irradiance=Add(irradiance,Mul(light.color,strength));
            }
            const float values[]{irradiance.x,irradiance.y,irradiance.z};
            for(unsigned channel=0;channel<3;++channel) result.rgba[pixel*4+channel]=static_cast<std::uint8_t>(
                std::clamp(kb::math::LinearToSrgb(std::clamp(values[channel],0.0F,1.0F))*255.0F+0.5F,0.0F,255.0F));
            result.rgba[pixel*4+3]=255;
        }
    }
    if(result.coveredTexels==0) return fail("receiver has no covered lightmap texels");
    // Four dilation steps pad atlas islands before the ordinary texture mip baker runs.
    for(unsigned pass=0;pass<4;++pass) {
        auto padded=result.rgba;
        for(std::size_t y=0;y<size;++y) for(std::size_t x=0;x<size;++x) {
            const auto pixel=y*size+x;if(result.rgba[pixel*4+3]) continue;
            const std::size_t neighbors[]{y*size+(x?x-1:x),y*size+std::min(x+1,size-1),(y?y-1:y)*size+x,std::min(y+1,size-1)*size+x};
            for(const auto other:neighbors) if(result.rgba[other*4+3]) { std::copy_n(result.rgba.data()+other*4,4,padded.data()+pixel*4);break; }
        }
        result.rgba.swap(padded);
    }
    return result;
}
} // namespace kb::render
