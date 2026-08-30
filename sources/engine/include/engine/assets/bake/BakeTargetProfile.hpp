#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace kb::assets::bake {

// Which hardware-decodable texture compression family a target can sample.
//
// Split at the line bgfx actually draws on the web, not at the vendor's marketing
// line: on Emscripten bgfx never probes the driver for a compressed format, it
// answers from a fixed list of cases (third_party/bgfx.cmake/bgfx/src/
// renderer_gl.cpp, isTextureFormatValidPerSpec), and that list carries BC1..BC3
// and ASTC but has no case at all for BC4, BC5, BC6H or BC7 -- they fall through
// to `default: break` and read as unsupported however capable the GPU is.
enum class TextureCompressionFamily : std::uint8_t {
    // BC1 and BC3. The BCn subset a browser can be given.
    BlockCompressedBaseline,
    // BC4, BC5, BC6H and BC7. Desktop GPUs only, for the reason above.
    BlockCompressedExtended,
    // ASTC. Preferred on Android devices that expose the native format.
    AdaptiveScalable,
    // ETC2 RGB8/RGBA8. APPENDED because this ordinal is part of bake settings.
    // It is the guaranteed Android GLES 3 fallback and one half of the WebGL2
    // compressed-texture portability contract.
    Ericsson2,
};

inline constexpr std::uint32_t kTextureCompressionFamilyCount = 4U;

// A set of families. A target may genuinely need more than one baked: a browser
// Different devices within one target can expose different native families, so
// a package may need more than one. Same mask idiom as ShaderBakeBackendMask below.
using TextureCompressionFamilyMask = std::uint32_t;

[[nodiscard]] constexpr TextureCompressionFamilyMask
TextureCompressionFamilyBit(TextureCompressionFamily family) noexcept {
    return static_cast<TextureCompressionFamilyMask>(1U) << static_cast<std::uint32_t>(family);
}

[[nodiscard]] constexpr bool
HasTextureCompressionFamily(TextureCompressionFamilyMask mask, TextureCompressionFamily family) noexcept {
    return (mask & TextureCompressionFamilyBit(family)) != 0U;
}

// Stable manifest qualifier: "bc-baseline", "bc-extended", "astc", "etc2".
[[nodiscard]] std::string_view TextureCompressionFamilyName(TextureCompressionFamily family) noexcept;

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
    // WGSL, for bgfx's WebGPU backend. APPENDED, never inserted: the enumerator
    // order is the bit order of ShaderBakeBackendMask and that mask reaches a
    // bake key, so renumbering would silently invalidate every cached asset.
    //
    // No shipped profile bakes it. bgfx's WebGPU backend is Dawn-native only and
    // is switched off for Emscripten -- BGFX_CONFIG_RENDERER_WEBGPU lists Linux,
    // OSX and Windows and keeps BX_PLATFORM_EMSCRIPTEN commented out
    // (third_party/bgfx.cmake/bgfx/src/config.h) -- so a browser package holding
    // WGSL binaries would be bytes nobody can execute. The enumerator exists
    // because the stride classification below has to answer for WebGPU: it is
    // the renderer the vertex layout would be sized by the moment bgfx enables
    // that backend, and getting that answer wrong is what bakes broken geometry.
    Wgsl,
};

inline constexpr std::uint32_t kShaderBakeBackendCount = 7U;

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

// "dxbc", "dxil", "spirv", "glsl", "essl", "metal", "wgsl".
[[nodiscard]] std::string_view ShaderBakeBackendName(ShaderBakeBackend backend) noexcept;

// shaderc's target platform is independent from the binary backend. In
// particular, SPIR-V is emitted for Windows, Linux and Android with different
// platform defines, while ESSL differs between Android and Emscripten. Keeping
// this in the target profile prevents cross-platform cache collisions and
// binaries compiled with the wrong BX_PLATFORM_* contract.
enum class ShaderBakePlatform : std::uint8_t {
    Windows,
    Linux,
    Android,
    MacOS,
    WebGl,
};

inline constexpr std::uint32_t kShaderBakePlatformCount = 5U;

// Canonical shaderc --platform spellings.
[[nodiscard]] std::string_view ShaderBakePlatformName(ShaderBakePlatform platform) noexcept;
[[nodiscard]] bool TryParseShaderBakePlatform(std::string_view name, ShaderBakePlatform& out) noexcept;

