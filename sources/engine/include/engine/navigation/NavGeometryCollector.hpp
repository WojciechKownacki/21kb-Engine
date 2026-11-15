#pragma once

#include "engine/navigation/NavMeshAsset.hpp"
#include "engine/navigation/NavMeshBuild.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <span>
#include <unordered_map>
#include <vector>

namespace kb::assets {
class AssetManager;
}

namespace kb::scene {
struct NavBounds;
struct ScenePrefabNodeDesc;
}

namespace kb::navigation {

// Triangles of a mesh asset in the mesh's own space.
struct NavSourceMesh {
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
};

// Resolves the mesh assets scene geometry refers to.
class INavGeometrySource {
public:
    virtual ~INavGeometrySource() = default;
    // The finest level of detail of a mesh renderer's mesh (a static mesh or a terrain); null when
    // the asset is unknown or holds no triangles this source can read.
    [[nodiscard]] virtual std::shared_ptr<const NavSourceMesh> RenderMesh(std::uint64_t assetId) = 0;
    // A mesh collider's CollisionMesh asset.
    [[nodiscard]] virtual std::shared_ptr<const NavSourceMesh> CollisionMesh(std::uint64_t assetId) = 0;
};

// The engine's own geometry: CollisionMesh assets through the asset manager, and terrain
// (.kbterrain) heightfields, holes included. Other render meshes need the renderer's loaders
// (kb::render::RenderNavGeometrySource).
class AssetNavGeometrySource : public INavGeometrySource {
public:
    explicit AssetNavGeometrySource(kb::assets::AssetManager& assets) noexcept;

    [[nodiscard]] std::shared_ptr<const NavSourceMesh> RenderMesh(std::uint64_t assetId) override;
    [[nodiscard]] std::shared_ptr<const NavSourceMesh> CollisionMesh(std::uint64_t assetId) override;

protected:
    [[nodiscard]] kb::assets::AssetManager& Assets() noexcept { return assets_; }
    // The heightfield of a terrain asset; null when the asset is not a terrain.
    [[nodiscard]] std::shared_ptr<const NavSourceMesh> TerrainMesh(std::uint64_t assetId);
    // Cache shared by the overrides: one resolution per asset id.
    std::unordered_map<std::uint64_t, std::shared_ptr<const NavSourceMesh>> renderMeshes_;

private:
    kb::assets::AssetManager& assets_;
    std::unordered_map<std::uint64_t, std::shared_ptr<const NavSourceMesh>> collisionMeshes_;
};

struct NavGeometryCollectStats {
    std::size_t meshes = 0U;
    std::size_t colliders = 0U;
    // Mesh renderers or mesh colliders whose geometry the source could not provide.
    std::size_t unresolved = 0U;
};

// Adds the static geometry of a node hierarchy (a prefab, a world object or a scene's world prefab;
// parents before children) to `geometry`. Static means: no dynamic or kinematic rigidbody on the
// node or above it, and no NavAgent, NavObstacle or character controller on the node. Trigger
// colliders and hidden mesh renderers are skipped; without a source only primitive colliders
// (box, sphere, capsule) are collected. The world-space bounds of every Navigation Bounds box are
// added as bake boxes.
void CollectNavGeometry(std::span<const kb::scene::ScenePrefabNodeDesc> nodes, const NavMeshBuildSettings& settings,
    INavGeometrySource* source, NavGeometry& geometry, NavGeometryCollectStats& stats);

struct NavSceneBakeResult {
    bool succeeded = false;
    NavMeshAsset asset;
    NavMeshBakeStats stats;
    NavGeometryCollectStats geometry;
    std::string error;
};

// The build settings a Navigation Bounds holds: one agent profile and its cell and tile size.
[[nodiscard]] NavMeshBuildSettings NavBoundsBuildSettings(const kb::scene::NavBounds& bounds);

// What a scene bakes from: the settings and the static geometry of its nodes.
struct NavScenePreparation {
    bool succeeded = false;
    NavMeshBuildSettings settings;
    NavGeometry geometry;
    NavGeometryCollectStats stats;
    std::string error;
};

// Collects a scene's geometry for a bake. A scene with Navigation Bounds bakes with their settings
// (they must all agree) inside their boxes; one without bakes with `settings` everywhere. Reads
// assets through `source`, so it runs where the source may be used.
[[nodiscard]] NavScenePreparation PrepareSceneNavBake(std::span<const kb::scene::ScenePrefabNodeDesc> nodes, const NavMeshBuildSettings& settings,
    INavGeometrySource* source);
// Bakes prepared geometry; touches nothing but `prepared` (and `progress`), so it may run on any thread.
[[nodiscard]] NavSceneBakeResult BakePreparedSceneNavMesh(const NavScenePreparation& prepared, NavBakeProgress* progress = nullptr);

// Bakes the static geometry of a saved scene (.21kbscene): PrepareSceneNavBake, then
// BakePreparedSceneNavMesh. The editor, kb_cli and tools bake through these with the same source, so
// a scene bakes to the same bytes everywhere.
[[nodiscard]] NavSceneBakeResult BakeSceneNavMesh(const std::filesystem::path& scenePath, const NavMeshBuildSettings& settings,
    INavGeometrySource* source);
// Where a scene's navigation mesh lives: <scene>.21kbnavmesh beside it.
[[nodiscard]] std::filesystem::path SceneNavMeshPath(const std::filesystem::path& scenePath);
// The settings the scene's navigation mesh was last baked with; the defaults without one.
[[nodiscard]] NavMeshBuildSettings SceneNavMeshSettings(const std::filesystem::path& scenePath);

} // namespace kb::navigation
