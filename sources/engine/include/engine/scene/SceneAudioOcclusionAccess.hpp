#pragma once

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

class SceneAudioOcclusionAccess {
public:
    SceneAudioOcclusionAccess() = delete;

    static void Configure(Scene& scene, const AudioOcclusionSettings& settings) noexcept;
    [[nodiscard]] static const AudioOcclusionSettings& Settings(const Scene& scene) noexcept;
};

} // namespace kb::scene
