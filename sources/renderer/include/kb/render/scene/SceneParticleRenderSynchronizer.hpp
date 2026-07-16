#pragma once

#include "kb/render/scene/RenderScene.hpp"

#include <cstdint>
#include <unordered_map>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::render {

// LIB-143: the per-frame bridge from kb::scene::SceneParticleSystems' pure CPU simulation
// (position/age/lifetime, no GPU concept at all) to real, GPU-visible billboard quads -
// reads the scene's live particle instances (never mutates kb::scene), evaluates each
// instance's ParticleEffectAsset sizeOverLifetime/colorOverLifetime curves per particle
// (rendering-only appearance data - see ParticleState's own doc comment for why kb::scene
// does not own this), and submits one RenderScene::UpsertMesh per live particle using the
// built-in quad mesh (BuiltInParticleQuadMesh.hpp) + the instance's already-resolved
// material - every particle sharing mesh+material batches into ONE real GPU-instanced draw
// call through RenderScene's own existing DrawGroupKey grouping, exactly like ordinary
// MeshRenderer components.
//
// Stateful (unlike EcsRenderSceneSynchronizer's ECS-entity-keyed sync, which can compare
// against the ECS's own current entity set): synthetic particle proxy ids are NOT real
// entities, so nothing else naturally reports "this slot is gone now" - this class tracks
// last frame's live particle count per instance itself, so a particle system whose count
// shrinks (particles died) or whose instance was released has its now-stale proxy slots
// explicitly removed rather than rendering a frozen ghost forever.
class SceneParticleRenderSynchronizer {
public:
    // Reserved high-bit namespace so synthetic particle proxy ids can never collide with a
    // real ECS entity id (RenderScene::MeshProxyMap's key is documented as "just an opaque
    // uint64", never required to be a real entity - see RuntimeMeshResourceEnsurer's own
    // entityId-is-just-a-map-key comment). Public since LIB-144 so
    // SceneRenderVisibilityPublisher can recognize (and skip) synthetic particle proxies
    // when publishing per-ENTITY visibility feedback - a script can never hold a synthetic
    // id, so an entry for one would be dead weight in every published frame.
    static constexpr std::uint64_t kSyntheticProxyIdBase = 0x8000'0000'0000'0000ULL;

    // Called once per viewport submission, after the ECS mesh/camera sync (so the resolved
    // primary camera for `targetViewportId` is available for billboard orientation) and
    // BEFORE EnsureSceneResources (so newly injected proxies get their GPU mesh/material
    // resolved this same frame) - see Renderer.cpp's SubmitSceneToViewport for the exact
    // call site.
    void Sync(const kb::scene::Scene& scene, RenderScene& renderScene, std::uint32_t targetViewportId);

private:
    std::unordered_map<std::uint64_t, std::uint32_t> lastFrameParticleCounts_;
};

} // namespace kb::render
