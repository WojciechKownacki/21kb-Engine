#include "rendering/EditorHostSurfaceLayoutResolver.hpp"

#if defined(_WIN32)
#include "rendering/InspectorPanelRenderer.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "rendering/MaterialPreviewViewportKeys.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"

#include <optional>
#include <span>

namespace kb::editor {
namespace {

[[nodiscard]] RECT ToRect(const DockRect& rect) noexcept {
    return RECT{
        .left = rect.x,
        .top = rect.y,
        .right = rect.x + rect.width,
        .bottom = rect.y + rect.height,
    };
}

[[nodiscard]] RECT IntersectRectOrEmpty(const RECT& a, const RECT& b) noexcept {
    RECT clipped{};
    if (IntersectRect(&clipped, &a, &b) == 0) {
        return {};
    }
    return clipped;
}

void AppendMaterialPreview(
    std::vector<EditorSceneBgfxViewport::HostSurfaceLayout>& layouts,
    std::uint64_t key,
    const std::optional<RECT>& rect) {
    if (!rect.has_value() || rect->right <= rect->left || rect->bottom <= rect->top) {
        return;
    }
    layouts.push_back(EditorSceneBgfxViewport::HostSurfaceLayout{
        .viewportKey = key,
        .bounds = *rect,
    });
}

} // namespace

std::vector<EditorSceneBgfxViewport::HostSurfaceLayout> EditorHostSurfaceLayoutResolver::ResolveMainWindow(
    HWND window,
    const EditorDockModel& dockModel,
    const EditorMetrics& metrics,
    const EditorSceneContext& sceneContext) {
    std::vector<EditorSceneBgfxViewport::HostSurfaceLayout> layouts;
    if (window == nullptr || IsWindow(window) == 0) {
        return layouts;
    }

    RECT client{};
    GetClientRect(window, &client);
    const DockLayout layout = dockModel.Queries().BuildLayout(
        client.right - client.left,
        client.bottom - client.top,
        metrics.menuHeight,
        metrics.toolbarHeight,
        metrics.tabStripHeight,
        metrics.tabMinWidth,
        metrics.tabWidth,
        metrics.splitterSize,
        metrics.panelPadding);

    for (const DockPanelLayout& panelLayout : layout.panels) {
        if (!panelLayout.active) {
            continue;
        }
        const DockPanel* panel = dockModel.Queries().FindPanel(panelLayout.panelId);
        if (panel == nullptr) {
            continue;
        }

        const RECT content = IntersectRectOrEmpty(ToRect(panelLayout.content), ToRect(panelLayout.contentClip));
        if (content.right <= content.left || content.bottom <= content.top) {
            continue;
        }
        if (panel->kind == DockPanelKind::Scene) {
            layouts.push_back(EditorSceneBgfxViewport::HostSurfaceLayout{
                .viewportKey = panelLayout.panelId,
                .bounds = SceneViewportToolbarRenderer::Resolve(content, sceneContext.ViewportPreview(panelLayout.panelId), sceneContext).renderArea,
            });
            continue;
        }
        if (panel->kind == DockPanelKind::Inspector) {
            AppendMaterialPreview(
                layouts,
                kInspectorMaterialPreviewViewportKey,
                InspectorPanelRenderer::MaterialPreviewRect(content, sceneContext));
            continue;
        }
        if (panel->kind == DockPanelKind::MaterialEditor) {
            AppendMaterialPreview(
                layouts,
                kMaterialEditorPreviewViewportKey,
                MaterialEditorPanelRenderer::MaterialPreviewRect(content, sceneContext));
        }
    }

    return layouts;
}

void EditorHostSurfaceLayoutResolver::SyncMainWindow(
    HWND window,
    const EditorDockModel& dockModel,
    const EditorMetrics& metrics,
    const EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& sceneViewport) {
    const std::vector<EditorSceneBgfxViewport::HostSurfaceLayout> layouts =
        ResolveMainWindow(window, dockModel, metrics, sceneContext);
    sceneViewport.SyncHostSurfaceLayoutsForResize(
        window,
        std::span<const EditorSceneBgfxViewport::HostSurfaceLayout>{layouts.data(), layouts.size()});
}

} // namespace kb::editor

#endif
