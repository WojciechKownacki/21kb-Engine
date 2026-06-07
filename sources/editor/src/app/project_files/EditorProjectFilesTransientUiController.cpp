#include "app/project_files/EditorProjectFilesTransientUiController.hpp"

#include "scene/EditorSceneContext.hpp"

namespace kb::editor {

EditorProjectFilesTransientUiController::EditorProjectFilesTransientUiController(EditorSceneContext& sceneContext) noexcept
    : sceneContext_(sceneContext) {}

void EditorProjectFilesTransientUiController::CloseTransientUi() noexcept {
    sceneContext_.AssetBrowser().CloseFilterMenu();
    sceneContext_.AssetBrowser().CloseContextMenu();
    sceneContext_.AssetBrowser().CloseDropActionMenu();
}

} // namespace kb::editor
