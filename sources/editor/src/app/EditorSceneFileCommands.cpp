#include "app/EditorSceneFileCommands.hpp"

#if defined(_WIN32)
#include "app/EditorSceneLifecycleGuard.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "localization/EditorLocalization.hpp"
#include "platform/win32/EditorChoiceDialog.hpp"
#include "platform/win32/EditorTextEntryDialog.hpp"
#include "platform/win32/EditorNewSceneDialog.hpp"
#include "platform/win32/EditorSceneFileDialog.hpp"
#include "project/EditorProjectPaths.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace kb::editor {
namespace {

[[nodiscard]] std::string Utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string{ reinterpret_cast<const char*>(text.data()), text.size() };
}

} // namespace

std::vector<EditorPathMenuRow> EditorSceneFileCommands::SceneMenuRows(const EditorSceneContext& sceneContext) {
    constexpr std::size_t kMaximumScenes = 8U;
    std::vector<EditorPathMenuRow> rows;
    std::error_code error;
    const std::filesystem::path current = std::filesystem::weakly_canonical(sceneContext.CurrentScenePath(), error);
    const auto& mounts = sceneContext.Scene().Assets().Manager().Mounts();
    for (const std::string& scene : sceneContext.ProjectScenePaths()) {
        const std::optional<std::filesystem::path> path = mounts.Resolve(std::filesystem::path{ scene });
        if (path.has_value()) {
            rows.push_back(EditorPathMenuRow{
                .label = Utf8(path->filename()),
                .path = *path,
                .active = std::filesystem::weakly_canonical(*path, error) == current,
            });
        }
    }
    if (rows.size() > kMaximumScenes) {
        // The open scene stays listed; the others past the limit are behind Open Scene.
        for (std::size_t index = kMaximumScenes; index < rows.size(); ++index) {
            if (rows[index].active) {
                std::swap(rows[kMaximumScenes - 1U], rows[index]);
            }
        }
        rows.resize(kMaximumScenes);
    }
    return rows;
}

bool EditorSceneFileCommands::CanChangeOpenSceneFile(const EditorSceneContext& sceneContext) {
    const std::filesystem::path& path = sceneContext.CurrentScenePath();
    std::error_code error;
    return path.extension() == ".21kbscene" && std::filesystem::is_regular_file(path, error) &&
        !sceneContext.HasPlayModeSceneSession() && !sceneContext.InPrefabEditMode();
}

bool EditorSceneFileCommands::OpenSceneFile(HWND owner, EditorSceneContext& sceneContext, const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::equivalent(path, sceneContext.CurrentScenePath(), error) && !error) {
        return false;
    }
    const std::optional<EditorDirtySceneResolution> resolution =
        EditorSceneLifecycleGuard::ConfirmDirtySceneTransition(owner, sceneContext, EditorSceneTransition::OpenScene);
    return resolution.has_value() && sceneContext.OpenScene(path, *resolution);
}

bool EditorSceneFileCommands::DuplicateScene(EditorSceneContext& sceneContext) {
    return CanChangeOpenSceneFile(sceneContext) && sceneContext.DuplicateOpenScene();
}

bool EditorSceneFileCommands::RenameScene(HWND owner, EditorSceneContext& sceneContext) {
    if (!CanChangeOpenSceneFile(sceneContext)) {
        return false;
    }
    const std::optional<std::string> name = EditorTextEntryDialog::Show(owner, {
        .title = std::string{ EditorText("scene.renameTitle", "Rename Scene") },
        .label = std::string{ EditorText("scene.nameLabel", "Scene name") },
        .value = Utf8(sceneContext.CurrentScenePath().stem()),
        .hint = std::string{ EditorText("scene.nameHint", "The scene file gets this name, in the same folder.") },
        .acceptLabel = std::string{ EditorText("scene.renameAccept", "Rename") },
        .icon = HeroIconKind::Pencil,
    });
    return name.has_value() && *name != Utf8(sceneContext.CurrentScenePath().stem()) && sceneContext.RenameOpenScene(*name);
}

bool EditorSceneFileCommands::DeleteScene(HWND owner, EditorSceneContext& sceneContext) {
    if (!CanChangeOpenSceneFile(sceneContext)) {
        return false;
    }
    if (SceneMenuRows(sceneContext).size() < 2U) {
        static_cast<void>(EditorChoiceDialog::Show(owner, EditorChoiceDialogDescriptor{
            .title = std::string{ EditorText("scene.deleteTitle", "Delete Scene") },
            .message = std::string{ EditorText("scene.onlyScene", "This is the project's only scene, so it stays. Create another one first.") },
            .icon = HeroIconKind::Warning,
        }));
        return false;
    }
    const EditorChoiceDialogResult choice = EditorChoiceDialog::Show(owner, EditorChoiceDialogDescriptor{
        .title = std::string{ EditorText("scene.deleteTitle", "Delete Scene") },
        .message = EditorLocalization::Format("scene.deleteQuestion", "Delete the scene {scene}?", { { "scene", Utf8(sceneContext.CurrentScenePath().filename()) } }),
        .supportingText = std::string{ EditorText("scene.deleteSupporting", "It goes to the Recycle Bin, with any unsaved changes lost. The editor moves to another scene.") },
        .primaryLabel = std::string{ EditorText("common.delete", "Delete") },
        .cancelLabel = std::string{ EditorText("common.cancel", "Cancel") },
        .icon = HeroIconKind::Trash,
        .primaryTone = EditorDialogButtonTone::Destructive,
    });
    return choice == EditorChoiceDialogResult::Primary && sceneContext.DeleteOpenScene();
}

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
        EditorSceneLifecycleGuard::ConfirmDirtySceneTransition(owner, sceneContext, EditorSceneTransition::NewScene);
    return resolution.has_value() &&
        sceneContext.CreateScene(choice->folder, choice->name, choice->sceneTemplate, choice->setAsStartupScene, *resolution);
}

bool EditorSceneFileCommands::OpenScene(HWND owner, EditorSceneContext& sceneContext) {
    const std::optional<EditorDirtySceneResolution> resolution =
        EditorSceneLifecycleGuard::ConfirmDirtySceneTransition(owner, sceneContext, EditorSceneTransition::OpenScene);
    if (!resolution.has_value()) {
        return false;
    }
    const std::optional<std::filesystem::path> path = EditorSceneFileDialog::Open(owner, EditorProjectPaths::ScenesRoot());
    return path.has_value() && sceneContext.OpenScene(*path, *resolution);
}

bool EditorSceneFileCommands::SaveSceneAs(HWND owner, EditorSceneContext& sceneContext) {
    const std::optional<std::filesystem::path> path =
        EditorSceneFileDialog::SaveAs(owner, sceneContext.CurrentScenePath(), EditorProjectPaths::ScenesRoot(),
            EditorProjectPaths::AssetsRoot());
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
