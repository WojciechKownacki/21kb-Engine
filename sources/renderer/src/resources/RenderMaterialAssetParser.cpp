#include "resources/RenderMaterialAssetParser.hpp"

#include "resources/RenderMaterialAssetFieldParser.hpp"
#include "resources/RenderMaterialGraphFieldParser.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialTypeSchema.hpp"

#include <cmath>
#include <cstddef>
#include <charconv>
#include <fstream>
#include <istream>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

[[nodiscard]] std::string_view StripComment(std::string_view line) noexcept {
    const std::size_t comment = line.find('#');
    return comment == std::string_view::npos ? line : line.substr(0U, comment);
}

[[nodiscard]] bool ParseUInt32(std::string_view text, std::uint32_t& output) noexcept {
    text = Trim(text);
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, output);
    return result.ec == std::errc{} && result.ptr == end;
}

[[nodiscard]] bool ParseUInt64(std::string_view text, std::uint64_t& output) noexcept {
    text = Trim(text);
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, output);
    return result.ec == std::errc{} && result.ptr == end;
}

[[nodiscard]] bool ParseFloat(std::string_view text, float& output) noexcept {
    text = Trim(text);
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, output);
    return result.ec == std::errc{} && result.ptr == end;
}

[[nodiscard]] bool ParseBoolToken(std::string_view text, bool& output) noexcept {
    text = Trim(text);
    if (text == "true" || text == "1" || text == "yes") {
        output = true;
        return true;
    }
    if (text == "false" || text == "0" || text == "no") {
        output = false;
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseMaterialParameterType(std::string_view text, RenderMaterialParameterType& output) noexcept {
    if (text == "Scalar") output = RenderMaterialParameterType::Scalar;
    else if (text == "Vec3") output = RenderMaterialParameterType::Vec3;
    else if (text == "Vec4") output = RenderMaterialParameterType::Vec4;
    else if (text == "Color") output = RenderMaterialParameterType::Color;
    else if (text == "Enum") output = RenderMaterialParameterType::Enum;
    else if (text == "Bool") output = RenderMaterialParameterType::Bool;
    else if (text == "Texture") output = RenderMaterialParameterType::Texture;
    else return false;
    return true;
}

[[nodiscard]] bool ParseGraphParameterValue(std::string_view rest, RenderMaterialGraphParameterValue& output) {
    std::istringstream stream{ std::string{ rest } };
    std::string stableId;
    std::string typeName;
    if (!(stream >> stableId >> typeName) || stableId.empty()) {
        return false;
    }
    RenderMaterialParameterType type = RenderMaterialParameterType::Scalar;
    if (!ParseMaterialParameterType(typeName, type)) {
        return false;
    }

    output = RenderMaterialGraphParameterValue{
        .stableId = std::move(stableId),
        .type = type,
    };
    std::string token;
    switch (type) {
    case RenderMaterialParameterType::Scalar:
        return (stream >> token) && ParseFloat(token, output.numbers[0]);
    case RenderMaterialParameterType::Vec3:
        for (std::size_t index = 0U; index < 3U; ++index) {
            if (!(stream >> token) || !ParseFloat(token, output.numbers[index])) {
                return false;
            }
        }
        return true;
    case RenderMaterialParameterType::Vec4:
    case RenderMaterialParameterType::Color:
        for (std::size_t index = 0U; index < 4U; ++index) {
            if (!(stream >> token) || !ParseFloat(token, output.numbers[index])) {
                return false;
            }
        }
        return true;
    case RenderMaterialParameterType::Bool:
        return (stream >> token) && ParseBoolToken(token, output.boolValue);
    case RenderMaterialParameterType::Enum:
        if (!(stream >> output.text)) {
            return false;
        }
        if (output.text == "_") {
            output.text.clear();
        }
        return true;
    case RenderMaterialParameterType::Texture:
        return (stream >> token) && ParseUInt64(token, output.assetId);
    }
    return false;
}

[[nodiscard]] bool IsSupportedMaterialType(std::string_view text) noexcept {
    return text == kRenderMaterialAssetBuiltInPbrType;
}

[[nodiscard]] std::uint32_t SupportedMaterialTypeVersion(std::string_view text) noexcept {
    return text == kRenderMaterialAssetBuiltInPbrType ? kRenderMaterialAssetBuiltInPbrTypeVersion : 0U;
}

void AttachContext(RenderMaterialAssetParseResult& result, const std::filesystem::path& path, kb::assets::AssetId assetId) {
    for (RenderMaterialAssetParseDiagnostic& diagnostic : result.diagnostics) {
        diagnostic.path = path;
        diagnostic.assetId = assetId;
    }
}

void AttachContext(RenderMaterialAssetParseResult& result, const RenderMaterialAssetParseSourceContext& sourceContext) {
    AttachContext(result, sourceContext.path, sourceContext.assetId);
}

[[nodiscard]] bool HasError(const std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) noexcept {
    for (const RenderMaterialAssetParseDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == RenderMaterialAssetParseDiagnosticSeverity::Error) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool IsEnumMaterialField(std::string_view keyword) noexcept {
    return keyword == "alphaMode" ||
        keyword == "decalBlendMode" ||
        keyword == "layerBlendMode" ||
        keyword == "doubleSided";
}

[[nodiscard]] bool IsNumericMaterialField(std::string_view keyword) noexcept {
    return keyword == "baseColor" ||
        keyword == "baseColorFactor" ||
        keyword == "emissiveColor" ||
        keyword == "emissiveFactor" ||
        keyword == "metallicFactor" ||
        keyword == "roughnessFactor" ||
        keyword == "normalScale" ||
        keyword == "occlusionStrength" ||
        keyword == "emissiveStrength" ||
        keyword == "alphaCutoff" ||
        keyword == "clearcoatFactor" ||
        keyword == "clearcoatRoughnessFactor" ||
        keyword == "sheenColor" ||
        keyword == "sheenRoughnessFactor" ||
        keyword == "transmissionFactor" ||
        keyword == "thicknessFactor" ||
        keyword == "attenuationColor" ||
        keyword == "attenuationDistance" ||
        keyword == "subsurfaceColor" ||
        keyword == "subsurfaceFactor" ||
        keyword == "anisotropyStrength" ||
        keyword == "anisotropyRotation" ||
        keyword == "layerWeight";
}

[[nodiscard]] bool InRange(float value, float minimum, float maximum) noexcept {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

[[nodiscard]] bool Vec3InRange(const float* values, float minimum, float maximum) noexcept {
    return values != nullptr && InRange(values[0], minimum, maximum) && InRange(values[1], minimum, maximum) && InRange(values[2], minimum, maximum);
}

[[nodiscard]] bool BaseColorInRange(const RenderMaterialAssetData& asset) noexcept {
    return InRange(asset.desc.baseColor[0], 0.0F, 1.0F) &&
        InRange(asset.desc.baseColor[1], 0.0F, 1.0F) &&
        InRange(asset.desc.baseColor[2], 0.0F, 1.0F) &&
        InRange(asset.desc.baseColor[3], 0.0F, 1.0F);
}

[[nodiscard]] const float* ResolveVec3Field(std::string_view keyword, const RenderMaterialAssetData& asset) noexcept {
    if (keyword == "emissiveColor" || keyword == "emissiveFactor") return asset.desc.emissiveColor;
    if (keyword == "sheenColor") return asset.desc.sheenColor;
    if (keyword == "attenuationColor") return asset.desc.attenuationColor;
    if (keyword == "subsurfaceColor") return asset.desc.subsurfaceColor;
    return nullptr;
}

[[nodiscard]] float ResolveScalarField(std::string_view keyword, const RenderMaterialAssetData& asset) noexcept {
    if (keyword == "metallicFactor") return asset.desc.metallicFactor;
    if (keyword == "roughnessFactor") return asset.desc.roughnessFactor;
    if (keyword == "normalScale") return asset.desc.normalScale;
    if (keyword == "occlusionStrength") return asset.desc.occlusionStrength;
    if (keyword == "emissiveStrength") return asset.desc.emissiveStrength;
    if (keyword == "alphaCutoff") return asset.desc.alphaCutoff;
    if (keyword == "clearcoatFactor") return asset.desc.clearcoatFactor;
    if (keyword == "clearcoatRoughnessFactor") return asset.desc.clearcoatRoughnessFactor;
    if (keyword == "sheenRoughnessFactor") return asset.desc.sheenRoughnessFactor;
    if (keyword == "transmissionFactor") return asset.desc.transmissionFactor;
    if (keyword == "thicknessFactor") return asset.desc.thicknessFactor;
    if (keyword == "attenuationDistance") return asset.desc.attenuationDistance;
    if (keyword == "subsurfaceFactor") return asset.desc.subsurfaceFactor;
    if (keyword == "anisotropyStrength") return asset.desc.anisotropyStrength;
    if (keyword == "anisotropyRotation") return asset.desc.anisotropyRotation;
    if (keyword == "layerWeight") return asset.desc.layerWeight;
    return 0.0F;
}

[[nodiscard]] bool IsOutOfRangeMaterialField(std::string_view keyword, const RenderMaterialAssetData& asset) noexcept {
    const RenderMaterialTypeSchema& schema = GetBuiltInPbrMaterialTypeSchema();
    const RenderMaterialParameterSchema* param = FindMaterialParameterSchema(schema, keyword);
    if (!param || !param->range.has_value()) {
        return false;
    }
    const RenderMaterialParameterRange& range = param->range.value();
    if (keyword == "baseColor" || keyword == "baseColorFactor") {
        return !BaseColorInRange(asset);
    }
    if (const float* values = ResolveVec3Field(keyword, asset); values != nullptr) {
        return !Vec3InRange(values, range.min, range.max);
    }
    const float value = ResolveScalarField(keyword, asset);
    return !InRange(value, range.min, range.max);
}

[[nodiscard]] bool AnyNonZero(const float* values) noexcept {
    return values != nullptr && (values[0] != 0.0F || values[1] != 0.0F || values[2] != 0.0F);
}

[[nodiscard]] bool AnyNotOne(const float* values) noexcept {
    return values != nullptr && (values[0] != 1.0F || values[1] != 1.0F || values[2] != 1.0F);
}

[[nodiscard]] bool IsActiveUnsupportedAdvancedField(std::string_view keyword, const RenderMaterialAssetData& asset) noexcept {
    const RenderMaterialTypeSchema& schema = GetBuiltInPbrMaterialTypeSchema();
    const RenderMaterialParameterSchema* param = FindMaterialParameterSchema(schema, keyword);
    if (param && param->runtimeSupport == RenderMaterialFeatureSupport::ParsedButIgnored) {
        if (keyword == "baseColor" || keyword == "baseColorFactor") {
            return false; // Core parameter, never unsupported
        }
        if (const float* values = ResolveVec3Field(keyword, asset); values != nullptr) {
            if (keyword == "attenuationColor" || keyword == "subsurfaceColor") {
                return AnyNotOne(values);
            }
            return AnyNonZero(values);
        }
        if (keyword == "decalBlendMode") {
            return asset.desc.decalBlendMode != RenderMaterialDecalBlendMode::Disabled;
        }
        if (keyword == "layerBlendMode") {
            return asset.desc.layerBlendMode != RenderMaterialLayerBlendMode::Replace;
        }
        const float value = ResolveScalarField(keyword, asset);
        if (keyword == "anisotropyRotation") {
            return value != 0.0F;
        }
        if (keyword == "layerWeight") {
            return value != 1.0F;
        }
        return value != 0.0F;
    }

    // Check texture slots for unsupported advanced fields
    const RenderMaterialTextureSlotSchema* slot = FindMaterialTextureSlotSchema(schema, keyword);
    if (slot && slot->runtimeSupport == RenderMaterialFeatureSupport::ParsedButIgnored) {
        if (keyword == "clearcoatTextureAssetId") return asset.desc.clearcoatTextureAssetId != 0U;
        if (keyword == "clearcoatRoughnessTextureAssetId") return asset.desc.clearcoatRoughnessTextureAssetId != 0U;
        if (keyword == "sheenColorTextureAssetId") return asset.desc.sheenColorTextureAssetId != 0U;
        if (keyword == "transmissionTextureAssetId") return asset.desc.transmissionTextureAssetId != 0U;
        if (keyword == "thicknessTextureAssetId") return asset.desc.thicknessTextureAssetId != 0U;
        if (keyword == "anisotropyTextureAssetId") return asset.desc.anisotropyTextureAssetId != 0U;
        if (keyword == "decalTextureAssetId") return asset.desc.decalTextureAssetId != 0U;
        if (keyword == "layerMaskTextureAssetId") return asset.desc.layerMaskTextureAssetId != 0U;
    }

    // Check texture paths for unsupported advanced fields
    if (IsMaterialTexturePathField(schema, keyword)) {
        const RenderMaterialTextureSlotSchema* pathSlot = nullptr;
        for (const auto& s : schema.textureSlots) {
            if (s.pathFieldName == keyword) {
                pathSlot = &s;
                break;
            }
        }
        if (pathSlot && pathSlot->runtimeSupport == RenderMaterialFeatureSupport::ParsedButIgnored) {
            if (keyword == "clearcoatTexture") return !asset.clearcoatTexturePath.empty();
            if (keyword == "clearcoatRoughnessTexture") return !asset.clearcoatRoughnessTexturePath.empty();
            if (keyword == "sheenColorTexture") return !asset.sheenColorTexturePath.empty();
            if (keyword == "transmissionTexture") return !asset.transmissionTexturePath.empty();
            if (keyword == "thicknessTexture") return !asset.thicknessTexturePath.empty();
            if (keyword == "anisotropyTexture") return !asset.anisotropyTexturePath.empty();
            if (keyword == "decalTexture") return !asset.decalTexturePath.empty();
            if (keyword == "layerMaskTexture") return !asset.layerMaskTexturePath.empty();
        }
    }

    return false;
}

[[nodiscard]] bool IsTextureColorSpaceExpectationField(std::string_view keyword, const RenderMaterialAssetData& asset) noexcept {
    const RenderMaterialTypeSchema& schema = GetBuiltInPbrMaterialTypeSchema();

    // Map assetId field names to path field names for lookup
    std::string_view pathFieldName = keyword;
    if (keyword == "albedoTextureAssetId" || keyword == "baseColorTextureAssetId") pathFieldName = "albedoTexture";
    else if (keyword == "normalTextureAssetId") pathFieldName = "normalTexture";
    else if (keyword == "metallicRoughnessTextureAssetId") pathFieldName = "metallicRoughnessTexture";
    else if (keyword == "occlusionTextureAssetId") pathFieldName = "occlusionTexture";
    else if (keyword == "emissiveTextureAssetId") pathFieldName = "emissiveTexture";
    else if (keyword == "clearcoatTextureAssetId") pathFieldName = "clearcoatTexture";
    else if (keyword == "clearcoatRoughnessTextureAssetId") pathFieldName = "clearcoatRoughnessTexture";
    else if (keyword == "sheenColorTextureAssetId") pathFieldName = "sheenColorTexture";
    else if (keyword == "transmissionTextureAssetId") pathFieldName = "transmissionTexture";
    else if (keyword == "thicknessTextureAssetId") pathFieldName = "thicknessTexture";
    else if (keyword == "anisotropyTextureAssetId") pathFieldName = "anisotropyTexture";
    else if (keyword == "decalTextureAssetId") pathFieldName = "decalTexture";
    else if (keyword == "layerMaskTextureAssetId") pathFieldName = "layerMaskTexture";

    const RenderMaterialTextureSlotSchema* slot = nullptr;
    for (const auto& s : schema.textureSlots) {
        if (s.pathFieldName == pathFieldName) {
            slot = &s;
            break;
        }
    }
    if (!slot) {
        return false;
    }

    // Only validate supported texture slots (not advanced/ignored ones)
    if (slot->runtimeSupport != RenderMaterialFeatureSupport::Supported) {
        return false;
    }
    if (slot->expectedColorSpace == RenderMaterialTextureColorSpace::Unknown) {
        return false;
    }

    // Check if a texture is actually assigned (by assetId or path)
    bool hasTexture = false;
    if (keyword == "albedoTextureAssetId" || keyword == "baseColorTextureAssetId") {
        hasTexture = asset.desc.albedoTextureAssetId != 0U;
    } else if (keyword == "normalTextureAssetId") {
        hasTexture = asset.desc.normalTextureAssetId != 0U;
    } else if (keyword == "metallicRoughnessTextureAssetId") {
        hasTexture = asset.desc.metallicRoughnessTextureAssetId != 0U;
    } else if (keyword == "occlusionTextureAssetId") {
        hasTexture = asset.desc.occlusionTextureAssetId != 0U;
    } else if (keyword == "emissiveTextureAssetId") {
        hasTexture = asset.desc.emissiveTextureAssetId != 0U;
    } else if (keyword == "clearcoatTextureAssetId") {
        hasTexture = asset.desc.clearcoatTextureAssetId != 0U;
    } else if (keyword == "clearcoatRoughnessTextureAssetId") {
        hasTexture = asset.desc.clearcoatRoughnessTextureAssetId != 0U;
    } else if (keyword == "sheenColorTextureAssetId") {
        hasTexture = asset.desc.sheenColorTextureAssetId != 0U;
    } else if (keyword == "transmissionTextureAssetId") {
        hasTexture = asset.desc.transmissionTextureAssetId != 0U;
    } else if (keyword == "thicknessTextureAssetId") {
        hasTexture = asset.desc.thicknessTextureAssetId != 0U;
    } else if (keyword == "anisotropyTextureAssetId") {
        hasTexture = asset.desc.anisotropyTextureAssetId != 0U;
    } else if (keyword == "decalTextureAssetId") {
        hasTexture = asset.desc.decalTextureAssetId != 0U;
    } else if (keyword == "layerMaskTextureAssetId") {
        hasTexture = asset.desc.layerMaskTextureAssetId != 0U;
    }

    if (!hasTexture) {
        // Also check path fields
        if (pathFieldName == "albedoTexture") hasTexture = !asset.albedoTexturePath.empty();
        else if (pathFieldName == "normalTexture") hasTexture = !asset.normalTexturePath.empty();
        else if (pathFieldName == "metallicRoughnessTexture") hasTexture = !asset.metallicRoughnessTexturePath.empty();
        else if (pathFieldName == "occlusionTexture") hasTexture = !asset.occlusionTexturePath.empty();
        else if (pathFieldName == "emissiveTexture") hasTexture = !asset.emissiveTexturePath.empty();
        else if (pathFieldName == "clearcoatTexture") hasTexture = !asset.clearcoatTexturePath.empty();
        else if (pathFieldName == "clearcoatRoughnessTexture") hasTexture = !asset.clearcoatRoughnessTexturePath.empty();
        else if (pathFieldName == "sheenColorTexture") hasTexture = !asset.sheenColorTexturePath.empty();
        else if (pathFieldName == "transmissionTexture") hasTexture = !asset.transmissionTexturePath.empty();
        else if (pathFieldName == "thicknessTexture") hasTexture = !asset.thicknessTexturePath.empty();
        else if (pathFieldName == "anisotropyTexture") hasTexture = !asset.anisotropyTexturePath.empty();
        else if (pathFieldName == "decalTexture") hasTexture = !asset.decalTexturePath.empty();
        else if (pathFieldName == "layerMaskTexture") hasTexture = !asset.layerMaskTexturePath.empty();
    }

    // We cannot validate actual texture asset color space at parse time (no asset DB access).
    // Instead, we validate that the slot has a well-defined expected color space and the
    // texture reference is present. The runtime/editor validates actual texture metadata.
    // For now, we report a diagnostic that the slot expects a specific color space so the
    // consumer (editor, runtime) knows to validate it.
    return hasTexture;
}

} // namespace

std::optional<RenderMaterialAssetData> RenderMaterialAssetParser::Load(const std::filesystem::path& path) {
    return LoadWithDiagnostics(path).asset;
}

std::optional<RenderMaterialAssetData> RenderMaterialAssetParser::Parse(std::istream& input) {
    return ParseWithDiagnostics(input).asset;
}

RenderMaterialAssetParseResult RenderMaterialAssetParser::LoadWithDiagnostics(const std::filesystem::path& path) {
    return LoadWithDiagnostics(path, {});
}

RenderMaterialAssetParseResult RenderMaterialAssetParser::LoadWithDiagnostics(const std::filesystem::path& path, kb::assets::AssetId assetId) {
    std::ifstream input{ path };
    if (!input) {
        return RenderMaterialAssetParseResult{
            .asset = std::nullopt,
            .diagnostics = {
                RenderMaterialAssetParseDiagnostic{
                    .code = RenderMaterialAssetParseDiagnosticCode::FileOpenFailed,
                    .line = 0U,
                    .assetId = assetId,
                    .path = path,
                    .field = {},
                    .message = "Material file could not be opened: " + path.generic_string(),
                    .text = {},
                },
            },
        };
    }
    return ParseWithDiagnostics(input, RenderMaterialAssetParseSourceContext{
        .assetId = assetId,
        .path = path,
    });
}

RenderMaterialAssetParseResult RenderMaterialAssetParser::ParseWithDiagnostics(std::istream& input) {
    RenderMaterialAssetData asset{};
    std::vector<RenderMaterialAssetParseDiagnostic> diagnostics;
    bool sawMaterialProperty = false;
    std::size_t graphLastGoodArtifactLine = 0U;
    std::size_t materialTypeLine = 0U;
    std::size_t materialTypeVersionLine = 0U;

    std::string line;
    std::size_t lineNumber = 0U;
    while (std::getline(input, line)) {
        ++lineNumber;
        std::string_view trimmed = Trim(StripComment(line));
        if (trimmed.empty()) {
            continue;
        }

        const std::size_t keywordEnd = trimmed.find_first_of(" \t");
        const std::string_view keyword = keywordEnd == std::string_view::npos ? trimmed : trimmed.substr(0U, keywordEnd);
        const std::string_view rest = keywordEnd == std::string_view::npos ? std::string_view{} : Trim(trimmed.substr(keywordEnd + 1U));

        if (keyword == "version") {
            std::uint32_t version = 0U;
            if (!ParseUInt32(rest, version) || version == 0U) {
                diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
                    .code = RenderMaterialAssetParseDiagnosticCode::InvalidDocumentVersion,
                    .line = lineNumber,
                    .field = "version",
                    .message = "Invalid value for material document version.",
                    .text = std::string{ trimmed },
                });
                continue;
            }
            if (version > kRenderMaterialAssetDocumentVersion) {
                diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
                    .code = RenderMaterialAssetParseDiagnosticCode::UnsupportedDocumentVersion,
                    .line = lineNumber,
                    .field = "version",
                    .message = "Unsupported material document version " + std::to_string(version) + ".",
                    .text = std::string{ trimmed },
                });
                continue;
            }
            asset.documentVersion = version;
            asset.hasExplicitDocumentVersion = true;
            continue;
        }

        if (keyword == "materialType") {
            if (rest.empty()) {
                diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
                    .code = RenderMaterialAssetParseDiagnosticCode::MissingMaterialType,
                    .line = lineNumber,
                    .field = "materialType",
                    .message = "Material type is required for material assets.",
                    .text = std::string{ trimmed },
                });
                continue;
            }
            asset.materialType = std::string{ rest };
            asset.hasExplicitMaterialType = true;
            materialTypeLine = lineNumber;
            continue;
        }

        if (keyword == "materialTypeVersion") {
            std::uint32_t version = 0U;
            if (!ParseUInt32(rest, version) || version == 0U) {
                diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
                    .code = RenderMaterialAssetParseDiagnosticCode::InvalidMaterialTypeVersion,
                    .line = lineNumber,
                    .field = "materialTypeVersion",
                    .message = "Invalid value for material type version.",
                    .text = std::string{ trimmed },
                });
                continue;
            }
            asset.materialTypeVersion = version;
            asset.hasExplicitMaterialTypeVersion = true;
            materialTypeVersionLine = lineNumber;
            continue;
        }

        if (keyword == "materialTypeAssetId") {
            std::uint64_t assetId = 0U;
            if (!ParseUInt64(rest, assetId) || assetId == 0U) {
                diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
                    .code = RenderMaterialAssetParseDiagnosticCode::InvalidFieldValue,
                    .line = lineNumber,
                    .field = "materialTypeAssetId",
                    .message = "Invalid material type asset id.",
                    .text = std::string{ trimmed },
                });
                continue;
            }
            asset.materialTypeAssetId = assetId;
            sawMaterialProperty = true;
            continue;
        }

        if (keyword == "materialTypeAsset") {
            if (rest.empty()) {
                diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
                    .code = RenderMaterialAssetParseDiagnosticCode::InvalidFieldValue,
                    .line = lineNumber,
                    .field = "materialTypeAsset",
                    .message = "Invalid material type asset path.",
                    .text = std::string{ trimmed },
                });
                continue;
            }
            asset.materialTypeAssetPath = std::string{ rest };
            sawMaterialProperty = true;
            continue;
        }

        if (keyword == "graphSourceAssetId") {
            std::uint64_t assetId = 0U;
            if (!ParseUInt64(rest, assetId) || assetId == 0U) {
                diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
                    .code = RenderMaterialAssetParseDiagnosticCode::InvalidFieldValue,
                    .line = lineNumber,
                    .field = "graphSourceAssetId",
                    .message = "Invalid material graph source asset id.",
                    .text = std::string{ trimmed },
                });
                continue;
            }
            asset.graphSourceAssetId = assetId;
            sawMaterialProperty = true;
            continue;
        }

        if (keyword == "graphSourceAsset") {
            if (rest.empty()) {
                diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
                    .code = RenderMaterialAssetParseDiagnosticCode::InvalidFieldValue,
                    .line = lineNumber,
                    .field = "graphSourceAsset",
                    .message = "Invalid material graph source asset path.",
                    .text = std::string{ trimmed },
                });
                continue;
            }
            asset.graphSourceAssetPath = std::string{ rest };
            sawMaterialProperty = true;
            continue;
        }

        const RenderMaterialTypeSchema& materialTypeSchema = GetBuiltInPbrMaterialTypeSchema();
        if (const RenderMaterialTypeMigrationOperation* removed = FindMaterialTypeMigration(
                materialTypeSchema,
                RenderMaterialTypeMigrationOperationKind::RemoveUnsupported,
                keyword)) {
            diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
                .code = RenderMaterialAssetParseDiagnosticCode::UnsupportedAdvancedField,
                .severity = RenderMaterialAssetParseDiagnosticSeverity::Warning,
                .line = lineNumber,
                .field = std::string{ keyword },
                .message = "Material field '" + std::string{ keyword } + "' was removed by the material type migration table. " + std::string{ removed->reason },
                .text = std::string{ trimmed },
            });
            continue;
        }

        const RenderMaterialTypeMigrationOperation* rename = FindMaterialTypeMigration(
            materialTypeSchema,
            RenderMaterialTypeMigrationOperationKind::RenameParameter,
            keyword);
        const std::string_view materialKeyword = rename == nullptr ? keyword : rename->targetField;
        if ((materialKeyword == "graphLastGoodArtifactAssetId" || materialKeyword == "graphLastGoodArtifactHash") && graphLastGoodArtifactLine == 0U) {
            graphLastGoodArtifactLine = lineNumber;
        }

        if (materialKeyword == "graphParameterValue") {
            RenderMaterialGraphParameterValue value{};
            if (!ParseGraphParameterValue(rest, value)) {
                diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
                    .code = RenderMaterialAssetParseDiagnosticCode::InvalidFieldValue,
                    .line = lineNumber,
                    .field = "graphParameterValue",
                    .message = "Invalid graph material parameter value.",
                    .text = std::string{ trimmed },
                });
                continue;
            }
            asset.graphParameterValues.push_back(std::move(value));
            sawMaterialProperty = true;
            continue;
        }

        const RenderMaterialGraphFieldParseResult graphField = RenderMaterialGraphFieldParser::Apply(materialKeyword, rest, lineNumber, asset, diagnostics);
        if (graphField != RenderMaterialGraphFieldParseResult::Unknown) {
            sawMaterialProperty = true;
            continue;
        }

        if (!RenderMaterialAssetFieldParser::Apply(materialKeyword, rest, asset)) {
            const bool known = RenderMaterialAssetFieldParser::IsKnown(materialKeyword);
            const RenderMaterialAssetParseDiagnosticCode code = !known
                ? RenderMaterialAssetParseDiagnosticCode::UnknownField
                : IsEnumMaterialField(materialKeyword)
                    ? RenderMaterialAssetParseDiagnosticCode::InvalidEnum
                    : IsNumericMaterialField(materialKeyword)
                        ? RenderMaterialAssetParseDiagnosticCode::InvalidFloat
                        : RenderMaterialAssetParseDiagnosticCode::InvalidFieldValue;
            diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
                .code = code,
                .line = lineNumber,
                .field = std::string{ materialKeyword },
                .message = !known
                    ? "Unknown material field '" + std::string{ keyword } + "'."
                    : code == RenderMaterialAssetParseDiagnosticCode::InvalidEnum
                        ? "Invalid enum value for material field '" + std::string{ materialKeyword } + "'."
                        : code == RenderMaterialAssetParseDiagnosticCode::InvalidFloat
                            ? "Invalid float value for material field '" + std::string{ materialKeyword } + "'."
                            : "Invalid value for material field '" + std::string{ materialKeyword } + "'.",
                .text = std::string{ trimmed },
            });
            continue;
        }

        if (IsOutOfRangeMaterialField(materialKeyword, asset)) {
            diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
                .code = RenderMaterialAssetParseDiagnosticCode::OutOfRange,
                .line = lineNumber,
                .field = std::string{ materialKeyword },
                .message = "Material field '" + std::string{ materialKeyword } + "' is outside the supported built-in PBR range.",
                .text = std::string{ trimmed },
            });
            continue;
        }

        if (IsActiveUnsupportedAdvancedField(materialKeyword, asset)) {
            diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
                .code = RenderMaterialAssetParseDiagnosticCode::UnsupportedAdvancedField,
                .severity = RenderMaterialAssetParseDiagnosticSeverity::Warning,
                .line = lineNumber,
                .field = std::string{ materialKeyword },
                .message = "Material field '" + std::string{ materialKeyword } + "' is parsed but ignored by the current runtime shader path.",
                .text = std::string{ trimmed },
            });
        }

        if (IsTextureColorSpaceExpectationField(materialKeyword, asset)) {
            std::string_view pathFieldName = materialKeyword;
            if (materialKeyword == "albedoTextureAssetId" || materialKeyword == "baseColorTextureAssetId") pathFieldName = "albedoTexture";
            else if (materialKeyword == "normalTextureAssetId") pathFieldName = "normalTexture";
            else if (materialKeyword == "metallicRoughnessTextureAssetId") pathFieldName = "metallicRoughnessTexture";
            else if (materialKeyword == "occlusionTextureAssetId") pathFieldName = "occlusionTexture";
            else if (materialKeyword == "emissiveTextureAssetId") pathFieldName = "emissiveTexture";
            else if (materialKeyword == "clearcoatTextureAssetId") pathFieldName = "clearcoatTexture";
            else if (materialKeyword == "clearcoatRoughnessTextureAssetId") pathFieldName = "clearcoatRoughnessTexture";
            else if (materialKeyword == "sheenColorTextureAssetId") pathFieldName = "sheenColorTexture";
            else if (materialKeyword == "transmissionTextureAssetId") pathFieldName = "transmissionTexture";
            else if (materialKeyword == "thicknessTextureAssetId") pathFieldName = "thicknessTexture";
            else if (materialKeyword == "anisotropyTextureAssetId") pathFieldName = "anisotropyTexture";
            else if (materialKeyword == "decalTextureAssetId") pathFieldName = "decalTexture";
            else if (materialKeyword == "layerMaskTextureAssetId") pathFieldName = "layerMaskTexture";

            const RenderMaterialTextureSlotSchema* slot = nullptr;
            for (const auto& s : materialTypeSchema.textureSlots) {
                if (s.pathFieldName == pathFieldName) {
                    slot = &s;
                    break;
                }
            }
            if (slot) {
                std::string expectedSpace;
                if (slot->expectedColorSpace == RenderMaterialTextureColorSpace::Srgb) expectedSpace = "sRGB";
                else if (slot->expectedColorSpace == RenderMaterialTextureColorSpace::Linear) expectedSpace = "linear";
                else expectedSpace = "unknown";
                diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
                    .code = RenderMaterialAssetParseDiagnosticCode::TextureColorSpaceExpectation,
                    .severity = RenderMaterialAssetParseDiagnosticSeverity::Warning,
                    .line = lineNumber,
                    .field = std::string{ materialKeyword },
                    .message = "Texture slot '" + std::string{ slot->name } + "' expects " + expectedSpace + " color space. Validate that the assigned texture matches this expectation.",
                    .text = std::string{ trimmed },
                });
            }
        }
        sawMaterialProperty = true;
    }

    const bool builtInMaterialType = IsSupportedMaterialType(asset.materialType);
    const bool hasMaterialTypeAssetReference = asset.materialTypeAssetId != 0U || !asset.materialTypeAssetPath.empty();
    if (!builtInMaterialType && !hasMaterialTypeAssetReference) {
        diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
            .code = RenderMaterialAssetParseDiagnosticCode::UnsupportedMaterialType,
            .line = materialTypeLine,
            .field = "materialType",
            .message = "Unsupported material type '" + asset.materialType + "'.",
            .text = asset.materialType,
        });
        if (asset.hasExplicitMaterialTypeVersion) {
            diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
                .code = RenderMaterialAssetParseDiagnosticCode::UnsupportedMaterialTypeVersion,
                .line = materialTypeVersionLine,
                .field = "materialTypeVersion",
                .message = "Unsupported material type version " + std::to_string(asset.materialTypeVersion) + " for '" + asset.materialType + "'.",
                .text = std::to_string(asset.materialTypeVersion),
            });
        }
    } else if (builtInMaterialType) {
        const std::uint32_t supportedVersion = SupportedMaterialTypeVersion(asset.materialType);
        if (asset.materialTypeVersion > supportedVersion) {
            diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
                .code = RenderMaterialAssetParseDiagnosticCode::UnsupportedMaterialTypeVersion,
                .line = materialTypeVersionLine,
                .field = "materialTypeVersion",
                .message = "Unsupported material type version " + std::to_string(asset.materialTypeVersion) + " for '" + asset.materialType + "'.",
                .text = std::to_string(asset.materialTypeVersion),
            });
        }
    }

    if (!sawMaterialProperty && diagnostics.empty()) {
        diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
            .code = RenderMaterialAssetParseDiagnosticCode::EmptyDocument,
            .line = 0U,
            .field = {},
            .message = "Material asset does not contain any material properties.",
            .text = {},
        });
    }
    if (sawMaterialProperty && asset.graph.nodes.empty()) {
        RenderMaterialGraphDocument graphMetadata = std::move(asset.graph);
        asset.graph = MakeDefaultRenderMaterialGraphDocument();
        asset.graph.documentVersion = graphMetadata.documentVersion;
        asset.graph.hasExplicitDocumentVersion = graphMetadata.hasExplicitDocumentVersion;
        asset.graph.materialDomain = std::move(graphMetadata.materialDomain);
        asset.graph.shadingModel = std::move(graphMetadata.shadingModel);
        asset.graph.storageModel = std::move(graphMetadata.storageModel);
        asset.graph.diagnosticSchemaVersion = graphMetadata.diagnosticSchemaVersion;
        asset.graph.persistCompileDiagnostics = graphMetadata.persistCompileDiagnostics;
        asset.graph.artifactFailurePolicy = graphMetadata.artifactFailurePolicy;
        asset.graph.hasExplicitArtifactFailurePolicy = graphMetadata.hasExplicitArtifactFailurePolicy;
        asset.graph.lastGoodArtifact = graphMetadata.lastGoodArtifact;
    }
    if ((asset.graph.lastGoodArtifact.assetId == 0U) != (asset.graph.lastGoodArtifact.contentHash == 0U)) {
        diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
            .code = RenderMaterialAssetParseDiagnosticCode::InvalidGraphField,
            .line = graphLastGoodArtifactLine,
            .field = "graphLastGoodArtifact",
            .message = "Material graph last-good artifact requires both asset id and content hash.",
            .text = {},
        });
    }

    return RenderMaterialAssetParseResult{
        .asset = !HasError(diagnostics) && sawMaterialProperty ? std::optional<RenderMaterialAssetData>{ asset } : std::nullopt,
        .diagnostics = std::move(diagnostics),
    };
}

RenderMaterialAssetParseResult RenderMaterialAssetParser::ParseWithDiagnostics(std::istream& input, const RenderMaterialAssetParseSourceContext& sourceContext) {
    RenderMaterialAssetParseResult result = ParseWithDiagnostics(input);
    AttachContext(result, sourceContext);
    return result;
}

} // namespace kb::render
