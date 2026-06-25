#include "inspection/InspectorMeshRendererMaterialSlotModel.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

[[nodiscard]] std::uint32_t MeshSlotCount(const std::optional<kb::render::RenderMeshAssetData>& mesh) noexcept {
    if (!mesh.has_value()) {
        return 0U;
    }
    return static_cast<std::uint32_t>(std::max(mesh->materialSlots.size(), mesh->materialNames.size()));
}

[[nodiscard]] std::uint32_t RowCount(const kb::scene::MeshRendererComponent& renderer, const std::optional<kb::render::RenderMeshAssetData>& mesh) noexcept {
    const std::uint32_t overrideCount = std::min(renderer.materialSlotOverrideCount, kb::scene::kMaxMeshRendererMaterialSlotOverrides);
    return std::min<std::uint32_t>(std::max(MeshSlotCount(mesh), overrideCount), kb::scene::kMaxMeshRendererMaterialSlotOverrides);
}

[[nodiscard]] std::string SlotLabel(const std::optional<kb::render::RenderMeshAssetData>& mesh, std::uint32_t slotIndex) {
    std::string label = slotIndex == 0U ? "Material Override" : "Material Override " + std::to_string(slotIndex + 1U);
    if (slotIndex > 0U && mesh.has_value() && slotIndex < mesh->materialNames.size() && !mesh->materialNames[slotIndex].empty()) {
        label += " (" + mesh->materialNames[slotIndex] + ")";
    }
    return label;
}

[[nodiscard]] std::uint64_t DefaultMaterialAssetId(const std::optional<kb::render::RenderMeshAssetData>& mesh, std::uint32_t slotIndex) noexcept {
    if (!mesh.has_value() || slotIndex >= mesh->materialSlots.size()) {
        return 0U;
    }
    return mesh->materialSlots[slotIndex].defaultMaterialAssetId;
}

[[nodiscard]] std::uint64_t OverrideMaterialAssetId(const kb::scene::MeshRendererComponent& renderer, std::uint32_t slotIndex) noexcept {
    if (slotIndex >= renderer.materialSlotOverrideCount || slotIndex >= kb::scene::kMaxMeshRendererMaterialSlotOverrides) {
        return 0U;
    }
    return renderer.materialSlotAssetIds[slotIndex];
}

} // namespace

std::vector<InspectorMeshRendererMaterialSlotRow> InspectorMeshRendererMaterialSlotModel::Build(
    const kb::scene::MeshRendererComponent& renderer,
    const std::optional<kb::render::RenderMeshAssetData>& mesh,
    const std::function<std::string(std::uint64_t)>& materialName) {
    std::vector<InspectorMeshRendererMaterialSlotRow> rows;
    const std::uint32_t rowsToBuild = RowCount(renderer, mesh);
    rows.reserve(rowsToBuild);

    for (std::uint32_t slotIndex = 0U; slotIndex < rowsToBuild; ++slotIndex) {
        const std::uint64_t defaultMaterial = DefaultMaterialAssetId(mesh, slotIndex);
        const std::uint64_t overrideMaterial = OverrideMaterialAssetId(renderer, slotIndex);
        const bool hasOverride = overrideMaterial != 0U;
        InspectorMeshRendererMaterialSlotRow row{
            .slotIndex = slotIndex,
            .defaultMaterialAssetId = defaultMaterial,
            .overrideMaterialAssetId = overrideMaterial,
            .hasOverride = hasOverride,
            .label = SlotLabel(mesh, slotIndex),
        };
        row.value = hasOverride ? materialName(overrideMaterial) : "None";
        rows.push_back(std::move(row));
    }
    return rows;
}

} // namespace kb::editor
