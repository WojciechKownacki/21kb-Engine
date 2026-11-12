#pragma once

#include "engine/navigation/NavMeshAsset.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "kb/render/overlay/DebugFillTriangle.hpp"
#include "kb/render/overlay/PhysicsDebugLine.hpp"

#include <array>
#include <cstddef>
#include <memory>
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
    // Replaces the drawing with the polygons of the first agent profile of `meshes`.
    void Show(const std::vector<std::shared_ptr<const kb::navigation::NavMeshAsset>>& meshes);
    void Clear() noexcept;
    [[nodiscard]] const std::vector<kb::render::PhysicsDebugLine>& Lines() const noexcept { return lines_; }
    [[nodiscard]] const std::vector<kb::render::DebugFillTriangle>& Fills() const noexcept { return fills_; }
    [[nodiscard]] std::size_t TriangleCount() const noexcept { return triangles_; }
    [[nodiscard]] bool HasMesh() const noexcept { return triangles_ != 0U; }
    // The fill colour of a navigation area when the shown meshes use more than one area.
    [[nodiscard]] static std::array<float, 3U> AreaColor(std::uint8_t area) noexcept;
    static constexpr std::array<float, 3U> BoundsColor{ 0.78F, 0.78F, 0.78F };
    // The box edges of every visible Navigation Bounds of `scene`, relative to `origin`.
    [[nodiscard]] static std::vector<EditorNavBoundsLine> BoundsLines(const kb::scene::Scene& scene, const kb::math::DVec3& origin);

private:
    bool visible_ = true;
    std::vector<kb::render::PhysicsDebugLine> lines_;
    std::vector<kb::render::DebugFillTriangle> fills_;
    std::size_t triangles_ = 0U;
};

} // namespace kb::editor
