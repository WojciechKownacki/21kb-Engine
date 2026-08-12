#pragma once

#include <cmath>
#include <cstdint>

namespace kb::scene {

class Scene;

// LIB-151: the scene-global audio occlusion configuration. Occlusion runs against the
// engine's EXISTING, long-tested collider raycast geometry (kb::scene::RaycastAllNonAlloc
// - pure ColliderComponent/TransformComponent math, the same backend Physics.Raycast has
// always used, available with or without a physics plugin) - never a new spatial system.
//
// Cost control is explicit and hard: the audio backend spends at most
// `maxRaycastsPerTick` occlusion rays per tick; sources beyond the budget keep their last
// sampled result (a per-source cache) and are revisited on later ticks through a rotating
// start offset, so a large scene degrades to slower occlusion refresh, never to an
// unbounded per-frame raycast bill. `maxDistance` additionally skips (and un-occludes)
// sources far from the listener without spending a ray. `occludedVolumeScale` is the
// multiplier applied to a fully occluded source/voice; v1 is a binary occluded/clear
// state without temporal smoothing (documented simplification - obstruction shares the
// same single-ray model rather than adding a multi-ray spread).
struct AudioOcclusionSettings {
    bool enabled = false;
    float occludedVolumeScale = 0.35F;
    float maxDistance = 100.0F;
    std::uint32_t layerMask = 0xFFFFFFFFU;
    std::uint32_t maxRaycastsPerTick = 8U;
};

inline constexpr std::uint32_t kMaxAudioOcclusionRaycastsPerTick = 4096U;

[[nodiscard]] inline bool IsAudioOcclusionSettingsValid(const AudioOcclusionSettings& settings) noexcept {
    return std::isfinite(settings.occludedVolumeScale)
        && settings.occludedVolumeScale >= 0.0F
        && settings.occludedVolumeScale <= 1.0F
        && std::isfinite(settings.maxDistance)
        && settings.maxDistance >= 0.0F
        && settings.maxRaycastsPerTick <= kMaxAudioOcclusionRaycastsPerTick;
}

// Per-tick production telemetry published by the active audio backend. It makes the hard
// cost cap and actual collider hits observable without exposing plugin-private objects or
// inferring occlusion from an audio device's physical output.
struct AudioOcclusionRuntimeStats {
    std::uint32_t sampleRequests = 0U;
    std::uint32_t raycasts = 0U;
    std::uint32_t occludedSamples = 0U;
};

class SceneAudioOcclusionAccess {
public:
    SceneAudioOcclusionAccess() = delete;

    [[nodiscard]] static bool Configure(Scene& scene, const AudioOcclusionSettings& settings) noexcept;
    [[nodiscard]] static const AudioOcclusionSettings& Settings(const Scene& scene) noexcept;
    static void PublishRuntimeStats(
        Scene& scene, const AudioOcclusionRuntimeStats& stats) noexcept;
    [[nodiscard]] static const AudioOcclusionRuntimeStats& RuntimeStats(
        const Scene& scene) noexcept;
};

} // namespace kb::scene
