#include "engine/render/StaticLightmapBaker.hpp"
#include "engine/math/ColorSpace.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>

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

bool ValidLight(const LightmapLight& light) {
    return !(!Finite(light.position)||!Finite(light.direction)||!Finite(light.color)||
        !std::isfinite(light.intensity)||light.intensity<0||!std::isfinite(light.range)||light.range<=0||
        light.color.x<0||light.color.y<0||light.color.z<0||((light.directional||light.spot)&&Dot(light.direction,light.direction)<1.0e-12F)||
        (light.spot&&(!std::isfinite(light.innerConeDegrees)||!std::isfinite(light.outerConeDegrees)||light.innerConeDegrees<0||light.outerConeDegrees<light.innerConeDegrees||light.outerConeDegrees>89)));
}

// Cosine-weighted strength of one light at p with normal n, before the shadow test; `direction` points at the light.
float DirectStrength(const LightmapLight& light,V p,V n,V& direction,float& distance) {
    const V delta=light.directional?Mul(Unit(light.direction),-1):Sub(light.position,p);
    distance=std::sqrt(Dot(delta,delta)); direction=Unit(delta);
    float strength=std::max(Dot(n,direction),0.0F)*light.intensity;
    if(light.directional) return strength;
    // The runtime falloff (fs_mesh_instanced.sc): inverse square, windowed to zero at the range.
    const float ratio=distance/light.range, window=std::clamp(1-ratio*ratio*ratio*ratio,0.0F,1.0F);
    strength*=window*window/std::max(distance*distance,0.0001F);
    if(light.spot) {
        const float inner=std::cos(light.innerConeDegrees*0.0174532925F), outer=std::cos(light.outerConeDegrees*0.0174532925F);
        const float ramp=std::clamp((Dot(Unit(light.direction),Mul(direction,-1))-outer)/std::max(inner-outer,0.001F),0.0F,1.0F);
        strength*=ramp*ramp;
    }
    return strength;
}

// Van der Corput radical inverse in [0,1): with the index as the other axis it gives a fixed Hammersley point set.
float RadicalInverse(unsigned bits) {
    bits=(bits<<16)|(bits>>16); bits=((bits&0x55555555U)<<1)|((bits&0xAAAAAAAAU)>>1);
    bits=((bits&0x33333333U)<<2)|((bits&0xCCCCCCCCU)>>2); bits=((bits&0x0F0F0F0FU)<<4)|((bits&0xF0F0F0F0U)>>4);
    bits=((bits&0x00FF00FFU)<<8)|((bits&0xFF00FF00U)>>8);
    return static_cast<float>(bits)*2.3283064365386963e-10F;
}

constexpr unsigned kBounceRays=128;
V Mean(const LightmapTriangle& t) { return Mul(Add(Add(t[0].albedo,t[1].albedo),t[2].albedo),1.0F/3.0F); }
// Ray/triangle hit with barycentrics (b1 on corner 1, b2 on corner 2); same epsilon rules as Hit.
bool Intersect(V p,V d,const LightmapTriangle& t,float& distance,float& b1,float& b2) {
    const V e1=Sub(t[1].position,t[0].position), e2=Sub(t[2].position,t[0].position);
    const V h=Cross(d,e2); const float determinant=Dot(e1,h);
    if (std::abs(determinant)<1.0e-8F) return false;
    const float inverse=1.0F/determinant; const V s=Sub(p,t[0].position);
    const float u=Dot(s,h)*inverse; if(u<0||u>1) return false;
    const V q=Cross(s,e1); const float v=Dot(d,q)*inverse; if(v<0||u+v>1) return false;
    distance=Dot(e2,q)*inverse; b1=u; b2=v; return distance>0.00001F;
}

