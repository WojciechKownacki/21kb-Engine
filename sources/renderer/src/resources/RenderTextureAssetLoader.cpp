#include "kb/render/resources/RenderTextureAssetLoader.hpp"

#include "engine/assets/ImportedAsset.hpp"
#include "engine/assets/ImportedAssetLoader.hpp"

#include <bimg/decode.h>
#include <bx/allocator.h>

#include <cstddef>
#include <charconv>
#include <cctype>
#include <fstream>
#include <istream>
#include <limits>
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

template <typename T>
[[nodiscard]] bool ParseUnsigned(std::string_view text, T& output) noexcept {
    text = Trim(text);
    std::uint32_t parsed = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed > static_cast<std::uint32_t>(std::numeric_limits<T>::max())) {
        return false;
    }
    output = static_cast<T>(parsed);
    return true;
}

[[nodiscard]] bool ParseSize(std::string_view rest, RenderTextureAssetData& asset) {
    std::istringstream stream{ std::string{ rest } };
    std::string width;
    std::string height;
    return (stream >> width >> height) &&
        ParseUnsigned(width, asset.width) &&
        ParseUnsigned(height, asset.height) &&
        asset.width > 0U &&
        asset.height > 0U;
}

[[nodiscard]] bool ParseDimension(std::string_view text, RenderTextureDimension& dimension) noexcept {
    text = Trim(text);
    if (text == "2d" || text == "Texture2D") {
        dimension = RenderTextureDimension::Texture2D;
        return true;
    }
    if (text == "cube" || text == "TextureCube") {
        dimension = RenderTextureDimension::TextureCube;
        return true;
    }
    if (text == "3d" || text == "volume" || text == "Texture3D") {
        dimension = RenderTextureDimension::Texture3D;
        return true;
    }
    if (text == "2dArray" || text == "array" || text == "Texture2DArray") {
        dimension = RenderTextureDimension::Texture2DArray;
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseColorSpace(std::string_view text, RenderTextureAssetColorSpace& colorSpace) noexcept {
    text = Trim(text);
    if (text == "unknown" || text == "Unknown") {
        colorSpace = RenderTextureAssetColorSpace::Unknown;
        return true;
    }
    if (text == "linear" || text == "Linear") {
        colorSpace = RenderTextureAssetColorSpace::Linear;
        return true;
    }
    if (text == "srgb" || text == "sRGB" || text == "Srgb") {
        colorSpace = RenderTextureAssetColorSpace::Srgb;
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseSemantic(std::string_view text, RenderTextureAssetSemantic& semantic) noexcept {
    text = Trim(text);
    if (text == "unknown" || text == "Unknown") {
        semantic = RenderTextureAssetSemantic::Unknown;
        return true;
    }
    if (text == "baseColor" || text == "BaseColor") {
        semantic = RenderTextureAssetSemantic::BaseColor;
        return true;
    }
    if (text == "normal" || text == "Normal") {
        semantic = RenderTextureAssetSemantic::Normal;
        return true;
    }
    if (text == "metallicRoughness" || text == "MetallicRoughness") {
        semantic = RenderTextureAssetSemantic::MetallicRoughness;
        return true;
    }
    if (text == "occlusion" || text == "Occlusion") {
        semantic = RenderTextureAssetSemantic::Occlusion;
        return true;
    }
    if (text == "emissive" || text == "Emissive") {
        semantic = RenderTextureAssetSemantic::Emissive;
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseRgba8(std::string_view rest, std::uint8_t (&rgba)[4]) {
    std::istringstream stream{ std::string{ rest } };
    std::string r;
    std::string g;
    std::string b;
    std::string a;
    return (stream >> r >> g >> b >> a) &&
        ParseUnsigned(r, rgba[0]) &&
        ParseUnsigned(g, rgba[1]) &&
        ParseUnsigned(b, rgba[2]) &&
        ParseUnsigned(a, rgba[3]);
}

[[nodiscard]] std::optional<std::size_t> TextureTexelCount(const RenderTextureAssetData& asset) noexcept {
    std::size_t sliceCount = 1U;
    switch (asset.dimension) {
    case RenderTextureDimension::Texture2D:
        if (asset.depth != 1U || asset.layers != 1U) return std::nullopt;
        break;
    case RenderTextureDimension::TextureCube:
        if (asset.width != asset.height || asset.depth != 1U || asset.layers != 1U) return std::nullopt;
        sliceCount = 6U;
        break;
    case RenderTextureDimension::Texture3D:
        if (asset.depth <= 1U || asset.layers != 1U) return std::nullopt;
        sliceCount = asset.depth;
        break;
    case RenderTextureDimension::Texture2DArray:
        if (asset.depth != 1U || asset.layers <= 1U) return std::nullopt;
        sliceCount = asset.layers;
        break;
    }

    const std::size_t width = asset.width;
    const std::size_t height = asset.height;
    if (width == 0U || height == 0U || width > std::numeric_limits<std::size_t>::max() / height) {
        return std::nullopt;
    }
    const std::size_t sliceTexels = width * height;
    if (sliceTexels > std::numeric_limits<std::size_t>::max() / sliceCount) {
        return std::nullopt;
    }
    return sliceTexels * sliceCount;
}

[[nodiscard]] bool FillTexture(RenderTextureAssetData& asset, const std::uint8_t (&rgba)[4]) {
    const std::optional<std::size_t> texelCount = TextureTexelCount(asset);
    if (!texelCount.has_value() || *texelCount > std::numeric_limits<std::size_t>::max() / 4U) {
        return false;
    }
    asset.rgba8.resize(*texelCount * 4U);
    for (std::size_t index = 0U; index < asset.rgba8.size(); index += 4U) {
        asset.rgba8[index + 0U] = rgba[0];
        asset.rgba8[index + 1U] = rgba[1];
        asset.rgba8[index + 2U] = rgba[2];
        asset.rgba8[index + 3U] = rgba[3];
    }
    return true;
}

[[nodiscard]] std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    for (char& character : extension) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return extension;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> ReadBinaryFile(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    if (!input) {
        return std::nullopt;
    }

    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size <= 0) {
        return std::nullopt;
    }
    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input) {
        return std::nullopt;
    }
    return bytes;
}

[[nodiscard]] std::optional<RenderTextureAssetData> LoadImageBytes(const void* data, std::size_t size) {
    if (data == nullptr || size == 0U || size > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return std::nullopt;
    }

    bx::DefaultAllocator allocator;
    bimg::ImageContainer* image = bimg::imageParse(
        &allocator,
        data,
        static_cast<std::uint32_t>(size),
        bimg::TextureFormat::RGBA8);
    if (image == nullptr) {
        return std::nullopt;
    }

    RenderTextureAssetData asset{};
    if (image->m_width == 0U ||
        image->m_height == 0U ||
        image->m_depth == 0U ||
        image->m_numLayers == 0U ||
        image->m_numMips == 0U ||
        image->m_width > std::numeric_limits<std::uint16_t>::max() ||
        image->m_height > std::numeric_limits<std::uint16_t>::max() ||
        image->m_depth > std::numeric_limits<std::uint16_t>::max() ||
        image->m_data == nullptr ||
        image->m_size == 0U) {
        bimg::imageFree(image);
        return std::nullopt;
    }

    asset.width = static_cast<std::uint16_t>(image->m_width);
    asset.height = static_cast<std::uint16_t>(image->m_height);
    asset.depth = static_cast<std::uint16_t>(image->m_depth);
    asset.layers = image->m_numLayers;
    // The runtime raw-texture API accepts only a hasMips bit and therefore requires a complete
    // chain. Preserve the legacy loader contract (LOD0 only), but collect LOD0 from every
    // face/layer; a volume's side 0 payload already contains its complete depth.
    asset.mipCount = 1U;
    if (image->m_cubeMap) {
        // samplerCubeArray is not part of the material graph contract. Reject it instead of silently
        // presenting a cube array as a samplerCube resource.
        if (asset.layers != 1U || asset.depth != 1U || asset.width != asset.height) {
            bimg::imageFree(image);
            return std::nullopt;
        }
        asset.dimension = RenderTextureDimension::TextureCube;
    } else if (asset.depth > 1U) {
        if (asset.layers != 1U) {
            bimg::imageFree(image);
            return std::nullopt;
        }
        asset.dimension = RenderTextureDimension::Texture3D;
    } else if (asset.layers > 1U) {
        asset.dimension = RenderTextureDimension::Texture2DArray;
    } else {
        asset.dimension = RenderTextureDimension::Texture2D;
    }

    const std::optional<std::size_t> texelCount = TextureTexelCount(asset);
    const std::size_t baseLevelBytes = texelCount.has_value() && *texelCount <= std::numeric_limits<std::size_t>::max() / 4U
        ? *texelCount * 4U
        : 0U;
    if (baseLevelBytes == 0U) {
        bimg::imageFree(image);
        return std::nullopt;
    }

    const std::uint16_t sideCount = image->m_cubeMap ? 6U : asset.layers;
    asset.rgba8.reserve(baseLevelBytes);
    for (std::uint16_t side = 0U; side < sideCount; ++side) {
        bimg::ImageMip mip{};
        if (!bimg::imageGetRawData(*image, side, 0U, image->m_data, image->m_size, mip) ||
            mip.m_data == nullptr || mip.m_format != bimg::TextureFormat::RGBA8 ||
            mip.m_width != asset.width || mip.m_height != asset.height ||
            mip.m_depth != (asset.dimension == RenderTextureDimension::Texture3D ? asset.depth : 1U) ||
            mip.m_size > baseLevelBytes - asset.rgba8.size()) {
            bimg::imageFree(image);
            return std::nullopt;
        }
        const auto* begin = static_cast<const std::uint8_t*>(mip.m_data);
        asset.rgba8.insert(asset.rgba8.end(), begin, begin + mip.m_size);
    }
    if (asset.rgba8.size() != baseLevelBytes) {
        bimg::imageFree(image);
        return std::nullopt;
    }

    bimg::imageFree(image);
    return asset;
}

[[nodiscard]] std::optional<RenderTextureAssetData> LoadImageFile(const std::filesystem::path& path) {
    std::optional<std::vector<std::uint8_t>> bytes = ReadBinaryFile(path);
    if (!bytes.has_value()) {
        return std::nullopt;
    }
    return LoadImageBytes(bytes->data(), bytes->size());
}

[[nodiscard]] std::optional<RenderTextureAssetData> LoadImportedTextureContainer(const std::filesystem::path& path) {
    kb::assets::AssetMetadata metadata{};
    metadata.physicalPath = path;
    metadata.virtualPath = path.filename();
    metadata.type = "Texture";
    metadata.importCategory = "Texture";

    kb::assets::ImportedAssetLoader importedLoader;
    kb::assets::AssetLoadResult result = importedLoader.Load(kb::assets::AssetLoadRequest{
        .metadata = metadata,
        .resolvedPath = path,
    });
    if (!result.Succeeded()) {
        return std::nullopt;
    }

    const std::shared_ptr<kb::assets::ImportedAsset> imported = std::static_pointer_cast<kb::assets::ImportedAsset>(result.asset);
    if (imported == nullptr || imported->category != kb::assets::AssetImportCategory::Texture || imported->payload.empty()) {
        return std::nullopt;
    }
    return LoadImageBytes(imported->payload.data(), imported->payload.size());
}

} // namespace

RenderTextureDesc RenderTextureAssetData::MakeDesc(const bgfx::Memory* memory, RenderTextureColorSpace runtimeColorSpace) const noexcept {
    return RenderTextureDesc{
        .width = width,
        .height = height,
        .depth = depth,
        .layers = layers,
        .mipCount = mipCount,
        .dimension = dimension,
        .format = bgfx::TextureFormat::RGBA8,
        .flags = BGFX_SAMPLER_NONE | (runtimeColorSpace == RenderTextureColorSpace::Srgb ? BGFX_TEXTURE_SRGB : 0ULL),
        .memory = memory,
        .colorSpace = runtimeColorSpace,
    };
}

std::string_view RenderTextureAssetLoader::Type() const noexcept {
    return "RenderTexture";
}

std::type_index RenderTextureAssetLoader::PayloadType() const noexcept {
    return typeid(RenderTextureAssetData);
}

std::vector<std::string> RenderTextureAssetLoader::Extensions() const {
    return { ".kbtex", ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".dds", ".ktx" };
}

kb::assets::AssetLoadResult RenderTextureAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::optional<RenderTextureAssetData> texture = LoadTexture(request.resolvedPath);
    if (!texture.has_value()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = "Render texture asset load failed" };
    }
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<RenderTextureAssetData>(std::move(*texture)),
        .error = {},
    };
}

