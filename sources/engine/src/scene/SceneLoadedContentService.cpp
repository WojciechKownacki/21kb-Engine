#include "scene/SceneLoadedContentService.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>

namespace kb::scene {
namespace {

[[nodiscard]] SceneState::LoadedSceneRecord* FindRecord(SceneState& state, std::uint64_t id) noexcept {
    const auto iterator = std::ranges::find_if(state.loadedScenes, [id](const SceneState::LoadedSceneRecord& record) { return record.id == id; });
    return iterator == state.loadedScenes.end() ? nullptr : &*iterator;
}

[[nodiscard]] bool RecordExists(const SceneState& state, std::uint64_t id) noexcept {
    return std::ranges::find_if(state.loadedScenes, [id](const SceneState::LoadedSceneRecord& record) { return record.id == id; }) != state.loadedScenes.end();
}

// LIB-073: pushes one notification onto SceneState's pending queue —
// SceneLoadedContentService never dispatches a real ScriptEvent itself
// (kb::scene has no reach to kb::script::ScriptRuntime); this queue is the
// "unambiguous command deferred to a queue" the whole notification relies
// on, drained by kb::script::ScriptRuntimeSceneSystem once per frame.
void QueueLifecycleEvent(SceneState& state, std::string name, std::uint64_t sceneId, std::string sceneName) {
    state.pendingSceneLifecycleEvents.push_back(SceneState::PendingSceneLifecycleEvent{
        .name = std::move(name),
        .sceneId = sceneId,
        .sceneName = std::move(sceneName),
    });
}

} // namespace

std::uint64_t SceneLoadedContentService::Load(Scene& scene, const std::filesystem::path& path, bool additive) {
    const SceneDocumentLoadResult loaded = SceneDocumentService::Load(path);
    if (!loaded.succeeded) {
        return 0U;
    }

    SceneState& state = SceneAccess::State(scene);
    // LIB-073: the document read from disk (above) succeeded, so an actual
    // load attempt against the live scene is genuinely starting — queue
    // SceneLoading now, using nextLoadedSceneId (not yet incremented) as
    // the provisional id. Loads are synchronous (LIB-071), so on the
    // success path below the SAME id is assigned moments later — no other
    // call can interleave and increment nextLoadedSceneId in between. If
    // the load fails past this point, SceneLoading was queued but no
    // matching SceneLoaded follows — an honest "attempted, not completed"
    // signal, not a fabricated success.
    QueueLifecycleEvent(state, "SceneLoading", state.nextLoadedSceneId, loaded.document.name);
    if (!additive) {
        // LoadIntoScene's ClearSceneRoots destroys every root entity EXCEPT
        // ones marked persistent (LIB-072) — so every existing record is
        // now stale and must be dropped, EXCEPT that a persistent entity
        // that happened to be a previous record's root survives the wipe
        // while its record does not: it stays alive in the hierarchy, just
        // no longer addressable via Scene.Find/Unload under its old id.
        // Documented scope limit, not a crash risk — the entity itself is
        // never destroyed by this.
        const std::vector<SceneEntity> rootsBefore = scene.Hierarchy().RootEntities();
        if (!SceneDocumentService::LoadIntoScene(scene, loaded.document)) {
            return 0U;
        }
        state.loadedScenes.clear();
        state.activeLoadedSceneId = 0U;
        // Persistent roots survive ClearSceneRoots, so RootEntities() after
        // the load can contain BOTH the freshly instantiated document root
        // and any persistent survivors from before — the survivors were
        // already present in rootsBefore, so the one genuinely NEW root is
        // whichever entity in the after-set was not in the before-set.
        const std::vector<SceneEntity> rootsAfter = scene.Hierarchy().RootEntities();
        SceneEntity newRoot{};
        for (const SceneEntity candidate : rootsAfter) {
            if (std::ranges::find(rootsBefore, candidate) == rootsBefore.end()) {
                newRoot = candidate;
                break;
            }
        }
        const std::uint64_t id = state.nextLoadedSceneId++;
        state.loadedScenes.push_back(SceneState::LoadedSceneRecord{
            .id = id,
            .name = loaded.document.name,
            .path = path.string(),
            .root = newRoot,
        });
        QueueLifecycleEvent(state, "SceneLoaded", id, loaded.document.name);
        state.activeLoadedSceneId = id;
        QueueLifecycleEvent(state, "SceneActivated", id, loaded.document.name);
        return id;
    }

    const SceneDocumentAdditiveLoadResult additiveResult = SceneDocumentService::LoadIntoSceneAdditive(scene, loaded.document);
    if (!additiveResult.succeeded) {
        return 0U;
    }
    const std::uint64_t id = state.nextLoadedSceneId++;
    state.loadedScenes.push_back(SceneState::LoadedSceneRecord{
        .id = id,
        .name = loaded.document.name,
        .path = path.string(),
        .root = additiveResult.root,
    });
    QueueLifecycleEvent(state, "SceneLoaded", id, loaded.document.name);
    if (state.activeLoadedSceneId == 0U) {
        state.activeLoadedSceneId = id;
        QueueLifecycleEvent(state, "SceneActivated", id, loaded.document.name);
    }
    return id;
}

bool SceneLoadedContentService::Unload(Scene& scene, std::uint64_t id) noexcept {
    SceneState& state = SceneAccess::State(scene);
    const auto iterator = std::ranges::find_if(state.loadedScenes, [id](const SceneState::LoadedSceneRecord& record) { return record.id == id; });
    if (iterator == state.loadedScenes.end()) {
        return false;
    }
    const std::string name = iterator->name;
    QueueLifecycleEvent(state, "SceneUnloading", id, name);
    if (iterator->root.IsValid() && scene.Entities().IsAlive(iterator->root)) {
        scene.Entities().Destroy(iterator->root);
    }
    state.loadedScenes.erase(iterator);
    if (state.activeLoadedSceneId == id) {
        state.activeLoadedSceneId = state.loadedScenes.empty() ? 0U : state.loadedScenes.front().id;
    }
    QueueLifecycleEvent(state, "SceneUnloaded", id, name);
    return true;
}

std::uint64_t SceneLoadedContentService::Find(const Scene& scene, std::string_view name) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    const auto iterator = std::ranges::find_if(state.loadedScenes, [name](const SceneState::LoadedSceneRecord& record) { return record.name == name; });
    return iterator == state.loadedScenes.end() ? 0U : iterator->id;
}