[[nodiscard]] constexpr bool ShaderBakePlatformSupportsBackend(
    ShaderBakePlatform platform,
    ShaderBakeBackend backend) noexcept {
    switch (platform) {
    case ShaderBakePlatform::Windows:
        return backend == ShaderBakeBackend::Dxbc || backend == ShaderBakeBackend::Dxil ||
            backend == ShaderBakeBackend::Spirv;
    case ShaderBakePlatform::Linux:
        return backend == ShaderBakeBackend::Spirv || backend == ShaderBakeBackend::Glsl;
    case ShaderBakePlatform::Android:
        return backend == ShaderBakeBackend::Spirv || backend == ShaderBakeBackend::Essl;
    case ShaderBakePlatform::MacOS:
        return backend == ShaderBakeBackend::Metal;
    case ShaderBakePlatform::WebGl:
        return backend == ShaderBakeBackend::Essl;
    }
    return false;
}

// Which of bgfx's two vertex-attribute size tables a backend is sized by.
// bgfx picks the table from the ACTIVE renderer, not from the layout, so this
// classification is what makes a baked vertex buffer portable or not.
enum class VertexAttributeStrideFamily : std::uint8_t {
    // s_attribTypeSizeD3D1x: Direct3D11, Direct3D12, Vulkan, WebGPU.
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
    // WebGPU is a D3D-style row in bgfx's dispatch table (s_attribTypeSize in
    // third_party/bgfx.cmake/bgfx/src/vertexlayout.cpp), and the WebGPU spec
    // agrees from the other side: it has no 3-component 16-bit vertex format at
    // all, only the *16x2 and *16x4 pairs.
    case ShaderBakeBackend::Wgsl:
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

    // Every texture compression family a package for this profile must contain.
    // A profile with an empty set bakes no sampleable texture and is invalid.
    TextureCompressionFamilyMask textureCompressions = 0U;

    // Every shader backend whose binaries a package for this profile must
    // contain. A profile with an empty set bakes no shaders and is invalid.
    ShaderBakeBackendMask shaderBackends = 0U;

    // Explicit shaderc platform shared by all shader backends in this package.
    // A backend alone is insufficient to derive it (for example SPIR-V is
    // valid on Windows, Linux and Android).
    ShaderBakePlatform shaderPlatform = ShaderBakePlatform::Windows;

    // Index width emitted for this profile. A baker using 16-bit indices must
    // split draw sections into independently addressable vertex ranges rather
    // than reject a large mesh; the API may still support wider indices.
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
    //
    // Every shipped profile uses 256, and that number is a floor, not a taste:
    // Vulkan's required-limits table caps minUniformBufferOffsetAlignment,
    // minStorageBufferOffsetAlignment and minTexelBufferOffsetAlignment at 256,
    // so a 256-aligned block offset is a legal buffer offset on every conformant
    // device and nothing coarser can be demanded of us; WebGPU independently
    // requires the bytesPerRow of a buffer-to-texture copy to be a multiple of
    // 256. Under-aligning is not a padding saving -- it produces a block that
    // simply cannot be bound or copied from where it lies.
    std::uint32_t packageBlockAlignmentBytes = 1U;

