#include "RendererTestSupport.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/IAssetLoader.hpp"
#include "engine/assets/bake/AssetPackWriter.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/RuntimeAssetManifest.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "engine/assets/streaming/BackgroundLoadService.hpp"
#include "kb/render/DisplayConfig.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RendererMemory.hpp"
#include "kb/render/bake/MeshBaker.hpp"
#include "kb/render/bake/TextureBaker.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "kb/render/runtime/RuntimeContentStreamer.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderer.hpp"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef small
#undef far
#endif

namespace kb::render::tests {
namespace {

namespace asset_bake = kb::assets::bake;
using Clock = std::chrono::steady_clock;

class HeadlessSurface final : public RenderSurface {
public:
    explicit HeadlessSurface(bool hardware = false) {
#if defined(_WIN32)
        if (hardware) {
            window_ = CreateWindowExW(0U, L"STATIC", L"Content Streaming", WS_OVERLAPPEDWINDOW,
                0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            Require(window_ != nullptr, "Could not create the hidden streaming test window");
        }
#else
        Require(!hardware, "Hardware streaming readback requires a native surface");
#endif
    }
    ~HeadlessSurface() override {
#if defined(_WIN32)
        if (window_ != nullptr) DestroyWindow(static_cast<HWND>(window_));
#endif
    }
    [[nodiscard]] std::uint32_t Width() const noexcept override {
        return 64U;
    }
    [[nodiscard]] std::uint32_t Height() const noexcept override {
        return 64U;
    }
    [[nodiscard]] void* NativeWindowHandle() const noexcept override {
        return window_;
    }
    [[nodiscard]] void* NativeDisplayHandle() const noexcept override {
        return nullptr;
    }
private:
    void* window_ = nullptr;
};

[[nodiscard]] std::filesystem::path TestRoot() {
    return std::filesystem::temp_directory_path() /
        ("21kb_renderer_content_streaming_" + std::to_string(Clock::now().time_since_epoch().count()));
}

// An uncompressed 32-bit TGA of a smooth two-axis gradient with some detail on top.
[[nodiscard]] std::vector<std::uint8_t> MakeTga(std::uint16_t width, std::uint16_t height) {
    std::vector<std::uint8_t> bytes(18U, 0U);
    bytes[2] = 2U;
    bytes[12] = static_cast<std::uint8_t>(width & 0xFFU);
    bytes[13] = static_cast<std::uint8_t>(width >> 8U);
    bytes[14] = static_cast<std::uint8_t>(height & 0xFFU);
    bytes[15] = static_cast<std::uint8_t>(height >> 8U);
    bytes[16] = 32U;
    bytes[17] = 0x28U;
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            const std::uint8_t r = static_cast<std::uint8_t>((x * 255U) / (width - 1U));
            const std::uint8_t g = static_cast<std::uint8_t>((y * 255U) / (height - 1U));
            const std::uint8_t b = static_cast<std::uint8_t>(((x / 8U + y / 8U) % 2U) * 200U);
            bytes.insert(bytes.end(), { b, g, r, 255U });
        }
    }
    return bytes;
}

// A closed sphere with enough triangles for a real level-of-detail chain.
[[nodiscard]] RenderMeshAssetData MakeSphere(std::uint32_t segments, std::uint32_t rings) {
    RenderMeshAssetData mesh{};
    for (std::uint32_t ring = 0U; ring <= rings; ++ring) {
        const float phi = static_cast<float>(std::numbers::pi) * static_cast<float>(ring) / static_cast<float>(rings);
        for (std::uint32_t segment = 0U; segment <= segments; ++segment) {
            const float theta =
                2.0F * static_cast<float>(std::numbers::pi) * static_cast<float>(segment) / static_cast<float>(segments);
            RenderStaticMeshVertexP3N3T4UV2 vertex{};
            vertex.nx = std::sin(phi) * std::cos(theta);
            vertex.ny = std::cos(phi);
            vertex.nz = std::sin(phi) * std::sin(theta);
            vertex.x = vertex.nx;
            vertex.y = vertex.ny;
            vertex.z = vertex.nz;
            vertex.u = static_cast<float>(segment) / static_cast<float>(segments);
            vertex.v = static_cast<float>(ring) / static_cast<float>(rings);
            mesh.tangentVertices.push_back(vertex);
        }
    }
    const std::uint32_t stride = segments + 1U;
    for (std::uint32_t ring = 0U; ring < rings; ++ring) {
        for (std::uint32_t segment = 0U; segment < segments; ++segment) {
            const std::uint32_t a = ring * stride + segment;
            const std::uint32_t b = a + stride;
            mesh.indices32.insert(mesh.indices32.end(), { a, b, a + 1U, a + 1U, b, b + 1U });
        }
    }
    mesh.sections.push_back(RenderMeshSectionDesc{
        .indexStart = 0U,
        .indexCount = static_cast<std::uint32_t>(mesh.indices32.size()),
        .materialSlot = 0U,
    });
    mesh.materialSlots.push_back(RenderMaterialSlotDesc{ .defaultMaterialAssetId = 0U });
    return mesh;
}

// Records what a baker hands over, so a test can look at the blocks without a container.
class CaptureSink final : public asset_bake::IBakedAssetSink {
public:
    asset_bake::BakedAssetSinkStatus BeginAsset(const asset_bake::BakedAssetDescriptor&) override {
        return asset_bake::BakedAssetSinkStatus::Success;
    }
    asset_bake::BakedAssetSinkStatus WritePrimaryBlock(std::span<const std::uint8_t> bytes, std::uint32_t) override {
        primary.assign(bytes.begin(), bytes.end());
        return asset_bake::BakedAssetSinkStatus::Success;
    }
    asset_bake::BakedAssetSinkStatus WriteAuxiliaryBlock(
        const asset_bake::BakedAssetBlock& block,
        std::span<const std::uint8_t> bytes) override {
        names.emplace_back(block.name);
        residencies.push_back(block.residency);
        auxiliary.emplace_back(bytes.begin(), bytes.end());
        return asset_bake::BakedAssetSinkStatus::Success;
    }
    asset_bake::BakedAssetSinkStatus CommitAsset() override {
        return asset_bake::BakedAssetSinkStatus::Success;
    }
    void AbortAsset() noexcept override {}

