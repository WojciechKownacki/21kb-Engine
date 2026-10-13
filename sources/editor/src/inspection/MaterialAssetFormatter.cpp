#include "inspection/MaterialAssetFormatter.hpp"
#include "localization/EditorLocalization.hpp"

#include "inspection/EditorValueFormatter.hpp"

#include <string>
#include <utility>

namespace kb::editor {
namespace {

[[nodiscard]] std::string FormatRgb(const float (&value)[3]) {
    return EditorValueFormatter::FormatFloat(value[0]) + ", " +
        EditorValueFormatter::FormatFloat(value[1]) + ", " +
        EditorValueFormatter::FormatFloat(value[2]);
}

[[nodiscard]] std::string FormatRgba(const float (&value)[4]) {
    return EditorValueFormatter::FormatFloat(value[0]) + ", " +
        EditorValueFormatter::FormatFloat(value[1]) + ", " +
        EditorValueFormatter::FormatFloat(value[2]) + ", " +
        EditorValueFormatter::FormatFloat(value[3]);
}

[[nodiscard]] std::string TextureOrFallback(std::uint64_t assetId, std::string_view fallback) {
    return assetId == 0U
        ? std::string{ fallback }
        : EditorLocalization::Format("inspector.materialChannel.texture", "texture #{id}", { { "id", EditorValueFormatter::FormatUInt64(assetId) } });
}

} // namespace

std::string MaterialAssetFormatter::AlphaModeName(kb::render::RenderMaterialAlphaMode mode) {
    switch (mode) {
    case kb::render::RenderMaterialAlphaMode::Opaque:
        return std::string{ EditorText("material.alphaMode.opaque", "Opaque") };
    case kb::render::RenderMaterialAlphaMode::Mask:
        return std::string{ EditorText("material.alphaMode.mask", "Mask") };
    case kb::render::RenderMaterialAlphaMode::Blend:
        return std::string{ EditorText("material.alphaMode.blend", "Blend") };
    }
    return std::string{ EditorText("material.alphaMode.opaque", "Opaque") };
}

std::vector<MaterialDebugChannelRow> MaterialAssetFormatter::DebugChannelRows(
    const kb::render::RenderMaterialDesc& material,
    std::uint64_t materialAssetId) {
    std::vector<MaterialDebugChannelRow> rows;
    rows.reserve(6U);
    rows.push_back(MaterialDebugChannelRow{
        .label = std::string{ EditorText("inspector.materialPreview.materialId", "Material Id") },
        .value = EditorValueFormatter::FormatUInt64(materialAssetId),
    });
    rows.push_back(MaterialDebugChannelRow{
        .label = std::string{ EditorText("inspector.materialChannel.baseColor", "Base Color") },
        .value = EditorLocalization::Format("inspector.materialChannel.baseColorValue", "rgba {color} | {source}", {
            { "color", FormatRgba(material.baseColor) },
            { "source", TextureOrFallback(material.albedoTextureAssetId, EditorText("inspector.materialChannel.whiteFallback", "white fallback")) },
        }),
    });
    rows.push_back(MaterialDebugChannelRow{
        .label = std::string{ EditorText("inspector.materialChannel.roughness", "Roughness") },
        .value = EditorLocalization::Format("inspector.materialChannel.roughnessValue", "factor {factor} | MR.g {source}", {
            { "factor", EditorValueFormatter::FormatFloat(material.roughnessFactor) },
            { "source", TextureOrFallback(material.metallicRoughnessTextureAssetId, EditorText("inspector.materialChannel.whiteFallback", "white fallback")) },
        }),
    });
    rows.push_back(MaterialDebugChannelRow{
        .label = std::string{ EditorText("inspector.materialChannel.metallic", "Metallic") },
        .value = EditorLocalization::Format("inspector.materialChannel.metallicValue", "factor {factor} | MR.b {source}", {
            { "factor", EditorValueFormatter::FormatFloat(material.metallicFactor) },
            { "source", TextureOrFallback(material.metallicRoughnessTextureAssetId, EditorText("inspector.materialChannel.whiteFallback", "white fallback")) },
        }),
    });
    rows.push_back(MaterialDebugChannelRow{
        .label = std::string{ EditorText("inspector.materialChannel.normal", "Normal") },
        .value = EditorLocalization::Format("inspector.materialChannel.normalValue", "scale {scale} | {source}", {
            { "scale", EditorValueFormatter::FormatFloat(material.normalScale) },
            { "source", TextureOrFallback(material.normalTextureAssetId, EditorText("inspector.materialChannel.flatNormalFallback", "flat normal fallback")) },
        }),
    });
    rows.push_back(MaterialDebugChannelRow{
        .label = std::string{ EditorText("inspector.materialChannel.emissive", "Emissive") },
        .value = EditorLocalization::Format("inspector.materialChannel.emissiveValue", "rgb {color} strength {strength} | {source}", {
            { "color", FormatRgb(material.emissiveColor) },
            { "strength", EditorValueFormatter::FormatFloat(material.emissiveStrength) },
            { "source", TextureOrFallback(material.emissiveTextureAssetId, EditorText("inspector.materialChannel.whiteFallback", "white fallback")) },
        }),
    });
    return rows;
}

} // namespace kb::editor
