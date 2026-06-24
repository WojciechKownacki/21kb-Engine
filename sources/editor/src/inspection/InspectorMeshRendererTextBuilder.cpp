#include "inspection/InspectorMeshRendererTextBuilder.hpp"

#include <cstdio>
#include <string>

namespace kb::editor {

void InspectorMeshRendererTextBuilder::Append(std::string& text, const kb::scene::MeshRendererComponent& renderer) const {
    char component[320]{};
    std::snprintf(
        component,
        sizeof(component),
        "\n\nMesh Renderer\nMesh: %llu\nMaterial: %llu\nMaterial slot overrides: %u\nCasts shadow: %s\nReceives shadow: %s",
        static_cast<unsigned long long>(renderer.meshAssetId),
        static_cast<unsigned long long>(renderer.materialAssetId),
        renderer.materialSlotOverrideCount,
        renderer.castsShadow ? "true" : "false",
        renderer.receivesShadow ? "true" : "false");
    text += component;
    for (std::uint32_t slotIndex = 0U; slotIndex < renderer.materialSlotOverrideCount && slotIndex < kb::scene::kMaxMeshRendererMaterialSlotOverrides; ++slotIndex) {
        text += "\nMaterial slot ";
        text += std::to_string(slotIndex);
        text += ": ";
        text += renderer.materialSlotAssetIds[slotIndex] == 0U ? "None" : std::to_string(renderer.materialSlotAssetIds[slotIndex]);
    }
}

} // namespace kb::editor
