#pragma once

#include "engine/scene/MeshRendererComponent.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace kb::editor {

struct InspectorMeshRendererMaterialSlotRow {
    std::uint32_t slotIndex = 0U;
    std::uint64_t defaultMaterialAssetId = 0U;
    std::uint64_t overrideMaterialAssetId = 0U;
    std::uint64_t activeMaterialAssetId = 0U;
    bool hasOverride = false;
    std::string slotName;
    std::string importedSourceName;
    std::string defaultMaterialName;
    std::string overrideMaterialName;
    std::string activeMaterialName;
    std::string activeMaterialStatus;
    std::string sectionsUsingSlot;
    std::vector<std::uint32_t> sectionIndices;
    std::string label;
    std::string value;
};

class InspectorMeshRendererMaterialSlotModel {
public:
    InspectorMeshRendererMaterialSlotModel() = delete;

    [[nodiscard]] static std::vector<InspectorMeshRendererMaterialSlotRow> Build(
        const kb::scene::MeshRendererComponent& renderer,
        const std::optional<kb::render::RenderMeshAssetData>& mesh,
        const std::function<std::string(std::uint64_t)>& materialName,
        const std::function<std::string(std::uint64_t)>& materialStatus = {});
};

} // namespace kb::editor
