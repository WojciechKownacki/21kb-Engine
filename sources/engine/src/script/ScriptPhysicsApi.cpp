#include "engine/script/ScriptPhysicsApi.hpp"

#include "engine/math/EngineMath.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

// LIB-042: kb::math::Vec3 replaces this file's former private duplicate
// Vec3 (the exact "parallel set of vectors" the plan's section-5 header
// warns against) — kb::scene::Vec3 is itself now an alias to the same
// kb::math::Vec3, so no conversion is needed at the scene boundary either.
using kb::math::Vec3;
using kb::math::Abs;
using kb::math::Dot;
using kb::math::Length;
using kb::math::Max;
using kb::math::Normalize;

const ScriptValue* FindArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) {
    for (const ScriptFunctionArgument& argument : arguments) {
        if (argument.name == name) {
            return &argument.value;
        }
    }
    return nullptr;
}

[[nodiscard]] float FloatArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, float fallback) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? fallback : value->AsFloat(fallback);
}

[[nodiscard]] Vec3 VecArg(std::span<const ScriptFunctionArgument> arguments, std::string_view prefix, Vec3 fallback = {}) noexcept {
    return Vec3{
        FloatArg(arguments, std::string{ prefix } + "X", fallback.x),
        FloatArg(arguments, std::string{ prefix } + "Y", fallback.y),
        FloatArg(arguments, std::string{ prefix } + "Z", fallback.z),
    };
}

[[nodiscard]] kb::math::Quat QuatArg(std::span<const ScriptFunctionArgument> arguments, std::string_view prefix, kb::math::Quat fallback = {}) noexcept {
    return kb::math::Quat{
        FloatArg(arguments, std::string{ prefix } + "X", fallback.x),
        FloatArg(arguments, std::string{ prefix } + "Y", fallback.y),
        FloatArg(arguments, std::string{ prefix } + "Z", fallback.z),
        FloatArg(arguments, std::string{ prefix } + "W", fallback.w),
    };
}

[[nodiscard]] kb::scene::SceneEntity EntityArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? kb::scene::SceneEntity{} : kb::scene::SceneEntity{ value->AsUInt64() };
}

ScriptFunctionCallResult Error(std::string message) {
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { std::move(message) } };
}

ScriptFunctionCallResult BoolResult(std::string_view pin, bool value) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ std::string{ pin }, ScriptValue{ value } } },
        .errors = {},
    };
}

ScriptFunctionCallResult VectorResult(bool found, Vec3 value) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "found", ScriptValue{ found } },
            ScriptFunctionArgument{ "x", ScriptValue{ value.x } },
            ScriptFunctionArgument{ "y", ScriptValue{ value.y } },
            ScriptFunctionArgument{ "z", ScriptValue{ value.z } },
        },
        .errors = {},
    };
}

struct RaycastHit {
    bool hit = false;
    kb::scene::SceneEntity entity{};
    float distance = std::numeric_limits<float>::max();
    Vec3 point{};
    Vec3 normal{};
};

[[nodiscard]] bool IntersectSphere(Vec3 origin, Vec3 direction, float maxDistance, Vec3 center, float radius, float& distance, Vec3& normal) noexcept {
    const Vec3 oc = origin - center;
    const float b = Dot(oc, direction);
    const float c = Dot(oc, oc) - radius * radius;
    const float discriminant = b * b - c;
    if (discriminant < 0.0F) {
        return false;
    }
    const float root = std::sqrt(discriminant);
    float candidate = -b - root;
    if (candidate < 0.0F) {
        candidate = -b + root;
    }
    if (candidate < 0.0F || candidate > maxDistance) {
        return false;
    }
    distance = candidate;
    normal = Normalize((origin + direction * distance) - center);
    return true;
}

