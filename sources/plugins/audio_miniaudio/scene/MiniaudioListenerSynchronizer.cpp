#include "scene/MiniaudioListenerSynchronizer.hpp"

#include "engine/math/EngineMath.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cmath>
#include <cstdint>

namespace kb::audio_miniaudio {
namespace {

// LIB-042/LIB-043: kb::scene::Vec3/Quat are now aliases to kb::math::Vec3/
// Quat (see TransformComponent.hpp), which already provides Add
// (operator+), Cross, and Rotate — this file's own copies used to be a
// second definition (this Rotate formula assumed a unit-length quaternion,
// which every caller here already passes, so it's numerically the same as
// kb::math::Rotate's general formula) and would be ambiguous overloads via
// ADL against kb::math's. Scale has no kb::math equivalent yet, so it
// stays local.
using kb::math::Rotate;

[[nodiscard]] kb::scene::Vec3 Scale(kb::scene::Vec3 value, float scale) noexcept {
    return kb::scene::Vec3{ value.x * scale, value.y * scale, value.z * scale };
}

[[nodiscard]] kb::scene::Vec3 NormalizeOr(kb::scene::Vec3 value, kb::scene::Vec3 fallback) noexcept {
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z;
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.000001F) {
        return fallback;
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    return Scale(value, inverseLength);
}

struct ListenerScan {
    kb::scene::Scene* scene = nullptr;
    bool found = false;
    std::int32_t priority = 0;
    bool primary = false;
    kb::scene::SceneEntity entity{};
    kb::scene::TransformComponent transform{};
};

void ScanListener(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, void* rawContext) {
    auto* scan = static_cast<ListenerScan*>(rawContext);
    const kb::scene::AudioListenerComponent* listener = scan->scene->Components().AudioListeners().TryGet(entity);
    if (listener == nullptr || !listener->enabled || !scan->scene->Entities().IsActive(entity)) {
        return;
    }
    const bool preferred = !scan->found || listener->priority > scan->priority ||
        (listener->priority == scan->priority && listener->primary && !scan->primary) ||
        (listener->priority == scan->priority && listener->primary == scan->primary && entity < scan->entity);
    if (preferred) {
        scan->found = true;
        scan->priority = listener->priority;
        scan->primary = listener->primary;
        scan->entity = entity;
        scan->transform = transform;
    }
}

[[nodiscard]] float FiniteOrZero(float value) noexcept {
    return std::isfinite(value) ? value : 0.0F;
}

[[nodiscard]] kb::scene::Vec3 FiniteOrZero(kb::scene::Vec3 value) noexcept {
    return { FiniteOrZero(value.x), FiniteOrZero(value.y), FiniteOrZero(value.z) };
}

} // namespace

MiniaudioListenerSynchronizer::State MiniaudioListenerSynchronizer::Sync(ma_engine& engine, kb::scene::SceneSystemContext& context) {
    ListenerScan scan{ .scene = &context.GetScene() };
    context.Transforms().ForEach(&ScanListener, &scan);

    if (!scan.found) {
        Disable(engine);
        return {};
    }

    const kb::scene::Vec3 position = FiniteOrZero(scan.transform.worldPosition);
    const kb::scene::Vec3 direction = NormalizeOr(
        Rotate(scan.transform.worldRotation, kb::scene::Vec3{ 0.0F, 0.0F, 1.0F }),
        kb::scene::Vec3{ 0.0F, 0.0F, 1.0F });
    const kb::scene::Vec3 up = NormalizeOr(
        Rotate(scan.transform.worldRotation, kb::scene::Vec3{ 0.0F, 1.0F, 0.0F }),
        kb::scene::Vec3{ 0.0F, 1.0F, 0.0F });
    kb::scene::Vec3 velocity{};
    const float deltaSeconds = context.DeltaSeconds();
    if (hasPreviousPosition_ && previousEntity_ == scan.entity && std::isfinite(deltaSeconds) && deltaSeconds > 0.0F) {
        velocity = kb::scene::Vec3{
            (position.x - previousPosition_.x) / deltaSeconds,
            (position.y - previousPosition_.y) / deltaSeconds,
            (position.z - previousPosition_.z) / deltaSeconds,
        };
        velocity = FiniteOrZero(velocity);
    }
    ma_engine_listener_set_enabled(&engine, 0U, MA_TRUE);
    ma_engine_listener_set_position(&engine, 0U, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&engine, 0U, direction.x, direction.y, direction.z);
    ma_engine_listener_set_world_up(&engine, 0U, up.x, up.y, up.z);
    ma_engine_listener_set_velocity(&engine, 0U, velocity.x, velocity.y, velocity.z);
    previousEntity_ = scan.entity;
    previousPosition_ = position;
    hasPreviousPosition_ = true;
    return State{ .active = true, .position = position };
}

void MiniaudioListenerSynchronizer::Disable(ma_engine& engine) noexcept {
    ma_engine_listener_set_enabled(&engine, 0U, MA_FALSE);
    ma_engine_listener_set_velocity(&engine, 0U, 0.0F, 0.0F, 0.0F);
    Reset();
}

void MiniaudioListenerSynchronizer::Reset() noexcept {
    previousEntity_ = {};
    previousPosition_ = {};
    hasPreviousPosition_ = false;
}

} // namespace kb::audio_miniaudio
