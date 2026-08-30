#include "kb/render/bake/TextureBaker.hpp"

#include "runtime/RuntimeTextureMipChain.hpp"

#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bimg/encode.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <bx/readerwriter.h>

#include <array>
#include <cstddef>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace kb::render::bake {
namespace {

using kb::assets::bake::AssetBakeKey;
using kb::assets::bake::BakedAssetDescriptor;
using kb::assets::bake::BakedAssetSinkStatus;
using kb::assets::bake::BakeTargetProfile;
using kb::assets::bake::TextureCompressionFamily;

// bgfx mirrors bimg's TextureFormat enumeration one-for-one and casts between them throughout
// its own sources. Pinning the enumerators this baker names keeps a future divergence a build
// failure instead of a texture baked in the wrong format.
static_assert(static_cast<int>(bgfx::TextureFormat::BC1) == static_cast<int>(bimg::TextureFormat::BC1));
static_assert(static_cast<int>(bgfx::TextureFormat::BC3) == static_cast<int>(bimg::TextureFormat::BC3));
static_assert(static_cast<int>(bgfx::TextureFormat::BC7) == static_cast<int>(bimg::TextureFormat::BC7));
static_assert(static_cast<int>(bgfx::TextureFormat::ASTC4x4) == static_cast<int>(bimg::TextureFormat::ASTC4x4));
static_assert(static_cast<int>(bgfx::TextureFormat::RGBA8) == static_cast<int>(bimg::TextureFormat::RGBA8));
static_assert(static_cast<int>(bgfx::TextureFormat::Count) == static_cast<int>(bimg::TextureFormat::Count));

[[nodiscard]] constexpr bimg::TextureFormat::Enum ToBimgFormat(bgfx::TextureFormat::Enum format) noexcept {
    return static_cast<bimg::TextureFormat::Enum>(format);
}

struct ImageContainerDeleter {
    void operator()(bimg::ImageContainer* container) const noexcept {
        if (container != nullptr) {
            bimg::imageFree(container);
        }
    }
};

using ImageContainerPtr = std::unique_ptr<bimg::ImageContainer, ImageContainerDeleter>;

// bimg writes through bx's writer interface; the bake seam wants a byte block. Ten lines of
// adapter rather than a bx::MemoryBlock whose lifetime would have to outlive the writer.
class BlockWriter final : public bx::WriterI {
public:
    explicit BlockWriter(std::vector<std::uint8_t>& target) noexcept
        : target_{ &target } {}