// Median-split bounding volume hierarchy over a triangle list, used by the bounce pass only.
struct Bvh {
    struct Node { V lo{},hi{}; int left=-1,right=-1,first=0,count=0; };
    std::vector<Node> nodes; std::vector<int> order; const std::vector<LightmapTriangle>* triangles=nullptr;
    void Build(const std::vector<LightmapTriangle>& source) {
        triangles=&source; order.resize(source.size()); for(std::size_t i=0;i<order.size();++i) order[i]=static_cast<int>(i);
        nodes.reserve(source.size()/2+2); BuildRange(0,static_cast<int>(order.size()));
    }
    static V Centroid(const LightmapTriangle& t) { return Mul(Add(Add(t[0].position,t[1].position),t[2].position),1.0F/3.0F); }
    int BuildRange(int begin,int end) {
        const int index=static_cast<int>(nodes.size()); nodes.emplace_back();
        V lo{1e30F,1e30F,1e30F},hi{-1e30F,-1e30F,-1e30F},clo=lo,chi=hi;
        for(int i=begin;i<end;++i) {
            const auto& t=(*triangles)[static_cast<std::size_t>(order[static_cast<std::size_t>(i)])]; const V c=Centroid(t);
            for(const auto& v:t) { lo={std::min(lo.x,v.position.x),std::min(lo.y,v.position.y),std::min(lo.z,v.position.z)};
                hi={std::max(hi.x,v.position.x),std::max(hi.y,v.position.y),std::max(hi.z,v.position.z)}; }
            clo={std::min(clo.x,c.x),std::min(clo.y,c.y),std::min(clo.z,c.z)}; chi={std::max(chi.x,c.x),std::max(chi.y,c.y),std::max(chi.z,c.z)};
        }
        nodes[static_cast<std::size_t>(index)].lo=lo; nodes[static_cast<std::size_t>(index)].hi=hi;
        if(end-begin<=4) { nodes[static_cast<std::size_t>(index)].first=begin; nodes[static_cast<std::size_t>(index)].count=end-begin; return index; }
        const V extent=Sub(chi,clo); const int axis=extent.x>=extent.y&&extent.x>=extent.z?0:(extent.y>=extent.z?1:2);
        const auto key=[&](int triangle) { const V c=Centroid((*triangles)[static_cast<std::size_t>(triangle)]); return axis==0?c.x:(axis==1?c.y:c.z); };
        const int middle=(begin+end)/2;
        std::nth_element(order.begin()+begin,order.begin()+middle,order.begin()+end,[&](int a,int b) { const float ka=key(a),kb=key(b); return ka<kb||(ka==kb&&a<b); });
        const int left=BuildRange(begin,middle), right=BuildRange(middle,end);
        nodes[static_cast<std::size_t>(index)].left=left; nodes[static_cast<std::size_t>(index)].right=right; return index;
    }
    static bool Slab(const Node& n,V o,V inverse,float limit) {
        float t0=0,t1=limit;
        const float lo[]{n.lo.x,n.lo.y,n.lo.z},hi[]{n.hi.x,n.hi.y,n.hi.z},origin[]{o.x,o.y,o.z},inv[]{inverse.x,inverse.y,inverse.z};
        for(int axis=0;axis<3;++axis) {
            float a=(lo[axis]-origin[axis])*inv[axis], b=(hi[axis]-origin[axis])*inv[axis]; if(a>b) std::swap(a,b);
            t0=std::max(t0,a); t1=std::min(t1,b); if(t0>t1) return false;
        }
        return true;
    }
    // Nearest hit closer than `limit`; ties go to the lower triangle index.
    bool Nearest(V o,V d,float limit,float& distance,int& hit,float& b1,float& b2) const {
        const auto safe=[](float c) { return std::abs(c)<1.0e-12F?(c<0?-1.0e12F:1.0e12F):1.0F/c; };
        const V inverse{safe(d.x),safe(d.y),safe(d.z)};
        int stack[64]; int depth=0; stack[depth++]=0; hit=-1; float best=limit;
        while(depth>0) {
            const Node& n=nodes[static_cast<std::size_t>(stack[--depth])];
            if(!Slab(n,o,inverse,best)) continue;
            if(n.left<0) {
                for(int i=n.first;i<n.first+n.count;++i) {
                    const int triangle=order[static_cast<std::size_t>(i)]; float t=0,u=0,v=0;
                    if(!Intersect(o,d,(*triangles)[static_cast<std::size_t>(triangle)],t,u,v)||t>best) continue;
                    if(t==best&&hit>=0&&triangle>hit) continue;
                    best=t; hit=triangle; b1=u; b2=v;
                }
            } else if(depth<62) { stack[depth++]=n.left; stack[depth++]=n.right; }
        }
        distance=best; return hit>=0;
    }
};

bool InsideUnit(const LightmapTriangle& t) {
    for(const auto& v:t) if(!std::isfinite(v.u)||!std::isfinite(v.v)||v.u<0||v.u>1||v.v<0||v.v>1) return false;
    return true;
}
float UvDeterminant(const LightmapTriangle& t) { return (t[1].u-t[0].u)*(t[2].v-t[0].v)-(t[2].u-t[0].u)*(t[1].v-t[0].v); }
}