[[nodiscard]] bool IntersectAabb(Vec3 origin, Vec3 direction, float maxDistance, Vec3 center, Vec3 halfExtents, float& distance, Vec3& normal) noexcept {
    const Vec3 minimum = center - halfExtents;
    const Vec3 maximum = center + halfExtents;
    float tMin = 0.0F;
    float tMax = maxDistance;
    Vec3 hitNormal{};

    const auto testAxis = [&](float rayOrigin, float rayDirection, float minValue, float maxValue, Vec3 axisNormal) {
        if (std::abs(rayDirection) <= 0.000001F) {
            return rayOrigin >= minValue && rayOrigin <= maxValue;
        }
        float t1 = (minValue - rayOrigin) / rayDirection;
        float t2 = (maxValue - rayOrigin) / rayDirection;
        Vec3 normal1 = axisNormal * -1.0F;
        Vec3 normal2 = axisNormal;
        if (t1 > t2) {
            std::swap(t1, t2);
            std::swap(normal1, normal2);
        }
        if (t1 > tMin) {
            tMin = t1;
            hitNormal = normal1;
        }
        tMax = std::min(tMax, t2);
        return tMin <= tMax;
    };

    if (!testAxis(origin.x, direction.x, minimum.x, maximum.x, Vec3{ 1.0F, 0.0F, 0.0F }) ||
        !testAxis(origin.y, direction.y, minimum.y, maximum.y, Vec3{ 0.0F, 1.0F, 0.0F }) ||
        !testAxis(origin.z, direction.z, minimum.z, maximum.z, Vec3{ 0.0F, 0.0F, 1.0F })) {
        return false;
    }

    distance = tMin;
    normal = hitNormal;
    return distance >= 0.0F && distance <= maxDistance;
}

[[nodiscard]] bool IntersectCollider(
    Vec3 origin,
    Vec3 direction,
    float maxDistance,
    const kb::scene::ColliderComponent& collider,
    const kb::scene::TransformComponent& transform,
    float& distance,
    Vec3& normal) noexcept {
    const Vec3 scale = Max(Abs(transform.worldScale), Vec3{ 0.0001F, 0.0001F, 0.0001F });
    const Vec3 center = transform.worldPosition + Vec3{ collider.center.x * scale.x, collider.center.y * scale.y, collider.center.z * scale.z };
    switch (collider.shape) {
    case kb::scene::ColliderShape::Sphere: {
        const float radius = collider.radius * std::max({ scale.x, scale.y, scale.z });
        return IntersectSphere(origin, direction, maxDistance, center, radius, distance, normal);
    }
    case kb::scene::ColliderShape::Capsule: {
        const float radius = collider.radius * std::max(scale.x, scale.z);
        const float halfHeight = std::max(0.0F, collider.height * scale.y * 0.5F - radius);
        float bestDistance = std::numeric_limits<float>::max();
        Vec3 bestNormal{};
        bool hit = false;
        for (Vec3 sphereCenter : { center + Vec3{ 0.0F, halfHeight, 0.0F }, center - Vec3{ 0.0F, halfHeight, 0.0F } }) {
            float candidateDistance = 0.0F;
            Vec3 candidateNormal{};
            if (IntersectSphere(origin, direction, maxDistance, sphereCenter, radius, candidateDistance, candidateNormal) && candidateDistance < bestDistance) {
                bestDistance = candidateDistance;
                bestNormal = candidateNormal;
                hit = true;
            }
        }
        if (!hit) {
            return false;
        }
        distance = bestDistance;
        normal = bestNormal;
        return true;
    }
    case kb::scene::ColliderShape::Box:
        return IntersectAabb(origin, direction, maxDistance, center, Vec3{
            std::max(0.0001F, collider.boxSize.x * scale.x * 0.5F),
            std::max(0.0001F, collider.boxSize.y * scale.y * 0.5F),
            std::max(0.0001F, collider.boxSize.z * scale.z * 0.5F),
        }, distance, normal);
    }
    return false;
}

struct RaycastContext {
    kb::scene::Scene* scene = nullptr;
    Vec3 origin{};
    Vec3 direction{};
    float maxDistance = 0.0F;
    RaycastHit best{};
};

