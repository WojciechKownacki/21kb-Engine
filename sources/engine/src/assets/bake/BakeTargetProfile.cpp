#include "engine/assets/bake/BakeTargetProfile.hpp"

#include "engine/assets/bake/AssetBakeKey.hpp"
#include "engine/assets/bake/AssetPack.hpp"

#include <array>

namespace kb::assets::bake {
namespace {

constexpr std::array<BakeTargetProfile, 6U> kProfiles{
    WindowsX64BakeTargetProfile(),
    LinuxX64BakeTargetProfile(),
    AndroidAstcArm64BakeTargetProfile(),
    AndroidEtc2Arm64BakeTargetProfile(),
    WebGlWasm32BakeTargetProfile(),
    WebGpuWasm32BakeTargetProfile(),
};

[[nodiscard]] constexpr bool IsPowerOfTwo(std::uint32_t value) noexcept {
    return value != 0U && (value & (value - 1U)) == 0U;
}

} // namespace

std::string_view TextureCompressionFamilyName(TextureCompressionFamily family) noexcept {
    switch (family) {
    case TextureCompressionFamily::BlockCompressedBaseline:
        return "bc-baseline";
    case TextureCompressionFamily::BlockCompressedExtended:
        return "bc-extended";
    case TextureCompressionFamily::AdaptiveScalable:
        return "astc";
    case TextureCompressionFamily::Ericsson2:
        return "etc2";
    }
    return {};
}

std::string_view ShaderBakeBackendName(ShaderBakeBackend backend) noexcept {
    switch (backend) {
    case ShaderBakeBackend::Dxbc:
        return "dxbc";
    case ShaderBakeBackend::Dxil:
        return "dxil";
    case ShaderBakeBackend::Spirv:
        return "spirv";
    case ShaderBakeBackend::Glsl:
        return "glsl";
    case ShaderBakeBackend::Essl:
        return "essl";
    case ShaderBakeBackend::Metal:
        return "metal";
    case ShaderBakeBackend::Wgsl:
        return "wgsl";
    }
    return {};
}

std::string_view ShaderBakePlatformName(ShaderBakePlatform platform) noexcept {
    switch (platform) {
    case ShaderBakePlatform::Windows:
        return "windows";
    case ShaderBakePlatform::Linux:
        return "linux";
    case ShaderBakePlatform::Android:
        return "android";
    case ShaderBakePlatform::MacOS:
        return "osx";
    case ShaderBakePlatform::WebGl:
        return "asm.js";
    case ShaderBakePlatform::WebGpu:
        return "webgpu";
    }
    return {};
}

bool TryParseShaderBakePlatform(std::string_view name, ShaderBakePlatform& out) noexcept {
    for (std::uint32_t index = 0U; index < kShaderBakePlatformCount; ++index) {
        const auto candidate = static_cast<ShaderBakePlatform>(index);
        if (ShaderBakePlatformName(candidate) == name) {
            out = candidate;
            return true;
        }
    }
    return false;
}

std::string_view ShaderBakePlatformShadercToken(ShaderBakePlatform platform) noexcept {
    switch (platform) {
    case ShaderBakePlatform::Windows:
        return "windows";
    case ShaderBakePlatform::Linux:
        return "linux";
    case ShaderBakePlatform::Android:
        return "android";
    case ShaderBakePlatform::MacOS:
        return "osx";
    case ShaderBakePlatform::WebGl:
    case ShaderBakePlatform::WebGpu:
        return "asm.js";
    }
    return {};
}

bool IsValidBakeTargetProfile(const BakeTargetProfile& profile) noexcept {
    if (!IsValidBakeCacheName(profile.identifier)) {
        return false;
    }
    constexpr TextureCompressionFamilyMask kKnownFamilies =
        (static_cast<TextureCompressionFamilyMask>(1U) << kTextureCompressionFamilyCount) - 1U;
    if (profile.textureCompressions == 0U || (profile.textureCompressions & ~kKnownFamilies) != 0U) {
        return false;
    }
    constexpr ShaderBakeBackendMask kKnownBackends =
        (static_cast<ShaderBakeBackendMask>(1U) << kShaderBakeBackendCount) - 1U;
    if (profile.shaderBackends == 0U || (profile.shaderBackends & ~kKnownBackends) != 0U) {
        return false;
    }
    if (static_cast<std::uint32_t>(profile.shaderPlatform) >= kShaderBakePlatformCount) {
        return false;
    }
    for (std::uint32_t index = 0U; index < kShaderBakeBackendCount; ++index) {
        const auto backend = static_cast<ShaderBakeBackend>(index);
        if (HasShaderBakeBackend(profile.shaderBackends, backend) &&
            !ShaderBakePlatformSupportsBackend(profile.shaderPlatform, backend)) {
            return false;
        }
    }
    if (!IsPowerOfTwo(profile.packageBlockAlignmentBytes) || !IsPowerOfTwo(profile.mappedBlockAlignmentBytes)) {
        return false;
    }
    // A mapped block is also a package block, so its granularity has to be a
    // whole number of package alignments or the two placements disagree.
    if (profile.mappedBlockAlignmentBytes % profile.packageBlockAlignmentBytes != 0U) {
        return false;
    }
    if (profile.maxGeometryChunkBytes == 0U || profile.maxGeometryChunkBytes > kMaxAssetPackBlockBytes) {
        return false;
    }
    // The 3x16-bit stride trap, enforced rather than merely documented: a raw
    // vertex buffer only survives the trip if every backend in the profile is
    // sized by the same bgfx attribute table.
    return !profile.allowsThreeComponent16BitAttributes || BakeTargetProfileHasUniformVertexStride(profile);
}

std::span<const BakeTargetProfile> BakeTargetProfiles() noexcept {
    return { kProfiles };
}

bool TryFindBakeTargetProfile(std::string_view identifier, BakeTargetProfile& out) noexcept {
    for (const BakeTargetProfile& profile : kProfiles) {
        if (profile.identifier == identifier) {
            out = profile;
            return true;
        }
    }
    return false;
}


std::uint64_t BakeTargetProfileFingerprint(const BakeTargetProfile& profile) noexcept {
    // FNV-1a over an explicit little-endian stream, the same shape AssetBakeKey
    // hashes with, so the two agree about what "the same input" means. Fields
    // are fixed-width and written in declaration order; the identifier is left
    // out because the key already carries it in full.
    std::uint64_t hash = 14695981039346656037ULL;
    const auto absorb = [&hash](std::uint64_t value, std::uint32_t byteCount) noexcept {
        for (std::uint32_t shift = 0U; shift < byteCount * 8U; shift += 8U) {
            hash = (hash ^ static_cast<std::uint8_t>((value >> shift) & 0xFFU)) * 1099511628211ULL;
        }
    };
    absorb(profile.textureCompressions, 4U);
    absorb(profile.shaderBackends, 4U);
    absorb(static_cast<std::uint64_t>(profile.shaderPlatform), 1U);
    absorb(static_cast<std::uint64_t>(profile.indexWidth), 1U);
    absorb(profile.allowsThreeComponent16BitAttributes ? 1U : 0U, 1U);
    absorb(profile.packageBlockAlignmentBytes, 4U);
    absorb(profile.mappedBlockAlignmentBytes, 4U);
    absorb(profile.maxGeometryChunkBytes, 8U);
    // A profile whose fields happened to hash to zero would be indistinguishable
    // from a key that never asked for a fingerprint, which IsValid() rejects.
    return hash == 0U ? 1U : hash;
}

} // namespace kb::assets::bake