    std::int32_t write(const void* data, std::int32_t size, bx::Error* error) override {
        BX_UNUSED(error);
        if (data == nullptr || size <= 0) {
            return 0;
        }
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        target_->insert(target_->end(), bytes, bytes + size);
        return size;
    }

private:
    std::vector<std::uint8_t>* target_;
};

// Whether anything actually samples this texture's alpha. The mesh shaders read a base colour
// as `.rgba` but a normal, metallic-roughness, occlusion or emissive map through `.xyz`,
// `.g`/`.b` or `.r` only, so an alpha channel on one of those is authoring residue and paying
// for it would double the block size for nothing. Unknown keeps the alpha: an unlabelled
// texture may be a base colour, and dropping a real alpha channel is not recoverable.
[[nodiscard]] constexpr bool SemanticSamplesAlpha(RenderTextureAssetSemantic semantic) noexcept {
    switch (semantic) {
    case RenderTextureAssetSemantic::Unknown:
    case RenderTextureAssetSemantic::BaseColor:
        return true;
    case RenderTextureAssetSemantic::Normal:
    case RenderTextureAssetSemantic::MetallicRoughness:
    case RenderTextureAssetSemantic::Occlusion:
    case RenderTextureAssetSemantic::Emissive:
        return false;
    }
    return true;
}

// Fixed field order, one byte each, little-endian by construction: the settings that are not
// already covered by the profile id or the source content. Positional, so no two different
// settings can serialize to the same bytes by swapping values between fields.
[[nodiscard]] std::array<std::uint8_t, 3U> SerializeTextureBakeSettings(
    TextureCompressionFamily family,
    const TextureBakeSettings& settings) noexcept {
    return {
        static_cast<std::uint8_t>(family),
        static_cast<std::uint8_t>(settings.semantic),
        static_cast<std::uint8_t>(settings.colorSpace),
    };
}

[[nodiscard]] constexpr std::uint32_t RoundUpToBlock(std::uint32_t value, std::uint32_t block) noexcept {
    return ((value + block - 1U) / block) * block;
}

// One mip level's RGBA8 pixels, grown to whole blocks by repeating the last row and column.
// Every encoder here reads a whole block whatever the level's true size is - nvtt's BC7 tile
// loop is unconditional - so the padding is what keeps those reads inside the buffer, and
// repeating the edge rather than leaving the padding arbitrary is what keeps the encoded block
// a function of the image alone.
void CopyLevelIntoBlockAlignedBuffer(
    const std::uint8_t* level,
    std::uint32_t levelWidth,
    std::uint32_t levelHeight,
    std::uint32_t paddedWidth,
    std::uint32_t paddedHeight,
    std::vector<std::uint8_t>& padded) {
    padded.assign(static_cast<std::size_t>(paddedWidth) * paddedHeight * 4U, 0U);
    for (std::uint32_t y = 0U; y < paddedHeight; ++y) {
        const std::uint32_t sourceY = y < levelHeight ? y : levelHeight - 1U;
        for (std::uint32_t x = 0U; x < paddedWidth; ++x) {
            const std::uint32_t sourceX = x < levelWidth ? x : levelWidth - 1U;
            const std::size_t source = (static_cast<std::size_t>(sourceY) * levelWidth + sourceX) * 4U;
            const std::size_t destination = (static_cast<std::size_t>(y) * paddedWidth + x) * 4U;
            padded[destination + 0U] = level[source + 0U];
            padded[destination + 1U] = level[source + 1U];
            padded[destination + 2U] = level[source + 2U];
            padded[destination + 3U] = level[source + 3U];
        }
    }
}

[[nodiscard]] bool ReadFileBytes(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes) {
    std::ifstream input{ path, std::ios::binary };
    if (!input) {
        return false;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size <= 0) {
        return false;
    }
    input.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    return static_cast<bool>(input);
}

} // namespace

std::string_view ToString(TextureBakeStatus status) noexcept {
    switch (status) {
    case TextureBakeStatus::Success:
        return "Success";
    case TextureBakeStatus::InvalidProfile:
        return "InvalidProfile";
    case TextureBakeStatus::FamilyNotInProfile:
        return "FamilyNotInProfile";
    case TextureBakeStatus::SourceUnreadable:
        return "SourceUnreadable";
    case TextureBakeStatus::SourceUndecodable:
        return "SourceUndecodable";
    case TextureBakeStatus::UnsupportedSourceShape:
        return "UnsupportedSourceShape";
    case TextureBakeStatus::UnalignedDimensions:
        return "UnalignedDimensions";
    case TextureBakeStatus::SrgbNormalMapRejected:
        return "SrgbNormalMapRejected";
    case TextureBakeStatus::EncodeFailed:
        return "EncodeFailed";
    case TextureBakeStatus::SinkRejected:
        return "SinkRejected";
    }
    return "Unknown";
}

bool TryChooseBakedTextureFormat(
    TextureCompressionFamily family,
    RenderTextureAssetSemantic semantic,
    bool sourceHasAlpha,
    bgfx::TextureFormat::Enum& format) noexcept {
    const bool needsAlpha = sourceHasAlpha && SemanticSamplesAlpha(semantic);
    switch (family) {
    case TextureCompressionFamily::BlockCompressedBaseline:
        // BC1 and BC3 are the whole family, and they share one RGB block: BC3 buys nothing but
        // an alpha block, so it is worth its doubled size only when the alpha is sampled.
        format = needsAlpha ? bgfx::TextureFormat::BC3 : bgfx::TextureFormat::BC1;
        return true;
    case TextureCompressionFamily::BlockCompressedExtended:
        // BC7 for everything. It carries alpha at the same rate as opaque, so the alpha
        // question does not arise, and its per-block partitioning is what keeps a normal map
        // out of the banding BC1's 5:6:5 endpoints put it in.
        format = bgfx::TextureFormat::BC7;
        return true;
    case TextureCompressionFamily::AdaptiveScalable:
        // ASTC4x4 for everything. The wider footprints are cheaper per texel, but their block
        // edges (5, 6, 10, 12) divide almost no real texture dimension, and the alignment rule
        // this baker enforces would then refuse the content rather than the footprint. 4x4
        // also puts ASTC at BC7's rate, so the two families are comparable at the same size.
        format = bgfx::TextureFormat::ASTC4x4;
        return true;
    }
    return false;
}

