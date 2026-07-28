#pragma once

#include "engine/scene/AnimationAssets.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace kb::scene {

class Scene;

class SceneAnimatorQueries {
public:
    explicit SceneAnimatorQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Exists(SceneEntity entity) const noexcept;
    [[nodiscard]] std::uint64_t Controller(SceneEntity entity) const noexcept;
    [[nodiscard]] std::span<const AnimatorParameterValue> Parameters(SceneEntity entity) const noexcept;
    [[nodiscard]] float Speed(SceneEntity entity) const noexcept;
    [[nodiscard]] std::optional<AnimatorStateInfo> State(SceneEntity entity, std::string_view layer) const;

private:
    const Scene& scene_;
};

class SceneAnimators {
public:
    static constexpr std::size_t kMaxPendingEvents = 4096U;
    explicit SceneAnimators(Scene& scene) noexcept;
    [[nodiscard]] bool Exists(SceneEntity entity) const noexcept;
    [[nodiscard]] std::uint64_t Controller(SceneEntity entity) const noexcept;
    [[nodiscard]] std::span<const AnimatorParameterValue> Parameters(SceneEntity entity) const noexcept;
    [[nodiscard]] bool Play(SceneEntity entity, std::string_view layer, std::string_view state, float normalizedTime = 0.0F) noexcept;
    [[nodiscard]] bool CrossFade(SceneEntity entity, std::string_view layer, std::string_view state, float durationSeconds, float normalizedTime = 0.0F) noexcept;
    [[nodiscard]] bool SetSpeed(SceneEntity entity, float speed) noexcept;
    [[nodiscard]] float Speed(SceneEntity entity) const noexcept;
    [[nodiscard]] bool SetBool(SceneEntity entity, std::string_view name, bool value) noexcept;
    [[nodiscard]] bool SetInt(SceneEntity entity, std::string_view name, std::int32_t value) noexcept;
    [[nodiscard]] bool SetFloat(SceneEntity entity, std::string_view name, float value) noexcept;
    [[nodiscard]] bool SetTrigger(SceneEntity entity, std::string_view name) noexcept;
    [[nodiscard]] bool ResetTrigger(SceneEntity entity, std::string_view name) noexcept;
    [[nodiscard]] std::optional<AnimatorStateInfo> State(SceneEntity entity, std::string_view layer) const;
    [[nodiscard]] std::vector<AnimationEventRecord> DrainEvents();

private:
    Scene& scene_;
};

} // namespace kb::scene
