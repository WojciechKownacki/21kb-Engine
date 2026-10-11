#include "app/EditorSceneFileCommands.hpp"

#if defined(_WIN32)
#include "app/EditorSceneLifecycleGuard.hpp"
#include "platform/win32/EditorNewSceneDialog.hpp"
#include "platform/win32/EditorSceneFileDialog.hpp"
#include "project/EditorProjectPaths.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace kb::editor {

bool EditorSceneFileCommands::NewScene(HWND owner, EditorSceneContext& sceneContext) {
    // The name and folder are asked first; the open scene is settled only once there is a scene to go to.
    const std::optional<EditorNewSceneChoice> choice = EditorNewSceneDialog::Show(owner, EditorNewSceneDialogDescriptor{
        .validate = [&sceneContext](const std::string& name, const std::string& folder) {
            return sceneContext.NewScenePathError(folder, name);
        },
    });
    if (!choice.has_value()) {
        return false;
    }
    const std::optional<EditorDirtySceneResolution> resolution =
        EditorSceneLifecycleGuard::ConfirmDirtySceneTransition(owner, sceneContext, L"creating a new scene");
    return resolution.has_value() &&
        sceneContext.CreateScene(choice->folder, choice->name, choice->sceneTemplate, choice->setAsStartupScene, *resolution);
}

bool EditorSceneFileCommands::OpenScene(HWND owner, EditorSceneContext& sceneContext) {
    const std::optional<EditorDirtySceneResolution> resolution =
        EditorSceneLifecycleGuard::ConfirmDirtySceneTransition(owner, sceneContext, L"opening another scene");
    if (!resolution.has_value()) {
        return false;
    }
    const std::optional<std::filesystem::path> path = EditorSceneFileDialog::Open(owner, EditorProjectPaths::ScenesRoot());
    return path.has_value() && sceneContext.OpenScene(*path, *resolution);
}

bool EditorSceneFileCommands::SaveSceneAs(HWND owner, EditorSceneContext& sceneContext) {
    const std::optional<std::filesystem::path> path =
        EditorSceneFileDialog::SaveAs(owner, sceneContext.CurrentScenePath(), EditorProjectPaths::ScenesRoot());
    return path.has_value() && sceneContext.SaveCurrentSceneAs(*path);
}

bool EditorSceneFileCommands::HandleShortcut(HWND owner, EditorSceneContext& sceneContext, WPARAM key, bool& changed) {
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    if (!control || alt) {
        return false;
    }
    if (key == 'N' && !shift) {
        changed = NewScene(owner, sceneContext);
        return true;
    }
    if (key == 'O' && !shift) {
        changed = OpenScene(owner, sceneContext);
        return true;
    }
    if (key == 'S' && shift) {
        changed = SaveSceneAs(owner, sceneContext);
        return true;
    }
    return false;
}

} // namespace kb::editor

#endif