StaticLightmapResult BakeStaticLightmap(std::span<const LightmapTriangle> receiver,
    std::span<const LightmapTriangle> occluders,std::span<const LightmapLight> lights,const StaticLightmapSettings& settings) {
    StaticLightmapResult result;
    const auto fail=[&](const char* error) { result.error=error; result.rgba.clear(); return result; };
    if(receiver.empty()||settings.resolution<4||settings.resolution>1024||!std::isfinite(settings.ambient)||settings.ambient<0||
        !std::isfinite(settings.rayBias)||settings.rayBias<=0||settings.rayBias>1||settings.bounces>4) return fail("invalid lightmap settings or empty receiver");
    const auto size=static_cast<std::size_t>(settings.resolution);
    const double work=static_cast<double>(size*size)*static_cast<double>(std::max<std::size_t>(lights.size(),1))*
        static_cast<double>(std::max<std::size_t>(occluders.size(),1));
    const double bounceRays=static_cast<double>(size*size)*kBounceRays*settings.bounces*static_cast<double>(1+std::min<std::size_t>(lights.size(),8));
    if(receiver.size()>100000||lights.size()>256||work>200000000.0||bounceRays>150000000.0)
        return fail("lightmap work budget exceeded; reduce resolution, lights, occluders or bounces");
    for(const auto& collection : {receiver,occluders}) for(const auto& t:collection) for(const auto& v:t)
        if(!Finite(v.position)||!Finite(v.normal)||!std::isfinite(v.u)||!std::isfinite(v.v)) return fail("non-finite lightmap geometry");
    for(const auto& t:receiver) for(const auto& v:t) if(v.u<0||v.u>1||v.v<0||v.v>1||Dot(v.normal,v.normal)<1.0e-12F)
        return fail("receiver requires normals and UV0 in [0,1]");
    for(const auto& light:lights) if(!ValidLight(light)) return fail("invalid lightmap light");
    result.rgba.resize(size*size*4,0);
    std::vector<int> owners(size*size,-1);
    struct Texel { V position{},normal{},direct{}; };
    std::vector<Texel> texels(settings.bounces>0?size*size:0);
    const auto encode=[&](std::size_t pixel,V irradiance) {
        const float values[]{irradiance.x,irradiance.y,irradiance.z};
        for(unsigned channel=0;channel<3;++channel) result.rgba[pixel*4+channel]=static_cast<std::uint8_t>(
            std::clamp(kb::math::LinearToSrgb(std::clamp(values[channel],0.0F,1.0F))*255.0F+0.5F,0.0F,255.0F));
        result.rgba[pixel*4+3]=255;
    };
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
                V direction; float distance=0; const float strength=DirectStrength(light,p,n,direction,distance);
                if(strength<=0) continue;
                if(light.castsShadow&&std::any_of(occluders.begin(),occluders.end(),[&](const auto& blocker) {
                    return Hit(Add(p,Mul(n,settings.rayBias)),direction,light.directional?1.0e20F:distance-settings.rayBias,blocker);
                })) { ++result.shadowedSamples;continue; }
                irradiance=Add(irradiance,Mul(light.color,strength));
            }
            if(settings.bounces>0) texels[pixel]={p,n,irradiance};
            encode(pixel,irradiance);
        }
    }
    if(result.coveredTexels==0) return fail("receiver has no covered lightmap texels");
    if(settings.bounces>0) {
        // Gather pass: cosine-weighted rays pick up the previous pass's outgoing light (albedo * irradiance)
        // of whatever they hit. Receiver hits read the lightmap itself, other geometry is lit directly.
        std::vector<LightmapTriangle> geometry(receiver.begin(),receiver.end());
        geometry.insert(geometry.end(),occluders.begin(),occluders.end());
        Bvh bvh; bvh.Build(geometry);
        const auto directAt=[&](V p,V n) {
            V sum{settings.ambient,settings.ambient,settings.ambient};
            for(const auto& light:lights) {
                V direction; float distance=0; const float strength=DirectStrength(light,p,n,direction,distance);
                if(strength<=0) continue;
                float t=0,b1=0,b2=0; int blocker=-1;
                if(light.castsShadow&&bvh.Nearest(Add(p,Mul(n,settings.rayBias)),direction,light.directional?1.0e20F:distance-settings.rayBias,t,blocker,b1,b2)) continue;
                sum=Add(sum,Mul(light.color,strength));
            }
            return sum;
        };
        std::vector<V> current(size*size),next(size*size);
        for(std::size_t i=0;i<current.size();++i) current[i]=texels[i].direct;
        for(unsigned bounce=0;bounce<settings.bounces;++bounce) {
            for(std::size_t pixel=0;pixel<size*size;++pixel) {
                if(owners[pixel]<0) continue;
                const auto& texel=texels[pixel];
                const V axis=std::abs(texel.normal.y)<0.99F?V{0,1,0}:V{1,0,0};
                const V tangent=Unit(Cross(axis,texel.normal)), bitangent=Cross(texel.normal,tangent);
                const V origin=Add(texel.position,Mul(texel.normal,settings.rayBias));
                V gathered{0,0,0};
                for(unsigned ray=0;ray<kBounceRays;++ray) {
                    // Hammersley points mapped to a cosine-weighted hemisphere: fixed set, so the bake is deterministic.
                    const float radius2=(static_cast<float>(ray)+0.5F)/static_cast<float>(kBounceRays), angle=6.28318530718F*RadicalInverse(ray);
                    const float radius=std::sqrt(radius2), up=std::sqrt(1-radius2);
                    const V direction=Add(Add(Mul(tangent,radius*std::cos(angle)),Mul(bitangent,radius*std::sin(angle))),Mul(texel.normal,up));
                    float distance=0,b1=0,b2=0; int hit=-1;
                    if(!bvh.Nearest(origin,direction,1.0e20F,distance,hit,b1,b2)) continue;
                    const auto& surface=geometry[static_cast<std::size_t>(hit)];
                    V incoming;
                    if(static_cast<std::size_t>(hit)<receiver.size()) {
                        const float b0=1-b1-b2;
                        const float hitU=surface[0].u*b0+surface[1].u*b1+surface[2].u*b2, hitV=surface[0].v*b0+surface[1].v*b1+surface[2].v*b2;
                        const auto x=std::min(static_cast<std::size_t>(std::max(hitU,0.0F)*static_cast<float>(size)),size-1);
                        const auto y=std::min(static_cast<std::size_t>(std::max(hitV,0.0F)*static_cast<float>(size)),size-1);
                        if(owners[y*size+x]<0) continue;
                        incoming=current[y*size+x];
                    } else {
                        // Two-sided: the shading normal faces the ray.
                        V normal=Unit(Cross(Sub(surface[1].position,surface[0].position),Sub(surface[2].position,surface[0].position)));
                        if(Dot(normal,direction)>0) normal=Mul(normal,-1);
                        incoming=directAt(Add(origin,Mul(direction,distance)),normal);
                    }
                    const V albedo=Mean(surface);
                    gathered=Add(gathered,{incoming.x*albedo.x,incoming.y*albedo.y,incoming.z*albedo.z});
                }
                next[pixel]=Add(texel.direct,Mul(gathered,1.0F/static_cast<float>(kBounceRays)));
            }
            current.swap(next);
        }
        for(std::size_t pixel=0;pixel<size*size;++pixel) if(owners[pixel]>=0) encode(pixel,current[pixel]);
    }
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

