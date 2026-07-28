#pragma once

#include "engine/scene/SceneTimelines.hpp"

namespace kb::scene {

class SceneTimelineService final {
public:
    SceneTimelineService() = delete;
    [[nodiscard]] static std::uint64_t Create(
        Scene& scene, std::uint64_t assetId, SceneEntity owner);
    [[nodiscard]] static bool Release(
        Scene& scene, std::uint64_t instance) noexcept;
    [[nodiscard]] static bool Exists(
        const Scene& scene, std::uint64_t instance) noexcept;
    [[nodiscard]] static bool Play(
        Scene& scene, std::uint64_t instance) noexcept;
    [[nodiscard]] static bool Pause(
        Scene& scene, std::uint64_t instance) noexcept;
    [[nodiscard]] static bool IsPlaying(
        const Scene& scene, std::uint64_t instance) noexcept;
    [[nodiscard]] static bool Seek(
        Scene& scene, std::uint64_t instance, float timeSeconds);
    [[nodiscard]] static bool Skip(
        Scene& scene, std::uint64_t instance, float targetTimeSeconds,
        TimelineSkipMarkerPolicy markerPolicy);
    [[nodiscard]] static bool Bind(
        Scene& scene, std::uint64_t instance, const std::string& binding,
        SceneEntity target);
    [[nodiscard]] static float Time(
        const Scene& scene, std::uint64_t instance) noexcept;
    [[nodiscard]] static std::uint64_t Asset(
        const Scene& scene, std::uint64_t instance) noexcept;
    [[nodiscard]] static std::vector<TimelineMarkerEvent> DrainMarkerEvents(
        Scene& scene);
    static void Advance(Scene& scene, float deltaSeconds);
};

} // namespace kb::scene
