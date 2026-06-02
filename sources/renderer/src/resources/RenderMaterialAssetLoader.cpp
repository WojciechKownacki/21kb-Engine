#include "kb/render/resources/RenderMaterialAssetLoader.hpp"

#include <charconv>
#include <cstddef>
#include <fstream>
#include <istream>
#include <memory>
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

[[nodiscard]] std::string_view StripComment(std::string_view line) noexcept {
    const std::size_t comment = line.find('#');
    return comment == std::string_view::npos ? line : line.substr(0U, comment);
}

[[nodiscard]] bool ParseFloat(std::string_view text, float& output) noexcept {
    text = Trim(text);
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, output);
    return result.ec == std::errc{} && result.ptr == end;
}

[[nodiscard]] bool ParseUint64(std::string_view text, std::uint64_t& output) noexcept {
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

std::string_view RenderMaterialAssetLoader::Type() const noexcept {
    return "RenderMaterial";
}

std::type_index RenderMaterialAssetLoader::PayloadType() const noexcept {
    return typeid(RenderMaterialAssetData);
}

std::vector<std::string> RenderMaterialAssetLoader::Extensions() const {
    return { ".kbmat" };
}

kb::assets::AssetLoadResult RenderMaterialAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::optional<RenderMaterialAssetData> material = LoadMaterial(request.resolvedPath);
    if (!material.has_value()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = "Render material asset load failed" };
    }
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<RenderMaterialAssetData>(*material),
        .error = {},
    };
}

std::optional<RenderMaterialAssetData> RenderMaterialAssetLoader::LoadMaterial(const std::filesystem::path& path) {
    std::ifstream input{ path };
    if (!input) {
        return std::nullopt;
    }
    return LoadMaterial(input);
}

std::optional<RenderMaterialAssetData> RenderMaterialAssetLoader::LoadMaterial(std::istream& input) {
    RenderMaterialAssetData asset{};
    bool sawMaterialProperty = false;

    std::string line;
    while (std::getline(input, line)) {
        std::string_view trimmed = Trim(StripComment(line));
        if (trimmed.empty()) {
            continue;
        }

        const std::size_t keywordEnd = trimmed.find_first_of(" \t");
        const std::string_view keyword = keywordEnd == std::string_view::npos ? trimmed : trimmed.substr(0U, keywordEnd);
        const std::string_view rest = keywordEnd == std::string_view::npos ? std::string_view{} : Trim(trimmed.substr(keywordEnd + 1U));
        if (keyword == "baseColor" || keyword == "baseColorFactor") {
            if (!ParseBaseColor(rest, asset.desc)) {
                return std::nullopt;
            }
            sawMaterialProperty = true;
        } else if (keyword == "emissiveColor" || keyword == "emissiveFactor") {
            if (!ParseVec3(rest, asset.desc.emissiveColor)) {
                return std::nullopt;
            }
            sawMaterialProperty = true;
        } else if (keyword == "metallicFactor") {
            if (!ParseFloat(rest, asset.desc.metallicFactor)) {
                return std::nullopt;
            }
            sawMaterialProperty = true;
        } else if (keyword == "roughnessFactor") {
            if (!ParseFloat(rest, asset.desc.roughnessFactor)) {
                return std::nullopt;
            }
            sawMaterialProperty = true;
        } else if (keyword == "normalScale") {
            if (!ParseFloat(rest, asset.desc.normalScale)) {
                return std::nullopt;
            }
            sawMaterialProperty = true;
        } else if (keyword == "occlusionStrength") {
            if (!ParseFloat(rest, asset.desc.occlusionStrength)) {
                return std::nullopt;
            }
            sawMaterialProperty = true;
        } else if (keyword == "emissiveStrength") {
            if (!ParseFloat(rest, asset.desc.emissiveStrength)) {
                return std::nullopt;
            }
            sawMaterialProperty = true;
        } else if (keyword == "alphaCutoff") {
            if (!ParseFloat(rest, asset.desc.alphaCutoff)) {
                return std::nullopt;
            }
            sawMaterialProperty = true;
        } else if (keyword == "alphaMode") {
            if (!ParseAlphaMode(rest, asset.desc.alphaMode)) {
                return std::nullopt;
            }
            sawMaterialProperty = true;
        } else if (keyword == "doubleSided") {
            if (!ParseBool(rest, asset.desc.doubleSided)) {
                return std::nullopt;
            }
            sawMaterialProperty = true;
        } else if (keyword == "albedoTextureAssetId" || keyword == "baseColorTextureAssetId") {
            if (!ParseUint64(rest, asset.desc.albedoTextureAssetId)) {
                return std::nullopt;
            }
            sawMaterialProperty = true;
        } else if (keyword == "albedoTexture" || keyword == "baseColorTexture") {
            asset.albedoTexturePath = std::string{ rest };
            sawMaterialProperty = true;
        } else if (keyword == "normalTextureAssetId") {
            if (!ParseUint64(rest, asset.desc.normalTextureAssetId)) {
                return std::nullopt;
            }
            sawMaterialProperty = true;
        } else if (keyword == "normalTexture") {
            asset.normalTexturePath = std::string{ rest };
            sawMaterialProperty = true;
        } else if (keyword == "metallicRoughnessTextureAssetId") {
            if (!ParseUint64(rest, asset.desc.metallicRoughnessTextureAssetId)) {
                return std::nullopt;
            }
            sawMaterialProperty = true;
        } else if (keyword == "metallicRoughnessTexture") {
            asset.metallicRoughnessTexturePath = std::string{ rest };
            sawMaterialProperty = true;
        } else if (keyword == "occlusionTextureAssetId") {
            if (!ParseUint64(rest, asset.desc.occlusionTextureAssetId)) {
                return std::nullopt;
            }
            sawMaterialProperty = true;
        } else if (keyword == "occlusionTexture") {
            asset.occlusionTexturePath = std::string{ rest };
            sawMaterialProperty = true;
        } else if (keyword == "emissiveTextureAssetId") {
            if (!ParseUint64(rest, asset.desc.emissiveTextureAssetId)) {
                return std::nullopt;
            }
            sawMaterialProperty = true;
        } else if (keyword == "emissiveTexture") {
            asset.emissiveTexturePath = std::string{ rest };
            sawMaterialProperty = true;
        } else {
            return std::nullopt;
        }
    }

    return sawMaterialProperty ? std::optional<RenderMaterialAssetData>{ asset } : std::nullopt;
}

} // namespace kb::render