void RaycastVisitor(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, void* rawContext) {
    auto* context = static_cast<RaycastContext*>(rawContext);
    if (context == nullptr || context->scene == nullptr) {
        return;
    }
    const kb::scene::ColliderComponent* collider = context->scene->Components().Colliders().TryGet(entity);
    if (collider == nullptr) {
        return;
    }
    float distance = 0.0F;
    Vec3 normal{};
    if (IntersectCollider(context->origin, context->direction, context->maxDistance, *collider, transform, distance, normal) && distance < context->best.distance) {
        context->best = RaycastHit{
            .hit = true,
            .entity = entity,
            .distance = distance,
            .point = context->origin + context->direction * distance,
            .normal = normal,
        };
    }
}

ScriptFunctionCallResult Raycast(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("physics api requires an active scene");
    }
    const Vec3 origin = VecArg(arguments, "origin");
    const Vec3 direction = Normalize(VecArg(arguments, "direction", Vec3{ 0.0F, -1.0F, 0.0F }));
    const float maxDistance = std::max(0.0F, FloatArg(arguments, "distance", 1000.0F));
    if (Length(direction) <= 0.000001F || maxDistance <= 0.0F) {
        return Error("raycast direction or distance is invalid");
    }
    RaycastContext raycastContext{
        .scene = context.scene,
        .origin = origin,
        .direction = direction,
        .maxDistance = maxDistance,
        .best = {},
    };
    context.scene->Runtime().SynchronizeTransforms();
    context.scene->Transforms().ForEach(&RaycastVisitor, &raycastContext);
    const RaycastHit& hit = raycastContext.best;
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "hit", ScriptValue{ hit.hit } },
            ScriptFunctionArgument{ "entity", ScriptValue{ hit.entity.Id(), ScriptValueType::Entity } },
            ScriptFunctionArgument{ "distance", ScriptValue{ hit.hit ? hit.distance : 0.0F } },
            ScriptFunctionArgument{ "x", ScriptValue{ hit.hit ? hit.point.x : 0.0F } },
            ScriptFunctionArgument{ "y", ScriptValue{ hit.hit ? hit.point.y : 0.0F } },
            ScriptFunctionArgument{ "z", ScriptValue{ hit.hit ? hit.point.z : 0.0F } },
            ScriptFunctionArgument{ "normalX", ScriptValue{ hit.hit ? hit.normal.x : 0.0F } },
            ScriptFunctionArgument{ "normalY", ScriptValue{ hit.hit ? hit.normal.y : 0.0F } },
            ScriptFunctionArgument{ "normalZ", ScriptValue{ hit.hit ? hit.normal.z : 0.0F } },
        },
        .errors = {},
    };
}

// LIB-125: sphere/box/capsule CAST (swept, real physics-engine-backed
// query), unlike Raycast above which deliberately stays pure geometry - a
// hand-rolled swept-shape-vs-arbitrary-shape solver is a genuinely hard
// problem (see kb::scene::IPhysicsBackend::CastShape's own doc comment) the
// real backend already solves correctly, so LIB-125 routes through it
// instead of duplicating that math. Honest false/not-found when no physics
// backend is registered, same contract as every LIB-124 function.
[[nodiscard]] std::uint32_t LayerMaskArg(std::span<const ScriptFunctionArgument> arguments) noexcept {
    const ScriptValue* value = FindArg(arguments, "layerMask");
    return value == nullptr ? kb::scene::kPhysicsAllLayers : static_cast<std::uint32_t>(std::max(0, value->AsInt(static_cast<int>(kb::scene::kPhysicsAllLayers))));
}

