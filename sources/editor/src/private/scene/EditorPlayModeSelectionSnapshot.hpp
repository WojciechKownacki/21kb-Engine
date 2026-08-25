#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "scene/EditorHierarchySelectionState.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace kb::editor {

class EditorPlayModeSelectionSnapshot final {
public:
    void CaptureAuthoredHierarchy(const kb::scene::Scene& scene) {
        Clear();
        const std::vector<kb::scene::SceneEntity> roots =
            scene.Hierarchy().RootEntities();
        HierarchyPath path;
        for (std::size_t rootIndex = 0U; rootIndex < roots.size(); ++rootIndex) {
            path.assign(1U, rootIndex);
            IndexBranch(scene, roots[rootIndex], path);
        }
    }

    void CaptureSelection(const EditorHierarchySelectionState& selection) {
        selectedPaths_.clear();
        selectedPaths_.reserve(selection.SelectedEntities().size());
        const kb::scene::SceneEntity primary = selection.Primary();

        for (const kb::scene::SceneEntity entity :
             selection.SelectedEntities()) {
            if (entity != primary) {
                AppendPath(entity);
            }
        }
        if (selection.IsSelected(primary)) {
            AppendPath(primary);
        }
    }

    void Restore(
        const kb::scene::Scene& scene,
        EditorHierarchySelectionState& selection) const {
        std::vector<kb::scene::SceneEntity> restored;
        restored.reserve(selectedPaths_.size());
        for (const HierarchyPath& path : selectedPaths_) {
            const kb::scene::SceneEntity entity = ResolvePath(scene, path);
            if (entity.IsValid() && scene.Entities().IsAlive(entity) &&
                std::ranges::find(restored, entity) == restored.end()) {
                restored.push_back(entity);
            }
        }
        selection.SelectEntities(restored);
    }

    void Clear() noexcept {
        authoredPaths_.clear();
        selectedPaths_.clear();
    }

private:
    using HierarchyPath = std::vector<std::size_t>;

    void IndexBranch(
        const kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        HierarchyPath& path) {
        authoredPaths_.insert_or_assign(entity.Id(), path);
        const std::size_t childCount = scene.Hierarchy().ChildCount(entity);
        for (std::size_t childIndex = 0U;
             childIndex < childCount;
             ++childIndex) {
            path.push_back(childIndex);
            IndexBranch(
                scene,
                scene.Hierarchy().ChildAt(entity, childIndex),
                path);
            path.pop_back();
        }
    }

    void AppendPath(kb::scene::SceneEntity entity) {
        const auto path = authoredPaths_.find(entity.Id());
        if (path != authoredPaths_.end()) {
            selectedPaths_.push_back(path->second);
        }
    }

    [[nodiscard]] static kb::scene::SceneEntity ResolvePath(
        const kb::scene::Scene& scene,
        const HierarchyPath& path) {
        if (path.empty()) {
            return {};
        }
        const std::vector<kb::scene::SceneEntity> roots =
            scene.Hierarchy().RootEntities();
        if (path.front() >= roots.size()) {
            return {};
        }

        kb::scene::SceneEntity entity = roots[path.front()];
        for (std::size_t depth = 1U; depth < path.size(); ++depth) {
            if (path[depth] >= scene.Hierarchy().ChildCount(entity)) {
                return {};
            }
            entity = scene.Hierarchy().ChildAt(entity, path[depth]);
        }
        return entity;
    }

    std::unordered_map<std::uint64_t, HierarchyPath> authoredPaths_;
    std::vector<HierarchyPath> selectedPaths_;
};

} // namespace kb::editor
