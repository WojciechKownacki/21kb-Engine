#include "kb/render/bake/TextureBaker.hpp"

#include "resources/TextureContainerMagic.hpp"

#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bx/error.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace kb::render::bake {
namespace {

// The streaming header in front of the tail container: magic, header version, the full chain's
// edges and mip count, how many of its levels are streamed, and a reserved byte.
constexpr std::size_t kStreamedTextureHeaderBytes = 20U;
constexpr std::uint32_t kStreamedTextureHeaderVersion = 1U;

[[nodiscard]] std::uint32_t PeekUInt32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    std::uint32_t value = 0U;
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8U);
    }
    return value;
}

[[nodiscard]] std::uint16_t PeekUInt16(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

// Bytes of one full-chain level of a block-compressed texture, padded to whole blocks exactly as
// the baker encodes it.
[[nodiscard]] std::uint32_t LevelBytes(bimg::TextureFormat::Enum format, std::uint32_t width, std::uint32_t height) noexcept {
    return bimg::imageGetSize(nullptr, static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height), 1U, false,
        false, 1U, format);
}

[[nodiscard]] bool ReadBakedKtx(std::span<const std::uint8_t> primaryBlock, RenderTextureAssetData& out);

} // namespace

std::string BakedTextureMipBlockName(std::uint32_t level) {
    return "mip" + std::to_string(level);
}

bool BakedTextureFormatMatchesFamily(
    bgfx::TextureFormat::Enum format,
    std::string_view qualifier) noexcept {
    if (qualifier == "bc-baseline") {
        return format == bgfx::TextureFormat::BC1 || format == bgfx::TextureFormat::BC3;
    }
    if (qualifier == "bc-extended") {
        return format == bgfx::TextureFormat::BC7;
    }
    if (qualifier == "astc") {
        return format == bgfx::TextureFormat::ASTC4x4;
    }
    if (qualifier == "etc2") {
        return format == bgfx::TextureFormat::ETC2 || format == bgfx::TextureFormat::ETC2A;
    }
    return false;
}

bool ReadBakedTexture(std::span<const std::uint8_t> primaryBlock, RenderTextureAssetData& out) {
    if (primaryBlock.size() < kStreamedTextureMagic.size() ||
        std::memcmp(primaryBlock.data(), kStreamedTextureMagic.data(), kStreamedTextureMagic.size()) != 0) {
        return ReadBakedKtx(primaryBlock, out);
    }
    if (primaryBlock.size() <= kStreamedTextureHeaderBytes ||
        PeekUInt32(primaryBlock, 8U) != kStreamedTextureHeaderVersion) {
        return false;
    }
    const std::uint16_t width = PeekUInt16(primaryBlock, 12U);
    const std::uint16_t height = PeekUInt16(primaryBlock, 14U);
    const std::uint8_t mipCount = primaryBlock[16U];
    const std::uint8_t streamedMipCount = primaryBlock[17U];
    if (primaryBlock[18U] != 0U || primaryBlock[19U] != 0U || width == 0U || height == 0U ||
        streamedMipCount == 0U || streamedMipCount >= mipCount) {
        return false;
    }
    RenderTextureAssetData tail{};
    if (!ReadBakedKtx(primaryBlock.subspan(kStreamedTextureHeaderBytes), tail) || !tail.gpuBlocks.has_value()) {
        return false;
    }
    // The tail has to be exactly the full chain's levels below the streamed ones, and the
    // streamed levels exactly the ones above the tail edge; anything else is not what this
    // baker wrote.
    const auto format = static_cast<bimg::TextureFormat::Enum>(tail.gpuBlocks->format);
    if (mipCount != bimg::imageGetNumMips(format, width, height) ||
        tail.width != std::max<std::uint32_t>(1U, width >> streamedMipCount) ||
        tail.height != std::max<std::uint32_t>(1U, height >> streamedMipCount) ||
        tail.mipCount != mipCount - streamedMipCount) {
        return false;
    }
    RenderTextureStreamingLayout layout{};
    layout.width = width;
    layout.height = height;
    layout.mipCount = mipCount;
    layout.streamedMipCount = streamedMipCount;
    layout.firstLevel = streamedMipCount;
    for (std::uint32_t level = 0U; level < streamedMipCount; ++level) {
        const std::uint32_t levelWidth = std::max<std::uint32_t>(1U, width >> level);
        const std::uint32_t levelHeight = std::max<std::uint32_t>(1U, height >> level);
        if (std::max(levelWidth, levelHeight) <= kStreamedTextureTailEdge) {
            return false;
        }
        layout.streamedLevelBytes.push_back(LevelBytes(format, levelWidth, levelHeight));
    }
    if (std::max(tail.width, tail.height) > kStreamedTextureTailEdge) {
        return false;
    }
    tail.streaming = std::move(layout);
    out = std::move(tail);
    return true;
}

