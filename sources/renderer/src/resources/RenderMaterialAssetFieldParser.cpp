#include "resources/RenderMaterialAssetFieldParser.hpp"

#include "resources/RenderMaterialTextureFieldParser.hpp"

#include <charconv>
#include <cstddef>
#include <sstream>
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

[[nodiscard]] bool ParseFloat(std::string_view text, float& output) noexcept {
    text = Trim(text);
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, output);
    return result.ec == std::errc{} && result.ptr == end;
}

[[nodiscard]] bool ParseBaseColor(std::string_view rest, RenderMaterialDesc& desc) {
    std::istringstream stream{ std::string{ rest } };
    std::string r;
    std::string g;
    std::string b;
    std::string a;
    if (!(stream >> r >> g >> b >> a)) {
        return false;
    }
    return ParseFloat(r, desc.baseColor[0]) &&
        ParseFloat(g, desc.baseColor[1]) &&
        ParseFloat(b, desc.baseColor[2]) &&
        ParseFloat(a, desc.baseColor[3]);
}

[[nodiscard]] bool ParseVec3(std::string_view rest, float (&output)[3]) {
    std::istringstream stream{ std::string{ rest } };
    std::string x;
    std::string y;
    std::string z;
    if (!(stream >> x >> y >> z)) {
        return false;
    }
    return ParseFloat(x, output[0]) &&
        ParseFloat(y, output[1]) &&
        ParseFloat(z, output[2]);
}

