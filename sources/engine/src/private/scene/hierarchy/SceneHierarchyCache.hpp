#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <vector>

namespace kb::scene {

class SceneHierarchyCache {
public:
    SceneHierarchyCache() = delete;

    static void Add(SceneState& state, SceneEntity entity, SceneEntity parent) {
        state.hierarchyParents[entity.Id()] = parent;
        AddToParentList(state, parent, entity);
    }

    static void AddRoot(SceneState& state, SceneEntity entity) {
        Add(state, entity, {});
    }

    static void Move(SceneState& state, SceneEntity child, SceneEntity oldParent, SceneEntity newParent) {
        RemoveFromParentList(state, oldParent, child);
        state.hierarchyParents[child.Id()] = newParent;
        AddToParentList(state, newParent, child);
    }

    static void Remove(SceneState& state, SceneEntity entity, SceneEntity parent) {
        RemoveFromParentList(state, parent, entity);
        state.hierarchyParents.erase(entity.Id());
        state.hierarchyChildren.erase(entity.Id());
        state.hierarchyOrder.erase(entity.Id());
    }

    [[nodiscard]] static std::vector<SceneEntity> Roots(const SceneState& state) {
        return state.hierarchyRoots;
    }

    [[nodiscard]] static std::vector<SceneEntity> Children(const SceneState& state, SceneEntity entity) {
        const auto children = state.hierarchyChildren.find(entity.Id());
        return children == state.hierarchyChildren.end() ? std::vector<SceneEntity>{} : children->second;
    }

private:
    static void AddToParentList(SceneState& state, SceneEntity parent, SceneEntity child) {
        if (parent.IsValid()) {
            AppendUnique(state.hierarchyChildren[parent.Id()], child);
        } else {
            AppendUnique(state.hierarchyRoots, child);
        }
    }

    static void RemoveFromParentList(SceneState& state, SceneEntity parent, SceneEntity child) {
        if (parent.IsValid()) {
            const auto children = state.hierarchyChildren.find(parent.Id());
            if (children == state.hierarchyChildren.end()) {
                return;
            }
            Erase(children->second, child);
            if (children->second.empty()) {
                state.hierarchyChildren.erase(children);
            }
        } else {
            Erase(state.hierarchyRoots, child);
        }
    }

    static void AppendUnique(std::vector<SceneEntity>& entities, SceneEntity entity) {
        if (std::ranges::find(entities, entity) == entities.end()) {
            entities.push_back(entity);
        }
    }

    static void Erase(std::vector<SceneEntity>& entities, SceneEntity entity) {
        std::erase(entities, entity);
    }
};

} // namespace kb::scene
