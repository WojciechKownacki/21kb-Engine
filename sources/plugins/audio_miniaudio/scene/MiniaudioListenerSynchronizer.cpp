#include "scene/MiniaudioListenerSynchronizer.hpp"

#include "engine/math/EngineMath.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cmath>

namespace kb::audio_miniaudio {
namespace {

// LIB-042: kb::scene::Vec3 is now an alias to kb::math::Vec3 (see
// TransformComponent.hpp), which already provides Add (operator+) — this
// file's own copy would now be an ambiguous overload via ADL against
// kb::math's. Cross/Scale have no kb::math equivalent yet, so they stay
// local.
[[nodiscard]] kb::scene::Vec3 Cross(kb::scene::Vec3 lhs, kb::scene::Vec3 rhs) noexcept {
    return kb::scene::Vec3{
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

[[nodiscard]] kb::scene::Vec3 Scale(kb::scene::Vec3 value, float scale) noexcept {
    return kb::scene::Vec3{ value.x * scale, value.y * scale, value.z * scale };
}

[[nodiscard]] kb::scene::Vec3 Rotate(kb::scene::Quat rotation, kb::scene::Vec3 value) noexcept {
    const kb::scene::Vec3 axis{ rotation.x, rotation.y, rotation.z };
    const kb::scene::Vec3 twiceCross = Scale(Cross(axis, value), 2.0F);
    return value + Scale(twiceCross, rotation.w) + Cross(axis, twiceCross);
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
    bool primary = false;
    kb::scene::TransformComponent transform{};
};

void ScanListener(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, void* rawContext) {
    auto* scan = static_cast<ListenerScan*>(rawContext);
    const kb::scene::AudioListenerComponent* listener = scan->scene->Components().AudioListeners().TryGet(entity);
    if (listener == nullptr || !listener->enabled) {
        return;
    }
    if (!scan->found || (!scan->primary && listener->primary)) {
        scan->found = true;
        scan->primary = listener->primary;
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
