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
        // LoadIntoScene's ClearSceneRoots destroys every root entity,
        // including any previously tracked loaded-content roots — so
        // every existing record is now stale and must be dropped too.
        if (!SceneDocumentService::LoadIntoScene(scene, loaded.document)) {
            return 0U;
        }
        state.loadedScenes.clear();
        state.activeLoadedSceneId = 0U;
        const std::vector<SceneEntity> roots = scene.Hierarchy().RootEntities();
        const std::uint64_t id = state.nextLoadedSceneId++;
        state.loadedScenes.push_back(SceneState::LoadedSceneRecord{
            .id = id,
            .name = loaded.document.name,
            .path = path.string(),
            .root = roots.empty() ? SceneEntity{} : roots.front(),
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
