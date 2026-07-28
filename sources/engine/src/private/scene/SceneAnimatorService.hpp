#pragma once

#include "engine/scene/SceneAnimators.hpp"

namespace kb::scene {

class SceneAnimatorService final {
public:
    SceneAnimatorService() = delete;
    [[nodiscard]] static bool Attach(Scene& scene, SceneEntity entity, std::uint64_t controllerAssetId);
    [[nodiscard]] static bool Exists(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static std::uint64_t Controller(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static std::span<const AnimatorParameterValue> Parameters(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static std::uint64_t RuntimeBindingGeneration(
        const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool Play(Scene& scene, SceneEntity entity, std::string_view layer, std::string_view state, float normalizedTime) noexcept;
    [[nodiscard]] static bool CrossFade(Scene& scene, SceneEntity entity, std::string_view layer, std::string_view state, float durationSeconds, float normalizedTime) noexcept;
    [[nodiscard]] static bool SetSpeed(Scene& scene, SceneEntity entity, float speed) noexcept;
    [[nodiscard]] static float Speed(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool SetBool(Scene& scene, SceneEntity entity, std::string_view name, bool value) noexcept;
    [[nodiscard]] static bool SetInt(Scene& scene, SceneEntity entity, std::string_view name, std::int32_t value) noexcept;
    [[nodiscard]] static bool SetFloat(Scene& scene, SceneEntity entity, std::string_view name, float value) noexcept;
    [[nodiscard]] static bool SetTrigger(Scene& scene, SceneEntity entity, std::string_view name, bool value) noexcept;
    [[nodiscard]] static bool SetIkTarget(
        Scene& scene, SceneEntity entity, std::string_view name,
        const AnimatorIkTarget& target) noexcept;
    [[nodiscard]] static bool ClearIkTarget(
        Scene& scene, SceneEntity entity, std::string_view name) noexcept;
    [[nodiscard]] static std::optional<AnimatorStateInfo> State(const Scene& scene, SceneEntity entity, std::string_view layer);
    [[nodiscard]] static std::vector<AnimationEventRecord> DrainEvents(Scene& scene);
    static void Advance(Scene& scene, float deltaSeconds);
    static void SyncComponents(Scene& scene);
};

} // namespace kb::scene
