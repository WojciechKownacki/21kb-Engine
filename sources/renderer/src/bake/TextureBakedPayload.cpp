#include "kb/render/bake/TextureBaker.hpp"

#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bx/error.h>

#include <limits>

namespace kb::render::bake {

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
    if (primaryBlock.empty() ||
        primaryBlock.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
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

} // namespace kb::render::bake
