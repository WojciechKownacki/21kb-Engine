#pragma once

#include "engine/navigation/NavGeometryCollector.hpp"
#include "engine/navigation/NavMeshAsset.hpp"
#include "engine/scene/Navigation.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "kb/render/overlay/DebugFillTriangle.hpp"
#include "kb/render/overlay/PhysicsDebugLine.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <future>
#include <memory>
#include <span>
#include <vector>

namespace kb::scene {
class Scene;
}

namespace kb::editor {

// One edge of the box of a Navigation Bounds, relative to a viewport origin. The Scene View draws
// these lines and a click on one selects the owner.
struct EditorNavBoundsLine {
    kb::scene::SceneEntity owner{};
    kb::scene::Vec3 from{};
    kb::scene::Vec3 to{};
};

// Editor side of navigation meshes: the debug view of the polygons the open scene's (or world's)
// baked navigation meshes build into. The walkable polygons are filled in translucent green (one
// colour per navigation area when several are used), outlined along their own edges with the outer
// edges stronger, and every tile's bounds are drawn as a box. Positions are world space.
class EditorNavigation {
public:
    static constexpr std::array<float, 3U> FillColor{ 140.0F / 255.0F, 1.0F, 0.0F };
    static constexpr float FillAlpha = 164.0F / 255.0F;
    static constexpr std::array<float, 3U> PolygonEdgeColor{ 0.06F, 0.06F, 0.06F };
    static constexpr float PolygonEdgeAlpha = 0.35F;
    static constexpr std::array<float, 3U> BoundaryEdgeColor{ 0.10F, 0.42F, 0.0F };
    static constexpr std::array<float, 3U> TileBoundsColor{ 1.0F, 1.0F, 0.25F };
    // The drawing is capped so a huge world cannot flood the overlay pass.
    static constexpr std::size_t MaxLines = 400000U;
    static constexpr std::size_t MaxFillTriangles = 40000U;
    // Everything is lifted this far above the polygons so it does not fight the ground.
    static constexpr float DrawLift = 0.10F;

    [[nodiscard]] bool Visible() const noexcept { return visible_; }
    void SetVisible(bool visible) noexcept { visible_ = visible; }
    // Replaces the drawing with the polygons of the first agent profile of `meshes`, each filled with the
    // colour of its area in `areas` (the project's navigation areas).
    void Show(const std::vector<std::shared_ptr<const kb::navigation::NavMeshAsset>>& meshes, std::span<const kb::scene::NavAreaDefinition> areas);
    void Clear() noexcept;
    [[nodiscard]] const std::vector<kb::render::PhysicsDebugLine>& Lines() const noexcept { return lines_; }
    [[nodiscard]] const std::vector<kb::render::DebugFillTriangle>& Fills() const noexcept { return fills_; }
    [[nodiscard]] std::size_t TriangleCount() const noexcept { return triangles_; }
    [[nodiscard]] bool HasMesh() const noexcept { return triangles_ != 0U; }
    // The fill colour of a navigation area: its colour in `areas`, or the walkable fill for an area the list leaves out.
    [[nodiscard]] static std::array<float, 3U> AreaColor(std::span<const kb::scene::NavAreaDefinition> areas, std::uint8_t area) noexcept;
    static constexpr std::array<float, 3U> BoundsColor{ 0.78F, 0.78F, 0.78F };
    // The box edges of every visible Navigation Bounds of `scene`, relative to `origin`.
    [[nodiscard]] static std::vector<EditorNavBoundsLine> BoundsLines(const kb::scene::Scene& scene, const kb::math::DVec3& origin);

private:
    bool visible_ = true;
    std::vector<kb::render::PhysicsDebugLine> lines_;
    std::vector<kb::render::DebugFillTriangle> fills_;
    std::size_t triangles_ = 0U;
};

// A navigation bake running on a worker: the whole scene (World > Bake Navigation Mesh, or an
// update that cannot be narrowed to tiles) or the tiles changed since the last bake. The geometry is
// collected on the main thread before the job starts and the result is applied there when it ends.
struct EditorNavigationBakeJob {
    std::filesystem::path scenePath;
    // What the job bakes from; the baseline of the next update once applied.
    std::shared_ptr<const kb::navigation::NavScenePreparation> prepared;
    // The tiles rebuilt by an update; empty for a whole bake.
    std::vector<kb::navigation::NavTileCoord> tiles;
    bool automatic = false;
    kb::navigation::NavBakeProgress progress;
    std::future<kb::navigation::NavSceneBakeResult> result;
    int shownPercent = -1;
};

// Keeps the open scene's navigation mesh in step with its static geometry: after an edit settles,
// only the tiles the edit touched are rebuilt, in the background.
struct EditorNavigationUpdates {
    static constexpr std::chrono::milliseconds SettleDelay{ 500 };

    bool automatic = true;
    // The geometry and the mesh of the last bake of the open scene; null until one is known.
    std::shared_ptr<const kb::navigation::NavScenePreparation> baseline;
    std::shared_ptr<const kb::navigation::NavMeshAsset> asset;
    std::filesystem::path scenePath;
    // Reads the meshes for the updates; dropped with the scene and before a bake the user starts.
    std::unique_ptr<kb::navigation::INavGeometrySource> source;
    std::uint64_t checkedRevision = 0U;
    std::uint64_t pendingRevision = 0U;
    std::chrono::steady_clock::time_point pendingSince{};
    std::unique_ptr<EditorNavigationBakeJob> job;
};

} // namespace kb::editor
