#pragma once

#include "engine/scene/SceneAudioOcclusionAccess.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::audio_miniaudio {

enum class MiniaudioOcclusionKeyKind : std::uint8_t {
    Source,
    Voice,
};

struct MiniaudioOcclusionKey {
    MiniaudioOcclusionKeyKind kind = MiniaudioOcclusionKeyKind::Source;
    std::uint64_t id = 0U;

    [[nodiscard]] bool operator==(const MiniaudioOcclusionKey&) const noexcept = default;
};

struct MiniaudioOcclusionKeyHash {
    [[nodiscard]] std::size_t operator()(const MiniaudioOcclusionKey& key) const noexcept {
        const std::size_t idHash = std::hash<std::uint64_t>{}(key.id);
        return idHash ^ (static_cast<std::size_t>(key.kind) + 0x9e3779b9U + (idHash << 6U) + (idHash >> 2U));
    }
};

// LIB-151: samples audio occlusion against the engine's existing collider raycast
// geometry (kb::scene::RaycastAllNonAlloc - pure CPU, physics-plugin-independent), under
// the hard per-tick ray budget AudioOcclusionSettings demands. A source beyond the budget
// (or skipped by this tick's rotating offset) keeps its LAST sampled scale from the
// per-key cache, so occlusion refresh degrades gracefully with source count instead of
// the raycast bill growing unboundedly. Keys are entity ids for component sources and
// voice ids for attached one-shots; ForgetMissing-style cleanup is unnecessary - the map
// is bounded by live sources/voices and cleared with the backend.
class MiniaudioOcclusionSampler final {
public:
    // Resets the tick budget and advances the rotating skip offset (fairness: sources
    // past last tick's budget get sampled on later ticks).
    void BeginTick(const kb::scene::AudioOcclusionSettings& settings) noexcept;
    void EndTick() noexcept;
    // Volume scale in {occludedVolumeScale, 1} for a source at `sourcePosition`.
    // `excludeEntityId` ignores the source's own collider (0 = exclude nothing).
    [[nodiscard]] float Sample(
        kb::scene::Scene& scene,
        const kb::scene::AudioOcclusionSettings& settings,
        MiniaudioOcclusionKey key,
        const kb::scene::Vec3& listenerPosition,
        const kb::scene::Vec3& sourcePosition,
        std::uint64_t excludeEntityId);
    [[nodiscard]] kb::scene::AudioOcclusionRuntimeStats RuntimeStats()
        const noexcept;
    void Clear() noexcept;

private:
    [[nodiscard]] float CachedOr(MiniaudioOcclusionKey key, float fallback) const noexcept;

    std::unordered_map<MiniaudioOcclusionKey, float, MiniaudioOcclusionKeyHash> lastScale_;
    std::unordered_set<MiniaudioOcclusionKey, MiniaudioOcclusionKeyHash> seenThisTick_;
    std::uint32_t budgetLeft_ = 0U;
    std::uint32_t skipLeft_ = 0U;
    std::uint32_t requestsThisTick_ = 0U;
    std::uint32_t requestsLastTick_ = 0U;
    std::uint32_t raycastsThisTick_ = 0U;
    std::uint32_t occludedSamplesThisTick_ = 0U;
    std::uint64_t tickIndex_ = 0U;
};

} // namespace kb::audio_miniaudio
