#include "kb/render/resources/RenderTextureAssetLoader.hpp"

#include <bimg/decode.h>
#include <bx/allocator.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstring>
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

void FillTexture(RenderTextureAssetData& asset, const std::uint8_t (&rgba)[4]) {
    asset.rgba8.resize(static_cast<std::size_t>(asset.width) * static_cast<std::size_t>(asset.height) * 4U);
    for (std::size_t index = 0U; index < asset.rgba8.size(); index += 4U) {
        asset.rgba8[index + 0U] = rgba[0];
        asset.rgba8[index + 1U] = rgba[1];
        asset.rgba8[index + 2U] = rgba[2];
        asset.rgba8[index + 3U] = rgba[3];
    }
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

[[nodiscard]] std::optional<RenderTextureAssetData> LoadImageFile(const std::filesystem::path& path) {
    std::optional<std::vector<std::uint8_t>> bytes = ReadBinaryFile(path);
    if (!bytes.has_value()) {
        return std::nullopt;
    }

    bx::DefaultAllocator allocator;
    bimg::ImageContainer* image = bimg::imageParse(
        &allocator,
        bytes->data(),
        static_cast<std::uint32_t>(bytes->size()),
        bimg::TextureFormat::RGBA8);
    if (image == nullptr) {
        return std::nullopt;
    }

    RenderTextureAssetData asset{};
    if (image->m_width == 0U ||
        image->m_height == 0U ||
        image->m_width > std::numeric_limits<std::uint16_t>::max() ||
        image->m_height > std::numeric_limits<std::uint16_t>::max() ||
        image->m_data == nullptr ||
        image->m_size == 0U) {
        bimg::imageFree(image);
        return std::nullopt;
    }

    asset.width = static_cast<std::uint16_t>(image->m_width);
    asset.height = static_cast<std::uint16_t>(image->m_height);
    asset.rgba8.resize(static_cast<std::size_t>(asset.width) * static_cast<std::size_t>(asset.height) * 4U);
    const std::size_t copyBytes = std::min<std::size_t>(asset.rgba8.size(), image->m_size);
    std::memcpy(asset.rgba8.data(), image->m_data, copyBytes);
    if (copyBytes < asset.rgba8.size()) {
        std::fill(asset.rgba8.begin() + static_cast<std::ptrdiff_t>(copyBytes), asset.rgba8.end(), 255U);
    }

    bimg::imageFree(image);
    return asset;
}

} // namespace

RenderTextureDesc RenderTextureAssetData::MakeDesc(const bgfx::Memory* memory, RenderTextureColorSpace colorSpace) const noexcept {
    return RenderTextureDesc{
        .width = width,
        .height = height,
        .format = bgfx::TextureFormat::RGBA8,
        .flags = BGFX_SAMPLER_NONE | (colorSpace == RenderTextureColorSpace::Srgb ? BGFX_TEXTURE_SRGB : 0ULL),
        .memory = memory,
        .colorSpace = colorSpace,
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

    FillTexture(asset, rgba);
    return asset;
}

} // namespace kb::render
