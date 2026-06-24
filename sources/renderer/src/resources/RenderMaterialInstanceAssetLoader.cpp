#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"

#include <charconv>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace kb::render {
namespace {

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1U);
    }
    return text;
}

[[nodiscard]] std::optional<std::uint64_t> ParseUInt64(std::string_view text) noexcept {
    text = Trim(text);
    std::uint64_t value = 0U;
    const char* first = text.data();
    const char* last = first + text.size();
    const std::from_chars_result result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) {
        return std::nullopt;
    }
    return value;
}

void AddDiagnostic(
    RenderMaterialInstanceAssetParseResult& result,
    RenderMaterialInstanceAssetParseDiagnosticCode code,
    std::size_t line,
    std::string field,
    std::string message,
    std::string text = {}) {
    result.diagnostics.push_back(RenderMaterialInstanceAssetParseDiagnostic{
        .code = code,
        .line = line,
        .field = std::move(field),
        .message = std::move(message),
        .text = std::move(text),
    });
}

[[nodiscard]] RenderMaterialInstanceAssetParseResult Parse(std::istream& input) {
    RenderMaterialInstanceAssetParseResult result;
    RenderMaterialInstanceAssetData asset{};
    bool sawContent = false;

    std::string line;
    std::size_t lineNumber = 0U;
    while (std::getline(input, line)) {
        ++lineNumber;
        std::string_view text = Trim(line);
        if (text.empty() || text.front() == '#') {
            continue;
        }
        sawContent = true;

        const std::size_t split = text.find_first_of(" \t");
        const std::string_view field = split == std::string_view::npos ? text : text.substr(0U, split);
        const std::string_view value = split == std::string_view::npos ? std::string_view{} : Trim(text.substr(split + 1U));
        if (field == "version") {
            const std::optional<std::uint64_t> version = ParseUInt64(value);
            if (!version.has_value()) {
                AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::InvalidDocumentVersion, lineNumber, "version", "Material instance version must be an unsigned integer.", std::string{ value });
                continue;
            }
            if (*version != kRenderMaterialInstanceAssetDocumentVersion) {
                AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::UnsupportedDocumentVersion, lineNumber, "version", "Material instance document version is not supported.", std::string{ value });
                continue;
            }
            asset.documentVersion = static_cast<std::uint32_t>(*version);
            asset.hasExplicitDocumentVersion = true;
        } else if (field == "parentMaterialAssetId") {
            const std::optional<std::uint64_t> id = ParseUInt64(value);
            if (!id.has_value()) {
                AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::InvalidFieldValue, lineNumber, "parentMaterialAssetId", "Parent material asset id must be an unsigned integer.", std::string{ value });
                continue;
            }
            asset.parentMaterialAssetId = kb::assets::AssetId{ *id };
        } else {
            AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::UnknownField, lineNumber, std::string{ field }, "Unknown material instance field.", std::string{ text });
        }
    }

    if (!sawContent) {
        AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::EmptyDocument, 0U, {}, "Material instance document is empty.");
    }
    if (!asset.parentMaterialAssetId.IsValid()) {
        AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::MissingParentMaterial, 0U, "parentMaterialAssetId", "Material instance is missing a parent material asset reference.");
    }
    if (result.diagnostics.empty()) {
        result.asset = std::move(asset);
    }
    return result;
}

} // namespace

std::string_view RenderMaterialInstanceAssetParseDiagnosticCodeName(RenderMaterialInstanceAssetParseDiagnosticCode code) noexcept {
    switch (code) {
    case RenderMaterialInstanceAssetParseDiagnosticCode::FileOpenFailed:
        return "file_open_failed";
    case RenderMaterialInstanceAssetParseDiagnosticCode::EmptyDocument:
        return "empty_document";
    case RenderMaterialInstanceAssetParseDiagnosticCode::UnknownField:
        return "unknown_field";
    case RenderMaterialInstanceAssetParseDiagnosticCode::InvalidFieldValue:
        return "invalid_field_value";
    case RenderMaterialInstanceAssetParseDiagnosticCode::InvalidDocumentVersion:
        return "invalid_document_version";
    case RenderMaterialInstanceAssetParseDiagnosticCode::UnsupportedDocumentVersion:
        return "unsupported_document_version";
    case RenderMaterialInstanceAssetParseDiagnosticCode::MissingParentMaterial:
        return "missing_parent_material";
    }
    return "unknown_diagnostic";
}

bool RenderMaterialInstanceAssetParseResult::Succeeded() const noexcept {
    return asset.has_value() && diagnostics.empty();
}

std::string RenderMaterialInstanceAssetParseResult::ErrorMessage() const {
    if (diagnostics.empty()) {
        return {};
    }
    std::ostringstream output;
    output << "Render material instance asset load failed";
    for (const RenderMaterialInstanceAssetParseDiagnostic& diagnostic : diagnostics) {
        output << "; code " << RenderMaterialInstanceAssetParseDiagnosticCodeName(diagnostic.code);
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

std::string_view RenderMaterialInstanceAssetLoader::Type() const noexcept {
    return "RenderMaterialInstance";
}

std::type_index RenderMaterialInstanceAssetLoader::PayloadType() const noexcept {
    return typeid(RenderMaterialInstanceAssetData);
}

std::vector<std::string> RenderMaterialInstanceAssetLoader::Extensions() const {
    return { ".kbmatinst" };
}

kb::assets::AssetLoadResult RenderMaterialInstanceAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    RenderMaterialInstanceAssetParseResult material = LoadInstanceWithDiagnostics(request.resolvedPath, request.metadata.id);
    if (!material.asset.has_value()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = material.ErrorMessage() };
    }
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<RenderMaterialInstanceAssetData>(*material.asset),
        .error = {},
    };
}

std::optional<RenderMaterialInstanceAssetData> RenderMaterialInstanceAssetLoader::LoadInstance(const std::filesystem::path& path) {
    RenderMaterialInstanceAssetParseResult result = LoadInstanceWithDiagnostics(path);
    return result.asset;
}

std::optional<RenderMaterialInstanceAssetData> RenderMaterialInstanceAssetLoader::LoadInstance(std::istream& input) {
    RenderMaterialInstanceAssetParseResult result = LoadInstanceWithDiagnostics(input);
    return result.asset;
}

RenderMaterialInstanceAssetParseResult RenderMaterialInstanceAssetLoader::LoadInstanceWithDiagnostics(const std::filesystem::path& path) {
    return LoadInstanceWithDiagnostics(path, {});
}

RenderMaterialInstanceAssetParseResult RenderMaterialInstanceAssetLoader::LoadInstanceWithDiagnostics(const std::filesystem::path& path, kb::assets::AssetId assetId) {
    std::ifstream input{ path, std::ios::binary };
    if (!input) {
        RenderMaterialInstanceAssetParseResult result;
        result.diagnostics.push_back(RenderMaterialInstanceAssetParseDiagnostic{
            .code = RenderMaterialInstanceAssetParseDiagnosticCode::FileOpenFailed,
            .assetId = assetId,
            .path = path,
            .message = "Material instance file could not be opened.",
        });
        return result;
    }
    RenderMaterialInstanceAssetParseResult result = Parse(input);
    for (RenderMaterialInstanceAssetParseDiagnostic& diagnostic : result.diagnostics) {
        diagnostic.assetId = assetId;
        diagnostic.path = path;
    }
    return result;
}

RenderMaterialInstanceAssetParseResult RenderMaterialInstanceAssetLoader::LoadInstanceWithDiagnostics(std::istream& input) {
    return Parse(input);
}

} // namespace kb::render
