#include "rendering/SkeletalMeshEditorPanelRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/SkeletalMeshEditorPanelLayout.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

namespace kb::editor {
namespace {

void PaintPanel(HDC dc, const RECT& rect, const char* title, const char* subtitle) {
    GdiDrawing::FillRectColor(dc, rect, RGB(28, 30, 34));
    GdiDrawing::DrawSharpFrame(dc, rect, RGB(28, 30, 34), RGB(53, 57, 64));
    const ScopedFont titleFont{ 13, FW_SEMIBOLD };
    const ScopedGdiObject selectedTitleFont(dc, titleFont.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(207, 214, 222));
    RECT titleRect{ rect.left + 10, rect.top + 8, rect.right - 8, rect.top + 28 };
    DrawTextA(dc, title, -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    const ScopedFont subtitleFont{ 11, FW_NORMAL };
    const ScopedGdiObject selectedSubtitleFont(dc, subtitleFont.handle);
    SetTextColor(dc, RGB(139, 149, 161));
    RECT subtitleRect{ rect.left + 10, rect.top + 34, rect.right - 8, rect.bottom - 8 };
    DrawTextA(dc, subtitle, -1, &subtitleRect, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
}

} // namespace

void SkeletalMeshEditorPanelRenderer::Paint(
    HDC dc,
    HWND host,
    const RECT& content,
    const DockPanel& panel,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& renderBackendSettings,
    EditorSceneBgfxViewport* sceneViewport) const {
    static_cast<void>(theme);
    if (!sceneContext.HasSkeletalMeshEditorAsset() ||
        sceneContext.SkeletalMeshEditorPreviewScene() == nullptr) {
        GdiDrawing::FillRectColor(dc, content, RGB(27, 29, 33));
        const ScopedFont font{ 15, FW_NORMAL };
        const ScopedGdiObject selectedFont(dc, font.handle);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(168, 178, 190));
        RECT text = content;
        DrawTextA(dc, "Open a Skeletal Mesh asset to begin editing.", -1, &text,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }
    const SkeletalMeshEditorPanelLayout layout = SkeletalMeshEditorPanelLayoutResolver::Resolve(content);
    PaintPanel(dc, layout.toolbox, "Toolbox", "Preview workspace");
    PaintPanel(dc, layout.skeletonTree, "Skeleton Tree", "Skeleton hierarchy");
    PaintPanel(dc, layout.assetDetails, "Asset Details", "Skeletal Mesh properties");
    if (sceneViewport == nullptr) return;

    const std::uint64_t revision = sceneContext.SkeletalMeshEditorPreviewRevision();
    EditorSceneBgfxViewport::PresentSettings settings{};
    settings.viewportKey = panel.id;
    settings.editorSceneOverlaysEnabled = false;
    settings.sceneRevision = revision;
    settings.sceneDirtyBaseRevision = revision;
    settings.sceneFullSyncRequired = false;
    settings.msaaSamples = renderBackendSettings.MsaaSamples();
    settings.shadowPassEnabled = renderBackendSettings.ShadowsEnabled();
    settings.postProcessEnabled = true;
    settings.selectionMaskEnabled = false;
    settings.selectionOutlineEnabled = false;
    settings.gpuDrivenRuntimeDispatchEnabled = renderBackendSettings.GpuDrivenEnabled();
    sceneViewport->Present(dc, host, layout.viewport, *sceneContext.SkeletalMeshEditorPreviewScene(), theme, settings);
}

} // namespace kb::editor

#endif
