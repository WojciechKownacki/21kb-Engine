#pragma once

#include "engine/ecs/CommandBuffer.hpp"
#include "engine/library/EngineLibraryEntityHandle.hpp"
#include "engine/library/EngineLibraryScriptComponentAccess.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/TagsComponent.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::library {

// LIB-080: a handle for an entity recorded by CommandBatch::Spawn() but not
// yet real — kb::ecs::CommandBuffer's own CommandEntity (Existing(Entity)
// or Deferred(workerIndex, localIndex)) resolves to a genuine Entity only
// once Flush() (CommandBuffer::Playback) runs. Deliberately NOT an
// EntityHandle: EntityHandle::IsAlive/Validate assume a real, checkable
// entity, which a freshly-spawned-but-not-yet-played-back one is not.
class BatchEntity final {
public:
    constexpr BatchEntity() noexcept = default;
    explicit constexpr BatchEntity(kb::ecs::CommandEntity commandEntity) noexcept
        : commandEntity_(commandEntity) {}

    [[nodiscard]] constexpr kb::ecs::CommandEntity Raw() const noexcept { return commandEntity_; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return commandEntity_.IsValid(); }

private:
    kb::ecs::CommandEntity commandEntity_{};
};

// LIB-080: the first script-facing wrapper over kb::ecs::CommandBuffer —
// EngineLibraryCommandApplication.hpp's own doc comment already named this
// exact task as the mechanism a script needs "to record [structural]
// changes while a Query loop is open" instead of applying them
// immediately (which kb::ecs::StructuralChangeValidator rejects, LIB-078).
// Native C++ script code only, like EntityHandle (LIB-075) and Query<T>
// (LIB-078/079) — Lua/Visual Graph cannot express these C++ templates.
//
// Spawn/Destroy/Add<T>/Remove<T> are thin wrappers over kb::ecs::
// CommandBuffer::WorkerBuffer's already-existing templated Set<T>/
// Remove<T>/CreateEntity/DestroyEntity — genuinely new work was only
// needed for two things:
//
// 1. Component types are restricted to the exact same closed set LIB-075/
//    076/077/078/079 already established (Transform/Visibility/Behaviour/
//    Camera/Light/MeshRenderer) — reusing kb::library::ScriptComponentAccess<T>
//    (LIB-075) as the compile-time gate directly (its primary template's
//    static_assert fires for anything else) rather than duplicating that
//    trait a second time.
//
// 2. Tag assignment ("przypisania tagów", this task's own second half) has
//    NO command-buffer support in kb::ecs at all — kb::ecs::World::AddTag<T>/
//    RemoveTag<T> is a completely different, TYPE-based ECS tag mechanism
//    scripts have never touched; the tag concept scripts actually use
//    (World.SetTag/HasTag/FindByTag, since LIB-065..069) is a STRING stored
//    in kb::scene::TagsComponent. AddTag/RemoveTag COALESCE per target: the
//    first touch of an existing entity seeds a pending tag set from its live
//    tags, every later AddTag/RemoveTag in this batch mutates that same
//    pending set (read-your-own-writes, so "add A then add B" keeps BOTH,
//    not last-write-wins), and Flush() materializes exactly one
//    Set<TagsComponent> (or Remove, if the set ends empty) per target. A
//    BatchEntity spawned in this same batch is a first-class tag target too:
//    its pending set starts empty and its final Set<TagsComponent> is queued
//    against the deferred CommandEntity, which Playback resolves to the
//    just-created entity in the same pass (the same mechanism Add<T>(
//    BatchEntity) already uses). This closes the audit's two LIB-080 tag
//    gaps (last-write-wins across calls; no tags on a spawned-this-batch
//    entity).
//
// Flush() applies every recorded command via kb::ecs::CommandBuffer::
// Playback(world) — the SAME World::CreateEntity/SetComponent/etc. every
// direct mutation already uses, so it is validated by the SAME
// kb::ecs::StructuralChangeValidator (LIB-078) and THROWS std::logic_error
// if called while a Query<T>::ForEach iteration is still open. kb::library
// never calls Flush() on a script's behalf (matching
// CommandApplicationPointFor's own "no phase performs this automatically"
// contract, EngineLibraryCommandApplication.hpp) — a script must call it
// explicitly, after any open Query loop has returned.
//
// LIB-012: every EntityHandle-targeted command (Destroy/Add<T>/Remove<T>/
// AddTag/RemoveTag) is cancelled — not queued at all — for a handle already
// dead when the call is made, and re-checked again at Flush() for one that
// went stale AFTER being recorded but BEFORE Flush() ran (see Flush()'s own
// comment). Neither case throws; both are ordinary, expected outcomes of a
// script racing its own command batch against a direct destroy elsewhere.
class CommandBatch final {
public:
    explicit CommandBatch(kb::scene::Scene& scene) noexcept
        : scene_(&scene)
        , commandBuffer_(1) {}