namespace {
// Smallest barycentric weight of texel-centre (u,v) in the UV triangle.
float UvMinWeight(const LightmapTriangle& t,float u,float v) {
    const float determinant=UvDeterminant(t);
    const float b=((u-t[0].u)*(t[2].v-t[0].v)-(v-t[0].v)*(t[2].u-t[0].u))/determinant;
    const float c=((t[1].u-t[0].u)*(v-t[0].v)-(t[1].v-t[0].v)*(u-t[0].u))/determinant;
    return std::min({1-b-c,b,c});
}
}

bool HasUsableLightmapUv(std::span<const LightmapTriangle> triangles,std::uint16_t resolution) {
    if(triangles.empty()||resolution<4||resolution>1024) return false;
    const auto size=static_cast<std::size_t>(resolution);
    std::vector<int> owners(size*size,-1);
    for(std::size_t index=0;index<triangles.size();++index) {
        const auto& t=triangles[index];
        if(!InsideUnit(t)||std::abs(UvDeterminant(t))<1.0e-10F) return false;
        const int minX=std::max(0,static_cast<int>(std::floor(std::min({t[0].u,t[1].u,t[2].u})*size)));
        const int maxX=std::min(static_cast<int>(size)-1,static_cast<int>(std::ceil(std::max({t[0].u,t[1].u,t[2].u})*size)));
        const int minY=std::max(0,static_cast<int>(std::floor(std::min({t[0].v,t[1].v,t[2].v})*size)));
        const int maxY=std::min(static_cast<int>(size)-1,static_cast<int>(std::ceil(std::max({t[0].v,t[1].v,t[2].v})*size)));
        for(int y=minY;y<=maxY;++y) for(int x=minX;x<=maxX;++x) {
            const float u=(x+0.5F)/static_cast<float>(size),v=(y+0.5F)/static_cast<float>(size);
            const float weight=UvMinWeight(t,u,v);
            if(weight< -0.00001F) continue;
            auto& owner=owners[static_cast<std::size_t>(y)*size+x];
            if(owner>=0) { if(weight>0.00001F||UvMinWeight(triangles[static_cast<std::size_t>(owner)],u,v)>0.00001F) return false; continue; }
            owner=static_cast<int>(index);
        }
    }
    return true;
}

