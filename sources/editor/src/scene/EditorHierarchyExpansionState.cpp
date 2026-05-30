#include "scene/EditorHierarchyExpansionState.hpp"

namespace kb::editor {

const EditorHierarchyExpansionState::EntityIdSet& EditorHierarchyExpansionState::CollapsedEntities() const noexcept {
    return collapsedEntities_;
}

void EditorHierarchyExpansionState::SetExpanded(kb::scene::SceneEntity entity, bool expanded) {
    if (expanded) {
        collapsedEntities_.erase(entity.Id());
        return;
    }

    collapsedEntities_.insert(entity.Id());
}

} // namespace kb::editor