    [[nodiscard]] BatchEntity Spawn(std::string_view name = {}) {
        return BatchEntity{ commandBuffer_.Worker(0).CreateEntity(name) };
    }

    void Destroy(BatchEntity entity) {
        commandBuffer_.Worker(0).DestroyEntity(entity.Raw());
    }

    // LIB-012: false (no command queued), not a crash, for an already-dead
    // handle — the same honest contract QueueTagChange already established
    // for AddTag/RemoveTag. `entity` is also tracked so Flush() can re-check
    // it: kb::ecs::World::ValidateEntityHandle (reached through
    // CommandBuffer::Playback's Apply/Destroy phases) throws std::
    // out_of_range for a target that was ALIVE when recorded here but got
    // destroyed through some unrelated, immediate path before Flush() ran —
    // a real race this single-threaded-but-reentrant-via-scripts engine can
    // genuinely hit (one behaviour records a command against an entity a
    // LATER-dispatched behaviour in the same frame then destroys directly).
    [[nodiscard]] bool Destroy(EntityHandle entity) {
        if (!entity.IsAlive(*scene_)) {
            return false;
        }
        commandBuffer_.Worker(0).DestroyEntity(entity.Entity());
        trackedTargets_.push_back(entity);
        return true;
    }

    template <typename Component>
    void Add(BatchEntity entity, const Component& value) {
        static_cast<void>(sizeof(ScriptComponentAccess<Component>));
        commandBuffer_.Worker(0).Set<Component>(entity.Raw(), value);
    }

    // See the EntityHandle overload of Destroy() for why this returns bool
    // and tracks `entity`.
    template <typename Component>
    [[nodiscard]] bool Add(EntityHandle entity, const Component& value) {
        static_cast<void>(sizeof(ScriptComponentAccess<Component>));
        if (!entity.IsAlive(*scene_)) {
            return false;
        }
        commandBuffer_.Worker(0).Set<Component>(entity.Entity(), value);
        trackedTargets_.push_back(entity);
        return true;
    }

    template <typename Component>
    void Remove(BatchEntity entity) {
        static_cast<void>(sizeof(ScriptComponentAccess<Component>));
        commandBuffer_.Worker(0).Remove<Component>(entity.Raw());
    }

    // See the EntityHandle overload of Destroy() for why this returns bool
    // and tracks `entity`.
    template <typename Component>
    [[nodiscard]] bool Remove(EntityHandle entity) {
        static_cast<void>(sizeof(ScriptComponentAccess<Component>));
        if (!entity.IsAlive(*scene_)) {
            return false;
        }
        commandBuffer_.Worker(0).Remove<Component>(entity.Entity());
        trackedTargets_.push_back(entity);
        return true;
    }

    // Accumulates `tag` into a per-target pending tag set that is materialized
    // into a single Set<TagsComponent> (or Remove, if the set ends empty) at
    // Flush(). Calling AddTag/RemoveTag repeatedly for the SAME target within
    // one batch reads-your-own-writes: the second call sees the first call's
    // effect (it mutates the same pending set), so a batch that adds "A" then
    // "B" ends with BOTH, not last-write-wins. An existing entity's pending
    // set is seeded ONCE from its live tags the first time it is touched;
    // every later op in this batch builds on that snapshot. False (no change
    // recorded) for a dead handle or an empty/whitespace tag.
    [[nodiscard]] bool AddTag(EntityHandle entity, std::string_view tag) {
        if (!entity.IsAlive(*scene_)) {
            return false;
        }
        return ApplyPendingTag(ResolveExistingPendingTags(entity), tag, true);
    }

    [[nodiscard]] bool RemoveTag(EntityHandle entity, std::string_view tag) {
        if (!entity.IsAlive(*scene_)) {
            return false;
        }
        return ApplyPendingTag(ResolveExistingPendingTags(entity), tag, false);
    }