ScriptFunctionCallResult CastShapeResult(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments, const kb::scene::PhysicsShapeDesc& shape) {
    if (context.scene == nullptr) {
        return Error("physics api requires an active scene");
    }
    const Vec3 origin = VecArg(arguments, "origin");
    const Vec3 direction = VecArg(arguments, "direction", Vec3{ 0.0F, -1.0F, 0.0F });
    const float maxDistance = std::max(0.0F, FloatArg(arguments, "distance", 1000.0F));
    const kb::scene::PhysicsCastResult hit = kb::scene::PhysicsBackend::CastShape(*context.scene, shape, origin, direction, maxDistance, LayerMaskArg(arguments));
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "hit", ScriptValue{ hit.hit } },
            ScriptFunctionArgument{ "entity", ScriptValue{ hit.entity.Id(), ScriptValueType::Entity } },
            ScriptFunctionArgument{ "distance", ScriptValue{ hit.hit ? hit.distance : 0.0F } },
            ScriptFunctionArgument{ "x", ScriptValue{ hit.hit ? hit.point.x : 0.0F } },
            ScriptFunctionArgument{ "y", ScriptValue{ hit.hit ? hit.point.y : 0.0F } },
            ScriptFunctionArgument{ "z", ScriptValue{ hit.hit ? hit.point.z : 0.0F } },
            ScriptFunctionArgument{ "normalX", ScriptValue{ hit.hit ? hit.normal.x : 0.0F } },
            ScriptFunctionArgument{ "normalY", ScriptValue{ hit.hit ? hit.normal.y : 0.0F } },
            ScriptFunctionArgument{ "normalZ", ScriptValue{ hit.hit ? hit.normal.z : 0.0F } },
        },
        .errors = {},
    };
}

ScriptFunctionCallResult OverlapShapeResult(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments, const kb::scene::PhysicsShapeDesc& shape) {
    if (context.scene == nullptr) {
        return Error("physics api requires an active scene");
    }
    const Vec3 center = VecArg(arguments, "center");
    const kb::scene::PhysicsOverlapResult overlap = kb::scene::PhysicsBackend::OverlapShape(*context.scene, shape, center, LayerMaskArg(arguments));
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "overlapping", ScriptValue{ overlap.overlapping } },
            ScriptFunctionArgument{ "entity", ScriptValue{ overlap.entity.Id(), ScriptValueType::Entity } },
        },
        .errors = {},
    };
}

ScriptFunctionCallResult SphereCast(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    return CastShapeResult(context, arguments, kb::scene::PhysicsShapeDesc{ .kind = kb::scene::PhysicsShapeKind::Sphere, .radius = std::max(0.0001F, FloatArg(arguments, "radius", 0.5F)) });
}

ScriptFunctionCallResult BoxCast(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    return CastShapeResult(context, arguments, kb::scene::PhysicsShapeDesc{ .kind = kb::scene::PhysicsShapeKind::Box, .boxHalfExtents = VecArg(arguments, "halfExtents", Vec3{ 0.5F, 0.5F, 0.5F }) });
}

ScriptFunctionCallResult CapsuleCast(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    return CastShapeResult(context, arguments,
        kb::scene::PhysicsShapeDesc{ .kind = kb::scene::PhysicsShapeKind::Capsule, .radius = std::max(0.0001F, FloatArg(arguments, "radius", 0.5F)), .height = std::max(0.0001F, FloatArg(arguments, "height", 2.0F)) });
}

ScriptFunctionCallResult OverlapSphere(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    return OverlapShapeResult(context, arguments, kb::scene::PhysicsShapeDesc{ .kind = kb::scene::PhysicsShapeKind::Sphere, .radius = std::max(0.0001F, FloatArg(arguments, "radius", 0.5F)) });
}

ScriptFunctionCallResult OverlapBox(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    return OverlapShapeResult(context, arguments, kb::scene::PhysicsShapeDesc{ .kind = kb::scene::PhysicsShapeKind::Box, .boxHalfExtents = VecArg(arguments, "halfExtents", Vec3{ 0.5F, 0.5F, 0.5F }) });
}

ScriptFunctionCallResult OverlapCapsule(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    return OverlapShapeResult(context, arguments,
        kb::scene::PhysicsShapeDesc{ .kind = kb::scene::PhysicsShapeKind::Capsule, .radius = std::max(0.0001F, FloatArg(arguments, "radius", 0.5F)), .height = std::max(0.0001F, FloatArg(arguments, "height", 2.0F)) });
}