    std::vector<std::uint8_t> primary;
    std::vector<std::string> names;
    std::vector<asset_bake::BakedAssetBlockResidency> residencies;
    std::vector<std::vector<std::uint8_t>> auxiliary;
};

// Red when: a texture larger than the tail edge does not keep only its tail in the primary block
// and every larger mip in a streaming block of its own; when the tail and the streamed levels do
// not compose back into exactly the chain that was baked; or when a level of the wrong size or a
// damaged streaming header is accepted.
void StreamedTextureBakeKeepsTheTailResident() {
    const asset_bake::BakeTargetProfile profile = asset_bake::WindowsX64BakeTargetProfile();
    const std::vector<std::uint8_t> source = MakeTga(512U, 512U);
    CaptureSink sink;
    const bake::TextureBakeOutput baked = bake::BakeTextureBytes(source,
        bake::TextureBakeSettings{ RenderTextureAssetSemantic::BaseColor, RenderTextureAssetColorSpace::Srgb },
        profile, asset_bake::TextureCompressionFamily::BlockCompressedBaseline, sink);
    Require(baked.status == bake::TextureBakeStatus::Success && baked.mipCount == 10U, "A 512x512 texture did not bake");
    Require(sink.names == std::vector<std::string>{ "mip0", "mip1" } &&
            std::ranges::all_of(sink.residencies, [](asset_bake::BakedAssetBlockResidency residency) {
                return residency == asset_bake::BakedAssetBlockResidency::Streaming;
            }),
        "The levels above the tail edge are not each in a streaming block");
    Require(sink.primary.size() > bake::kStreamedTextureMagic.size() &&
            std::memcmp(sink.primary.data(), bake::kStreamedTextureMagic.data(), bake::kStreamedTextureMagic.size()) == 0,
        "The primary block of a streamed texture does not open with its streaming header");

    RenderTextureAssetData tail{};
    Require(bake::ReadBakedTexture(sink.primary, tail) && tail.gpuBlocks.has_value() && tail.streaming.has_value(),
        "The tail of a streamed texture does not read back");
    Require(tail.width == 128U && tail.height == 128U && tail.mipCount == 8U && tail.streaming->width == 512U &&
            tail.streaming->mipCount == 10U && tail.streaming->streamedMipCount == 2U && tail.streaming->firstLevel == 2U,
        "The tail does not describe the levels below the streamed ones");
    Require(tail.streaming->streamedLevelBytes ==
                std::vector<std::uint32_t>{ static_cast<std::uint32_t>(sink.auxiliary[0].size()),
                    static_cast<std::uint32_t>(sink.auxiliary[1].size()) },
        "The layout does not know the size of every streamed level");

    RenderTextureAssetData half{};
    const std::array<std::span<const std::uint8_t>, 1U> mip1{ std::span<const std::uint8_t>{ sink.auxiliary[1] } };
    Require(bake::ComposeBakedTextureLevels(tail, mip1, half) && half.width == 256U && half.mipCount == 9U &&
            half.streaming->firstLevel == 1U,
        "One streamed level did not compose onto the tail");
    RenderTextureAssetData full{};
    const std::array<std::span<const std::uint8_t>, 2U> both{
        std::span<const std::uint8_t>{ sink.auxiliary[0] }, std::span<const std::uint8_t>{ sink.auxiliary[1] } };
    Require(bake::ComposeBakedTextureLevels(tail, both, full) && full.width == 512U && full.mipCount == 10U,
        "Every streamed level did not compose onto the tail");
    std::vector<std::uint8_t> expected = sink.auxiliary[0];
    expected.insert(expected.end(), sink.auxiliary[1].begin(), sink.auxiliary[1].end());
    expected.insert(expected.end(), tail.gpuBlocks->blocks.begin(), tail.gpuBlocks->blocks.end());
    bgfx::TextureInfo chain{};
    bgfx::calcTextureSize(chain, 512U, 512U, 1U, false, true, 1U, full.gpuBlocks->format);
    Require(full.gpuBlocks->blocks == expected && full.gpuBlocks->blocks.size() == chain.storageSize,
        "The composed chain is not the baked chain level by level");

    const std::array<std::span<const std::uint8_t>, 1U> wrongLevel{ std::span<const std::uint8_t>{ sink.auxiliary[0] } };
    RenderTextureAssetData refused{};
    Require(!bake::ComposeBakedTextureLevels(tail, wrongLevel, refused), "A level of the wrong size was composed");
    std::vector<std::uint8_t> damaged = sink.primary;
    damaged[17] = 3U;
    Require(!bake::ReadBakedTexture(damaged, refused), "A streaming header that disagrees with its tail was read");

    // A texture within the tail edge has nothing to stream and keeps its plain container.
    CaptureSink small;
    const bake::TextureBakeOutput smallBake = bake::BakeTextureBytes(MakeTga(128U, 64U),
        bake::TextureBakeSettings{ RenderTextureAssetSemantic::BaseColor, RenderTextureAssetColorSpace::Srgb },
        profile, asset_bake::TextureCompressionFamily::BlockCompressedBaseline, small);
    RenderTextureAssetData smallTexture{};
    Require(smallBake.status == bake::TextureBakeStatus::Success && small.auxiliary.empty() &&
            bake::ReadBakedTexture(small.primary, smallTexture) && !smallTexture.streaming.has_value() &&
            smallTexture.width == 128U,
        "A texture within the tail edge was split into streaming blocks");
}

// Red when: the coarser levels of detail of a baked mesh cannot be read without the finer ones,
// or what they read is not exactly those levels of the full mesh, rebased to the front.
void BakedMeshLevelsReadOnTheirOwn() {
    const asset_bake::BakeTargetProfile profile = asset_bake::WindowsX64BakeTargetProfile();
    CaptureSink sink;
    const bake::MeshBakeOutput baked = bake::BakeMesh(MakeSphere(96U, 64U), profile, sink);
    Require(baked.status == bake::MeshBakeStatus::Success && baked.lodCount > 1U, "The sphere did not bake with levels");
    bake::BakedMeshLayout layout{};
    Require(bake::ReadBakedMeshLayout(baked.primaryBlock, layout) && layout.lods.size() == baked.lodCount &&
            layout.chunkCount == baked.chunks.size(),
        "The level layout does not read from the primary block");
    RenderMeshAssetData full{};
    Require(bake::ReadBakedMesh(baked.primaryBlock, baked.chunks, full), "The full mesh does not read");
    std::uint32_t expectedChunk = 0U;
    for (std::uint32_t level = 0U; level < layout.lods.size(); ++level) {
        Require(layout.lods[level].firstChunk == expectedChunk && layout.lods[level].chunkCount > 0U &&
                layout.lods[level].geometryBytes > 0U && (level == 0U) == (layout.lods[level].error == 0.0F),
            "The levels do not partition the chunks");
        expectedChunk += layout.lods[level].chunkCount;

        const std::vector<std::vector<std::uint8_t>> chunks(
            baked.chunks.begin() + layout.lods[level].firstChunk, baked.chunks.end());
        RenderMeshAssetData partial{};
        Require(bake::ReadBakedMeshLods(baked.primaryBlock, level, chunks, partial),
            "A suffix of the levels of detail does not read on its own");
        Require(partial.lods.size() == layout.lods.size() - level, "The partial mesh has the wrong number of levels");
        const RenderMeshSectionDesc& firstSection = full.sections[full.lods[level].firstSection];
        const std::size_t vertexBase = firstSection.vertexStart;
        const std::size_t indexBase = firstSection.indexStart;
        Require(partial.tangentVertices.size() == full.tangentVertices.size() - vertexBase &&
                std::memcmp(partial.tangentVertices.data(), full.tangentVertices.data() + vertexBase,
                    partial.tangentVertices.size() * sizeof(RenderStaticMeshVertexP3N3T4UV2)) == 0,
            "The partial mesh's vertices are not the full mesh's vertices of those levels");
        const bool narrow = !full.indices16.empty();
        Require(narrow ? std::equal(partial.indices16.begin(), partial.indices16.end(), full.indices16.begin() + indexBase) &&
                    partial.indices16.size() == full.indices16.size() - indexBase
                       : std::equal(partial.indices32.begin(), partial.indices32.end(), full.indices32.begin() + indexBase) &&
                    partial.indices32.size() == full.indices32.size() - indexBase,
            "The partial mesh's indices are not the full mesh's indices of those levels");
        Require(partial.sections.size() == full.sections.size() - full.lods[level].firstSection &&
                partial.sections.front().vertexStart == 0U && partial.sections.front().indexStart == 0U &&
                partial.sections.front().lodLevel == 0U && partial.lods.front().firstSection == 0U,
            "The partial mesh is not rebased onto its first level");
        Require(partial.bounds.radius >= full.bounds.radius * 0.999F, "The partial mesh lost the full mesh's bounds");
    }
    const std::uint32_t last = static_cast<std::uint32_t>(layout.lods.size() - 1U);
    std::vector<std::vector<std::uint8_t>> tooFew(baked.chunks.begin() + layout.lods[last].firstChunk + 1U, baked.chunks.end());
    RenderMeshAssetData refused{};
    Require(!bake::ReadBakedMeshLods(baked.primaryBlock, last, tooFew, refused) &&
            !bake::ReadBakedMeshLods(baked.primaryBlock, last + 1U, {}, refused),
        "A level read without its chunks, or a level that does not exist, was accepted");
}

struct StreamingPack {
    std::shared_ptr<asset_bake::RuntimeAssetPack> pack;
    asset_bake::RuntimeAssetManifestEntry texture;
    asset_bake::RuntimeAssetManifestEntry mesh;
};

[[nodiscard]] asset_bake::AssetBakeKey SourceKey(const asset_bake::BakeTargetProfile& profile, std::span<const std::uint8_t> bytes,
    std::string_view salt) {
    return asset_bake::AssetBakeKey{
        .sourceContentHash = asset_bake::HashBakeBytes(bytes),
        .bakerId = "RuntimeSource",
        .bakerVersion = "1",
        .targetProfileId = std::string{ profile.identifier },
        .targetProfileHash = asset_bake::BakeTargetProfileFingerprint(profile),
        .settingsHash = asset_bake::HashBakeText(salt),
    };
}

// A compressed runtime pack with a scene, a 512x512 texture in every family the profile ships
// and a sphere with a level-of-detail chain. The ETC2 profile ships one texture family with a
// fast encoder, which keeps the fixture cheap; nothing here is specific to it.
[[nodiscard]] StreamingPack BuildStreamingPack(const std::filesystem::path& path, bool hardware = false) {
    asset_bake::BakeTargetProfile profile = hardware ? asset_bake::WebGlWasm32BakeTargetProfile()
                                                   : asset_bake::AndroidEtc2Arm64BakeTargetProfile();
    asset_bake::AssetPackWriterOptions options{};
    options.compression = asset_bake::AssetPackBlockCompression::Zstd;
    asset_bake::AssetPackWriter writer{ path, profile, options };
    const std::string scenePath = "/Game/Scenes/Main.21kbscene";
    const std::string texturePath = "/Game/Textures/Ground.tga";
    const std::string meshPath = "/Game/Meshes/Sphere.obj";

    const std::vector<std::uint8_t> textureSource = MakeTga(512U, 512U);
    asset_bake::RuntimeAssetManifestEntry texture{
        .id = kb::assets::MakeAssetId(texturePath + ":RenderTexture"),
        .type = "RenderTexture",
        .name = "Ground",
        .virtualPath = texturePath,
        .sourceExtension = ".tga",
        .contentHash = asset_bake::HashBakeBytes(textureSource),
    };
    for (std::uint32_t familyIndex = 0U; familyIndex < asset_bake::kTextureCompressionFamilyCount; ++familyIndex) {
        const auto family = static_cast<asset_bake::TextureCompressionFamily>(familyIndex);
        if (!asset_bake::HasTextureCompressionFamily(profile.textureCompressions, family)) {
            continue;
        }
        const bake::TextureBakeOutput baked = bake::BakeTextureBytes(textureSource,
            bake::TextureBakeSettings{ RenderTextureAssetSemantic::BaseColor, RenderTextureAssetColorSpace::Linear },
            profile, family, writer);
        Require(baked.status == bake::TextureBakeStatus::Success, "A streaming texture variant did not bake");
        texture.artifacts.push_back(asset_bake::RuntimeArtifactReference{
            .digest = baked.key.Digest(),
            .encoding = asset_bake::RuntimeArtifactEncoding::BakedTexture,
            .qualifier = std::string{ asset_bake::TextureCompressionFamilyName(family) },
        });
    }
    const RenderMeshAssetData sphere = MakeSphere(160U, 120U);
    const bake::MeshBakeOutput bakedMesh = bake::BakeMesh(sphere, profile, writer);
    Require(bakedMesh.status == bake::MeshBakeStatus::Success && bakedMesh.lodCount > 1U, "The streaming sphere did not bake");
    asset_bake::RuntimeAssetManifestEntry mesh{
        .id = kb::assets::MakeAssetId(meshPath + ":RenderMesh"),
        .type = "RenderMesh",
        .name = "Sphere",
        .virtualPath = meshPath,
        .sourceExtension = ".obj",
        .contentHash = 0x5EF1U,
        .artifacts = { asset_bake::RuntimeArtifactReference{
            .digest = bakedMesh.key.Digest(), .encoding = asset_bake::RuntimeArtifactEncoding::BakedMesh } },
    };

    const std::array<std::uint8_t, 5U> sceneSource{ 's', 'c', 'e', 'n', 'e' };
    std::vector<std::uint8_t> sceneBlob;
    Require(asset_bake::EncodeRuntimeSourceBlob(sceneSource, sceneBlob), "The streaming scene source did not encode");
    const asset_bake::AssetBakeKey sceneKey = SourceKey(profile, sceneSource, scenePath);
    Require(writer.BeginAsset({ .key = sceneKey, .assetTypeId = std::string{ asset_bake::kSourceAssetTypeId } }) ==
                asset_bake::BakedAssetSinkStatus::Success &&
            writer.WritePrimaryBlock(sceneBlob, profile.packageBlockAlignmentBytes) == asset_bake::BakedAssetSinkStatus::Success &&
            writer.CommitAsset() == asset_bake::BakedAssetSinkStatus::Success,
        "The streaming scene could not be stored");

    asset_bake::RuntimeAssetManifest manifest{
        .targetProfileId = std::string{ profile.identifier },
        .targetProfileHash = asset_bake::BakeTargetProfileFingerprint(profile),
    };
    manifest.descriptor.targetPlatforms = { "Windows" };
    manifest.settings.name = "ContentStreaming";
    manifest.settings.defaultMap = scenePath;
    manifest.assets = {
        asset_bake::RuntimeAssetManifestEntry{
            .id = kb::assets::MakeAssetId(scenePath + ":Scene"),
            .type = "Scene",
            .name = "Main",
            .virtualPath = scenePath,
            .sourceExtension = ".21kbscene",
            .contentHash = asset_bake::HashBakeBytes(sceneSource),
            .artifacts = { asset_bake::RuntimeArtifactReference{
                .digest = sceneKey.Digest(), .encoding = asset_bake::RuntimeArtifactEncoding::SourceBytes } },
        },
        texture,
        mesh,
    };
    std::vector<std::uint8_t> manifestBytes;
    Require(asset_bake::EncodeRuntimeAssetManifest(manifest, manifestBytes) == asset_bake::RuntimeAssetManifestStatus::Success,
        "The streaming manifest did not encode");
    asset_bake::AssetBakeKey manifestKey = SourceKey(profile, manifestBytes, "manifest");
    manifestKey.bakerId = "RuntimeManifest";
    Require(writer.BeginAsset({ .key = manifestKey, .assetTypeId = std::string{ asset_bake::kRuntimeManifestAssetTypeId } }) ==
                asset_bake::BakedAssetSinkStatus::Success &&
            writer.WritePrimaryBlock(manifestBytes, profile.packageBlockAlignmentBytes) ==
                asset_bake::BakedAssetSinkStatus::Success &&
            writer.CommitAsset() == asset_bake::BakedAssetSinkStatus::Success &&
            writer.Finish() == asset_bake::BakedAssetSinkStatus::Success,
        "The streaming pack could not be published");

    StreamingPack result{};
    result.pack = std::make_shared<asset_bake::RuntimeAssetPack>();
    Require(result.pack->Mount(path, profile) == asset_bake::RuntimeAssetPackStatus::Success, "The streaming pack did not mount");
    result.texture = texture;
    result.mesh = mesh;
    return result;
}

[[nodiscard]] kb::assets::AssetMetadata MetadataOf(const asset_bake::RuntimeAssetManifestEntry& entry) {
    kb::assets::AssetMetadata metadata{};
    metadata.id = entry.id;
    metadata.type = entry.type;
    metadata.name = entry.name;
    metadata.virtualPath = entry.virtualPath;
    metadata.sourceExtension = entry.sourceExtension;
    metadata.contentHash = entry.contentHash;
    return metadata;
}

// A camera at `distance` in front of the origin, looking at it, with a 60 degree vertical field.
[[nodiscard]] SceneRenderCamera CameraAt(float distance) {
    SceneRenderCamera camera{};
    camera.view = { 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, -distance, 1.0F };
    const float focal = 1.0F / std::tan(static_cast<float>(std::numbers::pi) / 6.0F);
    camera.projection = { focal, 0.0F, 0.0F, 0.0F, 0.0F, focal, 0.0F, 0.0F, 0.0F, 0.0F, -1.0F, -1.0F, 0.0F, 0.0F, -0.1F, 0.0F };
    return camera;
}

// Red when: a packaged texture and mesh do not load with only their tail and coarsest level; when
// a close camera does not stream every finer level in -- read, verified and decompressed off the
// render thread -- and swap the GPU resources and their bindings; when the budget is ever
// exceeded; or when a far camera and a smaller budget do not evict back to the resident floor.
void PackagedTexturesAndMeshesStreamUnderTheBudget() {
    const std::filesystem::path root = TestRoot();
    std::error_code error;
    std::filesystem::create_directories(root, error);
    StreamingPack fixture = BuildStreamingPack(root / "Game.kbpack");

    // bgfx comes up headless; the runtime cache's resources live in a SceneRenderer of their own.
    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "The headless renderer did not initialize");
    renderer.ConfigureContentStreaming(RuntimeContentStreamingSettings{ .budgetBytes = 16ULL * 1024ULL * 1024ULL });
    Require(renderer.ContentStreamingStats().residency.budgetBytes == 16ULL * 1024ULL * 1024ULL,
        "The renderer does not forward its streaming budget");

