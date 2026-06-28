#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "resources/RenderMaterialAssetParser.hpp"

#include <charconv>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace kb::render {
namespace {

void AppendUnique(std::vector<kb::assets::AssetId>& dependencies, kb::assets::AssetId id) {
    if (!id.IsValid()) {
        return;
    }
    for (const kb::assets::AssetId existing : dependencies) {
        if (existing == id) {
            return;
        }
    }
    dependencies.push_back(id);
}

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

[[nodiscard]] bool HasMaterialParseError(const RenderMaterialAssetParseResult& result) noexcept {
    for (const RenderMaterialAssetParseDiagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.severity == RenderMaterialAssetParseDiagnosticSeverity::Error) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::optional<RenderMaterialParameterType> ParameterTypeForGraphNode(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::ParameterScalar:
        return RenderMaterialParameterType::Scalar;
    case RenderMaterialGraphNodeKind::ParameterVector:
        return RenderMaterialParameterType::Vec4;
    case RenderMaterialGraphNodeKind::ParameterColor:
        return RenderMaterialParameterType::Color;
    case RenderMaterialGraphNodeKind::ParameterTexture:
    case RenderMaterialGraphNodeKind::TextureSample:
        return RenderMaterialParameterType::Texture;
    case RenderMaterialGraphNodeKind::MaterialOutput:
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::Add:
    case RenderMaterialGraphNodeKind::Subtract:
    case RenderMaterialGraphNodeKind::Multiply:
    case RenderMaterialGraphNodeKind::Divide:
    case RenderMaterialGraphNodeKind::Power:
    case RenderMaterialGraphNodeKind::OneMinus:
    case RenderMaterialGraphNodeKind::Absolute:
    case RenderMaterialGraphNodeKind::Minimum:
    case RenderMaterialGraphNodeKind::Maximum:
    case RenderMaterialGraphNodeKind::Saturate:
    case RenderMaterialGraphNodeKind::Floor:
    case RenderMaterialGraphNodeKind::Ceil:
    case RenderMaterialGraphNodeKind::Fraction:
    case RenderMaterialGraphNodeKind::SquareRoot:
    case RenderMaterialGraphNodeKind::Sine:
    case RenderMaterialGraphNodeKind::Cosine:
    case RenderMaterialGraphNodeKind::DotProduct:
    case RenderMaterialGraphNodeKind::CrossProduct:
    case RenderMaterialGraphNodeKind::Normalize:
    case RenderMaterialGraphNodeKind::Length:
    case RenderMaterialGraphNodeKind::Distance:
    case RenderMaterialGraphNodeKind::BreakVector:
    case RenderMaterialGraphNodeKind::MakeVector:
    case RenderMaterialGraphNodeKind::Step:
    case RenderMaterialGraphNodeKind::SmoothStep:
    case RenderMaterialGraphNodeKind::If:
    case RenderMaterialGraphNodeKind::Desaturate:
    case RenderMaterialGraphNodeKind::Fresnel:
    case RenderMaterialGraphNodeKind::Negate:
    case RenderMaterialGraphNodeKind::Sign:
    case RenderMaterialGraphNodeKind::Round:
    case RenderMaterialGraphNodeKind::Truncate:
    case RenderMaterialGraphNodeKind::Tangent:
    case RenderMaterialGraphNodeKind::ArcSine:
    case RenderMaterialGraphNodeKind::ArcCosine:
    case RenderMaterialGraphNodeKind::ArcTangent:
    case RenderMaterialGraphNodeKind::ArcTangent2:
    case RenderMaterialGraphNodeKind::Clamp:
    case RenderMaterialGraphNodeKind::Lerp:
    case RenderMaterialGraphNodeKind::NormalUnpack:
    case RenderMaterialGraphNodeKind::Uv:
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] std::string StableParameterIdForGraphNode(const RenderMaterialGraphNode& node) {
    if (!node.parameter.stableId.empty()) {
        return node.parameter.stableId;
    }
    switch (node.kind) {
    case RenderMaterialGraphNodeKind::ParameterScalar:
        return "scalar" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::ParameterVector:
        return "vector" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::ParameterColor:
        return "color" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::ParameterTexture:
        return "texture" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::TextureSample:
        return "textureSample" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::MaterialOutput:
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::Add:
    case RenderMaterialGraphNodeKind::Subtract:
    case RenderMaterialGraphNodeKind::Multiply:
    case RenderMaterialGraphNodeKind::Divide:
    case RenderMaterialGraphNodeKind::Power:
    case RenderMaterialGraphNodeKind::OneMinus:
    case RenderMaterialGraphNodeKind::Absolute:
    case RenderMaterialGraphNodeKind::Minimum:
    case RenderMaterialGraphNodeKind::Maximum:
    case RenderMaterialGraphNodeKind::Saturate:
    case RenderMaterialGraphNodeKind::Floor:
    case RenderMaterialGraphNodeKind::Ceil:
    case RenderMaterialGraphNodeKind::Fraction:
    case RenderMaterialGraphNodeKind::SquareRoot:
    case RenderMaterialGraphNodeKind::Sine:
    case RenderMaterialGraphNodeKind::Cosine:
    case RenderMaterialGraphNodeKind::DotProduct:
    case RenderMaterialGraphNodeKind::CrossProduct:
    case RenderMaterialGraphNodeKind::Normalize:
    case RenderMaterialGraphNodeKind::Length:
    case RenderMaterialGraphNodeKind::Distance:
    case RenderMaterialGraphNodeKind::BreakVector:
    case RenderMaterialGraphNodeKind::MakeVector:
    case RenderMaterialGraphNodeKind::Step:
    case RenderMaterialGraphNodeKind::SmoothStep:
    case RenderMaterialGraphNodeKind::If:
    case RenderMaterialGraphNodeKind::Desaturate:
    case RenderMaterialGraphNodeKind::Fresnel:
    case RenderMaterialGraphNodeKind::Negate:
    case RenderMaterialGraphNodeKind::Sign:
    case RenderMaterialGraphNodeKind::Round:
    case RenderMaterialGraphNodeKind::Truncate:
    case RenderMaterialGraphNodeKind::Tangent:
    case RenderMaterialGraphNodeKind::ArcSine:
    case RenderMaterialGraphNodeKind::ArcCosine:
    case RenderMaterialGraphNodeKind::ArcTangent:
    case RenderMaterialGraphNodeKind::ArcTangent2:
    case RenderMaterialGraphNodeKind::Clamp:
    case RenderMaterialGraphNodeKind::Lerp:
    case RenderMaterialGraphNodeKind::NormalUnpack:
    case RenderMaterialGraphNodeKind::Uv:
        break;
    }
    return "parameter" + std::to_string(node.id);
}

[[nodiscard]] std::optional<RenderMaterialParameterType> FindParentGraphParameterType(
    const RenderMaterialAssetData& parentMaterial,
    std::string_view stableId) {
    for (const RenderMaterialGraphParameterValue& value : parentMaterial.graphParameterValues) {
        if (value.stableId == stableId) {
            return value.type;
        }
    }
    for (const RenderMaterialGraphNode& node : parentMaterial.graph.nodes) {
        if (StableParameterIdForGraphNode(node) != stableId) {
            continue;
        }
        if (const std::optional<RenderMaterialParameterType> type = ParameterTypeForGraphNode(node.kind);
            type.has_value()) {
            return type;
        }
    }
    return std::nullopt;
}

void AppendOverrideDiagnostics(
    RenderMaterialInstanceAssetParseResult& result,
    const RenderMaterialAssetParseResult& materialResult) {
    for (const RenderMaterialAssetParseDiagnostic& diagnostic : materialResult.diagnostics) {
        if (diagnostic.severity != RenderMaterialAssetParseDiagnosticSeverity::Error) {
            continue;
        }
        std::string message{ "Invalid material instance override: " };
        message += std::string{ RenderMaterialAssetParseDiagnosticCodeName(diagnostic.code) };
        if (!diagnostic.message.empty()) {
            message += ": ";
            message += diagnostic.message;
        }
        AddDiagnostic(
            result,
            RenderMaterialInstanceAssetParseDiagnosticCode::InvalidOverrideMaterial,
            diagnostic.line,
            diagnostic.field,
            std::move(message),
            diagnostic.text);
    }
}

[[nodiscard]] RenderMaterialInstanceAssetParseResult Parse(std::istream& input) {
    RenderMaterialInstanceAssetParseResult result;
    RenderMaterialInstanceAssetData asset{};
    bool sawContent = false;
    bool sawOverrideContent = false;
    std::ostringstream overrideDocument;

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
            sawOverrideContent = true;
            overrideDocument << text << '\n';
        }
    }