bool BakedTextureBlockFootprint(
    bgfx::TextureFormat::Enum format,
    std::uint16_t& blockWidth,
    std::uint16_t& blockHeight) noexcept {
    if (format < 0 || format >= bgfx::TextureFormat::Count) {
        return false;
    }
    const bimg::TextureFormat::Enum bimgFormat = ToBimgFormat(format);
    if (!bimg::isCompressed(bimgFormat)) {
        return false;
    }
    const bimg::ImageBlockInfo& info = bimg::getBlockInfo(bimgFormat);
    if (info.blockWidth == 0U || info.blockHeight == 0U) {
        return false;
    }
    blockWidth = info.blockWidth;
    blockHeight = info.blockHeight;
    return true;
}

AssetBakeKey MakeTextureBakeKey(
    std::span<const std::uint8_t> sourceBytes,
    const BakeTargetProfile& profile,
    TextureCompressionFamily family,
    const TextureBakeSettings& settings) {
    const std::array<std::uint8_t, 3U> serialized = SerializeTextureBakeSettings(family, settings);
    AssetBakeKey key{};
    key.sourceContentHash = kb::assets::bake::HashBakeBytes(sourceBytes);
    key.bakerId = std::string{ kTextureBakerId };
    key.bakerVersion = std::string{ kTextureBakerVersion };
    key.targetProfileId = std::string{ profile.identifier };
    key.settingsHash = kb::assets::bake::HashBakeBytes(std::span<const std::uint8_t>{ serialized });
    return key;
}

TextureBakeOutput BakeTextureBytes(
    std::span<const std::uint8_t> sourceBytes,
    const TextureBakeSettings& settings,
    const BakeTargetProfile& profile,
    TextureCompressionFamily family,
    kb::assets::bake::IBakedAssetSink& sink) {
    TextureBakeOutput output{};

    if (!kb::assets::bake::IsValidBakeTargetProfile(profile)) {
        output.status = TextureBakeStatus::InvalidProfile;
        return output;
    }
    if (static_cast<std::uint32_t>(family) >= kb::assets::bake::kTextureCompressionFamilyCount ||
        !HasTextureCompressionFamily(profile.textureCompressions, family)) {
        output.status = TextureBakeStatus::FamilyNotInProfile;
        return output;
    }
    if (settings.semantic == RenderTextureAssetSemantic::Normal &&
        settings.colorSpace == RenderTextureAssetColorSpace::Srgb) {
        output.status = TextureBakeStatus::SrgbNormalMapRejected;
        return output;
    }
    if (sourceBytes.empty() ||
        sourceBytes.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        output.status = TextureBakeStatus::SourceUnreadable;
        return output;
    }

    output.key = MakeTextureBakeKey(sourceBytes, profile, family, settings);

    bx::DefaultAllocator allocator;
    // The bx::Error is not optional here even though the pointer defaults to null. bimg opens
    // every parse with BX_ERROR_SCOPE, which substitutes a local Error when it is handed none
    // and then asserts in its destructor that nothing failed - so a source this baker is
    // supposed to refuse would trip a debug break instead of returning a status.
    bx::Error parseError;
    const ImageContainerPtr source{ bimg::imageParse(
        &allocator,
        sourceBytes.data(),
        static_cast<std::uint32_t>(sourceBytes.size()),
        bimg::TextureFormat::RGBA8,
        &parseError) };
    if (source == nullptr || !parseError.isOk()) {
        output.status = TextureBakeStatus::SourceUndecodable;
        return output;
    }
    if (source->m_cubeMap || source->m_depth != 1U || source->m_numLayers != 1U) {
        output.status = TextureBakeStatus::UnsupportedSourceShape;
        return output;
    }
    if (source->m_width == 0U || source->m_height == 0U ||
        source->m_width > std::numeric_limits<std::uint16_t>::max() ||
        source->m_height > std::numeric_limits<std::uint16_t>::max()) {
        output.status = TextureBakeStatus::UnsupportedSourceShape;
        return output;
    }

    const auto width = static_cast<std::uint16_t>(source->m_width);
    const auto height = static_cast<std::uint16_t>(source->m_height);

    bimg::ImageMip baseLevel{};
    if (!bimg::imageGetRawData(*source, 0U, 0U, source->m_data, source->m_size, baseLevel) ||
        baseLevel.m_data == nullptr ||
        baseLevel.m_format != bimg::TextureFormat::RGBA8 ||
        baseLevel.m_size != static_cast<std::uint32_t>(width) * height * 4U) {
        output.status = TextureBakeStatus::SourceUndecodable;
        return output;
    }

    bool sourceHasAlpha = false;
    for (std::uint32_t index = 3U; index < baseLevel.m_size; index += 4U) {
        if (baseLevel.m_data[index] != 0xFFU) {
            sourceHasAlpha = true;
            break;
        }
    }

    bgfx::TextureFormat::Enum format = bgfx::TextureFormat::Count;
    std::uint16_t blockWidth = 0U;
    std::uint16_t blockHeight = 0U;
    if (!TryChooseBakedTextureFormat(family, settings.semantic, sourceHasAlpha, format) ||
        !BakedTextureBlockFootprint(format, blockWidth, blockHeight)) {
        output.status = TextureBakeStatus::EncodeFailed;
        return output;
    }
    if (width % blockWidth != 0U || height % blockHeight != 0U) {
        output.status = TextureBakeStatus::UnalignedDimensions;
        return output;
    }

    // Colour space belongs to the mip chain, not to the format: averaging sRGB code values as
    // if they were light darkens every level below the first. A normal map is linear by
    // construction and an sRGB one was already refused above.
    const RenderTextureColorSpace mipColorSpace = settings.colorSpace == RenderTextureAssetColorSpace::Srgb
        ? RenderTextureColorSpace::Srgb
        : RenderTextureColorSpace::Linear;
    const std::optional<RuntimeTextureMipChain> chain = BuildRuntimeTexture2DMipChain(
        std::span<const std::uint8_t>{ baseLevel.m_data, baseLevel.m_size }, width, height, mipColorSpace);
    if (!chain.has_value() || chain->mipCount == 0U) {
        output.status = TextureBakeStatus::EncodeFailed;
        return output;
    }

    const bimg::TextureFormat::Enum bimgFormat = ToBimgFormat(format);
    if (chain->mipCount != bimg::imageGetNumMips(bimgFormat, width, height)) {
        output.status = TextureBakeStatus::EncodeFailed;
        return output;
    }
    const std::uint32_t encodedSize =
        bimg::imageGetSize(nullptr, width, height, 1U, false, true, 1U, bimgFormat);
    if (encodedSize == 0U) {
        output.status = TextureBakeStatus::EncodeFailed;
        return output;
    }

    // Encoded level by level into a buffer this baker owns. bimg's whole-container encode would
    // do the same walk, but it allocates its destination uninitialised and hands every level's
    // true size straight to the codec, and nvtt's BC7 tile loop reads a full 4x4 tile whatever
    // that size is - so a chain whose tail levels are smaller than one block reads past the
    // level it was given. Padding here is what makes the tail levels defined, and a buffer that
    // is written level by level in full is what makes two runs byte-identical.
    std::vector<std::uint8_t> blocks(encodedSize, 0U);
    std::vector<std::uint8_t> paddedLevel;
    std::size_t levelSourceOffset = 0U;
    std::size_t levelDestinationOffset = 0U;
    std::uint32_t levelWidth = width;
    std::uint32_t levelHeight = height;
    bx::Error encodeError;
    for (std::uint8_t lod = 0U; lod < chain->mipCount; ++lod) {
        const std::size_t levelBytes = static_cast<std::size_t>(levelWidth) * levelHeight * 4U;
        if (levelSourceOffset + levelBytes > chain->rgba8.size()) {
            output.status = TextureBakeStatus::EncodeFailed;
            return output;
        }
        const std::uint32_t paddedWidth = RoundUpToBlock(levelWidth, blockWidth);
        const std::uint32_t paddedHeight = RoundUpToBlock(levelHeight, blockHeight);
        const std::size_t encodedLevelBytes = static_cast<std::size_t>(paddedWidth / blockWidth) *
            (paddedHeight / blockHeight) * bimg::getBlockInfo(bimgFormat).blockSize;
        if (levelDestinationOffset + encodedLevelBytes > blocks.size()) {
            output.status = TextureBakeStatus::EncodeFailed;
            return output;
        }

        CopyLevelIntoBlockAlignedBuffer(
            chain->rgba8.data() + levelSourceOffset, levelWidth, levelHeight, paddedWidth, paddedHeight, paddedLevel);
        bimg::imageEncode(
            &allocator,
            blocks.data() + levelDestinationOffset,
            paddedLevel.data(),
            bimg::TextureFormat::RGBA8,
            paddedWidth,
            paddedHeight,
            1U,
            bimgFormat,
            bimg::Quality::Default,
            &encodeError);
        if (!encodeError.isOk()) {
            output.status = TextureBakeStatus::EncodeFailed;
            return output;
        }

        levelSourceOffset += levelBytes;
        levelDestinationOffset += encodedLevelBytes;
        levelWidth = levelWidth > 1U ? levelWidth / 2U : 1U;
        levelHeight = levelHeight > 1U ? levelHeight / 2U : 1U;
    }
    if (levelDestinationOffset != blocks.size()) {
        output.status = TextureBakeStatus::EncodeFailed;
        return output;
    }

    std::vector<std::uint8_t> primaryBlock;
    primaryBlock.reserve(blocks.size() + 128U);
    {
        BlockWriter writer{ primaryBlock };
        bx::Error writeError;
        bimg::imageWriteKtx(
            &writer,
            bimgFormat,
            false,
            width,
            height,
            1U,
            chain->mipCount,
            1U,
            settings.colorSpace == RenderTextureAssetColorSpace::Srgb,
            blocks.data(),
            &writeError);
        if (!writeError.isOk() || primaryBlock.empty()) {
            output.status = TextureBakeStatus::EncodeFailed;
            return output;
        }
    }

    BakedAssetDescriptor descriptor{};
    descriptor.key = output.key;
    descriptor.assetTypeId = std::string{ kTextureBakedAssetTypeId };
    output.sinkStatus = sink.BeginAsset(descriptor);
    if (output.sinkStatus != BakedAssetSinkStatus::Success) {
        output.status = TextureBakeStatus::SinkRejected;
        return output;
    }
    output.sinkStatus = sink.WritePrimaryBlock(primaryBlock, profile.packageBlockAlignmentBytes);
    if (output.sinkStatus != BakedAssetSinkStatus::Success) {
        sink.AbortAsset();
        output.status = TextureBakeStatus::SinkRejected;
        return output;
    }
    output.sinkStatus = sink.CommitAsset();
    if (output.sinkStatus != BakedAssetSinkStatus::Success) {
        sink.AbortAsset();
        output.status = TextureBakeStatus::SinkRejected;
        return output;
    }

    output.status = TextureBakeStatus::Success;
    output.format = format;
    output.width = width;
    output.height = height;
    output.mipCount = chain->mipCount;
    output.primaryBlock = std::move(primaryBlock);
    return output;
}

TextureBakeOutput BakeTexture(
    const std::filesystem::path& sourcePath,
    const TextureBakeSettings& settings,
    const BakeTargetProfile& profile,
    TextureCompressionFamily family,
    kb::assets::bake::IBakedAssetSink& sink) {
    std::vector<std::uint8_t> sourceBytes;
    if (!ReadFileBytes(sourcePath, sourceBytes)) {
        TextureBakeOutput output{};
        output.status = TextureBakeStatus::SourceUnreadable;
        return output;
    }
    return BakeTextureBytes(sourceBytes, settings, profile, family, sink);
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
    // A partial chain is refused rather than uploaded: bgfx sizes the memory it is handed from
    // a complete chain, so a container missing its tail is a bake this runtime cannot use.
    if (container.m_numMips != bimg::imageGetNumMips(format, width, height)) {
        return false;
    }

    const std::uint32_t payloadSize = bimg::imageGetSize(nullptr, width, height, 1U, false, true, 1U, format);
    // Each KTX level is preceded by its own 4-byte size field. Checked before any level is
    // read, because bimg's raw-data walk trusts the buffer to be long enough.
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
    // KTX carries no authoring semantic. It is not reconstructed from the format either: a
    // guess here would be indistinguishable from a declaration everywhere it is read.
    asset.semantic = RenderTextureAssetSemantic::Unknown;
    asset.gpuBlocks = std::move(gpuBlocks);
    out = std::move(asset);
    return true;
}

} // namespace kb::render::bake
