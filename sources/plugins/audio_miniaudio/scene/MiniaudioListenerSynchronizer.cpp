#include "scene/MiniaudioListenerSynchronizer.hpp"

#include "engine/math/EngineMath.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
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

[[nodiscard]] kb::scene::Vec3 NormalizeOrForward(kb::scene::Vec3 value) noexcept {
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z;
    if (lengthSquared <= 0.000001F) {
        return kb::scene::Vec3{ 0.0F, 0.0F, 1.0F };
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
    if (listener == nullptr || !listener->enabled) {
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

} // namespace

void MiniaudioListenerSynchronizer::Sync(ma_engine& engine, kb::scene::SceneSystemContext& context) const {
    ListenerScan scan{ .scene = &context.GetScene() };
    context.Transforms().ForEach(&ScanListener, &scan);

    if (!scan.found) {
        ma_engine_listener_set_enabled(&engine, 0U, MA_FALSE);
        return;
    }

    const kb::scene::Vec3 position = scan.transform.worldPosition;
    const kb::scene::Vec3 direction = NormalizeOrForward(Rotate(scan.transform.worldRotation, kb::scene::Vec3{ 0.0F, 0.0F, 1.0F }));
    ma_engine_listener_set_enabled(&engine, 0U, MA_TRUE);
    ma_engine_listener_set_position(&engine, 0U, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&engine, 0U, direction.x, direction.y, direction.z);
    ma_engine_listener_set_world_up(&engine, 0U, 0.0F, 1.0F, 0.0F);
}

} // namespace kb::audio_miniaudio
