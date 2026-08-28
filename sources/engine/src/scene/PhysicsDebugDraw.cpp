#include "engine/scene/PhysicsDebugDraw.hpp"

#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/JointComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace kb::scene {
namespace {

using kb::math::kPi;
using kb::math::Quat;
using kb::math::Rotate;

constexpr Vec3 kColliderSolidColor{0.35F, 0.90F, 0.35F};
constexpr Vec3 kColliderTriggerColor{0.95F, 0.85F, 0.25F};
constexpr Vec3 kCharacterColor{0.30F, 0.75F, 0.95F};
constexpr Vec3 kJointColor{0.85F, 0.35F, 0.85F};
constexpr Vec3 kQueryHitColor{0.20F, 1.00F, 0.20F};
constexpr Vec3 kQueryMissColor{1.00F, 0.25F, 0.25F};
constexpr float kShapeAlpha = 0.85F;
constexpr float kJointAlpha = 0.90F;
constexpr std::uint32_t kCircleSegments = 24U;
constexpr std::uint32_t kCapsuleCapSegments = 8U;

[[nodiscard]] float NonZeroAbs(float value) noexcept {
    return std::max(std::fabs(value), 0.0001F);
}

void AppendLine(std::vector<PhysicsDebugLineDesc>& lines, Vec3 from, Vec3 to, Vec3 color, float alpha) {
    lines.push_back(PhysicsDebugLineDesc{.from = from, .to = to, .color = color, .alpha = alpha});
}

void AppendWireCircle(std::vector<PhysicsDebugLineDesc>& lines, Vec3 center, Vec3 axisA, Vec3 axisB, float radius, std::uint32_t segments, Vec3 color, float alpha) {
    for (std::uint32_t i = 0; i < segments; ++i) {
        const float a0 = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0F * kPi;
        const float a1 = (static_cast<float>(i + 1U) / static_cast<float>(segments)) * 2.0F * kPi;
        const Vec3 p0 = center + axisA * (std::cos(a0) * radius) + axisB * (std::sin(a0) * radius);
        const Vec3 p1 = center + axisA * (std::cos(a1) * radius) + axisB * (std::sin(a1) * radius);
        AppendLine(lines, p0, p1, color, alpha);
    }
}

void AppendWireArc(std::vector<PhysicsDebugLineDesc>& lines, Vec3 center, Vec3 axisA, Vec3 axisB, float radius, float startAngle, float endAngle, std::uint32_t segments, Vec3 color, float alpha) {
    for (std::uint32_t i = 0; i < segments; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(segments);
        const float t1 = static_cast<float>(i + 1U) / static_cast<float>(segments);
        const float a0 = startAngle + t0 * (endAngle - startAngle);
        const float a1 = startAngle + t1 * (endAngle - startAngle);
        const Vec3 p0 = center + axisA * (std::cos(a0) * radius) + axisB * (std::sin(a0) * radius);
        const Vec3 p1 = center + axisA * (std::cos(a1) * radius) + axisB * (std::sin(a1) * radius);
        AppendLine(lines, p0, p1, color, alpha);
    }
}

// LIB-132: sphere is rotation-invariant, so unlike box/capsule below this needs no entity
// rotation - only center+radius (already scale-adjusted by the caller) matter.
void AppendWireSphere(std::vector<PhysicsDebugLineDesc>& lines, Vec3 center, float radius, Vec3 color, float alpha) {
    AppendWireCircle(lines, center, Vec3{1.0F, 0.0F, 0.0F}, Vec3{0.0F, 1.0F, 0.0F}, radius, kCircleSegments, color, alpha);
    AppendWireCircle(lines, center, Vec3{1.0F, 0.0F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}, radius, kCircleSegments, color, alpha);
    AppendWireCircle(lines, center, Vec3{0.0F, 1.0F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}, radius, kCircleSegments, color, alpha);
}

void AppendWireBox(std::vector<PhysicsDebugLineDesc>& lines, Vec3 center, Vec3 halfExtents, Quat rotation, Vec3 color, float alpha) {
    std::array<Vec3, 8U> corners{};
    std::size_t index = 0U;
    for (const float sx : {-1.0F, 1.0F}) {
        for (const float sy : {-1.0F, 1.0F}) {
            for (const float sz : {-1.0F, 1.0F}) {
                const Vec3 local{sx * halfExtents.x, sy * halfExtents.y, sz * halfExtents.z};
                corners[index] = center + Rotate(rotation, local);
                ++index;
            }
        }
    }
    // corners[i] indexes (sx,sy,sz) as bits 2,1,0 of i (0=-1, 1=+1 per axis).
    constexpr std::array<std::array<int, 2>, 12U> edges{{
        {0, 1}, {2, 3}, {4, 5}, {6, 7}, // vary sz
        {0, 2}, {1, 3}, {4, 6}, {5, 7}, // vary sy
        {0, 4}, {1, 5}, {2, 6}, {3, 7}, // vary sx
    }};
    for (const std::array<int, 2>& edge : edges) {
        AppendLine(lines, corners[static_cast<std::size_t>(edge[0])], corners[static_cast<std::size_t>(edge[1])], color, alpha);
    }
}

// LIB-132: cylinder body (2 equatorial circles + 4 side lines) plus two hemisphere caps (4
// quarter-arcs each) - a standard, proportionate capsule wireframe (the conventional
// capsule debug draw), not full lat/long tessellation.
void AppendWireCapsule(std::vector<PhysicsDebugLineDesc>& lines, Vec3 center, float radius, float halfCylinder, Quat rotation, Vec3 color, float alpha) {
    const Vec3 up = Rotate(rotation, Vec3{0.0F, 1.0F, 0.0F});
    const Vec3 right = Rotate(rotation, Vec3{1.0F, 0.0F, 0.0F});
    const Vec3 forward = Rotate(rotation, Vec3{0.0F, 0.0F, 1.0F});
    const Vec3 negRight = right * -1.0F;
    const Vec3 negForward = forward * -1.0F;
    const Vec3 negUp = up * -1.0F;
    const Vec3 top = center + up * halfCylinder;
    const Vec3 bottom = center - up * halfCylinder;

    AppendWireCircle(lines, top, right, forward, radius, kCircleSegments, color, alpha);
    AppendWireCircle(lines, bottom, right, forward, radius, kCircleSegments, color, alpha);
    for (const Vec3& side : {right, negRight, forward, negForward}) {
        AppendLine(lines, top + side * radius, bottom + side * radius, color, alpha);
    }

    for (const Vec3& axisA : {right, negRight, forward, negForward}) {
        AppendWireArc(lines, top, axisA, up, radius, 0.0F, kPi * 0.5F, kCapsuleCapSegments, color, alpha);
        AppendWireArc(lines, bottom, axisA, negUp, radius, 0.0F, kPi * 0.5F, kCapsuleCapSegments, color, alpha);
    }
}

using CollectContext = std::pair<const Scene*, std::vector<PhysicsDebugLineDesc>*>;

void CollectShapesVisitor(SceneEntity entity, const TransformComponent& transform, void* rawContext) {
    auto* context = static_cast<CollectContext*>(rawContext);
    const Scene& scene = *context->first;
    std::vector<PhysicsDebugLineDesc>& output = *context->second;

    const float scaleX = NonZeroAbs(transform.worldScale.x);
    const float scaleY = NonZeroAbs(transform.worldScale.y);
    const float scaleZ = NonZeroAbs(transform.worldScale.z);

    // LIB-132: center is added UNROTATED, matching JoltPhysicsSceneSystem::CreateBody's own
    // `bodyPosition = transform.worldPosition + collider.center` exactly - the debug shape
    // must show what is actually simulated, including this existing, documented
    // simplification, not a "corrected" version of it.
    if (const ColliderComponent* collider = scene.Components().Colliders().TryGet(entity)) {
        const Vec3 color = collider->trigger ? kColliderTriggerColor : kColliderSolidColor;
        const Vec3 center = transform.worldPosition + collider->center;
        switch (collider->shape) {
        case ColliderShape::Sphere:
            AppendWireSphere(output, center, collider->radius * std::max({ scaleX, scaleY, scaleZ }), color, kShapeAlpha);
            break;
        case ColliderShape::Capsule: {
            const float radius = collider->radius * std::max(scaleX, scaleZ);
            const float height = collider->height * scaleY;
            AppendWireCapsule(output, center, radius, std::max(0.0F, (height * 0.5F) - radius), transform.worldRotation, color, kShapeAlpha);
            break;
        }
        case ColliderShape::Box:
            AppendWireBox(
                output, center,
                Vec3{ collider->boxSize.x * scaleX * 0.5F, collider->boxSize.y * scaleY * 0.5F, collider->boxSize.z * scaleZ * 0.5F },
                transform.worldRotation, color, kShapeAlpha);
            break;
        }
    }

    // LIB-132: unlike Collider above, CharacterControllerComponent's center IS rotated -
    // matches JPH::CharacterVirtual's own mShapeOffset formula exactly (see LIB-131's
    // GetCenterOfMassPosition: `mPosition + mRotation * (mShapeOffset + ...)`).
    if (const CharacterControllerComponent* character = scene.Components().CharacterControllers().TryGet(entity)) {
        const Vec3 center = transform.worldPosition + Rotate(transform.worldRotation, character->center);
        const float radius = character->radius * std::max(scaleX, scaleZ);
        const float height = character->height * scaleY;
        AppendWireCapsule(output, center, radius, std::max(0.0F, (height * 0.5F) - radius), transform.worldRotation, kCharacterColor, kShapeAlpha);
    }

    if (const JointComponent* joint = scene.Components().Joints().TryGet(entity)) {
        const Vec3 ownerAnchor = transform.worldPosition + Rotate(transform.worldRotation, joint->anchor);
        Vec3 connectedAnchor = joint->connectedAnchor; // World position already when connectedEntity is invalid (LIB-130).
        if (joint->connectedEntity.IsValid()) {
            if (const TransformComponent* connectedTransform = scene.Transforms().TryGet(joint->connectedEntity)) {
                connectedAnchor = connectedTransform->worldPosition + Rotate(connectedTransform->worldRotation, joint->connectedAnchor);
            }
        }
        AppendLine(output, ownerAnchor, connectedAnchor, kJointColor, kJointAlpha);
    }
}

} // namespace

