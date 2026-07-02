#include "app/EditorEditCommandPolicy.hpp"

#include "scene/EditorSceneContext.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] bool TextInputActive(const EditorSceneContext& sceneContext) noexcept {
    return sceneContext.Inspector().IsTextEditing() || sceneContext.AssetBrowser().IsTextEditing() || sceneContext.IsHierarchyRenaming() ||
        sceneContext.IsHierarchySearchFocused() || sceneContext.IsMaterialGraphConstantInlineEditing();
}

} // namespace

bool EditorEditCommandPolicy::CanExecute(const EditorSceneContext& sceneContext) noexcept {
    return !TextInputActive(sceneContext);
}

bool EditorEditCommandPolicy::Execute(EditorSceneContext& sceneContext, EditorEditCommand command) {
    if (!CanExecute(sceneContext)) {
        return false;
    }

    switch (command) {
    case EditorEditCommand::Undo:
        return sceneContext.UndoSceneCommand();
    case EditorEditCommand::Redo:
        return sceneContext.RedoSceneCommand();
    case EditorEditCommand::Duplicate:
        return sceneContext.DuplicateSelectedHierarchyEntities();
    case EditorEditCommand::Save:
        return sceneContext.SaveCurrentScene();
    }

    return false;
}

} // namespace kb::editor
