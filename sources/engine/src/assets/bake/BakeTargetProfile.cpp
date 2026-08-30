#include "engine/assets/bake/BakeTargetProfile.hpp"

#include "engine/assets/bake/AssetBakeKey.hpp"

#include <array>

namespace kb::assets::bake {
namespace {

constexpr std::array<BakeTargetProfile, 4U> kProfiles{
    WindowsX64BakeTargetProfile(),
    LinuxX64BakeTargetProfile(),
    AndroidArm64BakeTargetProfile(),
    WebWasm32BakeTargetProfile(),
};

[[nodiscard]] constexpr bool IsPowerOfTwo(std::uint32_t value) noexcept {
    return value != 0U && (value & (value - 1U)) == 0U;
}

} // namespace

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
    if (!IsPowerOfTwo(profile.packageBlockAlignmentBytes) || !IsPowerOfTwo(profile.mappedBlockAlignmentBytes)) {
        return false;
    }
    // A mapped block is also a package block, so its granularity has to be a
    // whole number of package alignments or the two placements disagree.
    if (profile.mappedBlockAlignmentBytes % profile.packageBlockAlignmentBytes != 0U) {
        return false;
    }
    if (profile.maxGeometryChunkBytes == 0U) {
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

} // namespace kb::assets::bake
