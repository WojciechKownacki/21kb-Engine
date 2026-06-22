#include "kb/render/resources/RenderMaterialAssetLoader.hpp"

#include "resources/RenderMaterialAssetParser.hpp"

#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace kb::render {

std::string_view RenderMaterialAssetParseDiagnosticCodeName(RenderMaterialAssetParseDiagnosticCode code) noexcept {
    switch (code) {
    case RenderMaterialAssetParseDiagnosticCode::FileOpenFailed:
        return "file_open_failed";
    case RenderMaterialAssetParseDiagnosticCode::EmptyDocument:
        return "empty_document";
    case RenderMaterialAssetParseDiagnosticCode::UnknownField:
        return "unknown_field";
    case RenderMaterialAssetParseDiagnosticCode::InvalidFieldValue:
        return "invalid_field_value";
    case RenderMaterialAssetParseDiagnosticCode::InvalidFloat:
        return "invalid_float";
    case RenderMaterialAssetParseDiagnosticCode::InvalidEnum:
        return "invalid_enum";
    case RenderMaterialAssetParseDiagnosticCode::OutOfRange:
        return "out_of_range";
    case RenderMaterialAssetParseDiagnosticCode::UnsupportedAdvancedField:
        return "unsupported_advanced_field";
    case RenderMaterialAssetParseDiagnosticCode::InvalidDocumentVersion:
        return "invalid_document_version";
    case RenderMaterialAssetParseDiagnosticCode::UnsupportedDocumentVersion:
        return "unsupported_document_version";
    case RenderMaterialAssetParseDiagnosticCode::MissingMaterialType:
        return "missing_material_type";
    case RenderMaterialAssetParseDiagnosticCode::UnsupportedMaterialType:
        return "unsupported_material_type";
    case RenderMaterialAssetParseDiagnosticCode::InvalidMaterialTypeVersion:
        return "invalid_material_type_version";
    case RenderMaterialAssetParseDiagnosticCode::UnsupportedMaterialTypeVersion:
        return "unsupported_material_type_version";
    case RenderMaterialAssetParseDiagnosticCode::TextureColorSpaceExpectation:
        return "texture_color_space_expectation";
    }
    return "unknown_diagnostic";
}

bool RenderMaterialAssetParseResult::Succeeded() const noexcept {
    return asset.has_value() && diagnostics.empty();
}

std::string RenderMaterialAssetParseResult::ErrorMessage() const {
    if (diagnostics.empty()) {
        return {};
    }

    std::ostringstream output;
    output << "Render material asset load failed";
    for (const RenderMaterialAssetParseDiagnostic& diagnostic : diagnostics) {
        output << "; code " << RenderMaterialAssetParseDiagnosticCodeName(diagnostic.code);
        if (diagnostic.assetId.IsValid()) {
            output << ", asset " << diagnostic.assetId.value;
        }
        if (!diagnostic.path.empty()) {
            output << ", path " << diagnostic.path.generic_string();
        }
        output << ": ";
        if (diagnostic.line > 0U) {
            output << "line " << diagnostic.line << ": ";
        }
        output << diagnostic.message;
        if (!diagnostic.text.empty()) {
            output << " [" << diagnostic.text << "]";
        }
    }
    return output.str();
}

std::string_view RenderMaterialAssetLoader::Type() const noexcept {
    // Asset type identifier used by the AssetManager. This is a Material Instance
    // asset type, even though the display name remains "Material" for UI brevity.
    return "RenderMaterial";
}

std::type_index RenderMaterialAssetLoader::PayloadType() const noexcept {
    return typeid(RenderMaterialAssetData);
}

std::vector<std::string> RenderMaterialAssetLoader::Extensions() const {
    return { ".kbmat" };
}

kb::assets::AssetLoadResult RenderMaterialAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    RenderMaterialAssetParseResult material = LoadMaterialWithDiagnostics(request.resolvedPath, request.metadata.id);
    if (!material.asset.has_value()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = material.ErrorMessage() };
    }
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<RenderMaterialAssetData>(*material.asset),
        .error = {},
    };
}

std::optional<RenderMaterialAssetData> RenderMaterialAssetLoader::LoadMaterial(const std::filesystem::path& path) {
    return RenderMaterialAssetParser::Load(path);
}

std::optional<RenderMaterialAssetData> RenderMaterialAssetLoader::LoadMaterial(std::istream& input) {
    return RenderMaterialAssetParser::Parse(input);
}

RenderMaterialAssetParseResult RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(const std::filesystem::path& path) {
    return RenderMaterialAssetParser::LoadWithDiagnostics(path);
}

RenderMaterialAssetParseResult RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(const std::filesystem::path& path, kb::assets::AssetId assetId) {
    return RenderMaterialAssetParser::LoadWithDiagnostics(path, assetId);
}

RenderMaterialAssetParseResult RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(std::istream& input) {
    return RenderMaterialAssetParser::ParseWithDiagnostics(input);
}

} // namespace kb::render
