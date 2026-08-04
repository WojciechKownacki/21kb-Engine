#include "rendering/AnimatorEditorPanelRenderer.hpp"

#if defined(_WIN32)
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <string>

namespace kb::editor {

void AnimatorEditorPanelRenderer::Paint(
    HDC dc,
    HWND host,
    const RECT& content,
    const DockPanel& panel,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& renderBackendSettings,
    EditorSceneBgfxViewport* sceneViewport) const {
    if (!sceneContext.HasAnimatorEditorAsset() || sceneContext.AnimatorEditorPreviewScene() == nullptr) {
        GdiDrawing::FillRectColor(dc, content, RGB(27, 29, 33));
        const ScopedFont font{ 15, FW_NORMAL };
        const ScopedGdiObject selectedFont(dc, font.handle);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(168, 178, 190));
        RECT text = content;
        DrawTextA(dc, "Open an Animator Controller asset to begin editing.", -1, &text,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }

    constexpr int headerHeight = 30;
    GdiDrawing::FillRectColor(dc, RECT{ content.left, content.top, content.right, content.top + headerHeight }, RGB(34, 37, 43));
    const ScopedFont titleFont{ 13, FW_SEMIBOLD };
    const ScopedGdiObject selectedTitleFont(dc, titleFont.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(219, 225, 233));
    const kb::assets::AssetMetadata* metadata =
        sceneContext.Scene().Assets().Manager().Registry().Find(sceneContext.AnimatorEditorAssetId());
    const kb::assets::AssetMetadata* previewMesh =
        sceneContext.Scene().Assets().Manager().Registry().Find(sceneContext.AnimationPreview().SkeletalMeshAsset());
    const std::string title = (metadata == nullptr ? std::string{ "Animator Controller" } : metadata->name) +
        (previewMesh == nullptr ? std::string{} : "  |  Preview " + previewMesh->name);
    RECT text{ content.left + 10, content.top, content.right - 10, content.top + headerHeight };
    DrawTextA(dc, title.c_str(), -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    if (sceneViewport == nullptr) return;
    const std::uint64_t revision = sceneContext.AnimatorEditorPreviewRevision();
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
    sceneViewport->Present(dc, host, RECT{ content.left, content.top + headerHeight, content.right, content.bottom },
        *sceneContext.AnimatorEditorPreviewScene(), theme, settings);
}

} // namespace kb::editor

#endif
