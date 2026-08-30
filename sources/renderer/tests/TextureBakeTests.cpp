#include "RendererTestSupport.hpp"

#include "engine/assets/bake/AssetBakeKey.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/BakedAssetSink.hpp"
#include "kb/render/bake/TextureBaker.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"

#include <bgfx/bgfx.h>
#include <bimg/bimg.h>
#include <bimg/encode.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <bx/readerwriter.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace kb::render::tests {
namespace {

using kb::assets::bake::AssetBakeDigest;
using kb::assets::bake::BakedAssetDescriptor;
using kb::assets::bake::BakeIndexWidth;
using kb::assets::bake::BakeTargetProfile;
using kb::assets::bake::LooseBakedAssetSink;
using kb::assets::bake::ShaderBakeBackend;
using kb::assets::bake::ShaderBakeBackendBit;
using kb::assets::bake::TextureCompressionFamily;
using kb::assets::bake::TextureCompressionFamilyBit;
using kb::render::bake::BakeTextureBytes;
using kb::render::bake::TextureBakeOutput;
using kb::render::bake::TextureBakeSettings;
using kb::render::bake::TextureBakeStatus;

class VectorWriter final : public bx::WriterI {
public:
    explicit VectorWriter(std::vector<std::uint8_t>& target) noexcept
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

// Test profiles are built here rather than taken from the shipped set: these tests are about
// the baker's reaction to a profile, so they must not go red when a shipped profile's
// alignment or backend list is revised for reasons of its own.
[[nodiscard]] BakeTargetProfile DesktopTestProfile() noexcept {
    return BakeTargetProfile{
        .identifier = "Test.Desktop",
        .textureCompressions = TextureCompressionFamilyBit(TextureCompressionFamily::BlockCompressedBaseline) |
            TextureCompressionFamilyBit(TextureCompressionFamily::BlockCompressedExtended),
        .shaderBackends = ShaderBakeBackendBit(ShaderBakeBackend::Spirv),
        .indexWidth = BakeIndexWidth::Bits32,
        .allowsThreeComponent16BitAttributes = true,
        .packageBlockAlignmentBytes = 256U,
        .mappedBlockAlignmentBytes = 65536U,
        .maxGeometryChunkBytes = 64ULL * 1024ULL * 1024ULL,
    };
}

[[nodiscard]] BakeTargetProfile MobileTestProfile() noexcept {
    return BakeTargetProfile{
        .identifier = "Test.Mobile",
        .textureCompressions = TextureCompressionFamilyBit(TextureCompressionFamily::AdaptiveScalable),
        .shaderBackends = ShaderBakeBackendBit(ShaderBakeBackend::Spirv),
        .indexWidth = BakeIndexWidth::Bits16,
        .allowsThreeComponent16BitAttributes = false,
        .packageBlockAlignmentBytes = 256U,
        .mappedBlockAlignmentBytes = 16384U,
        .maxGeometryChunkBytes = 16ULL * 1024ULL * 1024ULL,
    };
}

// Smooth gradients with a soft blob. Block compression is a fit to a small colour subspace, so
// a hostile high-frequency pattern would fail a quality floor for reasons that have nothing to
// do with whether the encoder ran; content a texture artist would recognise is what makes the
// floor mean something.
[[nodiscard]] std::vector<std::uint8_t> MakeRgba8Gradient(
    std::uint16_t width,
    std::uint16_t height,
    std::uint8_t alpha) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4U, 0U);
    for (std::uint16_t y = 0U; y < height; ++y) {
        for (std::uint16_t x = 0U; x < width; ++x) {
            const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4U;
            const std::uint32_t horizontal = width > 1U ? (x * 255U) / (width - 1U) : 0U;
            const std::uint32_t vertical = height > 1U ? (y * 255U) / (height - 1U) : 0U;
            pixels[index + 0U] = static_cast<std::uint8_t>(horizontal);
            pixels[index + 1U] = static_cast<std::uint8_t>(vertical);
            pixels[index + 2U] = static_cast<std::uint8_t>((horizontal + vertical) / 2U);
            pixels[index + 3U] = alpha;
        }
    }
    return pixels;
}

[[nodiscard]] std::vector<std::uint8_t> MakeRgba8Solid(
    std::uint16_t width,
    std::uint16_t height,
    std::array<std::uint8_t, 4U> color) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4U, 0U);
    for (std::size_t index = 0U; index < pixels.size(); index += 4U) {
        pixels[index + 0U] = color[0];
        pixels[index + 1U] = color[1];
        pixels[index + 2U] = color[2];
        pixels[index + 3U] = color[3];
    }
    return pixels;
}

// A real image container, not a hand-rolled blob: the source has to travel through the same
// bimg parse a shipped asset does.
[[nodiscard]] std::vector<std::uint8_t> MakePngSource(
    const std::vector<std::uint8_t>& rgba8,
    std::uint16_t width,
    std::uint16_t height) {
    std::vector<std::uint8_t> png;
    VectorWriter writer{ png };
    bx::Error error;
    bimg::imageWritePng(
        &writer,
        width,
        height,
        static_cast<std::uint32_t>(width) * 4U,
        rgba8.data(),
        bimg::TextureFormat::RGBA8,
        false,
        &error);
    Require(error.isOk() && !png.empty(), "Texture bake tests could not encode a PNG source fixture");
    return png;
}

[[nodiscard]] std::vector<std::uint8_t> DecodeLevelToRgba8(
    std::span<const std::uint8_t> blocks,
    std::uint16_t width,
    std::uint16_t height,
    bgfx::TextureFormat::Enum format) {
    bx::DefaultAllocator allocator;
    std::vector<std::uint8_t> rgba8(static_cast<std::size_t>(width) * height * 4U, 0U);
    bimg::imageDecodeToRgba8(
        &allocator,
        rgba8.data(),
        blocks.data(),
        width,
        height,
        static_cast<std::uint32_t>(width) * 4U,
        static_cast<bimg::TextureFormat::Enum>(format));
    return rgba8;
}

// Mean absolute error over every channel of every texel. imageQualityRgba8 is used alongside
// it, but that one is IQA's single-channel SSIM handed an RGBA8 buffer with a four-times row
// stride, so it only ever looks at the leading quarter of each row; this covers the rest.
[[nodiscard]] double MeanAbsoluteError(
    std::span<const std::uint8_t> reference,
    std::span<const std::uint8_t> decoded) {
    Require(reference.size() == decoded.size() && !reference.empty(), "Quality comparison got mismatched buffers");
    std::uint64_t total = 0U;
    for (std::size_t index = 0U; index < reference.size(); ++index) {
        const int difference = static_cast<int>(reference[index]) - static_cast<int>(decoded[index]);
        total += static_cast<std::uint64_t>(difference < 0 ? -difference : difference);
    }
    return static_cast<double>(total) / static_cast<double>(reference.size());
}

class TempStore {
public:
    explicit TempStore(const char* name) {
        std::error_code error;
        root_ = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(root_, error);
        std::filesystem::create_directories(root_, error);
        Require(!error, "Texture bake tests could not create a temporary bake store");
    }

