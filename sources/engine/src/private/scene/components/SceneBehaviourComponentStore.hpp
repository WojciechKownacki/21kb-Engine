#pragma once

#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneBehaviourComponentStore {
public:
    SceneBehaviourComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const BehaviourComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] BehaviourComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const BehaviourComponent& behaviour);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
    // An entity is about to leave the world: a behaviour it carries is gone with it.
    void NoteEntityDestroyed(SceneEntity entity) noexcept;

    // Moves whenever a behaviour is set, removed or marked modified (the call that follows a write through TryGet), or
    // an entity that carries one is destroyed. Cheap enough to read after every script call.
    [[nodiscard]] std::uint64_t Revision() const noexcept { return revision_; }
    // The counter itself, for a loop that reads it after every call and cannot afford a function call to do so; it
    // lives as long as the store.
    [[nodiscard]] const std::uint64_t* RevisionCounter() const noexcept { return &revision_; }
    // Revision plus what the world stamps for the archetypes that hold behaviours, so it also moves for changes that
    // did not pass through this store (snapshot restore, bulk loads). One value to compare before visiting any behaviour.
    [[nodiscard]] std::uint64_t ChangeStamp() const noexcept;

private:
    kb::ecs::World* world_ = nullptr;
    std::uint64_t componentId_ = 0U;
    std::uint64_t revision_ = 0U;
};

} // namespace kb::scene
