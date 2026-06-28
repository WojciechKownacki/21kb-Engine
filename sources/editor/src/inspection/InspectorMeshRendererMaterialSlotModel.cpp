#include "inspection/InspectorMeshRendererMaterialSlotModel.hpp"

#include <algorithm>
#include <utility>

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
    std::string label = "Slot " + std::to_string(slotIndex + 1U) + " Override";
    if (mesh.has_value() && slotIndex < mesh->materialNames.size() && !mesh->materialNames[slotIndex].empty()) {
        label += " (" + mesh->materialNames[slotIndex] + ")";
    }
    return label;
}

[[nodiscard]] std::string ImportedSourceName(const std::optional<kb::render::RenderMeshAssetData>& mesh, std::uint32_t slotIndex) {
    if (!mesh.has_value() || slotIndex >= mesh->materialNames.size() || mesh->materialNames[slotIndex].empty()) {
        return "-";
    }
    return mesh->materialNames[slotIndex];
}

[[nodiscard]] std::string SlotName(const std::optional<kb::render::RenderMeshAssetData>& mesh, std::uint32_t slotIndex) {
    const std::string importedName = ImportedSourceName(mesh, slotIndex);
    if (importedName != "-") {
        return importedName;
    }
    return "Slot " + std::to_string(slotIndex + 1U);
}

[[nodiscard]] std::vector<std::uint32_t> SectionsUsingSlot(
    const std::optional<kb::render::RenderMeshAssetData>& mesh,
    std::uint32_t slotIndex) {
    std::vector<std::uint32_t> sections;
    if (!mesh.has_value()) {
        return sections;
    }
    for (std::uint32_t sectionIndex = 0U; sectionIndex < mesh->sections.size(); ++sectionIndex) {
        if (mesh->sections[sectionIndex].materialSlot == slotIndex) {
            sections.push_back(sectionIndex);
        }
    }
    return sections;
}

[[nodiscard]] std::string SectionsSummary(const std::vector<std::uint32_t>& sections) {
    if (sections.empty()) {
        return "None";
    }
    std::string text;
    for (std::uint32_t section : sections) {
        if (!text.empty()) {
            text += ", ";
        }
        text += std::to_string(section);
    }
    return text;
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
    const std::function<std::string(std::uint64_t)>& materialName,
    const std::function<std::string(std::uint64_t)>& materialStatus) {
    std::vector<InspectorMeshRendererMaterialSlotRow> rows;
    const std::uint32_t rowsToBuild = RowCount(renderer, mesh);
    rows.reserve(rowsToBuild);

    for (std::uint32_t slotIndex = 0U; slotIndex < rowsToBuild; ++slotIndex) {
        const std::uint64_t defaultMaterial = DefaultMaterialAssetId(mesh, slotIndex);
        const std::uint64_t overrideMaterial = OverrideMaterialAssetId(renderer, slotIndex);
        const bool hasOverride = overrideMaterial != 0U;
        const std::uint64_t activeMaterial = hasOverride ? overrideMaterial : defaultMaterial;
        std::vector<std::uint32_t> sectionIndices = SectionsUsingSlot(mesh, slotIndex);
        InspectorMeshRendererMaterialSlotRow row{
            .slotIndex = slotIndex,
            .defaultMaterialAssetId = defaultMaterial,
            .overrideMaterialAssetId = overrideMaterial,
            .activeMaterialAssetId = activeMaterial,
            .hasOverride = hasOverride,
            .slotName = SlotName(mesh, slotIndex),
            .importedSourceName = ImportedSourceName(mesh, slotIndex),
            .defaultMaterialName = defaultMaterial != 0U ? materialName(defaultMaterial) : "None",
            .overrideMaterialName = hasOverride ? materialName(overrideMaterial) : "None",
            .activeMaterialName = activeMaterial != 0U ? materialName(activeMaterial) : "None",
            .activeMaterialStatus = activeMaterial != 0U && materialStatus ? materialStatus(activeMaterial) : "None",
            .sectionsUsingSlot = SectionsSummary(sectionIndices),
            .sectionIndices = std::move(sectionIndices),
            .label = SlotLabel(mesh, slotIndex),
        };
        row.value = row.overrideMaterialName;
        rows.push_back(std::move(row));
    }
    return rows;
}

} // namespace kb::editor