    TempStore(const TempStore&) = delete;
    TempStore& operator=(const TempStore&) = delete;

    ~TempStore() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] const std::filesystem::path& Root() const noexcept { return root_; }

    [[nodiscard]] std::size_t PublishedFileCount() const {
        std::error_code error;
        std::size_t count = 0U;
        for (std::filesystem::recursive_directory_iterator it{ root_, error }, end; !error && it != end; ++it) {
            if (it->is_regular_file(error)) {
                ++count;
            }
        }
        return count;
    }

private:
    std::filesystem::path root_;
};

[[nodiscard]] std::vector<std::uint8_t> ReadAllBytes(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    Require(static_cast<bool>(input), "Texture bake tests could not open a published block");
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    Require(size > 0, "Texture bake tests read an empty published block");
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    Require(static_cast<bool>(input), "Texture bake tests could not read a published block");
    return bytes;
}

[[nodiscard]] std::uint8_t ExpectedMipCount(std::uint16_t width, std::uint16_t height) noexcept {
    std::uint8_t count = 1U;
    std::uint32_t levelWidth = width;
    std::uint32_t levelHeight = height;
    while (levelWidth > 1U || levelHeight > 1U) {
        levelWidth = levelWidth > 1U ? levelWidth / 2U : 1U;
        levelHeight = levelHeight > 1U ? levelHeight / 2U : 1U;
        ++count;
    }
    return count;
}

// ---------------------------------------------------------------------------------------
// Format choice: the family picks the format, the semantic picks only whether alpha is paid
// for. Every row is asserted through the public chooser AND, for the interesting ones, through
// a real bake, so an implementation that satisfies the table and then ignores it still fails.
void RunBakedTextureFormatChoiceTest() {
    struct Case {
        TextureCompressionFamily family;
        RenderTextureAssetSemantic semantic;
        bool sourceHasAlpha;
        bgfx::TextureFormat::Enum expected;
        const char* label;
    };

    const std::array cases{
        Case{ TextureCompressionFamily::BlockCompressedBaseline, RenderTextureAssetSemantic::BaseColor, false,
            bgfx::TextureFormat::BC1, "baseline opaque base colour is BC1" },
        Case{ TextureCompressionFamily::BlockCompressedBaseline, RenderTextureAssetSemantic::BaseColor, true,
            bgfx::TextureFormat::BC3, "baseline base colour with alpha is BC3" },
        Case{ TextureCompressionFamily::BlockCompressedBaseline, RenderTextureAssetSemantic::Unknown, true,
            bgfx::TextureFormat::BC3, "baseline unknown semantic keeps a real alpha channel" },
        Case{ TextureCompressionFamily::BlockCompressedBaseline, RenderTextureAssetSemantic::Normal, true,
            bgfx::TextureFormat::BC1, "baseline normal map does not pay for an alpha nothing samples" },
        Case{ TextureCompressionFamily::BlockCompressedBaseline, RenderTextureAssetSemantic::Occlusion, true,
            bgfx::TextureFormat::BC1, "baseline occlusion map does not pay for an alpha nothing samples" },
        Case{ TextureCompressionFamily::BlockCompressedBaseline, RenderTextureAssetSemantic::MetallicRoughness, true,
            bgfx::TextureFormat::BC1, "baseline metallic-roughness does not pay for an alpha nothing samples" },
        Case{ TextureCompressionFamily::BlockCompressedBaseline, RenderTextureAssetSemantic::Emissive, true,
            bgfx::TextureFormat::BC1, "baseline emissive does not pay for an alpha nothing samples" },
        Case{ TextureCompressionFamily::BlockCompressedExtended, RenderTextureAssetSemantic::BaseColor, false,
            bgfx::TextureFormat::BC7, "extended base colour is BC7" },
        Case{ TextureCompressionFamily::BlockCompressedExtended, RenderTextureAssetSemantic::Normal, true,
            bgfx::TextureFormat::BC7, "extended normal map is BC7, never a two-channel format" },
        Case{ TextureCompressionFamily::AdaptiveScalable, RenderTextureAssetSemantic::BaseColor, true,
            bgfx::TextureFormat::ASTC4x4, "ASTC base colour is ASTC4x4" },
        Case{ TextureCompressionFamily::AdaptiveScalable, RenderTextureAssetSemantic::Normal, false,
            bgfx::TextureFormat::ASTC4x4, "ASTC normal map is ASTC4x4" },
    };

    for (const Case& testCase : cases) {
        bgfx::TextureFormat::Enum chosen = bgfx::TextureFormat::Count;
        Require(
            kb::render::bake::TryChooseBakedTextureFormat(
                testCase.family, testCase.semantic, testCase.sourceHasAlpha, chosen),
            testCase.label);
        if (chosen != testCase.expected) {
            std::fprintf(stderr, "format choice: %s -> %s (expected %s)\n", testCase.label,
                bimg::getName(static_cast<bimg::TextureFormat::Enum>(chosen)),
                bimg::getName(static_cast<bimg::TextureFormat::Enum>(testCase.expected)));
        }
        Require(chosen == testCase.expected, testCase.label);
    }

    // The same table, reached the long way: through a bake, so the chooser's answer is what the
    // baked artifact actually holds.
    const std::uint16_t edge = 16U;
    const std::vector<std::uint8_t> opaque = MakeRgba8Gradient(edge, edge, 0xFFU);
    const std::vector<std::uint8_t> translucent = MakeRgba8Gradient(edge, edge, 0x80U);
    const std::vector<std::uint8_t> opaquePng = MakePngSource(opaque, edge, edge);
    const std::vector<std::uint8_t> translucentPng = MakePngSource(translucent, edge, edge);

    TempStore store{ "21kb_texture_bake_format_choice" };
    LooseBakedAssetSink sink{ store.Root() };

    const TextureBakeOutput opaqueBaseColor = BakeTextureBytes(
        opaquePng,
        TextureBakeSettings{ RenderTextureAssetSemantic::BaseColor, RenderTextureAssetColorSpace::Srgb },
        DesktopTestProfile(),
        TextureCompressionFamily::BlockCompressedBaseline,
        sink);
    Require(opaqueBaseColor.status == TextureBakeStatus::Success && opaqueBaseColor.format == bgfx::TextureFormat::BC1,
        "Baking an opaque base colour for the baseline family did not produce BC1");

    const TextureBakeOutput translucentBaseColor = BakeTextureBytes(
        translucentPng,
        TextureBakeSettings{ RenderTextureAssetSemantic::BaseColor, RenderTextureAssetColorSpace::Srgb },
        DesktopTestProfile(),
        TextureCompressionFamily::BlockCompressedBaseline,
        sink);
    Require(translucentBaseColor.status == TextureBakeStatus::Success &&
            translucentBaseColor.format == bgfx::TextureFormat::BC3,
        "Baking a base colour with alpha for the baseline family did not produce BC3");

    const TextureBakeOutput translucentNormal = BakeTextureBytes(
        translucentPng,
        TextureBakeSettings{ RenderTextureAssetSemantic::Normal, RenderTextureAssetColorSpace::Linear },
        DesktopTestProfile(),
        TextureCompressionFamily::BlockCompressedBaseline,
        sink);
    Require(translucentNormal.status == TextureBakeStatus::Success &&
            translucentNormal.format == bgfx::TextureFormat::BC1,
        "Baking a normal map with a stray alpha channel must not upgrade it to BC3");

    const TextureBakeOutput mobile = BakeTextureBytes(
        translucentPng,
        TextureBakeSettings{ RenderTextureAssetSemantic::BaseColor, RenderTextureAssetColorSpace::Srgb },
        MobileTestProfile(),
        TextureCompressionFamily::AdaptiveScalable,
        sink);
    Require(mobile.status == TextureBakeStatus::Success && mobile.format == bgfx::TextureFormat::ASTC4x4,
        "Baking for the ASTC family did not produce ASTC4x4");

    // The family drives the choice, so one source baked for two families of one profile lands
    // in two different formats and two different artifacts.
    Require(opaqueBaseColor.key.Digest() != mobile.key.Digest(),
        "Two compression families of the same source must not share a bake key");
}