[[nodiscard]] bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < lhs.size(); ++index) {
        char left = lhs[index];
        char right = rhs[index];
        if (left >= 'A' && left <= 'Z') {
            left = static_cast<char>(left - 'A' + 'a');
        }
        if (right >= 'A' && right <= 'Z') {
            right = static_cast<char>(right - 'A' + 'a');
        }
        if (left != right) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ParseAlphaMode(std::string_view rest, RenderMaterialAlphaMode& output) noexcept {
    rest = Trim(rest);
    if (EqualsIgnoreCase(rest, "OPAQUE")) {
        output = RenderMaterialAlphaMode::Opaque;
        return true;
    }
    if (EqualsIgnoreCase(rest, "MASK")) {
        output = RenderMaterialAlphaMode::Mask;
        return true;
    }
    if (EqualsIgnoreCase(rest, "BLEND")) {
        output = RenderMaterialAlphaMode::Blend;
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseDecalBlendMode(std::string_view rest, RenderMaterialDecalBlendMode& output) noexcept {
    rest = Trim(rest);
    if (EqualsIgnoreCase(rest, "DISABLED") || EqualsIgnoreCase(rest, "NONE")) {
        output = RenderMaterialDecalBlendMode::Disabled;
        return true;
    }
    if (EqualsIgnoreCase(rest, "BASE_COLOR") || EqualsIgnoreCase(rest, "BASECOLOR")) {
        output = RenderMaterialDecalBlendMode::BaseColor;
        return true;
    }
    if (EqualsIgnoreCase(rest, "NORMAL")) {
        output = RenderMaterialDecalBlendMode::Normal;
        return true;
    }
    if (EqualsIgnoreCase(rest, "PBR")) {
        output = RenderMaterialDecalBlendMode::Pbr;
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseLayerBlendMode(std::string_view rest, RenderMaterialLayerBlendMode& output) noexcept {
    rest = Trim(rest);
    if (EqualsIgnoreCase(rest, "REPLACE")) {
        output = RenderMaterialLayerBlendMode::Replace;
        return true;
    }
    if (EqualsIgnoreCase(rest, "ADD")) {
        output = RenderMaterialLayerBlendMode::Add;
        return true;
    }
    if (EqualsIgnoreCase(rest, "MULTIPLY")) {
        output = RenderMaterialLayerBlendMode::Multiply;
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseBool(std::string_view rest, bool& output) noexcept {
    rest = Trim(rest);
    if (EqualsIgnoreCase(rest, "true") || EqualsIgnoreCase(rest, "1") || EqualsIgnoreCase(rest, "yes")) {
        output = true;
        return true;
    }
    if (EqualsIgnoreCase(rest, "false") || EqualsIgnoreCase(rest, "0") || EqualsIgnoreCase(rest, "no")) {
        output = false;
        return true;
    }
    return false;
}

} // namespace

bool RenderMaterialAssetFieldParser::IsKnown(std::string_view keyword) noexcept {
    return RenderMaterialTextureFieldParser::IsKnown(keyword) ||
        keyword == "baseColor" ||
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
        keyword == "layerWeight" ||
        keyword == "alphaMode" ||
        keyword == "decalBlendMode" ||
        keyword == "layerBlendMode" ||
        keyword == "doubleSided";
}

bool RenderMaterialAssetFieldParser::Apply(std::string_view keyword, std::string_view rest, RenderMaterialAssetData& asset) {
    const RenderMaterialFieldParseResult textureField = RenderMaterialTextureFieldParser::Apply(keyword, rest, asset);
    if (textureField != RenderMaterialFieldParseResult::Unknown) {
        return textureField == RenderMaterialFieldParseResult::Parsed;
    }

    if (keyword == "baseColor" || keyword == "baseColorFactor") {
        return ParseBaseColor(rest, asset.desc);
    }
    if (keyword == "emissiveColor" || keyword == "emissiveFactor") {
        return ParseVec3(rest, asset.desc.emissiveColor);
    }
    if (keyword == "metallicFactor") {
        return ParseFloat(rest, asset.desc.metallicFactor);
    }
    if (keyword == "roughnessFactor") {
        return ParseFloat(rest, asset.desc.roughnessFactor);
    }
    if (keyword == "normalScale") {
        return ParseFloat(rest, asset.desc.normalScale);
    }
    if (keyword == "occlusionStrength") {
        return ParseFloat(rest, asset.desc.occlusionStrength);
    }
    if (keyword == "emissiveStrength") {
        return ParseFloat(rest, asset.desc.emissiveStrength);
    }
    if (keyword == "alphaCutoff") {
        return ParseFloat(rest, asset.desc.alphaCutoff);
    }
    if (keyword == "clearcoatFactor") {
        return ParseFloat(rest, asset.desc.clearcoatFactor);
    }
    if (keyword == "clearcoatRoughnessFactor") {
        return ParseFloat(rest, asset.desc.clearcoatRoughnessFactor);
    }
    if (keyword == "sheenColor") {
        return ParseVec3(rest, asset.desc.sheenColor);
    }
    if (keyword == "sheenRoughnessFactor") {
        return ParseFloat(rest, asset.desc.sheenRoughnessFactor);
    }
    if (keyword == "transmissionFactor") {
        return ParseFloat(rest, asset.desc.transmissionFactor);
    }
    if (keyword == "thicknessFactor") {
        return ParseFloat(rest, asset.desc.thicknessFactor);
    }
    if (keyword == "attenuationColor") {
        return ParseVec3(rest, asset.desc.attenuationColor);
    }
    if (keyword == "attenuationDistance") {
        return ParseFloat(rest, asset.desc.attenuationDistance);
    }
    if (keyword == "subsurfaceColor") {
        return ParseVec3(rest, asset.desc.subsurfaceColor);
    }
    if (keyword == "subsurfaceFactor") {
        return ParseFloat(rest, asset.desc.subsurfaceFactor);
    }
    if (keyword == "anisotropyStrength") {
        return ParseFloat(rest, asset.desc.anisotropyStrength);
    }
    if (keyword == "anisotropyRotation") {
        return ParseFloat(rest, asset.desc.anisotropyRotation);
    }
    if (keyword == "layerWeight") {
        return ParseFloat(rest, asset.desc.layerWeight);
    }
    if (keyword == "alphaMode") {
        return ParseAlphaMode(rest, asset.desc.alphaMode);
    }
    if (keyword == "decalBlendMode") {
        return ParseDecalBlendMode(rest, asset.desc.decalBlendMode);
    }
    if (keyword == "layerBlendMode") {
        return ParseLayerBlendMode(rest, asset.desc.layerBlendMode);
    }
    if (keyword == "doubleSided") {
        return ParseBool(rest, asset.desc.doubleSided);
    }
    return false;
}

} // namespace kb::render