ScriptFunctionCallResult ClosestPoint(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("physics api requires an active scene");
    }
    const kb::scene::PhysicsClosestPointResult result = kb::scene::PhysicsBackend::ClosestPoint(*context.scene, EntityArg(arguments, "entity"), VecArg(arguments, "point"));
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "found", ScriptValue{ result.found } },
            ScriptFunctionArgument{ "x", ScriptValue{ result.found ? result.point.x : 0.0F } },
            ScriptFunctionArgument{ "y", ScriptValue{ result.found ? result.point.y : 0.0F } },
            ScriptFunctionArgument{ "z", ScriptValue{ result.found ? result.point.z : 0.0F } },
            ScriptFunctionArgument{ "distance", ScriptValue{ result.found ? result.distance : 0.0F } },
        },
        .errors = {},
    };
}

// LIB-124: force/impulse/velocity/kinematic-move/sleep-wake are one-shot or
// instantaneous-read operations against whatever live physics simulation is
// actually running - unlike Raycast above (pure geometry against
// ColliderComponent/TransformComponent, no simulation needed), these MUST
// reach a real backend (kb::scene::PhysicsBackend, registered by whichever
// physics plugin is loaded - e.g. kb_physics_jolt_plugin) or fail honestly.
// Writing RigidbodyComponent.linearVelocity directly (as LIB-123's
// World.SetPropertyFloat already permits) is NOT equivalent to SetVelocity:
// for a live Dynamic body it is silently overwritten by the physics
// backend's own per-fixed-step write-back before it ever takes effect.
ScriptFunctionCallResult AddForce(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("physics api requires an active scene");
    }
    return BoolResult("applied", kb::scene::PhysicsBackend::AddForce(*context.scene, EntityArg(arguments, "entity"), VecArg(arguments, "force")));
}

ScriptFunctionCallResult AddImpulse(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("physics api requires an active scene");
    }
    return BoolResult("applied", kb::scene::PhysicsBackend::AddImpulse(*context.scene, EntityArg(arguments, "entity"), VecArg(arguments, "impulse")));
}

ScriptFunctionCallResult SetVelocity(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("physics api requires an active scene");
    }
    return BoolResult("applied", kb::scene::PhysicsBackend::SetVelocity(*context.scene, EntityArg(arguments, "entity"), VecArg(arguments, "velocity")));
}

ScriptFunctionCallResult GetVelocity(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("physics api requires an active scene");
    }
    const kb::scene::PhysicsVectorResult result = kb::scene::PhysicsBackend::GetVelocity(*context.scene, EntityArg(arguments, "entity"));
    return VectorResult(result.found, result.value);
}

ScriptFunctionCallResult SetAngularVelocity(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("physics api requires an active scene");
    }
    return BoolResult("applied", kb::scene::PhysicsBackend::SetAngularVelocity(*context.scene, EntityArg(arguments, "entity"), VecArg(arguments, "angularVelocity")));
}

ScriptFunctionCallResult GetAngularVelocity(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("physics api requires an active scene");
    }
    const kb::scene::PhysicsVectorResult result = kb::scene::PhysicsBackend::GetAngularVelocity(*context.scene, EntityArg(arguments, "entity"));
    return VectorResult(result.found, result.value);
}

ScriptFunctionCallResult MoveKinematic(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("physics api requires an active scene");
    }
    const float deltaSeconds = FloatArg(arguments, "deltaSeconds", context.deltaSeconds);
    const bool applied = kb::scene::PhysicsBackend::MoveKinematic(
        *context.scene, EntityArg(arguments, "entity"), VecArg(arguments, "target"), QuatArg(arguments, "rotation"), deltaSeconds);
    return BoolResult("applied", applied);
}

ScriptFunctionCallResult Sleep(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("physics api requires an active scene");
    }
    return BoolResult("applied", kb::scene::PhysicsBackend::Sleep(*context.scene, EntityArg(arguments, "entity")));
}