// ---------------------------------------------------------------------------------------
// Determinism: the same source and the same arguments produce the same bytes, twice, into two
// independent stores. Run for BC7 and ASTC, the two encoders with the most internal state.
void RunBakedTextureDeterminismTest() {
    const std::uint16_t edge = 16U;
    const std::vector<std::uint8_t> png = MakePngSource(MakeRgba8Gradient(edge, edge, 0xC0U), edge, edge);

    struct Case {
        BakeTargetProfile profile;
        TextureCompressionFamily family;
        const char* label;
    };
    const std::array cases{
        Case{ DesktopTestProfile(), TextureCompressionFamily::BlockCompressedExtended, "BC7" },
        Case{ DesktopTestProfile(), TextureCompressionFamily::BlockCompressedBaseline, "BC3" },
        Case{ MobileTestProfile(), TextureCompressionFamily::AdaptiveScalable, "ASTC4x4" },
    };

    for (const Case& testCase : cases) {
        const TextureBakeSettings settings{
            RenderTextureAssetSemantic::BaseColor, RenderTextureAssetColorSpace::Srgb
        };

        TempStore firstStore{ "21kb_texture_bake_determinism_a" };
        TempStore secondStore{ "21kb_texture_bake_determinism_b" };
        LooseBakedAssetSink firstSink{ firstStore.Root() };
        LooseBakedAssetSink secondSink{ secondStore.Root() };

        const TextureBakeOutput first =
            BakeTextureBytes(png, settings, testCase.profile, testCase.family, firstSink);
        const TextureBakeOutput second =
            BakeTextureBytes(png, settings, testCase.profile, testCase.family, secondSink);
        Require(first.status == TextureBakeStatus::Success && second.status == TextureBakeStatus::Success,
            "Determinism test could not bake the source");
        Require(first.key.Digest() == second.key.Digest(), "Two identical bakes produced different keys");
        Require(!first.primaryBlock.empty() && first.primaryBlock == second.primaryBlock,
            "Two identical bakes produced different bytes");

        // ... and the same bytes came out of the store, not just out of the return value.
        BakedAssetDescriptor descriptor{};
        descriptor.key = first.key;
        descriptor.assetTypeId = std::string{ kb::render::bake::kTextureBakedAssetTypeId };
        const std::filesystem::path firstBlock =
            firstSink.BlockPath(descriptor, kb::assets::bake::kBakedAssetPrimaryBlockName);
        const std::filesystem::path secondBlock =
            secondSink.BlockPath(descriptor, kb::assets::bake::kBakedAssetPrimaryBlockName);
        Require(!firstBlock.empty() && !secondBlock.empty(), "Bake store did not name the primary block");
        const std::vector<std::uint8_t> firstBytes = ReadAllBytes(firstBlock);
        Require(firstBytes == first.primaryBlock, "The published block is not what the baker returned");
        Require(firstBytes == ReadAllBytes(secondBlock), "Two identical bakes published different bytes");

        // A repeat bake into a store that already holds the artifact leaves it exactly as it is.
        const TextureBakeOutput repeat =
            BakeTextureBytes(png, settings, testCase.profile, testCase.family, firstSink);
        Require(repeat.status == TextureBakeStatus::Success, "Re-baking an unchanged source failed");
        Require(ReadAllBytes(firstBlock) == firstBytes, "Re-baking an unchanged source rewrote the artifact");
        static_cast<void>(testCase.label);
    }
}

// ---------------------------------------------------------------------------------------
// The key moves for every input that changes the output, and only for those.
void RunBakedTextureBakeKeyTest() {
    const std::uint16_t edge = 8U;
    const std::vector<std::uint8_t> pngA = MakePngSource(MakeRgba8Gradient(edge, edge, 0xFFU), edge, edge);
    const std::vector<std::uint8_t> pngB =
        MakePngSource(MakeRgba8Solid(edge, edge, { 10U, 20U, 30U, 255U }), edge, edge);

    const TextureBakeSettings baseSettings{
        RenderTextureAssetSemantic::BaseColor, RenderTextureAssetColorSpace::Srgb
    };
    const BakeTargetProfile desktop = DesktopTestProfile();
    const BakeTargetProfile mobile = MobileTestProfile();

    const AssetBakeDigest reference = kb::render::bake::MakeTextureBakeKey(
        pngA, desktop, TextureCompressionFamily::BlockCompressedBaseline, baseSettings).Digest();

    Require(reference == kb::render::bake::MakeTextureBakeKey(
                             pngA, desktop, TextureCompressionFamily::BlockCompressedBaseline, baseSettings)
                             .Digest(),
        "The bake key is not a function of its inputs alone");

    Require(reference != kb::render::bake::MakeTextureBakeKey(
                             pngB, desktop, TextureCompressionFamily::BlockCompressedBaseline, baseSettings)
                             .Digest(),
        "Changing the source content did not move the bake key");

    Require(reference != kb::render::bake::MakeTextureBakeKey(
                             pngA, mobile, TextureCompressionFamily::AdaptiveScalable, baseSettings)
                             .Digest(),
        "Changing the target profile did not move the bake key");

    Require(reference != kb::render::bake::MakeTextureBakeKey(
                             pngA, desktop, TextureCompressionFamily::BlockCompressedExtended, baseSettings)
                             .Digest(),
        "Changing the compression family did not move the bake key");

    Require(reference != kb::render::bake::MakeTextureBakeKey(
                             pngA,
                             desktop,
                             TextureCompressionFamily::BlockCompressedBaseline,
                             TextureBakeSettings{ RenderTextureAssetSemantic::Emissive,
                                 RenderTextureAssetColorSpace::Srgb })
                             .Digest(),
        "Changing the semantic alone did not move the bake key");

    Require(reference != kb::render::bake::MakeTextureBakeKey(
                             pngA,
                             desktop,
                             TextureCompressionFamily::BlockCompressedBaseline,
                             TextureBakeSettings{ RenderTextureAssetSemantic::BaseColor,
                                 RenderTextureAssetColorSpace::Linear })
                             .Digest(),
        "Changing the colour space did not move the bake key");

    // No cross-field aliasing: two settings that carry the same numbers in different fields
    // must not serialize to the same stream. Semantic 2 is Normal, colour space 2 is Srgb.
    const AssetBakeDigest semanticTwo = kb::render::bake::MakeTextureBakeKey(
        pngA,
        desktop,
        TextureCompressionFamily::BlockCompressedBaseline,
        TextureBakeSettings{ RenderTextureAssetSemantic::Normal, RenderTextureAssetColorSpace::Unknown })
                                            .Digest();
    const AssetBakeDigest colorSpaceTwo = kb::render::bake::MakeTextureBakeKey(
        pngA,
        desktop,
        TextureCompressionFamily::BlockCompressedBaseline,
        TextureBakeSettings{ RenderTextureAssetSemantic::Unknown, RenderTextureAssetColorSpace::Srgb })
                                              .Digest();
    Require(semanticTwo != colorSpaceTwo,
        "Two settings that swap the same values between fields collided on one bake key");

    // The baker's identity is in the key, so a texture bake never collides with another baker's
    // artifact built from the same bytes.
    const kb::assets::bake::AssetBakeKey key = kb::render::bake::MakeTextureBakeKey(
        pngA, desktop, TextureCompressionFamily::BlockCompressedBaseline, baseSettings);
    Require(key.IsValid(), "The texture bake key is not a valid bake-store key");
    Require(key.bakerId == kb::render::bake::kTextureBakerId, "The texture bake key does not carry the baker id");
    Require(key.bakerVersion == kb::render::bake::kTextureBakerVersion,
        "The texture bake key does not carry the baker version");
    Require(key.targetProfileId == desktop.identifier, "The texture bake key does not carry the profile id");

    kb::assets::bake::AssetBakeKey bumped = key;
    bumped.bakerVersion = "2";
    Require(bumped.Digest() != reference, "Bumping the baker version did not move the bake key");
}