std::optional<RenderTextureAssetData> RenderTextureAssetLoader::LoadTexture(const std::filesystem::path& path) {
    if (LowerExtension(path) == ".21kb") {
        return LoadImportedTextureContainer(path);
    }
    if (LowerExtension(path) != ".kbtex") {
        return LoadImageFile(path);
    }

    std::ifstream input{ path };
    if (!input) {
        return std::nullopt;
    }
    return LoadTexture(input);
}

std::optional<RenderTextureAssetData> RenderTextureAssetLoader::LoadTexture(std::istream& input) {
    RenderTextureAssetData asset{};
    std::uint8_t rgba[4]{ 255U, 255U, 255U, 255U };
    bool sawSize = false;
    bool sawColor = false;

    std::string line;
    while (std::getline(input, line)) {
        std::string_view trimmed = Trim(StripComment(line));
        if (trimmed.empty()) {
            continue;
        }

        const std::size_t keywordEnd = trimmed.find_first_of(" \t");
        const std::string_view keyword = keywordEnd == std::string_view::npos ? trimmed : trimmed.substr(0U, keywordEnd);
        const std::string_view rest = keywordEnd == std::string_view::npos ? std::string_view{} : Trim(trimmed.substr(keywordEnd + 1U));
        if (keyword == "size") {
            if (!ParseSize(rest, asset)) {
                return std::nullopt;
            }
            sawSize = true;
        } else if (keyword == "dimension") {
            if (!ParseDimension(rest, asset.dimension)) {
                return std::nullopt;
            }
        } else if (keyword == "depth") {
            if (!ParseUnsigned(rest, asset.depth) || asset.depth == 0U) {
                return std::nullopt;
            }
        } else if (keyword == "layers") {
            if (!ParseUnsigned(rest, asset.layers) || asset.layers == 0U) {
                return std::nullopt;
            }
        } else if (keyword == "colorSpace") {
            if (!ParseColorSpace(rest, asset.colorSpace)) {
                return std::nullopt;
            }
        } else if (keyword == "semantic") {
            if (!ParseSemantic(rest, asset.semantic)) {
                return std::nullopt;
            }
        } else if (keyword == "rgba8") {
            if (!ParseRgba8(rest, rgba)) {
                return std::nullopt;
            }
            sawColor = true;
        } else {
            return std::nullopt;
        }
    }

    if (!sawSize || !sawColor) {
        return std::nullopt;
    }

    return FillTexture(asset, rgba) ? std::optional<RenderTextureAssetData>{ std::move(asset) } : std::nullopt;
}

} // namespace kb::render
