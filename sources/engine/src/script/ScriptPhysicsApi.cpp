#include "engine/script/ScriptPhysicsApi.hpp"

#include "engine/math/EngineMath.hpp"
#include "engine/scene/ColliderComponent.hpp"
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

ScriptFunctionCallResult Error(std::string message) {
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { std::move(message) } };
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
    return host.RegisterFunction(std::move(desc));
}

} // namespace kb::script
