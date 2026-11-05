#pragma once
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace kb::scene {
class Scene;
struct SurfaceGeometryVertex { Vec3 position{}, normal{0,0,-1}; float u = 0, v = 0; };
struct SurfaceGeometry {
    SceneEntity owner{};
    std::uint64_t meshAssetId = 0, revision = 0;
    std::vector<SurfaceGeometryVertex> vertices;
    std::vector<std::uint32_t> indices;
};
// CPU surfaces shared with render providers. Publishing marks the existing MeshRenderer dirty.
class SceneSurfaceGeometry final {
public:
    [[nodiscard]] static bool Publish(Scene& scene, SceneEntity owner, std::span<const SurfaceGeometryVertex> vertices,
        std::span<const std::uint32_t> indices, std::string& error);
    [[nodiscard]] static std::span<const SurfaceGeometry> All(const Scene& scene) noexcept;
    static void Remove(Scene& scene, SceneEntity owner) noexcept;
    static void Clear(Scene& scene) noexcept;
};
} // namespace kb::scene
