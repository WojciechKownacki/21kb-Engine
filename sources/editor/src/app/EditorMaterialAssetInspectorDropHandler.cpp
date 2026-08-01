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
    case InspectorPropertyId::MeshRendererMaterialSlotPicker0:
        return 0U;
    case InspectorPropertyId::MeshRendererMaterialSlotPicker1:
        return 1U;
    case InspectorPropertyId::MeshRendererMaterialSlotPicker2:
        return 2U;
    case InspectorPropertyId::MeshRendererMaterialSlotPicker3:
        return 3U;
    case InspectorPropertyId::MeshRendererMaterialSlotPicker4:
        return 4U;
    case InspectorPropertyId::MeshRendererMaterialSlotPicker5:
        return 5U;
    case InspectorPropertyId::MeshRendererMaterialSlotPicker6:
        return 6U;
    case InspectorPropertyId::MeshRendererMaterialSlotPicker7:
        return 7U;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<std::uint8_t> TerrainLayerForProperty(InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::TerrainMaterialLayer0:
    case InspectorPropertyId::TerrainMaterialLayerPicker0: return 0U;
    case InspectorPropertyId::TerrainMaterialLayer1:
    case InspectorPropertyId::TerrainMaterialLayerPicker1: return 1U;
    case InspectorPropertyId::TerrainMaterialLayer2:
    case InspectorPropertyId::TerrainMaterialLayerPicker2: return 2U;
    case InspectorPropertyId::TerrainMaterialLayer3:
    case InspectorPropertyId::TerrainMaterialLayerPicker3: return 3U;
    default: return std::nullopt;
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
    if (hit.section == InspectorSectionId::Terrain) {
        if (const std::optional<std::uint8_t> layer = TerrainLayerForProperty(hit.property)) {
            return sceneContext.SetTerrainMaterialLayer(entity, *layer, assetId);
        }
        if (hit.property == InspectorPropertyId::TerrainMaterialLayerAdd) {
            return sceneContext.AddTerrainMaterialLayer(entity, assetId);
        }
        return false;
    }
    if (hit.section != InspectorSectionId::MeshRenderer) {
        return false;
    }
    if (hit.property == InspectorPropertyId::MeshRendererMaterial || hit.property == InspectorPropertyId::MeshRendererMaterialPicker) {
        return sceneContext.SetMeshRendererMaterialAsset(entity, assetId);
    }
    if (hit.property == InspectorPropertyId::MeshRendererMaterialOverridePicker) {
        return sceneContext.SetMeshRendererMaterialSlotAsset(entity, 0U, assetId);
    }
    if (const std::optional<std::uint32_t> slotIndex = SlotIndexForProperty(hit.property)) {
        return sceneContext.SetMeshRendererMaterialSlotAsset(entity, *slotIndex, assetId);
    }
    return false;
}

} // namespace kb::editor

#endif
