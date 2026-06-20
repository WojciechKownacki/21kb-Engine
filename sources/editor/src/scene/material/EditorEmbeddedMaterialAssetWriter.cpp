#include "scene/material/EditorEmbeddedMaterialAssetWriter.hpp"

#include "engine/assets/AssetManager.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "scene/material/EditorEmbeddedMaterialTextureResolver.hpp"
#include "scene/material/EditorMaterialAssetGateway.hpp"

#include <cctype>
#include <string>
#include <string_view>

namespace kb::editor {
namespace {

[[nodiscard]] std::string SanitizedMaterialName(std::string_view name, std::uint32_t slotIndex) {
    std::string output;
    output.reserve(name.size());
    for (const char character : name) {
        const unsigned char value = static_cast<unsigned char>(character);
        output.push_back(std::isalnum(value) ? character : '_');
    }
    while (!output.empty() && output.back() == '_') {
        output.pop_back();
    }
    if (output.empty()) {
        output = "EmbeddedMaterial" + std::to_string(slotIndex);
    }
    return output;
}

[[nodiscard]] std::string BaseNameFor(const kb::render::RenderMeshEmbeddedMaterial& embedded, std::uint32_t slotIndex) {
    return SanitizedMaterialName(embedded.name, slotIndex);
}

} // namespace

std::optional<EditorExtractedMaterialSlot> EditorEmbeddedMaterialAssetWriter::Write(
    const kb::render::RenderMeshEmbeddedMaterial& embedded,
    std::uint32_t slotIndex,
    const kb::assets::AssetMetadata& meshMetadata,
    const std::filesystem::path& outputFolder,
    kb::assets::AssetManager& manager,
    std::vector<std::string>& diagnostics) {
    kb::render::RenderMaterialAssetData material{};
    material.desc = embedded.desc;
    EditorEmbeddedMaterialTextureResolver::Resolve(material, embedded, meshMetadata, manager, diagnostics);

    const std::filesystem::path materialPath = EditorMaterialAssetGateway::UniqueFilePath(outputFolder, BaseNameFor(embedded, slotIndex));
    if (!kb::render::RenderMaterialAssetWriter::Save(materialPath, material)) {
        diagnostics.push_back("Could not write extracted material: " + materialPath.generic_string());
        return std::nullopt;
    }

    static_cast<void>(manager.DiscoverMountedAssets());
    const std::optional<std::filesystem::path> virtualPath = manager.Mounts().ToVirtual(materialPath);
    if (!virtualPath.has_value()) {
        diagnostics.push_back("Extracted material is outside mounted project assets: " + materialPath.generic_string());
        return std::nullopt;
    }

    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(*virtualPath);
    if (metadata == nullptr || metadata->type != "RenderMaterial") {
        diagnostics.push_back("Extracted material was not registered as RenderMaterial: " + virtualPath->generic_string());
        return std::nullopt;
    }

    return EditorExtractedMaterialSlot{
        .slotIndex = slotIndex,
        .materialAssetId = metadata->id,
        .virtualPath = *virtualPath,
    };
}

} // namespace kb::editor
