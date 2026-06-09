#pragma once

namespace kb::editor {

class EditorSceneContext;

enum class EditorEditCommand {
    Undo,
    Redo,
    Duplicate,
    Save,
};

class EditorEditCommandPolicy {
public:
    [[nodiscard]] static bool CanExecute(const EditorSceneContext& sceneContext) noexcept;
    [[nodiscard]] static bool Execute(EditorSceneContext& sceneContext, EditorEditCommand command);
};

} // namespace kb::editor
