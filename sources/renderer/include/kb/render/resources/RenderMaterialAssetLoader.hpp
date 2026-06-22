#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/IAssetLoader.hpp"
#include "kb/render/resources/RenderResources.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <typeindex>
#include <vector>

namespace kb::render {

inline constexpr std::uint32_t kRenderMaterialAssetDocumentVersion = 1U;
inline constexpr const char* kRenderMaterialAssetBuiltInPbrType = "builtin.pbr";
inline constexpr std::uint32_t kRenderMaterialAssetBuiltInPbrTypeVersion = 1U;

/// RenderMaterialAssetData represents a Material Instance asset (.kbmat file).
/// It stores parameter values and texture references for a specific material
/// instance, using a Material Type (e.g., builtin.pbr) as its shader contract.
/// This is NOT a Material Graph or Material Type definition.
struct RenderMaterialAssetData {
    std::uint32_t documentVersion = kRenderMaterialAssetDocumentVersion;
    bool hasExplicitDocumentVersion = false;
    std::string materialType = kRenderMaterialAssetBuiltInPbrType;
    std::uint32_t materialTypeVersion = kRenderMaterialAssetBuiltInPbrTypeVersion;
    bool hasExplicitMaterialType = false;
    bool hasExplicitMaterialTypeVersion = false;
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

enum class RenderMaterialAssetParseDiagnosticCode : std::uint8_t {
    FileOpenFailed,
    EmptyDocument,
    UnknownField,
    InvalidFieldValue,
    InvalidFloat,
    InvalidEnum,
    OutOfRange,
    UnsupportedAdvancedField,
    InvalidDocumentVersion,
    UnsupportedDocumentVersion,
    MissingMaterialType,
    UnsupportedMaterialType,
    InvalidMaterialTypeVersion,
    UnsupportedMaterialTypeVersion,
    TextureColorSpaceExpectation,
};

enum class RenderMaterialAssetParseDiagnosticSeverity : std::uint8_t {
    Error,
    Warning,
};

struct RenderMaterialAssetParseDiagnostic {
    RenderMaterialAssetParseDiagnosticCode code = RenderMaterialAssetParseDiagnosticCode::InvalidFieldValue;
    RenderMaterialAssetParseDiagnosticSeverity severity = RenderMaterialAssetParseDiagnosticSeverity::Error;
    std::size_t line = 0U;
    kb::assets::AssetId assetId{};
    std::filesystem::path path;
    std::string field;
    std::string message;
    std::string text;
};

[[nodiscard]] std::string_view RenderMaterialAssetParseDiagnosticCodeName(RenderMaterialAssetParseDiagnosticCode code) noexcept;

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
    [[nodiscard]] static RenderMaterialAssetParseResult LoadMaterialWithDiagnostics(const std::filesystem::path& path, kb::assets::AssetId assetId);
    [[nodiscard]] static RenderMaterialAssetParseResult LoadMaterialWithDiagnostics(std::istream& input);
};

} // namespace kb::render
