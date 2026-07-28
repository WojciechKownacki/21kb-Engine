#pragma once

#include "engine/scene/SceneUIDocuments.hpp"

namespace kb::scene {

class SceneUIDocumentService final {
public:
    SceneUIDocumentService() = delete;
    [[nodiscard]] static bool Exists(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static std::uint64_t Asset(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static UIElementId Root(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool HasElement(const Scene& scene, SceneEntity entity, UIElementId element) noexcept;
    [[nodiscard]] static bool StyleIsResolved(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static std::size_t ElementCount(const Scene& scene, SceneEntity entity) noexcept;
    static void SyncComponents(Scene& scene);
};

} // namespace kb::scene