// ---------------------------------------------------------------------------------------
// The chain is complete, its level count is the one the runtime expects, and every level is
// really there in the payload.
void RunBakedTextureMipChainTest() {
    struct Case {
        std::uint16_t width;
        std::uint16_t height;
    };
    const std::array cases{ Case{ 32U, 32U }, Case{ 32U, 8U }, Case{ 12U, 12U } };

    TempStore store{ "21kb_texture_bake_mips" };
    LooseBakedAssetSink sink{ store.Root() };

    for (const Case& testCase : cases) {
        const std::vector<std::uint8_t> png =
            MakePngSource(MakeRgba8Gradient(testCase.width, testCase.height, 0xFFU), testCase.width, testCase.height);
        const TextureBakeOutput baked = BakeTextureBytes(
            png,
            TextureBakeSettings{ RenderTextureAssetSemantic::BaseColor, RenderTextureAssetColorSpace::Srgb },
            DesktopTestProfile(),
            TextureCompressionFamily::BlockCompressedBaseline,
            sink);
        Require(baked.status == TextureBakeStatus::Success, "Mip chain test could not bake the source");
        Require(baked.mipCount == ExpectedMipCount(testCase.width, testCase.height),
            "The baked chain does not run from the base level down to 1x1");

        RenderTextureAssetData asset{};
        Require(kb::render::bake::ReadBakedTexture(baked.primaryBlock, asset),
            "The baked primary block could not be read back");
        Require(asset.mipCount == baked.mipCount, "The read-back chain lost levels");
        Require(asset.gpuBlocks.has_value() && asset.gpuBlocks->format == baked.format,
            "The read-back texture did not keep its baked format");

        // Level sizes computed here, from the format's block footprint, rather than borrowed
        // from the baker.
        std::uint16_t blockWidth = 0U;
        std::uint16_t blockHeight = 0U;
        Require(kb::render::bake::BakedTextureBlockFootprint(baked.format, blockWidth, blockHeight),
            "Mip chain test could not resolve the block footprint");
        const std::uint32_t blockSize =
            bimg::getBlockInfo(static_cast<bimg::TextureFormat::Enum>(baked.format)).blockSize;
        std::size_t expectedPayload = 0U;
        std::uint32_t levelWidth = testCase.width;
        std::uint32_t levelHeight = testCase.height;
        for (std::uint8_t lod = 0U; lod < baked.mipCount; ++lod) {
            const std::uint32_t blocksX = (levelWidth + blockWidth - 1U) / blockWidth;
            const std::uint32_t blocksY = (levelHeight + blockHeight - 1U) / blockHeight;
            expectedPayload += static_cast<std::size_t>(blocksX) * blocksY * blockSize;
            levelWidth = levelWidth > 1U ? levelWidth / 2U : 1U;
            levelHeight = levelHeight > 1U ? levelHeight / 2U : 1U;
        }
        Require(asset.gpuBlocks->blocks.size() == expectedPayload,
            "The baked payload is not the sum of its levels' block counts");
    }

    // Every level of a flat source decodes back to that colour: a chain whose tail levels held
    // uninitialised memory, or that repeated level 0 instead of downsampling, fails here.
    const std::uint16_t edge = 32U;
    const std::array<std::uint8_t, 4U> solid{ 200U, 100U, 50U, 255U };
    const std::vector<std::uint8_t> flatPng = MakePngSource(MakeRgba8Solid(edge, edge, solid), edge, edge);
    const TextureBakeOutput flat = BakeTextureBytes(
        flatPng,
        TextureBakeSettings{ RenderTextureAssetSemantic::BaseColor, RenderTextureAssetColorSpace::Linear },
        DesktopTestProfile(),
        TextureCompressionFamily::BlockCompressedBaseline,
        sink);
    Require(flat.status == TextureBakeStatus::Success, "Mip chain test could not bake the flat source");

    RenderTextureAssetData flatAsset{};
    Require(kb::render::bake::ReadBakedTexture(flat.primaryBlock, flatAsset), "Flat bake could not be read back");
    std::size_t levelOffset = 0U;
    std::uint32_t levelWidth = edge;
    std::uint32_t levelHeight = edge;
    for (std::uint8_t lod = 0U; lod < flatAsset.mipCount; ++lod) {
        const std::uint32_t paddedWidth = ((levelWidth + 3U) / 4U) * 4U;
        const std::uint32_t paddedHeight = ((levelHeight + 3U) / 4U) * 4U;
        const std::size_t levelBytes = static_cast<std::size_t>(paddedWidth / 4U) * (paddedHeight / 4U) * 8U;
        const std::vector<std::uint8_t> decoded = DecodeLevelToRgba8(
            std::span<const std::uint8_t>{ flatAsset.gpuBlocks->blocks.data() + levelOffset, levelBytes },
            static_cast<std::uint16_t>(paddedWidth),
            static_cast<std::uint16_t>(paddedHeight),
            flat.format);
        for (std::size_t index = 0U; index < decoded.size(); index += 4U) {
            const int deltaR = static_cast<int>(decoded[index + 0U]) - static_cast<int>(solid[0]);
            const int deltaG = static_cast<int>(decoded[index + 1U]) - static_cast<int>(solid[1]);
            const int deltaB = static_cast<int>(decoded[index + 2U]) - static_cast<int>(solid[2]);
            Require(std::abs(deltaR) <= 8 && std::abs(deltaG) <= 8 && std::abs(deltaB) <= 8,
                "A baked mip level does not hold the flat colour the whole source had");
        }
        levelOffset += levelBytes;
        levelWidth = levelWidth > 1U ? levelWidth / 2U : 1U;
        levelHeight = levelHeight > 1U ? levelHeight / 2U : 1U;
    }
    Require(levelOffset == flatAsset.gpuBlocks->blocks.size(), "The flat chain has bytes no level accounts for");
}

