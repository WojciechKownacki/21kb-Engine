#pragma once

#include "engine/ecs/ComponentEvent.hpp"
#include "engine/ecs/World.hpp"
#include "engine/library/EngineLibraryScriptComponentAccess.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneRuntime.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace kb::library {

// LIB-081: a bounded, coalesced, allocation-free-per-change notification
// of "this component was modified" for one component type. Wraps the
// engine's EXISTING push-based observer primitive
// (kb::ecs::World::ObserveComponent<T>, backed by a real flecs observer,
// already firing on every World::Set<T>/MarkModified<T> call — confirmed
// by EcsEventTests.cpp::RunComponentObserverTest) rather than inventing a
// new event mechanism; that primitive itself has no limit, no coalescing,
// and fires once per raw mutation (two Set calls in one frame = two
// notifications) — this class is what adds those three properties on top.
//
// Deliberately NOT built on kb::script's Events.Subscribe-style generic
// event bus: that mechanism does not exist yet (LIB-105, still blocked on
// LIB-095/097-100 per others/_temp.md's own POWRÓT notes) and this task
// does not require it — a script polls Drain() at a point of its own
// choosing (e.g. once per Tick), mirroring the same pattern
// ScriptRuntimeSceneSystem::DispatchPendingSceneLifecycleEvents (LIB-073)
// established for a different flat notification queue, except LIB-081's
// "limit"/"coalescing" requirements call for a FIXED-CAPACITY structure
// instead of LIB-073's unbounded vector.
//
// Restricted to the same closed set of six components LIB-075/077/078/080
// already established (Transform/Visibility/Behaviour/Camera/Light/
// MeshRenderer) — reuses kb::library::ScriptComponentAccess<T> (LIB-075)
// directly as the compile-time gate rather than duplicating that trait a
// third time.
//
// "No allocation per change": coalescing is a LINEAR SCAN over an
// already-reserved, fixed-capacity std::vector, not a hash set — even a
// reserve()'d std::unordered_set can allocate one new node per insert on
// this standard library (MSVC), which would violate the requirement; a
// scan bounded by `capacity` (expected to be small, a few hundred at
// most) allocates nothing, ever, once the vector's initial reserve has
// run. "Limit": once `capacity` distinct entities have been recorded
// since the last Drain(), further modifications are honestly counted via
// DroppedCount() rather than silently lost or grown into an unbounded
// allocation.
template <typename Component>
class ComponentChangeTracker final {
public:
    explicit ComponentChangeTracker(kb::scene::Scene& scene, std::size_t capacity = 256U)
        : scene_(&scene)
        , world_(&scene.Runtime().EcsWorld())
        , capacity_(capacity) {
        static_cast<void>(sizeof(ScriptComponentAccess<Component>));
        changed_.reserve(capacity_);
        observerId_ = world_->ObserveComponent<Component>(kb::ecs::ComponentEventKind::Modified, &OnModified, this);
    }

    ~ComponentChangeTracker() {
        world_->DestroyObserver(observerId_);
    }

    ComponentChangeTracker(const ComponentChangeTracker&) = delete;
    ComponentChangeTracker& operator=(const ComponentChangeTracker&) = delete;
    ComponentChangeTracker(ComponentChangeTracker&&) = delete;
    ComponentChangeTracker& operator=(ComponentChangeTracker&&) = delete;

    [[nodiscard]] std::size_t Capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::span<const kb::scene::SceneEntity> PendingChanges() const noexcept { return changed_; }
    // How many DISTINCT-entity modifications were dropped (limit reached)
    // since the last Drain() — never silently absorbed.
    [[nodiscard]] std::size_t DroppedCount() const noexcept { return droppedCount_; }

    // Returns every entity whose Component changed since the last Drain()
    // (or since construction, for the first call), then clears both the
    // pending list and the dropped count — "since the last Drain()"
    // becomes the new baseline.
    [[nodiscard]] std::vector<kb::scene::SceneEntity> Drain() {
        std::vector<kb::scene::SceneEntity> drained = std::move(changed_);
        changed_.clear();
        changed_.reserve(capacity_);
        droppedCount_ = 0U;
        return drained;
    }

private:
    static void OnModified(kb::ecs::Entity entity, kb::ecs::ComponentEventKind event, const Component*, void* context) noexcept {
        if (event != kb::ecs::ComponentEventKind::Modified) {
            return;
        }
        auto* self = static_cast<ComponentChangeTracker*>(context);
        if (std::ranges::find(self->changed_, entity) != self->changed_.end()) {
            return; // Already recorded since the last Drain() — coalesced.
        }
        if (self->changed_.size() >= self->capacity_) {
            ++self->droppedCount_;
            return;
        }
        self->changed_.push_back(entity);
    }

    kb::scene::Scene* scene_ = nullptr;
    kb::ecs::World* world_ = nullptr;
    kb::ecs::ObserverId observerId_ = 0U;
    std::size_t capacity_ = 0U;
    std::vector<kb::scene::SceneEntity> changed_;
    std::size_t droppedCount_ = 0U;
};

} // namespace kb::library
