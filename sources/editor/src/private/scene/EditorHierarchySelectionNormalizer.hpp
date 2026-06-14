#pragma once

#include "scene/EditorHierarchyRow.hpp"

#include <span>

namespace kb::scene {
class Scene;
}

namespace kb::editor {

class EditorHierarchySelectionState;

class EditorHierarchySelectionNormalizer {
public:
    EditorHierarchySelectionNormalizer() = delete;

    static void NormalizeAfterSceneRestore(
        const kb::scene::Scene& scene,
        EditorHierarchySelectionState& selection,
        std::span<const EditorHierarchyRow> visibleRows);
};

} // namespace kb::editor
