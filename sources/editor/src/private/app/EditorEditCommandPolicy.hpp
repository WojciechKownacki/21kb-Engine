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
    // Keyboard route. Refuses while a text field owns the keys (Ctrl+S belongs to the field, not the app)
    // and while a material-graph gesture owns the working copy.
    [[nodiscard]] static bool CanExecute(const EditorSceneContext& sceneContext) noexcept;
    [[nodiscard]] static bool Execute(EditorSceneContext& sceneContext, EditorEditCommand command);

    // Pointer route: the toolbar buttons and the File/Edit menu rows. Only the gesture guard applies here.
    // A click on "Save" is unambiguous in a way Ctrl+S is not while a field owns the keys, and refusing on
    // text input would leave the Save button dead after any click into an Inspector, rename or search box -
    // none of which a toolbar click clears. (Save does commit the pending edits it knows about; Undo/Redo
    // commit only the hierarchy rename and the Inspector edit, so a graph rename armed at the same time is
    // still left behind. That is pre-existing behaviour of those rows, not something this route introduces.)
    [[nodiscard]] static bool CanExecuteFromPointer(const EditorSceneContext& sceneContext) noexcept;
    [[nodiscard]] static bool ExecuteFromPointer(EditorSceneContext& sceneContext, EditorEditCommand command);
};

} // namespace kb::editor