    // Alignment a block must additionally get when the runtime reads it through
    // a memory mapping, i.e. the granularity the platform's mapping call
    // accepts as a file offset. Must be a power of two and a multiple of
    // packageBlockAlignmentBytes.
    //
    // A mapped block is also a package block, so this can never be finer than
    // packageBlockAlignmentBytes. A target with no memory mapping at all
    // therefore says so by setting it EQUAL to packageBlockAlignmentBytes --
    // that is this field's "no further alignment" value, and it costs no padding
    // beyond what the package alignment already spent.
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

// Digest of everything in the profile except its identifier, for AssetBakeKey.
//
// The key carries the profile's NAME, and a name does not change when the
// profile behind it does. Widening an alignment or adding a texture family
// while "Windows.x64" stays "Windows.x64" would leave every artifact baked
// under the old answers addressable under the same key -- a cache that returns
// output no baker would produce today, silently. This closes that: the key
// carries what the profile SAYS, not only what it is called.
//
// Never zero, so a key that forgot to ask is distinguishable from one that did.
[[nodiscard]] std::uint64_t BakeTargetProfileFingerprint(const BakeTargetProfile& profile) noexcept;

// Rejects a profile that cannot be baked for: a non-portable identifier, an
// empty texture family set, an empty backend set, a non-power-of-two or
// inconsistent alignment, a zero chunk budget, or the 3x16-bit trap above
// enabled on a mixed-stride backend set.
[[nodiscard]] bool IsValidBakeTargetProfile(const BakeTargetProfile& profile) noexcept;

// Windows x64: Direct3D 11/12 and Vulkan, all D3D-style, so a raw-byte vertex
// buffer has one stride and 3x16-bit attributes are safe here. Memory mapping
// goes through MapViewOfFile, whose file offset must be a multiple of the Win32
// allocation granularity (64 KiB), not of the 4 KiB page -- a fixed property of
// the API, not of the CPU, so no page-size question arises here.
[[nodiscard]] constexpr BakeTargetProfile WindowsX64BakeTargetProfile() noexcept {
    return BakeTargetProfile{
        .identifier = "Windows.x64",
        .textureCompressions = TextureCompressionFamilyBit(TextureCompressionFamily::BlockCompressedBaseline) |
            TextureCompressionFamilyBit(TextureCompressionFamily::BlockCompressedExtended),
        .shaderBackends = ShaderBakeBackendBit(ShaderBakeBackend::Dxbc) |
            ShaderBakeBackendBit(ShaderBakeBackend::Dxil) | ShaderBakeBackendBit(ShaderBakeBackend::Spirv),
        .shaderPlatform = ShaderBakePlatform::Windows,
        .indexWidth = BakeIndexWidth::Bits32,
        .allowsThreeComponent16BitAttributes = true,
        .packageBlockAlignmentBytes = 256U,
        .mappedBlockAlignmentBytes = 65536U,
        .maxGeometryChunkBytes = 64ULL * 1024ULL * 1024ULL,
    };
}

// Linux x64: Vulkan (D3D-style) and OpenGL (GL-style) in one package, so the
// stride of a 3x16-bit attribute would differ between the two -- the flag must
// stay false. mmap offsets must be page-size multiples, and 4096 is measured
// rather than assumed here: x86-64 fixes the base page at 4 KiB, so unlike
// arm64 below there is no larger page variant a device could show up with.
[[nodiscard]] constexpr BakeTargetProfile LinuxX64BakeTargetProfile() noexcept {
    return BakeTargetProfile{
        .identifier = "Linux.x64",
        .textureCompressions = TextureCompressionFamilyBit(TextureCompressionFamily::BlockCompressedBaseline) |
            TextureCompressionFamilyBit(TextureCompressionFamily::BlockCompressedExtended),
        .shaderBackends =
            ShaderBakeBackendBit(ShaderBakeBackend::Spirv) | ShaderBakeBackendBit(ShaderBakeBackend::Glsl),
        .shaderPlatform = ShaderBakePlatform::Linux,
        .indexWidth = BakeIndexWidth::Bits32,
        .allowsThreeComponent16BitAttributes = false,
        .packageBlockAlignmentBytes = 256U,
        .mappedBlockAlignmentBytes = 4096U,
        .maxGeometryChunkBytes = 64ULL * 1024ULL * 1024ULL,
    };
}

// Android arm64: Vulkan (D3D-style) and OpenGL ES (GL-style) in one package, so
// 3x16-bit attributes are unsafe here too. 16-bit indices keep the mobile
// vertex budget honest.
//
// The mapping granularity is 16 KiB and must not be trimmed back to the 4 KiB
// that arm64 Linux used to imply: Android 15 ships devices whose page size is
// 16 KiB, where mmap rejects a 4 KiB-aligned file offset outright. 16384 is a
// whole number of pages on a 4 KiB device as well, so one baked package serves
// both page sizes; the runtime still reads the live value from getpagesize()
// rather than trusting this number to be the device's page size.
[[nodiscard]] constexpr BakeTargetProfile AndroidArm64BakeTargetProfile() noexcept {
    return BakeTargetProfile{
        .identifier = "Android.arm64",
        .textureCompressions = TextureCompressionFamilyBit(TextureCompressionFamily::AdaptiveScalable) |
            TextureCompressionFamilyBit(TextureCompressionFamily::Ericsson2),
        .shaderBackends =
            ShaderBakeBackendBit(ShaderBakeBackend::Spirv) | ShaderBakeBackendBit(ShaderBakeBackend::Essl),
        .shaderPlatform = ShaderBakePlatform::Android,
        .indexWidth = BakeIndexWidth::Bits16,
        .allowsThreeComponent16BitAttributes = false,
        .packageBlockAlignmentBytes = 256U,
        .mappedBlockAlignmentBytes = 16384U,
        .maxGeometryChunkBytes = 16ULL * 1024ULL * 1024ULL,
    };
}

// Web (wasm32): the browser build, which today means WebGL2 and nothing else.
//
// Shaders: ESSL only. bgfx's WebGPU backend is Dawn-native and switched off for
// Emscripten (config.h, see ShaderBakeBackend::Wgsl above), and bgfx has no
// "WebGL" renderer type -- WebGL2 arrives as RendererType::OpenGLES. Adding WGSL
// here would ship binaries the browser build cannot reach.
//
// 3x16-bit attributes stay forbidden even though ESSL alone is a uniform
// GL-style set. WebGPU is the renderer this profile gains next, it is a D3D-style
// row in bgfx's table, and its spec has no 3-component 16-bit vertex format at
// all; a buffer baked with one would have to be re-baked -- i.e. all shipped
// content re-cooked -- the day that backend lands, which is exactly the cost
// this field exists to avoid.
//
// Textures: BC1/BC3 and ETC2, both. A WebGL2 context guarantees
// WEBGL_compressed_texture_etc OR the s3tc trio, so neither family alone covers
// every browser. ASTC is deliberately absent: it is not the WebGL2 portability
// fallback and WebGPU is a separate, currently unsupported target. The BC half
// stops at BC3 because bgfx on Emscripten answers for
// compressed formats from a fixed list that has no BC4/BC5/BC6H/BC7 case.
//
// Mapping: none exists. Emscripten's mmap implements MAP_PRIVATE by copying the
// range into the heap and does not support MAP_SHARED, so a `Mapped` residency
// hint degrades to a plain read and any extra alignment would be pure padding in
// a payload the client pays to download. Hence mapped == package alignment.
//
// Chunk budget: 8 MiB, a quarter of desktop's. wasm32 caps the heap at 4 GiB and
// Emscripten's MAXIMUM_MEMORY defaults to 2 GB; a chunk arrives as one HTTP range
// response that is buffered before it reaches the GPU, and growing the heap to
// hold it copies the entire heap, so the chunk is resident twice at the worst
// moment. A smaller chunk also makes a failed or unsatisfied range cheap to retry.
[[nodiscard]] constexpr BakeTargetProfile WebGlWasm32BakeTargetProfile() noexcept {
    return BakeTargetProfile{
        .identifier = "WebGL.wasm32",
        .textureCompressions = TextureCompressionFamilyBit(TextureCompressionFamily::BlockCompressedBaseline) |
            TextureCompressionFamilyBit(TextureCompressionFamily::Ericsson2),
        .shaderBackends = ShaderBakeBackendBit(ShaderBakeBackend::Essl),
        .shaderPlatform = ShaderBakePlatform::WebGl,
        // WebGL2 is OpenGL ES 3.0, where 32-bit indices are core rather than an
        // extension, so the widest guaranteed index buffer really is 32-bit.
        .indexWidth = BakeIndexWidth::Bits32,
        .allowsThreeComponent16BitAttributes = false,
        .packageBlockAlignmentBytes = 256U,
        .mappedBlockAlignmentBytes = 256U,
        .maxGeometryChunkBytes = 8ULL * 1024ULL * 1024ULL,
    };
}

// Every profile we ship. macOS is deliberately absent: the target is frozen, so
// no profile bakes ShaderBakeBackend::Metal. The enumerator stays because the
// renderer still knows the flavour and the stride-family classification above
// has to cover it. ShaderBakeBackend::Wgsl is absent from every profile for the
// separate reason given at the enumerator itself.
[[nodiscard]] std::span<const BakeTargetProfile> BakeTargetProfiles() noexcept;

// Resolves a profile from its stable identifier (a command-line or config
// value). Leaves `out` untouched and returns false for an unknown id -- an
// unrecognised target is a malformed request, never a silent fallback.
[[nodiscard]] bool TryFindBakeTargetProfile(std::string_view identifier, BakeTargetProfile& out) noexcept;

} // namespace kb::assets::bake