LightmapUvLayout GenerateLightmapUv(std::span<const LightmapTriangle> triangles,std::uint16_t resolution) {
    LightmapUvLayout layout;
    const auto fail=[&](const char* error) { layout.error=error; layout.uv.clear(); return layout; };
    if(triangles.empty()||triangles.size()>100000||resolution<16||resolution>1024) return fail("invalid lightmap UV request");
    const std::size_t count=triangles.size();
    std::vector<V> faceNormal(count);
    for(std::size_t i=0;i<count;++i) {
        for(const auto& v:triangles[i]) if(!Finite(v.position)) return fail("non-finite lightmap geometry");
        const V c=Cross(Sub(triangles[i][1].position,triangles[i][0].position),Sub(triangles[i][2].position,triangles[i][0].position));
        const float length=std::sqrt(Dot(c,c));
        if(!(length>1.0e-10F)) return fail("lightmap UV generation needs non-degenerate triangles");
        faceNormal[i]=Mul(c,1.0F/length);
    }
    // Triangles that share an edge (same welded positions) are neighbours.
    std::map<std::array<long long,3>,int> corners;
    std::map<std::pair<int,int>,std::vector<int>> edges;
    for(std::size_t i=0;i<count;++i) {
        int id[3];
        for(int k=0;k<3;++k) {
            const V p=triangles[i][static_cast<std::size_t>(k)].position;
            const std::array<long long,3> key{std::llround(p.x*10000.0),std::llround(p.y*10000.0),std::llround(p.z*10000.0)};
            id[k]=corners.emplace(key,static_cast<int>(corners.size())).first->second;
        }
        for(int k=0;k<3;++k) { const int a=id[k],b=id[(k+1)%3]; edges[{std::min(a,b),std::max(a,b)}].push_back(static_cast<int>(i)); }
    }
    std::vector<std::vector<int>> neighbours(count);
    for(const auto& edge:edges) for(const int a:edge.second) for(const int b:edge.second) if(a!=b) neighbours[static_cast<std::size_t>(a)].push_back(b);
    for(auto& list:neighbours) { std::sort(list.begin(),list.end()); list.erase(std::unique(list.begin(),list.end()),list.end()); }
    // Charts grow over neighbours whose normal stays within 45 degrees of the seed's, then project onto the seed's plane.
    struct Chart { std::vector<int> triangles; V tangent{},bitangent{}; float minX=0,minY=0,width=0,height=0,x=0,y=0; };
    std::vector<Chart> charts; std::vector<int> owner(count,-1);
    for(std::size_t seed=0;seed<count;++seed) {
        if(owner[seed]>=0) continue;
        Chart chart; const int id=static_cast<int>(charts.size());
        std::vector<int> queue{static_cast<int>(seed)}; owner[seed]=id;
        for(std::size_t head=0;head<queue.size();++head) {
            chart.triangles.push_back(queue[head]);
            for(const int next:neighbours[static_cast<std::size_t>(queue[head])]) {
                if(owner[static_cast<std::size_t>(next)]>=0||Dot(faceNormal[static_cast<std::size_t>(next)],faceNormal[seed])<0.7071F) continue;
                owner[static_cast<std::size_t>(next)]=id; queue.push_back(next);
            }
        }
        const auto& t=triangles[seed]; const V normal=faceNormal[seed];
        const V edgesOf[]{Sub(t[1].position,t[0].position),Sub(t[2].position,t[1].position),Sub(t[0].position,t[2].position)};
        V longest=edgesOf[0]; for(const auto& e:edgesOf) if(Dot(e,e)>Dot(longest,longest)) longest=e;
        chart.tangent=Unit(Sub(longest,Mul(normal,Dot(longest,normal)))); chart.bitangent=Cross(normal,chart.tangent);
        float maxX=-1e30F,maxY=-1e30F; chart.minX=1e30F; chart.minY=1e30F;
        for(const int tri:chart.triangles) for(const auto& v:triangles[static_cast<std::size_t>(tri)]) {
            const float px=Dot(v.position,chart.tangent),py=Dot(v.position,chart.bitangent);
            chart.minX=std::min(chart.minX,px); maxX=std::max(maxX,px); chart.minY=std::min(chart.minY,py); maxY=std::max(maxY,py);
        }
        chart.width=maxX-chart.minX; chart.height=maxY-chart.minY; charts.push_back(std::move(chart));
    }
    // Shelf packing, tallest first; texel gap and edge margin keep the bake's padding of one chart off its neighbours.
    constexpr float gap=3.0F,margin=2.0F; const float usable=static_cast<float>(resolution)-2*margin;
    std::vector<std::size_t> order(charts.size()); for(std::size_t i=0;i<order.size();++i) order[i]=i;
    std::sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b) { return charts[a].height>charts[b].height||(charts[a].height==charts[b].height&&a<b); });
    const auto pack=[&](float scale,bool commit) {
        float x=0,y=0,row=0;
        for(const std::size_t index:order) {
            auto& chart=charts[index]; const float w=chart.width*scale,h=chart.height*scale;
            if(w>usable||h>usable) return false;
            if(x+w>usable) { y+=row+gap; x=0; row=0; }
            if(y+h>usable) return false;
            if(commit) { chart.x=margin+x; chart.y=margin+y; }
            x+=w+gap; row=std::max(row,h);
        }
        return true;
    };
    float extent=1.0e-6F; for(const auto& chart:charts) extent=std::max({extent,chart.width,chart.height});
    float low=0,high=usable/extent;
    if(!pack(1.0e-7F,false)) return fail("lightmap atlas too small for this many surfaces; raise the resolution");
    if(pack(high,false)) low=high; else for(int step=0;step<40;++step) { const float middle=(low+high)*0.5F; (pack(middle,false)?low:high)=middle; }
    const float scale=low>0?low:1.0e-7F; static_cast<void>(pack(scale,true));
    layout.uv.assign(count*6,0.0F);
    for(const auto& chart:charts) for(const int tri:chart.triangles) for(int k=0;k<3;++k) {
        const V p=triangles[static_cast<std::size_t>(tri)][static_cast<std::size_t>(k)].position;
        layout.uv[static_cast<std::size_t>(tri)*6+static_cast<std::size_t>(k)*2]=(chart.x+(Dot(p,chart.tangent)-chart.minX)*scale)/static_cast<float>(resolution);
        layout.uv[static_cast<std::size_t>(tri)*6+static_cast<std::size_t>(k)*2+1]=(chart.y+(Dot(p,chart.bitangent)-chart.minY)*scale)/static_cast<float>(resolution);
    }
    return layout;
}
namespace {
constexpr unsigned kProbeRays=256;
constexpr float kPi=3.14159265359F;
// Real SH basis of a unit direction, band order L00, L1-1, L10, L11, L2-2, L2-1, L20, L21, L22.
std::array<float,9> ShBasis(V d) {
    return {0.282095F,0.488603F*d.y,0.488603F*d.z,0.488603F*d.x,1.092548F*d.x*d.y,1.092548F*d.y*d.z,
        0.315392F*(3*d.z*d.z-1),1.092548F*d.x*d.z,0.546274F*(d.x*d.x-d.y*d.y)};
}
}

