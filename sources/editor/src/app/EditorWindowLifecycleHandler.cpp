#include "app/EditorWindowLifecycleHandler.hpp"

#if defined(_WIN32)
#include "app/EditorSceneLifecycleGuard.hpp"
#include "app/panels/EditorPanelCloser.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/editor/docking/DockTypes.hpp"
#include "localization/EditorLocalization.hpp"
#include "platform/win32/EditorChoiceDialog.hpp"
#include "platform/win32/EditorMap2DWindow.hpp"
#include "scene/EditorSceneContext.hpp"
#include "rendering/TimelineEditorPanel.hpp"
#include "rendering/BlendSpace2DEditorPanel.hpp"
#include "rendering/GameplayGraphEditorPanel.hpp"
#include "scene/GameplayGraphEditorState.hpp"

#include <optional>
#include <string>

namespace kb::editor {
EditorWindowLifecycleHandler::EditorWindowLifecycleHandler(HWND& mainWindow, bool& running, EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows, EditorSceneContext& sceneContext) noexcept
    : mainWindow_(mainWindow)
    , running_(running)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , sceneContext_(sceneContext) {}

LRESULT EditorWindowLifecycleHandler::HandleClose(HWND messageWindow) {
    if (const std::uint32_t panelId = floatingWindows_.Queries().PanelId(messageWindow); panelId != 0) {
        // The close button of a torn-off panel closes the panel, as the one on its tab does.
        if (EditorPanelCloser::Close(messageWindow, panelId, dockModel_, floatingWindows_, sceneContext_)) {
            InvalidateRect(mainWindow_, nullptr, FALSE);
        }
        return 0;
    }

    static_cast<void>(sceneContext_.RestorePlayModeSceneSession());
    for (const DockPanelKind kind : { DockPanelKind::TimelineEditor, DockPanelKind::GameplayGraphEditor, DockPanelKind::BlendSpace2DEditor,
             DockPanelKind::MaterialEditor, DockPanelKind::SkeletalMeshEditor, DockPanelKind::AnimatorEditor, DockPanelKind::ParticleEditor }) {
        if (!EditorPanelCloser::ResolveUnsavedDocument(messageWindow, kind, sceneContext_, true, false)) {
            return 0;
        }
    }
    if (!EditorMap2DWindow::CloseAll(mainWindow_)) {
        return 0;
    }
    if (!EditorSceneLifecycleGuard::LeavePrefabEditMode(mainWindow_, sceneContext_, EditorPrefabLeaveReason::CloseEditor)) {
        return 0;
    }
    const std::optional<EditorDirtySceneResolution> resolution =
        EditorSceneLifecycleGuard::ConfirmDirtySceneTransition(mainWindow_, sceneContext_, EditorSceneTransition::CloseEditor);
    if (!resolution.has_value()) {
        return 0;
    }
    if (*resolution == EditorDirtySceneResolution::Save) {
        if (!sceneContext_.PrepareDirtySceneTransition("application close", *resolution)) {
            return 0;
        }
    } else {
        sceneContext_.DiscardDirtySceneDocument("application close");
    }

    running_ = false;
    DestroyWindow(mainWindow_);
    mainWindow_ = nullptr;
    PostQuitMessage(0);
    return 0;
}

LRESULT EditorWindowLifecycleHandler::HandleDestroy(HWND messageWindow) {
    if (floatingWindows_.Queries().IsFloatingWindow(messageWindow)) {
        floatingWindows_.Lifecycle().OnDestroyed(messageWindow);
        return 0;
    }

    if (mainWindow_ != nullptr && messageWindow == mainWindow_) {
        mainWindow_ = nullptr;
        running_ = false;
        PostQuitMessage(0);
    }

    return 0;
}

} // namespace kb::editor

#endif
