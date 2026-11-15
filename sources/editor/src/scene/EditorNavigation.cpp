#include "scene/EditorNavigation.hpp"

#include "engine/math/DVec3.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneVisibilityResolution.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <tuple>
#include <utility>

namespace kb::editor {
namespace {

// A polygon corner snapped to a millimetre grid, so an edge two tiles share is drawn once.
using Corner = std::tuple<std::int64_t, std::int64_t, std::int64_t>;

[[nodiscard]] Corner Snap(const kb::math::DVec3& point) noexcept {
    return { std::llround(point.x * 1000.0), std::llround(point.y * 1000.0), std::llround(point.z * 1000.0) };
}

[[nodiscard]] std::array<float, 3U> Lifted(const kb::math::DVec3& point) noexcept {
    return { static_cast<float>(point.x), static_cast<float>(point.y) + EditorNavigation::DrawLift, static_cast<float>(point.z) };
}

struct BoundsLinesContext {
    const kb::scene::Scene* scene = nullptr;
    kb::math::DVec3 origin{};
    std::vector<EditorNavBoundsLine>* lines = nullptr;
};

void CollectBoundsLines(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, void* raw) {
    auto& context = *static_cast<BoundsLinesContext*>(raw);
    const kb::scene::NavBounds* bounds = context.scene->Components().NavBounds().TryGet(entity);
    if (bounds == nullptr || !kb::scene::ResolveVisibility(*context.scene, entity).visible) return;
    const kb::scene::Vec3 position = kb::math::RelativeTo(context.scene->Transforms().WorldTranslation(entity, transform), context.origin);
    std::array<kb::scene::Vec3, 8U> corners{};
    for (std::size_t corner = 0U; corner < corners.size(); ++corner) {
        const kb::scene::Vec3 local{
            (bounds->center.x + bounds->size.x * ((corner & 1U) != 0U ? 0.5F : -0.5F)) * transform.worldScale.x,
            (bounds->center.y + bounds->size.y * ((corner & 2U) != 0U ? 0.5F : -0.5F)) * transform.worldScale.y,
            (bounds->center.z + bounds->size.z * ((corner & 4U) != 0U ? 0.5F : -0.5F)) * transform.worldScale.z,
        };
        corners[corner] = position + kb::math::Rotate(transform.worldRotation, local);
    }
    for (std::size_t from = 0U; from < corners.size(); ++from) {
        for (const std::size_t axis : { 1U, 2U, 4U }) {
            if ((from & axis) == 0U) context.lines->push_back(EditorNavBoundsLine{ .owner = entity, .from = corners[from], .to = corners[from | axis] });
        }
    }
}

} // namespace

std::array<float, 3U> EditorNavigation::AreaColor(std::span<const kb::scene::NavAreaDefinition> areas, std::uint8_t area) noexcept {
    if (area >= areas.size()) return FillColor;
    const std::uint32_t colour = areas[area].colour;
    return { static_cast<float>((colour >> 16U) & 0xFFU) / 255.0F, static_cast<float>((colour >> 8U) & 0xFFU) / 255.0F,
        static_cast<float>(colour & 0xFFU) / 255.0F };
}

void EditorNavigation::Show(const std::vector<std::shared_ptr<const kb::navigation::NavMeshAsset>>& meshes,
    std::span<const kb::scene::NavAreaDefinition> areas) {
    Clear();
    std::vector<kb::navigation::NavMeshDebugGeometry> shown;
    for (const std::shared_ptr<const kb::navigation::NavMeshAsset>& mesh : meshes) {
        shown.push_back(kb::navigation::NavMeshAssetDebugGeometry(mesh, 0U));
    }
    std::set<std::pair<Corner, Corner>> drawn;
    const auto addLine = [this](const kb::math::DVec3& from, const kb::math::DVec3& to, const std::array<float, 3U>& color, float alpha) {
        if (lines_.size() < MaxLines) lines_.push_back(kb::render::PhysicsDebugLine{ .from = Lifted(from), .to = Lifted(to), .color = color, .alpha = alpha });
    };
    for (const kb::navigation::NavMeshDebugGeometry& geometry : shown) {
        triangles_ += geometry.triangles.size() / 3U;
        for (std::size_t index = 0U; index + 2U < geometry.triangles.size() && fills_.size() < MaxFillTriangles; index += 3U) {
            fills_.push_back(kb::render::DebugFillTriangle{
                .corners = { Lifted(geometry.triangles[index]), Lifted(geometry.triangles[index + 1U]), Lifted(geometry.triangles[index + 2U]) },
                .color = AreaColor(areas, geometry.triangleAreas[index / 3U]),
                .alpha = FillAlpha,
            });
        }
        for (const kb::navigation::NavMeshDebugGeometry::Edge& edge : geometry.edges) {
            std::pair<Corner, Corner> key{ Snap(edge.from), Snap(edge.to) };
            if (key.second < key.first) std::swap(key.first, key.second);
            if (!drawn.insert(key).second) continue;
            addLine(edge.from, edge.to, edge.boundary ? BoundaryEdgeColor : PolygonEdgeColor, edge.boundary ? 1.0F : PolygonEdgeAlpha);
        }
        for (const kb::navigation::NavMeshDebugGeometry::Box& box : geometry.tiles) {
            const auto corner = [&box](int bits) {
                return kb::math::DVec3{ (bits & 1) != 0 ? box.max.x : box.min.x, (bits & 2) != 0 ? box.max.y : box.min.y, (bits & 4) != 0 ? box.max.z : box.min.z };
            };
            for (int from = 0; from < 8; ++from) {
                for (const int axis : { 1, 2, 4 }) {
                    if ((from & axis) == 0) addLine(corner(from), corner(from | axis), TileBoundsColor, 0.8F);
                }
            }
        }
    }
}

std::vector<EditorNavBoundsLine> EditorNavigation::BoundsLines(const kb::scene::Scene& scene, const kb::math::DVec3& origin) {
    std::vector<EditorNavBoundsLine> lines;
    BoundsLinesContext context{ .scene = &scene, .origin = origin, .lines = &lines };
    scene.Transforms().ForEach(&CollectBoundsLines, &context);
    return lines;
}

void EditorNavigation::Clear() noexcept {
    lines_.clear();
    fills_.clear();
    triangles_ = 0U;
}

} // namespace kb::editor