void AddShL2Direction(ShL2& sh,V fromDirection,V radiance) {
    const auto basis=ShBasis(fromDirection);
    for(std::size_t i=0;i<9;++i) sh.c[i]=Add(sh.c[i],Mul(radiance,basis[i]));
}

V EvaluateShL2Irradiance(const ShL2& sh,V normal) {
    // Cosine lobe weights per band (pi, 2pi/3, pi/4) over pi: the lightmap units carry no 1/pi.
    static constexpr float weight[9]{1.0F,2.0F/3,2.0F/3,2.0F/3,0.25F,0.25F,0.25F,0.25F,0.25F};
    const auto basis=ShBasis(Unit(normal)); V sum{0,0,0};
    for(std::size_t i=0;i<9;++i) sum=Add(sum,Mul(sh.c[i],weight[i]*basis[i]));
    return sum;
}

LightProbeGrid BakeLightProbes(std::span<const BakedLightmapSurface> surfaces,std::span<const LightmapLight> lights,
    V boundsMin,V boundsMax,const LightProbeSettings& settings) {
    LightProbeGrid grid;
    const auto fail=[&](const char* error) { grid.error=error; grid.probes.clear(); return grid; };
    if(surfaces.empty()||!Finite(boundsMin)||!Finite(boundsMax)||boundsMax.x<boundsMin.x||boundsMax.y<boundsMin.y||boundsMax.z<boundsMin.z||
        !std::isfinite(settings.spacing)||settings.spacing<0.05F||settings.maxProbes==0||settings.maxProbes>65536||
        !std::isfinite(settings.ambient)||settings.ambient<0||!std::isfinite(settings.rayBias)||settings.rayBias<=0||lights.size()>256)
        return fail("invalid light probe settings or bounds");
    std::vector<LightmapTriangle> geometry; std::vector<std::uint32_t> owner;
    for(std::size_t s=0;s<surfaces.size();++s) {
        const auto& surface=surfaces[s];
        if(surface.triangles.empty()||surface.resolution<4||surface.rgba.size()!=static_cast<std::size_t>(surface.resolution)*surface.resolution*4) return fail("invalid light probe surface");
        for(const auto& t:surface.triangles) for(const auto& v:t) if(!Finite(v.position)||!Finite(v.normal)||!std::isfinite(v.u)||!std::isfinite(v.v)) return fail("non-finite light probe geometry");
        geometry.insert(geometry.end(),surface.triangles.begin(),surface.triangles.end());
        owner.insert(owner.end(),surface.triangles.size(),static_cast<std::uint32_t>(s));
    }
    for(const auto& light:lights) if(!ValidLight(light)) return fail("invalid lightmap light");
    // Coarsen the spacing until the grid fits the probe budget.
    float spacing=settings.spacing; std::array<std::uint32_t,3> dims{};
    for(;;) {
        const V extent=Sub(boundsMax,boundsMin); const float length[]{extent.x,extent.y,extent.z}; double count=1;
        for(int axis=0;axis<3;++axis) { dims[static_cast<std::size_t>(axis)]=static_cast<std::uint32_t>(std::floor(length[axis]/spacing))+1U; count*=dims[static_cast<std::size_t>(axis)]; }
        if(count<=settings.maxProbes) break;
        spacing*=1.25F;
    }
    grid.origin=boundsMin; grid.spacing=spacing; grid.dimensions=dims;
    const std::size_t total=static_cast<std::size_t>(dims[0])*dims[1]*dims[2];
    grid.probes.assign(total,ShL2{});
    Bvh bvh; bvh.Build(geometry);
    std::vector<char> valid(total,1); std::size_t remaining=0;
    const auto linear=[](std::uint8_t byte) { return kb::math::SrgbToLinear(static_cast<float>(byte)/255.0F); };
    for(std::size_t index=0;index<total;++index) {
        const std::size_t x=index%dims[0], y=(index/dims[0])%dims[1], z=index/(static_cast<std::size_t>(dims[0])*dims[1]);
        const V p{boundsMin.x+static_cast<float>(x)*spacing,boundsMin.y+static_cast<float>(y)*spacing,boundsMin.z+static_cast<float>(z)*spacing};
        ShL2& sh=grid.probes[index]; unsigned backHits=0;
        for(unsigned ray=0;ray<kProbeRays;++ray) {
            // Fixed Hammersley set over the sphere (equal-area in z), so the bake is deterministic.
            const float h=1-2*(static_cast<float>(ray)+0.5F)/static_cast<float>(kProbeRays), r=std::sqrt(std::max(1-h*h,0.0F)), angle=2*kPi*RadicalInverse(ray);
            const V direction{r*std::cos(angle),h,r*std::sin(angle)};
            V radiance{settings.ambient,settings.ambient,settings.ambient};
            float distance=0,b1=0,b2=0; int hit=-1;
            if(bvh.Nearest(p,direction,1.0e20F,distance,hit,b1,b2)) {
                const auto& t=geometry[static_cast<std::size_t>(hit)]; const float b0=1-b1-b2;
                const V normal=Add(Add(Mul(t[0].normal,b0),Mul(t[1].normal,b1)),Mul(t[2].normal,b2));
                if(Dot(normal,direction)>0) { ++backHits; continue; }
                const auto& surface=surfaces[owner[static_cast<std::size_t>(hit)]]; const auto size=static_cast<std::size_t>(surface.resolution);
                const float hitU=t[0].u*b0+t[1].u*b1+t[2].u*b2, hitV=t[0].v*b0+t[1].v*b1+t[2].v*b2;
                const auto tx=std::min(static_cast<std::size_t>(std::max(hitU,0.0F)*static_cast<float>(size)),size-1);
                const auto ty=std::min(static_cast<std::size_t>(std::max(hitV,0.0F)*static_cast<float>(size)),size-1);
                const std::uint8_t* texel=surface.rgba.data()+(ty*size+tx)*4; const V albedo=Mean(t);
                radiance={linear(texel[0])*albedo.x,linear(texel[1])*albedo.y,linear(texel[2])*albedo.z};
            }
            AddShL2Direction(sh,direction,Mul(radiance,4*kPi/static_cast<float>(kProbeRays)));
        }
        if(backHits*4>kProbeRays) { valid[index]=0; ++remaining; continue; }
        for(const auto& light:lights) {
            // Light that reaches the probe unobstructed arrives from one direction: the cosine is left to the lookup.
            const V toward=light.directional?Mul(Unit(light.direction),-1):Unit(Sub(light.position,p));
            V direction; float distance=0; const float strength=DirectStrength(light,p,toward,direction,distance);
            if(strength<=0) continue;
            float blockerDistance=0,u=0,v=0; int blocker=-1;
            if(light.castsShadow&&bvh.Nearest(p,direction,light.directional?1.0e20F:distance-settings.rayBias,blockerDistance,blocker,u,v)) continue;
            AddShL2Direction(sh,direction,Mul(light.color,kPi*strength));
        }
    }
    grid.invalidProbes=static_cast<std::uint32_t>(remaining);
    if(remaining==total) return fail("every light probe lies inside geometry");
    // Probes inside geometry take the mean of their valid neighbours, growing inwards one layer per pass.
    for(std::size_t pass=0;pass<static_cast<std::size_t>(dims[0])+dims[1]+dims[2]&&remaining>0;++pass) {
        auto filled=valid; std::vector<std::pair<std::size_t,ShL2>> updates;
        for(std::size_t index=0;index<total;++index) {
            if(valid[index]) continue;
            const std::size_t x=index%dims[0], y=(index/dims[0])%dims[1], z=index/(static_cast<std::size_t>(dims[0])*dims[1]);
            const std::size_t neighbours[6]{x>0?index-1:total,x+1<dims[0]?index+1:total,y>0?index-dims[0]:total,y+1<dims[1]?index+dims[0]:total,
                z>0?index-static_cast<std::size_t>(dims[0])*dims[1]:total,z+1<dims[2]?index+static_cast<std::size_t>(dims[0])*dims[1]:total};
            ShL2 sum{}; float count=0;
            for(const auto other:neighbours) if(other<total&&valid[other]) { for(std::size_t i=0;i<9;++i) sum.c[i]=Add(sum.c[i],grid.probes[other].c[i]); ++count; }
            if(count>0) { for(auto& c:sum.c) c=Mul(c,1.0F/count); updates.emplace_back(index,sum); }
        }
        for(const auto& [index,sh] : updates) { grid.probes[index]=sh; filled[index]=1; --remaining; }
        valid.swap(filled);
    }
    return grid;
}