    // LIB-080: tags for an entity SPAWNED in this same batch. A fresh
    // CommandEntity has no live tags to read, so its pending set starts
    // empty and accumulates purely from this batch's AddTag/RemoveTag calls;
    // Flush() materializes the Set<TagsComponent> against the deferred entity,
    // which kb::ecs::CommandBuffer::Playback resolves to the just-created
    // entity in the same pass (the same "spawn X then Set component on X"
    // pattern Add<T>(BatchEntity) already relies on). This closes LIB-080's
    // originally-documented BatchEntity gap. False for an invalid handle or
    // an empty/whitespace tag.
    [[nodiscard]] bool AddTag(BatchEntity entity, std::string_view tag) {
        if (!entity.IsValid()) {
            return false;
        }
        return ApplyPendingTag(ResolveDeferredPendingTags(entity), tag, true);
    }

    [[nodiscard]] bool RemoveTag(BatchEntity entity, std::string_view tag) {
        if (!entity.IsValid()) {
            return false;
        }
        return ApplyPendingTag(ResolveDeferredPendingTags(entity), tag, false);
    }

    // LIB-012: nullopt — not a crash, not a partial/silent apply — if any
    // EntityHandle-targeted command recorded by this batch (Destroy/Add/
    // Remove/AddTag/RemoveTag) has since gone stale (its target was
    // destroyed by something other than this batch between the recording
    // call and this Flush()). Re-checks every tracked target BEFORE calling
    // Playback, so a stale target is caught here — an honest "nothing in
    // this batch applied" — rather than reaching kb::ecs::CommandBuffer::
    // Playback, which would throw std::out_of_range (World::
    // ValidateEntityHandle) and roll back everything Playback itself had
    // already applied; this pre-check keeps Playback's own throw reserved
    // for genuine command-buffer misuse (e.g. a malformed deferred
    // CommandEntity reference), not a legitimate same-frame destroy race.
    // BatchEntity-targeted commands need no such check: a freshly-spawned
    // entity is not resolvable (and therefore not destroyable) by any other
    // code until this same Flush() creates it.
    [[nodiscard]] std::optional<kb::ecs::CommandBufferPlaybackResult> Flush() {
        for (const EntityHandle& target : trackedTargets_) {
            if (!target.IsAlive(*scene_)) {
                return std::nullopt;
            }
        }
        // LIB-080: an existing-entity tag target that went stale between its
        // AddTag/RemoveTag call and now is the same honest "nothing applied"
        // race as trackedTargets_ above — checked before any tag command is
        // queued so a stale target never reaches Playback.
        for (const PendingTagState& pending : pendingTags_) {
            if (pending.existing && !pending.handle.IsAlive(*scene_)) {
                return std::nullopt;
            }
        }
        // Materialize the coalesced tag sets LAST (after every other recorded
        // command): one Set<TagsComponent> per target holding the batch's
        // final accumulated tags, or Remove<TagsComponent> for a target whose
        // set ended empty. A spawned-this-batch target with no tags never had
        // the component, so nothing is queued for it.
        for (const PendingTagState& pending : pendingTags_) {
            if (pending.tags.empty()) {
                if (pending.existing) {
                    commandBuffer_.Worker(0).Remove<kb::scene::TagsComponent>(pending.entity);
                }
                continue;
            }
            kb::scene::TagsComponent tagsComponent;
            kb::scene::SetTagsText(tagsComponent, JoinTagList(pending.tags));
            commandBuffer_.Worker(0).Set<kb::scene::TagsComponent>(pending.entity, tagsComponent);
        }
        return commandBuffer_.Playback(scene_->Runtime().EcsWorld());
    }

private:
    // LIB-080: the coalesced pending tag state for one target within this
    // batch. `entity` is the CommandEntity every tag command for this target
    // is queued against at Flush(); `existing` distinguishes an already-live
    // EntityHandle target (whose `handle` must be re-checked for liveness at
    // Flush, and whose initial `tags` were seeded from its live tags) from a
    // spawned-this-batch BatchEntity (empty seed, no liveness concept).
    struct PendingTagState {
        kb::ecs::CommandEntity entity{};
        bool existing = false;
        EntityHandle handle{};
        std::vector<std::string> tags;
    };

    [[nodiscard]] static bool SameCommandEntity(kb::ecs::CommandEntity lhs, kb::ecs::CommandEntity rhs) noexcept {
        if (lhs.IsDeferred() != rhs.IsDeferred()) {
            return false;
        }
        if (lhs.IsDeferred()) {
            return lhs.WorkerIndex() == rhs.WorkerIndex() && lhs.LocalIndex() == rhs.LocalIndex();
        }
        return lhs.ExistingEntity().Id() == rhs.ExistingEntity().Id();
    }