    {
        SceneRenderer sceneRenderer;
        const kb::assets::AssetMetadata textureMetadata = MetadataOf(fixture.texture);
        const kb::assets::AssetMetadata meshMetadata = MetadataOf(fixture.mesh);
        RenderTextureAssetLoader textureLoader{ bgfx::RendererType::Noop };
        const kb::assets::AssetLoadResult textureLoad = textureLoader.Load(kb::assets::AssetLoadRequest{
            .metadata = textureMetadata, .resolvedPath = {}, .runtimePack = fixture.pack });
        RenderMeshAssetLoader meshLoader;
        const kb::assets::AssetLoadResult meshLoad = meshLoader.Load(kb::assets::AssetLoadRequest{
            .metadata = meshMetadata, .resolvedPath = {}, .runtimePack = fixture.pack });
        Require(textureLoad.Succeeded() && meshLoad.Succeeded(), "The packaged texture or mesh did not load");
        const auto tail = std::static_pointer_cast<const RenderTextureAssetData>(textureLoad.asset);
        const auto coarse = std::static_pointer_cast<const RenderMeshAssetData>(meshLoad.asset);
        Require(tail->streaming.has_value() && tail->width == 128U && tail->streaming->streamedMipCount == 2U,
            "The packaged texture did not load with only its tail");
        Require(coarse->streaming.has_value() && coarse->lods.size() == 1U &&
                coarse->streaming->firstLod + 1U == coarse->streaming->lodGeometryBytes.size(),
            "The packaged mesh did not load with only its coarsest level");
        const std::uint32_t meshLevels = static_cast<std::uint32_t>(coarse->streaming->lodGeometryBytes.size());

        const RenderTextureHandle textureHandle = sceneRenderer.Resources().RegisterTexture(tail->MakeDesc(
            sceneRenderer.Resources().CopyUploadMemory(tail->gpuBlocks->blocks.data(), static_cast<std::uint32_t>(tail->gpuBlocks->blocks.size())),
            RenderTextureColorSpace::Linear));
        const RenderMeshHandle meshHandle = sceneRenderer.Resources().RegisterMesh(coarse->desc);
        Require(textureHandle.IsValid() && meshHandle.IsValid(), "The streamed resources could not be created");
        constexpr std::uint64_t kSceneId = 9U;
        constexpr std::uint64_t kMaterialId = 0xA11CEU;
        const RuntimeTextureAssetKey textureKey{ .sceneId = kSceneId, .assetId = fixture.texture.id.value,
            .colorSpace = RenderTextureColorSpace::Linear };
        const RuntimeAssetKey meshKey{ .sceneId = kSceneId, .assetId = fixture.mesh.id.value };
        RuntimeStreamedTextureMap textures{ { textureKey, RuntimeTextureResource{ .handle = textureHandle } } };
        RuntimeStreamedMeshMap meshes{ { meshKey, RuntimeMeshResource{ .handle = meshHandle } } };
        sceneRenderer.ResourceMap().BindTexture(textureKey.assetId, textureKey.colorSpace, textureHandle);
        sceneRenderer.ResourceMap().BindMesh(meshKey.assetId, meshHandle);

        const std::shared_ptr<kb::assets::streaming::BackgroundLoadService> background =
            kb::assets::streaming::BackgroundLoadService::Shared();
        const std::uint64_t bytesBefore = background->Stats().bytesRead;
        RuntimeContentStreamer streamer;
        // A sphere this smooth looks right at a coarse level even up close; a strict error bound
        // makes the close camera ask for every level.
        streamer.Configure(RuntimeContentStreamingSettings{
            .budgetBytes = 64ULL * 1024ULL * 1024ULL, .meshMaxScreenErrorPixels = 1.0e-6F });
        streamer.TrackTexture(textureKey, textureHandle, tail, fixture.pack);
        streamer.TrackMesh(meshKey, meshHandle, coarse, fixture.pack, coarse->materialSlots);
        Require(streamer.IsTracking(textureKey) && streamer.IsTracking(meshKey), "The streamer did not take the resources");

        RenderScene renderScene;
        MeshRenderProxyDesc proxy{};
        proxy.entityId = 1U;
        proxy.meshAssetId = meshKey.assetId;
        proxy.materialAssetId = kMaterialId;
        proxy.model = { 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
        static_cast<void>(renderScene.UpsertMesh(proxy));

        std::uint64_t frame = 1U;
        const auto describe = [&] {
            const RuntimeContentStreamingStats stats = streamer.Stats();
            return " (texture level " + std::to_string(streamer.UploadedLevel(textureKey)) + ", mesh level " +
                std::to_string(streamer.UploadedLevel(meshKey)) + ", loads started " +
                std::to_string(stats.residency.loadsStarted) + " completed " + std::to_string(stats.residency.loadsCompleted) +
                " failed " + std::to_string(stats.failedLoads) + ", rebuilds " + std::to_string(stats.rebuilds) +
                ", resident " + std::to_string(stats.residency.residentBytes) + " of " +
                std::to_string(stats.residency.budgetBytes) + ")";
        };
        const auto run = [&](const SceneRenderCamera& camera, const auto& done) {
            const Clock::time_point deadline = Clock::now() + std::chrono::seconds{ 60 };
            while (!done() && Clock::now() < deadline) {
                // The texture ensurer reports this every frame for the material it binds.
                streamer.NoteTextureUse(textureKey, kMaterialId);
                streamer.Update(RuntimeContentStreamingFrame{ .sceneId = kSceneId, .renderScene = &renderScene,
                                    .camera = &camera, .viewportHeight = 1080U, .frame = frame++ },
                    sceneRenderer, textures, meshes);
                const RuntimeContentStreamingStats stats = streamer.Stats();
                Require(stats.residency.residentBytes + stats.residency.inFlightBytes <= stats.residency.budgetBytes,
                    "Streaming exceeded its budget");
                sceneRenderer.TickFrame();
                std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
            }
            return done();
        };

        // Close: everything streams in.
        const SceneRenderCamera closeCamera = CameraAt(2.0F);
        const Clock::time_point started = Clock::now();
        const bool streamedIn =
            run(closeCamera, [&] { return streamer.UploadedLevel(textureKey) == 0 && streamer.UploadedLevel(meshKey) == 0; });
        Require(streamedIn, ("A close camera did not stream every level in" + describe()).c_str());
        Require(background->Stats().bytesRead > bytesBefore,
            "The streamer did not read through the engine's background load service");
        const double milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        const RenderTextureResource* sharp = sceneRenderer.Resources().FindTexture(textures.at(textureKey).handle);
        const RenderMeshResource* detailed = sceneRenderer.Resources().FindMesh(meshes.at(meshKey).handle);
        Require(sharp != nullptr && sharp->width == 512U && sharp->mipCount == 10U,
            "The rebuilt texture does not carry the full chain");
        Require(detailed != nullptr && detailed->lods.size() == meshLevels, "The rebuilt mesh does not carry every level");
        Require(sceneRenderer.ResourceMap().ResolveMesh(meshKey.assetId) == meshes.at(meshKey).handle &&
                textures.at(textureKey).handle != textureHandle && meshes.at(meshKey).handle != meshHandle,
            "The rebuilt resources were not swapped into the cache and the bindings");
        const RuntimeContentStreamingStats loaded = streamer.Stats();
        std::cout << "content-streaming: 512x512 texture (2 streamed mips) and " << meshLevels
                  << "-level mesh fully streamed in " << milliseconds << " ms over " << frame - 1U
                  << " frames; slowest level load " << loaded.maxLoadMilliseconds << " ms; "
                  << loaded.residency.residentBytes << " bytes resident\n";

        // Far, with a budget that holds only the floors: back down to them.
        const SceneRenderCamera distantCamera = CameraAt(4000.0F);
        streamer.Configure(RuntimeContentStreamingSettings{ .budgetBytes = loaded.residency.floorBytes });
        const bool evicted = run(distantCamera, [&] {
            return streamer.UploadedLevel(textureKey) == 2 &&
                streamer.UploadedLevel(meshKey) == static_cast<std::int32_t>(meshLevels - 1U);
        });
        Require(evicted, ("A far camera and a small budget did not evict back to the resident floor" + describe()).c_str());
        Require(streamer.Stats().budgetContractionOverrunBytes > 0U,
            "Shrinking below the existing working set must report its temporary budget overrun");
        const RenderTextureResource* evictedTexture = sceneRenderer.Resources().FindTexture(textures.at(textureKey).handle);
        Require(evictedTexture != nullptr && evictedTexture->width == 128U && streamer.Stats().residency.evictions > 0U,
            "The evicted texture was not rebuilt from its tail");
        sceneRenderer.Resources().DestroyTexture(textures.at(textureKey).handle);
        sceneRenderer.Resources().DestroyMesh(meshes.at(meshKey).handle);
        textures.clear();
        meshes.clear();
        // Resources the cache dropped are forgotten on the next update.
        streamer.Update(RuntimeContentStreamingFrame{ .sceneId = kSceneId, .frame = frame++ }, sceneRenderer, textures, meshes);
        Require(!streamer.IsTracking(textureKey) && !streamer.IsTracking(meshKey) &&
                streamer.Stats().residency.residentBytes == 0U,
            "Resources the cache dropped were still streamed");
        for (int tick = 0; tick < 8; ++tick) {
            sceneRenderer.TickFrame();
        }
    }
    renderer.Shutdown();
    fixture.pack->Unmount();
    std::filesystem::remove_all(root, error);
}

// The pieces of a streaming run that the tests below share: a packaged texture and mesh with only their tails
// resident, a streamer that tracks them, and a loop that updates it frame by frame.
struct StreamingRig {
    const Renderer* renderer = nullptr;
    SceneRenderer sceneRenderer;
    RenderTextureHandle textureHandle{};
    RenderMeshHandle meshHandle{};
    RuntimeTextureAssetKey textureKey{};
    RuntimeAssetKey meshKey{};
    RuntimeStreamedTextureMap textures;
    RuntimeStreamedMeshMap meshes;
    RuntimeContentStreamer streamer;
    RenderScene renderScene;
    std::uint32_t meshLevels = 0U;
    std::uint64_t frame = 1U;
    std::vector<double> updateMilliseconds;
    static constexpr std::uint64_t kSceneId = 9U;
    static constexpr std::uint64_t kMaterialId = 0xA11CEU;

