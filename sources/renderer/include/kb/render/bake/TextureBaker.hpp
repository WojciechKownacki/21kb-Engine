#pragma once

#include "engine/assets/bake/AssetBakeKey.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/BakedAssetSink.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

// Turns a source image into the exact bytes a GPU samples: the profile's compression family,
// the complete mip chain, no CPU decode left for the runtime to pay.
//
// It lives in the host-only kb_renderer_bake target rather than beside the rest of the bake seam
// in kb_engine. Runtime kb_renderer keeps only the compressed-payload reader. The seam itself
// (BakeTargetProfile, AssetBakeKey, IBakedAssetSink) stays in kb_engine where every baker
// reaches it; only this baker, which needs the image stack, sits on the renderer's side.
namespace kb::render::bake {

// Identity of this baker inside a bake key. `bakerId` scopes `bakerVersion`, so bumping the
// version below re-bakes every texture and leaves every other baker's cache untouched.
inline constexpr std::string_view kTextureBakerId = "Texture";
// 3 adds the pinned, scalar etcpak 2.0 ETC2 RGB/RGBA encoder. Version 2 flattened alpha that a
// format did not retain; version 3 keeps that rule and changes the set of supported families.
inline constexpr std::string_view kTextureBakerVersion = "3";

// Runtime type of the artifact this baker publishes; a path component of the bake store.
inline constexpr std::string_view kTextureBakedAssetTypeId = "Texture2D";

enum class TextureBakeStatus : std::uint8_t {
    Success,
    // The profile itself is not bakeable (IsValidBakeTargetProfile said no).
    InvalidProfile,
    // The requested family is not one this profile's packages carry. Baking it anyway would
    // produce an artifact the target can never sample.
    FamilyNotInProfile,
    // The source file could not be read.
    SourceUnreadable,
    // bimg could not parse the source bytes as an image.
    SourceUndecodable,
    // Cube maps, volumes and arrays are out of scope: their mip chains and face ordering are
    // a separate contract, and producing a wrong one silently is worse than refusing.
    UnsupportedSourceShape,
    // The source dimensions are not whole multiples of the chosen format's block footprint.
    // See the note on the rule at BakedTextureBlockFootprint.
    UnalignedDimensions,
    // A normal map declared as sRGB. Its texels are a direction, not a colour: decoding them
    // through a transfer function bends every normal, and mip generation would average them
    // in the wrong space.
    SrgbNormalMapRejected,
    // The encoder, the mip builder or the container writer refused.
    EncodeFailed,
    // The sink refused the artifact; the sink's own status says why.
    SinkRejected,
};

[[nodiscard]] std::string_view ToString(TextureBakeStatus status) noexcept;

// Everything about a bake that is not already in the profile or the source bytes. Every field
// here reaches the bake key, so changing one re-bakes.
struct TextureBakeSettings {
    RenderTextureAssetSemantic semantic = RenderTextureAssetSemantic::Unknown;
    RenderTextureAssetColorSpace colorSpace = RenderTextureAssetColorSpace::Unknown;
};

struct TextureBakeOutput {
    TextureBakeStatus status = TextureBakeStatus::SourceUnreadable;
    // Set for every call that got as far as reading the source, including failures - a caller
    // that is deciding whether to skip an unchanged bake needs the key before the answer.
    kb::assets::bake::AssetBakeKey key{};
    // Only meaningful on Success.
    bgfx::TextureFormat::Enum format = bgfx::TextureFormat::Count;
    std::uint16_t width = 0U;
    std::uint16_t height = 0U;
    std::uint8_t mipCount = 0U;
    // The bytes handed to the sink as the primary block: a KTX container holding the whole
    // chain. Returned as well so a caller can verify a bake without going back to the store.
    std::vector<std::uint8_t> primaryBlock;
    // The status the sink returned, when the failure was the sink's.
    kb::assets::bake::BakedAssetSinkStatus sinkStatus = kb::assets::bake::BakedAssetSinkStatus::Success;
};

// The format this baker picks for a (family, semantic, alpha) combination, with no I/O, so the
// choice can be asked about - and tested - without an image.
//
// The family decides the format; the semantic decides only whether alpha is worth paying for.
// It never decides a channel LAYOUT, and that is a deliberate refusal rather than an omission:
// the mesh shaders read a normal map as `.xyz` and a metallic-roughness map as `.g`/`.b`
// (sources/renderer/shaders/fs_mesh_instanced.sc), so a two-channel format such as BC5 - or
// bimg's Quality::NormalMap* setting, which for ASTC compresses through an RRRG swizzle -
// would leave those shaders sampling channels that no longer hold what they read. That is a
// silent rendering fault, not a load error, so this baker does not emit one.
//
// "Not paying for alpha" means the bake DISCARDS it, and BakeTextureBytes flattens it to
// opaque before encoding: BC1's alpha is a single bit that squish actually spends, encoding
// every texel below 128 in punch-through mode and forcing its RGB to black. Choosing BC1 for a
// semantic that does not sample alpha is only safe because the residue never reaches the
// encoder - the format choice alone would corrupt the colour those shaders do read.
//
// Returns false for a combination it has no format for, leaving `format` untouched.
[[nodiscard]] bool TryChooseBakedTextureFormat(
    kb::assets::bake::TextureCompressionFamily family,
    RenderTextureAssetSemantic semantic,
    bool sourceHasAlpha,
    bgfx::TextureFormat::Enum& format) noexcept;

// Block footprint of a baked format, in texels. The dimension rule this baker enforces is that
// the source's width and height are whole multiples of it.
//
// The rule is stricter than a desktop API demands - D3D and Vulkan round the base level up to
// whole blocks themselves and never sample the padding - and it is stricter on purpose. A
// browser is the strictest target we ship to, and WEBGL_compressed_texture_s3tc rejects a
// level whose width or height is not a multiple of four outright; a texture that bakes on the
// desktop and fails to upload in the browser is a defect found by a player, not by a build. It
// is also why the ASTC family below uses the 4x4 footprint: every other ASTC footprint has an
// odd or non-power-of-two edge that divides almost no real texture size.
[[nodiscard]] bool BakedTextureBlockFootprint(
    bgfx::TextureFormat::Enum format,
    std::uint16_t& blockWidth,
    std::uint16_t& blockHeight) noexcept;

// The key for this bake. Everything that changes the output bytes is in it: the source
// content, the target profile, the compression family (a profile may carry more than one), the
// settings, and this baker's id and version.
[[nodiscard]] kb::assets::bake::AssetBakeKey MakeTextureBakeKey(
    std::span<const std::uint8_t> sourceBytes,
    const kb::assets::bake::BakeTargetProfile& profile,
    kb::assets::bake::TextureCompressionFamily family,
    const TextureBakeSettings& settings);

// Bakes `sourceBytes` for one compression family and hands the result to `sink` as the
// artifact's primary block. Deterministic: the same source and the same arguments produce
// byte-identical output on repeated supported x86-64 cooker runs. Cross-OS identity is a release
// gate before Windows and Linux may share a remote bake cache.
//
// A profile may list several families (a WebGL2 browser guarantees BC or ETC2 and only says which at
// runtime), so a caller bakes once per family; the family is part of the key, so the two
// artifacts never collide.
[[nodiscard]] TextureBakeOutput BakeTextureBytes(
    std::span<const std::uint8_t> sourceBytes,
    const TextureBakeSettings& settings,
    const kb::assets::bake::BakeTargetProfile& profile,
    kb::assets::bake::TextureCompressionFamily family,
    kb::assets::bake::IBakedAssetSink& sink);

[[nodiscard]] TextureBakeOutput BakeTexture(
    const std::filesystem::path& sourcePath,
    const TextureBakeSettings& settings,
    const kb::assets::bake::BakeTargetProfile& profile,
    kb::assets::bake::TextureCompressionFamily family,
    kb::assets::bake::IBakedAssetSink& sink);

// Reads a primary block this baker wrote back into the runtime's texture shape, keeping the
// blocks compressed. The counterpart of the bake, and the only way a baked texture becomes a
// RenderTextureAssetData - the ordinary loader is untouched and still decodes what it always
// decoded. Returns false for anything that is not a 2D block-compressed container this baker
// could have produced.
[[nodiscard]] bool ReadBakedTexture(std::span<const std::uint8_t> primaryBlock, RenderTextureAssetData& out);

// Keeps the manifest qualifier and the GPU payload format under one runtime-owned contract.
// The package validator and the runtime loader both call this function so neither can accept
// a variant that the other later rejects.
[[nodiscard]] bool BakedTextureFormatMatchesFamily(
    bgfx::TextureFormat::Enum format,
    std::string_view qualifier) noexcept;

} // namespace kb::render::bake
