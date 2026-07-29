#pragma once

#include "engine/gameplay/Damage.hpp"
#include "engine/gameplay/GameplayIdentity.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace kb::gameplay {

using GameplayItemId = std::uint64_t;
using AttributeId = GameplayTagId;

struct HealthState { float current = 0.0F; float maximum = 0.0F; };
struct AttributeState { float current = 0.0F; float minimum = 0.0F; float maximum = 0.0F; };
struct PickupState { GameplayItemId item = 0U; std::uint32_t quantity = 0U; };

// A scene or game mode owns this registry. It stores optional gameplay data
// externally, so entities never acquire mandatory health or inventory fields.
class GameplayModules final {
public:
    [[nodiscard]] bool AddHealth(kb::scene::SceneEntity entity, HealthState state);
    [[nodiscard]] std::optional<HealthState> Health(kb::scene::SceneEntity entity) const noexcept;
    [[nodiscard]] bool ApplyDamage(const DamageResolution& resolution) noexcept;
    [[nodiscard]] bool SetAttribute(kb::scene::SceneEntity entity, AttributeId id, AttributeState state);
    [[nodiscard]] std::optional<AttributeState> Attribute(kb::scene::SceneEntity entity, AttributeId id) const noexcept;
    [[nodiscard]] bool SpendAttribute(kb::scene::SceneEntity entity, AttributeId id, float amount) noexcept;
    [[nodiscard]] bool AddItems(kb::scene::SceneEntity entity, GameplayItemId item, std::uint32_t quantity);
    [[nodiscard]] std::uint32_t ItemCount(kb::scene::SceneEntity entity, GameplayItemId item) const noexcept;
    [[nodiscard]] bool Equip(kb::scene::SceneEntity entity, GameplayTagId slot, GameplayItemId item);
    [[nodiscard]] std::optional<GameplayItemId> Equipped(kb::scene::SceneEntity entity, GameplayTagId slot) const noexcept;
    [[nodiscard]] bool RegisterPickup(kb::scene::SceneEntity entity, PickupState pickup);
    [[nodiscard]] bool CollectPickup(kb::scene::SceneEntity pickup, kb::scene::SceneEntity collector);
    [[nodiscard]] bool Remove(kb::scene::SceneEntity entity) noexcept;

private:
    using EntityId = kb::scene::SceneEntity::IdType;
    std::unordered_map<EntityId, HealthState> health_;
    std::unordered_map<EntityId, std::unordered_map<AttributeId, AttributeState>> attributes_;
    std::unordered_map<EntityId, std::unordered_map<GameplayItemId, std::uint32_t>> inventories_;
    std::unordered_map<EntityId, std::unordered_map<GameplayTagId, GameplayItemId>> equipment_;
    std::unordered_map<EntityId, PickupState> pickups_;
};

} // namespace kb::gameplay
