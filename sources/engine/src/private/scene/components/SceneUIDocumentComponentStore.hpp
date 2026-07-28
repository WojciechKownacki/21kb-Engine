#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/UIAssets.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneUIDocumentComponentStore {
public:
    SceneUIDocumentComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const UIDocumentComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] UIDocumentComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const UIDocumentComponent& document);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene
