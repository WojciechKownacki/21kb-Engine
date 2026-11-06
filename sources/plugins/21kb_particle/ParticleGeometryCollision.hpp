#pragma once

#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/ParticleEffectAssetSchema.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace kb::particle_plugin {
namespace geometry_collision {
using Vec3=kb::math::Vec3;
using Grid=kb::scene::ParticleCollisionGeometryModule;
struct Sample {float distance=std::numeric_limits<float>::infinity();Vec3 gradient{0,1,0};};

inline Sample At(const Grid& g,Vec3 p) noexcept {
    const Vec3 q{(p.x-g.origin.x)/g.cellSize.x,(p.y-g.origin.y)/g.cellSize.y,(p.z-g.origin.z)/g.cellSize.z};
    const bool heightfield=g.kind==kb::scene::ParticleCollisionGeometryKind::Heightfield;
    if(q.x<0||q.z<0||q.x>static_cast<float>(g.width-1)||q.z>static_cast<float>(g.depth-1)||
        (!heightfield&&(q.y<0||q.y>static_cast<float>(g.height-1)))) return {};
    const auto x=std::min(static_cast<std::uint32_t>(q.x),g.width-2),z=std::min(static_cast<std::uint32_t>(q.z),g.depth-2);
    const float u=q.x-static_cast<float>(x),w=q.z-static_cast<float>(z);
    const auto lerp=[](float a,float b,float t){return a+(b-a)*t;};
    const auto value=[&](std::uint32_t xx,std::uint32_t yy,std::uint32_t zz){return g.samples[xx+g.width*(yy+g.height*zz)];};
    if(heightfield) {
        const float a=value(x,0,z),b=value(x+1,0,z),c=value(x,0,z+1),d=value(x+1,0,z+1);
        const Vec3 normal{-(lerp(b-a,d-c,w)/g.cellSize.x),1,-(lerp(c-a,d-b,u)/g.cellSize.z)};
        const float length=kb::math::Length(normal);
        return {(p.y-g.origin.y-lerp(lerp(a,b,u),lerp(c,d,u),w))/length,normal*(1/length)};
    }
    const auto y=std::min(static_cast<std::uint32_t>(q.y),g.height-2);const float v=q.y-static_cast<float>(y);
    float c[2][2][2];for(unsigned zz=0;zz<2;++zz) for(unsigned yy=0;yy<2;++yy) for(unsigned xx=0;xx<2;++xx) c[zz][yy][xx]=value(x+xx,y+yy,z+zz);
    const float a=lerp(c[0][0][0],c[0][0][1],u),b=lerp(c[0][1][0],c[0][1][1],u),cc=lerp(c[1][0][0],c[1][0][1],u),d=lerp(c[1][1][0],c[1][1][1],u);
    const Vec3 gradient{
        lerp(lerp(c[0][0][1]-c[0][0][0],c[0][1][1]-c[0][1][0],v),lerp(c[1][0][1]-c[1][0][0],c[1][1][1]-c[1][1][0],v),w)/g.cellSize.x,
        lerp(b-a,d-cc,w)/g.cellSize.y,
        (lerp(cc,d,v)-lerp(a,b,v))/g.cellSize.z};
    return {lerp(lerp(a,b,v),lerp(cc,d,v),w),gradient};
}

// Particle positions and velocities already use world axes, including local
// emitters whose owner deltas are applied by the existing simulation stage.
inline bool Resolve(kb::scene::Scene* scene,const Grid& g,Vec3 from,Vec3& to,const kb::math::DVec3& origin,Vec3& normal) noexcept {
    const auto travel=to-from;const float distance=kb::math::Length(travel);
    if(g.kind==kb::scene::ParticleCollisionGeometryKind::PhysicsWorld) {
        if(!scene) return false;
        const kb::scene::PhysicsShapeDesc shape{.kind=kb::scene::PhysicsShapeKind::Sphere,.radius=g.radius};
        if(distance>1.0e-7F) {
            const auto hit=kb::scene::PhysicsBackend::CastShapePrecise(*scene,shape,origin+kb::math::ToDVec3(from),travel*(1/distance),distance,g.layerMask);
            if(hit.hit) {normal=hit.normal;to=from+travel*(std::clamp(hit.distance,0.0F,distance)/distance)+normal*0.001F;return true;}
        }
        const auto world=origin+kb::math::ToDVec3(to);
        const auto overlap=kb::scene::PhysicsBackend::OverlapShapePrecise(*scene,shape,world,g.layerMask);
        if(!overlap.overlapping) return false;
        const auto closest=kb::scene::PhysicsBackend::ClosestPointPrecise(*scene,overlap.entity,world,g.layerMask);
        if(!closest.found) return false;
        const auto outward=kb::math::ToVec3(world-closest.worldPoint);const float length=kb::math::Length(outward);
        normal=length>1.0e-6F?outward*(1/length):Vec3{0,1,0};
        to=to+normal*(overlap.penetrationDepth+0.001F);return true;
    }
    // Sample the segment at a quarter-cell spacing. The explicit 256-step cap
    // bounds work; thin features need enough grid samples and bounded travel.
    const float cell=std::min({g.cellSize.x,g.cellSize.y,g.cellSize.z});
    const auto count=static_cast<unsigned>(std::clamp(std::ceil(distance/(cell*0.25F)),1.0F,256.0F));
    for(unsigned i=0;i<=count;++i) {
        Vec3 point=from+travel*(static_cast<float>(i)/static_cast<float>(count));
        auto sample=At(g,kb::math::ToVec3(origin+kb::math::ToDVec3(point)));
        if(sample.distance>=g.radius) continue;
        for(unsigned iteration=0;iteration<6;++iteration) {
            const float length=kb::math::Length(sample.gradient);
            normal=length>1.0e-6F?sample.gradient*(1/length):Vec3{0,1,0};
            point=point+normal*(g.radius-sample.distance+0.001F);
            sample=At(g,kb::math::ToVec3(origin+kb::math::ToDVec3(point)));
            if(sample.distance>=g.radius-0.0001F) break;
        }
        to=point;return true;
    }
    return false;
}
} // namespace geometry_collision
} // namespace kb::particle_plugin
