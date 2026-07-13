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

} // namespace

std::uint64_t SceneLoadedContentService::Load(Scene& scene, const std::filesystem::path& path, bool additive) {
    const SceneDocumentLoadResult loaded = SceneDocumentService::Load(path);
    if (!loaded.succeeded) {
        return 0U;
    }

    SceneState& state = SceneAccess::State(scene);
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
        state.activeLoadedSceneId = id;
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
    if (state.activeLoadedSceneId == 0U) {
        state.activeLoadedSceneId = id;
    }
    return id;
}

bool SceneLoadedContentService::Unload(Scene& scene, std::uint64_t id) noexcept {
    SceneState& state = SceneAccess::State(scene);
    const auto iterator = std::ranges::find_if(state.loadedScenes, [id](const SceneState::LoadedSceneRecord& record) { return record.id == id; });
    if (iterator == state.loadedScenes.end()) {
        return false;
    }
    if (iterator->root.IsValid() && scene.Entities().IsAlive(iterator->root)) {
        scene.Entities().Destroy(iterator->root);
    }
    state.loadedScenes.erase(iterator);
    if (state.activeLoadedSceneId == id) {
        state.activeLoadedSceneId = state.loadedScenes.empty() ? 0U : state.loadedScenes.front().id;
    }
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
    if (FindRecord(state, id) == nullptr) {
        return false;
    }
    state.activeLoadedSceneId = id;
    return true;
}

std::uint64_t SceneLoadedContentService::ActiveScene(const Scene& scene) noexcept {
    return SceneAccess::State(scene).activeLoadedSceneId;
}

} // namespace kb::scene
