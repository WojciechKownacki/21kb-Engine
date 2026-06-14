#include "scene/EditorSceneSelectionPivot.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <algorithm>
#include <vector>

namespace kb::editor {
namespace {

[[nodiscard]] bool Contains(std::span<const kb::scene::SceneEntity> entities, kb::scene::SceneEntity entity) noexcept {
    return std::ranges::find(entities, entity) != entities.end();
}

[[nodiscard]] bool HasSelectedAncestor(
    const kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    std::span<const kb::scene::SceneEntity> selected) noexcept {
    kb::scene::SceneEntity parent = scene.Hierarchy().Parent(entity);
    while (parent.IsValid()) {
        if (Contains(selected, parent)) {
            return true;
        }
        parent = scene.Hierarchy().Parent(parent);
    }
    return false;
}

[[nodiscard]] std::vector<kb::scene::SceneEntity> TopLevelAliveSelection(
    const kb::scene::Scene& scene,
    std::span<const kb::scene::SceneEntity> selected) {
    std::vector<kb::scene::SceneEntity> topLevel;
    topLevel.reserve(selected.size());
    for (const kb::scene::SceneEntity entity : selected) {
        if (!entity.IsValid() || !scene.Entities().IsAlive(entity) || Contains(topLevel, entity) || HasSelectedAncestor(scene, entity, selected)) {
            continue;
        }
        if (scene.Transforms().TryGet(entity) == nullptr) {
            continue;
        }
        topLevel.push_back(entity);
    }
    return topLevel;
}

[[nodiscard]] std::optional<kb::scene::Vec3> EntityPosition(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    if (!entity.IsValid() || !scene.Entities().IsAlive(entity)) {
        return std::nullopt;
    }
    const kb::scene::TransformComponent* transform = scene.Transforms().TryGet(entity);
    if (transform == nullptr) {
        return std::nullopt;
    }
    return transform->localPosition;
}

} // namespace

std::optional<kb::scene::Vec3> EditorSceneSelectionPivot::Resolve(
    const kb::scene::Scene& scene,
    std::span<const kb::scene::SceneEntity> selected,
    kb::scene::SceneEntity fallback) noexcept {
    const std::vector<kb::scene::SceneEntity> topLevel = TopLevelAliveSelection(scene, selected);
    if (topLevel.empty()) {
        return EntityPosition(scene, fallback);
    }

    kb::scene::Vec3 sum{};
    std::size_t count = 0U;
    for (const kb::scene::SceneEntity entity : topLevel) {
        const std::optional<kb::scene::Vec3> position = EntityPosition(scene, entity);
        if (!position.has_value()) {
            continue;
        }
        sum.x += position->x;
        sum.y += position->y;
        sum.z += position->z;
        ++count;
    }

    if (count == 0U) {
        return EntityPosition(scene, fallback);
    }

    const float invCount = 1.0F / static_cast<float>(count);
    return kb::scene::Vec3{sum.x * invCount, sum.y * invCount, sum.z * invCount};
}

} // namespace kb::editor
