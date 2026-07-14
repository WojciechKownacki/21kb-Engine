#pragma once

#include "engine/ecs/ComponentEvent.hpp"
#include "engine/ecs/World.hpp"
#include "engine/library/EngineLibraryScriptComponentAccess.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace kb::library {

// LIB-090: whether a firing for TransformComponent reflects a genuine
// LOCAL write (localPosition/localRotation/localScale), a WORLD-only
// recompute (this entity's cached worldPosition/worldRotation/worldScale
// were recomputed by SceneTransformHierarchySystem — either because a
// parent moved and this entity is a cascaded descendant, or because this
// is the deferred follow-up recompute of this entity's OWN earlier local
// write), or Both (this entity had at least one Local-classified firing
// AND at least one World-classified firing since the last Drain()).
enum class TransformChangeKind : std::uint8_t {
    Local,
    World,
    Both,
};

struct TransformChangeEntry {
    kb::scene::SceneEntity entity{};
    TransformChangeKind kind = TransformChangeKind::Local;
};

// LIB-090: ComponentChanged for TransformComponent specifically, carrying
// local/world classification — deliberately NOT built on the generic
// kb::library::ComponentChangeTracker<T> (LIB-081), because that type's
// payload is only "which entity changed," with no slot for WHY. Reuses
// LIB-081's entire skeleton otherwise: wraps the same
// kb::ecs::World::ObserveComponent<T>(Modified,...)/DestroyObserver
// primitive, gated by the same ScriptComponentAccess<T> (LIB-075) closed-set
// check, backed by the same reserve()'d fixed-capacity vector with
// linear-scan coalescing and an honest DroppedCount() rather than
// unbounded growth, and the same Drain()-resets-the-baseline contract.
//
// Classification needs NO extra history or side-storage (a naive
// version-diffing approach would need a per-entity "last seen" map, plus a
// bootstrapping answer for an entity's first-ever firing) — confirmed by
// reading the two real firing sites directly:
// TransformComponent::worldDirty on the POST-change payload the observer
// callback receives is ITSELF the discriminant.
// SceneTransformComponentStore::Set/MarkModified
// (SceneTransformStorage.cpp) explicitly set worldDirty=true on the very
// payload that triggers the Modified event for a genuine local write,
// while SceneTransformHierarchySystem's mirror-to-backend step
// (SceneTransformHierarchySystem.cpp, gated by
// world.Config().mirrorNativeComponentChangesToBackend, default true)
// fires MarkModified AFTER TransformMath::Compose* has already reset
// worldDirty=false on the stored component for every entity whose world
// was recomputed this sync (confirmed: that set includes an entity whose
// OWN local just changed, not just its cascaded descendants) — so a
// firing with worldDirty==true is a local write, and worldDirty==false is
// a world recompute (whether that recompute is this entity's own deferred
// follow-up after its local write, or a pure cascade from an ancestor's
// move).
//
// A directly-modified entity therefore fires twice per round (Local, then
// World) and coalesces to Both; a cascade-only descendant fires once
// (World only). A parent move with N children thus produces N+1
// *coalesced* pending entries (the parent Both, each child World), not an
// unbounded stream of raw firings — the fan-out is captured and labeled,
// not hidden, while staying bounded exactly like LIB-081's tracker.
class TransformChangeTracker final {
public:
    explicit TransformChangeTracker(kb::scene::Scene& scene, std::size_t capacity = 256U)
        : world_(&scene.Runtime().EcsWorld())
        , capacity_(capacity) {
        static_cast<void>(sizeof(ScriptComponentAccess<kb::scene::TransformComponent>));
        changed_.reserve(capacity_);
        observerId_ = world_->ObserveComponent<kb::scene::TransformComponent>(kb::ecs::ComponentEventKind::Modified, &OnModified, this);
    }

    ~TransformChangeTracker() {
        world_->DestroyObserver(observerId_);
    }

    TransformChangeTracker(const TransformChangeTracker&) = delete;
    TransformChangeTracker& operator=(const TransformChangeTracker&) = delete;
    TransformChangeTracker(TransformChangeTracker&&) = delete;
    TransformChangeTracker& operator=(TransformChangeTracker&&) = delete;

    [[nodiscard]] std::size_t Capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::span<const TransformChangeEntry> PendingChanges() const noexcept { return changed_; }
    // How many DISTINCT-entity modifications were dropped (limit reached)
    // since the last Drain() — never silently absorbed.
    [[nodiscard]] std::size_t DroppedCount() const noexcept { return droppedCount_; }

    // Returns every entry recorded since the last Drain() (or since
    // construction, for the first call), then clears both the pending list
    // and the dropped count — "since the last Drain()" becomes the new
    // baseline.
    [[nodiscard]] std::vector<TransformChangeEntry> Drain() {
        std::vector<TransformChangeEntry> drained = std::move(changed_);
        changed_.clear();
        changed_.reserve(capacity_);
        droppedCount_ = 0U;
        return drained;
    }

private:
    [[nodiscard]] static TransformChangeKind Widen(TransformChangeKind existing, TransformChangeKind incoming) noexcept {
        return existing == incoming ? existing : TransformChangeKind::Both;
    }

    static void OnModified(kb::ecs::Entity entity, kb::ecs::ComponentEventKind event, const kb::scene::TransformComponent* transform, void* context) noexcept {
        if (event != kb::ecs::ComponentEventKind::Modified || transform == nullptr) {
            return;
        }
        auto* self = static_cast<TransformChangeTracker*>(context);
        const TransformChangeKind kind = transform->worldDirty ? TransformChangeKind::Local : TransformChangeKind::World;

        const auto existing = std::ranges::find(self->changed_, entity, &TransformChangeEntry::entity);
        if (existing != self->changed_.end()) {
            existing->kind = Widen(existing->kind, kind); // Coalesced — widen, never narrow.
            return;
        }
        if (self->changed_.size() >= self->capacity_) {
            ++self->droppedCount_;
            return;
        }
        self->changed_.push_back(TransformChangeEntry{ entity, kind });
    }

    kb::ecs::World* world_ = nullptr;
    kb::ecs::ObserverId observerId_ = 0U;
    std::size_t capacity_ = 0U;
    std::vector<TransformChangeEntry> changed_;
    std::size_t droppedCount_ = 0U;
};

} // namespace kb::library
