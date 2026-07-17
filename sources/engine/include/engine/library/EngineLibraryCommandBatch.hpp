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
//    in kb::scene::TagsComponent. AddTag/RemoveTag here queue a
//    Set<TagsComponent> command built the same way World.SetTag already
//    computes it (read current tags, add/remove one, rejoin) — real,
//    working code, not a stub. Scoped HONESTLY to EntityHandle (already-
//    real entities) only, not BatchEntity (freshly spawned within the same
//    batch): a fresh CommandEntity has no live TagsComponent to read from
//    yet, and correctly accumulating multiple AddTag calls for the same
//    not-yet-real entity before Playback resolves it is meaningfully more
//    machinery (a second pending-state map, keyed by the deferred entity's
//    worker/local index, applied in a second pass after Playback) than
//    this task's proportionate scope — a real, documented gap, not a
//    silent one: AddTag/RemoveTag simply do not accept a BatchEntity at
//    the type level, so a caller who tries gets a compile error, never a
//    dropped tag at runtime.
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

    // Queues a Set<TagsComponent> command that adds `tag` to the entity's
    // CURRENT tag set (read now, at record time — a plain scene read, not
    // a structural change, so it is safe to call from inside an open
    // Query<T>::ForEach). Calling this more than once for the SAME entity
    // within the same batch (before Flush()) is an honestly-documented
    // last-write-wins: each call independently reads the LIVE entity's
    // current tags, not any earlier queued-but-not-yet-applied command
    // from this same batch. False (no command queued) for a dead handle.
    [[nodiscard]] bool AddTag(EntityHandle entity, std::string_view tag) {
        return QueueTagChange(entity, tag, true);
    }

    [[nodiscard]] bool RemoveTag(EntityHandle entity, std::string_view tag) {
        return QueueTagChange(entity, tag, false);
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
        return commandBuffer_.Playback(scene_->Runtime().EcsWorld());
    }

private:
    [[nodiscard]] bool QueueTagChange(EntityHandle entity, std::string_view tag, bool add) {
        if (!entity.IsAlive(*scene_)) {
            return false;
        }
        const std::string normalizedTag = TrimTag(tag);
        if (normalizedTag.empty()) {
            return false;
        }

        std::vector<std::string> tags;
        if (const kb::scene::TagsComponent* current = scene_->Components().Tags().TryGet(entity.Entity())) {
            tags = ParseTagList(kb::scene::TagsText(*current));
        }
        const auto existing = std::ranges::find(tags, normalizedTag);
        if (add) {
            if (existing == tags.end()) {
                tags.push_back(normalizedTag);
            }
        } else if (existing != tags.end()) {
            tags.erase(existing);
        }

        // Matches World.SetTag's existing convention exactly (ScriptWorldApi.cpp):
        // an empty tag list removes the component entirely rather than
        // queuing a Set with an empty string — the same "no tags" state
        // TryGet(entity) == nullptr already represents everywhere else.
        if (tags.empty()) {
            commandBuffer_.Worker(0).Remove<kb::scene::TagsComponent>(entity.Entity());
        } else {
            kb::scene::TagsComponent tagsComponent;
            kb::scene::SetTagsText(tagsComponent, JoinTagList(tags));
            commandBuffer_.Worker(0).Set<kb::scene::TagsComponent>(entity.Entity(), tagsComponent);
        }
        trackedTargets_.push_back(entity);
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
};

} // namespace kb::library
