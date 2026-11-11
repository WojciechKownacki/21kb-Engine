#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

class Scene;

// The baked light probe grid the scene renders with (an asset id, 0 = none). A scene file does not
// name its grid: the lighting bake writes it to a fixed place derived from the scene file's name, so
// whoever loads a scene file assigns it with AssignBakedGrid.
class SceneLightProbeAccess {
public:
    SceneLightProbeAccess() = delete;

    static void SetGrid(Scene& scene, std::uint64_t gridAssetId) noexcept;
    [[nodiscard]] static std::uint64_t Grid(const Scene& scene) noexcept;
    // Looks up LightProbeGridVirtualPath(sceneName) in the scene's asset registry; no grid when it is absent.
    static void AssignBakedGrid(Scene& scene, std::string_view sceneName);
};

} // namespace kb::scene