    if (!sawContent) {
        AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::EmptyDocument, 0U, {}, "Material instance document is empty.");
    }
    if (!asset.parentMaterialAssetId.IsValid()) {
        AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::MissingParentMaterial, 0U, "parentMaterialAssetId", "Material instance is missing a parent material asset reference.");
    }
    if (sawOverrideContent) {
        std::istringstream overrideInput{ overrideDocument.str() };
        const RenderMaterialAssetParseResult overrides = RenderMaterialAssetParser::ParseWithDiagnostics(overrideInput);
        if (!overrides.asset.has_value() || HasMaterialParseError(overrides)) {
            AppendOverrideDiagnostics(result, overrides);
            if (result.diagnostics.empty()) {
                AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::InvalidOverrideMaterial, 0U, {}, "Material instance override document is invalid.");
            }
        } else {
            asset.overrides = *overrides.asset;
            asset.hasOverrides = true;
        }
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
    case RenderMaterialInstanceAssetParseDiagnosticCode::InvalidOverrideMaterial:
        return "invalid_override_material";
    }
    return "unknown_diagnostic";
}

std::string_view RenderMaterialInstanceValidationDiagnosticCodeName(RenderMaterialInstanceValidationDiagnosticCode code) noexcept {
    switch (code) {
    case RenderMaterialInstanceValidationDiagnosticCode::IncompatibleMaterialType:
        return "incompatible_material_type";
    case RenderMaterialInstanceValidationDiagnosticCode::IncompatibleMaterialTypeVersion:
        return "incompatible_material_type_version";
    case RenderMaterialInstanceValidationDiagnosticCode::UnknownOverrideParameter:
        return "unknown_override_parameter";
    case RenderMaterialInstanceValidationDiagnosticCode::IncompatibleOverrideParameterType:
        return "incompatible_override_parameter_type";
    }
    return "unknown_diagnostic";
}

