#pragma once

#include <cstdint>

namespace kb::scene {

class Scene;

// LIB-139: the engine-side logic behind MaterialInstance.Create/Release —
// private (kb::scene internals), consumed through the public
// SceneMaterialInstances/SceneMaterialInstanceQueries facades on Scene,
// mirroring SceneTimerService's own facade/service split.
class SceneMaterialInstanceService {
public:
    SceneMaterialInstanceService() = delete;

    [[nodiscard]] static std::uint64_t Create(Scene& scene, std::uint64_t parentMaterialAssetId) noexcept;
    [[nodiscard]] static bool Release(Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static bool Exists(const Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static std::uint64_t Parent(const Scene& scene, std::uint64_t id) noexcept;
};

} // namespace kb::scene