    // Runs `streamer.Update` until `done`, calling `perFrame` after each update; true when `done` came true in time.
    template <typename Done, typename PerFrame>
    bool Run(const SceneRenderCamera& camera, const Done& done, const PerFrame& perFrame) {
        const Clock::time_point deadline = Clock::now() + std::chrono::seconds{ 60 };
        while (!done() && Clock::now() < deadline) {
            const auto updateStarted = Clock::now();
            streamer.NoteTextureUse(textureKey, kMaterialId);
            streamer.Update(RuntimeContentStreamingFrame{ .sceneId = kSceneId, .renderScene = &renderScene,
                                .camera = &camera, .viewportHeight = 1080U, .frame = frame++ },
                sceneRenderer, textures, meshes);
            // A second view in the same frame must share the upload limits.
            streamer.Update(RuntimeContentStreamingFrame{ .sceneId = kSceneId, .renderScene = &renderScene,
                                .camera = &camera, .viewportHeight = 1080U, .frame = frame - 1U },
                sceneRenderer, textures, meshes);
            const RuntimeContentStreamingStats stats = streamer.Stats();
            updateMilliseconds.push_back(std::chrono::duration<double, std::milli>(Clock::now() - updateStarted).count());
            Require(stats.residency.residentBytes + stats.residency.inFlightBytes <= stats.residency.budgetBytes, "Streaming exceeded its budget");
            perFrame(stats);
            sceneRenderer.TickFrame();
            bgfx::frame();
            std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
        }
        return done();
    }
};

template <typename Body>
void WithStreamingRig(const RuntimeContentStreamingSettings& settings, int damagedPack, const Body& body, bool hardware = false) {
    const std::filesystem::path root = TestRoot();
    std::error_code error;
    std::filesystem::create_directories(root, error);
    StreamingPack fixture = BuildStreamingPack(root / "Game.kbpack", hardware);
    HeadlessSurface surface{ hardware };
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(hardware ? bgfx::RendererType::Direct3D11 : bgfx::RendererType::Noop);
    Renderer renderer;
    Require(!kb::render::ProcessGpuMemoryBytes(renderer), "GPU reading must be unavailable before initialization");
    Require(renderer.Initialize(surface, &config), "The headless renderer did not initialize");
    if (!hardware) Require(!kb::render::ProcessGpuMemoryBytes(renderer), "Noop must not report estimated GPU bytes as measured");
    {
        StreamingRig rig;
        rig.renderer = &renderer;
        const kb::assets::AssetMetadata textureMetadata = MetadataOf(fixture.texture);
        const kb::assets::AssetMetadata meshMetadata = MetadataOf(fixture.mesh);
        RenderTextureAssetLoader textureLoader{ hardware ? bgfx::RendererType::Direct3D11 : bgfx::RendererType::Noop };
        const kb::assets::AssetLoadResult textureLoad = textureLoader.Load(kb::assets::AssetLoadRequest{
            .metadata = textureMetadata, .resolvedPath = {}, .runtimePack = fixture.pack });
        RenderMeshAssetLoader meshLoader;
        const kb::assets::AssetLoadResult meshLoad = meshLoader.Load(kb::assets::AssetLoadRequest{
            .metadata = meshMetadata, .resolvedPath = {}, .runtimePack = fixture.pack });
        Require(textureLoad.Succeeded() && meshLoad.Succeeded(), "The packaged texture or mesh did not load");
        const auto tail = std::static_pointer_cast<const RenderTextureAssetData>(textureLoad.asset);
        const auto coarse = std::static_pointer_cast<const RenderMeshAssetData>(meshLoad.asset);
        rig.meshLevels = static_cast<std::uint32_t>(coarse->streaming->lodGeometryBytes.size());
        rig.textureHandle = rig.sceneRenderer.Resources().RegisterTexture(tail->MakeDesc(
            rig.sceneRenderer.Resources().CopyUploadMemory(tail->gpuBlocks->blocks.data(), static_cast<std::uint32_t>(tail->gpuBlocks->blocks.size())),
            RenderTextureColorSpace::Linear));
        rig.meshHandle = rig.sceneRenderer.Resources().RegisterMesh(coarse->desc);
        Require(rig.textureHandle.IsValid() && rig.meshHandle.IsValid(), "The streamed resources could not be created");
        rig.textureKey = RuntimeTextureAssetKey{ .sceneId = StreamingRig::kSceneId, .assetId = fixture.texture.id.value,
            .colorSpace = RenderTextureColorSpace::Linear };
        rig.meshKey = RuntimeAssetKey{ .sceneId = StreamingRig::kSceneId, .assetId = fixture.mesh.id.value };
        rig.textures = RuntimeStreamedTextureMap{ { rig.textureKey, RuntimeTextureResource{ .handle = rig.textureHandle } } };
        rig.meshes = RuntimeStreamedMeshMap{ { rig.meshKey, RuntimeMeshResource{ .handle = rig.meshHandle } } };
        rig.sceneRenderer.ResourceMap().BindTexture(rig.textureKey.assetId, rig.textureKey.colorSpace, rig.textureHandle);
        rig.sceneRenderer.ResourceMap().BindMesh(rig.meshKey.assetId, rig.meshHandle);
        rig.streamer.Configure(settings);
        rig.streamer.TrackTexture(rig.textureKey, rig.textureHandle, tail, fixture.pack);
        rig.streamer.TrackMesh(rig.meshKey, rig.meshHandle, coarse, fixture.pack, coarse->materialSlots);
        MeshRenderProxyDesc proxy{};
        proxy.entityId = 1U;
        proxy.meshAssetId = rig.meshKey.assetId;
        proxy.materialAssetId = StreamingRig::kMaterialId;
        proxy.model = { 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
        static_cast<void>(rig.renderScene.UpsertMesh(proxy));
        if (damagedPack == 1) fixture.pack->Unmount();
        if (damagedPack == 2) {
            std::filesystem::resize_file(root / "Game.kbpack", 0U, error);
            Require(!error, "The packaged data could not be truncated for the damage test");
        }
        body(rig);
        for (int tick = 0; tick < 8; ++tick) rig.sceneRenderer.TickFrame();
    }
    renderer.Shutdown();
    if (damagedPack != 1) fixture.pack->Unmount();
    Require(!kb::render::ProcessGpuMemoryBytes(renderer), "GPU reading must be unavailable after shutdown");
    std::filesystem::remove_all(root, error);
}


#if defined(_WIN32)
class StreamingReadback {
public:
    StreamingReadback() {
        color_ = bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | BGFX_TEXTURE_BLIT_DST);
        depth_ = bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT);
        readback_ = bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
        Require(bgfx::isValid(color_) && bgfx::isValid(depth_) && bgfx::isValid(readback_), "Streaming readback textures failed");
        const std::array attachments{ color_, depth_ };
        target_ = bgfx::createFrameBuffer(2U, attachments.data(), false);
        Require(bgfx::isValid(target_), "Streaming readback target failed");
    }
    ~StreamingReadback() {
        bgfx::destroy(target_);
        bgfx::destroy(readback_);
        bgfx::destroy(depth_);
        bgfx::destroy(color_);
    }
    std::vector<std::uint8_t> Draw(StreamingRig& rig) {
        RenderMaterialDesc material{};
        material.albedoTexture = rig.textures.at(rig.textureKey).handle;
        material.doubleSided = true;
        const auto handle = rig.sceneRenderer.Resources().RegisterMaterial(material);
        rig.sceneRenderer.ResourceMap().BindMaterial(StreamingRig::kMaterialId, handle);
        auto camera = CameraAt(4.0F);
        camera.projection[10] = 0.0F;
        camera.projection[14] = 0.1F;
        bgfx::setViewFrameBuffer(0U, target_);
        bgfx::setViewRect(0U, 0U, 0U, 64U, 64U);
        bgfx::setViewClear(0U, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0xFF00FFFFU, 0.0F);
        bgfx::touch(0U);
        rig.sceneRenderer.Submit(0U, rig.renderScene, 64U, 64U, &camera);
        const auto submitted = rig.sceneRenderer.LastSubmitStats();
        Require(submitted.submittedDrawCallCount > 0U && !rig.sceneRenderer.LastDiagnostics().HasErrors(),
            "The streamed mesh must submit a draw without resource errors");
        bgfx::blit(250U, bgfx::TextureRegion{ .handle = readback_ },
            bgfx::TextureRegion{ .handle = color_, .width = 64U, .height = 64U });
        std::vector<std::uint8_t> pixels(64U * 64U * 4U);
        const auto ready = bgfx::read(bgfx::TextureRegion{ .handle = readback_ }, pixels.data());
        auto frame = bgfx::frame();
        for (unsigned guard = 0; frame < ready && guard < 8; ++guard) frame = bgfx::frame();
        Require(frame >= ready, "Streaming image readback timed out");
        rig.sceneRenderer.ResourceMap().UnbindMaterialHandle(handle);
        rig.sceneRenderer.Resources().DestroyMaterial(handle);
        // Every pixel inside the projected sphere's center must remain covered across replacements.
        for (unsigned y = 28; y < 36; ++y) for (unsigned x = 28; x < 36; ++x) {
            const auto i = (y * 64U + x) * 4U;
            Require(pixels[i] != 255U || pixels[i + 1] != 0U || pixels[i + 2] != 255U,
                "Streaming left a hole in the visible mesh");
        }
        return pixels;
    }
private:
    bgfx::TextureHandle color_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depth_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle readback_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle target_ = BGFX_INVALID_HANDLE;
};