void PhysicsDebugDraw::SetEnabled(Scene& scene, bool enabled) noexcept {
    SceneAccess::State(scene).physicsDebugDrawEnabled = enabled;
}

bool PhysicsDebugDraw::IsEnabled(const Scene& scene) noexcept {
    return SceneAccess::State(scene).physicsDebugDrawEnabled;
}

void PhysicsDebugDraw::RecordQueryTrace(Scene& scene, PhysicsDebugQueryTrace trace) noexcept {
    if (!SceneAccess::State(scene).physicsDebugDrawEnabled) {
        return;
    }
    SceneAccess::State(scene).physicsDebugQueryTrace = trace;
}

PhysicsDebugQueryTrace PhysicsDebugDraw::QueryTrace(const Scene& scene) noexcept {
    return SceneAccess::State(scene).physicsDebugQueryTrace;
}

std::vector<PhysicsDebugLineDesc> PhysicsDebugDraw::CollectLines(const Scene& scene) {
    std::vector<PhysicsDebugLineDesc> lines;
    CollectContext context{ &scene, &lines };
    scene.Transforms().ForEach(&CollectShapesVisitor, &context);

    const PhysicsDebugQueryTrace trace = QueryTrace(scene);
    if (trace.valid) {
        const Vec3 color = trace.hit ? kQueryHitColor : kQueryMissColor;
        AppendLine(lines, trace.origin, trace.endpoint, color, 1.0F);
        if (trace.hit) {
            AppendLine(lines, trace.endpoint, trace.endpoint + trace.normal * 0.3F, kQueryHitColor, 1.0F);
        }
    }
    return lines;
}

} // namespace kb::scene
