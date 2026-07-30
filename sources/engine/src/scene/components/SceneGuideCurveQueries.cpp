#include "engine/scene/SceneGuideCurveQueries.hpp"

#include "engine/math/EngineMath.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <algorithm>
#include <cmath>

namespace kb::scene {
namespace {
[[nodiscard]] bool IsFinite(Vec3 v) noexcept { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
[[nodiscard]] Vec3 Add(Vec3 a, Vec3 b) noexcept { return { a.x+b.x, a.y+b.y, a.z+b.z }; }
[[nodiscard]] Vec3 Sub(Vec3 a, Vec3 b) noexcept { return { a.x-b.x, a.y-b.y, a.z-b.z }; }
[[nodiscard]] Vec3 Mul(Vec3 v, float s) noexcept { return { v.x*s, v.y*s, v.z*s }; }
[[nodiscard]] std::uint32_t PointIndex(const GuideCurveComponent& c, std::int32_t i) noexcept { const std::int32_t count=static_cast<std::int32_t>(c.controlPointCount); return c.closed ? static_cast<std::uint32_t>((i%count+count)%count) : static_cast<std::uint32_t>(std::clamp(i,0,count-1)); }
[[nodiscard]] Vec3 Catmull(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float t) noexcept { const float t2=t*t, t3=t2*t; return Mul(Add(Add(Mul(p1,2.F),Mul(Sub(p2,p0),t)),Add(Mul(Add(Sub(Mul(p0,2.F),Mul(p1,5.F)),Add(Mul(p2,4.F),Mul(p3,-1.F))),t2),Mul(Add(Add(Mul(p0,-1.F),Mul(p1,3.F)),Add(Mul(p2,-3.F),p3)),t3))),.5F); }
[[nodiscard]] Vec3 CatmullDerivative(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float t) noexcept { return Mul(Add(Sub(p2,p0),Add(Mul(Add(Sub(Mul(p0,2.F),Mul(p1,5.F)),Add(Mul(p2,4.F),Mul(p3,-1.F))),2.F*t),Mul(Add(Add(Mul(p0,-1.F),Mul(p1,3.F)),Add(Mul(p2,-3.F),p3)),3.F*t*t))),.5F); }
} // namespace

bool GuideCurveEvaluateLocal(const GuideCurveComponent& curve, float parameter, Vec3& position, Vec3& tangent) noexcept {
    if (!curve.enabled || !IsGuideCurveInterpolationValid(curve.interpolation) || !IsGuideCurveControlPointCountValid(curve.controlPointCount) || !std::isfinite(parameter)) return false;
    for (std::uint32_t i=0; i<curve.controlPointCount; ++i) if (!IsFinite(curve.controlPoints[i])) return false;
    const std::uint32_t segments=curve.closed ? curve.controlPointCount : curve.controlPointCount-1U;
    const float normalized=curve.closed ? parameter-std::floor(parameter) : std::clamp(parameter,0.F,1.F);
    const float scaled=normalized*static_cast<float>(segments);
    const std::uint32_t segment=std::min(static_cast<std::uint32_t>(scaled),segments-1U);
    const float local=(!curve.closed && normalized>=1.F) ? 1.F : scaled-static_cast<float>(segment);
    const Vec3 p0=curve.controlPoints[PointIndex(curve,static_cast<std::int32_t>(segment)-1)];
    const Vec3 p1=curve.controlPoints[PointIndex(curve,static_cast<std::int32_t>(segment))];
    const Vec3 p2=curve.controlPoints[PointIndex(curve,static_cast<std::int32_t>(segment)+1)];
    const Vec3 p3=curve.controlPoints[PointIndex(curve,static_cast<std::int32_t>(segment)+2)];
    if (curve.interpolation==GuideCurveInterpolation::Linear) { position=Add(p1,Mul(Sub(p2,p1),local)); tangent=kb::math::Normalize(Sub(p2,p1)); }
    else { position=Catmull(p0,p1,p2,p3,local); tangent=kb::math::Normalize(CatmullDerivative(p0,p1,p2,p3,local)); }
    return IsFinite(position) && IsFinite(tangent);
}

bool SceneGuideCurveEvaluate(const Scene& scene, SceneEntity entity, float parameter, Vec3& position, Vec3& tangent) noexcept {
    const GuideCurveComponent* curve=scene.Components().GuideCurves().TryGet(entity);
    const TransformComponent* transform=scene.Transforms().TryGet(entity);
    Vec3 localPosition{}, localTangent{};
    if (curve==nullptr || transform==nullptr || !GuideCurveEvaluateLocal(*curve,parameter,localPosition,localTangent) || !IsFinite(transform->worldPosition) || !IsFinite(transform->worldScale)) return false;
    position=Add(transform->worldPosition,kb::math::Rotate(transform->worldRotation,{localPosition.x*transform->worldScale.x,localPosition.y*transform->worldScale.y,localPosition.z*transform->worldScale.z}));
    tangent=kb::math::Normalize(kb::math::Rotate(transform->worldRotation,{localTangent.x*transform->worldScale.x,localTangent.y*transform->worldScale.y,localTangent.z*transform->worldScale.z}));
    return IsFinite(position) && IsFinite(tangent);
}
} // namespace kb::scene