void StreamingImagesSurviveZoomAndEviction() {
    WithStreamingRig(RuntimeContentStreamingSettings{ .budgetBytes = 64ULL * 1024ULL * 1024ULL,
                         .maxUploadBytesPerFrame = 1U, .meshMaxScreenErrorPixels = 1.0e-6F },
        false, [](StreamingRig& rig) {
            Require(rig.sceneRenderer.Initialize(), "The streaming scene renderer did not initialize");
            StreamingReadback target;
            const auto floorBytes = rig.streamer.Stats().residency.residentBytes;
            auto previous = target.Draw(rig);
            double maximumImageDelta = 0.0;
            const auto sample = [&](const RuntimeContentStreamingStats&) {
                auto current = target.Draw(rig);
                double delta = 0.0;
                for (std::size_t i = 0; i < current.size(); ++i)
                    delta += std::abs(static_cast<int>(current[i]) - static_cast<int>(previous[i]));
                maximumImageDelta = std::max(maximumImageDelta, delta / current.size());
                previous = std::move(current);
            };
            const auto close = CameraAt(2.0F);
            const auto far = CameraAt(4000.0F);
            for (unsigned cycle = 0; cycle < 4; ++cycle) {
                rig.streamer.Configure(RuntimeContentStreamingSettings{ .budgetBytes = 64ULL * 1024ULL * 1024ULL,
                    .maxUploadBytesPerFrame = 1U, .meshMaxScreenErrorPixels = 1.0e-6F });
                Require(rig.Run(close, [&] {
                    return rig.streamer.UploadedLevel(rig.textureKey) == 0 && rig.streamer.UploadedLevel(rig.meshKey) == 0;
                }, sample), "Hardware resources did not stream to full detail");
                rig.streamer.Configure(RuntimeContentStreamingSettings{ .budgetBytes = floorBytes,
                    .maxUploadBytesPerFrame = 1U, .meshMaxScreenErrorPixels = 1.0e-6F });
                Require(rig.Run(far, [&] {
                    return rig.streamer.UploadedLevel(rig.textureKey) == 2 &&
                        rig.streamer.UploadedLevel(rig.meshKey) == static_cast<std::int32_t>(rig.meshLevels - 1U);
                }, sample), "Hardware resources did not return to their resident floor");
            }
            for (unsigned frame = 0; frame < 32; ++frame) {
                auto movingCamera = CameraAt(frame % 2 == 0 ? 2.0F : 4000.0F);
                movingCamera.view[12] = (static_cast<int>(frame % 5) - 2) * 0.1F;
                rig.streamer.Configure(RuntimeContentStreamingSettings{
                    .budgetBytes = 64ULL * 1024ULL * 1024ULL,
                    .maxUploadBytesPerFrame = 1U, .meshMaxScreenErrorPixels = 1.0e-6F });
                const auto nextFrame = rig.frame + 1U;
                Require(rig.Run(movingCamera, [&] { return rig.frame >= nextFrame; }, sample),
                    "A rapid camera transition failed to advance");
            }
            const auto stats = rig.streamer.Stats();
            Require(stats.maxTextureRebuildMilliseconds > 0.0 && stats.maxMeshRebuildMilliseconds > 0.0,
                "Hardware resource creation must have timing evidence");
            std::ranges::sort(rig.updateMilliseconds);
            const double p99 = rig.updateMilliseconds[static_cast<std::size_t>(
                std::ceil(rig.updateMilliseconds.size() * 0.99)) - 1U];
            std::cout << "hardware streaming: cycles=4 rapid_zoom_frames=32 image_mean_delta_max=" << maximumImageDelta
                << " update_cpu_p99_ms=" << p99
                << " texture_rebuild_max_ms=" << stats.maxTextureRebuildMilliseconds
                << " mesh_rebuild_max_ms=" << stats.maxMeshRebuildMilliseconds
                << " mesh_buffer_creation_max_ms=" << stats.maxMeshBufferCreationMilliseconds
                << " peak_upload_bytes=" << stats.peakUploadBytesPerFrame
                << " peak_transient_estimate_bytes=" << stats.peakRebuildTransientBytes << '\n';
            rig.sceneRenderer.Resources().DestroyTexture(rig.textures.at(rig.textureKey).handle);
            rig.sceneRenderer.Resources().DestroyMesh(rig.meshes.at(rig.meshKey).handle);
            rig.textures.clear();
            rig.meshes.clear();
            rig.streamer.Update(RuntimeContentStreamingFrame{ .frame = rig.frame++ },
                rig.sceneRenderer, rig.textures, rig.meshes);
            Require(rig.streamer.Stats().streamedMeshes == 0U && rig.streamer.Stats().streamedTextures == 0U,
                "Unloaded GPU resources must not retain streaming records");
        }, true);
}
#endif