// ---------------------------------------------------------------------------------------
// Colour space is honoured where it matters: a chain averaged in the wrong space is darker.
void RunBakedTextureColorSpaceTest() {
    const std::uint16_t edge = 16U;
    // Half black, half white: the correct level-1 average of 0 and 255 in sRGB is around 188,
    // and around 128 if the code values are averaged as if they were light.
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(edge) * edge * 4U, 0U);
    for (std::uint16_t y = 0U; y < edge; ++y) {
        for (std::uint16_t x = 0U; x < edge; ++x) {
            const std::size_t index = (static_cast<std::size_t>(y) * edge + x) * 4U;
            const std::uint8_t value = (x % 2U) == 0U ? 0U : 255U;
            pixels[index + 0U] = value;
            pixels[index + 1U] = value;
            pixels[index + 2U] = value;
            pixels[index + 3U] = 255U;
        }
    }
    const std::vector<std::uint8_t> png = MakePngSource(pixels, edge, edge);

    TempStore store{ "21kb_texture_bake_color_space" };
    LooseBakedAssetSink sink{ store.Root() };

    const TextureBakeOutput linear = BakeTextureBytes(
        png,
        TextureBakeSettings{ RenderTextureAssetSemantic::BaseColor, RenderTextureAssetColorSpace::Linear },
        DesktopTestProfile(),
        TextureCompressionFamily::BlockCompressedBaseline,
        sink);
    const TextureBakeOutput srgb = BakeTextureBytes(
        png,
        TextureBakeSettings{ RenderTextureAssetSemantic::BaseColor, RenderTextureAssetColorSpace::Srgb },
        DesktopTestProfile(),
        TextureCompressionFamily::BlockCompressedBaseline,
        sink);
    Require(linear.status == TextureBakeStatus::Success && srgb.status == TextureBakeStatus::Success,
        "Colour space test could not bake the source");
    Require(linear.primaryBlock != srgb.primaryBlock,
        "The declared colour space changed nothing about the baked bytes");

    RenderTextureAssetData linearAsset{};
    RenderTextureAssetData srgbAsset{};
    Require(kb::render::bake::ReadBakedTexture(linear.primaryBlock, linearAsset) &&
            kb::render::bake::ReadBakedTexture(srgb.primaryBlock, srgbAsset),
        "Colour space test could not read the baked containers back");
    Require(linearAsset.colorSpace == RenderTextureAssetColorSpace::Linear &&
            srgbAsset.colorSpace == RenderTextureAssetColorSpace::Srgb,
        "The baked container did not carry its colour space back");

    // Level 1 of the sRGB bake must be brighter than level 1 of the linear one.
    const std::size_t level0Bytes = static_cast<std::size_t>(edge / 4U) * (edge / 4U) * 8U;
    const std::uint16_t half = edge / 2U;
    const std::size_t level1Bytes = static_cast<std::size_t>(half / 4U) * (half / 4U) * 8U;
    const std::vector<std::uint8_t> linearLevel1 = DecodeLevelToRgba8(
        std::span<const std::uint8_t>{ linearAsset.gpuBlocks->blocks.data() + level0Bytes, level1Bytes },
        half, half, linear.format);
    const std::vector<std::uint8_t> srgbLevel1 = DecodeLevelToRgba8(
        std::span<const std::uint8_t>{ srgbAsset.gpuBlocks->blocks.data() + level0Bytes, level1Bytes },
        half, half, srgb.format);

    std::uint64_t linearSum = 0U;
    std::uint64_t srgbSum = 0U;
    for (std::size_t index = 0U; index < linearLevel1.size(); index += 4U) {
        linearSum += linearLevel1[index];
        srgbSum += srgbLevel1[index];
    }
    const double linearMean = static_cast<double>(linearSum) / static_cast<double>(linearLevel1.size() / 4U);
    const double srgbMean = static_cast<double>(srgbSum) / static_cast<double>(srgbLevel1.size() / 4U);
    Require(linearMean > 110.0 && linearMean < 145.0,
        "A linear chain did not average the two code values arithmetically");
    Require(srgbMean > 165.0 && srgbMean < 210.0,
        "An sRGB chain did not average its texels as light before re-encoding them");
}

// ---------------------------------------------------------------------------------------
// The dimension rule is enforced, not papered over.
void RunBakedTextureDimensionRuleTest() {
    TempStore store{ "21kb_texture_bake_dimensions" };
    LooseBakedAssetSink sink{ store.Root() };

    struct Case {
        std::uint16_t width;
        std::uint16_t height;
        bool bakeable;
        const char* label;
    };
    const std::array cases{
        Case{ 16U, 16U, true, "16x16 is a whole number of 4x4 blocks" },
        Case{ 12U, 8U, true, "12x8 is a whole number of 4x4 blocks" },
        Case{ 6U, 6U, false, "6x6 is not a whole number of 4x4 blocks" },
        Case{ 16U, 6U, false, "a height that is not a multiple of the block footprint is refused" },
        Case{ 6U, 16U, false, "a width that is not a multiple of the block footprint is refused" },
        Case{ 15U, 15U, false, "15x15 is not a whole number of 4x4 blocks" },
    };

    for (const Case& testCase : cases) {
        const std::vector<std::uint8_t> png =
            MakePngSource(MakeRgba8Gradient(testCase.width, testCase.height, 0xFFU), testCase.width, testCase.height);
        for (const TextureCompressionFamily family : { TextureCompressionFamily::BlockCompressedBaseline,
                 TextureCompressionFamily::BlockCompressedExtended }) {
            const TextureBakeOutput baked = BakeTextureBytes(
                png,
                TextureBakeSettings{ RenderTextureAssetSemantic::BaseColor, RenderTextureAssetColorSpace::Srgb },
                DesktopTestProfile(),
                family,
                sink);
            if (testCase.bakeable) {
                Require(baked.status == TextureBakeStatus::Success, testCase.label);
            } else {
                Require(baked.status == TextureBakeStatus::UnalignedDimensions, testCase.label);
                Require(baked.primaryBlock.empty(), "A refused bake still produced bytes");
            }
        }
    }

    // A refused bake publishes nothing at all.
    TempStore refusedStore{ "21kb_texture_bake_dimensions_refused" };
    LooseBakedAssetSink refusedSink{ refusedStore.Root() };
    const std::vector<std::uint8_t> odd = MakePngSource(MakeRgba8Gradient(6U, 6U, 0xFFU), 6U, 6U);
    const TextureBakeOutput refused = BakeTextureBytes(
        odd,
        TextureBakeSettings{ RenderTextureAssetSemantic::BaseColor, RenderTextureAssetColorSpace::Srgb },
        DesktopTestProfile(),
        TextureCompressionFamily::BlockCompressedBaseline,
        refusedSink);
    Require(refused.status == TextureBakeStatus::UnalignedDimensions, "A misaligned source was not refused");
    Require(refusedStore.PublishedFileCount() == 0U, "A refused bake left an artifact in the store");
}