ScriptFunctionCallResult Wake(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("physics api requires an active scene");
    }
    return BoolResult("applied", kb::scene::PhysicsBackend::Wake(*context.scene, EntityArg(arguments, "entity")));
}

ScriptFunctionCallResult IsSleeping(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("physics api requires an active scene");
    }
    return BoolResult("sleeping", kb::scene::PhysicsBackend::IsSleeping(*context.scene, EntityArg(arguments, "entity")));
}

[[nodiscard]] bool RegisterVectorSetter(ScriptRuntimeHost& host, std::string name, std::string vectorPin, ScriptFunctionCallback callback) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = {
        ScriptFunctionPin{ "entity", ScriptValueType::Entity, true },
        ScriptFunctionPin{ vectorPin + "X", ScriptValueType::Float, true },
        ScriptFunctionPin{ vectorPin + "Y", ScriptValueType::Float, true },
        ScriptFunctionPin{ vectorPin + "Z", ScriptValueType::Float, true },
    };
    desc.signature.outputs = { ScriptFunctionPin{ "applied", ScriptValueType::Bool, true } };
    desc.callback = callback;
    return host.RegisterFunction(std::move(desc));
}

[[nodiscard]] bool RegisterVectorGetter(ScriptRuntimeHost& host, std::string name, ScriptFunctionCallback callback) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } };
    desc.signature.outputs = {
        ScriptFunctionPin{ "found", ScriptValueType::Bool, true },
        ScriptFunctionPin{ "x", ScriptValueType::Float, true },
        ScriptFunctionPin{ "y", ScriptValueType::Float, true },
        ScriptFunctionPin{ "z", ScriptValueType::Float, true },
    };
    desc.callback = callback;
    return host.RegisterFunction(std::move(desc));
}

[[nodiscard]] bool RegisterEntityBoolQuery(ScriptRuntimeHost& host, std::string name, std::string outputPin, ScriptFunctionCallback callback) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } };
    desc.signature.outputs = { ScriptFunctionPin{ std::move(outputPin), ScriptValueType::Bool, true } };
    desc.callback = callback;
    return host.RegisterFunction(std::move(desc));
}

// LIB-125: shape-parameter pin sets shared by the Cast/Overlap registration
// helpers below - kept as free functions (not inlined per call site) so
// SphereCast and OverlapSphere agree on "radius" exactly, etc.
[[nodiscard]] std::vector<ScriptFunctionPin> SphereShapePins() {
    return { ScriptFunctionPin{ "radius", ScriptValueType::Float, false } };
}

[[nodiscard]] std::vector<ScriptFunctionPin> BoxShapePins() {
    return {
        ScriptFunctionPin{ "halfExtentsX", ScriptValueType::Float, false },
        ScriptFunctionPin{ "halfExtentsY", ScriptValueType::Float, false },
        ScriptFunctionPin{ "halfExtentsZ", ScriptValueType::Float, false },
    };
}

[[nodiscard]] std::vector<ScriptFunctionPin> CapsuleShapePins() {
    return {
        ScriptFunctionPin{ "radius", ScriptValueType::Float, false },
        ScriptFunctionPin{ "height", ScriptValueType::Float, false },
    };
}

