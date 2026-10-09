#pragma once

#include "engine/navigation/NavMeshAsset.hpp"
#include "scene/EditorWorldPartition.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

namespace kb::editor {

// Editor side of navigation meshes: the debug view of the polygons the open scene's (or world's)
// baked navigation meshes build into, drawn as outlines over the scene view.
class EditorNavigation {
public:
    static constexpr std::array<float, 3U> MeshColor{ 0.20F, 0.72F, 0.95F };
    // The outline is capped so a huge world cannot flood the debug line pass.
    static constexpr std::size_t MaxLines = 400000U;
    // Lines are lifted this far above the polygons so they do not fight the ground.
    static constexpr float LineLift = 0.05F;

    [[nodiscard]] bool Visible() const noexcept { return visible_; }
    void SetVisible(bool visible) noexcept { visible_ = visible; }
    // Replaces the drawn polygons with those of the first agent profile of `meshes`.
    void Show(const std::vector<std::shared_ptr<const kb::navigation::NavMeshAsset>>& meshes);
    void Clear() noexcept;
    [[nodiscard]] const std::vector<EditorWorldGridLine>& Lines() const noexcept { return lines_; }
    [[nodiscard]] std::size_t TriangleCount() const noexcept { return triangles_; }
    [[nodiscard]] bool HasMesh() const noexcept { return triangles_ != 0U; }

private:
    bool visible_ = true;
    std::vector<EditorWorldGridLine> lines_;
    std::size_t triangles_ = 0U;
};

} // namespace kb::editor
