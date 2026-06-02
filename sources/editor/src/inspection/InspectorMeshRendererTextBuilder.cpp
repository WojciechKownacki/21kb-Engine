#include "inspection/InspectorMeshRendererTextBuilder.hpp"

#include <cstdio>

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
}

} // namespace kb::editor