[[nodiscard]] bool RegisterCastFunction(ScriptRuntimeHost& host, std::string name, const std::vector<ScriptFunctionPin>& shapePins, ScriptFunctionCallback callback) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = {
        ScriptFunctionPin{ "originX", ScriptValueType::Float, true },
        ScriptFunctionPin{ "originY", ScriptValueType::Float, true },
        ScriptFunctionPin{ "originZ", ScriptValueType::Float, true },
        ScriptFunctionPin{ "directionX", ScriptValueType::Float, true },
        ScriptFunctionPin{ "directionY", ScriptValueType::Float, true },
        ScriptFunctionPin{ "directionZ", ScriptValueType::Float, true },
        ScriptFunctionPin{ "distance", ScriptValueType::Float, false },
    };
    desc.signature.inputs.insert(desc.signature.inputs.end(), shapePins.begin(), shapePins.end());
    desc.signature.inputs.push_back(ScriptFunctionPin{ "layerMask", ScriptValueType::Int, false });
    desc.signature.outputs = {
        ScriptFunctionPin{ "hit", ScriptValueType::Bool, true },
        ScriptFunctionPin{ "entity", ScriptValueType::Entity, true },
        ScriptFunctionPin{ "distance", ScriptValueType::Float, true },
        ScriptFunctionPin{ "x", ScriptValueType::Float, true },
        ScriptFunctionPin{ "y", ScriptValueType::Float, true },
        ScriptFunctionPin{ "z", ScriptValueType::Float, true },
        ScriptFunctionPin{ "normalX", ScriptValueType::Float, true },
        ScriptFunctionPin{ "normalY", ScriptValueType::Float, true },
        ScriptFunctionPin{ "normalZ", ScriptValueType::Float, true },
    };
    desc.callback = callback;
    return host.RegisterFunction(std::move(desc));
}

[[nodiscard]] bool RegisterOverlapFunction(ScriptRuntimeHost& host, std::string name, const std::vector<ScriptFunctionPin>& shapePins, ScriptFunctionCallback callback) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = {
        ScriptFunctionPin{ "centerX", ScriptValueType::Float, true },
        ScriptFunctionPin{ "centerY", ScriptValueType::Float, true },
        ScriptFunctionPin{ "centerZ", ScriptValueType::Float, true },
    };
    desc.signature.inputs.insert(desc.signature.inputs.end(), shapePins.begin(), shapePins.end());
    desc.signature.inputs.push_back(ScriptFunctionPin{ "layerMask", ScriptValueType::Int, false });
    desc.signature.outputs = {
        ScriptFunctionPin{ "overlapping", ScriptValueType::Bool, true },
        ScriptFunctionPin{ "entity", ScriptValueType::Entity, true },
    };
    desc.callback = callback;
    return host.RegisterFunction(std::move(desc));
}

} // namespace