bool RenderMaterialInstanceValidationResult::Succeeded() const noexcept {
    return diagnostics.empty();
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

std::vector<kb::assets::AssetId> RenderMaterialInstanceAssetLoader::DiscoverDependencies(
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetRegistry& registry) const {
    const RenderMaterialInstanceAssetParseResult instance = LoadInstanceWithDiagnostics(metadata.physicalPath, metadata.id);
    if (!instance.asset.has_value()) {
        return {};
    }

    std::vector<kb::assets::AssetId> dependencies;
    dependencies.reserve(16U);
    AppendUnique(dependencies, instance.asset->parentMaterialAssetId);
    if (instance.asset->hasOverrides) {
        std::vector<kb::assets::AssetId> overrideDependencies =
            RenderMaterialAssetLoader::DiscoverMaterialDependencies(instance.asset->overrides, metadata, registry);
        for (const kb::assets::AssetId dependency : overrideDependencies) {
            AppendUnique(dependencies, dependency);
        }
    }
    return dependencies;
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

RenderMaterialInstanceValidationResult RenderMaterialInstanceAssetLoader::ValidateAgainstParent(
    const RenderMaterialInstanceAssetData& instance,
    const RenderMaterialAssetData& parentMaterial) {
    RenderMaterialInstanceValidationResult result{};
    if (!instance.hasOverrides) {
        return result;
    }
    if (instance.overrides.materialType != parentMaterial.materialType) {
        result.diagnostics.push_back(RenderMaterialInstanceValidationDiagnostic{
            .code = RenderMaterialInstanceValidationDiagnosticCode::IncompatibleMaterialType,
            .message = "Material instance override type '" + instance.overrides.materialType + "' does not match parent material type '" + parentMaterial.materialType + "'.",
        });
    }
    if (instance.overrides.materialTypeVersion != parentMaterial.materialTypeVersion) {
        result.diagnostics.push_back(RenderMaterialInstanceValidationDiagnostic{
            .code = RenderMaterialInstanceValidationDiagnosticCode::IncompatibleMaterialTypeVersion,
            .message = "Material instance override type version " + std::to_string(instance.overrides.materialTypeVersion) +
                " does not match parent material type version " + std::to_string(parentMaterial.materialTypeVersion) + ".",
        });
    }
    for (const RenderMaterialGraphParameterValue& overrideValue : instance.overrides.graphParameterValues) {
        const std::optional<RenderMaterialParameterType> parentType =
            FindParentGraphParameterType(parentMaterial, overrideValue.stableId);
        if (!parentType.has_value()) {
            result.diagnostics.push_back(RenderMaterialInstanceValidationDiagnostic{
                .code = RenderMaterialInstanceValidationDiagnosticCode::UnknownOverrideParameter,
                .message = "Material instance override parameter '" + overrideValue.stableId + "' is not exposed by its parent material.",
            });
            continue;
        }
        if (*parentType != overrideValue.type) {
            result.diagnostics.push_back(RenderMaterialInstanceValidationDiagnostic{
                .code = RenderMaterialInstanceValidationDiagnosticCode::IncompatibleOverrideParameterType,
                .message = "Material instance override parameter '" + overrideValue.stableId + "' has a type that does not match its parent material parameter.",
            });
        }
    }
    return result;
}

} // namespace kb::render