ShL2 SampleLightProbeGrid(const LightProbeGrid& grid,V position) {
    ShL2 result{};
    if(!grid.Succeeded()) return result;
    std::size_t base[3],next[3]; float fraction[3]; const float local[]{position.x-grid.origin.x,position.y-grid.origin.y,position.z-grid.origin.z};
    for(std::size_t axis=0;axis<3;++axis) {
        const float maximum=static_cast<float>(grid.dimensions[axis]-1), cell=std::clamp(local[axis]/grid.spacing,0.0F,maximum);
        base[axis]=std::min(static_cast<std::size_t>(cell),static_cast<std::size_t>(grid.dimensions[axis]-1)); next[axis]=std::min(base[axis]+1,static_cast<std::size_t>(grid.dimensions[axis]-1));
        fraction[axis]=cell-static_cast<float>(base[axis]);
    }
    for(unsigned corner=0;corner<8;++corner) {
        const std::size_t x=(corner&1U)?next[0]:base[0], y=(corner&2U)?next[1]:base[1], z=(corner&4U)?next[2]:base[2];
        const float weight=((corner&1U)?fraction[0]:1-fraction[0])*((corner&2U)?fraction[1]:1-fraction[1])*((corner&4U)?fraction[2]:1-fraction[2]);
        const ShL2& probe=grid.probes[(z*grid.dimensions[1]+y)*grid.dimensions[0]+x];
        for(std::size_t i=0;i<9;++i) result.c[i]=Add(result.c[i],Mul(probe.c[i],weight));
    }
    return result;
}

