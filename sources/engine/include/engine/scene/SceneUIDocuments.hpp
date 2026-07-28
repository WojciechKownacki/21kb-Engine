#pragma once

#include "engine/scene/UIAssets.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstddef>
#include <cstdint>

namespace kb::scene {

class Scene;

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
    [[nodiscard]] bool StyleIsResolved(SceneEntity entity) const noexcept;
    [[nodiscard]] std::size_t ElementCount(SceneEntity entity) const noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene
