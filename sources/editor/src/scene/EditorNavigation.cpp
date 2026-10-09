#include "scene/EditorNavigation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <tuple>
#include <utility>

namespace kb::editor {
namespace {

// A polygon corner snapped to a millimetre grid, so an edge shared by two triangles is drawn once.
using Corner = std::tuple<std::int64_t, std::int64_t, std::int64_t>;

[[nodiscard]] Corner Snap(const kb::math::DVec3& point) noexcept {
    return { std::llround(point.x * 1000.0), std::llround(point.y * 1000.0), std::llround(point.z * 1000.0) };
}

} // namespace

void EditorNavigation::Show(const std::vector<std::shared_ptr<const kb::navigation::NavMeshAsset>>& meshes) {
    Clear();
    std::set<std::pair<Corner, Corner>> drawn;
    for (const std::shared_ptr<const kb::navigation::NavMeshAsset>& mesh : meshes) {
        const std::vector<kb::math::DVec3> triangles = kb::navigation::NavMeshAssetTriangles(mesh, 0U);
        triangles_ += triangles.size() / 3U;
        for (std::size_t index = 0U; index + 2U < triangles.size() && lines_.size() < MaxLines; index += 3U) {
            for (std::size_t edge = 0U; edge < 3U; ++edge) {
                const kb::math::DVec3& from = triangles[index + edge];
                const kb::math::DVec3& to = triangles[index + (edge + 1U) % 3U];
                std::pair<Corner, Corner> key{ Snap(from), Snap(to) };
                if (key.second < key.first) std::swap(key.first, key.second);
                if (!drawn.insert(key).second) continue;
                lines_.push_back(EditorWorldGridLine{
                    .from = { static_cast<float>(from.x), static_cast<float>(from.y) + LineLift, static_cast<float>(from.z) },
                    .to = { static_cast<float>(to.x), static_cast<float>(to.y) + LineLift, static_cast<float>(to.z) },
                    .color = MeshColor,
                });
            }
        }
    }
}

void EditorNavigation::Clear() noexcept {
    lines_.clear();
    triangles_ = 0U;
}

} // namespace kb::editor
