#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/visual/VisualGraphRuntimeExecutionContext.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace kb::visual {

struct VisualGraphBehaviourInstanceKey {
    std::uint64_t entityId = 0;
    std::uint64_t assetId = 0;

    [[nodiscard]] friend constexpr bool operator==(VisualGraphBehaviourInstanceKey lhs, VisualGraphBehaviourInstanceKey rhs) noexcept = default;
};

struct VisualGraphBehaviourInstance {
    kb::scene::SceneEntity entity{};
    kb::assets::AssetId assetId{};
    VisualGraphRuntimeExecutionContext context;
};

class VisualGraphBehaviourInstanceRegistry final {
public:
    [[nodiscard]] VisualGraphBehaviourInstance& FindOrCreate(kb::scene::SceneEntity entity, kb::assets::AssetId assetId);
    [[nodiscard]] VisualGraphBehaviourInstance* Find(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) noexcept;
    [[nodiscard]] const VisualGraphBehaviourInstance* Find(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) const noexcept;
    [[nodiscard]] std::size_t Count() const noexcept;

    void Remove(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) noexcept;
    void RemoveEntity(kb::scene::SceneEntity entity) noexcept;
    void RemoveAsset(kb::assets::AssetId assetId) noexcept;
    void Clear() noexcept;

private:
    struct KeyHasher {
        [[nodiscard]] std::size_t operator()(VisualGraphBehaviourInstanceKey key) const noexcept;
    };

    [[nodiscard]] static VisualGraphBehaviourInstanceKey MakeKey(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) noexcept;

    std::unordered_map<VisualGraphBehaviourInstanceKey, VisualGraphBehaviourInstance, KeyHasher> instances_;
};

} // namespace kb::visual
