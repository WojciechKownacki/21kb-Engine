#pragma once

#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneVisitorTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace kb::scene {

class Scene;

class SceneBehaviourComponentQueries {
public:
    explicit SceneBehaviourComponentQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const BehaviourComponent* TryGet(SceneEntity entity) const noexcept;
    // An entity runs up to kMaxBehavioursPerEntity behaviours. TryGet is the first one (index 0); the others follow it in the
    // order they were attached. ForEach visits every behaviour of every entity, an entity's own in that order.
    [[nodiscard]] std::size_t Count(SceneEntity entity) const noexcept;
    [[nodiscard]] const BehaviourComponent* TryGetAt(SceneEntity entity, std::size_t index) const noexcept;
    // The behaviour that runs this script on the entity, or null.
    [[nodiscard]] const BehaviourComponent* Find(SceneEntity entity, std::uint64_t assetId, BehaviourBackend backend) const noexcept;
    [[nodiscard]] std::span<const BehaviourComponent> Additional(SceneEntity entity) const noexcept;
    void ForEach(BehaviourVisitor visitor, void* context = nullptr) const;
    // Moves whenever a behaviour is set, removed or marked modified (the call that follows a write through TryGet), or
    // an entity that carries one is destroyed. The counter is read directly, so a loop can compare it after every
    // script call.
    [[nodiscard]] const std::uint64_t* RevisionCounter() const noexcept;
    // Moves for every change to the scene's behaviours, including those that bypass the calls above. One value to
    // compare before visiting any of them.
    [[nodiscard]] std::uint64_t ChangeStamp() const noexcept;

private:
    const Scene& scene_;
};

class SceneBehaviourComponents {
public:
    explicit SceneBehaviourComponents(Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const BehaviourComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] BehaviourComponent* TryGet(SceneEntity entity) noexcept;
    // Sets the first behaviour of the entity; the others stay.
    void Set(SceneEntity entity, const BehaviourComponent& behaviour);
    // Removes every behaviour of the entity.
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
    [[nodiscard]] std::size_t Count(SceneEntity entity) const noexcept;
    [[nodiscard]] const BehaviourComponent* TryGetAt(SceneEntity entity, std::size_t index) const noexcept;
    [[nodiscard]] const BehaviourComponent* Find(SceneEntity entity, std::uint64_t assetId, BehaviourBackend backend) const noexcept;
    [[nodiscard]] std::span<const BehaviourComponent> Additional(SceneEntity entity) const noexcept;
    // Adds a behaviour after the entity's others (the first one becomes its component). False when the entity runs the
    // script already or runs kMaxBehavioursPerEntity.
    [[nodiscard]] bool Add(SceneEntity entity, const BehaviourComponent& behaviour);
    // Takes the behaviour at the index away (0 is the first); the following ones move up. False when there is none.
    bool RemoveAt(SceneEntity entity, std::size_t index) noexcept;
    // Writes the behaviour at the index; false when there is none.
    bool SetAt(SceneEntity entity, std::size_t index, const BehaviourComponent& behaviour);
    // Makes the behaviours after the first exactly `additional` (repeats of a script and what passes the limit are dropped).
    void SetAdditional(SceneEntity entity, std::span<const BehaviourComponent> additional);
    void ForEach(BehaviourVisitor visitor, void* context = nullptr) const;
    // See SceneBehaviourComponentQueries.
    [[nodiscard]] const std::uint64_t* RevisionCounter() const noexcept;
    [[nodiscard]] std::uint64_t ChangeStamp() const noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
