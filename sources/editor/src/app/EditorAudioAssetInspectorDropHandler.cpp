#include "app/EditorAudioAssetInspectorDropHandler.hpp"

#if defined(_WIN32)
#include "app/EditorDropPanelResolver.hpp"
#include "app/EditorAudioInspectorDropPolicy.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "inspection/InspectorPanelState.hpp"
#include "rendering/InspectorPanelRenderer.hpp"

#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

} // namespace

bool EditorAudioAssetInspectorDropHandler::Drop(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    kb::assets::AssetId assetId) {
    const std::optional<RECT> inspector = EditorDropPanelResolver::Resolve(DockPanelKind::Inspector, sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!inspector.has_value() || !Contains(*inspector, x, y)) {
        return false;
    }

    const InspectorPanelRenderer::Hit hit = InspectorPanelRenderer::HitTest(*inspector, sceneContext, x, y);
    if (hit.section == InspectorSectionId::SceneAudioRouting
        && (hit.property == InspectorPropertyId::SceneAudioMixer
            || hit.property == InspectorPropertyId::SceneAudioMixerPicker)) {
        const kb::assets::AssetMetadata* metadata =
            sceneContext.Scene().Assets().Manager().Registry().Find(assetId);
        return metadata != nullptr
            && EditorAudioInspectorDropPolicy::Accepts(*metadata, hit.section, hit.property)
            && sceneContext.SetSceneAudioMixer(assetId);
    }

    const kb::scene::SceneEntity entity = sceneContext.SelectedEntity();
    if (!entity.IsValid() || !sceneContext.Scene().Entities().IsAlive(entity)) {
        return false;
    }
    if (hit.section != InspectorSectionId::AudioSource
        || (hit.property != InspectorPropertyId::AudioSourceClip && hit.property != InspectorPropertyId::AudioSourceClipPicker)) {
        return false;
    }
    const kb::assets::AssetMetadata* metadata =
        sceneContext.Scene().Assets().Manager().Registry().Find(assetId);
    return metadata != nullptr
        && EditorAudioInspectorDropPolicy::Accepts(*metadata, hit.section, hit.property)
        && sceneContext.SetAudioSourceClipAsset(entity, assetId);
}

} // namespace kb::editor

#endif
