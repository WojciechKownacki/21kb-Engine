#pragma once

#include "engine/assets/IAssetLoader.hpp"
#include "kb/render/resources/RenderResources.hpp"

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <typeindex>
#include <vector>

namespace kb::render {

struct RenderMaterialAssetData {
    RenderMaterialDesc desc{};
    std::string albedoTexturePath;
    std::string normalTexturePath;
    std::string metallicRoughnessTexturePath;
    std::string occlusionTexturePath;
    std::string emissiveTexturePath;
    std::string clearcoatTexturePath;
    std::string clearcoatRoughnessTexturePath;
    std::string sheenColorTexturePath;
    std::string transmissionTexturePath;
    std::string thicknessTexturePath;
    std::string anisotropyTexturePath;
    std::string decalTexturePath;
    std::string layerMaskTexturePath;
};

struct RenderMaterialAssetParseDiagnostic {
    std::size_t line = 0U;
    std::string field;
    std::string message;
    std::string text;
};

struct RenderMaterialAssetParseResult {
    std::optional<RenderMaterialAssetData> asset;
    std::vector<RenderMaterialAssetParseDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept;
    [[nodiscard]] std::string ErrorMessage() const;
};

class RenderMaterialAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;

    [[nodiscard]] static std::optional<RenderMaterialAssetData> LoadMaterial(const std::filesystem::path& path);
    [[nodiscard]] static std::optional<RenderMaterialAssetData> LoadMaterial(std::istream& input);
    [[nodiscard]] static RenderMaterialAssetParseResult LoadMaterialWithDiagnostics(const std::filesystem::path& path);
    [[nodiscard]] static RenderMaterialAssetParseResult LoadMaterialWithDiagnostics(std::istream& input);
};

} // namespace kb::render
