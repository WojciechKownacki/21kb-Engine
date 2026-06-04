#include "resources/RenderMaterialTextureFieldParser.hpp"

#include <charconv>
#include <string>

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

[[nodiscard]] bool ParseUint64(std::string_view text, std::uint64_t& output) noexcept {
    text = Trim(text);
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, output);
    return result.ec == std::errc{} && result.ptr == end;
}

[[nodiscard]] RenderMaterialFieldParseResult ParseAssetId(std::string_view rest, std::uint64_t& output) noexcept {
    return ParseUint64(rest, output) ? RenderMaterialFieldParseResult::Parsed : RenderMaterialFieldParseResult::Invalid;
}

} // namespace

RenderMaterialFieldParseResult RenderMaterialTextureFieldParser::Apply(std::string_view keyword, std::string_view rest, RenderMaterialAssetData& asset) {
    if (keyword == "albedoTextureAssetId" || keyword == "baseColorTextureAssetId") {
        return ParseAssetId(rest, asset.desc.albedoTextureAssetId);
    }
    if (keyword == "albedoTexture" || keyword == "baseColorTexture") {
        asset.albedoTexturePath = std::string{ rest };
        return RenderMaterialFieldParseResult::Parsed;
    }
    if (keyword == "normalTextureAssetId") {
        return ParseAssetId(rest, asset.desc.normalTextureAssetId);
    }
    if (keyword == "normalTexture") {
        asset.normalTexturePath = std::string{ rest };
        return RenderMaterialFieldParseResult::Parsed;
    }
    if (keyword == "metallicRoughnessTextureAssetId") {
        return ParseAssetId(rest, asset.desc.metallicRoughnessTextureAssetId);
    }
    if (keyword == "metallicRoughnessTexture") {
        asset.metallicRoughnessTexturePath = std::string{ rest };
        return RenderMaterialFieldParseResult::Parsed;
    }
    if (keyword == "occlusionTextureAssetId") {
        return ParseAssetId(rest, asset.desc.occlusionTextureAssetId);
    }
    if (keyword == "occlusionTexture") {
        asset.occlusionTexturePath = std::string{ rest };
        return RenderMaterialFieldParseResult::Parsed;
    }
    if (keyword == "emissiveTextureAssetId") {
        return ParseAssetId(rest, asset.desc.emissiveTextureAssetId);
    }
    if (keyword == "emissiveTexture") {
        asset.emissiveTexturePath = std::string{ rest };
        return RenderMaterialFieldParseResult::Parsed;
    }
    if (keyword == "clearcoatTextureAssetId") {
        return ParseAssetId(rest, asset.desc.clearcoatTextureAssetId);
    }
    if (keyword == "clearcoatTexture") {
        asset.clearcoatTexturePath = std::string{ rest };
        return RenderMaterialFieldParseResult::Parsed;
    }
    if (keyword == "clearcoatRoughnessTextureAssetId") {
        return ParseAssetId(rest, asset.desc.clearcoatRoughnessTextureAssetId);
    }
    if (keyword == "clearcoatRoughnessTexture") {
        asset.clearcoatRoughnessTexturePath = std::string{ rest };
        return RenderMaterialFieldParseResult::Parsed;
    }
    if (keyword == "sheenColorTextureAssetId") {
        return ParseAssetId(rest, asset.desc.sheenColorTextureAssetId);
    }
    if (keyword == "sheenColorTexture") {
        asset.sheenColorTexturePath = std::string{ rest };
        return RenderMaterialFieldParseResult::Parsed;
    }
    if (keyword == "transmissionTextureAssetId") {
        return ParseAssetId(rest, asset.desc.transmissionTextureAssetId);
    }
    if (keyword == "transmissionTexture") {
        asset.transmissionTexturePath = std::string{ rest };
        return RenderMaterialFieldParseResult::Parsed;
    }
    if (keyword == "thicknessTextureAssetId") {
        return ParseAssetId(rest, asset.desc.thicknessTextureAssetId);
    }
    if (keyword == "thicknessTexture") {
        asset.thicknessTexturePath = std::string{ rest };
        return RenderMaterialFieldParseResult::Parsed;
    }
    if (keyword == "anisotropyTextureAssetId") {
        return ParseAssetId(rest, asset.desc.anisotropyTextureAssetId);
    }
    if (keyword == "anisotropyTexture") {
        asset.anisotropyTexturePath = std::string{ rest };
        return RenderMaterialFieldParseResult::Parsed;
    }
    if (keyword == "decalTextureAssetId") {
        return ParseAssetId(rest, asset.desc.decalTextureAssetId);
    }
    if (keyword == "decalTexture") {
        asset.decalTexturePath = std::string{ rest };
        return RenderMaterialFieldParseResult::Parsed;
    }
    if (keyword == "layerMaskTextureAssetId") {
        return ParseAssetId(rest, asset.desc.layerMaskTextureAssetId);
    }
    if (keyword == "layerMaskTexture") {
        asset.layerMaskTexturePath = std::string{ rest };
        return RenderMaterialFieldParseResult::Parsed;
    }
    return RenderMaterialFieldParseResult::Unknown;
}

} // namespace kb::render