// ---------------------------------------------------------------------------------------
// Everything else the baker refuses, each with its own status and with nothing published.
void RunBakedTextureRefusalTest() {
    TempStore store{ "21kb_texture_bake_refusals" };
    LooseBakedAssetSink sink{ store.Root() };

    const std::uint16_t edge = 8U;
    const std::vector<std::uint8_t> png = MakePngSource(MakeRgba8Gradient(edge, edge, 0xFFU), edge, edge);
    const TextureBakeSettings baseColor{
        RenderTextureAssetSemantic::BaseColor, RenderTextureAssetColorSpace::Srgb
    };

    // A family the profile does not carry.
    Require(BakeTextureBytes(png, baseColor, DesktopTestProfile(), TextureCompressionFamily::AdaptiveScalable, sink)
                    .status == TextureBakeStatus::FamilyNotInProfile,
        "A family the profile does not carry was baked anyway");
    Require(BakeTextureBytes(png, baseColor, MobileTestProfile(),
                    TextureCompressionFamily::BlockCompressedExtended, sink)
                    .status == TextureBakeStatus::FamilyNotInProfile,
        "A family the mobile profile does not carry was baked anyway");

    // A profile that is not bakeable at all.
    BakeTargetProfile broken = DesktopTestProfile();
    broken.packageBlockAlignmentBytes = 100U; // not a power of two
    Require(BakeTextureBytes(png, baseColor, broken, TextureCompressionFamily::BlockCompressedBaseline, sink)
                    .status == TextureBakeStatus::InvalidProfile,
        "An invalid profile was baked for");

    // A normal map declared as sRGB.
    Require(BakeTextureBytes(png,
                    TextureBakeSettings{ RenderTextureAssetSemantic::Normal, RenderTextureAssetColorSpace::Srgb },
                    DesktopTestProfile(),
                    TextureCompressionFamily::BlockCompressedBaseline,
                    sink)
                    .status == TextureBakeStatus::SrgbNormalMapRejected,
        "An sRGB normal map was baked instead of refused");
    // ... but a linear one, and one whose colour space was never declared, are fine.
    Require(BakeTextureBytes(png,
                    TextureBakeSettings{ RenderTextureAssetSemantic::Normal, RenderTextureAssetColorSpace::Linear },
                    DesktopTestProfile(),
                    TextureCompressionFamily::BlockCompressedBaseline,
                    sink)
                    .status == TextureBakeStatus::Success,
        "A linear normal map was refused");

    // Bytes that are not an image, and no bytes at all.
    const std::vector<std::uint8_t> garbage(512U, 0x5AU);
    Require(BakeTextureBytes(garbage, baseColor, DesktopTestProfile(),
                    TextureCompressionFamily::BlockCompressedBaseline, sink)
                    .status == TextureBakeStatus::SourceUndecodable,
        "Bytes that are not an image were not refused");
    Require(BakeTextureBytes({}, baseColor, DesktopTestProfile(),
                    TextureCompressionFamily::BlockCompressedBaseline, sink)
                    .status == TextureBakeStatus::SourceUnreadable,
        "An empty source was not refused");

    // A cube map: a shape this baker does not claim.
    const std::uint16_t faceEdge = 8U;
    const std::vector<std::uint8_t> face = MakeRgba8Solid(faceEdge, faceEdge, { 8U, 16U, 32U, 255U });
    std::vector<std::uint8_t> cubeFaces;
    for (int side = 0; side < 6; ++side) {
        cubeFaces.insert(cubeFaces.end(), face.begin(), face.end());
    }
    std::vector<std::uint8_t> cubeKtx;
    {
        VectorWriter writer{ cubeKtx };
        bx::Error error;
        bimg::imageWriteKtx(&writer, bimg::TextureFormat::RGBA8, true, faceEdge, faceEdge, 1U, 1U, 1U, false,
            cubeFaces.data(), &error);
        Require(error.isOk() && !cubeKtx.empty(), "Refusal test could not build a cube map fixture");
    }
    Require(BakeTextureBytes(cubeKtx, baseColor, DesktopTestProfile(),
                    TextureCompressionFamily::BlockCompressedBaseline, sink)
                    .status == TextureBakeStatus::UnsupportedSourceShape,
        "A cube map was baked as if it were a 2D texture");

    // A file that is not there.
    Require(kb::render::bake::BakeTexture(store.Root() / "no_such_source.png", baseColor, DesktopTestProfile(),
                TextureCompressionFamily::BlockCompressedBaseline, sink)
                    .status == TextureBakeStatus::SourceUnreadable,
        "A missing source file was not refused");
}

// ---------------------------------------------------------------------------------------
// The encoded texture resembles the one that went in.
void RunBakedTextureQualityTest() {
    const std::uint16_t edge = 32U;
    const std::vector<std::uint8_t> reference = MakeRgba8Gradient(edge, edge, 0xFFU);
    const std::vector<std::uint8_t> png = MakePngSource(reference, edge, edge);

    TempStore store{ "21kb_texture_bake_quality" };
    LooseBakedAssetSink sink{ store.Root() };

    struct Case {
        BakeTargetProfile profile;
        TextureCompressionFamily family;
        double maxMeanAbsoluteError;
        float minStructuralSimilarity;
        const char* label;
    };
    const std::array cases{
        Case{ DesktopTestProfile(), TextureCompressionFamily::BlockCompressedBaseline, 4.0, 0.90F, "BC1" },
        Case{ MobileTestProfile(), TextureCompressionFamily::AdaptiveScalable, 4.0, 0.90F, "ASTC4x4" },
    };

    for (const Case& testCase : cases) {
        const TextureBakeOutput baked = BakeTextureBytes(
            png,
            TextureBakeSettings{ RenderTextureAssetSemantic::BaseColor, RenderTextureAssetColorSpace::Linear },
            testCase.profile,
            testCase.family,
            sink);
        Require(baked.status == TextureBakeStatus::Success, "Quality test could not bake the source");

        RenderTextureAssetData asset{};
        Require(kb::render::bake::ReadBakedTexture(baked.primaryBlock, asset), "Quality test could not read it back");
        const std::optional<RenderTextureAssetData> decoded = DecodeRenderTextureToRgba8(asset);
        Require(decoded.has_value() && decoded->rgba8.size() == reference.size(),
            "Quality test could not decode the baked base level");

        const double error = MeanAbsoluteError(reference, decoded->rgba8);
        const float similarity = bimg::imageQualityRgba8(reference.data(), decoded->rgba8.data(), edge, edge);
        if (error > testCase.maxMeanAbsoluteError || similarity < testCase.minStructuralSimilarity) {
            std::fprintf(stderr, "%s: mean abs error %.3f, ssim %.4f\n", testCase.label, error,
                static_cast<double>(similarity));
        }
        Require(error <= testCase.maxMeanAbsoluteError,
            "A baked texture does not resemble its source closely enough");
        Require(similarity >= testCase.minStructuralSimilarity,
            "A baked texture lost the structure of its source");

        // A negative control on the metric itself: an unrelated image must fail the same floor,
        // so a passing score means something.
        const std::vector<std::uint8_t> unrelated = MakeRgba8Solid(edge, edge, { 255U, 0U, 255U, 255U });
        Require(MeanAbsoluteError(reference, unrelated) > testCase.maxMeanAbsoluteError,
            "The quality floor is loose enough to pass an unrelated image");
    }
}

