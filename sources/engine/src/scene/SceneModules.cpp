#include "engine/scene/Scene.hpp"

#include "engine/save/SaveGame.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneAnimators.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneHistory.hpp"
#include "engine/scene/SceneLoadedContent.hpp"
#include "engine/scene/SceneMaterialInstances.hpp"
#include "engine/scene/SceneParticleSystems.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTasks.hpp"
#include "engine/scene/SceneTimelines.hpp"
#include "engine/scene/SceneTimers.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneUIDocuments.hpp"

namespace kb::scene {

SceneEntities Scene::Entities() noexcept {
    return SceneEntities{ *this };
}

SceneEntityQueries Scene::Entities() const noexcept {
    return SceneEntityQueries{ *this };
}

SceneTransforms Scene::Transforms() noexcept {
    return SceneTransforms{ *this };
}

SceneTransformQueries Scene::Transforms() const noexcept {
    return SceneTransformQueries{ *this };
}

SceneComponents Scene::Components() noexcept {
    return SceneComponents{ *this };
}

SceneComponentQueries Scene::Components() const noexcept {
    return SceneComponentQueries{ *this };
}

SceneHierarchyAccess Scene::Hierarchy() noexcept {
    return SceneHierarchyAccess{ *this };
}

SceneHierarchyQueries Scene::Hierarchy() const noexcept {
    return SceneHierarchyQueries{ *this };
}

SceneHistory Scene::History() noexcept {
    return SceneHistory{ *this };
}

ScenePrefabs Scene::Prefabs() noexcept {
    return ScenePrefabs{ *this };
}

ScenePrefabs Scene::Prefabs() const noexcept {
    return ScenePrefabs{ const_cast<Scene&>(*this) };
}

SceneRuntime Scene::Runtime() noexcept {
    return SceneRuntime{ *this };
}

SceneRuntimeQueries Scene::Runtime() const noexcept {
    return SceneRuntimeQueries{ *this };
}

SceneLoadedContent Scene::LoadedContent() noexcept {
    return SceneLoadedContent{ *this };
}

SceneLoadedContentQueries Scene::LoadedContent() const noexcept {
    return SceneLoadedContentQueries{ *this };
}

SceneTimers Scene::Timers() noexcept {
    return SceneTimers{ *this };
}

SceneTasks Scene::Tasks() noexcept {
    return SceneTasks{ *this };
}

SceneMaterialInstances Scene::MaterialInstances() noexcept {
    return SceneMaterialInstances{ *this };
}

SceneMaterialInstanceQueries Scene::MaterialInstances() const noexcept {
    return SceneMaterialInstanceQueries{ *this };
}

SceneParticleSystems Scene::Particles() noexcept {
    return SceneParticleSystems{ *this };
}

SceneParticleSystemQueries Scene::Particles() const noexcept {
    return SceneParticleSystemQueries{ *this };
}

SceneAnimators Scene::Animators() noexcept {
    return SceneAnimators{ *this };
}

SceneAnimatorQueries Scene::Animators() const noexcept {
    return SceneAnimatorQueries{ *this };
}

SceneTimelines Scene::Timelines() noexcept {
    return SceneTimelines{ *this };
}

SceneTimelineQueries Scene::Timelines() const noexcept {
    return SceneTimelineQueries{ *this };
}

SceneUIDocuments Scene::UIDocuments() noexcept {
    return SceneUIDocuments{ *this };
}

SceneUIDocumentQueries Scene::UIDocuments() const noexcept {
    return SceneUIDocumentQueries{ *this };
}

kb::save::SaveGame& Scene::AmbientSave() noexcept {
    return SceneAccess::State(*this).ambientSave;
}

const kb::save::SaveGame& Scene::AmbientSave() const noexcept {
    return SceneAccess::State(*this).ambientSave;
}

kb::save::SaveGame& Scene::AmbientSettings() noexcept {
    return SceneAccess::State(*this).ambientSettings;
}

const kb::save::SaveGame& Scene::AmbientSettings() const noexcept {
    return SceneAccess::State(*this).ambientSettings;
}

} // namespace kb::scene