// Red when: a burst of arrivals is not spread over frames by the rebuild limits; a resource larger than the upload
// limit is starved instead of admitted alone; or a rebuilt resource leaves its old handle alive.
void UploadLimitsSpreadRebuildsWithoutStarving() {
    // Once with a rebuild count limit alone and once with an upload byte limit alone.
    for (const bool byteLimit : { false, true }) {
    WithStreamingRig(RuntimeContentStreamingSettings{ .budgetBytes = 64ULL * 1024ULL * 1024ULL,
                         .maxRebuildsPerFrame = byteLimit ? 8U : 1U,
                         .maxUploadBytesPerFrame = byteLimit ? 1U : 64ULL * 1024ULL * 1024ULL, .meshMaxScreenErrorPixels = 1.0e-6F },
        false, [byteLimit](StreamingRig& rig) {
            const SceneRenderCamera closeCamera = CameraAt(2.0F);
            std::uint64_t previousRebuilds = 0U;
            std::uint64_t largestStep = 0U;
            std::uint64_t previousUploaded = 0U;
            std::uint64_t largestUpload = 0U;
            const bool streamedIn = rig.Run(closeCamera,
                [&] { return rig.streamer.UploadedLevel(rig.textureKey) == 0 && rig.streamer.UploadedLevel(rig.meshKey) == 0; },
                [&](const RuntimeContentStreamingStats& stats) {
                    largestStep = std::max(largestStep, stats.rebuilds - previousRebuilds);
                    largestUpload = std::max(largestUpload, stats.uploadedBytes - previousUploaded);
                    previousRebuilds = stats.rebuilds;
                    previousUploaded = stats.uploadedBytes;
                });
            Require(streamedIn, "A one-rebuild-per-frame limit starved a resource: it never reached its finest level");
            Require(largestStep == 1U, "No update may rebuild more than the per-frame limit of one resource");
            Require(!byteLimit || largestUpload > 1U, "A resource larger than the upload limit must still be admitted alone");
            const auto stats = rig.streamer.Stats();
            Require(!byteLimit || stats.oversizedUploads == stats.rebuilds, "Every oversized upload must be counted");
            Require(stats.peakUploadBytesPerFrame == largestUpload, "The upload peak must include all views of a frame");
            Require(stats.maxTextureRebuildMilliseconds > 0.0 && stats.maxMeshRebuildMilliseconds > 0.0,
                "Texture and geometry upload submission times must both be measured");
            std::cout << "streaming uploads: peak_bytes=" << stats.peakUploadBytesPerFrame
                      << " oversized=" << stats.oversizedUploads
                      << " texture_max_ms=" << stats.maxTextureRebuildMilliseconds
                      << " mesh_max_ms=" << stats.maxMeshRebuildMilliseconds << '\n';
            // The old handles died with the rebuild: nothing can draw with them.
            Require(rig.sceneRenderer.Resources().FindTexture(rig.textureHandle) == nullptr &&
                    rig.sceneRenderer.Resources().FindMesh(rig.meshHandle) == nullptr,
                "A rebuilt texture or mesh must not leave its old handle alive");
            Require(rig.sceneRenderer.Resources().FindTexture(rig.textures.at(rig.textureKey).handle) != nullptr &&
                    rig.sceneRenderer.Resources().FindMesh(rig.meshes.at(rig.meshKey).handle) != nullptr,
                "The cache must hold the handles of the rebuilt resources");
            rig.sceneRenderer.Resources().DestroyTexture(rig.textures.at(rig.textureKey).handle);
            rig.sceneRenderer.Resources().DestroyMesh(rig.meshes.at(rig.meshKey).handle);
        });
    }
}

