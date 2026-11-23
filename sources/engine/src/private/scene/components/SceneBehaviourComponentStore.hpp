#pragma once

#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneBehaviourComponentStore {
public:
    SceneBehaviourComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const BehaviourComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] BehaviourComponent* TryGet(SceneEntity entity) noexcept;
    // Sets the entity's first behaviour (the component); the others stay. A script an additional behaviour already runs
    // is not run twice: the additional entry gives way.
    void Set(SceneEntity entity, const BehaviourComponent& behaviour);
    // Removes every behaviour of the entity.
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

    // An entity runs up to kMaxBehavioursPerEntity behaviours: the component, then the additional ones in attach order.
    // Index 0 is the component. An entity without the component has none.
    static constexpr std::size_t kNoBehaviour = static_cast<std::size_t>(-1);
    [[nodiscard]] std::size_t Count(SceneEntity entity) const noexcept;
    [[nodiscard]] const BehaviourComponent* TryGetAt(SceneEntity entity, std::size_t index) const noexcept;
    // The index of the behaviour that runs this script, or kNoBehaviour.
    [[nodiscard]] std::size_t IndexOf(SceneEntity entity, std::uint64_t assetId, BehaviourBackend backend) const noexcept;
    [[nodiscard]] std::span<const BehaviourComponent> Additional(SceneEntity entity) const noexcept;
    // Adds a behaviour after the others (the first one becomes the component). False, and nothing changes, when the entity
    // already runs the script or runs kMaxBehavioursPerEntity.
    [[nodiscard]] bool Add(SceneEntity entity, const BehaviourComponent& behaviour);
    // Takes one behaviour away; the next one moves up. False when there is none at the index.
    bool RemoveAt(SceneEntity entity, std::size_t index) noexcept;
    // Writes the behaviour at the index (0 is the component).
    bool SetAt(SceneEntity entity, std::size_t index, const BehaviourComponent& behaviour);
    // Makes the additional behaviours exactly `additional`; without the component there are none.
    void SetAdditional(SceneEntity entity, std::span<const BehaviourComponent> additional);
    // Visits every additional behaviour (the component of an entity is visited by the world's query).
    void ForEachAdditional(void (*visitor)(SceneEntity, const BehaviourComponent&, void*), void* context) const;
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
    std::unordered_map<SceneEntity::IdType, std::vector<BehaviourComponent>> additional_;
};

} // namespace kb::scene
