#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/assets/bake/AssetBakeKey.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/BakedAssetSink.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace kb::tests {
namespace {

namespace bake = kb::assets::bake;

[[nodiscard]] std::vector<std::uint8_t> Bytes(std::string_view text) {
    const auto* first = reinterpret_cast<const std::uint8_t*>(text.data());
    return { first, first + text.size() };
}

[[nodiscard]] std::string ReadFileText(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    return { std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

[[nodiscard]] std::size_t CountEntries(const std::filesystem::path& directory) {
    return static_cast<std::size_t>(
        std::distance(std::filesystem::directory_iterator{ directory }, std::filesystem::directory_iterator{}));
}

[[nodiscard]] std::filesystem::path TestRoot() {
    return std::filesystem::temp_directory_path() / "21kb_engine_asset_bake_tests";
}

[[nodiscard]] std::string ToUpperAscii(std::string_view text) {
    std::string upper{ text };
    for (char& character : upper) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
    }
    return upper;
}

[[nodiscard]] bake::AssetBakeKey MakeMeshKey() {
    bake::AssetBakeKey key;
    key.sourceContentHash = 0x0123456789ABCDEFULL;
    key.bakerId = "SkeletalMesh";
    key.bakerVersion = "3";
    key.targetProfileId = "Windows.x64";
    // A literal, not BakeTargetProfileFingerprint(WindowsX64BakeTargetProfile()): the golden
    // below must move when the key ENCODING changes and stay put when a shipped profile is
    // edited, which is a different test's job.
    key.targetProfileHash = 0x0F1E2D3C4B5A6978ULL;
    key.settingsHash = 0xFEDCBA9876543210ULL;
    key.dependencies = { bake::AssetBakeDigest{ .high = 1U, .low = 2U },
        bake::AssetBakeDigest{ .high = 1U, .low = 2U }, bake::AssetBakeDigest{ .high = 0U, .low = 9U } };
    return key;
}

[[nodiscard]] bake::BakedAssetDescriptor MakeMeshDescriptor() {
    bake::BakedAssetDescriptor descriptor;
    descriptor.key = MakeMeshKey();
    descriptor.assetTypeId = "SkeletalMesh";
    return descriptor;
}

// A full bake of a two-block artifact through a sink of its own, so a test can
// run one against a store that already holds something.
[[nodiscard]] bake::BakedAssetSinkStatus BakeTwoBlockArtifact(const std::filesystem::path& root,
                                                              const bake::BakedAssetDescriptor& descriptor,
                                                              std::string_view primaryText,
                                                              std::string_view auxiliaryText) {
    bake::LooseBakedAssetSink sink{ root };
    const bake::BakedAssetSinkStatus begun = sink.BeginAsset(descriptor);
    if (begun != bake::BakedAssetSinkStatus::Success) {
        return begun;
    }
    const bake::BakedAssetSinkStatus primary = sink.WritePrimaryBlock(Bytes(primaryText), 16U);
    if (primary != bake::BakedAssetSinkStatus::Success) {
        return primary;
    }
    const bake::BakedAssetSinkStatus auxiliary =
        sink.WriteAuxiliaryBlock({ .name = "lod-tail", .alignmentBytes = 16U }, Bytes(auxiliaryText));
    if (auxiliary != bake::BakedAssetSinkStatus::Success) {
        return auxiliary;
    }
    return sink.CommitAsset();
}

// The one directory the hidden staging root holds while an artifact is open.
[[nodiscard]] std::filesystem::path SoleStagingDirectory(const std::filesystem::path& root) {
    std::filesystem::path found;
    std::size_t count = 0U;
    for (const std::filesystem::directory_entry& entry :
        std::filesystem::directory_iterator{ root / ".kbbakestaging" }) {
        found = entry.path();
        ++count;
    }
    Require(count == 1U, "The sink did not stage the open artifact in exactly one hidden directory");
    return found;
}

// Red when: a factory profile stops describing its target -- Android losing
// ASTC or 16-bit indices, a profile dropping a shader backend the renderer
// still looks for, a non-power-of-two alignment, a macOS profile reappearing
// while the target is frozen, or the 3x16-bit stride trap being enabled on a
// backend set that spans both of bgfx's attribute-size tables.
//
// Also red on the 256-byte package alignment regressing: the table loop below
// pins it for EVERY shipped profile, so it fired on all three profiles at the
// 16 bytes they carried before, and it fires again the moment one of them
// drifts back to a value Vulkan's min*BufferOffsetAlignment or WebGPU's
// bytesPerRow rule cannot accept.
void ProfilesAnswerQuestionsAboutTheirTarget() {
    const std::span<const bake::BakeTargetProfile> profiles = bake::BakeTargetProfiles();
    Require(profiles.size() == 4U,
        "BakeTargetProfiles must ship exactly Windows x64, Linux x64, Android arm64 and Web wasm32");
    for (const bake::BakeTargetProfile& profile : profiles) {
        Require(bake::IsValidBakeTargetProfile(profile), "A shipped bake target profile is not bakeable");
        Require(bake::IsValidBakeCacheName(profile.identifier),
            "A bake target profile identifier is not usable as a path component");
        Require(!bake::HasShaderBakeBackend(profile.shaderBackends, bake::ShaderBakeBackend::Metal),
            "A shipped profile bakes Metal although the macOS target is frozen");
        Require(!bake::HasShaderBakeBackend(profile.shaderBackends, bake::ShaderBakeBackend::Wgsl),
            "A shipped profile bakes WGSL although bgfx's WebGPU backend is compiled out on Emscripten");
        Require(profile.textureCompressions != 0U,
            "A shipped profile bakes no texture compression family at all");
        // 256 is a floor shared by every target we ship to: Vulkan's required
        // limits cap minUniformBufferOffsetAlignment, minStorageBufferOffset-
        // Alignment and minTexelBufferOffsetAlignment at 256, and WebGPU wants a
        // copy's bytesPerRow to be a multiple of 256. Anything finer produces a
        // block that cannot be bound or copied from where it was baked.
        Require(profile.packageBlockAlignmentBytes == 256U,
            "A shipped profile aligns package blocks to something other than 256 bytes");
        Require(profile.mappedBlockAlignmentBytes >= profile.packageBlockAlignmentBytes &&
                profile.mappedBlockAlignmentBytes % profile.packageBlockAlignmentBytes == 0U,
            "A shipped profile's mapped alignment is not a whole number of package alignments");
    }

    const bake::BakeTargetProfile windows = bake::WindowsX64BakeTargetProfile();
    Require(bake::HasTextureCompressionFamily(
                windows.textureCompressions, bake::TextureCompressionFamily::BlockCompressedBaseline) &&
            bake::HasTextureCompressionFamily(
                windows.textureCompressions, bake::TextureCompressionFamily::BlockCompressedExtended),
        "Windows x64 must bake the whole BCn range, baseline and extended");
    Require(bake::HasShaderBakeBackend(windows.shaderBackends, bake::ShaderBakeBackend::Dxbc) &&
            bake::HasShaderBakeBackend(windows.shaderBackends, bake::ShaderBakeBackend::Dxil) &&
            bake::HasShaderBakeBackend(windows.shaderBackends, bake::ShaderBakeBackend::Spirv),
        "Windows x64 must bake dxbc, dxil and spirv shader binaries");
    Require(!bake::HasShaderBakeBackend(windows.shaderBackends, bake::ShaderBakeBackend::Glsl),
        "Windows x64 must not bake glsl shader binaries");
    Require(bake::BakeIndexWidthBytes(windows.indexWidth) == 4U, "Windows x64 must allow 32-bit indices");
    Require(windows.mappedBlockAlignmentBytes == 65536U,
        "Windows x64 mapped blocks must honour the Win32 64 KiB allocation granularity");
    Require(windows.mappedBlockAlignmentBytes % windows.packageBlockAlignmentBytes == 0U,
        "Windows x64 mapped alignment must be a whole number of package alignments");
    Require(windows.maxGeometryChunkBytes == 64ULL * 1024ULL * 1024ULL,
        "Windows x64 geometry chunk budget changed unnoticed");

    // Every Windows backend is sized by bgfx's D3D-style attribute table, so a
    // raw-byte vertex buffer has one stride and a 3x16-bit attribute is safe.
    Require(bake::BakeTargetProfileHasUniformVertexStride(windows) && windows.allowsThreeComponent16BitAttributes,
        "Windows x64 has a uniform vertex stride and must allow 3x16-bit attributes");

    // Linux bakes spirv (D3D-style, 8 bytes) and glsl (GL-style, 6 bytes) into
    // one package, so the same layout would produce two different strides.
    const bake::BakeTargetProfile linuxX64 = bake::LinuxX64BakeTargetProfile();
    Require(!bake::BakeTargetProfileHasUniformVertexStride(linuxX64) && !linuxX64.allowsThreeComponent16BitAttributes,
        "Linux x64 spans both bgfx stride families and must forbid 3x16-bit attributes");
    // x86-64 fixes the base page at 4 KiB, so unlike arm64 this one really is
    // the page size and not an assumption inherited from a smaller-page era.
    Require(linuxX64.mappedBlockAlignmentBytes == 4096U, "Linux x64 mmap offsets must be page-size multiples");

    const bake::BakeTargetProfile android = bake::AndroidArm64BakeTargetProfile();
    Require(bake::HasTextureCompressionFamily(
                android.textureCompressions, bake::TextureCompressionFamily::AdaptiveScalable),
        "Android arm64 must bake ASTC textures");
    Require(!bake::HasTextureCompressionFamily(
                android.textureCompressions, bake::TextureCompressionFamily::BlockCompressedBaseline) &&
            !bake::HasTextureCompressionFamily(
                android.textureCompressions, bake::TextureCompressionFamily::BlockCompressedExtended),
        "Android arm64 must not pay for BCn textures no shipped device decodes");
    // Android 15 ships 16 KiB-page devices, where mmap rejects a 4 KiB-aligned
    // file offset. 16384 is a whole number of pages on a 4 KiB device too, so
    // one package serves both; trimming this back to 4096 is the regression.
    Require(android.mappedBlockAlignmentBytes == 16384U,
        "Android arm64 mapped blocks must clear the 16 KiB page Android 15 ships");
    Require(bake::BakeIndexWidthBytes(android.indexWidth) == 2U, "Android arm64 must bake 16-bit indices");
    Require(!bake::BakeTargetProfileHasUniformVertexStride(android) && !android.allowsThreeComponent16BitAttributes,
        "Android arm64 spans both bgfx stride families and must forbid 3x16-bit attributes");
    Require(android.maxGeometryChunkBytes < windows.maxGeometryChunkBytes,
        "Android arm64 must bake smaller geometry chunks than desktop");

    Require(bake::ShaderBakeBackendName(bake::ShaderBakeBackend::Dxbc) == "dxbc" &&
            bake::ShaderBakeBackendName(bake::ShaderBakeBackend::Dxil) == "dxil" &&
            bake::ShaderBakeBackendName(bake::ShaderBakeBackend::Spirv) == "spirv" &&
            bake::ShaderBakeBackendName(bake::ShaderBakeBackend::Glsl) == "glsl" &&
            bake::ShaderBakeBackendName(bake::ShaderBakeBackend::Essl) == "essl" &&
            bake::ShaderBakeBackendName(bake::ShaderBakeBackend::Metal) == "metal" &&
            bake::ShaderBakeBackendName(bake::ShaderBakeBackend::Wgsl) == "wgsl",
        "Shader backend names no longer match the renderer's shaders/<name> profile directories");

    bake::BakeTargetProfile found = android;
    Require(bake::TryFindBakeTargetProfile("Windows.x64", found) && found == windows,
        "A shipped profile is not resolvable by its stable identifier");
    Require(!bake::TryFindBakeTargetProfile("macOS.arm64", found) && found == windows,
        "An unknown target profile identifier must fail without touching the output");

    // The trap encoded as a rule, not only as a comment: a mixed-stride backend
    // set may never claim 3x16-bit attributes are portable.
    bake::BakeTargetProfile lying = linuxX64;
    lying.allowsThreeComponent16BitAttributes = true;
    Require(!bake::IsValidBakeTargetProfile(lying),
        "A mixed-stride profile claiming portable 3x16-bit attributes must be rejected");

    // ... while a single-family profile legitimately may.
    bake::BakeTargetProfile glOnly = android;
    glOnly.shaderBackends = bake::ShaderBakeBackendBit(bake::ShaderBakeBackend::Essl);
    glOnly.allowsThreeComponent16BitAttributes = true;
    Require(bake::IsValidBakeTargetProfile(glOnly),
        "A single-stride-family profile must be allowed to use 3x16-bit attributes");
}

// Red when: there is no Web profile at all (it did not exist before this test,
// so every assertion below failed at TryFindBakeTargetProfile), or when the one
// there is stops matching what a browser build can actually execute --
//
//  * baking WGSL, which bgfx's Emscripten build cannot load because
//    BGFX_CONFIG_RENDERER_WEBGPU leaves BX_PLATFORM_EMSCRIPTEN commented out;
//  * dropping ESSL, the only flavour a WebGL2 context (RendererType::OpenGLES)
//    can be handed;
//  * dropping either texture family -- a browser guarantees BC *or* ASTC and
//    says which only at runtime, so both must be in the package;
//  * baking BC4/BC5/BC6H/BC7, which bgfx on Emscripten reports unsupported from
//    a fixed format list however capable the GPU is;
//  * allowing 3x16-bit attributes, which WebGPU has no vertex format for at all;
//  * padding blocks out to a mapping granularity that does not exist in a
//    browser, where a `Mapped` block is a range request, not an mmap;
//  * letting a chunk grow to the desktop budget, which a wasm32 heap has to hold
//    twice while it is fetched.
//
// The Wgsl stride classification is checked here rather than on the profile,
// because no profile carries the enumerator: it is a D3D-style row in bgfx's
// table, and getting that wrong is what would let a mixed web profile bake a
// vertex buffer whose stride changes under it.
void WebProfileMatchesWhatABrowserBuildCanRun() {
    bake::BakeTargetProfile web{};
    Require(bake::TryFindBakeTargetProfile("Web.wasm32", web), "There is no Web bake target profile");
    Require(web == bake::WebWasm32BakeTargetProfile(),
        "The registered Web profile is not the one the factory returns");
    Require(bake::IsValidBakeTargetProfile(web), "The Web bake target profile is not bakeable");

    Require(bake::HasShaderBakeBackend(web.shaderBackends, bake::ShaderBakeBackend::Essl),
        "Web must bake essl -- WebGL2 arrives as bgfx RendererType::OpenGLES");
    Require(!bake::HasShaderBakeBackend(web.shaderBackends, bake::ShaderBakeBackend::Wgsl),
        "Web must not bake wgsl while bgfx's WebGPU backend is compiled out on Emscripten");

    Require(bake::HasTextureCompressionFamily(
                web.textureCompressions, bake::TextureCompressionFamily::BlockCompressedBaseline) &&
            bake::HasTextureCompressionFamily(
                web.textureCompressions, bake::TextureCompressionFamily::AdaptiveScalable),
        "Web must bake both BC1/BC3 and ASTC -- a browser guarantees one family or the other, not which");
    Require(!bake::HasTextureCompressionFamily(
                web.textureCompressions, bake::TextureCompressionFamily::BlockCompressedExtended),
        "Web must not bake BC4/BC5/BC6H/BC7 -- bgfx on Emscripten reports them unsupported");

    Require(!web.allowsThreeComponent16BitAttributes,
        "Web must forbid 3x16-bit attributes -- WebGPU has no 3-component 16-bit vertex format");
    Require(web.packageBlockAlignmentBytes == 256U,
        "Web package blocks must be 256-aligned for WebGPU's bytesPerRow rule");
    Require(web.mappedBlockAlignmentBytes == web.packageBlockAlignmentBytes,
        "Web has no memory mapping, so a mapped block must cost no alignment beyond the package one");
    Require(web.maxGeometryChunkBytes < bake::WindowsX64BakeTargetProfile().maxGeometryChunkBytes,
        "Web must bake smaller geometry chunks than desktop -- a wasm32 heap holds a chunk twice while fetching");

    Require(bake::ShaderBakeBackendStrideFamily(bake::ShaderBakeBackend::Wgsl) ==
            bake::VertexAttributeStrideFamily::Direct3DStyle,
        "WGSL must be classified D3D-style: bgfx sizes WebGPU vertex attributes from s_attribTypeSizeD3D1x");
    Require(bake::ShaderBakeBackendStrideFamily(bake::ShaderBakeBackend::Essl) ==
            bake::VertexAttributeStrideFamily::OpenGLStyle,
        "ESSL must stay GL-style, otherwise the mixed-stride rule cannot see a web profile split");

    // A hypothetical web profile that gained WGSL would span both stride tables,
    // which is exactly why the shipped one may not claim 3x16-bit portability.
    bake::BakeTargetProfile withWebGpu = web;
    withWebGpu.shaderBackends |= bake::ShaderBakeBackendBit(bake::ShaderBakeBackend::Wgsl);
    Require(!bake::BakeTargetProfileHasUniformVertexStride(withWebGpu),
        "essl plus wgsl must read as a mixed-stride backend set");
    withWebGpu.allowsThreeComponent16BitAttributes = true;
    Require(!bake::IsValidBakeTargetProfile(withWebGpu),
        "A web profile spanning WebGL and WebGPU must not claim portable 3x16-bit attributes");
}

// Red when: ANY ONE of the validator's rejection rules stops firing. Every
// rejected case below differs from a bakeable profile by exactly one rule, so
// dropping the identifier check, either empty-set check, either unknown-bit
// check, either power-of-two check, the mapped-versus-package divisibility
// check, the chunk-budget check or the 3x16-bit stride rule turns exactly one
// named case red instead of hiding behind the rules that still work. The
// accepted cases stop the validator from simply refusing everything -- and the
// shipped Web profile is among them, so a Web profile whose alignments do not
// satisfy mapped % package == 0 is rejected here rather than shipped.
void ProfileValidationCoversEveryRejectionRule() {
    const bake::BakeTargetProfile uniform = bake::WindowsX64BakeTargetProfile();
    const bake::BakeTargetProfile mixed = bake::LinuxX64BakeTargetProfile();
    // Storage for the over-long identifier: the profile only borrows it.
    const std::string overlongIdentifier(bake::kMaxBakeCacheNameBytes + 1U, 'a');

    struct ProfileCase {
        const char* label;
        bake::BakeTargetProfile profile;
        bool bakeable;
    };

    std::vector<ProfileCase> cases;
    cases.push_back({ "the shipped Windows x64 profile", uniform, true });
    cases.push_back({ "the shipped Linux x64 profile", mixed, true });
    cases.push_back({ "the shipped Android arm64 profile", bake::AndroidArm64BakeTargetProfile(), true });
    cases.push_back({ "the shipped Web wasm32 profile", bake::WebWasm32BakeTargetProfile(), true });

    bake::BakeTargetProfile probe = uniform;
    probe.textureCompressions = 0U;
    cases.push_back({ "a profile that bakes no texture compression family at all", probe, false });

    probe = uniform;
    probe.textureCompressions |= static_cast<bake::TextureCompressionFamilyMask>(1U)
        << bake::kTextureCompressionFamilyCount;
    cases.push_back({ "a profile carrying a texture compression family bit no baker knows", probe, false });

    probe = uniform;
    probe.identifier = {};
    cases.push_back({ "a profile with an empty identifier", probe, false });

    probe = uniform;
    probe.identifier = "Windows/x64";
    cases.push_back({ "a profile whose identifier holds a path separator", probe, false });

    probe = uniform;
    probe.identifier = "con";
    cases.push_back({ "a profile named after a reserved Win32 device", probe, false });

    probe = uniform;
    probe.identifier = overlongIdentifier;
    cases.push_back({ "a profile whose identifier is longer than a bake cache name may be", probe, false });

    probe = uniform;
    probe.shaderBackends = 0U;
    // Cleared as well, so the empty set is the ONLY rule this case breaks.
    probe.allowsThreeComponent16BitAttributes = false;
    cases.push_back({ "a profile that bakes no shader backend at all", probe, false });

    probe = uniform;
    probe.shaderBackends |= static_cast<bake::ShaderBakeBackendMask>(1U) << bake::kShaderBakeBackendCount;
    cases.push_back({ "a profile carrying a shader backend bit the renderer does not know", probe, false });

    probe = uniform;
    probe.packageBlockAlignmentBytes = 0U;
    cases.push_back({ "a profile with a zero package block alignment", probe, false });

    probe = uniform;
    probe.packageBlockAlignmentBytes = 24U;
    probe.mappedBlockAlignmentBytes = 24U;
    cases.push_back({ "a profile whose alignments are not powers of two", probe, false });

    probe = uniform;
    probe.mappedBlockAlignmentBytes = 0U;
    cases.push_back({ "a profile with a zero mapped block alignment", probe, false });

    probe = uniform;
    probe.packageBlockAlignmentBytes = 16U;
    probe.mappedBlockAlignmentBytes = 8U;
    cases.push_back({ "a profile whose mapped alignment is not a whole number of package alignments", probe, false });

    probe = uniform;
    probe.maxGeometryChunkBytes = 0U;
    cases.push_back({ "a profile with no geometry chunk budget", probe, false });

    probe = mixed;
    probe.allowsThreeComponent16BitAttributes = true;
    cases.push_back({ "a mixed-stride profile claiming portable 3x16-bit attributes", probe, false });

    probe = uniform;
    probe.packageBlockAlignmentBytes = 1U;
    probe.mappedBlockAlignmentBytes = 1U;
    cases.push_back({ "a profile that asks for no alignment at all", probe, true });

    probe = uniform;
    probe.shaderBackends = bake::ShaderBakeBackendBit(bake::ShaderBakeBackend::Metal);
    cases.push_back({ "a Metal-only profile, whose bit is a known backend", probe, true });

    for (const ProfileCase& item : cases) {
        const bool bakeable = bake::IsValidBakeTargetProfile(item.profile);
        Require(bakeable == item.bakeable,
            (std::string{ "IsValidBakeTargetProfile disagrees about " } + item.label).c_str());
    }
}

// Red when: the reserved-device rule stops covering any one of the names Win32
// refuses as a file name whatever follows them. All twenty-four the contract
// lists are checked individually, in lower case, in upper case and with an
// extension, so trimming the rule to a couple of families turns a NAMED case
// red instead of slipping past a sampled list. The look-alikes below keep the
// rule from growing into a prefix match that rejects ordinary names.
void ReservedWin32DeviceNamesAreAllRejected() {
    constexpr std::array<std::string_view, 24U> kReservedDevices{ "con", "prn", "aux", "nul", "com0", "com1", "com2",
        "com3", "com4", "com5", "com6", "com7", "com8", "com9", "lpt0", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5",
        "lpt6", "lpt7", "lpt8", "lpt9" };
    for (const std::string_view device : kReservedDevices) {
        const std::string lower{ device };
        const std::string upper = ToUpperAscii(device);
        Require(!bake::IsValidBakeCacheName(lower),
            (std::string{ "A reserved Win32 device name was accepted: " } + lower).c_str());
        Require(!bake::IsValidBakeCacheName(upper),
            (std::string{ "A reserved Win32 device name was accepted in upper case: " } + upper).c_str());
        Require(!bake::IsValidBakeCacheName(lower + std::string{ bake::kBakedAssetBlockExtension }),
            (std::string{ "A reserved Win32 device name was accepted with a block extension: " } + lower).c_str());
        Require(!bake::IsValidBakeCacheName(upper + ".v2.bin"),
            (std::string{ "A reserved Win32 device name was accepted with an extension: " } + upper).c_str());
    }

    constexpr std::array<std::string_view, 10U> kPortableLookalikes{ "com", "lpt", "con1", "aux2", "nul9", "com10",
        "lpt10", "console", "auxiliary", "printer" };
    for (const std::string_view name : kPortableLookalikes) {
        Require(bake::IsValidBakeCacheName(name),
            (std::string{ "An ordinary name the file system accepts was rejected as a device: " } +
                std::string{ name })
                .c_str());
    }
}

// Red when: Digest() starts depending on anything but the field values --
// dropping the sort/unique of `dependencies` makes the shuffled, duplicated
// list disagree with the canonical one, and folding in an address, an
// iteration order or a wall clock makes two independently built keys disagree.
// The literal below was produced by an independent implementation of the
// documented canonical encoding, so it also goes red if the field order, the
// length prefixes or the little-endian byte order ever change.
void KeyIsDeterministicAndMachineIndependent() {
    const bake::AssetBakeKey first = MakeMeshKey();
    const bake::AssetBakeKey second = MakeMeshKey();
    Require(first.Digest() == second.Digest(),
        "Two independently built keys with the same state produced different digests");
    Require(first.Digest() == first.Digest(), "Digest() is not stable across calls on one key");

    bake::AssetBakeKey shuffled = MakeMeshKey();
    shuffled.dependencies = { bake::AssetBakeDigest{ .high = 0U, .low = 9U },
        bake::AssetBakeDigest{ .high = 1U, .low = 2U }, bake::AssetBakeDigest{ .high = 0U, .low = 9U },
        bake::AssetBakeDigest{ .high = 1U, .low = 2U } };
    Require(shuffled.Digest() == first.Digest(),
        "Dependency order or duplication leaked into the bake key");

    Require(first.ToString() == "d617b6e706fcffa5c4abcab1ec993c83",
        "The canonical bake key encoding changed; every cached artifact would be orphaned");
}

// Red when: the key stops carrying what a profile SAYS and carries only what it
// is CALLED. A profile identifier is stable by design -- "Windows.x64" is still
// "Windows.x64" after its alignment doubles -- so a key built from the name
// alone would hand back artifacts baked under answers no baker gives any more,
// with nothing to notice it. Also red if a baker can leave the fingerprint out:
// an unfingerprinted key must be refused, not written.
void KeyCarriesWhatTheProfileSaysNotOnlyItsName() {
    const bake::BakeTargetProfile windows = bake::WindowsX64BakeTargetProfile();
    Require(bake::BakeTargetProfileFingerprint(windows) != 0U,
        "A profile fingerprint of zero is indistinguishable from a key that never asked");
    Require(bake::BakeTargetProfileFingerprint(windows) == bake::BakeTargetProfileFingerprint(windows),
        "A profile fingerprint is not stable across calls");

    // Every field a baker reads must move the fingerprint. Each edit below leaves
    // the identifier alone, which is exactly the case the name could not catch.
    bake::BakeTargetProfile edited = windows;
    edited.packageBlockAlignmentBytes = 512U;
    Require(bake::BakeTargetProfileFingerprint(edited) != bake::BakeTargetProfileFingerprint(windows),
        "A block alignment change did not move the profile fingerprint");
    edited = windows;
    edited.textureCompressions |= bake::TextureCompressionFamilyBit(bake::TextureCompressionFamily::AdaptiveScalable);
    Require(bake::BakeTargetProfileFingerprint(edited) != bake::BakeTargetProfileFingerprint(windows),
        "A texture family change did not move the profile fingerprint");
    edited = windows;
    edited.shaderBackends |= bake::ShaderBakeBackendBit(bake::ShaderBakeBackend::Glsl);
    Require(bake::BakeTargetProfileFingerprint(edited) != bake::BakeTargetProfileFingerprint(windows),
        "A shader backend change did not move the profile fingerprint");
    edited = windows;
    edited.indexWidth = bake::BakeIndexWidth::Bits16;
    Require(bake::BakeTargetProfileFingerprint(edited) != bake::BakeTargetProfileFingerprint(windows),
        "An index width change did not move the profile fingerprint");
    edited = windows;
    edited.allowsThreeComponent16BitAttributes = !windows.allowsThreeComponent16BitAttributes;
    Require(bake::BakeTargetProfileFingerprint(edited) != bake::BakeTargetProfileFingerprint(windows),
        "The 3x16-bit attribute rule did not move the profile fingerprint");
    edited = windows;
    edited.mappedBlockAlignmentBytes = windows.mappedBlockAlignmentBytes * 2U;
    Require(bake::BakeTargetProfileFingerprint(edited) != bake::BakeTargetProfileFingerprint(windows),
        "A mapping alignment change did not move the profile fingerprint");
    edited = windows;
    edited.maxGeometryChunkBytes = windows.maxGeometryChunkBytes / 2U;
    Require(bake::BakeTargetProfileFingerprint(edited) != bake::BakeTargetProfileFingerprint(windows),
        "A geometry chunk budget change did not move the profile fingerprint");

    // Two profiles that differ in a field but not in a name must not share a key.
    bake::AssetBakeKey before = MakeMeshKey();
    before.targetProfileHash = bake::BakeTargetProfileFingerprint(windows);
    bake::BakeTargetProfile widened = windows;
    widened.packageBlockAlignmentBytes = 512U;
    bake::AssetBakeKey after = before;
    after.targetProfileHash = bake::BakeTargetProfileFingerprint(widened);
    Require(before.targetProfileId == after.targetProfileId,
        "This test only means something while both keys carry the same profile name");
    Require(before.Digest() != after.Digest(),
        "An edited profile kept its bake key, so artifacts baked under the old one stay addressable");

    // And a baker that never asked is refused rather than quietly written.
    bake::AssetBakeKey unfingerprinted = before;
    unfingerprinted.targetProfileHash = 0U;
    Require(before.IsValid() && !unfingerprinted.IsValid(),
        "A key with no profile fingerprint must be refused, not written");
}

// Red when: bakerVersion, targetProfileId, sourceContentHash or settingsHash
// stops feeding the digest, when bakerId stops scoping the version bump (a
// texture-baker bump would then move mesh keys too), or when a dependency's
// digest stops propagating into its parent.
void KeyInvalidationIsScopedAndPropagates() {
    const bake::AssetBakeKey baseline = MakeMeshKey();

    bake::AssetBakeKey bumped = baseline;
    bumped.bakerVersion = "4";
    Require(bumped.Digest() != baseline.Digest(), "A baker version bump did not move the bake key");

    bake::AssetBakeKey retargeted = baseline;
    retargeted.targetProfileId = std::string{ bake::AndroidArm64BakeTargetProfile().identifier };
    Require(retargeted.Digest() != baseline.Digest(), "A target profile change did not move the bake key");

    bake::AssetBakeKey resourced = baseline;
    resourced.sourceContentHash ^= 1ULL;
    Require(resourced.Digest() != baseline.Digest(), "A source content change did not move the bake key");

    bake::AssetBakeKey resettled = baseline;
    resettled.settingsHash = bake::HashBakeText("compression=fast");
    Require(resettled.Digest() != baseline.Digest(), "A bake settings change did not move the bake key");

    // A second baker at the same version, source and profile must own a
    // separate key, and must not move when the first baker is bumped.
    bake::AssetBakeKey texture = baseline;
    texture.bakerId = "Texture";
    const bake::AssetBakeDigest textureBefore = texture.Digest();
    Require(textureBefore != baseline.Digest(), "Two bakers share one artifact identity");
    Require(texture.Digest() == textureBefore && bumped.Digest() != textureBefore,
        "A baker version bump reached another baker's artifacts");

    // Dependency propagation: the parent never re-reads the child's source, it
    // only carries the child's digest.
    bake::AssetBakeKey child = baseline;
    child.bakerId = "Skeleton";
    bake::AssetBakeKey parent = baseline;
    parent.dependencies = { child.Digest() };
    const bake::AssetBakeDigest parentBefore = parent.Digest();

    bake::AssetBakeKey standalone = baseline;
    standalone.dependencies.clear();
    Require(parentBefore != standalone.Digest(), "A dependency did not contribute to its parent's key");

    child.sourceContentHash ^= 0xFFULL;
    parent.dependencies = { child.Digest() };
    Require(parent.Digest() != parentBefore, "A changed dependency key did not propagate to the parent key");
}

// Red when: ToString stops emitting a fixed-length lowercase hex digest, or
// IsValidBakeCacheName starts accepting a name the file system will not take
// (a separator, a reserved Win32 device, a leading dot, an over-long name) --
// the create/read round trip below then fails on a real directory.
void KeyIsUsableAsAFileName() {
    const std::string text = MakeMeshKey().ToString();
    Require(text.size() == 32U, "A bake key file name must be exactly 32 hex characters");
    Require(std::ranges::all_of(text, [](char character) noexcept {
        return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
    }),
        "A bake key file name must be lowercase hex only");
    Require(bake::IsValidBakeCacheName(text), "A bake key is not a valid bake cache name");

    Require(bake::IsValidBakeCacheName("primary") && bake::IsValidBakeCacheName("mip-tail.v2") &&
            bake::IsValidBakeCacheName("Windows.x64"),
        "A portable bake cache name was rejected");
    Require(!bake::IsValidBakeCacheName("") && !bake::IsValidBakeCacheName("a/b") &&
            !bake::IsValidBakeCacheName("a b") && !bake::IsValidBakeCacheName(".hidden") &&
            !bake::IsValidBakeCacheName("-flag") && !bake::IsValidBakeCacheName("trailing.") &&
            !bake::IsValidBakeCacheName("con") && !bake::IsValidBakeCacheName("LPT9.kbblock") &&
            !bake::IsValidBakeCacheName(std::string(65U, 'a')),
        "An unusable bake cache name was accepted");

    const std::filesystem::path root = TestRoot() / "filename";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    {
        std::ofstream output{ root / text, std::ios::binary | std::ios::trunc };
        output << "artifact";
    }
    Require(ReadFileText(root / text) == "artifact", "The file system rejected a bake key as a file name");
    std::filesystem::remove_all(root);
}

// Red when: CommitAsset stops publishing the staged blocks, publishes them
// before it is called, writes them anywhere other than BlockPath reports, or
// mangles the bytes on the way through.
void CommittedAssetIsReadable() {
    const std::filesystem::path root = TestRoot() / "commit";
    std::filesystem::remove_all(root);

    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    const bake::BakedAssetDescriptor descriptor = MakeMeshDescriptor();

    const std::vector<std::uint8_t> primary = Bytes("resident-payload");
    const std::vector<std::uint8_t> streaming = Bytes("streamed-lod-tail");
    const bake::BakedAssetBlock streamingBlock{ .name = "lod-tail",
        .residency = bake::BakedAssetBlockResidency::Streaming,
        .alignmentBytes = profile.mappedBlockAlignmentBytes };

    {
        bake::LooseBakedAssetSink sink{ root };
        Require(sink.BeginAsset(descriptor) == bake::BakedAssetSinkStatus::Success, "BeginAsset failed");
        Require(sink.WritePrimaryBlock(primary, profile.packageBlockAlignmentBytes) ==
                bake::BakedAssetSinkStatus::Success,
            "WritePrimaryBlock failed");
        Require(sink.WriteAuxiliaryBlock(streamingBlock, streaming) == bake::BakedAssetSinkStatus::Success,
            "WriteAuxiliaryBlock failed");
        Require(!std::filesystem::exists(sink.BlockPath(descriptor, bake::kBakedAssetPrimaryBlockName)),
            "A block became readable before the asset was committed");
        Require(sink.CommitAsset() == bake::BakedAssetSinkStatus::Success, "CommitAsset failed");

        Require(ReadFileText(sink.BlockPath(descriptor, bake::kBakedAssetPrimaryBlockName)) == "resident-payload",
            "The committed primary block is not readable");
        Require(ReadFileText(sink.BlockPath(descriptor, streamingBlock.name)) == "streamed-lod-tail",
            "The committed streaming block is not readable");
        Require(CountEntries(sink.AssetDirectory(descriptor)) == 2U,
            "The committed asset directory holds something other than its two blocks");
        Require(!std::filesystem::exists(root / ".kbbakestaging"),
            "A committed asset left its staging directory behind");
    }

    // The store is content-addressed, so re-baking the same key republishes
    // onto itself instead of failing or duplicating.
    {
        bake::LooseBakedAssetSink sink{ root };
        Require(sink.BeginAsset(descriptor) == bake::BakedAssetSinkStatus::Success, "Re-bake BeginAsset failed");
        Require(sink.WritePrimaryBlock(primary, profile.packageBlockAlignmentBytes) ==
                bake::BakedAssetSinkStatus::Success,
            "Re-bake WritePrimaryBlock failed");
        Require(sink.CommitAsset() == bake::BakedAssetSinkStatus::Success,
            "Re-baking an already published key must succeed");
        Require(ReadFileText(sink.BlockPath(descriptor, streamingBlock.name)) == "streamed-lod-tail",
            "Re-baking a published key destroyed the published artifact");
        Require(!std::filesystem::exists(root / ".kbbakestaging"), "A re-bake left its staging directory behind");
    }

    std::filesystem::remove_all(root);
}

// Red when: publication stops being ONE move of the whole staging directory --
// the mutation that swaps the single rename for a per-block publish leaves a
// half-published artifact observable under a key that claims to be complete.
// Two observations pin the mechanism, and only a whole-directory move satisfies
// both: a file the sink never wrote is carried across (so the blocks were not
// published one at a time), and the published primary block is the SAME file
// object a hard link was taken on before the commit (so nothing was copied).
void PublicationMovesTheWholeStagingDirectoryAtOnce() {
    const std::filesystem::path root = TestRoot() / "atomic";
    const std::filesystem::path linkDirectory = TestRoot() / "atomic-link";
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(linkDirectory);
    std::filesystem::create_directories(linkDirectory);

    const bake::BakedAssetDescriptor descriptor = MakeMeshDescriptor();
    const std::string primaryFileName =
        std::string{ bake::kBakedAssetPrimaryBlockName } + std::string{ bake::kBakedAssetBlockExtension };

    bake::LooseBakedAssetSink sink{ root };
    Require(sink.BeginAsset(descriptor) == bake::BakedAssetSinkStatus::Success, "Atomic publish BeginAsset failed");
    Require(sink.WritePrimaryBlock(Bytes("resident-payload"), 16U) == bake::BakedAssetSinkStatus::Success,
        "Atomic publish WritePrimaryBlock failed");
    Require(sink.WriteAuxiliaryBlock({ .name = "lod-tail", .alignmentBytes = 16U }, Bytes("streamed-lod-tail")) ==
            bake::BakedAssetSinkStatus::Success,
        "Atomic publish WriteAuxiliaryBlock failed");

    const std::filesystem::path staging = SoleStagingDirectory(root);
    {
        std::ofstream witness{ staging / "witness.marker", std::ios::binary | std::ios::trunc };
        witness << "carried";
    }
    const std::filesystem::path link = linkDirectory / "primary.hardlink";
    std::filesystem::create_hard_link(staging / primaryFileName, link);

    Require(sink.CommitAsset() == bake::BakedAssetSinkStatus::Success, "Atomic publish CommitAsset failed");

    const std::filesystem::path published = sink.AssetDirectory(descriptor);
    Require(!std::filesystem::exists(staging),
        "The staging directory survived publication, so it was copied rather than moved");
    Require(ReadFileText(published / "witness.marker") == "carried",
        "Publication did not carry the staging directory across whole; the blocks were published one by one");
    Require(std::filesystem::equivalent(link, published / primaryFileName),
        "The published primary block is a different file object, so publication copied instead of moving");
    Require(ReadFileText(published / primaryFileName) == "resident-payload",
        "The published primary block lost its bytes");
    Require(!std::filesystem::exists(root / ".kbbakestaging"),
        "An atomically published asset left its staging root behind");

    std::filesystem::remove_all(linkDirectory);
    std::filesystem::remove_all(root);
}

// Red when: CommitAsset decides "already published" from exists() instead of
// checking that a COMPLETE artifact sits on the path. An empty directory, a
// plain file, a directory that lost a block and a zero-length block file all
// satisfy exists(); each one used to make CommitAsset answer Success, throw the
// staged blocks away, and leave the content-addressed key permanently unable to
// produce the bytes it promises. The last case keeps the repair from turning
// into an unconditional clobber of a genuinely published artifact.
void CommitRefusesToTreatDebrisAsAPublishedArtifact() {
    const std::filesystem::path root = TestRoot() / "debris";
    std::filesystem::remove_all(root);

    const bake::BakedAssetDescriptor descriptor = MakeMeshDescriptor();
    const bake::LooseBakedAssetSink paths{ root };
    const std::filesystem::path published = paths.AssetDirectory(descriptor);
    const std::filesystem::path primaryPath = paths.BlockPath(descriptor, bake::kBakedAssetPrimaryBlockName);
    const std::filesystem::path auxiliaryPath = paths.BlockPath(descriptor, "lod-tail");
    Require(!published.empty() && !primaryPath.empty() && !auxiliaryPath.empty(),
        "The debris store paths are not addressable");

    // An empty directory standing where the artifact belongs.
    std::filesystem::create_directories(published);
    Require(std::filesystem::is_empty(published), "Test setup: the debris directory should be empty");
    Require(BakeTwoBlockArtifact(root, descriptor, "resident", "tail") == bake::BakedAssetSinkStatus::Success,
        "A bake over an empty artifact directory failed");
    Require(ReadFileText(primaryPath) == "resident" && ReadFileText(auxiliaryPath) == "tail",
        "An empty directory was taken for a published artifact and the staged blocks were dropped");

    // A plain FILE standing where the artifact directory belongs.
    std::filesystem::remove_all(published);
    std::filesystem::create_directories(published.parent_path());
    {
        std::ofstream impostor{ published, std::ios::binary | std::ios::trunc };
        impostor << "not a directory";
    }
    Require(std::filesystem::is_regular_file(published), "Test setup: the impostor should be a plain file");
    Require(BakeTwoBlockArtifact(root, descriptor, "resident", "tail") == bake::BakedAssetSinkStatus::Success,
        "A bake over a plain file on the artifact path failed");
    Require(std::filesystem::is_directory(published), "A plain file was taken for a published artifact directory");
    Require(ReadFileText(primaryPath) == "resident" && ReadFileText(auxiliaryPath) == "tail",
        "A plain file was taken for a published artifact and the staged blocks were dropped");

    // A directory that lost one of its blocks.
    std::filesystem::remove(auxiliaryPath);
    Require(BakeTwoBlockArtifact(root, descriptor, "resident", "tail") == bake::BakedAssetSinkStatus::Success,
        "A bake over an artifact directory missing a block failed");
    Require(ReadFileText(auxiliaryPath) == "tail",
        "An artifact directory missing one of its blocks was taken for a complete one");

    // A block file truncated to nothing is not a block either.
    {
        std::ofstream truncate{ auxiliaryPath, std::ios::binary | std::ios::trunc };
        Require(truncate.is_open(), "Test setup: the block file could not be truncated");
    }
    Require(std::filesystem::file_size(auxiliaryPath) == 0U, "Test setup: the block should be truncated");
    Require(BakeTwoBlockArtifact(root, descriptor, "resident", "tail") == bake::BakedAssetSinkStatus::Success,
        "A bake over a truncated block failed");
    Require(ReadFileText(auxiliaryPath) == "tail", "A truncated block was taken for a complete artifact");

    // ... and the content-addressed rule still holds for a COMPLETE artifact:
    // it is kept, never republished over.
    Require(BakeTwoBlockArtifact(root, descriptor, "second-bake", "second-tail") ==
            bake::BakedAssetSinkStatus::Success,
        "Re-baking a complete published artifact failed");
    Require(ReadFileText(primaryPath) == "resident" && ReadFileText(auxiliaryPath) == "tail",
        "A complete published artifact was replaced by a re-bake of the same key");

    Require(!std::filesystem::exists(root / ".kbbakestaging"),
        "A bake over debris left its staging directory behind");
    std::filesystem::remove_all(root);
}

// Red when: a staging directory left by a process killed mid-bake stays in the
// store forever. Nothing sweeps it on its own -- the owner never ran its abort,
// and every later successful bake used to prune only its own directory and then
// fail to remove the non-empty staging root. The second half keeps the sweep
// from taking a staging directory a concurrently running bake could still own.
void OrphanedStagingIsSweptByTheNextBake() {
    const std::filesystem::path root = TestRoot() / "orphan";
    std::filesystem::remove_all(root);

    const bake::BakedAssetDescriptor descriptor = MakeMeshDescriptor();
    const std::filesystem::path stagingRoot = root / ".kbbakestaging";
    const std::filesystem::path stale = stagingRoot / "00000000000000000000000000000000.0";
    std::filesystem::create_directories(stale);
    {
        std::ofstream halfBaked{ stale / "primary.kbblock", std::ios::binary | std::ios::trunc };
        halfBaked << "half-baked";
    }
    std::filesystem::last_write_time(stale,
        std::filesystem::file_time_type::clock::now() - bake::kOrphanedStagingMaxAge - std::chrono::hours{ 1 });

    Require(BakeTwoBlockArtifact(root, descriptor, "resident", "tail") == bake::BakedAssetSinkStatus::Success,
        "A bake next to orphaned staging debris failed");
    Require(!std::filesystem::exists(stale),
        "A staging directory left behind by a killed bake survived the next successful bake");
    Require(!std::filesystem::exists(stagingRoot),
        "The hidden staging root outlived the orphaned directory it held");

    const std::filesystem::path fresh = stagingRoot / "11111111111111111111111111111111.0";
    std::filesystem::create_directories(fresh);
    Require(BakeTwoBlockArtifact(root, descriptor, "resident", "tail") == bake::BakedAssetSinkStatus::Success,
        "A bake next to a live staging directory failed");
    Require(std::filesystem::exists(fresh),
        "The sweep removed a staging directory young enough for a concurrent bake to still own");

    std::filesystem::remove_all(root);
}

// Red when: the loose sink addresses its store through an ordinary Win32 path.
// Every name below sits at kMaxBakeCacheNameBytes, which the store's own
// contract promises to accept, and the published block path then passes Win32's
// MAX_PATH (260) while the much shorter staging path stays inside it. That
// asymmetry is what made the failure look like success: BeginAsset,
// WritePrimaryBlock, WriteAuxiliaryBlock and CommitAsset all returned Success,
// the block could not be opened through the path BlockPath itself reports, and
// remove_all could not delete the store again afterwards.
void LongStorePathsArePublishedAndReadable() {
    const std::string longName(bake::kMaxBakeCacheNameBytes, 'a');
    Require(bake::IsValidBakeCacheName(longName), "Test setup: a maximum-length name must be a legal cache name");

    std::filesystem::path root = TestRoot() / "longpath";
    // Padded so the published block path clears MAX_PATH on any machine, while
    // the staging path stays well inside it.
    while (root.native().size() < 60U) {
        root /= "d";
    }

    bake::BakedAssetDescriptor descriptor = MakeMeshDescriptor();
    descriptor.key.bakerId = longName;
    descriptor.key.targetProfileId = longName;
    descriptor.assetTypeId = longName;

    bake::LooseBakedAssetSink sink{ root };
    const std::filesystem::path blockPath = sink.BlockPath(descriptor, longName);
    Require(!blockPath.empty(), "A store path built from maximum-length names must still be addressable");
    Require(blockPath.native().size() > 260U,
        "Test setup: the published block path no longer clears the Win32 MAX_PATH budget");

    // Only the sink's own paths are long enough to reach a store a previous
    // failing run may have left behind, so the purge goes through them.
    const std::filesystem::path storeRoot =
        sink.AssetDirectory(descriptor).parent_path().parent_path().parent_path();
    std::error_code purgeError;
    std::filesystem::remove_all(storeRoot, purgeError);

    Require(sink.BeginAsset(descriptor) == bake::BakedAssetSinkStatus::Success, "Long-path BeginAsset failed");
    Require(sink.WritePrimaryBlock(Bytes("resident-payload"), 16U) == bake::BakedAssetSinkStatus::Success,
        "Long-path WritePrimaryBlock failed");
    Require(sink.WriteAuxiliaryBlock({ .name = longName, .alignmentBytes = 16U }, Bytes("deep-tail")) ==
            bake::BakedAssetSinkStatus::Success,
        "Long-path WriteAuxiliaryBlock failed");
    Require(sink.CommitAsset() == bake::BakedAssetSinkStatus::Success, "Long-path CommitAsset failed");

    Require(ReadFileText(blockPath) == "deep-tail",
        "A block committed under a long store path is not readable through the path BlockPath reports");
    Require(ReadFileText(sink.BlockPath(descriptor, bake::kBakedAssetPrimaryBlockName)) == "resident-payload",
        "A primary block committed under a long store path is not readable");
    Require(CountEntries(sink.AssetDirectory(descriptor)) == 2U,
        "The committed long-path asset directory holds something other than its two blocks");

    std::error_code cleanupError;
    std::filesystem::remove_all(storeRoot, cleanupError);
    Require(!cleanupError && !std::filesystem::exists(storeRoot),
        "A store published under a long path cannot be deleted again");
}

// Red when: the path budget is checked after the sink has already written, or
// not at all. The root below cannot hold ANY legal block path inside
// kMaxBakeStorePathLength, so BeginAsset has to refuse it before it creates the
// hidden staging root, AssetDirectory and BlockPath have to report the same
// refusal, and no artifact may be left open behind the failure.
void OverlongStoreRootIsRefusedBeforeAnythingIsWritten() {
    const std::filesystem::path base = TestRoot() / "overlong";
    std::filesystem::path root = base;
    while (root.native().size() < bake::kMaxBakeStorePathLength) {
        root /= std::string(200U, 'p');
    }

    const bake::BakedAssetDescriptor descriptor = MakeMeshDescriptor();
    bake::LooseBakedAssetSink sink{ root };
    Require(sink.AssetDirectory(descriptor).empty(),
        "AssetDirectory reported a path for a store root the sink cannot serve");
    Require(sink.BlockPath(descriptor, bake::kBakedAssetPrimaryBlockName).empty(),
        "BlockPath reported a path for a store root the sink cannot serve");
    Require(sink.BeginAsset(descriptor) == bake::BakedAssetSinkStatus::PathTooLong,
        "A store root past the path budget was accepted");
    Require(sink.WritePrimaryBlock(Bytes("payload"), 16U) == bake::BakedAssetSinkStatus::NoAssetOpen,
        "A refused BeginAsset left an artifact open");
    Require(!std::filesystem::exists(base), "A refused store root was created on disk anyway");
    Require(bake::ToString(bake::BakedAssetSinkStatus::PathTooLong) == "PathTooLong",
        "BakedAssetSinkStatus::PathTooLong has no name");
}

// Red when: AbortAsset (or the destructor's abort) stops removing the staging
// directory, or leaves the hidden staging root behind -- the store must be
// byte-for-byte what it was before BeginAsset.
void AbortedAssetLeavesNothingBehind() {
    const std::filesystem::path root = TestRoot() / "abort";
    std::filesystem::remove_all(root);

    const bake::BakedAssetDescriptor descriptor = MakeMeshDescriptor();
    const std::vector<std::uint8_t> payload = Bytes("half-baked");

    {
        bake::LooseBakedAssetSink sink{ root };
        Require(sink.BeginAsset(descriptor) == bake::BakedAssetSinkStatus::Success, "Abort test BeginAsset failed");
        Require(sink.WritePrimaryBlock(payload, 16U) == bake::BakedAssetSinkStatus::Success,
            "Abort test WritePrimaryBlock failed");
        sink.AbortAsset();
        Require(std::filesystem::is_empty(root), "An aborted asset left files behind");
        Require(!std::filesystem::exists(sink.AssetDirectory(descriptor)), "An aborted asset was published anyway");
    }

    // A sink destroyed with an artifact still open must abort it, not publish
    // a half-written one.
    {
        bake::LooseBakedAssetSink sink{ root };
        Require(sink.BeginAsset(descriptor) == bake::BakedAssetSinkStatus::Success, "Dropped bake BeginAsset failed");
        Require(sink.WritePrimaryBlock(payload, 16U) == bake::BakedAssetSinkStatus::Success,
            "Dropped bake WritePrimaryBlock failed");
    }
    Require(std::filesystem::is_empty(root), "A sink destroyed mid-bake left files behind");

    std::filesystem::remove_all(root);
}

// Red when: the sink stops reporting a protocol violation and silently accepts
// it -- an unopened asset, a second primary block, a name that collides with
// the primary or with another block on a case-insensitive file system, a
// non-power-of-two alignment, an empty PRIMARY block, an empty AUXILIARY block,
// or a commit with no payload. The empty-auxiliary case also has to leave no
// trace: a rejected block must not reach the published artifact.
void SinkRejectsProtocolViolations() {
    const std::filesystem::path root = TestRoot() / "protocol";
    std::filesystem::remove_all(root);

    const bake::BakedAssetDescriptor descriptor = MakeMeshDescriptor();
    const std::vector<std::uint8_t> payload = Bytes("payload");

    bake::LooseBakedAssetSink sink{ root };
    Require(sink.WritePrimaryBlock(payload, 16U) == bake::BakedAssetSinkStatus::NoAssetOpen,
        "Writing without an open asset was accepted");
    Require(sink.WriteAuxiliaryBlock({ .name = "extra" }, payload) == bake::BakedAssetSinkStatus::NoAssetOpen,
        "Writing an auxiliary block without an open asset was accepted");
    Require(sink.CommitAsset() == bake::BakedAssetSinkStatus::NoAssetOpen,
        "Committing without an open asset was accepted");

    bake::BakedAssetDescriptor brokenKey = descriptor;
    brokenKey.key.bakerId.clear();
    Require(sink.BeginAsset(brokenKey) == bake::BakedAssetSinkStatus::InvalidKey, "An invalid bake key was accepted");
    bake::BakedAssetDescriptor brokenType = descriptor;
    brokenType.assetTypeId = "Skeletal/Mesh";
    Require(sink.BeginAsset(brokenType) == bake::BakedAssetSinkStatus::InvalidAssetType,
        "An asset type that is not a path component was accepted");

    Require(sink.BeginAsset(descriptor) == bake::BakedAssetSinkStatus::Success, "Protocol test BeginAsset failed");
    Require(sink.BeginAsset(descriptor) == bake::BakedAssetSinkStatus::AssetAlreadyOpen,
        "A second asset was opened while one was still open");
    Require(sink.CommitAsset() == bake::BakedAssetSinkStatus::MissingPrimaryBlock,
        "An asset with no payload was committed");

    Require(sink.WritePrimaryBlock(payload, 24U) == bake::BakedAssetSinkStatus::InvalidAlignment,
        "A non-power-of-two alignment was accepted");
    Require(sink.WritePrimaryBlock({}, 16U) == bake::BakedAssetSinkStatus::EmptyBlock,
        "An empty primary block was accepted");
    Require(sink.WritePrimaryBlock(payload, 16U) == bake::BakedAssetSinkStatus::Success,
        "Protocol test WritePrimaryBlock failed");
    Require(sink.WritePrimaryBlock(payload, 16U) == bake::BakedAssetSinkStatus::DuplicateBlock,
        "A second primary block was accepted");

    Require(sink.WriteAuxiliaryBlock({ .name = "PRIMARY" }, payload) == bake::BakedAssetSinkStatus::InvalidBlockName,
        "An auxiliary block colliding with the reserved primary name was accepted");
    Require(sink.WriteAuxiliaryBlock({ .name = "bad/name" }, payload) == bake::BakedAssetSinkStatus::InvalidBlockName,
        "An auxiliary block name that is not a path component was accepted");
    Require(sink.WriteAuxiliaryBlock({ .name = "misaligned", .alignmentBytes = 24U }, payload) ==
            bake::BakedAssetSinkStatus::InvalidAlignment,
        "A non-power-of-two auxiliary block alignment was accepted");
    Require(sink.WriteAuxiliaryBlock({ .name = "hollow" }, {}) == bake::BakedAssetSinkStatus::EmptyBlock,
        "An empty auxiliary block was accepted");
    Require(sink.WriteAuxiliaryBlock({ .name = "extra" }, payload) == bake::BakedAssetSinkStatus::Success,
        "Protocol test WriteAuxiliaryBlock failed");
    Require(sink.WriteAuxiliaryBlock({ .name = "EXTRA" }, payload) == bake::BakedAssetSinkStatus::DuplicateBlock,
        "Two blocks differing only in case were accepted into one artifact");

    // The failed commit above kept the asset open, so the caller can still
    // repair it rather than losing the blocks it already handed over.
    Require(sink.CommitAsset() == bake::BakedAssetSinkStatus::Success, "Protocol test CommitAsset failed");
    Require(ReadFileText(sink.BlockPath(descriptor, "extra")) == "payload",
        "A repaired asset lost the blocks written before the failed commit");
    Require(!std::filesystem::exists(sink.BlockPath(descriptor, "hollow")) &&
            !std::filesystem::exists(sink.BlockPath(descriptor, "misaligned")),
        "A rejected auxiliary block still reached the published artifact");
    Require(CountEntries(sink.AssetDirectory(descriptor)) == 2U,
        "The published artifact holds something other than its primary and its one accepted auxiliary block");

    std::filesystem::remove_all(root);
}

} // namespace

void RunAssetBakeTests() {
    ProfilesAnswerQuestionsAboutTheirTarget();
    WebProfileMatchesWhatABrowserBuildCanRun();
    ProfileValidationCoversEveryRejectionRule();
    ReservedWin32DeviceNamesAreAllRejected();
    KeyIsDeterministicAndMachineIndependent();
    KeyCarriesWhatTheProfileSaysNotOnlyItsName();
    KeyInvalidationIsScopedAndPropagates();
    KeyIsUsableAsAFileName();
    CommittedAssetIsReadable();
    PublicationMovesTheWholeStagingDirectoryAtOnce();
    CommitRefusesToTreatDebrisAsAPublishedArtifact();
    OrphanedStagingIsSweptByTheNextBake();
    LongStorePathsArePublishedAndReadable();
    OverlongStoreRootIsRefusedBeforeAnythingIsWritten();
    AbortedAssetLeavesNothingBehind();
    SinkRejectsProtocolViolations();
    std::filesystem::remove_all(TestRoot());
}

} // namespace kb::tests