// Red when: the memory a rebuild holds beyond the resident levels (the old GPU resource and the staging copy of the new
// one) is not measured; the transient limit does not spread the rebuilds of one frame; or the measured peak with the
// transient bytes falls below the resident bytes.
void UploadBudgetChecksTheNextResource() {
    WithStreamingRig(RuntimeContentStreamingSettings{ .budgetBytes = 64ULL * 1024ULL * 1024ULL,
                         .maxRebuildsPerFrame = 0U, .meshMaxScreenErrorPixels = 1.0e-6F },
        false, [](StreamingRig& rig) {
            const SceneRenderCamera camera = CameraAt(2.0F);
            std::uint32_t settled = 0U;
            Require(rig.Run(camera, [&] { return settled >= 10U; },
                        [&](const RuntimeContentStreamingStats& stats) {
                            settled = stats.loadsInFlight == 0U ? settled + 1U : 0U;
                        }), "The packaged levels did not settle before the upload budget test");
            const std::uint64_t limit = rig.streamer.Stats().residency.residentBytes - 1U;
            rig.streamer.Configure(RuntimeContentStreamingSettings{ .budgetBytes = 64ULL * 1024ULL * 1024ULL,
                .maxRebuildsPerFrame = 8U, .maxUploadBytesPerFrame = limit, .meshMaxScreenErrorPixels = 1.0e-6F });
            std::uint64_t previous = 0U;
            Require(rig.Run(camera,
                        [&] { return rig.streamer.UploadedLevel(rig.textureKey) == 0 && rig.streamer.UploadedLevel(rig.meshKey) == 0; },
                        [&](const RuntimeContentStreamingStats& stats) {
                            Require(stats.uploadedBytes - previous <= limit,
                                "A second resource exceeded the remaining upload budget");
                            previous = stats.uploadedBytes;
                        }), "Deferring a resource that does not fit must not starve it");
            Require(previous > limit, "The fixture must require more than one frame's upload allowance");
            rig.sceneRenderer.Resources().DestroyTexture(rig.textures.at(rig.textureKey).handle);
            rig.sceneRenderer.Resources().DestroyMesh(rig.meshes.at(rig.meshKey).handle);
        });
}

#if defined(_WIN32)
[[nodiscard]] std::uint64_t ProcessGpuBytes(const Renderer& renderer) {
    const auto bytes = kb::render::ProcessGpuMemoryBytes(renderer);
    Require(bytes.has_value(), "Per-process GPU memory measurement failed");
    return *bytes;
}

void HardwareRebuildMemoryPeak() {
    constexpr std::uint64_t budget = 64ULL * 1024ULL * 1024ULL;
    WithStreamingRig(RuntimeContentStreamingSettings{.budgetBytes = budget, .meshMaxScreenErrorPixels = 1.0e-6F},
        false, [](StreamingRig& rig) {
            for (unsigned frame = 0U; frame < 8U; ++frame) bgfx::frame();
            const auto baseline = ProcessGpuBytes(*rig.renderer);
            auto peak = baseline;
            std::uint64_t pendingPeak = 0U, retiredPeak = 0U;
            const auto sample = [&](const RuntimeContentStreamingStats& stats) {
                peak = std::max(peak, ProcessGpuBytes(*rig.renderer));
                pendingPeak = std::max(pendingPeak, rig.sceneRenderer.Resources().PendingUploadBytes());
                retiredPeak = std::max(retiredPeak, stats.pendingGpuReleaseBytes);
                Require(stats.peakAdmittedRebuildBytes <= budget,
                    "Hardware rebuild admission exceeded its payload working-set budget");
            };
            for (unsigned cycle = 0U; cycle < 4U; ++cycle) {
                rig.streamer.Configure({.budgetBytes = budget, .meshMaxScreenErrorPixels = 1.0e-6F});
                Require(rig.Run(CameraAt(2.0F), [&] {
                    return rig.streamer.UploadedLevel(rig.textureKey) == 0 && rig.streamer.UploadedLevel(rig.meshKey) == 0;
                }, sample), "Hardware streaming did not reach full detail");
                rig.streamer.Configure({.budgetBytes = rig.streamer.Stats().residency.floorBytes, .meshMaxScreenErrorPixels = 1.0F});
                Require(rig.Run(CameraAt(4000.0F), [&] {
                    return rig.streamer.UploadedLevel(rig.textureKey) == 2 &&
                        rig.streamer.UploadedLevel(rig.meshKey) == static_cast<std::int32_t>(rig.meshLevels - 1U);
                }, sample), "Hardware streaming did not release fine detail");
            }
            Require(rig.streamer.Stats().budgetContractionOverrunBytes > 0U,
                "Budget-pressure eviction did not report the charged old/new overlap");
            Require(pendingPeak > 0U && retiredPeak > 0U, "Hardware workload did not overlap upload and old-resource lifetimes");
            Require(peak > baseline && peak - baseline <= budget, "Measured GPU memory growth exceeded the rebuild budget");
            std::cout << "rebuild_memory cycles=4 budget_bytes=" << budget << " admitted_peak_bytes="
                << rig.streamer.Stats().peakAdmittedRebuildBytes << " queued_upload_peak_bytes=" << pendingPeak
                << " retired_payload_peak_bytes=" << retiredPeak
                << " contraction_overrun_bytes=" << rig.streamer.Stats().budgetContractionOverrunBytes
                << " dxgi_baseline_bytes=" << baseline
                << " dxgi_peak_bytes=" << peak << " dxgi_growth_bytes=" << peak - baseline << '\n';
            rig.sceneRenderer.Resources().DestroyTexture(rig.textures.at(rig.textureKey).handle);
            rig.sceneRenderer.Resources().DestroyMesh(rig.meshes.at(rig.meshKey).handle);
        }, true);
}
#endif

void RebuildAdmissionDefersAndRetries() {
    WithStreamingRig(RuntimeContentStreamingSettings{.budgetBytes = 64ULL * 1024ULL * 1024ULL,
        .maxRebuildsPerFrame = 0U, .meshMaxScreenErrorPixels = 1.0e-6F}, false, [](StreamingRig& rig) {
        const auto camera = CameraAt(2.0F);
        unsigned settled = 0U;
        Require(rig.Run(camera, [&] { return settled >= 10U; }, [&](const RuntimeContentStreamingStats& stats) {
            settled = stats.loadsInFlight == 0U ? settled + 1U : 0U;
        }), "Admission fixture loads did not settle");
        const auto limit = rig.streamer.Stats().residency.residentBytes;
        rig.streamer.Configure({.budgetBytes = limit, .meshMaxScreenErrorPixels = 1.0e-6F});
        unsigned frames = 0U;
        Require(rig.Run(camera, [&] { return frames == 12U; }, [&](const RuntimeContentStreamingStats& stats) {
            ++frames;
            Require(stats.peakAdmittedRebuildBytes <= limit && stats.budgetContractionOverrunBytes == 0U,
                "Admission omitted old resources, upload copies or composition from the working set");
        }), "Admission fixture did not advance");
        Require(rig.streamer.Stats().rebuildBudgetDeferrals > 0U,
            "A resident-only budget incorrectly admitted every replacement and staging copy");
        rig.streamer.Configure({.budgetBytes = 64ULL * 1024ULL * 1024ULL, .meshMaxScreenErrorPixels = 1.0e-6F});
        Require(rig.Run(camera, [&] {
            return rig.streamer.UploadedLevel(rig.textureKey) == 0 && rig.streamer.UploadedLevel(rig.meshKey) == 0;
        }, [](const RuntimeContentStreamingStats&) {}), "Budget deferral permanently failed a valid resource");
        Require(rig.streamer.Stats().peakAdmittedRebuildBytes <= 64ULL * 1024ULL * 1024ULL,
            "The resumed rebuild exceeded its working-set allowance");
        rig.sceneRenderer.Resources().DestroyTexture(rig.textures.at(rig.textureKey).handle);
        rig.sceneRenderer.Resources().DestroyMesh(rig.meshes.at(rig.meshKey).handle);
    });
}

