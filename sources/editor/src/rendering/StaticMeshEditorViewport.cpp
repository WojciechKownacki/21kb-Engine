#include "rendering/StaticMeshEditorViewport.hpp"

#if defined(_WIN32)
#include "rendering/EditorPreviewRenderCamera.hpp"
#include "rendering/StaticMeshEditorPanel.hpp"

namespace kb::editor {

bool StaticMeshEditorViewport::Present(
    EditorSceneBgfxViewport& viewport, HWND host, const RECT& content, const DockPanel& panel,
    const StaticMeshEditorSession& session) {
    const kb::scene::Scene* scene = session.PreviewScene();
    if (host == nullptr || IsWindow(host) == 0 || IsWindowVisible(host) == 0 || scene == nullptr) return false;
    const RECT rect = StaticMeshEditorPanel::ViewportRect(content);
    if (rect.right <= rect.left || rect.bottom <= rect.top) return false;
    const std::uint32_t width = static_cast<std::uint32_t>(rect.right - rect.left);
    const std::uint32_t height = static_cast<std::uint32_t>(rect.bottom - rect.top);
    EditorSceneBgfxViewport::PresentSettings settings{};
    settings.renderWidth = width;
    settings.renderHeight = height;
    settings.cameraOverride = BuildEditorPreviewRenderCamera(session.Camera(), width, height);
    settings.viewportKey = panel.id;
    settings.editorSceneOverlaysEnabled = false;
    settings.lightingConfig.debugView = session.ViewMode() == StaticMeshViewMode::Normals
        ? kb::render::SceneRenderDebugView::GBufferNormal
        : kb::render::SceneRenderDebugView::None;
    // The preview has its own monotonic revision, and the camera rides the override: a camera move must not
    // re-synchronize the mesh.
    settings.sceneRevision = session.PreviewRevision();
    settings.sceneDirtyBaseRevision = session.PreviewRevision();
    settings.sceneFullSyncRequired = false;
    settings.msaaSamples = 0U;
    settings.shadowPassEnabled = false;
    settings.postProcessEnabled = false;
    settings.selectionMaskEnabled = false;
    settings.selectionOutlineEnabled = false;
    settings.gpuDrivenRuntimeDispatchEnabled = false;
    viewport.Present(host, rect, *scene, settings);
    return true;
}

} // namespace kb::editor
#endif
