#include "rendering/ParticleEditorPanelRenderer.hpp"

#if defined(_WIN32)
#include "engine/scene/SceneAssets.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "scene/EditorSceneContext.hpp"

#include <string>

namespace kb::editor {
namespace {
constexpr int kHeaderHeight = 32;
}

RECT ParticleEditorPanelRenderer::ViewportRect(const RECT& content) noexcept {
    return {content.left, content.top + kHeaderHeight, content.right, content.bottom};
}

bool ParticleEditorPanelRenderer::PresentViewport(
    EditorSceneBgfxViewport& viewport,
    HWND host,
    const RECT& content,
    const DockPanel& panel,
    const EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& settings) {
    const kb::scene::Scene* preview = sceneContext.ParticleEditorPreviewScene();
    const RECT renderRect = ViewportRect(content);
    if (preview == nullptr || renderRect.right <= renderRect.left || renderRect.bottom <= renderRect.top) return false;
    const std::uint64_t revision = sceneContext.ParticleEditorPreviewRevision();
    EditorSceneBgfxViewport::PresentSettings present{};
    present.viewportKey = panel.id;
    present.editorSceneOverlaysEnabled = false;
    present.sceneRevision = revision;
    present.sceneDirtyBaseRevision = revision;
    present.sceneFullSyncRequired = false;
    present.msaaSamples = settings.MsaaSamples();
    present.shadowPassEnabled = false;
    present.postProcessEnabled = true;
    present.selectionMaskEnabled = false;
    present.selectionOutlineEnabled = false;
    present.gpuDrivenRuntimeDispatchEnabled = settings.GpuDrivenEnabled();
    viewport.Present(host, renderRect, *preview, present);
    return true;
}

void ParticleEditorPanelRenderer::Paint(
    HDC dc,
    HWND host,
    const RECT& content,
    const DockPanel& panel,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& renderBackendSettings,
    EditorSceneBgfxViewport* sceneViewport) const {
    GdiDrawing::FillRectColor(dc, content, RGB(27, 29, 33));
    GdiDrawing::FillRectColor(dc, {content.left, content.top, content.right, content.top + kHeaderHeight}, RGB(34, 37, 43));
    const ScopedFont font{13, FW_SEMIBOLD};
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(219, 225, 233));
    RECT header{content.left + 10, content.top, content.right - 10, content.top + kHeaderHeight};
    std::string title = "21kb Particle System";
    if (sceneContext.HasParticleEditorAsset()) {
        const auto* metadata = sceneContext.Scene().Assets().Manager().Registry().Find(sceneContext.ParticleEditorAssetId());
        if (metadata != nullptr) title = metadata->name.empty() ? metadata->virtualPath.filename().string() : metadata->name;
        if (sceneContext.ParticleEditorDirty()) title += " *";
    }
    DrawTextA(dc, title.c_str(), -1, &header, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (!sceneContext.HasParticleEditorAsset()) {
        SetTextColor(dc, RGB(168, 178, 190));
        RECT empty = ViewportRect(content);
        DrawTextA(dc, "Open a .kbvfx asset to begin editing.", -1, &empty,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }
    if (sceneViewport != nullptr) {
        static_cast<void>(PresentViewport(*sceneViewport, host, content, panel, sceneContext, renderBackendSettings));
    }
    static_cast<void>(theme);
}

} // namespace kb::editor
#endif
