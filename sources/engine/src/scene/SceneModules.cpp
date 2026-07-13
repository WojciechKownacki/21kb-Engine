#include "engine/scene/Scene.hpp"

#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneHistory.hpp"
#include "engine/scene/SceneLoadedContent.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

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

} // namespace kb::scene
