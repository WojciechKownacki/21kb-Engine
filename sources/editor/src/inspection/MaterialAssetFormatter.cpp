#include "inspection/MaterialAssetFormatter.hpp"

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

[[nodiscard]] std::string TextureOrFallback(std::uint64_t assetId, std::string fallback) {
    return assetId == 0U
        ? std::move(fallback)
        : "texture #" + EditorValueFormatter::FormatUInt64(assetId);
}

} // namespace

std::string MaterialAssetFormatter::AlphaModeName(kb::render::RenderMaterialAlphaMode mode) noexcept {
    switch (mode) {
    case kb::render::RenderMaterialAlphaMode::Opaque:
        return "Opaque";
    case kb::render::RenderMaterialAlphaMode::Mask:
        return "Mask";
    case kb::render::RenderMaterialAlphaMode::Blend:
        return "Blend";
    }
    return "Opaque";
}

std::vector<MaterialDebugChannelRow> MaterialAssetFormatter::DebugChannelRows(
    const kb::render::RenderMaterialDesc& material,
    std::uint64_t materialAssetId) {
    std::vector<MaterialDebugChannelRow> rows;
    rows.reserve(6U);
    rows.push_back(MaterialDebugChannelRow{
        .label = "Material Id",
        .value = EditorValueFormatter::FormatUInt64(materialAssetId),
    });
    rows.push_back(MaterialDebugChannelRow{
        .label = "Base Color",
        .value = "rgba " + FormatRgba(material.baseColor) + " | " + TextureOrFallback(material.albedoTextureAssetId, "white fallback"),
    });
    rows.push_back(MaterialDebugChannelRow{
        .label = "Roughness",
        .value = "factor " + EditorValueFormatter::FormatFloat(material.roughnessFactor) + " | MR.g " + TextureOrFallback(material.metallicRoughnessTextureAssetId, "white fallback"),
    });
    rows.push_back(MaterialDebugChannelRow{
        .label = "Metallic",
        .value = "factor " + EditorValueFormatter::FormatFloat(material.metallicFactor) + " | MR.b " + TextureOrFallback(material.metallicRoughnessTextureAssetId, "white fallback"),
    });
    rows.push_back(MaterialDebugChannelRow{
        .label = "Normal",
        .value = "scale " + EditorValueFormatter::FormatFloat(material.normalScale) + " | " + TextureOrFallback(material.normalTextureAssetId, "flat normal fallback"),
    });
    rows.push_back(MaterialDebugChannelRow{
        .label = "Emissive",
        .value = "rgb " + FormatRgb(material.emissiveColor) + " strength " + EditorValueFormatter::FormatFloat(material.emissiveStrength) + " | " + TextureOrFallback(material.emissiveTextureAssetId, "white fallback"),
    });
    return rows;
}

} // namespace kb::editor
