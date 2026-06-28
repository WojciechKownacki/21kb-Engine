#pragma once

#include "engine/assets/IAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialTypeSchema.hpp"

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <typeindex>
#include <vector>

namespace kb::render {

inline constexpr const char* kRenderMaterialTypeAssetExtension = ".kbmaterialtype";
inline constexpr const char* kRenderMaterialTypeAssetType = "RenderMaterialType";

class RenderMaterialTypeAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;

    [[nodiscard]] static std::optional<RenderMaterialTypeDocument> LoadType(const std::filesystem::path& path);
    [[nodiscard]] static std::optional<RenderMaterialTypeDocument> LoadType(std::istream& input);
    [[nodiscard]] static RenderMaterialTypeDocumentParseResult LoadTypeWithDiagnostics(const std::filesystem::path& path);
    [[nodiscard]] static RenderMaterialTypeDocumentParseResult LoadTypeWithDiagnostics(std::istream& input);
    [[nodiscard]] static bool SaveType(const std::filesystem::path& path, const RenderMaterialTypeDocument& document);
};

} // namespace kb::render
