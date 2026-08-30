#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace kb::assets::bake {

// Which hardware-decodable texture compression family a target can sample.
enum class TextureCompressionFamily : std::uint8_t {
    // BCn (BC1/BC3/BC4/BC5/BC6H/BC7). Desktop GPUs.
    BlockCompressed,
    // ASTC. The only family every Android arm64 device we ship on decodes.
    AdaptiveScalable,
};

// Shader binary flavours the renderer can load. The names below are exactly
// the leaf directories the renderer looks under (`shaders/<name>` -- see
// ShaderProfileDirectoryForRenderer in sources/renderer/src/ShaderManifest.cpp,
// which stays the single source of truth for that layout). kb_engine must not
// depend on kb_renderer, so the leaf names are mirrored here and nowhere else.
enum class ShaderBakeBackend : std::uint8_t {
    Dxbc,
    Dxil,
    Spirv,
    Glsl,
    Essl,
    Metal,
};

inline constexpr std::uint32_t kShaderBakeBackendCount = 6U;

// A set of backends. A mask, not a list: order is meaningless, duplicates are
// impossible, and the encoding is canonical, so it can go straight into a
// bake key without a sorting step.
using ShaderBakeBackendMask = std::uint32_t;

[[nodiscard]] constexpr ShaderBakeBackendMask ShaderBakeBackendBit(ShaderBakeBackend backend) noexcept {
    return static_cast<ShaderBakeBackendMask>(1U) << static_cast<std::uint32_t>(backend);
}

[[nodiscard]] constexpr bool HasShaderBakeBackend(ShaderBakeBackendMask mask, ShaderBakeBackend backend) noexcept {
    return (mask & ShaderBakeBackendBit(backend)) != 0U;
}

// "dxbc", "dxil", "spirv", "glsl", "essl", "metal".
[[nodiscard]] std::string_view ShaderBakeBackendName(ShaderBakeBackend backend) noexcept;

// Which of bgfx's two vertex-attribute size tables a backend is sized by.
// bgfx picks the table from the ACTIVE renderer, not from the layout, so this
// classification is what makes a baked vertex buffer portable or not.
enum class VertexAttributeStrideFamily : std::uint8_t {
    // s_attribTypeSizeD3D1x: Direct3D11, Direct3D12, Vulkan.
    Direct3DStyle,
    // s_attribTypeSizeGl: OpenGL, OpenGL ES, Metal.
    OpenGLStyle,
};

[[nodiscard]] constexpr VertexAttributeStrideFamily
ShaderBakeBackendStrideFamily(ShaderBakeBackend backend) noexcept {
    switch (backend) {
    case ShaderBakeBackend::Dxbc:
    case ShaderBakeBackend::Dxil:
    case ShaderBakeBackend::Spirv:
        return VertexAttributeStrideFamily::Direct3DStyle;
    case ShaderBakeBackend::Glsl:
    case ShaderBakeBackend::Essl:
    case ShaderBakeBackend::Metal:
        return VertexAttributeStrideFamily::OpenGLStyle;
    }
    return VertexAttributeStrideFamily::Direct3DStyle;
}

enum class BakeIndexWidth : std::uint8_t {
    Bits16,
    Bits32,
};

[[nodiscard]] constexpr std::uint32_t BakeIndexWidthBytes(BakeIndexWidth width) noexcept {
    return width == BakeIndexWidth::Bits16 ? 2U : 4U;
}

// Description of ONE platform we bake for.
//
// This type answers questions ("may I emit a 3x16-bit attribute?", "how big may
// a geometry chunk get?"); it never converts anything. Conversion belongs to
// the individual bakers, which read a profile and act on it.
struct BakeTargetProfile {
    // Stable text id. It goes into AssetBakeKey::targetProfileId and becomes a
    // path component of the bake store, so it must satisfy
    // IsValidBakeCacheName. Non-owning: it must reference storage that outlives
    // the profile. Every factory profile below references a string literal.
    std::string_view identifier{};