void UploadStagingFollowsBackendConsumption() {
    WithStreamingRig(RuntimeContentStreamingSettings{}, false, [](StreamingRig&) {
        RenderUploadMemory uploads;
        const std::array<std::uint8_t, 64U * 64U * 4U> pixels{};
        const auto* memory = uploads.Copy(pixels.data(), static_cast<std::uint32_t>(pixels.size()));
        Require(uploads.PendingBytes() == pixels.size() && uploads.PeakBytes() == pixels.size(),
            "Upload bytes were not charged before backend submission");
        const auto texture = bgfx::createTexture2D(64U, 64U, false, 1U, bgfx::TextureFormat::RGBA8, 0U, memory);
        Require(bgfx::isValid(texture), "Upload lifetime fixture could not create a texture");
        for (unsigned frame = 0U; frame < 8U; ++frame) bgfx::frame();
        Require(uploads.PendingBytes() == 0U && uploads.PeakBytes() == pixels.size(),
            "Backend consumption did not release the upload charge");
        bgfx::destroy(texture);
    });
}

void RebuildTransientMemoryIsMeasuredAndBounded() {
    std::uint64_t limitedPeak = 0U;
    std::uint64_t unlimitedPeak = 0U;
    for (const unsigned mode : { 0U, 1U, 2U }) {
        const bool limited = mode != 2U;
        // Rebuilds wait until both level loads have landed, so both resources are due in the same frame.
        WithStreamingRig(RuntimeContentStreamingSettings{ .budgetBytes = 64ULL * 1024ULL * 1024ULL, .maxRebuildsPerFrame = 0U,
                             .meshMaxScreenErrorPixels = 1.0e-6F },
            false, [&](StreamingRig& rig) {
                const SceneRenderCamera closeCamera = CameraAt(2.0F);
                const std::uint64_t initialResident = rig.streamer.Stats().residency.residentBytes;
                const bool landed = rig.Run(closeCamera,
                    [&] {
                        const RuntimeContentStreamingStats stats = rig.streamer.Stats();
                        return stats.loadsInFlight == 0U && stats.residency.residentBytes > initialResident;
                    },
                    [](const RuntimeContentStreamingStats&) {});
                Require(landed, "The level loads did not land");
                Require(rig.streamer.Stats().peakRebuildTransientBytes == 0U, "Nothing rebuilt yet, so no transient bytes");
                const auto combinedCharge = initialResident + rig.streamer.Stats().residency.residentBytes;
                const auto limit = mode == 0U ? 1U : mode == 1U ? combinedCharge - 1U : 0U;
                rig.streamer.Configure(RuntimeContentStreamingSettings{ .budgetBytes = 64ULL * 1024ULL * 1024ULL,
                    .maxRebuildsPerFrame = 8U, .maxTransientBytesPerFrame = limit, .meshMaxScreenErrorPixels = 1.0e-6F });
                std::uint64_t previousRebuilds = 0U;
                std::uint64_t largestStep = 0U;
                const bool streamedIn = rig.Run(closeCamera,
                    [&] { return rig.streamer.UploadedLevel(rig.textureKey) == 0 && rig.streamer.UploadedLevel(rig.meshKey) == 0; },
                    [&](const RuntimeContentStreamingStats& stats) {
                        if (stats.rebuilds > previousRebuilds) Require(stats.pendingGpuReleaseBytes > 0U,
                            "Replaced GPU resources stopped being charged before deferred release");
                        largestStep = std::max(largestStep, stats.rebuilds - previousRebuilds);
                        previousRebuilds = stats.rebuilds;
                    });
                Require(streamedIn, "The resources did not reach their finest level");
                const RuntimeContentStreamingStats stats = rig.streamer.Stats();
                Require(stats.peakRebuildTransientBytes >= stats.uploadedBytes / std::max<std::uint64_t>(stats.rebuilds, 1U),
                    "The transient bytes of a rebuild must cover at least the new resource it uploads");
                Require(stats.peakCommittedWithTransientBytes > stats.peakRebuildTransientBytes,
                    "The committed peak must include the resident levels next to the transient bytes");
                if (limited) {
                    Require(largestStep == 1U, "The next rebuild must wait when its charge exceeds the remaining transient allowance");
                    if (mode == 1U) Require(stats.peakRebuildTransientBytes <= limit,
                        "Rebuild admission exceeded the transient budget despite each resource fitting alone");
                    limitedPeak = std::max(limitedPeak, stats.peakRebuildTransientBytes);
                } else {
                    Require(largestStep >= 2U, "Without a transient limit both due resources rebuild in the same frame");
                    unlimitedPeak = stats.peakRebuildTransientBytes;
                }
                rig.sceneRenderer.Resources().DestroyTexture(rig.textures.at(rig.textureKey).handle);
                rig.sceneRenderer.Resources().DestroyMesh(rig.meshes.at(rig.meshKey).handle);
                rig.streamer.ReleaseAll();
                const auto retiring = rig.streamer.Stats().pendingGpuReleaseBytes;
                Require(retiring > 0U, "Untracking resources lost their pending release charge");
                rig.textures.clear(); rig.meshes.clear();
                const auto update = [&] {
                    rig.streamer.Update({.sceneId = StreamingRig::kSceneId, .frame = rig.frame++},
                        rig.sceneRenderer, rig.textures, rig.meshes);
                };
                update(); update();
                Require(rig.streamer.Stats().pendingGpuReleaseBytes == retiring,
                    "Application frame numbers released GPU charges before the resource registry");
                for (unsigned tick = 0U; tick < 8U; ++tick) rig.sceneRenderer.TickFrame();
                update();
                Require(rig.streamer.Stats().pendingGpuReleaseBytes == 0U,
                    "An empty streamer retained charges after the registry released its resources");
            });
    }
    Require(unlimitedPeak > limitedPeak, "Rebuilding both at once must hold more transient memory than one after the other");
}

// Red when: the streamer drops, rebuilds or crashes a resource whose level data cannot be read; the failure is not
// counted; or the coarse level that is still resident stops being drawable.
void MissingLevelDataKeepsTheTailDrawable() {
    for (const int damage : { 1, 2 }) {
    WithStreamingRig(RuntimeContentStreamingSettings{ .budgetBytes = 64ULL * 1024ULL * 1024ULL, .meshMaxScreenErrorPixels = 1.0e-6F },
        damage, [](StreamingRig& rig) {
#if defined(_WIN32)
            Require(rig.sceneRenderer.Initialize(), "The damaged-pack renderer did not initialize");
            StreamingReadback target;
            const auto before = target.Draw(rig);
#endif
            const SceneRenderCamera closeCamera = CameraAt(2.0F);
            const bool failed = rig.Run(closeCamera, [&] { return rig.streamer.Stats().failedLoads > 0U; }, [](const RuntimeContentStreamingStats&) {});
            Require(failed, "Unreadable level data must be counted as failed loads");
            for (int frame = 0; frame < 30; ++frame) {
                rig.streamer.Update(RuntimeContentStreamingFrame{ .sceneId = StreamingRig::kSceneId, .renderScene = &rig.renderScene,
                                        .camera = &closeCamera, .viewportHeight = 1080U, .frame = rig.frame++ },
                    rig.sceneRenderer, rig.textures, rig.meshes);
                rig.sceneRenderer.TickFrame();
            }
            Require(rig.streamer.UploadedLevel(rig.textureKey) == 2 &&
                    rig.streamer.UploadedLevel(rig.meshKey) == static_cast<std::int32_t>(rig.meshLevels - 1U),
                "A resource whose finer levels cannot be read must stay at its resident tail");
            Require(rig.textures.at(rig.textureKey).handle == rig.textureHandle && rig.meshes.at(rig.meshKey).handle == rig.meshHandle &&
                    rig.sceneRenderer.Resources().FindTexture(rig.textureHandle) != nullptr &&
                    rig.sceneRenderer.Resources().FindMesh(rig.meshHandle) != nullptr,
                "A resource whose finer levels cannot be read must keep its drawable handles");
            Require(rig.streamer.Stats().rebuilds == 0U, "Nothing may be rebuilt from data that could not be read");
#if defined(_WIN32)
            Require(target.Draw(rig) == before, "Missing or damaged texture/mesh data changed the resident image");
#endif
            rig.sceneRenderer.Resources().DestroyTexture(rig.textureHandle);
            rig.sceneRenderer.Resources().DestroyMesh(rig.meshHandle);
        }
#if defined(_WIN32)
        , true
#endif
        );
    }
}

} // namespace

void RunRuntimeContentStreamingTests() {
#if defined(_WIN32)
    StreamingImagesSurviveZoomAndEviction();
#endif
    StreamedTextureBakeKeepsTheTailResident();
    BakedMeshLevelsReadOnTheirOwn();
    PackagedTexturesAndMeshesStreamUnderTheBudget();
    UploadLimitsSpreadRebuildsWithoutStarving();
    UploadBudgetChecksTheNextResource();
    MissingLevelDataKeepsTheTailDrawable();
    RebuildTransientMemoryIsMeasuredAndBounded();
    UploadStagingFollowsBackendConsumption();
    RebuildAdmissionDefersAndRetries();
#if defined(_WIN32)
    HardwareRebuildMemoryPeak();
#endif
}

} // namespace kb::render::tests
