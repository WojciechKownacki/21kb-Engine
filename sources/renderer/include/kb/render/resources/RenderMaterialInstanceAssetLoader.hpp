#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/IAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <typeindex>
#include <vector>

namespace kb::render {

inline constexpr std::uint32_t kRenderMaterialInstanceAssetDocumentVersion = 1U;

struct RenderMaterialInstanceAssetData {
    std::uint32_t documentVersion = kRenderMaterialInstanceAssetDocumentVersion;
    bool hasExplicitDocumentVersion = false;
    kb::assets::AssetId parentMaterialAssetId{};
    RenderMaterialAssetData overrides{};
};

enum class RenderMaterialInstanceAssetParseDiagnosticCode : std::uint8_t {
    FileOpenFailed,
    EmptyDocument,
    UnknownField,
    InvalidFieldValue,
    InvalidDocumentVersion,
    UnsupportedDocumentVersion,
    MissingParentMaterial,
};

struct RenderMaterialInstanceAssetParseDiagnostic {
    RenderMaterialInstanceAssetParseDiagnosticCode code = RenderMaterialInstanceAssetParseDiagnosticCode::InvalidFieldValue;
    std::size_t line = 0U;
    kb::assets::AssetId assetId{};
    std::filesystem::path path;
    std::string field;
    std::string message;
    std::string text;
};

[[nodiscard]] std::string_view RenderMaterialInstanceAssetParseDiagnosticCodeName(RenderMaterialInstanceAssetParseDiagnosticCode code) noexcept;

struct RenderMaterialInstanceAssetParseResult {
    std::optional<RenderMaterialInstanceAssetData> asset;
    std::vector<RenderMaterialInstanceAssetParseDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept;
    [[nodiscard]] std::string ErrorMessage() const;
};

class RenderMaterialInstanceAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;

    [[nodiscard]] static std::optional<RenderMaterialInstanceAssetData> LoadInstance(const std::filesystem::path& path);
    [[nodiscard]] static std::optional<RenderMaterialInstanceAssetData> LoadInstance(std::istream& input);
    [[nodiscard]] static RenderMaterialInstanceAssetParseResult LoadInstanceWithDiagnostics(const std::filesystem::path& path);
    [[nodiscard]] static RenderMaterialInstanceAssetParseResult LoadInstanceWithDiagnostics(const std::filesystem::path& path, kb::assets::AssetId assetId);
    [[nodiscard]] static RenderMaterialInstanceAssetParseResult LoadInstanceWithDiagnostics(std::istream& input);
};

} // namespace kb::render
