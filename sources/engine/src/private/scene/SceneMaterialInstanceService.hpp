#pragma once

#include "engine/scene/SceneMaterialInstances.hpp"

#include <cstdint>
#include <span>
#include <string_view>

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
    // LIB-140
    [[nodiscard]] static std::span<const MaterialParameterOverride> Parameters(const Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static bool SetParameterScalar(Scene& scene, std::uint64_t id, std::string_view name, float value) noexcept;
    [[nodiscard]] static bool SetParameterBool(Scene& scene, std::uint64_t id, std::string_view name, bool value) noexcept;
    [[nodiscard]] static bool ClearParameter(Scene& scene, std::uint64_t id, std::string_view name) noexcept;
};

} // namespace kb::scene
