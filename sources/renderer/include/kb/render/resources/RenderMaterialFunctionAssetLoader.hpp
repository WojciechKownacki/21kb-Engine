#pragma once

#include "engine/assets/IAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <typeindex>
#include <vector>

namespace kb::render {

inline constexpr const char* kRenderMaterialFunctionAssetExtension = ".kbmatfn";
inline constexpr const char* kRenderMaterialFunctionAssetType = "RenderMaterialFunction";

struct RenderMaterialFunctionAssetData {
    std::uint32_t documentVersion = kRenderMaterialGraphDocumentVersion;
    bool hasExplicitDocumentVersion = false;
    RenderMaterialGraphDocument graph{};
};

class RenderMaterialFunctionAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;
    [[nodiscard]] std::vector<kb::assets::AssetId> DiscoverDependencies(
        const kb::assets::AssetMetadata& metadata,
        const kb::assets::AssetRegistry& registry) const override;

    [[nodiscard]] static std::optional<RenderMaterialFunctionAssetData> LoadFunction(const std::filesystem::path& path);
    [[nodiscard]] static std::optional<RenderMaterialFunctionAssetData> LoadFunction(std::istream& input);
    [[nodiscard]] static RenderMaterialAssetParseResult LoadFunctionWithDiagnostics(const std::filesystem::path& path);
    [[nodiscard]] static RenderMaterialAssetParseResult LoadFunctionWithDiagnostics(const std::filesystem::path& path, kb::assets::AssetId assetId);
    [[nodiscard]] static RenderMaterialAssetParseResult LoadFunctionWithDiagnostics(std::istream& input);
    [[nodiscard]] static bool SaveFunction(const std::filesystem::path& path, const RenderMaterialFunctionAssetData& function);
};

} // namespace kb::render