    [[nodiscard]] PendingTagState* FindPendingTags(kb::ecs::CommandEntity entity) noexcept {
        for (PendingTagState& pending : pendingTags_) {
            if (SameCommandEntity(pending.entity, entity)) {
                return &pending;
            }
        }
        return nullptr;
    }

    // Seeds the pending set from the existing entity's LIVE tags exactly once
    // (the first time it is touched this batch); later calls reuse the same
    // accumulating set, which is what makes repeated AddTag/RemoveTag on one
    // entity read-your-own-writes instead of last-write-wins.
    [[nodiscard]] PendingTagState& ResolveExistingPendingTags(EntityHandle entity) {
        const kb::ecs::CommandEntity commandEntity = kb::ecs::CommandEntity::Existing(entity.Entity());
        if (PendingTagState* pending = FindPendingTags(commandEntity)) {
            return *pending;
        }
        std::vector<std::string> initialTags;
        if (const kb::scene::TagsComponent* current = scene_->Components().Tags().TryGet(entity.Entity())) {
            initialTags = ParseTagList(kb::scene::TagsText(*current));
        }
        pendingTags_.push_back(PendingTagState{ commandEntity, true, entity, std::move(initialTags) });
        return pendingTags_.back();
    }

    [[nodiscard]] PendingTagState& ResolveDeferredPendingTags(BatchEntity entity) {
        if (PendingTagState* pending = FindPendingTags(entity.Raw())) {
            return *pending;
        }
        pendingTags_.push_back(PendingTagState{ entity.Raw(), false, EntityHandle{}, {} });
        return pendingTags_.back();
    }

    [[nodiscard]] static bool ApplyPendingTag(PendingTagState& pending, std::string_view tag, bool add) {
        const std::string normalizedTag = TrimTag(tag);
        if (normalizedTag.empty()) {
            return false;
        }
        const auto existing = std::ranges::find(pending.tags, normalizedTag);
        if (add) {
            if (existing == pending.tags.end()) {
                pending.tags.push_back(normalizedTag);
            }
        } else if (existing != pending.tags.end()) {
            pending.tags.erase(existing);
        }
        return true;
    }

    [[nodiscard]] static std::string TrimTag(std::string_view value) {
        const std::size_t begin = value.find_first_not_of(" \t\r\n");
        if (begin == std::string_view::npos) {
            return {};
        }
        const std::size_t end = value.find_last_not_of(" \t\r\n");
        return std::string{ value.substr(begin, end - begin + 1U) };
    }

    // Same comma/semicolon-separated, trimmed convention World.SetTag/
    // HasTag (ScriptWorldApi.cpp) already use for kb::scene::TagsComponent
    // — not a new tag format.
    [[nodiscard]] static std::vector<std::string> ParseTagList(std::string_view tags) {
        std::vector<std::string> parsed;
        std::size_t tokenBegin = 0U;
        while (tokenBegin <= tags.size()) {
            const std::size_t tokenEnd = tags.find_first_of(",;", tokenBegin);
            std::string tag = TrimTag(tags.substr(tokenBegin, tokenEnd == std::string_view::npos ? std::string_view::npos : tokenEnd - tokenBegin));
            if (!tag.empty()) {
                parsed.push_back(std::move(tag));
            }
            if (tokenEnd == std::string_view::npos) {
                break;
            }
            tokenBegin = tokenEnd + 1U;
        }
        return parsed;
    }

    [[nodiscard]] static std::string JoinTagList(const std::vector<std::string>& tags) {
        std::string joined;
        for (const std::string& tag : tags) {
            if (!joined.empty()) {
                joined += ", ";
            }
            joined += tag;
        }
        return joined;
    }

    kb::scene::Scene* scene_ = nullptr;
    kb::ecs::CommandBuffer commandBuffer_;
    // LIB-012: every EntityHandle an already-recorded command targets
    // (Destroy/Add/Remove/AddTag/RemoveTag) — re-checked for liveness by
    // Flush(). Not deduplicated: batches are small (script-authored, one
    // Query loop's worth of entities), and re-checking the same entity
    // twice is harmless.
    std::vector<EntityHandle> trackedTargets_;
    // LIB-080: one entry per target that received an AddTag/RemoveTag this
    // batch, holding its coalesced final tag set — materialized into a single
    // tag command per target at Flush().
    std::vector<PendingTagState> pendingTags_;
};

} // namespace kb::library
