#pragma once

#include "engine/math/DVec3.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cmath>

namespace kb::audio_miniaudio {

// The mixer spatialises in float: listener, sources and voices are given to it relative to an audio origin near
// the listener (docs/large_worlds.md), so spatialisation and doppler stay exact far from the world origin. The
// origin stays put until the listener is further than kRebaseDistance from it, then moves to the listener rounded
// to kGridStep; positions kept between ticks (previous positions, free-standing voices) move with it.
class MiniaudioAudioSpace final {
public:
    static constexpr double kRebaseDistance = 1024.0;
    static constexpr double kGridStep = 1024.0;

    [[nodiscard]] const kb::math::DVec3& Origin() const noexcept { return origin_; }

    // Follows the listener at `listener`; returns how audio-space positions kept from earlier ticks move (the old
    // origin minus the new one, zero while the origin stays).
    kb::scene::Vec3 Follow(const kb::math::DVec3& listener) noexcept {
        if (!std::isfinite(listener.x) || !std::isfinite(listener.y) || !std::isfinite(listener.z) ||
            (std::abs(listener.x - origin_.x) <= kRebaseDistance && std::abs(listener.y - origin_.y) <= kRebaseDistance &&
                std::abs(listener.z - origin_.z) <= kRebaseDistance)) {
            return kb::scene::Vec3{};
        }
        const kb::math::DVec3 previous = origin_;
        origin_ = kb::math::DVec3{ std::round(listener.x / kGridStep) * kGridStep, std::round(listener.y / kGridStep) * kGridStep,
            std::round(listener.z / kGridStep) * kGridStep };
        return kb::math::ToVec3(previous - origin_);
    }

    [[nodiscard]] kb::scene::Vec3 ToAudio(const kb::math::DVec3& world) const noexcept {
        return kb::math::RelativeTo(world, origin_);
    }

    [[nodiscard]] kb::math::DVec3 ToWorld(kb::scene::Vec3 audio) const noexcept {
        return origin_ + audio;
    }

    void Reset() noexcept { origin_ = kb::math::DVec3{}; }

private:
    kb::math::DVec3 origin_{};
};

// The audio-space position of an entity's transform; world space (origin at zero) without an audio space.
[[nodiscard]] inline kb::scene::Vec3 AudioSpacePosition(const MiniaudioAudioSpace* space, const kb::scene::Scene& scene,
    kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform) noexcept {
    if (space == nullptr) return transform.worldPosition;
    return space->ToAudio(scene.Transforms().WorldTranslation(entity, transform));
}

} // namespace kb::audio_miniaudio
