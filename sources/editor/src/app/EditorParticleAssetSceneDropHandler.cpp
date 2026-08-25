#include "app/EditorParticleAssetSceneDropHandler.hpp"

#if defined(_WIN32)
#include "app/EditorDropPanelResolver.hpp"

#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

} // namespace

bool EditorParticleAssetSceneDropHandler::Drop(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    kb::assets::AssetId assetId) {
    const std::optional<RECT> scene = EditorDropPanelResolver::Resolve(
        DockPanelKind::Scene, sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!scene.has_value() || !Contains(*scene, x, y)) {
        return false;
    }

    return sceneContext.CreateParticleEffectEntity(assetId).IsValid();
}

} // namespace kb::editor

#endif
