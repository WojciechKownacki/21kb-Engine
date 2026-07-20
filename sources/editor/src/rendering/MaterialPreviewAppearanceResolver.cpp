#include "rendering/MaterialPreviewAppearanceResolver.hpp"

#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace kb::editor {
namespace {

[[nodiscard]] const kb::render::RenderMaterialGraphNode* SourceNodeFor(
    const kb::render::RenderMaterialGraphDocument& graph,
    std::uint32_t toNodeId,
    std::string_view toPin) {
    for (const kb::render::RenderMaterialGraphLink& link : graph.links) {
        if (link.toNodeId == toNodeId && link.toPin == toPin) {
            return kb::render::FindRenderMaterialGraphNode(graph, link.fromNodeId);
        }
    }
    return nullptr;
}

[[nodiscard]] const kb::render::RenderMaterialGraphNode* MaterialOutputNode(
    const kb::render::RenderMaterialGraphDocument& graph) {
    for (const kb::render::RenderMaterialGraphNode& node : graph.nodes) {
        if (node.kind == kb::render::RenderMaterialGraphNodeKind::MaterialOutput) {
            return &node;
        }
    }
    return nullptr;
}

// Constant nodes keep their value as the text hint the graph editor shows, e.g. "0.2 0.4 0.8".
[[nodiscard]] std::optional<std::array<float, 4U>> ParseValueHint(std::string_view hint) {
    std::array<float, 4U> values{ 0.0F, 0.0F, 0.0F, 1.0F };
    std::size_t parsed = 0U;
    std::size_t index = 0U;
    while (index < hint.size() && parsed < values.size()) {
        while (index < hint.size() && (hint[index] == ' ' || hint[index] == ',' || hint[index] == '\t')) {
            ++index;
        }
        if (index >= hint.size()) {
            break;
        }
        const std::string token{ hint.substr(index, hint.find_first_of(" ,\t", index) - index) };
        char* end = nullptr;
        const float value = std::strtof(token.c_str(), &end);
        if (end == token.c_str()) {
            return std::nullopt;
        }
        values[parsed++] = value;
        index += token.size();
    }
    if (parsed == 0U) {
        return std::nullopt;
    }
    if (parsed == 1U) {
        values[1] = values[0];
        values[2] = values[0];
    }
    return values;
}

[[nodiscard]] bool IsTextureNode(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    return kind == kb::render::RenderMaterialGraphNodeKind::TextureSample ||
        kind == kb::render::RenderMaterialGraphNodeKind::TextureSampleCube ||
        kind == kb::render::RenderMaterialGraphNodeKind::TextureSampleVolume ||
        kind == kb::render::RenderMaterialGraphNodeKind::TextureSample2DArray ||
        kind == kb::render::RenderMaterialGraphNodeKind::ParameterTexture ||
        kind == kb::render::RenderMaterialGraphNodeKind::TextureObject;
}

[[nodiscard]] kb::assets::AssetId TextureAssetForNode(
    const kb::render::RenderMaterialAssetData& material,
    const kb::render::RenderMaterialGraphNode& node) {
    const std::string stableId = !node.parameter.stableId.empty()
        ? node.parameter.stableId
        : (node.kind == kb::render::RenderMaterialGraphNodeKind::ParameterTexture
                ? "texture" + std::to_string(node.id)
                : "textureSample" + std::to_string(node.id));
    for (const kb::render::RenderMaterialGraphParameterValue& value : material.graphParameterValues) {
        if (value.stableId == stableId && value.type == kb::render::RenderMaterialParameterType::Texture) {
            return kb::assets::AssetId{ value.assetId };
        }
    }
    return {};
}

[[nodiscard]] std::optional<std::array<float, 4U>> ResolveInput(
    const kb::render::RenderMaterialAssetData& material,
    const kb::assets::AssetManager* assets,
    MaterialPreviewTextureAverageColorFn textureAverageColor,
    std::string_view outputPin) {
    const kb::render::RenderMaterialGraphNode* output = MaterialOutputNode(material.graph);
    if (output == nullptr) {
        return std::nullopt;
    }
    const kb::render::RenderMaterialGraphNode* source = SourceNodeFor(material.graph, output->id, outputPin);
    if (source == nullptr) {
        return std::nullopt;
    }
    if (IsTextureNode(source->kind)) {
        // A flat ball cannot show a grass texture, but the average colour of that texture reads as the
        // right material far better than the white fallback does.
        if (assets != nullptr && textureAverageColor != nullptr) {
            if (const std::optional<std::array<float, 3U>> average =
                    textureAverageColor(*assets, TextureAssetForNode(material, *source))) {
                return std::array<float, 4U>{ (*average)[0], (*average)[1], (*average)[2], 1.0F };
            }
        }
        return std::nullopt;
    }
    return ParseValueHint(source->parameter.defaultValueHint);
}

} // namespace

MaterialPreviewAppearance MaterialPreviewAppearanceResolver::Resolve(
    const kb::render::RenderMaterialAssetData& material,
    const kb::assets::AssetManager* assets,
    MaterialPreviewTextureAverageColorFn textureAverageColor) {
    MaterialPreviewAppearance appearance{};
    appearance.baseColor[0] = material.desc.baseColor[0];
    appearance.baseColor[1] = material.desc.baseColor[1];
    appearance.baseColor[2] = material.desc.baseColor[2];
    appearance.emissiveColor[0] = material.desc.emissiveColor[0];
    appearance.emissiveColor[1] = material.desc.emissiveColor[1];
    appearance.emissiveColor[2] = material.desc.emissiveColor[2];
    appearance.roughness = std::clamp(material.desc.roughnessFactor, 0.0F, 1.0F);
    appearance.metallic = std::clamp(material.desc.metallicFactor, 0.0F, 1.0F);
    appearance.emissiveStrength = std::clamp(material.desc.emissiveStrength, 0.0F, 64.0F);
    if (material.graph.nodes.empty()) {
        return appearance;
    }

    if (const std::optional<std::array<float, 4U>> baseColor = ResolveInput(material, assets, textureAverageColor, "baseColor")) {
        appearance.baseColor[0] = std::clamp((*baseColor)[0], 0.0F, 1.0F);
        appearance.baseColor[1] = std::clamp((*baseColor)[1], 0.0F, 1.0F);
        appearance.baseColor[2] = std::clamp((*baseColor)[2], 0.0F, 1.0F);
        appearance.fromGraph = true;
    }
    if (const std::optional<std::array<float, 4U>> roughness = ResolveInput(material, assets, textureAverageColor, "roughness")) {
        appearance.roughness = std::clamp((*roughness)[0], 0.0F, 1.0F);
        appearance.fromGraph = true;
    }
    if (const std::optional<std::array<float, 4U>> metallic = ResolveInput(material, assets, textureAverageColor, "metallic")) {
        appearance.metallic = std::clamp((*metallic)[0], 0.0F, 1.0F);
        appearance.fromGraph = true;
    }
    if (const std::optional<std::array<float, 4U>> emissive = ResolveInput(material, assets, textureAverageColor, "emissive")) {
        appearance.emissiveColor[0] = std::clamp((*emissive)[0], 0.0F, 1.0F);
        appearance.emissiveColor[1] = std::clamp((*emissive)[1], 0.0F, 1.0F);
        appearance.emissiveColor[2] = std::clamp((*emissive)[2], 0.0F, 1.0F);
        appearance.emissiveStrength = std::max(appearance.emissiveStrength, 1.0F);
        appearance.fromGraph = true;
    }
    return appearance;
}

} // namespace kb::editor