bool ScriptPhysicsApi::Register(ScriptRuntimeHost& host) {
    ScriptFunctionDesc desc;
    desc.signature.name = "Physics.Raycast";
    desc.signature.inputs = {
        ScriptFunctionPin{ "originX", ScriptValueType::Float, true },
        ScriptFunctionPin{ "originY", ScriptValueType::Float, true },
        ScriptFunctionPin{ "originZ", ScriptValueType::Float, true },
        ScriptFunctionPin{ "directionX", ScriptValueType::Float, true },
        ScriptFunctionPin{ "directionY", ScriptValueType::Float, true },
        ScriptFunctionPin{ "directionZ", ScriptValueType::Float, true },
        ScriptFunctionPin{ "distance", ScriptValueType::Float, false },
    };
    desc.signature.outputs = {
        ScriptFunctionPin{ "hit", ScriptValueType::Bool, true },
        ScriptFunctionPin{ "entity", ScriptValueType::Entity, true },
        ScriptFunctionPin{ "distance", ScriptValueType::Float, true },
        ScriptFunctionPin{ "x", ScriptValueType::Float, true },
        ScriptFunctionPin{ "y", ScriptValueType::Float, true },
        ScriptFunctionPin{ "z", ScriptValueType::Float, true },
        ScriptFunctionPin{ "normalX", ScriptValueType::Float, true },
        ScriptFunctionPin{ "normalY", ScriptValueType::Float, true },
        ScriptFunctionPin{ "normalZ", ScriptValueType::Float, true },
    };
    desc.callback = &Raycast;
    bool ok = host.RegisterFunction(std::move(desc));

    ok = RegisterVectorSetter(host, "Physics.AddForce", "force", &AddForce) && ok;
    ok = RegisterVectorSetter(host, "Physics.AddImpulse", "impulse", &AddImpulse) && ok;
    ok = RegisterVectorSetter(host, "Physics.SetVelocity", "velocity", &SetVelocity) && ok;
    ok = RegisterVectorGetter(host, "Physics.GetVelocity", &GetVelocity) && ok;
    ok = RegisterVectorSetter(host, "Physics.SetAngularVelocity", "angularVelocity", &SetAngularVelocity) && ok;
    ok = RegisterVectorGetter(host, "Physics.GetAngularVelocity", &GetAngularVelocity) && ok;
    ok = RegisterEntityBoolQuery(host, "Physics.Sleep", "applied", &Sleep) && ok;
    ok = RegisterEntityBoolQuery(host, "Physics.Wake", "applied", &Wake) && ok;
    ok = RegisterEntityBoolQuery(host, "Physics.IsSleeping", "sleeping", &IsSleeping) && ok;

    ScriptFunctionDesc moveKinematicDesc;
    moveKinematicDesc.signature.name = "Physics.MoveKinematic";
    moveKinematicDesc.signature.inputs = {
        ScriptFunctionPin{ "entity", ScriptValueType::Entity, true },
        ScriptFunctionPin{ "targetX", ScriptValueType::Float, true },
        ScriptFunctionPin{ "targetY", ScriptValueType::Float, true },
        ScriptFunctionPin{ "targetZ", ScriptValueType::Float, true },
        ScriptFunctionPin{ "rotationX", ScriptValueType::Float, false },
        ScriptFunctionPin{ "rotationY", ScriptValueType::Float, false },
        ScriptFunctionPin{ "rotationZ", ScriptValueType::Float, false },
        ScriptFunctionPin{ "rotationW", ScriptValueType::Float, false },
        // Optional; defaults to this call's own frame delta (the same
        // deltaSeconds Jolt's MoveKinematic needs to derive a velocity from
        // the position delta) - a script normally never needs to pass this.
        ScriptFunctionPin{ "deltaSeconds", ScriptValueType::Float, false },
    };
    moveKinematicDesc.signature.outputs = { ScriptFunctionPin{ "applied", ScriptValueType::Bool, true } };
    moveKinematicDesc.callback = &MoveKinematic;
    ok = host.RegisterFunction(std::move(moveKinematicDesc)) && ok;

    ok = RegisterCastFunction(host, "Physics.SphereCast", SphereShapePins(), &SphereCast) && ok;
    ok = RegisterCastFunction(host, "Physics.BoxCast", BoxShapePins(), &BoxCast) && ok;
    ok = RegisterCastFunction(host, "Physics.CapsuleCast", CapsuleShapePins(), &CapsuleCast) && ok;
    ok = RegisterOverlapFunction(host, "Physics.OverlapSphere", SphereShapePins(), &OverlapSphere) && ok;
    ok = RegisterOverlapFunction(host, "Physics.OverlapBox", BoxShapePins(), &OverlapBox) && ok;
    ok = RegisterOverlapFunction(host, "Physics.OverlapCapsule", CapsuleShapePins(), &OverlapCapsule) && ok;

    ScriptFunctionDesc closestPointDesc;
    closestPointDesc.signature.name = "Physics.ClosestPoint";
    closestPointDesc.signature.inputs = {
        ScriptFunctionPin{ "entity", ScriptValueType::Entity, true },
        ScriptFunctionPin{ "pointX", ScriptValueType::Float, true },
        ScriptFunctionPin{ "pointY", ScriptValueType::Float, true },
        ScriptFunctionPin{ "pointZ", ScriptValueType::Float, true },
    };
    closestPointDesc.signature.outputs = {
        ScriptFunctionPin{ "found", ScriptValueType::Bool, true },
        ScriptFunctionPin{ "x", ScriptValueType::Float, true },
        ScriptFunctionPin{ "y", ScriptValueType::Float, true },
        ScriptFunctionPin{ "z", ScriptValueType::Float, true },
        ScriptFunctionPin{ "distance", ScriptValueType::Float, true },
    };
    closestPointDesc.callback = &ClosestPoint;
    ok = host.RegisterFunction(std::move(closestPointDesc)) && ok;

    return ok;
}

} // namespace kb::script
