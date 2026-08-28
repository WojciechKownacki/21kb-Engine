#pragma once

#include <cstdint>

namespace kb::scene {

class Scene;

// LIB-142: the scene-global "active PostProcessProfile asset" toggle - the ONLY place a
// scene's post-processing parameters live, mirroring SceneLightingAccess's own
// scene-global-toggle shape (a plain field on SceneState, no per-entity component). `Scene()`
// storage is a plain kb::assets::AssetId value (0 = none, never a valid asset id) - kb::scene
// deliberately does not know what a PostProcessProfile asset actually CONTAINS (that schema,
// kb::render::ScenePostProcessSettings, lives entirely in kb::render - kb::scene never
// depends on kb::render, the same one-directional rule every other renderer-consumed asset
// reference on a scene component already follows, e.g. MeshRendererComponent::materialAssetId).
// A real resolution to a live GPU-affecting settings value happens once, lazily, at
// render-submission time (Renderer.cpp's ResolveScenePostProcessProfile) - an
// unresolvable/missing profile asset id honestly falls back to "no override" (the
// already-existing default/explicit-submit-desc behavior), not a crash.
//
// Deliberately scene-global rather than a spatial "volume" with bounding-box priority
// blending (the usual sense of a "post-process volume") - that is a substantially larger
// feature (spatial queries, blend weights, priority resolution across overlapping volumes)
// explicitly out of scope for this ticket; "profile" is the part LIB-142 actually delivers.
class ScenePostProcessAccess {
public:
    ScenePostProcessAccess() = delete;

    static void SetActiveProfile(Scene& scene, std::uint64_t profileAssetId) noexcept;
    [[nodiscard]] static std::uint64_t ActiveProfile(const Scene& scene) noexcept;
};

} // namespace kb::scene
