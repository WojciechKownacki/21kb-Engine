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
    [[nodiscard]] static bool Visible(const Scene& scene, SceneEntity entity, UIElementId element) noexcept;
    [[nodiscard]] static std::optional<UIControlState> Control(const Scene& scene, SceneEntity entity, UIElementId element);
    [[nodiscard]] static std::optional<UIElementId> Find(const Scene& scene, SceneEntity entity, std::string_view name) noexcept;
    [[nodiscard]] static bool StyleIsResolved(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static std::size_t ElementCount(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static std::optional<UIElementId> QueueCreate(Scene& scene, SceneEntity entity, const UIRuntimeElementDesc& desc);
    [[nodiscard]] static bool QueueDestroy(Scene& scene, SceneEntity entity, UIElementId element) noexcept;
    [[nodiscard]] static bool QueueVisibility(Scene& scene, SceneEntity entity, UIElementId element, bool visible) noexcept;
    [[nodiscard]] static bool QueueSetControl(Scene& scene, SceneEntity entity, UIElementId element, const UIControlState& control);
    [[nodiscard]] static bool QueueEvent(Scene& scene, SceneEntity entity, const UIRuntimeEvent& event);
    [[nodiscard]] static std::vector<UIRuntimeEventRecord> DrainEvents(Scene& scene);
    static void SyncComponents(Scene& scene);
};

} // namespace kb::scene