    TextureCompressionFamily textureCompression = TextureCompressionFamily::BlockCompressed;

    // Every shader backend whose binaries a package for this profile must
    // contain. A profile with an empty set bakes no shaders and is invalid.
    ShaderBakeBackendMask shaderBackends = 0U;

    // Widest index buffer the target is guaranteed to accept. A baker must
    // split a mesh that would need more indices than this fits.
    BakeIndexWidth indexWidth = BakeIndexWidth::Bits32;

    // TRAP -- read before baking any vertex buffer as raw bytes.
    //
    // bgfx sizes a 3-component 16-bit vertex attribute (Int16, Uint16, Half)
    // differently per renderer: 8 bytes under the D3D-style table and 6 bytes
    // under the GL-style one (third_party/bgfx.cmake/bgfx/src/vertexlayout.cpp,
    // s_attribTypeSizeD3D1x / s_attribTypeSizeGl, rows Int16/Uint16/Half,
    // column 3). bgfx::VertexLayout::begin() reads whichever table
    // initAttribTypeSizeTable() installed for the renderer that actually came
    // up, so the SAME layout description yields two different strides and two
    // different attribute offsets. A vertex buffer baked as raw bytes is
    // therefore NOT portable across those two families the moment it uses such
    // an attribute -- the GPU reads every vertex after the first from the wrong
    // offset, which shows up as scrambled geometry, not as a load error.
    //
    // This flag may only be true for a profile whose entire backend set sits in
    // one stride family (BakeTargetProfileHasUniformVertexStride enforces it,
    // and IsValidBakeTargetProfile rejects the combination outright). When it
    // is false a baker must pad such an attribute to 4 components or widen it
    // to float rather than emit a stride that depends on the runtime renderer.
    bool allowsThreeComponent16BitAttributes = false;

    // Alignment every block gets inside a package. Must be a power of two.
    std::uint32_t packageBlockAlignmentBytes = 1U;

    // Alignment a block must additionally get when the runtime reads it through
    // a memory mapping, i.e. the granularity the platform's mapping call
    // accepts as a file offset. Must be a power of two and a multiple of
    // packageBlockAlignmentBytes.
    std::uint32_t mappedBlockAlignmentBytes = 1U;

    // Largest geometry chunk a baker may emit for this target. A baker must
    // split its output rather than exceed it.
    std::uint64_t maxGeometryChunkBytes = 0U;

    [[nodiscard]] bool operator==(const BakeTargetProfile&) const noexcept = default;
};

// True when every backend in the profile is sized by the same bgfx attribute
// table, i.e. when a raw-byte vertex buffer has one stride across the whole
// profile.
[[nodiscard]] constexpr bool BakeTargetProfileHasUniformVertexStride(const BakeTargetProfile& profile) noexcept {
    bool seen = false;
    VertexAttributeStrideFamily family = VertexAttributeStrideFamily::Direct3DStyle;
    for (std::uint32_t index = 0U; index < kShaderBakeBackendCount; ++index) {
        const auto backend = static_cast<ShaderBakeBackend>(index);
        if (!HasShaderBakeBackend(profile.shaderBackends, backend)) {
            continue;
        }
        const VertexAttributeStrideFamily current = ShaderBakeBackendStrideFamily(backend);
        if (!seen) {
            family = current;
            seen = true;
        } else if (current != family) {
            return false;
        }
    }
    return seen;
}

// Rejects a profile that cannot be baked for: a non-portable identifier, an
// empty backend set, a non-power-of-two or inconsistent alignment, a zero chunk
// budget, or the 3x16-bit trap above enabled on a mixed-stride backend set.
[[nodiscard]] bool IsValidBakeTargetProfile(const BakeTargetProfile& profile) noexcept;

