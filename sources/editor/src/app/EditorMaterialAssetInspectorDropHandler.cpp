#include "app/EditorMaterialAssetInspectorDropHandler.hpp"

#if defined(_WIN32)
#include "app/EditorDropPanelResolver.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "inspection/InspectorPanelState.hpp"
#include "rendering/InspectorPanelRenderer.hpp"

#include <cstdint>
#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] std::optional<std::uint32_t> SlotIndexForProperty(InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::MeshRendererMaterialSlot0:
        return 0U;
    case InspectorPropertyId::MeshRendererMaterialSlot1:
        return 1U;
    case InspectorPropertyId::MeshRendererMaterialSlot2:
        return 2U;
    case InspectorPropertyId::MeshRendererMaterialSlot3:
        return 3U;
    case InspectorPropertyId::MeshRendererMaterialSlot4:
        return 4U;
    case InspectorPropertyId::MeshRendererMaterialSlot5:
        return 5U;
    case InspectorPropertyId::MeshRendererMaterialSlot6:
        return 6U;
    case InspectorPropertyId::MeshRendererMaterialSlot7:
        return 7U;
    default:
        return std::nullopt;
    }
}

} // namespace

bool EditorMaterialAssetInspectorDropHandler::Drop(
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

    const kb::scene::SceneEntity entity = sceneContext.SelectedEntity();
    if (!entity.IsValid() || !sceneContext.Scene().Entities().IsAlive(entity)) {
        return false;
    }

    const InspectorPanelRenderer::Hit hit = InspectorPanelRenderer::HitTest(*inspector, sceneContext, x, y);
    if (hit.section != InspectorSectionId::MeshRenderer) {
        return false;
    }
    if (hit.property == InspectorPropertyId::MeshRendererMaterial) {
        return sceneContext.SetMeshRendererMaterialAsset(entity, assetId);
    }
    if (const std::optional<std::uint32_t> slotIndex = SlotIndexForProperty(hit.property)) {
        return sceneContext.SetMeshRendererMaterialSlotAsset(entity, *slotIndex, assetId);
    }
    return false;
}

} // namespace kb::editor

#endif
