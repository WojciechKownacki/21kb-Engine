#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TimelineAsset.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kb::scene {

class Scene;

enum class TimelineSkipMarkerPolicy : std::uint8_t {
    Suppress,
    EmitCrossed,
};

struct TimelineMarkerEvent {
    static constexpr std::int32_t kSchemaMajor = 1;
    static constexpr std::int32_t kSchemaMinor = 0;
    SceneEntity target{};
    std::int32_t schemaMajor = kSchemaMajor;
    std::int32_t schemaMinor = kSchemaMinor;
    std::uint64_t instanceId = 0U;
    std::uint64_t assetId = 0U;
    TimelineMarkerId markerId = 0U;
    float timeSeconds = 0.0F;
};

class SceneTimelineQueries {
public:
    explicit SceneTimelineQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Exists(std::uint64_t instance) const noexcept;
    [[nodiscard]] bool IsPlaying(std::uint64_t instance) const noexcept;
    [[nodiscard]] float Time(std::uint64_t instance) const noexcept;
    [[nodiscard]] std::uint64_t Asset(std::uint64_t instance) const noexcept;
private:
    const Scene& scene_;
};

class SceneTimelines {
public:
    static constexpr std::size_t kMaxInstances = 4096U;
    static constexpr std::size_t kMaxPendingMarkers = 4096U;
    explicit SceneTimelines(Scene& scene) noexcept;
    [[nodiscard]] std::uint64_t Create(
        std::uint64_t assetId, SceneEntity owner);
    [[nodiscard]] bool Release(std::uint64_t instance) noexcept;
    [[nodiscard]] bool Exists(std::uint64_t instance) const noexcept;
    [[nodiscard]] bool Play(std::uint64_t instance) noexcept;
    [[nodiscard]] bool Pause(std::uint64_t instance) noexcept;
    [[nodiscard]] bool IsPlaying(std::uint64_t instance) const noexcept;
    [[nodiscard]] bool Seek(std::uint64_t instance, float timeSeconds);
    [[nodiscard]] bool Skip(
        std::uint64_t instance, float targetTimeSeconds,
        TimelineSkipMarkerPolicy markerPolicy);
    [[nodiscard]] bool Bind(
        std::uint64_t instance, const std::string& binding,
        SceneEntity target);
    [[nodiscard]] float Time(std::uint64_t instance) const noexcept;
    [[nodiscard]] std::uint64_t Asset(std::uint64_t instance) const noexcept;
    [[nodiscard]] std::vector<TimelineMarkerEvent> DrainMarkerEvents();
private:
    Scene& scene_;
};

} // namespace kb::scene