std::string SerializeLightProbeGrid(const LightProbeGrid& grid) {
    std::string bytes("KBLP",4);
    const auto put=[&](const void* data,std::size_t size) { bytes.append(static_cast<const char*>(data),size); };
    const std::uint32_t version=1; put(&version,4);
    const float header[]{grid.origin.x,grid.origin.y,grid.origin.z,grid.spacing}; put(header,sizeof(header));
    put(grid.dimensions.data(),sizeof(std::uint32_t)*3);
    for(const auto& probe:grid.probes) for(const auto& c:probe.c) { const float rgb[]{c.x,c.y,c.z}; put(rgb,sizeof(rgb)); }
    return bytes;
}

LightProbeGrid ParseLightProbeGrid(std::string_view bytes) {
    LightProbeGrid grid;
    const auto fail=[&](const char* error) { grid.error=error; grid.probes.clear(); return grid; };
    std::size_t at=0;
    const auto get=[&](void* data,std::size_t size) { if(bytes.size()-at<size) return false; std::memcpy(data,bytes.data()+at,size); at+=size; return true; };
    char magic[4]{}; std::uint32_t version=0; float header[4]{};
    if(!get(magic,4)||std::string_view(magic,4)!="KBLP"||!get(&version,4)||version!=1||!get(header,sizeof(header))||!get(grid.dimensions.data(),sizeof(std::uint32_t)*3))
        return fail("not a light probe grid");
    double total=1; for(const auto d:grid.dimensions) total*=d;
    if(total<1||total>1048576.0||!std::isfinite(header[0])||!std::isfinite(header[1])||!std::isfinite(header[2])||!std::isfinite(header[3])||header[3]<=0||
        bytes.size()-at!=static_cast<std::size_t>(total)*27*sizeof(float)) return fail("corrupt light probe grid");
    grid.origin={header[0],header[1],header[2]}; grid.spacing=header[3];
    grid.probes.resize(static_cast<std::size_t>(total));
    for(auto& probe:grid.probes) for(auto& c:probe.c) { float rgb[3]; get(rgb,sizeof(rgb)); c={rgb[0],rgb[1],rgb[2]}; }
    return grid;
}

std::string BakedLightingName(std::string_view text) {
    std::string out;
    for(const char c:text) out+=(std::isalnum(static_cast<unsigned char>(c))||c=='_'||c=='-')?c:'_';
    return out.empty()?"Entity":out;
}
std::string BakedLightingFolder(std::string_view sceneName) { return "/Game/Baked/"+BakedLightingName(sceneName); }
std::string LightProbeGridVirtualPath(std::string_view sceneName) { return BakedLightingFolder(sceneName)+"/LightProbes"+std::string{kLightProbeGridExtension}; }
} // namespace kb::render
