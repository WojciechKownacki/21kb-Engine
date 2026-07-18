#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneLoadedContent.hpp"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace kb::scene {

class Scene;

// LIB-071: the engine-side logic behind Scene.Load/Unload/SetActive/Find —
// private (kb::scene internals), consumed through the public
// SceneLoadedContent/SceneLoadedContentQueries facade on Scene, mirroring
// SceneEntityService's own facade/service split.
class SceneLoadedContentService {
public:
    SceneLoadedContentService() = delete;

    // additive=false replaces the scene's ENTIRE content (same destructive
    // ClearSceneRoots step SceneDocumentService::LoadIntoScene already
    // uses) and resets the loaded-content record list to just this one
    // load; additive=true instantiates alongside existing content and
    // adds a new record. Returns 0 (never a valid id — ids start at 1) on
    // failure (bad extension, unreadable file, empty worldPrefab).
    [[nodiscard]] static std::uint64_t Load(Scene& scene, const std::filesystem::path& path, bool additive);
    // Destroys the loaded content's root (cascades to its whole hierarchy,
    // per LIB-070's finding) and removes its record. False if `id` names
    // no current record — never throws on a stale/unknown id.
    [[nodiscard]] static bool Unload(Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static std::uint64_t Find(const Scene& scene, std::string_view name) noexcept;
    [[nodiscard]] static bool Exists(const Scene& scene, std::uint64_t id) noexcept;
    // Always 1.0 for an existing record and 0.0 for an unknown one — loads
    // are synchronous today (ScenePrefabs::Instantiate never runs partially
    // across multiple calls), so there is no genuine partial-progress state
    // to report; this is forward-compatible surface area for a future
    // async loader, not a fabricated in-between value.
    [[nodiscard]] static float Progress(const Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static bool SetActive(Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static std::uint64_t ActiveScene(const Scene& scene) noexcept;
    // LIB-071: hierarchy root of the active loaded scene (invalid if none).
    [[nodiscard]] static SceneEntity ActiveSceneRoot(const Scene& scene) noexcept;
    // LIB-106: which loaded-scene record (Scene.Load's own id) `entity`'s
    // hierarchy root belongs to; 0 if none (invalid/dead entity, or one
    // never part of any Scene.Load'ed content).
    [[nodiscard]] static std::uint64_t OwningScene(const Scene& scene, SceneEntity entity) noexcept;
    // LIB-073: returns and clears SceneState::pendingSceneLifecycleEvents.
    [[nodiscard]] static std::vector<SceneLifecycleEventRecord> DrainPendingLifecycleEvents(Scene& scene);
};

} // namespace kb::scene
