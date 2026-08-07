#pragma once

#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::render {

struct RenderMeshSourceTexture {
    std::string key;
    std::filesystem::path sourcePath;
    std::vector<std::byte> embeddedBytes;
    std::string extension;

    [[nodiscard]] bool IsEmbedded() const noexcept { return !embeddedBytes.empty(); }
};

struct RenderMeshSourceImportManifest {
    std::vector<RenderMeshEmbeddedMaterial> materials;
    std::vector<RenderMeshSourceTexture> textures;
};

class RenderMeshSourceImport final {
public:
    RenderMeshSourceImport() = delete;

    [[nodiscard]] static std::optional<RenderMeshSourceImportManifest> Inspect(
        const std::filesystem::path& sourcePath,
        std::string* error = nullptr);

    [[nodiscard]] static std::filesystem::path GeneratedMaterialVirtualPath(
        const std::filesystem::path& meshVirtualPath,
        std::string_view sourceName,
        std::string_view materialName);
};

} // namespace kb::render