// Windows x64: Direct3D 11/12 and Vulkan, all D3D-style, so a raw-byte vertex
// buffer has one stride and 3x16-bit attributes are safe here. Memory mapping
// goes through MapViewOfFile, whose file offset must be a multiple of the Win32
// allocation granularity (64 KiB), not of the 4 KiB page.
[[nodiscard]] constexpr BakeTargetProfile WindowsX64BakeTargetProfile() noexcept {
    return BakeTargetProfile{
        .identifier = "Windows.x64",
        .textureCompression = TextureCompressionFamily::BlockCompressed,
        .shaderBackends = ShaderBakeBackendBit(ShaderBakeBackend::Dxbc) |
            ShaderBakeBackendBit(ShaderBakeBackend::Dxil) | ShaderBakeBackendBit(ShaderBakeBackend::Spirv),
        .indexWidth = BakeIndexWidth::Bits32,
        .allowsThreeComponent16BitAttributes = true,
        .packageBlockAlignmentBytes = 16U,
        .mappedBlockAlignmentBytes = 65536U,
        .maxGeometryChunkBytes = 64ULL * 1024ULL * 1024ULL,
    };
}

// Linux x64: Vulkan (D3D-style) and OpenGL (GL-style) in one package, so the
// stride of a 3x16-bit attribute would differ between the two -- the flag must
// stay false. mmap offsets must be page-size multiples: 4 KiB on x86-64.
[[nodiscard]] constexpr BakeTargetProfile LinuxX64BakeTargetProfile() noexcept {
    return BakeTargetProfile{
        .identifier = "Linux.x64",
        .textureCompression = TextureCompressionFamily::BlockCompressed,
        .shaderBackends =
            ShaderBakeBackendBit(ShaderBakeBackend::Spirv) | ShaderBakeBackendBit(ShaderBakeBackend::Glsl),
        .indexWidth = BakeIndexWidth::Bits32,
        .allowsThreeComponent16BitAttributes = false,
        .packageBlockAlignmentBytes = 16U,
        .mappedBlockAlignmentBytes = 4096U,
        .maxGeometryChunkBytes = 64ULL * 1024ULL * 1024ULL,
    };
}

// Android arm64: Vulkan (D3D-style) and OpenGL ES (GL-style) in one package, so
// 3x16-bit attributes are unsafe here too. 16-bit indices keep the mobile
// vertex budget honest, and the mapping granularity is the 16 KiB page arm64
// Android is moving to, which is a valid offset multiple on 4 KiB devices too.
[[nodiscard]] constexpr BakeTargetProfile AndroidArm64BakeTargetProfile() noexcept {
    return BakeTargetProfile{
        .identifier = "Android.arm64",
        .textureCompression = TextureCompressionFamily::AdaptiveScalable,
        .shaderBackends =
            ShaderBakeBackendBit(ShaderBakeBackend::Spirv) | ShaderBakeBackendBit(ShaderBakeBackend::Essl),
        .indexWidth = BakeIndexWidth::Bits16,
        .allowsThreeComponent16BitAttributes = false,
        .packageBlockAlignmentBytes = 16U,
        .mappedBlockAlignmentBytes = 16384U,
        .maxGeometryChunkBytes = 16ULL * 1024ULL * 1024ULL,
    };
}

// Every profile we ship. macOS is deliberately absent: the target is frozen, so
// no profile bakes ShaderBakeBackend::Metal. The enumerator stays because the
// renderer still knows the flavour and the stride-family classification above
// has to cover it.
[[nodiscard]] std::span<const BakeTargetProfile> BakeTargetProfiles() noexcept;

// Resolves a profile from its stable identifier (a command-line or config
// value). Leaves `out` untouched and returns false for an unknown id -- an
// unrecognised target is a malformed request, never a silent fallback.
[[nodiscard]] bool TryFindBakeTargetProfile(std::string_view identifier, BakeTargetProfile& out) noexcept;

} // namespace kb::assets::bake
