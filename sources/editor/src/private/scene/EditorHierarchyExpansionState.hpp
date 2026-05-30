#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <unordered_set>

namespace kb::editor {

class EditorHierarchyExpansionState {
public:
    using EntityIdSet = std::unordered_set<kb::scene::SceneEntity::IdType>;

    [[nodiscard]] const EntityIdSet& CollapsedEntities() const noexcept;
    void SetExpanded(kb::scene::SceneEntity entity, bool expanded);

private:
    EntityIdSet collapsedEntities_;
};

} // namespace kb::editor
