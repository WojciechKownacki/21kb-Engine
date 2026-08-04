#include "app/pointer/EditorLeftButtonDoubleClickRouter.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserPointerHandler.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "app/inspector/EditorInspectorPointerController.hpp"
#include "docking/DockMainLayoutResolver.hpp"
#include "rendering/DockTabControlGeometry.hpp"
#include "platform/win32/EditorMaterialParameterValueDialog.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"

#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] const DockPanelLayout* TabHit(const DockLayout& layout, int x, int y) noexcept {
    for (const DockPanelLayout& panel : layout.panels) {
        if (DockTabControlGeometry::ContainsClose(panel.tab, x, y)) {
            return nullptr;
        }
        if (panel.tab.Contains(x, y)) {
            return &panel;
        }
    }
    return nullptr;
}

} // namespace

EditorLeftButtonDoubleClickRouter::EditorLeftButtonDoubleClickRouter(
    HWND mainWindow,
    EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    EditorSceneContext& sceneContext,
    const EditorMetrics& metrics) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , sceneContext_(sceneContext)
    , metrics_(metrics) {}

bool EditorLeftButtonDoubleClickRouter::Handle(HWND messageWindow, int x, int y) {
    if (messageWindow == mainWindow_) {
        const DockLayout layout = DockMainLayoutResolver::Resolve(mainWindow_, dockModel_, metrics_);
        if (const DockPanelLayout* tab = TabHit(layout, x, y); tab != nullptr) {
            dockModel_.Commands().ActivatePanel(tab->panelId);
            static_cast<void>(dockModel_.Commands().ToggleMaximizedLeaf(tab->leafId));
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return true;
        }
    }

    const std::optional<RECT> materialEditorContent = EditorPanelContentResolver::Resolve(
        DockPanelKind::MaterialEditor,
        messageWindow,
        mainWindow_,
        dockModel_,
        floatingWindows_,
        metrics_);
    if (materialEditorContent.has_value()) {
        const MaterialEditorPanelLayout materialLayout = MaterialEditorPanelRenderer::ResolveLayout(*materialEditorContent);
        if (MaterialEditorPanelPointInRect(materialLayout.graphCanvas, x, y)) {
            const kb::assets::AssetId materialId = sceneContext_.MaterialEditor().OpenAssetId();
            const std::optional<kb::render::RenderMaterialAssetData> material = sceneContext_.MaterialEditor().WorkingCopy().has_value()
                ? sceneContext_.MaterialEditor().WorkingCopy()
                : sceneContext_.ReadMaterialDocumentAsset(materialId);
            if (material.has_value()) {
                for (auto it = material->graph.composites.rbegin(); it != material->graph.composites.rend(); ++it) {
                    if (!it->collapsed) {
                        continue;
                    }
                    const std::optional<RECT> compositeRect = MaterialEditorPanelRenderer::GraphCompositeRect(
                        *materialEditorContent,
                        material->graph,
                        it->id,
                        sceneContext_);
                    if (compositeRect.has_value() && MaterialEditorPanelPointInRect(*compositeRect, x, y) &&
                        sceneContext_.ExpandMaterialGraphComposite(materialId, it->id)) {
                        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                        return true;
                    }
                }

                // Double-click an empty part of a comment (not over a node) to edit its label, the way a
                // node is renamed. Nodes sit above comments, so a node under the cursor wins.
                if (!MaterialEditorPanelRenderer::GraphNodeAt(*materialEditorContent, material->graph, sceneContext_, materialId, x, y).has_value()) {
                    if (const std::optional<std::uint32_t> commentId =
                            MaterialEditorPanelRenderer::GraphCommentAt(*materialEditorContent, material->graph, sceneContext_, materialId, x, y);
                        commentId.has_value()) {
                        const kb::render::RenderMaterialGraphCommentBox* comment = nullptr;
                        for (const kb::render::RenderMaterialGraphCommentBox& candidate : material->graph.comments) {
                            if (candidate.id == *commentId) {
                                comment = &candidate;
                                break;
                            }
                        }
                        const std::optional<std::string> text = EditorMaterialParameterValueDialog::Show(
                            messageWindow, "Comment", comment != nullptr ? comment->text : std::string{});
                        if (text.has_value()) {
                            static_cast<void>(sceneContext_.SetMaterialGraphCommentText(materialId, *commentId, *text));
                        }
                        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                        return true;
                    }
                }
            }
        }
    }

    const std::optional<RECT> inspectorContent = EditorPanelContentResolver::Resolve(DockPanelKind::Inspector, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    EditorInspectorPointerController inspectorPointer(sceneContext_);
    if (inspectorContent.has_value() && inspectorPointer.HandleDoubleClick(*inspectorContent, x, y)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return true;
    }

    const EditorAssetBrowserDoubleClickResult assetResult =
        EditorAssetBrowserPointerHandler::HandleDoubleClick(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_);
    if (assetResult == EditorAssetBrowserDoubleClickResult::None) {
        return false;
    }
    if (assetResult == EditorAssetBrowserDoubleClickResult::ScriptEditorOpened) {
        static_cast<void>(dockModel_.Commands().ActivatePanelKind(DockPanelKind::ScriptEditor, DockArea::Center));
    }
    if (assetResult == EditorAssetBrowserDoubleClickResult::MaterialEditorOpened) {
        static_cast<void>(dockModel_.Commands().ActivatePanelKind(DockPanelKind::MaterialEditor, DockArea::Center));
    }
    if (assetResult == EditorAssetBrowserDoubleClickResult::SkeletalMeshEditorOpened) {
        static_cast<void>(dockModel_.Commands().ActivatePanelKind(DockPanelKind::SkeletalMeshEditor, DockArea::Center));
    }
    if (assetResult == EditorAssetBrowserDoubleClickResult::AnimationClipEditorOpened) {
        static_cast<void>(dockModel_.Commands().ActivatePanelKind(DockPanelKind::AnimationClipEditor, DockArea::Center));
    }
    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    return true;
}

} // namespace kb::editor

#endif