bool ComposeBakedTextureLevels(
    const RenderTextureAssetData& tail,
    std::span<const std::span<const std::uint8_t>> levels,
    RenderTextureAssetData& out) {
    if (!tail.streaming.has_value() || !tail.gpuBlocks.has_value() ||
        tail.streaming->firstLevel != tail.streaming->streamedMipCount ||
        levels.size() > tail.streaming->streamedMipCount) {
        return false;
    }
    const RenderTextureStreamingLayout& layout = *tail.streaming;
    const std::uint32_t firstLevel = layout.streamedMipCount - static_cast<std::uint32_t>(levels.size());
    std::size_t total = tail.gpuBlocks->blocks.size();
    for (std::size_t index = 0U; index < levels.size(); ++index) {
        if (levels[index].size() != layout.streamedLevelBytes[firstLevel + index]) {
            return false;
        }
        total += levels[index].size();
    }
    RenderTextureAssetData texture = tail;
    texture.gpuBlocks->blocks.clear();
    texture.gpuBlocks->blocks.reserve(total);
    for (const std::span<const std::uint8_t> level : levels) {
        texture.gpuBlocks->blocks.insert(texture.gpuBlocks->blocks.end(), level.begin(), level.end());
    }
    texture.gpuBlocks->blocks.insert(
        texture.gpuBlocks->blocks.end(), tail.gpuBlocks->blocks.begin(), tail.gpuBlocks->blocks.end());
    texture.width = static_cast<std::uint16_t>(std::max<std::uint32_t>(1U, layout.width >> firstLevel));
    texture.height = static_cast<std::uint16_t>(std::max<std::uint32_t>(1U, layout.height >> firstLevel));
    texture.mipCount = static_cast<std::uint8_t>(layout.mipCount - firstLevel);
    texture.streaming->firstLevel = static_cast<std::uint8_t>(firstLevel);
    out = std::move(texture);
    return true;
}

namespace {

bool ReadBakedKtx(std::span<const std::uint8_t> primaryBlock, RenderTextureAssetData& out) {
    if (primaryBlock.empty() ||
        primaryBlock.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
        TextureContainerKindOf(primaryBlock.data(), primaryBlock.size()) != TextureContainerKind::Ktx) {
        return false;
    }

    bimg::ImageContainer container{};
    bx::Error parseError;
    if (!bimg::imageParse(
            container, primaryBlock.data(), static_cast<std::uint32_t>(primaryBlock.size()), &parseError) ||
        !parseError.isOk()) {
        return false;
    }
    if (!container.m_ktx || container.m_cubeMap || container.m_depth != 1U || container.m_numLayers != 1U) {
        return false;
    }
    if (container.m_width == 0U || container.m_height == 0U ||
        container.m_width > std::numeric_limits<std::uint16_t>::max() ||
        container.m_height > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }

    const auto format = static_cast<bimg::TextureFormat::Enum>(container.m_format);
    if (format < 0 || format >= bimg::TextureFormat::Count || !bimg::isCompressed(format)) {
        return false;
    }

    const auto width = static_cast<std::uint16_t>(container.m_width);
    const auto height = static_cast<std::uint16_t>(container.m_height);
    if (container.m_numMips != bimg::imageGetNumMips(format, width, height)) {
        return false;
    }

    const std::uint32_t payloadSize = bimg::imageGetSize(nullptr, width, height, 1U, false, true, 1U, format);
    const std::uint64_t expectedSize = static_cast<std::uint64_t>(container.m_offset) +
        static_cast<std::uint64_t>(container.m_numMips) * sizeof(std::uint32_t) + payloadSize;
    if (payloadSize == 0U || primaryBlock.size() < expectedSize) {
        return false;
    }

    RenderTextureGpuBlocks gpuBlocks{};
    gpuBlocks.format = static_cast<bgfx::TextureFormat::Enum>(format);
    gpuBlocks.blocks.reserve(payloadSize);
    for (std::uint8_t lod = 0U; lod < container.m_numMips; ++lod) {
        bimg::ImageMip mip{};
        if (!bimg::imageGetRawData(
                container, 0U, lod, primaryBlock.data(), static_cast<std::uint32_t>(primaryBlock.size()), mip) ||
            mip.m_data == nullptr) {
            return false;
        }
        // bimg walks the levels by sizes it derives from the header and does not check
        // them against the block, so each level must be shown to lie inside it.
        const auto blockBegin = reinterpret_cast<std::uintptr_t>(primaryBlock.data());
        const auto mipBegin = reinterpret_cast<std::uintptr_t>(mip.m_data);
        if (mipBegin < blockBegin || mipBegin - blockBegin > primaryBlock.size() ||
            mip.m_size > primaryBlock.size() - (mipBegin - blockBegin)) {
            return false;
        }
        gpuBlocks.blocks.insert(gpuBlocks.blocks.end(), mip.m_data, mip.m_data + mip.m_size);
    }
    if (gpuBlocks.blocks.size() != payloadSize) {
        return false;
    }

    RenderTextureAssetData asset{};
    asset.width = width;
    asset.height = height;
    asset.depth = 1U;
    asset.layers = 1U;
    asset.mipCount = container.m_numMips;
    asset.dimension = RenderTextureDimension::Texture2D;
    asset.colorSpace = container.m_srgb ? RenderTextureAssetColorSpace::Srgb : RenderTextureAssetColorSpace::Linear;
    asset.semantic = RenderTextureAssetSemantic::Unknown;
    asset.gpuBlocks = std::move(gpuBlocks);
    out = std::move(asset);
    return true;
}

} // namespace

} // namespace kb::render::bake