bool SceneLoadedContentService::Exists(const Scene& scene, std::uint64_t id) noexcept {
    return RecordExists(SceneAccess::State(scene), id);
}

float SceneLoadedContentService::Progress(const Scene& scene, std::uint64_t id) noexcept {
    return Exists(scene, id) ? 1.0F : 0.0F;
}

bool SceneLoadedContentService::SetActive(Scene& scene, std::uint64_t id) noexcept {
    SceneState& state = SceneAccess::State(scene);
    const SceneState::LoadedSceneRecord* record = FindRecord(state, id);
    if (record == nullptr) {
        return false;
    }
    if (state.activeLoadedSceneId != id) {
        state.activeLoadedSceneId = id;
        QueueLifecycleEvent(state, "SceneActivated", id, record->name);
    }
    return true;
}

std::uint64_t SceneLoadedContentService::ActiveScene(const Scene& scene) noexcept {
    return SceneAccess::State(scene).activeLoadedSceneId;
}

std::vector<SceneLifecycleEventRecord> SceneLoadedContentService::DrainPendingLifecycleEvents(Scene& scene) {
    SceneState& state = SceneAccess::State(scene);
    std::vector<SceneLifecycleEventRecord> drained;
    drained.reserve(state.pendingSceneLifecycleEvents.size());
    for (SceneState::PendingSceneLifecycleEvent& pending : state.pendingSceneLifecycleEvents) {
        drained.push_back(SceneLifecycleEventRecord{
            .name = std::move(pending.name),
            .sceneId = pending.sceneId,
            .sceneName = std::move(pending.sceneName),
        });
    }
    state.pendingSceneLifecycleEvents.clear();
    return drained;
}

} // namespace kb::scene
