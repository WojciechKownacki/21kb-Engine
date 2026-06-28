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

inline constexpr const char* kRenderMaterialGraphAssetExtension = ".kbmaterialgraph";
inline constexpr const char* kRenderMaterialGraphAssetType = "RenderMaterialGraph";

class RenderMaterialGraphAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;
    [[nodiscard]] std::vector<kb::assets::AssetId> DiscoverDependencies(
        const kb::assets::AssetMetadata& metadata,
        const kb::assets::AssetRegistry& registry) const override;

    [[nodiscard]] static std::optional<RenderMaterialGraphDocument> LoadGraph(const std::filesystem::path& path);
    [[nodiscard]] static std::optional<RenderMaterialGraphDocument> LoadGraph(std::istream& input);
    [[nodiscard]] static RenderMaterialAssetParseResult LoadGraphWithDiagnostics(const std::filesystem::path& path);
    [[nodiscard]] static RenderMaterialAssetParseResult LoadGraphWithDiagnostics(const std::filesystem::path& path, kb::assets::AssetId assetId);
    [[nodiscard]] static RenderMaterialAssetParseResult LoadGraphWithDiagnostics(std::istream& input);
    [[nodiscard]] static bool SaveGraph(const std::filesystem::path& path, const RenderMaterialGraphDocument& graph);
};

} // namespace kb::render