// ---------------------------------------------------------------------------------------
// The container round-trips, and a damaged one is refused rather than half-read.
void RunBakedTextureReadBackTest() {
    const std::uint16_t edge = 16U;
    const std::vector<std::uint8_t> png = MakePngSource(MakeRgba8Gradient(edge, edge, 0xFFU), edge, edge);

    TempStore store{ "21kb_texture_bake_readback" };
    LooseBakedAssetSink sink{ store.Root() };
    const TextureBakeOutput baked = BakeTextureBytes(
        png,
        TextureBakeSettings{ RenderTextureAssetSemantic::BaseColor, RenderTextureAssetColorSpace::Srgb },
        DesktopTestProfile(),
        TextureCompressionFamily::BlockCompressedBaseline,
        sink);
    Require(baked.status == TextureBakeStatus::Success, "Read-back test could not bake the source");

    RenderTextureAssetData asset{};
    Require(kb::render::bake::ReadBakedTexture(baked.primaryBlock, asset), "The baked container did not read back");
    Require(asset.width == edge && asset.height == edge && asset.depth == 1U && asset.layers == 1U &&
            asset.dimension == RenderTextureDimension::Texture2D,
        "The read-back texture lost its shape");
    Require(asset.gpuBlocks.has_value() && asset.gpuBlocks->format == baked.format && asset.rgba8.empty(),
        "The read-back texture is not the compressed payload the bake produced");

    // Truncation: every prefix short of the whole container is refused, never partly read.
    for (std::size_t length : { std::size_t{ 1U }, std::size_t{ 32U }, std::size_t{ 64U },
             baked.primaryBlock.size() / 2U, baked.primaryBlock.size() - 1U }) {
        RenderTextureAssetData damaged{};
        Require(!kb::render::bake::ReadBakedTexture(
                    std::span<const std::uint8_t>{ baked.primaryBlock.data(), length }, damaged),
            "A truncated baked container was read as if it were whole");
    }
    RenderTextureAssetData empty{};
    Require(!kb::render::bake::ReadBakedTexture({}, empty), "An empty block was read as a texture");

    // An uncompressed container is not a bake: the whole point is that nothing decodes it. It
    // carries a complete chain, so the only rule left to refuse it is the one being tested.
    std::vector<std::uint8_t> uncompressedKtx;
    {
        VectorWriter writer{ uncompressedKtx };
        bx::Error error;
        const std::uint32_t chainBytes =
            bimg::imageGetSize(nullptr, edge, edge, 1U, false, true, 1U, bimg::TextureFormat::RGBA8);
        const std::vector<std::uint8_t> pixels(chainBytes, 0x40U);
        bimg::imageWriteKtx(&writer,
            bimg::TextureFormat::RGBA8,
            false,
            edge,
            edge,
            1U,
            bimg::imageGetNumMips(bimg::TextureFormat::RGBA8, edge, edge),
            1U,
            false,
            pixels.data(),
            &error);
        Require(error.isOk(), "Read-back test could not build an uncompressed container");
    }
    RenderTextureAssetData uncompressed{};
    Require(!kb::render::bake::ReadBakedTexture(uncompressedKtx, uncompressed),
        "An uncompressed container was accepted as a baked texture");

    // A container that claims more levels than the image can have. The declared count must be
    // refused in its own right, before the payload is walked: bimg's raw-data walk reads each
    // level's length field at an offset it derives from the header, with no bound check of its
    // own, so an over-declared chain steps off the end of the block. The fixture is sized to
    // exactly the length such a header implies, so the length guard passes and only the
    // declared-count rule is left to refuse it.
    const std::uint32_t declaredMips = 255U;
    const std::uint32_t ktxHeaderBytes = 64U;
    const std::uint32_t completeChainBytes = bimg::imageGetSize(
        nullptr, edge, edge, 1U, false, true, 1U, static_cast<bimg::TextureFormat::Enum>(baked.format));
    std::vector<std::uint8_t> overDeclared = baked.primaryBlock;
    const std::size_t mipCountFieldOffset = 28U; // KTX 1.1: numberOfMipmapLevels
    overDeclared[mipCountFieldOffset + 0U] = static_cast<std::uint8_t>(declaredMips);
    overDeclared[mipCountFieldOffset + 1U] = 0U;
    overDeclared[mipCountFieldOffset + 2U] = 0U;
    overDeclared[mipCountFieldOffset + 3U] = 0U;
    overDeclared.resize(ktxHeaderBytes + declaredMips * sizeof(std::uint32_t) + completeChainBytes, 0U);
    RenderTextureAssetData overRead{};
    Require(!kb::render::bake::ReadBakedTexture(overDeclared, overRead),
        "A container claiming more mip levels than the image can have was read anyway");
}

