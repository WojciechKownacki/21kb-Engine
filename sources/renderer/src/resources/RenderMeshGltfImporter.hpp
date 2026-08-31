#pragma once

#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

namespace kb::render {

class RenderMeshGltfImporter final {
public:
    [[nodiscard]] static std::optional<RenderMeshAssetData> Load(const std::filesystem::path& path, const RenderMeshGltfImportDesc& desc);
    [[nodiscard]] static std::optional<RenderMeshAssetData> Load(
        std::span<const std::uint8_t> bytes,
        const std::filesystem::path& sourcePath,
        const RenderMeshGltfImportDesc& desc);
    [[nodiscard]] static std::optional<std::vector<std::filesystem::path>> ExternalBufferUris(
        std::span<const std::uint8_t> bytes);
};

} // namespace kb::render
