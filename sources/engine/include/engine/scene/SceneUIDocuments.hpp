#pragma once

#include "engine/scene/UIAssets.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace kb::scene {

class Scene;

struct UIRuntimeEventRecord {
    SceneEntity owner{};
    UIRuntimeEvent event{};
};

class SceneUIDocumentComponentQueries {
public:
    explicit SceneUIDocumentComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const UIDocumentComponent* TryGet(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class SceneUIDocumentComponents {
public:
    explicit SceneUIDocumentComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const UIDocumentComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] UIDocumentComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const UIDocumentComponent& document);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

class SceneUIDocumentQueries {
public:
    explicit SceneUIDocumentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Exists(SceneEntity entity) const noexcept;
    [[nodiscard]] std::uint64_t Asset(SceneEntity entity) const noexcept;
    [[nodiscard]] UIElementId Root(SceneEntity entity) const noexcept;
    [[nodiscard]] bool HasElement(SceneEntity entity, UIElementId element) const noexcept;
    [[nodiscard]] bool Visible(SceneEntity entity, UIElementId element) const noexcept;
    [[nodiscard]] std::optional<UIControlState> Control(SceneEntity entity, UIElementId element) const;
    [[nodiscard]] bool StyleIsResolved(SceneEntity entity) const noexcept;
    [[nodiscard]] std::size_t ElementCount(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class SceneUIDocuments {
public:
    explicit SceneUIDocuments(Scene& scene) noexcept;
    [[nodiscard]] bool Exists(SceneEntity entity) const noexcept;
    [[nodiscard]] std::uint64_t Asset(SceneEntity entity) const noexcept;
    [[nodiscard]] UIElementId Root(SceneEntity entity) const noexcept;
    [[nodiscard]] bool HasElement(SceneEntity entity, UIElementId element) const noexcept;
    [[nodiscard]] bool Visible(SceneEntity entity, UIElementId element) const noexcept;
    [[nodiscard]] std::optional<UIControlState> Control(SceneEntity entity, UIElementId element) const;
    [[nodiscard]] bool StyleIsResolved(SceneEntity entity) const noexcept;
    [[nodiscard]] std::size_t ElementCount(SceneEntity entity) const noexcept;
    // Commands are appended in call order and are applied once by
    // UIDocumentSceneSystem at the next frame boundary. A returned ID is
    // reserved immediately and remains unique for this runtime attachment;
    // it becomes queryable only after the queue is drained.
    [[nodiscard]] std::optional<UIElementId> QueueCreate(SceneEntity entity, const UIRuntimeElementDesc& desc);
    [[nodiscard]] bool QueueDestroy(SceneEntity entity, UIElementId element) noexcept;
    [[nodiscard]] bool QueueShow(SceneEntity entity, UIElementId element) noexcept;
    [[nodiscard]] bool QueueHide(SceneEntity entity, UIElementId element) noexcept;
    [[nodiscard]] bool QueueSetControl(SceneEntity entity, UIElementId element, const UIControlState& control);
    // Enqueues a validated interaction emitted by the UI input layer. The
    // event is delivered in FIFO order by ScriptRuntimeSceneSystem through
    // ScriptEventBus at the next script frame boundary.
    [[nodiscard]] bool QueueEvent(SceneEntity entity, const UIRuntimeEvent& event);
    [[nodiscard]] std::vector<UIRuntimeEventRecord> DrainEvents();
private:
    Scene& scene_;
};

} // namespace kb::scene