// ---------------------------------------------------------------------------------------
// The RGBA8 fallback refuses what it cannot decode instead of handing back a checkerboard.
void RunBakedTextureFallbackGuardTest() {
    const std::uint16_t edge = 8U;

    // No baked payload: the fallback is a no-op, not a failure.
    RenderTextureAssetData plain{};
    plain.width = edge;
    plain.height = edge;
    plain.rgba8 = MakeRgba8Gradient(edge, edge, 0xFFU);
    const std::optional<RenderTextureAssetData> untouched = DecodeRenderTextureToRgba8(plain);
    Require(untouched.has_value() && untouched->rgba8 == plain.rgba8 && !untouched->gpuBlocks.has_value(),
        "The RGBA8 fallback did not pass an already-decoded texture through unchanged");

    // A payload that claims an uncompressed format is malformed, not a texture to salvage:
    // bimg's decoder fills the target with a checkerboard for a format it cannot handle.
    RenderTextureAssetData uncompressedPayload{};
    uncompressedPayload.width = edge;
    uncompressedPayload.height = edge;
    uncompressedPayload.gpuBlocks =
        RenderTextureGpuBlocks{ bgfx::TextureFormat::RGBA8, MakeRgba8Gradient(edge, edge, 0xFFU) };
    Require(!DecodeRenderTextureToRgba8(uncompressedPayload).has_value(),
        "The RGBA8 fallback accepted a payload that is not a block format");
    Require(SelectRenderTextureUploadPath(uncompressedPayload, true) == RenderTextureUploadPath::DecodedRgba8,
        "A payload that claims RGBA8 must not be taken as a compressed upload");

    // A payload too short for the level it claims.
    RenderTextureAssetData truncatedPayload{};
    truncatedPayload.width = edge;
    truncatedPayload.height = edge;
    truncatedPayload.gpuBlocks = RenderTextureGpuBlocks{ bgfx::TextureFormat::BC1, std::vector<std::uint8_t>(8U, 0U) };
    Require(!DecodeRenderTextureToRgba8(truncatedPayload).has_value(),
        "The RGBA8 fallback decoded a payload shorter than the level it claims");

    // A shape this decoder does not claim.
    RenderTextureAssetData cubePayload{};
    cubePayload.width = edge;
    cubePayload.height = edge;
    cubePayload.dimension = RenderTextureDimension::TextureCube;
    cubePayload.gpuBlocks = RenderTextureGpuBlocks{ bgfx::TextureFormat::BC1, std::vector<std::uint8_t>(4096U, 0U) };
    Require(!DecodeRenderTextureToRgba8(cubePayload).has_value(),
        "The RGBA8 fallback decoded a shape it does not handle as if it were a 2D texture");
}

// ---------------------------------------------------------------------------------------
// The runtime side: the device decides, and an unsupported format falls back rather than
// reaching bgfx.
void RunBakedTextureRuntimeUploadPathTest() {
    const std::uint16_t edge = 32U;
    const std::vector<std::uint8_t> reference = MakeRgba8Gradient(edge, edge, 0xFFU);
    const std::vector<std::uint8_t> png = MakePngSource(reference, edge, edge);

    TempStore store{ "21kb_texture_bake_upload_path" };
    LooseBakedAssetSink sink{ store.Root() };
    const TextureBakeOutput baked = BakeTextureBytes(
        png,
        TextureBakeSettings{ RenderTextureAssetSemantic::BaseColor, RenderTextureAssetColorSpace::Linear },
        DesktopTestProfile(),
        TextureCompressionFamily::BlockCompressedBaseline,
        sink);
    Require(baked.status == TextureBakeStatus::Success, "Upload path test could not bake the source");

    RenderTextureAssetData bakedAsset{};
    Require(kb::render::bake::ReadBakedTexture(baked.primaryBlock, bakedAsset), "Upload path test could not read it");

    Require(SelectRenderTextureUploadPath(bakedAsset, true) == RenderTextureUploadPath::GpuBlocks,
        "A device that supports the baked format must get the baked blocks");
    Require(SelectRenderTextureUploadPath(bakedAsset, false) == RenderTextureUploadPath::DecodedRgba8,
        "A device that does not support the baked format must not be handed the baked blocks");

    RenderTextureAssetData plain{};
    plain.width = edge;
    plain.height = edge;
    plain.rgba8 = reference;
    Require(SelectRenderTextureUploadPath(plain, true) == RenderTextureUploadPath::DecodedRgba8,
        "A texture with no baked payload has no compressed path to take");

    // The descriptor the resource registry builds carries the baked format and the baked level
    // count, not RGBA8 and one level.
    const RenderTextureDesc bakedDesc = bakedAsset.MakeDesc(nullptr, RenderTextureColorSpace::Linear);
    Require(bakedDesc.format == baked.format && bakedDesc.mipCount == baked.mipCount,
        "The texture descriptor did not carry the baked format and chain");
    const RenderTextureDesc plainDesc = plain.MakeDesc(nullptr, RenderTextureColorSpace::Linear);
    Require(plainDesc.format == bgfx::TextureFormat::RGBA8,
        "An unbaked texture stopped describing itself as RGBA8");

    // The fallback really produces the old shape: RGBA8, one level, no baked payload, and
    // pixels that still look like the source.
    const std::optional<RenderTextureAssetData> fallback = DecodeRenderTextureToRgba8(bakedAsset);
    Require(fallback.has_value(), "The RGBA8 fallback refused a texture this engine baked");
    Require(!fallback->gpuBlocks.has_value() && fallback->mipCount == 1U &&
            fallback->rgba8.size() == static_cast<std::size_t>(edge) * edge * 4U,
        "The RGBA8 fallback did not produce the shape the unbaked path expects");
    Require(fallback->MakeDesc(nullptr, RenderTextureColorSpace::Linear).format == bgfx::TextureFormat::RGBA8,
        "The RGBA8 fallback still described itself as a block format");
    Require(MeanAbsoluteError(reference, fallback->rgba8) <= 4.0,
        "The RGBA8 fallback did not decode the baked texture back to something like its source");

    Require(!RenderDeviceSupportsTextureFormat(bgfx::TextureFormat::Count, RenderTextureColorSpace::Linear),
        "An out-of-range format was reported as device-supported");

    // Two properties of the device query that hold whatever device is up - including none at
    // all, where bgfx's caps table is still all zero because nothing has filled it in. Stated
    // as properties rather than as a second copy of the bit test, so a wrong bit test fails
    // here instead of being echoed back.
    for (std::uint32_t index = 0U; index < static_cast<std::uint32_t>(bgfx::TextureFormat::Count); ++index) {
        const auto format = static_cast<bgfx::TextureFormat::Enum>(index);
        const bool linear = RenderDeviceSupportsTextureFormat(format, RenderTextureColorSpace::Linear);
        const bool srgb = RenderDeviceSupportsTextureFormat(format, RenderTextureColorSpace::Srgb);
        // Nothing the device says nothing about is claimed to be sampleable.
        Require(bgfx::getCaps()->formats[index] != 0U || (!linear && !srgb),
            "A format the device reports no capability at all for was reported as sampleable");
        // sRGB is a narrower claim than linear, never a wider one: a device that cannot sample
        // the format at all cannot sample it as sRGB either.
        Require(!srgb || linear, "A format was reported sRGB-sampleable but not sampleable");
    }
}

} // namespace

void RunTextureBakeTests() {
    RunBakedTextureFormatChoiceTest();
    RunBakedTextureDeterminismTest();
    RunBakedTextureBakeKeyTest();
    RunBakedTextureMipChainTest();
    RunBakedTextureColorSpaceTest();
    RunBakedTextureDimensionRuleTest();
    RunBakedTextureRefusalTest();
    RunBakedTextureQualityTest();
    RunBakedTextureReadBackTest();
    RunBakedTextureFallbackGuardTest();
    RunBakedTextureRuntimeUploadPathTest();
}

} // namespace kb::render::tests
