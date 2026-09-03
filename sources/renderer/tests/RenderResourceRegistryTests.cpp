#include "RendererTestSupport.hpp"

#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/ImportedAssetLoader.hpp"
#include "engine/assets/bake/AssetPackWriter.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/RuntimeAssetManifest.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "engine/save/SaveGameService.hpp"
#include "kb/render/RuntimeAssetShaderProvider.hpp"
#include "kb/render/resources/RenderAssetRefs.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialCookPayload.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialFunctionAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialTypeAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/resources/RenderSkinningPaletteAllocator.hpp"
#include "kb/render/resources/SkeletalMeshRenderResourceBuilder.hpp"
#include "engine/scene/SkeletalMeshAsset.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "resources/RenderTextureResourceBuilder.hpp"
#include "resources/RenderMeshResourceBuilder.hpp"
#include "runtime/RuntimeTextureMipChain.hpp"
#include "runtime/RuntimeMaterialGraphDependencyLoader.hpp"
#include "kb/render/runtime/RuntimeMaterialResolver.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "kb/render/scene/SceneRenderer.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace kb::render::tests {
namespace {

[[nodiscard]] bool ContainsDependency(const std::vector<kb::assets::AssetId>& dependencies, kb::assets::AssetId id) noexcept {
    for (const kb::assets::AssetId dependency : dependencies) {
        if (dependency == id) {
            return true;
        }
    }
    return false;
}

void RunRenderTextureColorSpaceDescTest() {
    const bgfx::Memory memory{};
    RenderTextureAssetData texture{};
    texture.width = 2U;
    texture.height = 2U;

    const RenderTextureDesc linear = texture.MakeDesc(&memory, RenderTextureColorSpace::Linear);
    const RenderTextureDesc srgb = texture.MakeDesc(&memory, RenderTextureColorSpace::Srgb);

    Require(linear.colorSpace == RenderTextureColorSpace::Linear, "Linear texture desc did not preserve color-space metadata");
    Require((linear.flags & BGFX_TEXTURE_SRGB) == 0U, "Linear texture desc should not request bgfx sRGB sampling");
    Require(srgb.colorSpace == RenderTextureColorSpace::Srgb, "sRGB texture desc did not preserve color-space metadata");
    Require((srgb.flags & BGFX_TEXTURE_SRGB) != 0U, "Base Color sRGB texture desc should request bgfx sRGB sampling");
}

void RunRuntimeTextureMipChainGenerationTest() {
    constexpr std::array<std::uint8_t, 32U> checkerboard{
        0U, 0U, 0U, 255U, 255U, 255U, 255U, 255U, 0U, 0U, 0U, 255U, 255U, 255U, 255U, 255U,
        255U, 255U, 255U, 255U, 0U, 0U, 0U, 255U, 255U, 255U, 255U, 255U, 0U, 0U, 0U, 255U,
    };
    const std::optional<RuntimeTextureMipChain> linear =
        BuildRuntimeTexture2DMipChain(checkerboard, 4U, 2U, RenderTextureColorSpace::Linear);
    const std::optional<RuntimeTextureMipChain> srgb =
        BuildRuntimeTexture2DMipChain(checkerboard, 4U, 2U, RenderTextureColorSpace::Srgb);

    Require(linear.has_value() && srgb.has_value(), "Runtime texture mip-chain generation rejected valid RGBA8 data");
    Require(linear->mipCount == 3U && linear->rgba8.size() == 44U,
        "Runtime texture mip-chain generation did not produce the complete 4x2, 2x1, 1x1 chain");
    Require(linear->rgba8[32U] == 128U && linear->rgba8[40U] == 128U,
        "Linear texture mip levels were not box-filtered in linear space");
    Require(srgb->rgba8[32U] == 188U && srgb->rgba8[40U] == 188U,
        "sRGB texture mip levels were not box-filtered in linear-light space");

    constexpr std::array<std::uint8_t, 12U> oddStrip{
        10U, 20U, 30U, 255U,
        40U, 50U, 60U, 255U,
        70U, 80U, 90U, 255U,
    };
    const std::optional<RuntimeTextureMipChain> strip =
        BuildRuntimeTexture2DMipChain(oddStrip, 1U, 3U, RenderTextureColorSpace::Linear);
    Require(strip.has_value() && strip->mipCount == 2U && strip->rgba8.size() == 16U,
        "Runtime texture mip-chain generation did not support an odd one-dimensional texture");
    Require(strip->rgba8[12U] == 40U && strip->rgba8[13U] == 50U && strip->rgba8[14U] == 60U,
        "Odd texture downsampling dropped source texels instead of covering the full level");
    Require(!BuildRuntimeTexture2DMipChain(std::span<const std::uint8_t>{ oddStrip }.first(8U), 1U, 3U, RenderTextureColorSpace::Linear).has_value(),
        "Runtime texture mip-chain generation accepted a truncated base level");
}

void RunRenderTextureTextAssetPreservesDimensionTest() {
    struct TextureCase {
        const char* text;
        RenderTextureDimension dimension;
        std::uint16_t depth;
        std::uint16_t layers;
        std::size_t byteSize;
    };
    const std::array cases{
        TextureCase{ "size 2 2\nrgba8 1 2 3 255\n", RenderTextureDimension::Texture2D, 1U, 1U, 16U },
        TextureCase{ "dimension cube\nsize 2 2\nrgba8 4 5 6 255\n", RenderTextureDimension::TextureCube, 1U, 1U, 96U },
        TextureCase{ "dimension 3d\nsize 2 2\ndepth 3\nrgba8 7 8 9 255\n", RenderTextureDimension::Texture3D, 3U, 1U, 48U },
        TextureCase{ "dimension 2dArray\nsize 2 2\nlayers 3\nrgba8 10 11 12 255\n", RenderTextureDimension::Texture2DArray, 1U, 3U, 48U },
    };

    for (const TextureCase& textureCase : cases) {
        std::istringstream input{ textureCase.text };
        const std::optional<RenderTextureAssetData> loaded = RenderTextureAssetLoader::LoadTexture(input);
        Require(loaded.has_value(), "Render texture text asset dimension fixture did not load");
        Require(loaded->dimension == textureCase.dimension &&
                loaded->depth == textureCase.depth &&
                loaded->layers == textureCase.layers &&
                loaded->mipCount == 1U,
            "Render texture text asset did not preserve dimension metadata");
        Require(loaded->rgba8.size() == textureCase.byteSize,
            "Render texture text asset did not allocate every face/slice/layer");
        const RenderTextureDesc desc = loaded->MakeDesc(nullptr);
        Require(desc.dimension == textureCase.dimension && desc.depth == textureCase.depth &&
                desc.layers == textureCase.layers && desc.mipCount == 1U,
            "Render texture descriptor lost text asset dimension metadata");
    }

    // A file that is not a decodable image must fail the load and nothing more. bimg's parse
    // takes an optional bx::Error and, given none, substitutes a temporary whose scope object
    // asserts on destruction that no error was set -- and "Unrecognized image format." is an
    // error. So refusing a corrupt texture breaks into the debugger in a debug build instead
    // of returning empty, and a project cannot be allowed to bring the editor down by
    // containing one unreadable .png.
    {
        const std::filesystem::path corruptRoot =
            std::filesystem::temp_directory_path() / "kb_corrupt_texture_tests";
        std::filesystem::remove_all(corruptRoot);
        std::filesystem::create_directories(corruptRoot);
        const std::array<std::pair<const char*, std::string>, 4> corrupt{ {
            // Recognised by no parser at all: takes the "Unrecognized image format." path.
            { "prose.png", std::string{ "not an image at all, just prose" } },
            // A real PNG signature with nothing behind it: recognised, then fails to decode.
            { "truncated.png", std::string{ "\x89PNG\r\n\x1a\n" } + "IHDR but not really" },
            // Likewise for a container the DDS parser claims by magic and cannot read.
            { "truncated.dds", std::string{ "DDS " } + std::string( 16U, char{ 0 } ) },
            { "zeroes.png", std::string( 64U, char{ 0 } ) },
        } };
        for (const auto& [name, bytes] : corrupt) {
            const std::filesystem::path file = corruptRoot / name;
            {
                std::ofstream output{ file, std::ios::binary };
                output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            }
            Require(!RenderTextureAssetLoader::LoadTexture(file).has_value(),
                "A file that is not a decodable image must fail the load, not abort the process");
        }
        std::filesystem::remove_all(corruptRoot);
    }

    std::istringstream invalidVolume{ "dimension 3d\nsize 2 2\ndepth 1\nrgba8 1 2 3 255\n" };
    Require(!RenderTextureAssetLoader::LoadTexture(invalidVolume).has_value(),
        "A depth-1 texture must not be accepted as Texture3D because bgfx classifies it as Texture2D");
    RenderTextureDesc invalidVolumeDesc{};
    invalidVolumeDesc.width = 2U;
    invalidVolumeDesc.height = 2U;
    invalidVolumeDesc.depth = 1U;
    invalidVolumeDesc.dimension = RenderTextureDimension::Texture3D;
    invalidVolumeDesc.format = bgfx::TextureFormat::RGBA8;
    Require(!RenderTextureResourceBuilder::IsValidDesc(invalidVolumeDesc),
        "Texture resource builder must reject a depth-1 Texture3D descriptor");

    std::istringstream declaredMetadata{
        "dimension 2d\nsize 1 1\nsemantic normal\ncolorSpace linear\nrgba8 128 128 255 255\n"
    };
    const std::optional<RenderTextureAssetData> declared = RenderTextureAssetLoader::LoadTexture(declaredMetadata);
    Require(declared.has_value() &&
            declared->semantic == RenderTextureAssetSemantic::Normal &&
            declared->colorSpace == RenderTextureAssetColorSpace::Linear,
        "P1.35: Texture loader did not preserve authoritative semantic/color-space metadata");

    std::istringstream legacyMetadata{ "size 1 1\nrgba8 255 255 255 255\n" };
    const std::optional<RenderTextureAssetData> legacy = RenderTextureAssetLoader::LoadTexture(legacyMetadata);
    Require(legacy.has_value() &&
            legacy->semantic == RenderTextureAssetSemantic::Unknown &&
            legacy->colorSpace == RenderTextureAssetColorSpace::Unknown,
        "P1.35: Legacy texture assets must load compatibly with explicit Unknown authoring metadata");
}

void RunMaterialHandlesAreGenerationalTest() {
    RenderResourceRegistry registry;
    RenderMaterialDesc desc{};
    desc.baseColor[0] = 0.25F;
    desc.baseColor[1] = 0.5F;
    desc.baseColor[2] = 0.75F;
    desc.baseColor[3] = 1.0F;
    desc.emissiveColor[0] = 0.1F;
    desc.emissiveColor[1] = 0.2F;
    desc.emissiveColor[2] = 0.3F;
    desc.metallicFactor = 0.6F;
    desc.roughnessFactor = 0.35F;
    desc.normalScale = 0.8F;
    desc.occlusionStrength = 0.65F;
    desc.emissiveStrength = 2.0F;
    desc.clearcoatFactor = 0.8F;
    desc.clearcoatRoughnessFactor = 0.2F;
    desc.sheenColor[0] = 0.9F;
    desc.sheenColor[1] = 0.8F;
    desc.sheenColor[2] = 0.7F;
    desc.sheenRoughnessFactor = 0.45F;
    desc.transmissionFactor = 0.3F;
    desc.thicknessFactor = 0.12F;
    desc.attenuationColor[0] = 0.6F;
    desc.subsurfaceFactor = 0.25F;
    desc.anisotropyStrength = 0.5F;
    desc.anisotropyRotation = 0.125F;
    desc.decalBlendMode = RenderMaterialDecalBlendMode::Pbr;
    desc.layerBlendMode = RenderMaterialLayerBlendMode::Multiply;
    desc.alphaMode = RenderMaterialAlphaMode::Mask;
    desc.doubleSided = true;
    desc.normalTextureAssetId = 101U;
    desc.metallicRoughnessTextureAssetId = 102U;
    desc.occlusionTextureAssetId = 103U;
    desc.emissiveTextureAssetId = 104U;
    desc.clearcoatTextureAssetId = 105U;
    desc.transmissionTextureAssetId = 106U;
    desc.layerMaskTextureAssetId = 107U;

    const RenderMaterialHandle first = registry.RegisterMaterial(desc);
    Require(first.IsValid(), "RenderResourceRegistry did not allocate a material handle");

    const RenderMaterialResource* material = registry.FindMaterial(first);
    Require(material != nullptr, "RenderResourceRegistry could not resolve a live material");
    const std::uint64_t firstVersion = material->version;
    Require(firstVersion != 0U, "RenderResourceRegistry did not assign material resource version");
    Require(NearlyEqual(material->baseColor[0], 0.25F), "RenderResourceRegistry did not preserve material base color");
    Require(NearlyEqual(material->emissiveColor[2], 0.3F), "RenderResourceRegistry did not preserve material emissive color");
    Require(NearlyEqual(material->metallicFactor, 0.6F), "RenderResourceRegistry did not preserve material metallic factor");
    Require(NearlyEqual(material->roughnessFactor, 0.35F), "RenderResourceRegistry did not preserve material roughness factor");
    Require(NearlyEqual(material->normalScale, 0.8F), "RenderResourceRegistry did not preserve material normal scale");
    Require(NearlyEqual(material->occlusionStrength, 0.65F), "RenderResourceRegistry did not preserve material occlusion strength");
    Require(NearlyEqual(material->emissiveStrength, 2.0F), "RenderResourceRegistry did not preserve material emissive strength");
    Require(NearlyEqual(material->clearcoatFactor, 0.8F), "RenderResourceRegistry did not preserve material clearcoat factor");
    Require(NearlyEqual(material->clearcoatRoughnessFactor, 0.2F), "RenderResourceRegistry did not preserve material clearcoat roughness");
    Require(NearlyEqual(material->sheenColor[1], 0.8F), "RenderResourceRegistry did not preserve material sheen color");
    Require(NearlyEqual(material->transmissionFactor, 0.3F), "RenderResourceRegistry did not preserve material transmission factor");
    Require(NearlyEqual(material->thicknessFactor, 0.12F), "RenderResourceRegistry did not preserve material thickness factor");
    Require(NearlyEqual(material->attenuationColor[0], 0.6F), "RenderResourceRegistry did not preserve material attenuation color");
    Require(NearlyEqual(material->subsurfaceFactor, 0.25F), "RenderResourceRegistry did not preserve material subsurface factor");
    Require(NearlyEqual(material->anisotropyStrength, 0.5F), "RenderResourceRegistry did not preserve material anisotropy strength");
    Require(NearlyEqual(material->anisotropyRotation, 0.125F), "RenderResourceRegistry did not preserve material anisotropy rotation");
    Require(material->decalBlendMode == RenderMaterialDecalBlendMode::Pbr, "RenderResourceRegistry did not preserve material decal mode");
    Require(material->layerBlendMode == RenderMaterialLayerBlendMode::Multiply, "RenderResourceRegistry did not preserve material layer mode");
    Require(material->alphaMode == RenderMaterialAlphaMode::Mask, "RenderResourceRegistry did not preserve material alpha mode");
    Require(material->doubleSided, "RenderResourceRegistry did not preserve material double sided state");
    Require(material->normalTextureAssetId == 101U, "RenderResourceRegistry did not preserve material normal texture asset id");
    Require(material->metallicRoughnessTextureAssetId == 102U, "RenderResourceRegistry did not preserve material metallic-roughness texture asset id");
    Require(material->occlusionTextureAssetId == 103U, "RenderResourceRegistry did not preserve material occlusion texture asset id");
    Require(material->emissiveTextureAssetId == 104U, "RenderResourceRegistry did not preserve material emissive texture asset id");
    Require(material->clearcoatTextureAssetId == 105U, "RenderResourceRegistry did not preserve material clearcoat texture asset id");
    Require(material->transmissionTextureAssetId == 106U, "RenderResourceRegistry did not preserve material transmission texture asset id");
    Require(material->layerMaskTextureAssetId == 107U, "RenderResourceRegistry did not preserve material layer mask texture asset id");

    registry.DestroyMaterial(first);
    Require(registry.FindMaterial(first) == nullptr, "Destroyed material handle should stop resolving immediately");
    Require(registry.Stats().pendingDestroyCount == 1U, "Destroyed material should enter deferred destroy queue");

    for (int frame = 0; frame < 4; ++frame) {
        registry.TickFrame();
    }
    Require(registry.Stats().pendingDestroyCount == 0U, "Deferred material destroy did not drain after grace frames");

    const RenderMaterialHandle second = registry.RegisterMaterial(desc);
    Require(second.IsValid(), "RenderResourceRegistry did not allocate a second material handle");
    Require(second.Index() == first.Index(), "RenderResourceRegistry should reuse released material slots");
    Require(second.Generation() != first.Generation(), "RenderResourceRegistry reused a slot without changing generation");
    Require(registry.FindMaterial(first) == nullptr, "Stale material handle resolved after slot reuse");
    const RenderMaterialResource* secondMaterial = registry.FindMaterial(second);
    Require(secondMaterial != nullptr, "New material handle did not resolve after slot reuse");
    Require(secondMaterial->version != firstVersion, "RenderResourceRegistry reused a material resource version after slot reuse");
}

void RunGraphBlendModeDrivesResourceRenderStateTest() {
    // #25d: an active graph program's resolved blend mode overrides the resource render state so the
    // scene submits a translucent graph material in the transparent pass with the authored blend equation.
    RenderResourceRegistry registry;
    RenderMaterialDesc desc{};
    desc.alphaMode = RenderMaterialAlphaMode::Opaque; // desc is opaque; the graph blend mode must win.

    RenderMaterialGraphProgramBinding binding{};
    binding.active = true;
    binding.alphaMode = RenderMaterialAlphaMode::Blend;
    binding.translucencyBlend = RenderMaterialTranslucencyBlend::Additive;

    const RenderMaterialHandle handle = registry.RegisterMaterial(desc, std::move(binding));
    const RenderMaterialResource* material = registry.FindMaterial(handle);
    Require(material != nullptr, "#25d: graph material must register");
    Require(material->alphaMode == RenderMaterialAlphaMode::Blend,
        "#25d: an active graph program's blend mode must drive the resource alpha mode (transparent pass)");
    Require(material->translucencyBlend == RenderMaterialTranslucencyBlend::Additive,
        "#25d: an active graph program's blend equation must drive the resource translucency blend");

    RenderMaterialDesc opaqueDesc{};
    opaqueDesc.alphaMode = RenderMaterialAlphaMode::Opaque;
    const RenderMaterialHandle opaqueHandle = registry.RegisterMaterial(opaqueDesc);
    const RenderMaterialResource* opaque = registry.FindMaterial(opaqueHandle);
    Require(opaque != nullptr && opaque->alphaMode == RenderMaterialAlphaMode::Opaque,
        "#25d: a non-graph material must keep its authored alpha mode");
}

void RunMaterialReloadInvalidatesStaleSceneBindingTest() {
    RenderResourceRegistry registry;
    SceneRenderResourceMap resourceMap;
    RenderMaterialDesc desc{};
    desc.albedoTextureAssetId = 99U;
    desc.baseColor[0] = 0.2F;

    const RenderMaterialHandle first = registry.RegisterMaterial(desc);
    Require(first.IsValid(), "RenderResourceRegistry did not allocate material before reload");
    resourceMap.BindMaterial(7U, first);
    Require(resourceMap.ResolveMaterial(7U) == first, "SceneRenderResourceMap did not bind the initial material");

    const RenderMaterialResource* firstResource = registry.FindMaterial(first);
    Require(firstResource != nullptr, "Initial material resource did not resolve before reload");
    Require(firstResource->albedoTextureAssetId == 99U, "Material resource did not preserve texture asset id");

    registry.DestroyMaterial(first);
    resourceMap.PruneInvalidBindings(registry);
    Require(!resourceMap.ResolveMaterial(7U).IsValid(), "SceneRenderResourceMap kept stale material binding after reload destroy");
    Require(registry.Stats().frameNumber == 0U, "Registry frame number changed before TickFrame");

    for (int frame = 0; frame < 3; ++frame) {
        registry.TickFrame();
    }
    Require(registry.Stats().frameNumber == 3U, "Registry did not advance deferred destroy frame number");
    Require(registry.Stats().pendingDestroyCount == 0U, "Frame-number deferred destroy did not release at target frame");

    desc.baseColor[0] = 0.8F;
    const RenderMaterialHandle second = registry.RegisterMaterial(desc);
    Require(second.IsValid(), "RenderResourceRegistry did not allocate material after reload");
    Require(second.Index() == first.Index(), "Reloaded material did not reuse drained slot");
    Require(second.Generation() != first.Generation(), "Reloaded material reused stale generation");
    Require(registry.FindMaterial(first) == nullptr, "Stale material handle resolved after reload");

    resourceMap.BindMaterial(7U, second);
    Require(resourceMap.ResolveMaterial(7U) == second, "SceneRenderResourceMap did not bind reloaded material");
}

void RunInvalidHandlesAreIgnoredTest() {
    RenderResourceRegistry registry;
    registry.DestroyMaterial(RenderMaterialHandle{});
    registry.DestroyMaterial(RenderMaterialHandle{ 0x0000'0001'0000'0000ULL });
    registry.DestroyMesh(RenderMeshHandle{ 0xFFFF'FFFFULL });
    registry.DestroyMesh(RenderMeshHandle{ 0x0000'0001'0000'0000ULL });
    registry.DestroyTexture(RenderTextureHandle{ 0xFFFF'FFFFULL });
    registry.DestroyTexture(RenderTextureHandle{ 0x0000'0001'0000'0000ULL });
    Require(registry.Stats().pendingDestroyCount == 0U, "Invalid handles should not enter deferred destroy queue");
    Require(registry.FindMaterial(RenderMaterialHandle{}) == nullptr, "Invalid material handle resolved unexpectedly");
    Require(registry.FindMaterial(RenderMaterialHandle{ 0x0000'0001'0000'0000ULL }) == nullptr, "Zero-slot material handle resolved unexpectedly");
    Require(registry.FindMesh(RenderMeshHandle{}) == nullptr, "Invalid mesh handle resolved unexpectedly");
    Require(registry.FindMesh(RenderMeshHandle{ 0x0000'0001'0000'0000ULL }) == nullptr, "Zero-slot mesh handle resolved unexpectedly");
    Require(registry.FindTexture(RenderTextureHandle{}) == nullptr, "Invalid texture handle resolved unexpectedly");
    Require(registry.FindTexture(RenderTextureHandle{ 0x0000'0001'0000'0000ULL }) == nullptr, "Zero-slot texture handle resolved unexpectedly");
}

void RunShutdownInvalidatesLiveHandlesTest() {
    RenderResourceRegistry registry;
    const RenderMaterialHandle beforeShutdown = registry.RegisterMaterial(RenderMaterialDesc{});
    Require(registry.FindMaterial(beforeShutdown) != nullptr, "Material handle should resolve before shutdown");

    registry.Shutdown();
    Require(registry.FindMaterial(beforeShutdown) == nullptr, "Shutdown should invalidate live material handles");

    const RenderMaterialHandle afterShutdown = registry.RegisterMaterial(RenderMaterialDesc{});
    Require(afterShutdown.IsValid(), "Material registration failed after registry shutdown");
    Require(afterShutdown.Index() == beforeShutdown.Index(), "Registry should reuse slots after shutdown");
    Require(afterShutdown.Generation() != beforeShutdown.Generation(), "Registry reused slot after shutdown without generation bump");
}

void RunReserveAndStatsReportPoolPressureTest() {
    RenderResourceRegistry registry;
    registry.Reserve(RenderResourceRegistryReserveDesc{
        .meshSlots = 256U,
        .materialSlots = 512U,
        .textureSlots = 128U,
    });

    RenderResourceRegistryStats stats = registry.Stats();
    Require(stats.meshSlotCapacity >= 256U, "Reserve did not preallocate mesh slot capacity");
    Require(stats.materialSlotCapacity >= 512U, "Reserve did not preallocate material slot capacity");
    Require(stats.textureSlotCapacity >= 128U, "Reserve did not preallocate texture slot capacity");

    const RenderMaterialHandle first = registry.RegisterMaterial(RenderMaterialDesc{});
    const RenderMaterialHandle second = registry.RegisterMaterial(RenderMaterialDesc{});
    Require(first.IsValid() && second.IsValid(), "Reserved registry failed to allocate materials");
    registry.DestroyMaterial(first);

    stats = registry.Stats();
    Require(stats.materialCount == 1U, "Registry stats counted pending-destroy materials as live");
    Require(stats.pendingDestroyCount == 1U, "Registry stats did not count pending destroys");
    Require(stats.pendingMaterialDestroyCount == 1U, "Registry stats did not count pending material destroys by kind");
}

void RunStaticMeshVertexFormatsExposeExpectedStridesTest() {
    Require(RenderStaticMeshVertexStride(RenderVertexFormat::P3C3) == sizeof(RenderStaticMeshVertex), "P3C3 stride mismatch");
    Require(RenderStaticMeshVertexStride(RenderVertexFormat::P3N3UV2) == sizeof(RenderStaticMeshVertexP3N3UV2), "P3N3UV2 stride mismatch");
    Require(RenderStaticMeshVertexStride(RenderVertexFormat::P3N3T4UV2) == sizeof(RenderStaticMeshVertexP3N3T4UV2), "P3N3T4UV2 stride mismatch");
    Require(RenderStaticMeshVertexStride(RenderVertexFormat::SkinnedP3N3T4UV2J4W4) == sizeof(RenderStaticMeshVertexSkinned), "Skinned static mesh stride mismatch");
    Require(RenderStaticMeshVertexLayout(RenderVertexFormat::P3N3UV2).getStride() == sizeof(RenderStaticMeshVertexP3N3UV2), "P3N3UV2 layout stride mismatch");
}

[[nodiscard]] std::vector<std::uint8_t> MaterialDocumentBytes(const std::function<void(std::ostream&)>& write) {
    std::ostringstream output;
    write(output);
    const std::string text = output.str();
    return { reinterpret_cast<const std::uint8_t*>(text.data()),
        reinterpret_cast<const std::uint8_t*>(text.data()) + text.size() };
}

[[nodiscard]] kb::assets::bake::AssetBakeDigest StoreRuntimePackArtifact(
    kb::assets::bake::AssetPackWriter& writer,
    const kb::assets::bake::BakeTargetProfile& profile,
    std::span<const std::uint8_t> bytes,
    std::string_view assetType,
    std::string_view settings) {
    kb::assets::bake::AssetBakeKey key{
        .sourceContentHash = kb::assets::bake::HashBakeBytes(bytes),
        .bakerId = "RendererRuntimePackTest",
        .bakerVersion = "1",
        .targetProfileId = std::string{ profile.identifier },
        .targetProfileHash = kb::assets::bake::BakeTargetProfileFingerprint(profile),
        .settingsHash = kb::assets::bake::HashBakeText(settings),
    };
    Require(writer.BeginAsset(kb::assets::bake::BakedAssetDescriptor{
                .key = key,
                .assetTypeId = std::string{ assetType },
            }) == kb::assets::bake::BakedAssetSinkStatus::Success &&
            writer.WritePrimaryBlock(bytes, profile.packageBlockAlignmentBytes) ==
                kb::assets::bake::BakedAssetSinkStatus::Success &&
            writer.CommitAsset() == kb::assets::bake::BakedAssetSinkStatus::Success,
        "Packaged material test could not store an artifact");
    return key.Digest();
}

[[nodiscard]] kb::assets::bake::AssetBakeDigest StoreRuntimePackSource(
    kb::assets::bake::AssetPackWriter& writer,
    const kb::assets::bake::BakeTargetProfile& profile,
    std::span<const std::uint8_t> bytes,
    std::string_view settings) {
    std::vector<std::uint8_t> sourceBlob;
    Require(kb::assets::bake::EncodeRuntimeSourceBlob(bytes, sourceBlob),
        "Packaged material test could not encode source bytes");
    return StoreRuntimePackArtifact(
        writer, profile, sourceBlob, kb::assets::bake::kSourceAssetTypeId, settings);
}

void RunRuntimeShaderProviderRejectsTrailingQualifierFieldTest() {
    namespace bake = kb::assets::bake;

    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "21kb_runtime_shader_qualifier_test";
    const std::filesystem::path packPath = root / "malformed.kbpack";
    const std::filesystem::path sceneFile = root / "Main.21kbscene";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    kb::scene::Scene authoredScene;
    Require(!error && kb::scene::SceneDocumentService::Save(authoredScene, sceneFile, "Main"),
        "Runtime shader qualifier fixture could not write its Scene");
    std::ifstream sceneInput{ sceneFile, std::ios::binary };
    const std::vector<std::uint8_t> sceneBytes{
        std::istreambuf_iterator<char>{ sceneInput }, std::istreambuf_iterator<char>{} };
    const std::vector<std::uint8_t> materialBytes{ 'm', 'a', 't' };
    const std::vector<std::uint8_t> shaderBytes{ 's', 'h', 'd' };
    const std::string scenePath = "/Game/Scenes/Main.21kbscene";
    const std::string materialPath = "/Game/Materials/Malformed.kbmat";

    bake::AssetPackWriter writer{ packPath, profile };
    const bake::AssetBakeDigest sceneDigest =
        StoreRuntimePackSource(writer, profile, sceneBytes, scenePath);
    const bake::AssetBakeDigest materialDigest =
        StoreRuntimePackSource(writer, profile, materialBytes, materialPath);
    const bake::AssetBakeDigest shaderDigest = StoreRuntimePackArtifact(
        writer, profile, shaderBytes, bake::kMaterialShaderAssetTypeId, "malformed-qualifier");

    bake::RuntimeAssetManifest manifest{
        .targetProfileId = std::string{ profile.identifier },
        .targetProfileHash = bake::BakeTargetProfileFingerprint(profile),
    };
    manifest.descriptor.targetPlatforms = { "Windows" };
    manifest.settings.name = "MalformedShaderQualifier";
    manifest.settings.defaultMap = scenePath;
    manifest.assets = {
        bake::RuntimeAssetManifestEntry{
            .id = kb::assets::MakeAssetId(scenePath + ":Scene"),
            .type = "Scene",
            .name = "Main",
            .virtualPath = scenePath,
            .sourceExtension = ".21kbscene",
            .contentHash = bake::HashBakeBytes(sceneBytes),
            .artifacts = { bake::RuntimeArtifactReference{
                .digest = sceneDigest,
                .encoding = bake::RuntimeArtifactEncoding::SourceBytes,
            } },
        },
        bake::RuntimeAssetManifestEntry{
            .id = kb::assets::MakeAssetId(materialPath + ":RenderMaterial"),
            .type = "RenderMaterial",
            .name = "Malformed",
            .virtualPath = materialPath,
            .sourceExtension = ".kbmat",
            .contentHash = bake::HashBakeBytes(materialBytes),
            .artifacts = {
                bake::RuntimeArtifactReference{
                    .digest = materialDigest,
                    .encoding = bake::RuntimeArtifactEncoding::SourceBytes,
                },
                bake::RuntimeArtifactReference{
                    .digest = shaderDigest,
                    .encoding = bake::RuntimeArtifactEncoding::MaterialShader,
                    .qualifier = "1:2:BaseOpaque:dxbc:windows:fragment:junk",
                },
            },
        },
    };
    std::vector<std::uint8_t> manifestBytes;
    Require(bake::EncodeRuntimeAssetManifest(manifest, manifestBytes) ==
            bake::RuntimeAssetManifestStatus::Success,
        "Runtime shader qualifier fixture manifest could not be encoded");
    static_cast<void>(StoreRuntimePackArtifact(
        writer, profile, manifestBytes, bake::kRuntimeManifestAssetTypeId, "runtime-manifest"));
    Require(writer.Finish() == bake::BakedAssetSinkStatus::Success,
        "Runtime shader qualifier fixture pack could not be published");

    auto pack = std::make_shared<bake::RuntimeAssetPack>();
    Require(pack->Mount(packPath, profile) == bake::RuntimeAssetPackStatus::Success,
        "Runtime shader qualifier fixture pack could not be mounted");
    std::string providerError;
    Require(RuntimeAssetShaderProvider::Create(pack, providerError) == nullptr &&
            providerError.find("malformed material shader qualifier") != std::string::npos,
        "Runtime shader provider accepted a qualifier with a trailing field");
    std::filesystem::remove_all(root, error);
}

void RunMeshResourceBuildsSectionLocalVertexRangesTest() {
    const std::array<RenderStaticMeshVertex, 6U> vertices{
        RenderStaticMeshVertex{ .x = 0.0F, .y = 0.0F, .z = 0.0F },
        RenderStaticMeshVertex{ .x = 1.0F, .y = 0.0F, .z = 0.0F },
        RenderStaticMeshVertex{ .x = 0.0F, .y = 1.0F, .z = 0.0F },
        RenderStaticMeshVertex{ .x = 10.0F, .y = 0.0F, .z = 0.0F },
        RenderStaticMeshVertex{ .x = 11.0F, .y = 0.0F, .z = 0.0F },
        RenderStaticMeshVertex{ .x = 10.0F, .y = 1.0F, .z = 0.0F },
    };
    const std::array<std::uint16_t, 6U> indices{ 0U, 1U, 2U, 0U, 1U, 2U };
    const std::array<RenderMeshSectionDesc, 2U> sections{
        RenderMeshSectionDesc{
            .indexStart = 0U,
            .indexCount = 3U,
            .vertexStart = 0U,
            .vertexCount = 3U,
        },
        RenderMeshSectionDesc{
            .indexStart = 3U,
            .indexCount = 3U,
            .vertexStart = 3U,
            .vertexCount = 3U,
        },
    };
    const RenderMeshDesc desc{
        .vertices = vertices.data(),
        .vertexCount = static_cast<std::uint32_t>(vertices.size()),
        .indices = indices.data(),
        .indexCount = static_cast<std::uint32_t>(indices.size()),
        .sections = sections.data(),
        .sectionCount = static_cast<std::uint32_t>(sections.size()),
    };

    Require(RenderMeshResourceBuilder::IsValidDesc(desc),
        "Mesh registration rejected valid section-local 16-bit indices");
    const RenderMeshResource resource = RenderMeshResourceBuilder::Build(
        desc, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    Require(resource.sections.size() == 2U &&
            resource.sections[1].vertexStart == 3U && resource.sections[1].vertexCount == 3U,
        "Mesh registration lost a rebased section vertex range");
    Require(resource.sections[0].bounds.center[0] < 1.0F && resource.sections[1].bounds.center[0] > 10.0F,
        "Section bounds were computed without applying the section vertex base");

    std::array<std::uint16_t, 6U> invalidIndices = indices;
    invalidIndices[5] = 3U;
    RenderMeshDesc invalid = desc;
    invalid.indices = invalidIndices.data();
    Require(!RenderMeshResourceBuilder::IsValidDesc(invalid),
        "Mesh registration accepted an index outside its section-local vertex range");

    std::vector<RenderStaticMeshVertex> boundaryVertices(65'537U);
    const std::array<std::uint16_t, 3U> boundaryIndices{ 65'533U, 65'534U, 65'535U };
    RenderMeshSectionDesc boundarySection{
        .indexStart = 0U,
        .indexCount = 3U,
        .vertexStart = 0U,
        .vertexCount = 65'536U,
    };
    RenderMeshDesc boundaryDesc{
        .vertices = boundaryVertices.data(),
        .vertexCount = static_cast<std::uint32_t>(boundaryVertices.size()),
        .indices = boundaryIndices.data(),
        .indexCount = static_cast<std::uint32_t>(boundaryIndices.size()),
        .sections = &boundarySection,
        .sectionCount = 1U,
    };
    Require(RenderMeshResourceBuilder::IsValidDesc(boundaryDesc),
        "Mesh registration rejected the exact 65,536-vertex 16-bit boundary");
    boundarySection.vertexCount = 65'537U;
    Require(!RenderMeshResourceBuilder::IsValidDesc(boundaryDesc),
        "Mesh registration accepted a 16-bit section above 65,536 vertices");
}

void RunSkinnedMeshRegistrationContractTest() {
    RenderStaticMeshVertexSkinned vertices[]{
        { .x = 0.0F, .y = 0.0F, .z = 0.0F, .joints = { 0U, 1U, 0U, 0U }, .weights = { 0.75F, 0.25F, 0.0F, 0.0F } },
        { .x = 1.0F, .y = 0.0F, .z = 0.0F, .joints = { 0U, 1U, 0U, 0U }, .weights = { 0.75F, 0.25F, 0.0F, 0.0F } },
        { .x = 0.0F, .y = 1.0F, .z = 0.0F, .joints = { 0U, 1U, 0U, 0U }, .weights = { 0.75F, 0.25F, 0.0F, 0.0F } },
    };
    const std::uint16_t indices[]{ 0U, 1U, 2U };
    RenderMeshDesc desc{
        .vertexData = vertices,
        .vertexCount = 3U,
        .indices = indices,
        .indexCount = 3U,
        .vertexFormat = RenderVertexFormat::SkinnedP3N3T4UV2J4W4,
        .indexFormat = RenderIndexFormat::Uint16,
        .skinning = { .jointCount = 2U },
    };
    Require(RenderMeshResourceBuilder::IsValidDesc(desc),
        "Skinned mesh registration rejected a valid layout, influences, or joint range");
    const RenderMeshResource resource = RenderMeshResourceBuilder::Build(
        desc, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    Require(resource.vertexFormat == RenderVertexFormat::SkinnedP3N3T4UV2J4W4 &&
            resource.skinning.jointCount == 2U,
        "Skinned mesh registration did not retain its joint-range metadata");

    desc.skinning.jointCount = 0U;
    Require(!RenderMeshResourceBuilder::IsValidDesc(desc),
        "Skinned mesh registration accepted an absent joint range");
    desc.skinning.jointCount = 2U;
    vertices[1U].joints[0U] = 2U;
    Require(!RenderMeshResourceBuilder::IsValidDesc(desc),
        "Skinned mesh registration accepted an out-of-range joint index");
    vertices[1U].joints[0U] = 0U;
    vertices[1U].weights[0U] = 0.5F;
    Require(!RenderMeshResourceBuilder::IsValidDesc(desc),
        "Skinned mesh registration accepted non-normalized joint weights");
    vertices[1U].weights[0U] = 0.75F;
    desc.skinning.jointCount = kRenderSkinnedVertexJointLimit + 1U;
    Require(!RenderMeshResourceBuilder::IsValidDesc(desc),
        "Skinned mesh registration accepted a joint range beyond the BGFX Uint16 vertex layout");
}

void RunSkeletalMeshLodResourceUsesStablePaletteTest() {
    kb::scene::SkeletalMeshAsset asset{};
    asset.skeletonAssetId = 1U;
    asset.skeletonCompatibilitySignature = 1U;
    asset.conservativeBounds = { .center = { 0.5F, 0.5F, 0.0F }, .extents = { 0.5F, 0.5F, 0.1F } };
    asset.fixedBounds = asset.conservativeBounds;
    kb::scene::SkeletalMeshLod high{};
    high.requiredBones = { 1U };
    high.minScreenCoverage = 0.5F;
    high.vertices = { {}, { .position = { 1.0F, 0.0F, 0.0F } }, { .position = { 0.0F, 1.0F, 0.0F } } };
    high.indices = { 0U, 1U, 2U };
    high.sections = { { .firstIndex = 0U, .indexCount = 3U, .materialAssetId = 11U, .boneMap = { 1U } } };
    kb::scene::SkeletalMeshLod low = high;
    low.requiredBones = { 2U };
    low.minScreenCoverage = 0.0F;
    low.sections[0].materialAssetId = 12U;
    low.sections[0].boneMap = { 2U };
    asset.lods = { high, low };
    asset.morphTargets = { {
        .name = "Smile",
        .lodIndex = 0U,
        .deltas = { { .vertexIndex = 1U, .positionDelta = { 0.0F, 0.5F, 0.0F } } },
    } };

    const std::vector<std::string> morphNames{ "Smile" };
    const std::vector<float> morphWeights{ 0.5F };
    const auto resource = SkeletalMeshRenderResourceBuilder::Build(asset, morphNames, morphWeights);
    Require(resource.has_value() && resource->paletteBoneIds == std::vector<std::uint64_t>{ 1U, 2U } &&
            resource->desc.skinning.jointCount == 2U && resource->sections.size() == 2U &&
            resource->sections[0].lodLevel == 0U && resource->sections[1].lodLevel == 1U &&
            resource->dynamicVertexBuffer && resource->vertices.size() == 6U && resource->vertices[0].joints[0] == 0U &&
            resource->vertices[1].y == 0.25F &&
            resource->vertices[3].joints[0] == 1U && RenderMeshResourceBuilder::IsValidDesc(resource->desc),
        "Skeletal mesh LOD resource did not remap every section into one stable palette");
}

void RunSkeletalMeshValidatedBuildMatchesCheckedBuildTest() {
    kb::scene::SkeletalMeshAsset asset{};
    asset.skeletonAssetId = 10U;
    asset.skeletonCompatibilitySignature = 20U;
    asset.conservativeBounds = { .center = { 0.5F, 0.5F, 0.0F }, .extents = { 0.5F, 0.5F, 0.1F } };
    asset.fixedBounds = { .center = { 1.0F, 2.0F, 3.0F }, .extents = { 5.0F, 6.0F, 7.0F } };
    asset.boundsMode = kb::scene::SkeletalMeshBoundsMode::Fixed;

    kb::scene::SkeletalMeshVertex vertex{};
    vertex.jointIndices = { 0U, 1U, 0U, 1U };
    vertex.jointWeights = { 0.5F, 0.5F, 0.0F, 0.0F };
    kb::scene::SkeletalMeshLod lod{};
    lod.vertices = { vertex, vertex, vertex };
    lod.vertices[1].position = { 1.0F, 0.0F, 0.0F };
    lod.vertices[2].position = { 0.0F, 1.0F, 0.0F };
    lod.indices = { 0U, 1U, 2U };
    lod.sections = { kb::scene::SkeletalMeshSection{
        .firstIndex = 0U,
        .indexCount = 3U,
        .materialAssetId = 30U,
        .boneMap = { 20U, 10U },
    } };
    lod.requiredBones = { 10U, 20U };
    asset.lods.push_back(std::move(lod));
    asset.morphTargets.push_back(kb::scene::SkeletalMeshMorphTarget{
        .name = "Unused",
        .lodIndex = 0U,
        .deltas = { kb::scene::SkeletalMeshMorphDelta{
            .vertexIndex = 0U,
            .positionDelta = { 0.25F, 0.0F, 0.0F },
        } },
    });

    Require(kb::scene::ValidateSkeletalMeshAsset(asset).valid,
        "Validated skeletal mesh builder test fixture is invalid");
    const auto checked = SkeletalMeshRenderResourceBuilder::Build(asset);
    const auto validated = SkeletalMeshRenderResourceBuilder::BuildValidated(asset);
    Require(checked.has_value() && validated.has_value(),
        "Checked and validated skeletal mesh builds should both accept a valid asset");
    Require(checked->vertices.size() == validated->vertices.size() &&
            checked->indices == validated->indices && checked->sections.size() == validated->sections.size() &&
            checked->lods.size() == validated->lods.size() &&
            checked->materialSlots.size() == validated->materialSlots.size() &&
            checked->paletteBoneIds == validated->paletteBoneIds,
        "Validated skeletal mesh build diverged from the checked build");
    Require(validated->vertices.size() == 3U && validated->vertices[0].joints[0] == 1U &&
            validated->vertices[0].joints[1] == 0U,
        "Validated skeletal mesh build did not use the section-to-palette remap");
    Require(std::fabs(validated->vertices[0].x) <= 0.0001F && validated->dynamicVertexBuffer,
        "A zero-active-morph build changed vertex data or lost its dynamic-buffer contract");
    Require(std::fabs(validated->bounds.center[0] - 1.0F) <= 0.0001F &&
            std::fabs(validated->bounds.center[1] - 2.0F) <= 0.0001F &&
            std::fabs(validated->bounds.center[2] - 3.0F) <= 0.0001F &&
            std::fabs(validated->bounds.radius - std::sqrt(110.0F)) <= 0.0001F,
        "A zero-active-morph build did not reuse the authored fixed bounds");

    kb::scene::SkeletalMeshAsset invalid = asset;
    invalid.lods[0].indices[0] = 99U;
    Require(!SkeletalMeshRenderResourceBuilder::Build(invalid).has_value(),
        "Checked skeletal mesh build must continue to reject invalid assets");
    const std::array<std::string, 1U> morphNames{ "Unused" };
    Require(!SkeletalMeshRenderResourceBuilder::BuildValidated(asset, morphNames, {}).has_value(),
        "Validated skeletal mesh build must reject mismatched morph input spans");
}

void RunSkinningPaletteAllocatorLifetimeTest() {
    RenderSkinningPaletteAllocator allocator{
        RenderSkinningPaletteAllocatorDesc{ .matrixCapacityPerFrame = 4U } };
    Require(allocator.BeginFrame(1U, 0U),
        "Skinning palette allocator could not begin its first frame");
    const RenderSkinningPaletteHandle first = allocator.Allocate(3U);
    Require(first.IsValid() && first.firstMatrix == 0U && first.matrixCount == 3U &&
            !allocator.Allocate(2U).IsValid(),
        "Skinning palette allocator did not enforce its per-frame capacity");
    Require(allocator.BeginFrame(2U, 0U) && allocator.Allocate(4U).IsValid() &&
            !allocator.BeginFrame(3U, 0U),
        "Skinning palette allocator overwrote a palette before its frame fence completed");
    Require(allocator.BeginFrame(3U, 1U) && allocator.Allocate(1U).IsValid(),
        "Skinning palette allocator did not reuse a completed double-buffered frame");
    const RenderSkinningPaletteAllocatorStats stats = allocator.Stats();
    Require(stats.activeFrame == 3U && stats.completedFrame == 1U &&
            stats.allocatedMatrices == 1U && stats.allocationFailures == 2U,
        "Skinning palette allocator did not report frame ownership or explicit pressure");
    allocator.Shutdown();
    Require(allocator.BeginFrame(1U, 0U) && allocator.Allocate(4U).IsValid(),
        "Skinning palette allocator did not reset cleanly after renderer reload");
}

void RunObjImporterBuildsRenderMeshDescWithSectionsAndSlotsTest() {
    const RenderMeshAssetMaterialBinding bindings[]{
        RenderMeshAssetMaterialBinding{ .materialName = "body", .materialAssetId = 101U },
        RenderMeshAssetMaterialBinding{ .materialName = "trim", .materialAssetId = 102U },
    };
    std::istringstream obj{
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 1 1 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 1 1\n"
        "vt 0 1\n"
        "vn 0 0 1\n"
        "usemtl body\n"
        "f 1/1/1 2/2/1 3/3/1 4/4/1\n"
        "usemtl trim\n"
        "f 1/1/1 3/3/1 4/4/1\n"
    };

    const std::optional<RenderMeshAssetData> asset = RenderMeshAssetBuilder::LoadObj(obj, RenderMeshObjImportDesc{
        .materialBindings = bindings,
        .materialBindingCount = 2U,
    });

    Require(asset.has_value(), "OBJ importer failed to build a mesh asset");
    Require(asset->desc.vertexFormat == RenderVertexFormat::P3N3T4UV2, "OBJ importer did not synthesize tangent vertex storage for the PBR shader");
    Require(asset->vertices.empty() && asset->tangentVertices.size() == asset->desc.vertexCount, "OBJ importer did not expose tangent vertices through RenderMeshDesc");
    for (const RenderStaticMeshVertexP3N3T4UV2& vertex : asset->tangentVertices) {
        Require(NearlyEqual(vertex.tx, 1.0F) && NearlyEqual(vertex.ty, 0.0F) && NearlyEqual(vertex.tz, 0.0F) && vertex.tw > 0.0F,
            "OBJ importer tangent fallback must use UV-derived +U tangents for normal maps");
    }
    Require(asset->desc.indexFormat == RenderIndexFormat::Uint16, "OBJ importer did not compact small OBJ indices to uint16");
    Require(asset->desc.vertexCount == 4U, "OBJ importer did not deduplicate shared vertex tuples");
    Require(asset->desc.indexCount == 9U, "OBJ importer did not triangulate face indices");
    Require(asset->sections.size() == 2U, "OBJ importer did not create material sections");
    Require(asset->materialNames.size() == 2U && asset->materialNames[0] == "body" && asset->materialNames[1] == "trim", "OBJ importer did not preserve material slot names");
    Require(asset->materialSlots.size() == 2U, "OBJ importer did not preserve mesh material slots");
    Require(asset->sections[0].materialSlot == 0U && asset->sections[1].materialSlot == 1U, "OBJ importer did not keep section-to-material-slot mapping");
    Require(asset->bounds.IsValid() && asset->desc.bounds.IsValid(), "OBJ importer did not compute mesh bounds");
    Require(NearlyEqual(asset->bounds.center[0], 0.5F) && NearlyEqual(asset->bounds.center[1], 0.5F), "OBJ importer computed the wrong mesh bounds center");
    Require(asset->sections[0].bounds.IsValid() && asset->sections[1].bounds.IsValid(), "OBJ importer did not compute section bounds");
    Require(asset->meshlets.size() == asset->sections.size(), "OBJ importer did not build meshlet metadata per section");
    Require(asset->lods.size() == 1U, "OBJ importer did not build base LOD metadata");
    Require(asset->desc.gpuDriven.meshletCount == asset->meshlets.size(), "OBJ importer did not expose meshlets through RenderMeshDesc");
    Require(asset->desc.gpuDriven.lodCount == asset->lods.size(), "OBJ importer did not expose LODs through RenderMeshDesc");
    Require(asset->materialSlots[asset->sections[0].materialSlot].defaultMaterialAssetId == 101U, "OBJ importer did not bind first material slot");
    Require(asset->materialSlots[asset->sections[1].materialSlot].defaultMaterialAssetId == 102U, "OBJ importer did not bind second material slot");
}

void RunObjImporterGeneratesMissingNormalsTest() {
    // An OBJ exported without `vn` carries no normals at all. Shading them with a single
    // constant up vector makes every triangle of the model receive identical light, so the
    // mesh renders as a flat silhouette no matter how it is lit.
    std::istringstream withoutNormals{
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 1 1 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 1 1\n"
        "vt 0 1\n"
        "f 1/1 2/2 3/3 4/4\n"
    };

    const std::optional<RenderMeshAssetData> generated = RenderMeshAssetBuilder::LoadObj(
        withoutNormals, RenderMeshObjImportDesc{});
    Require(generated.has_value(), "OBJ importer failed to build a mesh asset without source normals");
    Require(!generated->tangentVertices.empty(), "OBJ importer produced no vertices for the normal-less source");
    for (const RenderStaticMeshVertexP3N3T4UV2& vertex : generated->tangentVertices) {
        Require(
            NearlyEqual(vertex.nx, 0.0F) && NearlyEqual(vertex.ny, 0.0F) && NearlyEqual(vertex.nz, 1.0F),
            "OBJ importer must derive normals from the geometry when the source has none");
    }

    // A source that does author normals keeps them: generation is a fallback, not an override.
    std::istringstream withNormals{
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 1 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 1 1\n"
        "vn 0 1 0\n"
        "f 1/1/1 2/2/1 3/3/1\n"
    };

    const std::optional<RenderMeshAssetData> authored = RenderMeshAssetBuilder::LoadObj(
        withNormals, RenderMeshObjImportDesc{});
    Require(authored.has_value(), "OBJ importer failed to build a mesh asset with source normals");
    Require(!authored->tangentVertices.empty(), "OBJ importer produced no vertices for the authored-normal source");
    for (const RenderStaticMeshVertexP3N3T4UV2& vertex : authored->tangentVertices) {
        Require(
            NearlyEqual(vertex.nx, 0.0F) && NearlyEqual(vertex.ny, 1.0F) && NearlyEqual(vertex.nz, 0.0F),
            "OBJ importer must keep the normals a source authored");
    }
}

void AppendFbxU32(std::vector<std::byte>& output, std::uint32_t value) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        output.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

void AppendFbxU64(std::vector<std::byte>& output, std::uint64_t value) {
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
        output.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

void PatchFbxU32(std::vector<std::byte>& output, std::size_t offset, std::uint32_t value) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        output[offset + (shift / 8U)] = static_cast<std::byte>((value >> shift) & 0xFFU);
    }
}

void AppendFbxStringProperty(std::vector<std::byte>& output, std::string_view value) {
    output.push_back(std::byte{ 'S' });
    AppendFbxU32(output, static_cast<std::uint32_t>(value.size()));
    for (const char character : value) {
        output.push_back(static_cast<std::byte>(character));
    }
}

void AppendFbxInt64Property(std::vector<std::byte>& output, std::uint64_t value) {
    output.push_back(std::byte{ 'L' });
    AppendFbxU64(output, value);
}

template <typename T>
void AppendFbxArrayProperty(std::vector<std::byte>& output, char type, std::span<const T> values) {
    output.push_back(static_cast<std::byte>(type));
    AppendFbxU32(output, static_cast<std::uint32_t>(values.size()));
    AppendFbxU32(output, 0U);
    AppendFbxU32(output, static_cast<std::uint32_t>(values.size_bytes()));
    const auto* bytes = reinterpret_cast<const std::byte*>(values.data());
    output.insert(output.end(), bytes, bytes + static_cast<std::ptrdiff_t>(values.size_bytes()));
}

struct FbxFixtureNode {
    std::string name;
    std::vector<std::byte> properties;
    std::uint32_t propertyCount = 0U;
    std::vector<FbxFixtureNode> children;
};

void AppendFbxFixtureNode(std::vector<std::byte>& output, const FbxFixtureNode& node) {
    const std::size_t nodeOffset = output.size();
    output.resize(output.size() + 13U);
    output.insert(output.end(), reinterpret_cast<const std::byte*>(node.name.data()), reinterpret_cast<const std::byte*>(node.name.data()) + static_cast<std::ptrdiff_t>(node.name.size()));
    output.insert(output.end(), node.properties.begin(), node.properties.end());
    for (const FbxFixtureNode& child : node.children) {
        AppendFbxFixtureNode(output, child);
    }
    output.insert(output.end(), 13U, std::byte{});
    PatchFbxU32(output, nodeOffset, static_cast<std::uint32_t>(output.size()));
    PatchFbxU32(output, nodeOffset + 4U, node.propertyCount);
    PatchFbxU32(output, nodeOffset + 8U, static_cast<std::uint32_t>(node.properties.size()));
    output[nodeOffset + 12U] = static_cast<std::byte>(node.name.size());
}

[[nodiscard]] std::vector<std::byte> MakeMultiMaterialFbxFixture() {
    std::vector<std::byte> geometryProperties;
    AppendFbxInt64Property(geometryProperties, 1U);
    AppendFbxStringProperty(geometryProperties, "Geometry::TwoMaterialQuad");
    AppendFbxStringProperty(geometryProperties, "Mesh");

    const std::array<double, 12U> vertices{
        0.0, 0.0, 0.0,
        1.0, 0.0, 0.0,
        1.0, 1.0, 0.0,
        0.0, 1.0, 0.0,
    };
    std::vector<std::byte> verticesProperties;
    AppendFbxArrayProperty(verticesProperties, 'd', std::span<const double>{ vertices });
    const std::array<std::int32_t, 6U> polygonIndices{ 0, 1, -3, 0, 2, -4 };
    std::vector<std::byte> polygonProperties;
    AppendFbxArrayProperty(polygonProperties, 'i', std::span<const std::int32_t>{ polygonIndices });
    const std::array<std::int32_t, 2U> materialIndices{ 0, 1 };
    std::vector<std::byte> materialIndexProperties;
    AppendFbxArrayProperty(materialIndexProperties, 'i', std::span<const std::int32_t>{ materialIndices });
    std::vector<std::byte> byPolygonProperties;
    AppendFbxStringProperty(byPolygonProperties, "ByPolygon");

    std::vector<std::byte> firstMaterialProperties;
    AppendFbxInt64Property(firstMaterialProperties, 10U);
    AppendFbxStringProperty(firstMaterialProperties, "Material::Body");
    AppendFbxStringProperty(firstMaterialProperties, "");
    std::vector<std::byte> secondMaterialProperties;
    AppendFbxInt64Property(secondMaterialProperties, 11U);
    AppendFbxStringProperty(secondMaterialProperties, "Material::Trim");
    AppendFbxStringProperty(secondMaterialProperties, "");

    FbxFixtureNode objects{
        .name = "Objects",
        .children = {
            FbxFixtureNode{
                .name = "Geometry",
                .properties = std::move(geometryProperties),
                .propertyCount = 3U,
                .children = {
                    FbxFixtureNode{ .name = "Vertices", .properties = std::move(verticesProperties), .propertyCount = 1U },
                    FbxFixtureNode{ .name = "PolygonVertexIndex", .properties = std::move(polygonProperties), .propertyCount = 1U },
                    FbxFixtureNode{
                        .name = "LayerElementMaterial",
                        .children = {
                            FbxFixtureNode{ .name = "MappingInformationType", .properties = std::move(byPolygonProperties), .propertyCount = 1U },
                            FbxFixtureNode{ .name = "Materials", .properties = std::move(materialIndexProperties), .propertyCount = 1U },
                        },
                    },
                },
            },
            FbxFixtureNode{ .name = "Material", .properties = std::move(firstMaterialProperties), .propertyCount = 3U },
            FbxFixtureNode{ .name = "Material", .properties = std::move(secondMaterialProperties), .propertyCount = 3U },
        },
    };

    std::vector<std::byte> output;
    constexpr std::array<std::byte, 23U> magic{
        std::byte{ 'K' }, std::byte{ 'a' }, std::byte{ 'y' }, std::byte{ 'd' }, std::byte{ 'a' }, std::byte{ 'r' }, std::byte{ 'a' }, std::byte{ ' ' },
        std::byte{ 'F' }, std::byte{ 'B' }, std::byte{ 'X' }, std::byte{ ' ' }, std::byte{ 'B' }, std::byte{ 'i' }, std::byte{ 'n' }, std::byte{ 'a' },
        std::byte{ 'r' }, std::byte{ 'y' }, std::byte{ ' ' }, std::byte{ ' ' }, std::byte{}, std::byte{ 0x1A }, std::byte{},
    };
    output.insert(output.end(), magic.begin(), magic.end());
    AppendFbxU32(output, 7400U);
    AppendFbxFixtureNode(output, objects);
    output.insert(output.end(), 13U, std::byte{});
    return output;
}

void RunFbxImporterBuildsSectionsForMaterialSlotsTest() {
    const std::vector<std::byte> fixture = MakeMultiMaterialFbxFixture();
    const std::optional<RenderMeshAssetData> asset = RenderMeshAssetBuilder::LoadFbx(std::span<const std::byte>{ fixture });

    Require(asset.has_value(), "FBX importer failed to load the multi-material fixture");
    Require(asset->materialSlots.size() == 2U, "FBX importer did not preserve both material slots");
    Require(asset->materialNames.size() == 2U && asset->materialNames[0] == "Body" && asset->materialNames[1] == "Trim",
        "FBX importer did not preserve material object names");
    Require(asset->sections.size() == 2U, "FBX importer did not create one section per used material slot");
    Require(asset->sections[0].materialSlot == 0U && asset->sections[1].materialSlot == 1U,
        "FBX importer lost polygon-to-material-slot assignments");
    Require(asset->sections[0].indexCount == 3U && asset->sections[1].indexCount == 3U,
        "FBX importer created incorrect material section index ranges");
}

// Every real binary FBX file appends a fixed ~161-byte footer AFTER the
// top-level null-record terminator (footer code + padding + version + magic).
// The prior fixture stopped at the terminator, so the parser never saw a
// footer — which is exactly why a genuine FBX (kuleczka.fbx, v7400) rendered
// nothing: ExtractGeometry parsed the footer as a node, failed, and discarded
// the already-decoded mesh (DC 0). This fixture reproduces the footer and
// asserts the geometry still loads.
void RunFbxImporterStopsAtFooterAfterNullTerminatorTest() {
    std::vector<std::byte> fixture = MakeMultiMaterialFbxFixture();
    const std::array<std::byte, 16U> footerHead{
        std::byte{ 0xF8 }, std::byte{ 0x5A }, std::byte{ 0x8C }, std::byte{ 0x6A },
        std::byte{ 0xDE }, std::byte{ 0xF5 }, std::byte{ 0xD9 }, std::byte{ 0x7E },
        std::byte{ 0x99 }, std::byte{ 0x35 }, std::byte{ 0x2A }, std::byte{ 0x8E },
        std::byte{ 0x33 }, std::byte{ 0xE9 }, std::byte{ 0xB2 }, std::byte{ 0x0F },
    };
    fixture.insert(fixture.end(), footerHead.begin(), footerHead.end());
    fixture.insert(fixture.end(), 120U, std::byte{});
    AppendFbxU32(fixture, 7400U);

    const std::optional<RenderMeshAssetData> asset = RenderMeshAssetBuilder::LoadFbx(std::span<const std::byte>{ fixture });
    Require(asset.has_value(), "FBX importer must load geometry even with the standard FBX footer trailing the null terminator");
    Require(asset->sections.size() == 2U, "FBX importer must preserve geometry sections when a footer follows the terminator");
    Require(asset->materialNames.size() == 2U, "FBX importer must preserve material names when a footer follows the terminator");
}

void RunRenderMeshAssetLoaderDiscoversAndLoadsObjThroughAssetManagerTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_mesh_asset_loader";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Render mesh asset loader test could not create temp root");

    const std::filesystem::path meshPath = root / "quad.obj";
    {
        std::ofstream output{ meshPath, std::ios::trunc };
        output
            << "v 0 0 0\n"
            << "v 1 0 0\n"
            << "v 1 1 0\n"
            << "v 0 1 0\n"
            << "vt 0 0\n"
            << "vt 1 0\n"
            << "vt 1 1\n"
            << "vt 0 1\n"
            << "vn 0 0 1\n"
            << "usemtl body\n"
            << "f 1/1/1 2/2/1 3/3/1\n"
            << "usemtl trim\n"
            << "f 1/1/1 3/3/1 4/4/1\n";
    }

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "AssetManager rejected RenderMeshAssetLoader");
    Require(manager.Mounts().Mount("Game", root), "AssetManager could not mount mesh asset test root");
    Require(manager.DiscoverMountedAssets() == 1U, "AssetManager did not discover the OBJ mesh asset");

    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath("/Game/quad.obj");
    Require(metadata != nullptr && metadata->type == "RenderMesh", "AssetManager registered the OBJ mesh with the wrong type");
    const kb::assets::AssetHandle<RenderMeshAssetData> asset = manager.Load<RenderMeshAssetData>(metadata->id);
    Require(asset.IsLoaded(), "AssetManager did not load RenderMeshAssetData through RenderMeshAssetLoader");
    Require(asset->desc.vertexCount == 4U && asset->desc.indexCount == 6U, "Loaded RenderMeshAssetData has the wrong geometry counts");
    Require(asset->materialNames.size() == 2U && asset->materialNames[0] == "body" && asset->materialNames[1] == "trim", "Loaded OBJ mesh did not preserve material slot names");
    Require(asset->materialSlots.size() == 2U && asset->sections.size() == 2U, "Loaded OBJ mesh did not preserve material slots");
    Require(asset->sections[0].materialSlot == 0U && asset->sections[1].materialSlot == 1U, "Loaded OBJ mesh did not preserve section material slot mapping");

    const std::string snapshotObj =
        "v 0 0 0\n"
        "v 2 0 0\n"
        "v 0 2 0\n"
        "f 1 2 3\n";
    const std::vector<std::uint8_t> snapshotBytes{ snapshotObj.begin(), snapshotObj.end() };
    RenderMeshAssetLoader snapshotLoader;
    kb::assets::AssetMetadata snapshotMetadata = *metadata;
    snapshotMetadata.sourceExtension = ".obj";
    const kb::assets::AssetLoadResult snapshot = snapshotLoader.Load(kb::assets::AssetLoadRequest{
        .metadata = snapshotMetadata,
        .resolvedPath = root / "missing.obj",
        .sourceBytes = std::span<const std::uint8_t>{ snapshotBytes },
    });
    Require(snapshot.Succeeded(), "Render mesh loader did not consume an immutable source snapshot");
    const auto snapshotMesh = std::static_pointer_cast<RenderMeshAssetData>(snapshot.asset);
    Require(snapshotMesh->desc.vertexCount == 3U && snapshotMesh->desc.indexCount == 3U,
        "Render mesh loader reread the filesystem instead of its source snapshot");

    std::filesystem::remove_all(root, error);
}

void RunRenderMeshAssetLoaderLoadsImportedObjContainerTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_imported_mesh_asset_loader";
    const std::filesystem::path sourceRoot = root / "External";
    const std::filesystem::path assetsRoot = root / "Project" / "Assets";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(sourceRoot, error);
    Require(!error, "Imported mesh loader test could not create temp root");

    const std::filesystem::path sourcePath = sourceRoot / "cube.obj";
    {
        std::ofstream output{ sourcePath, std::ios::trunc };
        output
            << "v 0 0 0\n"
            << "v 1 0 0\n"
            << "v 1 1 0\n"
            << "vt 0 0\n"
            << "vt 1 0\n"
            << "vt 1 1\n"
            << "vn 0 0 1\n"
            << "f 1/1/1 2/2/1 3/3/1\n";
    }

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<kb::assets::ImportedAssetLoader>()), "Imported asset loader registration failed for mesh container test");
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Render mesh loader registration failed for mesh container test");
    Require(manager.Mounts().Mount("Game", assetsRoot), "Imported mesh loader test could not mount project assets");

    const std::array<std::filesystem::path, 1> files{ sourcePath };
    const kb::assets::AssetImportResult imported = kb::assets::AssetImportService::ImportFiles(manager, files, "/Game/Meshes");
    Require(imported.Succeeded(), "Mesh source file did not import into a .21kb container");

    const kb::assets::AssetImportItemResult& item = imported.items.front();
    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(item.virtualPath);
    Require(metadata != nullptr && metadata->id == item.id, "Imported mesh metadata was not registered under the import id");
    Require(metadata != nullptr && metadata->type == "RenderMesh", "Imported mesh container was not registered as a render mesh");
    Require(metadata != nullptr && metadata->importCategory == "Mesh", "Imported mesh container did not expose the Mesh category");

    const kb::assets::AssetHandle<RenderMeshAssetData> loaded = manager.Load<RenderMeshAssetData>(item.id);
    Require(loaded.IsLoaded(), "RenderMeshAssetLoader did not load a mesh from the .21kb container payload");
    Require(loaded->desc.vertexCount == 3U && loaded->desc.indexCount == 3U, "Imported mesh container loaded the wrong geometry counts");

    kb::assets::AssetManager rediscovered;
    Require(rediscovered.RegisterLoader(std::make_unique<kb::assets::ImportedAssetLoader>()), "Imported mesh rediscovery loader registration failed");
    Require(rediscovered.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Imported mesh render loader registration failed");
    Require(rediscovered.Mounts().Mount("Game", assetsRoot), "Imported mesh rediscovery could not mount project assets");
    Require(rediscovered.DiscoverMountedAssets() == 1U, "Imported mesh rediscovery did not find the .21kb mesh file");

    const kb::assets::AssetMetadata* rediscoveredMetadata = rediscovered.Registry().FindByPath(item.virtualPath);
    Require(rediscoveredMetadata != nullptr && rediscoveredMetadata->id == item.id, "Imported mesh rediscovery did not preserve the render mesh asset id");
    Require(rediscoveredMetadata != nullptr && rediscoveredMetadata->type == "RenderMesh", "Imported mesh rediscovery did not keep the render mesh type");
    const kb::assets::AssetHandle<RenderMeshAssetData> rediscoveredLoaded = rediscovered.Load<RenderMeshAssetData>(item.id);
    Require(rediscoveredLoaded.IsLoaded(), "Rediscovered .21kb mesh did not load as RenderMeshAssetData");

    std::filesystem::remove_all(root, error);
}

void RunRenderMeshAssetLoaderLoadsWorkspaceImportedFbxCubeWhenPresentTest() {
    const std::filesystem::path projectAssets = std::filesystem::current_path().parent_path() / "Project" / "Assets";
    const std::filesystem::path cubePath = projectAssets / "Cube.21kb";
    std::error_code error;
    if (!std::filesystem::is_regular_file(cubePath, error)) {
        return;
    }

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<kb::assets::ImportedAssetLoader>()), "Workspace FBX cube test could not register imported asset loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Workspace FBX cube test could not register render mesh loader");
    Require(manager.Mounts().Mount("Game", projectAssets), "Workspace FBX cube test could not mount Project/Assets");
    Require(manager.DiscoverMountedAssets() >= 1U, "Workspace FBX cube test did not discover Cube.21kb");

    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath("/Game/Cube.21kb");
    Require(metadata != nullptr && metadata->type == "RenderMesh", "Workspace Cube.21kb was not discovered as RenderMesh");
    Require(metadata != nullptr && metadata->importCategory == "Mesh", "Workspace Cube.21kb did not expose Mesh import category");

    const kb::assets::AssetHandle<RenderMeshAssetData> loaded = manager.Load<RenderMeshAssetData>(metadata->id);
    Require(loaded.IsLoaded(), "Workspace Cube.21kb FBX payload did not load as RenderMeshAssetData");
    Require(loaded->desc.vertexCount > 0U && loaded->desc.indexCount > 0U, "Workspace Cube.21kb loaded an empty mesh");
    Require(!loaded->tangentVertices.empty(), "Workspace Cube.21kb did not produce tangent vertex storage for material rendering");
    bool hasReadableUv = false;
    const float firstU = loaded->tangentVertices.front().u;
    const float firstV = loaded->tangentVertices.front().v;
    for (const RenderStaticMeshVertexP3N3T4UV2& vertex : loaded->tangentVertices) {
        if (std::abs(vertex.u - firstU) > 0.0001F || std::abs(vertex.v - firstV) > 0.0001F) {
            hasReadableUv = true;
            break;
        }
    }
    Require(hasReadableUv, "Workspace Cube.21kb FBX fallback UVs collapsed to a single texture sample");
}

void RunRenderMeshAssetLoaderDiscoversAndLoadsGltfThroughAssetManagerTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_gltf_asset_loader";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Render glTF asset loader test could not create temp root");

    const std::filesystem::path binPath = root / "mesh.bin";
    {
        const std::vector<float> positions{
            0.0F, 0.0F, 0.0F,
            1.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F,
        };
        const std::vector<float> normals{
            1.0F, 1.0F, 0.0F,
            1.0F, 1.0F, 0.0F,
            1.0F, 1.0F, 0.0F,
        };
        const std::vector<float> tangents{
            1.0F, 0.0F, 0.0F, 1.0F,
            1.0F, 0.0F, 0.0F, 1.0F,
            1.0F, 0.0F, 0.0F, 1.0F,
        };
        const std::vector<float> texCoords{
            0.0F, 0.0F,
            1.0F, 0.0F,
            0.0F, 1.0F,
        };
        const std::uint16_t indices[]{ 0U, 1U, 2U };

        std::ofstream output{ binPath, std::ios::binary | std::ios::trunc };
        output.write(reinterpret_cast<const char*>(positions.data()), static_cast<std::streamsize>(positions.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(normals.data()), static_cast<std::streamsize>(normals.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(tangents.data()), static_cast<std::streamsize>(tangents.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(texCoords.data()), static_cast<std::streamsize>(texCoords.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(indices), static_cast<std::streamsize>(sizeof(indices)));
        const std::uint16_t padding = 0U;
        output.write(reinterpret_cast<const char*>(&padding), static_cast<std::streamsize>(sizeof(padding)));
    }

    const std::filesystem::path gltfPath = root / "triangle.gltf";
    {
        std::ofstream output{ gltfPath, std::ios::trunc };
        output
            << "{\n"
            << "  \"asset\": { \"version\": \"2.0\" },\n"
            << "  \"scene\": 0,\n"
            << "  \"scenes\": [{ \"nodes\": [0] }],\n"
            << "  \"nodes\": [{ \"mesh\": 0, \"matrix\": [0, 2, 0, 0, -3, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 1] }],\n"
            << "  \"materials\": [{\n"
            << "    \"name\": \"surface\",\n"
            << "    \"pbrMetallicRoughness\": {\n"
            << "      \"baseColorFactor\": [0.2, 0.4, 0.8, 0.5],\n"
            << "      \"metallicFactor\": 0.7,\n"
            << "      \"roughnessFactor\": 0.35,\n"
            << "      \"baseColorTexture\": { \"index\": 0, \"extensions\": { \"KHR_texture_transform\": { \"offset\": [0.25, 0.5], \"scale\": [2.0, 3.0] } } },\n"
            << "      \"metallicRoughnessTexture\": { \"index\": 1 }\n"
            << "    },\n"
            << "    \"normalTexture\": { \"index\": 2, \"scale\": 0.75 },\n"
            << "    \"occlusionTexture\": { \"index\": 3, \"strength\": 0.6 },\n"
            << "    \"emissiveFactor\": [0.1, 0.2, 0.3],\n"
            << "    \"emissiveTexture\": { \"index\": 4 },\n"
            << "    \"alphaMode\": \"BLEND\",\n"
            << "    \"doubleSided\": true,\n"
            << "    \"alphaCutoff\": 0.45,\n"
            << "    \"extensions\": { \"KHR_materials_emissive_strength\": { \"emissiveStrength\": 2.5 } }\n"
            << "  }],\n"
            << "  \"textures\": [{ \"source\": 0 }, { \"source\": 1 }, { \"source\": 2 }, { \"source\": 3 }, { \"source\": 4 }],\n"
            << "  \"images\": [{ \"uri\": \"albedo.png\" }, { \"uri\": \"mr.png\" }, { \"uri\": \"normal.png\" }, { \"uri\": \"ao.png\" }, { \"uri\": \"emissive.png\" }],\n"
            << "  \"extensionsUsed\": [\"KHR_materials_emissive_strength\", \"KHR_texture_transform\"],\n"
            << "  \"meshes\": [{ \"primitives\": [{ \"attributes\": { \"POSITION\": 0, \"NORMAL\": 1, \"TANGENT\": 2, \"TEXCOORD_0\": 3 }, \"indices\": 4, \"material\": 0 }] }],\n"
            << "  \"buffers\": [{ \"uri\": \"mesh.bin\", \"byteLength\": 152 }],\n"
            << "  \"bufferViews\": [\n"
            << "    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 36, \"target\": 34962 },\n"
            << "    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 36, \"target\": 34962 },\n"
            << "    { \"buffer\": 0, \"byteOffset\": 72, \"byteLength\": 48, \"target\": 34962 },\n"
            << "    { \"buffer\": 0, \"byteOffset\": 120, \"byteLength\": 24, \"target\": 34962 },\n"
            << "    { \"buffer\": 0, \"byteOffset\": 144, \"byteLength\": 6, \"target\": 34963 }\n"
            << "  ],\n"
            << "  \"accessors\": [\n"
            << "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\", \"min\": [0, 0, 0], \"max\": [1, 1, 0] },\n"
            << "    { \"bufferView\": 1, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
            << "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC4\" },\n"
            << "    { \"bufferView\": 3, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC2\" },\n"
            << "    { \"bufferView\": 4, \"componentType\": 5123, \"count\": 3, \"type\": \"SCALAR\" }\n"
            << "  ]\n"
            << "}\n";
    }

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "AssetManager rejected RenderMeshAssetLoader for glTF");
    Require(manager.Mounts().Mount("Game", root), "AssetManager could not mount glTF mesh asset test root");
    Require(manager.DiscoverMountedAssets() == 1U, "AssetManager did not discover exactly one glTF mesh asset");

    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath("/Game/triangle.gltf");
    Require(metadata != nullptr && metadata->type == "RenderMesh", "AssetManager registered the glTF mesh with the wrong type");
    const kb::assets::AssetHandle<RenderMeshAssetData> asset = manager.Load<RenderMeshAssetData>(metadata->id);
    Require(asset.IsLoaded(), "AssetManager did not load RenderMeshAssetData through glTF loader");
    Require(asset->desc.vertexFormat == RenderVertexFormat::P3N3T4UV2, "glTF importer did not choose the tangent static mesh vertex format");
    Require(asset->desc.vertexCount == 3U && asset->desc.indexCount == 3U, "glTF importer produced wrong geometry counts");
    Require(asset->tangentVertices.size() == 3U && asset->vertices.empty(), "glTF importer did not store tangent vertices");
    Require(NearlyEqual(asset->tangentVertices[0].nx, -0.5547002F) && NearlyEqual(asset->tangentVertices[0].ny, 0.8320503F), "glTF importer did not transform normals through inverse-transpose");
    Require(NearlyEqual(asset->tangentVertices[0].tx, 0.0F) && NearlyEqual(asset->tangentVertices[0].ty, 1.0F), "glTF importer did not transform tangents through the node transform");
    Require(asset->sections.size() == 1U && asset->sections[0].indexCount == 3U, "glTF importer did not create one primitive section");
    Require(asset->materialNames.size() == 1U && asset->materialNames[0] == "surface", "glTF importer did not preserve material slot name");
    Require(asset->embeddedMaterials.size() == 1U, "glTF importer did not preserve embedded material data");
    std::ifstream gltfSnapshotInput{ gltfPath, std::ios::binary };
    const std::vector<std::uint8_t> gltfSnapshot{
        std::istreambuf_iterator<char>{ gltfSnapshotInput }, std::istreambuf_iterator<char>{} };
    RenderMeshAssetLoader snapshotLoader;
    kb::assets::AssetMetadata snapshotMetadata = *metadata;
    snapshotMetadata.sourceExtension = ".gltf";
    const std::filesystem::path snapshotRoot = root / "cook-snapshot";
    std::filesystem::create_directories(snapshotRoot, error);
    Require(!error, "glTF cooker snapshot mirror could not be created");
    std::filesystem::copy_file(
        binPath, snapshotRoot / "mesh.bin", std::filesystem::copy_options::overwrite_existing, error);
    Require(!error, "glTF external buffer could not be copied into the cooker snapshot mirror");
    std::filesystem::remove(binPath, error);
    Require(!error, "glTF snapshot test could not remove the live external buffer");
    const kb::assets::AssetLoadResult snapshot = snapshotLoader.Load(kb::assets::AssetLoadRequest{
        .metadata = snapshotMetadata,
        .resolvedPath = snapshotRoot / "triangle.gltf",
        .sourceBytes = std::span<const std::uint8_t>{ gltfSnapshot },
    });
    Require(snapshot.Succeeded(), "glTF loader did not resolve external buffers from the cooker snapshot mirror");
    const auto snapshotMesh = std::static_pointer_cast<RenderMeshAssetData>(snapshot.asset);
    Require(snapshotMesh->desc.vertexCount == 3U && snapshotMesh->desc.indexCount == 3U,
        "glTF loader reread live files instead of the complete cooker snapshot");
    const kb::assets::AssetLoadResult missingSidecar = snapshotLoader.Load(kb::assets::AssetLoadRequest{
        .metadata = snapshotMetadata,
        .resolvedPath = root / "missing.gltf",
        .sourceBytes = std::span<const std::uint8_t>{ gltfSnapshot },
    });
    Require(!missingSidecar.Succeeded(),
        "glTF loader accepted a source snapshot whose external buffer was not captured");
    Require(NearlyEqual(asset->embeddedMaterials[0].desc.baseColor[0], 0.2F), "glTF importer did not preserve embedded material base color");
    Require(NearlyEqual(asset->embeddedMaterials[0].desc.baseColor[3], 0.5F), "glTF importer did not preserve embedded material alpha");
    Require(NearlyEqual(asset->embeddedMaterials[0].desc.metallicFactor, 0.7F), "glTF importer did not preserve embedded material metallic factor");
    Require(NearlyEqual(asset->embeddedMaterials[0].desc.roughnessFactor, 0.35F), "glTF importer did not preserve embedded material roughness factor");
    Require(NearlyEqual(asset->embeddedMaterials[0].desc.normalScale, 0.75F), "glTF importer did not preserve embedded material normal scale");
    Require(NearlyEqual(asset->embeddedMaterials[0].desc.occlusionStrength, 0.6F), "glTF importer did not preserve embedded material occlusion strength");
    Require(NearlyEqual(asset->embeddedMaterials[0].desc.emissiveColor[2], 0.3F), "glTF importer did not preserve embedded material emissive color");
    Require(NearlyEqual(asset->embeddedMaterials[0].desc.emissiveStrength, 2.5F), "glTF importer did not preserve embedded material emissive strength");
    Require(NearlyEqual(asset->embeddedMaterials[0].desc.alphaCutoff, 0.45F), "glTF importer did not preserve embedded material alpha cutoff");
    Require(NearlyEqual(asset->embeddedMaterials[0].desc.uvTiling[0], 2.0F) && NearlyEqual(asset->embeddedMaterials[0].desc.uvTiling[1], 3.0F), "glTF importer did not preserve embedded material UV tiling");
    Require(NearlyEqual(asset->embeddedMaterials[0].desc.uvOffset[0], 0.25F) && NearlyEqual(asset->embeddedMaterials[0].desc.uvOffset[1], 0.5F), "glTF importer did not preserve embedded material UV offset");
    Require(asset->embeddedMaterials[0].desc.alphaMode == RenderMaterialAlphaMode::Blend, "glTF importer did not preserve embedded material alpha mode");
    Require(asset->embeddedMaterials[0].desc.doubleSided, "glTF importer did not preserve embedded material double sided state");
    Require(asset->embeddedMaterials[0].albedoTexturePath == "albedo.png", "glTF importer did not preserve embedded albedo texture path");
    Require(asset->embeddedMaterials[0].metallicRoughnessTexturePath == "mr.png", "glTF importer did not preserve embedded metallic-roughness texture path");
    Require(asset->embeddedMaterials[0].normalTexturePath == "normal.png", "glTF importer did not preserve embedded normal texture path");
    Require(asset->embeddedMaterials[0].occlusionTexturePath == "ao.png", "glTF importer did not preserve embedded occlusion texture path");
    Require(asset->embeddedMaterials[0].emissiveTexturePath == "emissive.png", "glTF importer did not preserve embedded emissive texture path");
    Require(asset->desc.bounds.IsValid() && asset->sections[0].bounds.IsValid(), "glTF importer did not compute mesh and section bounds");

    std::filesystem::remove_all(root, error);
}

void RunGltfImporterKeepsDefaultUvTransformForUnsupportedTextureRotationTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_gltf_material_unsupported_uv_transform";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Unsupported glTF texture transform test could not create temp root");

    const std::filesystem::path binPath = root / "mesh.bin";
    {
        const std::vector<float> positions{
            0.0F, 0.0F, 0.0F,
            1.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F,
        };
        const std::vector<float> texCoords{
            0.0F, 0.0F,
            1.0F, 0.0F,
            0.0F, 1.0F,
        };
        const std::uint16_t indices[]{ 0U, 1U, 2U };

        std::ofstream output{ binPath, std::ios::binary | std::ios::trunc };
        output.write(reinterpret_cast<const char*>(positions.data()), static_cast<std::streamsize>(positions.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(texCoords.data()), static_cast<std::streamsize>(texCoords.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(indices), static_cast<std::streamsize>(sizeof(indices)));
        const std::uint16_t padding = 0U;
        output.write(reinterpret_cast<const char*>(&padding), static_cast<std::streamsize>(sizeof(padding)));
    }

    const std::filesystem::path gltfPath = root / "unsupported_uv_transform.gltf";
    {
        std::ofstream output{ gltfPath, std::ios::trunc };
        output
            << "{\n"
            << "  \"asset\": { \"version\": \"2.0\" },\n"
            << "  \"scene\": 0,\n"
            << "  \"scenes\": [{ \"nodes\": [0] }],\n"
            << "  \"nodes\": [{ \"mesh\": 0 }],\n"
            << "  \"materials\": [{\n"
            << "    \"name\": \"rotated_surface\",\n"
            << "    \"pbrMetallicRoughness\": {\n"
            << "      \"baseColorTexture\": { \"index\": 0, \"extensions\": { \"KHR_texture_transform\": { \"offset\": [0.25, 0.5], \"scale\": [2.0, 3.0], \"rotation\": 0.5 } } }\n"
            << "    }\n"
            << "  }],\n"
            << "  \"textures\": [{ \"source\": 0 }],\n"
            << "  \"images\": [{ \"uri\": \"albedo.png\" }],\n"
            << "  \"extensionsUsed\": [\"KHR_texture_transform\"],\n"
            << "  \"meshes\": [{ \"primitives\": [{ \"attributes\": { \"POSITION\": 0, \"TEXCOORD_0\": 1 }, \"indices\": 2, \"material\": 0 }] }],\n"
            << "  \"buffers\": [{ \"uri\": \"mesh.bin\", \"byteLength\": 68 }],\n"
            << "  \"bufferViews\": [\n"
            << "    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 36, \"target\": 34962 },\n"
            << "    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 24, \"target\": 34962 },\n"
            << "    { \"buffer\": 0, \"byteOffset\": 60, \"byteLength\": 6, \"target\": 34963 }\n"
            << "  ],\n"
            << "  \"accessors\": [\n"
            << "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\", \"min\": [0, 0, 0], \"max\": [1, 1, 0] },\n"
            << "    { \"bufferView\": 1, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC2\" },\n"
            << "    { \"bufferView\": 2, \"componentType\": 5123, \"count\": 3, \"type\": \"SCALAR\" }\n"
            << "  ]\n"
            << "}\n";
    }

    const std::optional<RenderMeshAssetData> asset = RenderMeshAssetBuilder::LoadGltf(gltfPath);
    Require(asset.has_value(), "glTF importer rejected a mesh with unsupported texture rotation metadata");
    Require(asset->embeddedMaterials.size() == 1U, "glTF importer lost material with unsupported texture rotation metadata");
    Require(NearlyEqual(asset->embeddedMaterials[0].desc.uvTiling[0], 1.0F) && NearlyEqual(asset->embeddedMaterials[0].desc.uvTiling[1], 1.0F),
            "glTF importer applied unsupported rotated texture tiling");
    Require(NearlyEqual(asset->embeddedMaterials[0].desc.uvOffset[0], 0.0F) && NearlyEqual(asset->embeddedMaterials[0].desc.uvOffset[1], 0.0F),
            "glTF importer applied unsupported rotated texture offset");

    std::filesystem::remove_all(root, error);
}

void RunGltfImporterBlocksTraversalTextureUrisTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_gltf_texture_traversal";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "glTF traversal texture URI test could not create temp root");

    const std::filesystem::path binPath = root / "mesh.bin";
    {
        const std::vector<float> positions{
            0.0F, 0.0F, 0.0F,
            1.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F,
        };
        const std::vector<float> texCoords{
            0.0F, 0.0F,
            1.0F, 0.0F,
            0.0F, 1.0F,
        };
        const std::uint16_t indices[]{ 0U, 1U, 2U };

        std::ofstream output{ binPath, std::ios::binary | std::ios::trunc };
        output.write(reinterpret_cast<const char*>(positions.data()), static_cast<std::streamsize>(positions.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(texCoords.data()), static_cast<std::streamsize>(texCoords.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(indices), static_cast<std::streamsize>(sizeof(indices)));
        const std::uint16_t padding = 0U;
        output.write(reinterpret_cast<const char*>(&padding), static_cast<std::streamsize>(sizeof(padding)));
    }

    const std::filesystem::path gltfPath = root / "unsafe_textures.gltf";
    {
        std::ofstream output{ gltfPath, std::ios::trunc };
        output
            << "{\n"
            << "  \"asset\": { \"version\": \"2.0\" },\n"
            << "  \"scene\": 0,\n"
            << "  \"scenes\": [{ \"nodes\": [0] }],\n"
            << "  \"nodes\": [{ \"mesh\": 0 }],\n"
            << "  \"materials\": [{\n"
            << "    \"name\": \"unsafe_surface\",\n"
            << "    \"pbrMetallicRoughness\": { \"baseColorTexture\": { \"index\": 0 } },\n"
            << "    \"normalTexture\": { \"index\": 1 }\n"
            << "  }],\n"
            << "  \"textures\": [{ \"source\": 0 }, { \"source\": 1 }],\n"
            << "  \"images\": [{ \"uri\": \"../outside/albedo.png\" }, { \"uri\": \"Textures/%2e%2e/normal.png\" }],\n"
            << "  \"meshes\": [{ \"primitives\": [{ \"attributes\": { \"POSITION\": 0, \"TEXCOORD_0\": 1 }, \"indices\": 2, \"material\": 0 }] }],\n"
            << "  \"buffers\": [{ \"uri\": \"mesh.bin\", \"byteLength\": 68 }],\n"
            << "  \"bufferViews\": [\n"
            << "    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 36, \"target\": 34962 },\n"
            << "    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 24, \"target\": 34962 },\n"
            << "    { \"buffer\": 0, \"byteOffset\": 60, \"byteLength\": 6, \"target\": 34963 }\n"
            << "  ],\n"
            << "  \"accessors\": [\n"
            << "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\", \"min\": [0, 0, 0], \"max\": [1, 1, 0] },\n"
            << "    { \"bufferView\": 1, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC2\" },\n"
            << "    { \"bufferView\": 2, \"componentType\": 5123, \"count\": 3, \"type\": \"SCALAR\" }\n"
            << "  ]\n"
            << "}\n";
    }

    const std::optional<RenderMeshAssetData> asset = RenderMeshAssetBuilder::LoadGltf(gltfPath);
    Require(asset.has_value(), "glTF importer rejected a mesh instead of blocking unsafe texture URIs");
    Require(asset->embeddedMaterials.size() == 1U, "glTF importer lost material while blocking unsafe texture URIs");
    Require(asset->embeddedMaterials[0].albedoTexturePath.empty(), "glTF importer preserved path traversal albedo texture URI");
    Require(asset->embeddedMaterials[0].normalTexturePath.empty(), "glTF importer preserved percent-encoded path traversal normal texture URI");

    std::filesystem::remove_all(root, error);
}

void RunRuntimeMaterialResolverBlocksTraversalTexturePathsTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_runtime_texture_traversal";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "Materials", error);
    std::filesystem::create_directories(root / "Textures", error);
    Require(!error, "Runtime texture traversal test could not create temp root");

    {
        std::ofstream output{ root / "Materials" / "unsafe.kbmat", std::ios::trunc };
        output
            << "baseColor 1 1 1 1\n"
            << "albedoTexture ../Textures/albedo.kbtex\n";
    }
    {
        std::ofstream output{ root / "Textures" / "albedo.kbtex", std::ios::trunc };
        output << "size 1 1\nrgba8 255 255 255 255\n";
    }

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Runtime texture traversal test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderTextureAssetLoader>()), "Runtime texture traversal test could not register texture loader");
    Require(manager.Mounts().Mount("Game", root), "Runtime texture traversal test could not mount asset root");
    Require(manager.DiscoverMountedAssets() == 2U, "Runtime texture traversal test did not discover material and texture");

    const kb::assets::AssetMetadata* material = manager.Registry().FindByPath("/Game/Materials/unsafe.kbmat");
    const kb::assets::AssetMetadata* texture = manager.Registry().FindByPath("/Game/Textures/albedo.kbtex");
    Require(material != nullptr && texture != nullptr, "Runtime texture traversal test did not register material and texture metadata");

    RuntimeMaterialResolver resolver;
    const ResolvedRuntimeMaterialAsset resolved = resolver.ResolveAsset(manager, *material);
    Require(resolved.resolved && resolved.status == RuntimeMaterialResolveStatus::Resolved, "Runtime texture traversal test material did not resolve");
    Require(resolved.material.desc.albedoTextureAssetId == 0U, "Runtime resolver followed a path traversal texture reference");
    Require(resolved.material.unresolvedTexturePathCount == 1U, "Runtime resolver did not count blocked traversal texture as unresolved");

    kb::assets::AssetMetadata importedOwner{};
    importedOwner.id = kb::assets::AssetId{ 0x7100U };
    importedOwner.type = "RenderMaterial";
    importedOwner.virtualPath = "/Game/ImportedOwner.kbmat";
    importedOwner.physicalPath = root / "ImportedOwner.kbmat";
    {
        std::ofstream output{ importedOwner.physicalPath, std::ios::trunc };
        output
            << "baseColor 1 1 1 1\n"
            << "albedoTexture Textures/imported_source.21kb\n";
    }
    Require(manager.RegisterAsset(importedOwner), "Runtime texture resolver imported texture test could not register material owner");
    kb::assets::AssetMetadata importedTexture{};
    importedTexture.id = kb::assets::AssetId{ 0x7101U };
    importedTexture.type = "Texture";
    importedTexture.importCategory = "Texture";
    importedTexture.virtualPath = "/Game/Textures/imported_source.21kb";
    importedTexture.physicalPath = root / "Textures" / "albedo.kbtex";
    Require(manager.RegisterAsset(importedTexture), "Runtime texture resolver imported texture test could not register imported texture");
    const ResolvedRuntimeMaterialAsset importedResolved = resolver.ResolveAsset(manager, importedOwner);
    Require(importedResolved.resolved, "Runtime resolver imported texture owner material did not resolve");
    Require(importedResolved.status == RuntimeMaterialResolveStatus::Resolved, "Runtime resolver imported texture owner material resolved to a fallback");
    Require(importedResolved.material.unresolvedTexturePathCount == 0U, "Runtime resolver did not resolve imported Texture virtual path");
    Require(importedResolved.material.desc.albedoTextureAssetId == importedTexture.id.value,
        "Runtime resolver did not bind editor-imported Texture assets to material texture ids");

    std::filesystem::remove_all(root, error);
}

void RunGltfImporterRejectsOutOfRangeIndicesTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_invalid_gltf_indices";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Invalid glTF index test could not create temp root");

    const std::filesystem::path binPath = root / "invalid.bin";
    {
        const std::vector<float> positions{
            0.0F, 0.0F, 0.0F,
            1.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F,
        };
        const std::uint16_t indices[]{ 0U, 1U, 4U };
        std::ofstream output{ binPath, std::ios::binary | std::ios::trunc };
        output.write(reinterpret_cast<const char*>(positions.data()), static_cast<std::streamsize>(positions.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(indices), static_cast<std::streamsize>(sizeof(indices)));
        const std::uint16_t padding = 0U;
        output.write(reinterpret_cast<const char*>(&padding), static_cast<std::streamsize>(sizeof(padding)));
    }

    const std::filesystem::path gltfPath = root / "invalid.gltf";
    {
        std::ofstream output{ gltfPath, std::ios::trunc };
        output
            << "{\n"
            << "  \"asset\": { \"version\": \"2.0\" },\n"
            << "  \"scene\": 0,\n"
            << "  \"scenes\": [{ \"nodes\": [0] }],\n"
            << "  \"nodes\": [{ \"mesh\": 0 }],\n"
            << "  \"meshes\": [{ \"primitives\": [{ \"attributes\": { \"POSITION\": 0 }, \"indices\": 1 }] }],\n"
            << "  \"buffers\": [{ \"uri\": \"invalid.bin\", \"byteLength\": 44 }],\n"
            << "  \"bufferViews\": [\n"
            << "    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 36, \"target\": 34962 },\n"
            << "    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 6, \"target\": 34963 }\n"
            << "  ],\n"
            << "  \"accessors\": [\n"
            << "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\", \"min\": [0, 0, 0], \"max\": [1, 1, 0] },\n"
            << "    { \"bufferView\": 1, \"componentType\": 5123, \"count\": 3, \"type\": \"SCALAR\" }\n"
            << "  ]\n"
            << "}\n";
    }

    Require(!RenderMeshAssetBuilder::LoadGltf(gltfPath).has_value(), "glTF importer accepted an out-of-range index");
    std::filesystem::remove_all(root, error);
}

void RunGltfImporterKeepsUint32IndicesForLargeTangentMeshesTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_large_tangent_gltf";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Large tangent glTF test could not create temp root");

    constexpr std::uint32_t vertexCount = 65'538U;
    const std::filesystem::path binPath = root / "large.bin";
    {
        std::ofstream output{ binPath, std::ios::binary | std::ios::trunc };
        for (std::uint32_t index = 0U; index < vertexCount; ++index) {
            const float position[]{ static_cast<float>(index % 257U), static_cast<float>((index / 257U) % 257U), 0.0F };
            output.write(reinterpret_cast<const char*>(position), static_cast<std::streamsize>(sizeof(position)));
        }
        for (std::uint32_t index = 0U; index < vertexCount; ++index) {
            const float normal[]{ 0.0F, 0.0F, 1.0F };
            output.write(reinterpret_cast<const char*>(normal), static_cast<std::streamsize>(sizeof(normal)));
        }
        for (std::uint32_t index = 0U; index < vertexCount; ++index) {
            const float tangent[]{ 1.0F, 0.0F, 0.0F, 1.0F };
            output.write(reinterpret_cast<const char*>(tangent), static_cast<std::streamsize>(sizeof(tangent)));
        }
        for (std::uint32_t index = 0U; index < vertexCount; ++index) {
            const float texCoord[]{ 0.0F, 0.0F };
            output.write(reinterpret_cast<const char*>(texCoord), static_cast<std::streamsize>(sizeof(texCoord)));
        }
        for (std::uint32_t index = 0U; index < vertexCount; ++index) {
            output.write(reinterpret_cast<const char*>(&index), static_cast<std::streamsize>(sizeof(index)));
        }
    }

    constexpr std::uint32_t positionsOffset = 0U;
    constexpr std::uint32_t positionsBytes = vertexCount * 3U * 4U;
    constexpr std::uint32_t normalsOffset = positionsOffset + positionsBytes;
    constexpr std::uint32_t normalsBytes = vertexCount * 3U * 4U;
    constexpr std::uint32_t tangentsOffset = normalsOffset + normalsBytes;
    constexpr std::uint32_t tangentsBytes = vertexCount * 4U * 4U;
    constexpr std::uint32_t texCoordsOffset = tangentsOffset + tangentsBytes;
    constexpr std::uint32_t texCoordsBytes = vertexCount * 2U * 4U;
    constexpr std::uint32_t indicesOffset = texCoordsOffset + texCoordsBytes;
    constexpr std::uint32_t indicesBytes = vertexCount * 4U;
    constexpr std::uint32_t totalBytes = indicesOffset + indicesBytes;

    const std::filesystem::path gltfPath = root / "large.gltf";
    {
        std::ofstream output{ gltfPath, std::ios::trunc };
        output
            << "{\n"
            << "  \"asset\": { \"version\": \"2.0\" },\n"
            << "  \"scene\": 0,\n"
            << "  \"scenes\": [{ \"nodes\": [0] }],\n"
            << "  \"nodes\": [{ \"mesh\": 0 }],\n"
            << "  \"meshes\": [{ \"primitives\": [{ \"attributes\": { \"POSITION\": 0, \"NORMAL\": 1, \"TANGENT\": 2, \"TEXCOORD_0\": 3 }, \"indices\": 4 }] }],\n"
            << "  \"buffers\": [{ \"uri\": \"large.bin\", \"byteLength\": " << totalBytes << " }],\n"
            << "  \"bufferViews\": [\n"
            << "    { \"buffer\": 0, \"byteOffset\": " << positionsOffset << ", \"byteLength\": " << positionsBytes << ", \"target\": 34962 },\n"
            << "    { \"buffer\": 0, \"byteOffset\": " << normalsOffset << ", \"byteLength\": " << normalsBytes << ", \"target\": 34962 },\n"
            << "    { \"buffer\": 0, \"byteOffset\": " << tangentsOffset << ", \"byteLength\": " << tangentsBytes << ", \"target\": 34962 },\n"
            << "    { \"buffer\": 0, \"byteOffset\": " << texCoordsOffset << ", \"byteLength\": " << texCoordsBytes << ", \"target\": 34962 },\n"
            << "    { \"buffer\": 0, \"byteOffset\": " << indicesOffset << ", \"byteLength\": " << indicesBytes << ", \"target\": 34963 }\n"
            << "  ],\n"
            << "  \"accessors\": [\n"
            << "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": " << vertexCount << ", \"type\": \"VEC3\" },\n"
            << "    { \"bufferView\": 1, \"componentType\": 5126, \"count\": " << vertexCount << ", \"type\": \"VEC3\" },\n"
            << "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": " << vertexCount << ", \"type\": \"VEC4\" },\n"
            << "    { \"bufferView\": 3, \"componentType\": 5126, \"count\": " << vertexCount << ", \"type\": \"VEC2\" },\n"
            << "    { \"bufferView\": 4, \"componentType\": 5125, \"count\": " << vertexCount << ", \"type\": \"SCALAR\" }\n"
            << "  ]\n"
            << "}\n";
    }

    const std::optional<RenderMeshAssetData> asset = RenderMeshAssetBuilder::LoadGltf(gltfPath);
    Require(asset.has_value(), "glTF importer failed to load a large tangent mesh");
    Require(asset->desc.vertexFormat == RenderVertexFormat::P3N3T4UV2, "Large tangent glTF did not preserve tangent vertex format");
    Require(asset->desc.indexFormat == RenderIndexFormat::Uint32, "Large tangent glTF was incorrectly compacted to uint16 indices");
    Require(asset->desc.indices32 != nullptr && asset->desc.indices == nullptr, "Large tangent glTF exposed the wrong index pointer");

    std::filesystem::remove_all(root, error);
}

void RunRenderMaterialAssetLoaderDiscoversAndLoadsMaterialThroughAssetManagerTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_material_asset_loader";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Render material asset loader test could not create temp root");

    const std::filesystem::path materialPath = root / "paint.kbmat";
    {
        std::ofstream output{ materialPath, std::ios::trunc };
        output
            << "# KB material\n"
            << "version 1\n"
            << "materialType builtin.pbr\n"
            << "materialTypeVersion 1\n"
            << "baseColor 0.2 0.4 0.8 1.0\n"
            << "emissiveColor 0.1 0.2 0.3\n"
            << "metallicFactor 0.6\n"
            << "roughnessFactor 0.35\n"
            << "normalScale 0.75\n"
            << "occlusionStrength 0.55\n"
            << "emissiveStrength 2.5\n"
            << "alphaCutoff 0.4\n"
            << "clearcoatFactor 0.8\n"
            << "clearcoatRoughnessFactor 0.2\n"
            << "sheenColor 0.9 0.8 0.7\n"
            << "sheenRoughnessFactor 0.45\n"
            << "transmissionFactor 0.3\n"
            << "thicknessFactor 0.12\n"
            << "attenuationColor 0.6 0.7 0.8\n"
            << "attenuationDistance 15.0\n"
            << "subsurfaceColor 0.5 0.4 0.3\n"
            << "subsurfaceFactor 0.25\n"
            << "anisotropyStrength 0.5\n"
            << "anisotropyRotation 0.125\n"
            << "decalBlendMode PBR\n"
            << "layerBlendMode MULTIPLY\n"
            << "alphaMode MASK\n"
            << "doubleSided true\n"
            << "albedoTextureAssetId 77\n"
            << "normalTextureAssetId 78\n"
            << "metallicRoughnessTextureAssetId 79\n"
            << "occlusionTextureAssetId 80\n"
            << "emissiveTextureAssetId 81\n"
            << "clearcoatTextureAssetId 82\n"
            << "transmissionTextureAssetId 83\n"
            << "layerMaskTextureAssetId 84\n"
            << "baseColorTexture Textures/albedo.kbtex\n"
            << "normalTexture Textures/normal.kbtex\n"
            << "metallicRoughnessTexture Textures/mr.kbtex\n"
            << "occlusionTexture Textures/ao.kbtex\n"
            << "emissiveTexture Textures/emissive.kbtex\n"
            << "clearcoatTexture Textures/clearcoat.kbtex\n"
            << "transmissionTexture Textures/transmission.kbtex\n"
            << "layerMaskTexture Textures/layer-mask.kbtex\n";
    }

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "AssetManager rejected RenderMaterialAssetLoader");
    Require(manager.Mounts().Mount("Game", root), "AssetManager could not mount material asset test root");
    Require(manager.DiscoverMountedAssets() == 1U, "AssetManager did not discover the material asset");

    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath("/Game/paint.kbmat");
    Require(metadata != nullptr && metadata->type == "RenderMaterial", "AssetManager registered the material with the wrong type");
    const kb::assets::AssetHandle<RenderMaterialAssetData> asset = manager.Load<RenderMaterialAssetData>(metadata->id);
    Require(asset.IsLoaded(), "AssetManager did not load RenderMaterialAssetData through RenderMaterialAssetLoader");
    Require(asset->documentVersion == kRenderMaterialAssetDocumentVersion, "Loaded material did not preserve document version");
    Require(asset->hasExplicitDocumentVersion, "Loaded material did not record explicit document version metadata");
    Require(asset->materialType == kRenderMaterialAssetBuiltInPbrType, "Loaded material did not preserve material type");
    Require(asset->materialTypeVersion == kRenderMaterialAssetBuiltInPbrTypeVersion, "Loaded material did not preserve material type version");
    Require(asset->hasExplicitMaterialType, "Loaded material did not record explicit material type metadata");
    Require(asset->hasExplicitMaterialTypeVersion, "Loaded material did not record explicit material type version metadata");
    Require(NearlyEqual(asset->desc.baseColor[0], 0.2F), "Loaded material did not preserve baseColor red");
    Require(NearlyEqual(asset->desc.baseColor[1], 0.4F), "Loaded material did not preserve baseColor green");
    Require(NearlyEqual(asset->desc.baseColor[2], 0.8F), "Loaded material did not preserve baseColor blue");
    Require(NearlyEqual(asset->desc.baseColor[3], 1.0F), "Loaded material did not preserve baseColor alpha");
    Require(NearlyEqual(asset->desc.emissiveColor[0], 0.1F), "Loaded material did not preserve emissive color red");
    Require(NearlyEqual(asset->desc.emissiveColor[1], 0.2F), "Loaded material did not preserve emissive color green");
    Require(NearlyEqual(asset->desc.emissiveColor[2], 0.3F), "Loaded material did not preserve emissive color blue");
    Require(NearlyEqual(asset->desc.metallicFactor, 0.6F), "Loaded material did not preserve metallic factor");
    Require(NearlyEqual(asset->desc.roughnessFactor, 0.35F), "Loaded material did not preserve roughness factor");
    Require(NearlyEqual(asset->desc.normalScale, 0.75F), "Loaded material did not preserve normal scale");
    Require(NearlyEqual(asset->desc.occlusionStrength, 0.55F), "Loaded material did not preserve occlusion strength");
    Require(NearlyEqual(asset->desc.emissiveStrength, 2.5F), "Loaded material did not preserve emissive strength");
    Require(NearlyEqual(asset->desc.alphaCutoff, 0.4F), "Loaded material did not preserve alpha cutoff");
    Require(NearlyEqual(asset->desc.clearcoatFactor, 0.8F), "Loaded material did not preserve clearcoat factor");
    Require(NearlyEqual(asset->desc.clearcoatRoughnessFactor, 0.2F), "Loaded material did not preserve clearcoat roughness");
    Require(NearlyEqual(asset->desc.sheenColor[1], 0.8F), "Loaded material did not preserve sheen color");
    Require(NearlyEqual(asset->desc.sheenRoughnessFactor, 0.45F), "Loaded material did not preserve sheen roughness");
    Require(NearlyEqual(asset->desc.transmissionFactor, 0.3F), "Loaded material did not preserve transmission factor");
    Require(NearlyEqual(asset->desc.thicknessFactor, 0.12F), "Loaded material did not preserve thickness factor");
    Require(NearlyEqual(asset->desc.attenuationColor[2], 0.8F), "Loaded material did not preserve attenuation color");
    Require(NearlyEqual(asset->desc.attenuationDistance, 15.0F), "Loaded material did not preserve attenuation distance");
    Require(NearlyEqual(asset->desc.subsurfaceColor[0], 0.5F), "Loaded material did not preserve subsurface color");
    Require(NearlyEqual(asset->desc.subsurfaceFactor, 0.25F), "Loaded material did not preserve subsurface factor");
    Require(NearlyEqual(asset->desc.anisotropyStrength, 0.5F), "Loaded material did not preserve anisotropy strength");
    Require(NearlyEqual(asset->desc.anisotropyRotation, 0.125F), "Loaded material did not preserve anisotropy rotation");
    Require(asset->desc.decalBlendMode == RenderMaterialDecalBlendMode::Pbr, "Loaded material did not preserve decal blend mode");
    Require(asset->desc.layerBlendMode == RenderMaterialLayerBlendMode::Multiply, "Loaded material did not preserve layer blend mode");
    Require(asset->desc.alphaMode == RenderMaterialAlphaMode::Mask, "Loaded material did not preserve alpha mode");
    Require(asset->desc.doubleSided, "Loaded material did not preserve double sided state");
    Require(asset->desc.albedoTextureAssetId == 77U, "Loaded material did not preserve albedo texture asset id");
    Require(asset->desc.normalTextureAssetId == 78U, "Loaded material did not preserve normal texture asset id");
    Require(asset->desc.metallicRoughnessTextureAssetId == 79U, "Loaded material did not preserve metallic-roughness texture asset id");
    Require(asset->desc.occlusionTextureAssetId == 80U, "Loaded material did not preserve occlusion texture asset id");
    Require(asset->desc.emissiveTextureAssetId == 81U, "Loaded material did not preserve emissive texture asset id");
    Require(asset->desc.clearcoatTextureAssetId == 82U, "Loaded material did not preserve clearcoat texture asset id");
    Require(asset->desc.transmissionTextureAssetId == 83U, "Loaded material did not preserve transmission texture asset id");
    Require(asset->desc.layerMaskTextureAssetId == 84U, "Loaded material did not preserve layer mask texture asset id");
    Require(asset->albedoTexturePath == "Textures/albedo.kbtex", "Loaded material did not preserve albedo texture path");
    Require(asset->normalTexturePath == "Textures/normal.kbtex", "Loaded material did not preserve normal texture path");
    Require(asset->metallicRoughnessTexturePath == "Textures/mr.kbtex", "Loaded material did not preserve metallic-roughness texture path");
    Require(asset->occlusionTexturePath == "Textures/ao.kbtex", "Loaded material did not preserve occlusion texture path");
    Require(asset->emissiveTexturePath == "Textures/emissive.kbtex", "Loaded material did not preserve emissive texture path");
    Require(asset->clearcoatTexturePath == "Textures/clearcoat.kbtex", "Loaded material did not preserve clearcoat texture path");
    Require(asset->transmissionTexturePath == "Textures/transmission.kbtex", "Loaded material did not preserve transmission texture path");
    Require(asset->layerMaskTexturePath == "Textures/layer-mask.kbtex", "Loaded material did not preserve layer mask texture path");

    std::filesystem::remove_all(root, error);
}

void RunMaterialAssetDiscoveryBuildsMaterialDependencyGraphTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_material_dependency_graph";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "Textures", error);
    Require(!error, "Material dependency graph test could not create temp root");

    {
        std::ofstream output{ root / "Textures" / "albedo.kbtex", std::ios::trunc };
        output << "size 1 1\nrgba8 255 255 255 255\n";
    }
    {
        std::ofstream output{ root / "Textures" / "normal.kbtex", std::ios::trunc };
        output << "size 1 1\nrgba8 128 128 255 255\n";
    }
    {
        std::ofstream output{ root / "Textures" / "graph_mask.kbtex", std::ios::trunc };
        output << "size 1 1\nrgba8 32 64 128 255\n";
    }

    RenderMaterialAssetData parent{};
    parent.materialType = kRenderMaterialAssetBuiltInPbrType;
    parent.materialTypeVersion = kRenderMaterialAssetBuiltInPbrTypeVersion;
    parent.hasExplicitMaterialType = true;
    parent.hasExplicitMaterialTypeVersion = true;
    parent.albedoTexturePath = "Textures/albedo.kbtex";
    Require(RenderMaterialAssetWriter::Save(root / "parent.kbmat", parent), "Material dependency graph test could not write parent material");

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Material dependency graph test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialInstanceAssetLoader>()), "Material dependency graph test could not register material instance loader");
    Require(manager.RegisterLoader(std::make_unique<RenderTextureAssetLoader>()), "Material dependency graph test could not register texture loader");
    Require(manager.Mounts().Mount("Game", root), "Material dependency graph test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 3U, "Material dependency graph test did not discover parent material and textures");

    const kb::assets::AssetMetadata* parentMetadata = manager.Registry().FindByPath("/Game/parent.kbmat");
    const kb::assets::AssetMetadata* albedoMetadata = manager.Registry().FindByPath("/Game/Textures/albedo.kbtex");
    const kb::assets::AssetMetadata* normalMetadata = manager.Registry().FindByPath("/Game/Textures/normal.kbtex");
    Require(parentMetadata != nullptr && parentMetadata->type == "RenderMaterial", "Material dependency graph test did not discover parent material");
    Require(albedoMetadata != nullptr && albedoMetadata->type == "RenderTexture", "Material dependency graph test did not discover albedo texture");
    Require(normalMetadata != nullptr && normalMetadata->type == "RenderTexture", "Material dependency graph test did not discover normal texture");

    RenderMaterialInstanceAssetData instance{};
    instance.parentMaterialAssetId = parentMetadata->id;
    instance.hasOverrides = true;
    instance.overrides.materialType = kRenderMaterialAssetBuiltInPbrType;
    instance.overrides.materialTypeVersion = kRenderMaterialAssetBuiltInPbrTypeVersion;
    instance.overrides.hasExplicitMaterialType = true;
    instance.overrides.hasExplicitMaterialTypeVersion = true;
    instance.overrides.normalTexturePath = "Textures/normal.kbtex";
    Require(RenderMaterialInstanceAssetWriter::Save(root / "parent_instance.kbmatinst", instance), "Material dependency graph test could not write material instance");
    Require(manager.DiscoverMountedAssets() >= 4U, "Material dependency graph test did not rediscover material instance");

    parentMetadata = manager.Registry().FindByPath("/Game/parent.kbmat");
    const kb::assets::AssetMetadata* instanceMetadata = manager.Registry().FindByPath("/Game/parent_instance.kbmatinst");
    albedoMetadata = manager.Registry().FindByPath("/Game/Textures/albedo.kbtex");
    normalMetadata = manager.Registry().FindByPath("/Game/Textures/normal.kbtex");
    Require(parentMetadata != nullptr && instanceMetadata != nullptr && albedoMetadata != nullptr && normalMetadata != nullptr, "Material dependency graph test lost discovered assets");

    const kb::assets::AssetId materialTypeDependency = kb::assets::MakeAssetId("MaterialType:builtin.pbr:1");
    Require(!ContainsDependency(parentMetadata->dependencies, materialTypeDependency),
        "Built-in PBR was emitted as a dependency even though it is code, not a registered asset");
    Require(ContainsDependency(parentMetadata->dependencies, albedoMetadata->id), "Material dependency graph did not link material to texture path dependency");
    Require(ContainsDependency(instanceMetadata->dependencies, parentMetadata->id), "Material dependency graph did not link instance to parent material");
    Require(!ContainsDependency(instanceMetadata->dependencies, materialTypeDependency),
        "Material instance emitted the built-in PBR code type as an unresolved asset dependency");
    Require(ContainsDependency(instanceMetadata->dependencies, normalMetadata->id), "Material dependency graph did not link instance override to texture dependency");

    std::filesystem::remove_all(root, error);
}

void RunMaterialGraphAndTypeAssetDiscoveryTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_material_graph_type_assets";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "MaterialTypes", error);
    std::filesystem::create_directories(root / "Graphs", error);
    std::filesystem::create_directories(root / "Functions", error);
    std::filesystem::create_directories(root / "Collections", error);
    Require(!error, "KBMAT-GRAPH-0005: Material graph/type asset test could not create temp root");

    const kb::assets::AssetId functionId = kb::assets::MakeAssetId(
        "/Game/Functions/Tint.kbmatfn:" + std::string{ kRenderMaterialFunctionAssetType });
    const kb::assets::AssetId collectionId = kb::assets::MakeAssetId(
        "/Game/Collections/Globals.kbmpc:" + std::string{ kRenderMaterialParameterCollectionAssetType });

    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.storageModel = "material-graph-asset";
    graph.lastGoodArtifact.assetId = 0xA771U;
    graph.lastGoodArtifact.contentHash = 0xBEEFU;
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::MaterialFunctionCall,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = std::to_string(functionId.value) },
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::CollectionParameter,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "GlobalTint",
            .defaultValueHint = std::to_string(collectionId.value),
        },
    });
    Require(RenderMaterialGraphAssetLoader::SaveGraph(root / "Graphs" / "Surface.kbmaterialgraph", graph),
        "KBMAT-GRAPH-0005: Material Graph asset writer failed");

    RenderMaterialGraphDocument functionGraph{};
    functionGraph.storageModel = "material-function-asset";
    functionGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 1U,
        .kind = RenderMaterialGraphNodeKind::FunctionOutput,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "Output",
            .defaultValueHint = "float4",
        },
    });
    functionGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::CollectionParameter,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "GlobalTint",
            .defaultValueHint = std::to_string(collectionId.value),
        },
    });
    Require(RenderMaterialFunctionAssetLoader::SaveFunction(
            root / "Functions" / "Tint.kbmatfn",
            RenderMaterialFunctionAssetData{ .graph = functionGraph }),
        "P1.15: Material Function dependency fixture failed to save");
    RenderMaterialParameterCollectionData collection{};
    collection.displayName = "Globals";
    collection.parameters.push_back(RenderMaterialParameterCollectionParameter{
        .stableId = "GlobalTint",
        .displayName = "Global Tint",
        .type = RenderMaterialParameterCollectionValueType::Vector,
        .defaultValue = { 1.0F, 1.0F, 1.0F, 1.0F },
    });
    Require(RenderMaterialParameterCollectionWriter::Save(root / "Collections" / "Globals.kbmpc", collection),
        "P1.15: Material Parameter Collection dependency fixture failed to save");

    RenderMaterialTypeDocument type = GetBuiltInPbrMaterialTypeDocument();
    type.stableTypeId = "graph.surface";
    type.displayName = "Graph Surface";
    type.schema.typeName = type.stableTypeId;
    Require(RenderMaterialTypeAssetLoader::SaveType(root / "MaterialTypes" / "GraphSurface.kbmaterialtype", type),
        "KBMAT-GRAPH-0005: Material Type asset writer failed");

    RenderMaterialAssetData material{};
    material.materialType = "graph.surface";
    material.materialTypeVersion = 1U;
    material.hasExplicitMaterialType = true;
    material.hasExplicitMaterialTypeVersion = true;
    material.materialTypeAssetPath = "/Game/MaterialTypes/GraphSurface.kbmaterialtype";
    material.graphSourceAssetPath = "/Game/Graphs/Surface.kbmaterialgraph";
    material.graph.lastGoodArtifact.assetId = 0xDEADBEEFU;
    Require(RenderMaterialAssetWriter::Save(root / "GraphBacked.kbmat", material),
        "KBMAT-GRAPH-0005: Graph-backed material writer failed");

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "KBMAT-GRAPH-0005: Could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialGraphAssetLoader>()), "KBMAT-GRAPH-0005: Could not register material graph loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialFunctionAssetLoader>()), "P1.15: Could not register material function loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialParameterCollectionAssetLoader>()), "P1.15: Could not register parameter collection loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialTypeAssetLoader>()), "KBMAT-GRAPH-0005: Could not register material type loader");
    Require(manager.Mounts().Mount("Game", root), "KBMAT-GRAPH-0005: Could not mount graph/type asset root");
    Require(manager.DiscoverMountedAssets() >= 5U, "KBMAT-GRAPH-0005: Asset discovery missed material graph/type/material dependency files");

    const kb::assets::AssetMetadata* graphMetadata = manager.Registry().FindByPath("/Game/Graphs/Surface.kbmaterialgraph");
    const kb::assets::AssetMetadata* typeMetadata = manager.Registry().FindByPath("/Game/MaterialTypes/GraphSurface.kbmaterialtype");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/GraphBacked.kbmat");
    const kb::assets::AssetMetadata* functionMetadata = manager.Registry().FindByPath("/Game/Functions/Tint.kbmatfn");
    const kb::assets::AssetMetadata* collectionMetadata = manager.Registry().FindByPath("/Game/Collections/Globals.kbmpc");
    Require(graphMetadata != nullptr && graphMetadata->type == kRenderMaterialGraphAssetType,
        "KBMAT-GRAPH-0005: Material Graph metadata was not discovered");
    Require(typeMetadata != nullptr && typeMetadata->type == kRenderMaterialTypeAssetType,
        "KBMAT-GRAPH-0005: Material Type metadata was not discovered");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial",
        "KBMAT-GRAPH-0005: Graph-backed material metadata was not discovered");
    Require(functionMetadata != nullptr && functionMetadata->id == functionId &&
            collectionMetadata != nullptr && collectionMetadata->id == collectionId,
        "P1.15: Function/MPC dependency metadata was not discovered with stable identities");

    const kb::assets::AssetHandle<RenderMaterialGraphDocument> graphHandle =
        manager.Load<RenderMaterialGraphDocument>(graphMetadata->id);
    const kb::assets::AssetHandle<RenderMaterialTypeDocument> typeHandle =
        manager.Load<RenderMaterialTypeDocument>(typeMetadata->id);
    const kb::assets::AssetHandle<RenderMaterialAssetData> materialHandle =
        manager.Load<RenderMaterialAssetData>(materialMetadata->id);
    Require(graphHandle.IsLoaded() && graphHandle->storageModel == "material-graph-asset",
        "KBMAT-GRAPH-0005: Material Graph asset should be runtime loadable");
    Require(typeHandle.IsLoaded() && typeHandle->stableTypeId == "graph.surface" && !typeHandle->schema.parameters.empty(),
        "KBMAT-GRAPH-0005: Material Type asset should be runtime loadable with schema");
    Require(materialHandle.IsLoaded(),
        "KBMAT-GRAPH-0005: Graph-backed material should be runtime loadable");
    const RenderMaterialSourceGraphResolveResult sourceGraph =
        ResolveRenderMaterialSourceGraph(manager, *materialMetadata, *materialHandle);
    Require(sourceGraph.graph.has_value() && sourceGraph.external &&
            sourceGraph.graph->storageModel == "material-graph-asset",
        "The authoritative material sourceGraph resolver used the stale inline snapshot");
    RenderMaterialAssetData missingLooseReference = *materialHandle;
    missingLooseReference.graphSourceAssetId = 0U;
    missingLooseReference.graphSourceAssetPath = "/Game/Graphs/Missing.kbmaterialgraph";
    const RuntimeMaterialSourceGraphLoadResult looseFallback =
        LoadRuntimeMaterialSourceGraph(manager, *materialMetadata, missingLooseReference);
    Require(looseFallback.graph.has_value() && looseFallback.usedInlineFallback &&
            looseFallback.parseResult.HasWarnings() && !looseFallback.parseResult.HasErrors(),
        "The loose runtime lost its explicit inline editor-snapshot fallback diagnostic");
    const RenderMaterialCookPayload inlinePayload =
        RenderMaterialCookPayloadBuilder::Build(
            *materialHandle, *materialMetadata, manager.Registry());
    RenderMaterialAssetData authoritativeMaterial = *materialHandle;
    authoritativeMaterial.graph = *sourceGraph.graph;
    const RenderMaterialCookPayload authoritativePayload =
        RenderMaterialCookPayloadBuilder::Build(
            authoritativeMaterial, *materialMetadata, manager.Registry());
    Require(!inlinePayload.graphBacked && authoritativePayload.graphBacked &&
            authoritativePayload.graphCompileKey.graphContentHash != 0U &&
            authoritativePayload.payloadHash != inlinePayload.payloadHash,
        "The cook payload did not switch from inline snapshot A to authoritative sourceGraph B");
    Require(ContainsDependency(graphMetadata->dependencies, kb::assets::AssetId{ 0xA771U }),
        "KBMAT-GRAPH-0005: Material Graph dependency discovery should include last-good artifact asset id");
    Require(ContainsDependency(graphMetadata->dependencies, functionId) &&
            ContainsDependency(graphMetadata->dependencies, collectionId),
        "P1.15: standalone Material Graph dependency discovery must include Function and MPC assets");
    Require(ContainsDependency(functionMetadata->dependencies, collectionId),
        "P1.15: Material Function dependency discovery must include nested MPC assets");
    Require(ContainsDependency(materialMetadata->dependencies, typeMetadata->id),
        "KBMAT-GRAPH-0005: .kbmat dependency discovery should link to referenced Material Type asset");
    Require(ContainsDependency(materialMetadata->dependencies, graphMetadata->id),
        "KBMAT-GRAPH-0304: .kbmat dependency discovery should link to source Material Graph asset");
    Require(!ContainsDependency(materialMetadata->dependencies, kb::assets::AssetId{ 0xDEADBEEFU }),
        "A material with an external sourceGraph retained a stale inline-graph dependency");

    std::filesystem::remove_all(root, error);
}

void RunPackagedMaterialDependencyMemoryTest() {
    namespace bake = kb::assets::bake;

    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    const std::filesystem::path packPath =
        std::filesystem::temp_directory_path() / "21kb_renderer_packaged_material_dependencies.kbpack";
    std::error_code removeError;
    std::filesystem::remove(packPath, removeError);

    const std::string materialPath = "/Game/Materials/Packaged.kbmat";
    const std::string graphPath = "/Game/Materials/Packaged.kbmaterialgraph";
    const std::string functionPath = "/Game/Materials/Tint.kbmatfn";
    const std::string collectionPath = "/Game/Materials/Globals.kbmpc";
    const std::string typePath = "/Game/Materials/Packaged.kbmaterialtype";
    const std::string scenePath = "/Game/Scenes/PackagedMaterial.21kbscene";
    const kb::assets::AssetId materialId = kb::assets::MakeAssetId(materialPath + ":RenderMaterial");
    const kb::assets::AssetId graphId = kb::assets::MakeAssetId(graphPath + ":" + kRenderMaterialGraphAssetType);
    const kb::assets::AssetId functionId = kb::assets::MakeAssetId(functionPath + ":" + kRenderMaterialFunctionAssetType);
    const kb::assets::AssetId collectionId = kb::assets::MakeAssetId(collectionPath + ":" + kRenderMaterialParameterCollectionAssetType);
    const kb::assets::AssetId typeId = kb::assets::MakeAssetId(typePath + ":" + kRenderMaterialTypeAssetType);

    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.storageModel = "material-graph-asset";
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::MaterialFunctionCall,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = std::to_string(functionId.value) },
    });
    RenderMaterialGraphDocument functionGraph{};
    functionGraph.storageModel = "material-function-asset";
    functionGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 1U,
        .kind = RenderMaterialGraphNodeKind::FunctionOutput,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "Output",
            .defaultValueHint = "float4",
        },
    });
    functionGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::CollectionParameter,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "GlobalTint",
            .defaultValueHint = std::to_string(collectionId.value),
        },
    });

    RenderMaterialParameterCollectionData collection{};
    collection.displayName = "Packaged Globals";
    collection.parameters.push_back(RenderMaterialParameterCollectionParameter{
        .stableId = "GlobalTint",
        .displayName = "Global Tint",
        .type = RenderMaterialParameterCollectionValueType::Vector,
        .defaultValue = { 0.25F, 0.50F, 0.75F, 1.0F },
    });

    RenderMaterialTypeDocument type = GetBuiltInPbrMaterialTypeDocument();
    type.stableTypeId = "packaged.surface";
    type.displayName = "Packaged Surface";
    type.schema.typeName = type.stableTypeId;

    RenderMaterialAssetData material{};
    material.materialType = type.stableTypeId;
    material.materialTypeVersion = type.version;
    material.hasExplicitMaterialType = true;
    material.hasExplicitMaterialTypeVersion = true;
    material.materialTypeAssetId = typeId.value;
    material.materialTypeAssetPath = typePath;
    material.graphSourceAssetId = graphId.value;
    material.graphSourceAssetPath = graphPath;

    const std::vector<std::uint8_t> materialBytes = MaterialDocumentBytes(
        [&material](std::ostream& output) { RenderMaterialAssetWriter::Write(output, material); });
    const std::vector<std::uint8_t> graphBytes = MaterialDocumentBytes(
        [&graph](std::ostream& output) { WriteRenderMaterialGraphDocument(output, graph); });
    const std::vector<std::uint8_t> functionBytes = MaterialDocumentBytes(
        [&functionGraph](std::ostream& output) { WriteRenderMaterialGraphDocument(output, functionGraph); });
    const std::vector<std::uint8_t> collectionBytes = MaterialDocumentBytes(
        [&collection](std::ostream& output) { RenderMaterialParameterCollectionWriter::Write(output, collection); });
    const std::vector<std::uint8_t> typeBytes = MaterialDocumentBytes(
        [&type](std::ostream& output) { WriteRenderMaterialTypeDocument(output, type); });

    bake::RuntimeAssetManifest manifest{
        .targetProfileId = std::string{ profile.identifier },
        .targetProfileHash = bake::BakeTargetProfileFingerprint(profile),
    };
    manifest.descriptor.targetPlatforms = { "Windows" };
    manifest.settings.name = "PackagedMaterialDependencies";
    manifest.settings.defaultMap = scenePath;

    bake::AssetPackWriter writer{ packPath, profile };
    const auto appendSource = [&](kb::assets::AssetId id,
                                  std::string typeName,
                                  std::string path,
                                  std::string extension,
                                  const std::vector<std::uint8_t>& bytes,
                                  std::vector<kb::assets::AssetId> dependencies) {
        const bake::AssetBakeDigest digest = StoreRuntimePackSource(writer, profile, bytes, path);
        manifest.assets.push_back(bake::RuntimeAssetManifestEntry{
            .id = id,
            .type = std::move(typeName),
            .name = std::filesystem::path{ path }.stem().string(),
            .virtualPath = std::move(path),
            .sourceExtension = std::move(extension),
            .contentHash = bake::HashBakeBytes(bytes),
            .dependencies = std::move(dependencies),
            .artifacts = { bake::RuntimeArtifactReference{
                .digest = digest,
                .encoding = bake::RuntimeArtifactEncoding::SourceBytes,
            } },
        });
    };
    appendSource(materialId, "RenderMaterial", materialPath, ".kbmat", materialBytes, { graphId, typeId });
    appendSource(graphId, kRenderMaterialGraphAssetType, graphPath, kRenderMaterialGraphAssetExtension,
        graphBytes, { functionId });
    appendSource(functionId, kRenderMaterialFunctionAssetType, functionPath, kRenderMaterialFunctionAssetExtension,
        functionBytes, { collectionId });
    appendSource(collectionId, kRenderMaterialParameterCollectionAssetType, collectionPath,
        kRenderMaterialParameterCollectionAssetExtension, collectionBytes, {});
    appendSource(typeId, kRenderMaterialTypeAssetType, typePath, kRenderMaterialTypeAssetExtension, typeBytes, {});
    const std::filesystem::path authoredScenePath =
        std::filesystem::temp_directory_path() / "21kb_renderer_packaged_material_scene.21kbscene";
    kb::scene::Scene authoredScene;
    Require(kb::scene::SceneDocumentService::Save(authoredScene, authoredScenePath, "PackagedMaterial"),
        "Packaged material fixture could not write its runtime Scene");
    std::ifstream authoredSceneInput{ authoredScenePath, std::ios::binary };
    const std::vector<std::uint8_t> sceneBytes{
        std::istreambuf_iterator<char>{ authoredSceneInput }, std::istreambuf_iterator<char>{} };
    appendSource(
        kb::assets::MakeAssetId(scenePath + ":Scene"),
        "Scene",
        scenePath,
        ".21kbscene",
        sceneBytes,
        {});
    std::filesystem::remove(authoredScenePath, removeError);

    std::vector<std::uint8_t> manifestBytes;
    Require(bake::EncodeRuntimeAssetManifest(manifest, manifestBytes) == bake::RuntimeAssetManifestStatus::Success,
        "Packaged material manifest could not be encoded");
    static_cast<void>(StoreRuntimePackArtifact(
        writer, profile, manifestBytes, bake::kRuntimeManifestAssetTypeId, "runtime-manifest"));
    Require(writer.Finish() == bake::BakedAssetSinkStatus::Success,
        "Packaged material pack could not be published");

    auto runtimePack = std::make_shared<bake::RuntimeAssetPack>();
    Require(runtimePack->Mount(packPath, profile) == bake::RuntimeAssetPackStatus::Success,
        "Packaged material pack could not be mounted");
    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()) &&
            manager.RegisterLoader(std::make_unique<RenderMaterialGraphAssetLoader>()) &&
            manager.RegisterLoader(std::make_unique<RenderMaterialFunctionAssetLoader>()) &&
            manager.RegisterLoader(std::make_unique<RenderMaterialParameterCollectionAssetLoader>()) &&
            manager.RegisterLoader(std::make_unique<RenderMaterialTypeAssetLoader>()) &&
            manager.MountRuntimePack(runtimePack),
        "Packaged material runtime could not register loaders and mount its pathless pack");
    Require(manager.Mounts().Resolve(graphPath).has_value() == false,
        "Packaged material fixture unexpectedly exposed a loose filesystem path");

    const kb::assets::AssetHandle<RenderMaterialAssetData> loadedMaterial =
        manager.Load<RenderMaterialAssetData>(materialId);
    Require(loadedMaterial.IsLoaded(), "Packaged base material did not load from source bytes");
    const RuntimeMaterialSourceGraphLoadResult loadedGraph =
        LoadRuntimeMaterialSourceGraph(manager, *manager.Registry().Find(materialId), *loadedMaterial);
    Require(loadedGraph.graph.has_value() && loadedGraph.graph->storageModel == "material-graph-asset",
        "Packaged material source graph bypassed the memory loader");

    RenderMaterialAssetData relativeReference = *loadedMaterial;
    relativeReference.graphSourceAssetId = 0U;
    relativeReference.graphSourceAssetPath = "Packaged.kbmaterialgraph";
    const RuntimeMaterialSourceGraphLoadResult relativeGraph =
        LoadRuntimeMaterialSourceGraph(
            manager, *manager.Registry().Find(materialId), relativeReference);
    Require(relativeGraph.graph.has_value() &&
            relativeGraph.graph->storageModel == "material-graph-asset",
        "A relative sourceGraph path was resolved differently from its packaged material owner");

    RenderMaterialAssetData missingReference = *loadedMaterial;
    missingReference.graphSourceAssetId = 0U;
    missingReference.graphSourceAssetPath = "Missing.kbmaterialgraph";
    const RenderMaterialSourceGraphResolveResult missingGraph =
        ResolveRenderMaterialSourceGraph(
            manager, *manager.Registry().Find(materialId), missingReference);
    Require(!missingGraph.graph.has_value() && missingGraph.parseResult.HasErrors(),
        "The authoritative sourceGraph resolver accepted a missing external graph");
    const RuntimeMaterialSourceGraphLoadResult packagedMissingGraph =
        LoadRuntimeMaterialSourceGraph(
            manager, *manager.Registry().Find(materialId), missingReference);
    Require(!packagedMissingGraph.graph.has_value() && !packagedMissingGraph.usedInlineFallback &&
            packagedMissingGraph.parseResult.HasErrors(),
        "The packaged runtime accepted an inline editor snapshot after sourceGraph resolution failed");

    const RuntimeMaterialFunctionLibraryBuildResult functions =
        BuildRuntimeMaterialFunctionLibrary(manager, *loadedGraph.graph);
    Require(functions.diagnostics.empty() && functions.library.entries.size() == 1U &&
            functions.library.entries.front().assetId == functionId.value,
        "Packaged material function library did not load through AssetManager");
    static_cast<void>(GlobalRenderMaterialParameterCollectionStore().UnloadCollection(collectionId.value));
    const std::vector<RenderMaterialGraphDiagnostic> collectionDiagnostics =
        LoadRuntimeMaterialParameterCollectionDefaults(manager, *loadedGraph.graph);
    const auto globalTint = GlobalRenderMaterialParameterCollectionStore().Resolve(collectionId.value, "GlobalTint");
    Require(collectionDiagnostics.empty() && globalTint.has_value() && NearlyEqual(globalTint->value[2], 0.75F),
        "Packaged material parameter collection defaults did not load through AssetManager");
    const RenderMaterialTypeReferenceValidationResult typeValidation =
        ValidateRenderMaterialTypeReference(*loadedMaterial, *manager.Registry().Find(materialId), manager);
    Require(typeValidation.Succeeded() && typeValidation.materialType.has_value() &&
            typeValidation.materialType->stableTypeId == type.stableTypeId,
        "Packaged Material Type reference did not load through AssetManager");

    static_cast<void>(GlobalRenderMaterialParameterCollectionStore().UnloadCollection(collectionId.value));
    runtimePack->Unmount();
    std::filesystem::remove(packPath, removeError);
}

void RunStandaloneMaterialGraphCodecParityAndAtomicSaveTest() {
    const std::uint32_t fromPinId = RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::TextureSample, "color", true);
    const std::uint32_t toPinId = RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::MaterialOutput, "baseColor", false);
    RenderMaterialGraphLink canonicalLink{
        .fromNodeId = 2U,
        .fromPinId = fromPinId,
        .toNodeId = 1U,
        .toPinId = toPinId,
    };
    const std::uint32_t canonicalLinkId = MakeRenderMaterialGraphLinkId(canonicalLink);
    const std::uint32_t staleLinkId = canonicalLinkId == 123U ? 124U : 123U;
    std::ostringstream source;
    source << "graphVersion 1\n"
           << "graphShadingModel lit\n"
           << "graphStorageModel material-graph-asset\n"
           << "graphNode 1 MaterialOutput 640 240\n"
           << "graphNode 2 TextureSample 240 180\n"
           << "graphLink " << staleLinkId << " 2 " << fromPinId << " color 1 " << toPinId << " baseColor\n";

    std::istringstream input{ source.str() };
    const RenderMaterialAssetParseResult migrated = RenderMaterialGraphAssetLoader::LoadGraphWithDiagnostics(input);
    Require(migrated.Succeeded() && migrated.HasWarnings() && !migrated.HasErrors() && migrated.asset.has_value(),
        "P2.11: standalone graph migrations must keep a valid graph loadable with visible warnings");
    Require(migrated.asset->graph.documentVersion == kRenderMaterialGraphDocumentVersion &&
            migrated.asset->graph.shadingModel == "defaultLit" &&
            migrated.asset->graph.links.size() == 1U &&
            migrated.asset->graph.links.front().id == canonicalLinkId,
        "P2.11: standalone graph codec must apply the same version and stable-link migrations as embedded graphs");

    std::istringstream invalid{ "graphVersion 1\ngraphNode 1 MaterialOutput 640 240\nunknownGraphField value\n" };
    const RenderMaterialAssetParseResult failed = RenderMaterialGraphAssetLoader::LoadGraphWithDiagnostics(invalid);
    Require(!failed.Succeeded() && failed.HasErrors() && !failed.asset.has_value(),
        "P2.11: standalone graph codec must keep error diagnostics fatal");

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_standalone_graph_atomic_save";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    const std::filesystem::path graphPath = root / "Nested" / "Surface.kbmaterialgraph";
    Require(RenderMaterialGraphAssetLoader::SaveGraph(graphPath, migrated.asset->graph),
        "P2.11: atomic standalone graph save must create its parent directory");
    const RenderMaterialAssetParseResult reloaded = RenderMaterialGraphAssetLoader::LoadGraphWithDiagnostics(graphPath);
    Require(reloaded.Succeeded() && reloaded.asset.has_value() &&
            reloaded.asset->graph.documentVersion == kRenderMaterialGraphDocumentVersion &&
            reloaded.asset->graph.links.size() == 1U,
        "P2.11: atomically saved standalone graph must round-trip through the production loader");
    std::size_t fileCount = 0U;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(graphPath.parent_path())) {
        if (entry.is_regular_file()) {
            ++fileCount;
        }
    }
    Require(fileCount == 1U, "P2.11: successful standalone graph save must not leave temporary files behind");

    const std::filesystem::path legacyGraphPath = root / "LegacyRuntime.kbmaterialgraph";
    {
        std::ofstream legacyGraph{ legacyGraphPath, std::ios::trunc };
        legacyGraph << "graphVersion 1\n"
                    << "graphShadingModel lit\n"
                    << "graphNode 1 MaterialOutput 640 240\n";
    }
    RenderMaterialAssetData graphBackedMaterial{};
    graphBackedMaterial.graphSourceAssetPath = "/Game/LegacyRuntime.kbmaterialgraph";
    graphBackedMaterial.graph = MakeDefaultRenderMaterialGraphDocument();
    const std::filesystem::path materialPath = root / "LegacyRuntimeMaterial.kbmat";
    Require(RenderMaterialAssetWriter::Save(materialPath, graphBackedMaterial),
        "P2.11: runtime warning propagation fixture material must save");

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()) &&
            manager.RegisterLoader(std::make_unique<RenderMaterialGraphAssetLoader>()) &&
            manager.Mounts().Mount("Game", root) && manager.DiscoverMountedAssets() >= 2U,
        "P2.11: runtime warning propagation fixtures must be discoverable");
    const kb::assets::AssetMetadata* materialMetadata =
        manager.Registry().FindByPath("/Game/LegacyRuntimeMaterial.kbmat");
    Require(materialMetadata != nullptr,
        "P2.11: runtime warning propagation material metadata must resolve");
    const ResolvedRuntimeMaterialAsset runtimeResolved =
        RuntimeMaterialResolver{}.ResolveAsset(manager, *materialMetadata);
    Require(runtimeResolved.status == RuntimeMaterialResolveStatus::Resolved &&
            std::ranges::any_of(runtimeResolved.diagnostics, [](const RuntimeMaterialResolveDiagnostic& diagnostic) {
                return diagnostic.severity == RuntimeMaterialResolveDiagnosticSeverity::Warning &&
                    diagnostic.message.find("graph_migration") != std::string::npos;
            }),
        "P2.11: production runtime must keep migrated source graphs loadable and surface their warning diagnostics");
    std::filesystem::remove_all(root, error);
}

void RunMaterialTypeReferenceValidationDrivesRuntimeErrorMaterialTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_material_type_reference_validation";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "MaterialTypes", error);
    Require(!error, "KBMAT-GRAPH-0305: Material Type validation test could not create temp root");

    RenderMaterialTypeDocument validType = GetBuiltInPbrMaterialTypeDocument();
    validType.stableTypeId = "graph.surface";
    validType.version = 1U;
    validType.schema.typeName = validType.stableTypeId;
    validType.schema.typeVersion = validType.version;
    Require(RenderMaterialTypeAssetLoader::SaveType(root / "MaterialTypes" / "GraphSurface.kbmaterialtype", validType),
        "KBMAT-GRAPH-0305: Could not write valid Material Type fixture");

    RenderMaterialTypeDocument incompatibleType = validType;
    incompatibleType.stableTypeId = "graph.other";
    incompatibleType.schema.typeName = incompatibleType.stableTypeId;
    Require(RenderMaterialTypeAssetLoader::SaveType(root / "MaterialTypes" / "GraphOther.kbmaterialtype", incompatibleType),
        "KBMAT-GRAPH-0305: Could not write incompatible Material Type fixture");

    RenderMaterialTypeDocument versionType = validType;
    versionType.version = 2U;
    versionType.schema.typeVersion = versionType.version;
    Require(RenderMaterialTypeAssetLoader::SaveType(root / "MaterialTypes" / "GraphSurfaceV2.kbmaterialtype", versionType),
        "KBMAT-GRAPH-0305: Could not write version-mismatched Material Type fixture");

    RenderMaterialAssetData validMaterial{};
    validMaterial.materialType = "graph.surface";
    validMaterial.materialTypeVersion = 1U;
    validMaterial.hasExplicitMaterialType = true;
    validMaterial.hasExplicitMaterialTypeVersion = true;
    validMaterial.materialTypeAssetPath = "/Game/MaterialTypes/GraphSurface.kbmaterialtype";
    Require(RenderMaterialAssetWriter::Save(root / "ValidGraphMaterial.kbmat", validMaterial),
        "KBMAT-GRAPH-0305: Could not write valid graph material fixture");

    RenderMaterialAssetData missingTypeMaterial = validMaterial;
    missingTypeMaterial.materialTypeAssetPath = "/Game/MaterialTypes/Missing.kbmaterialtype";
    Require(RenderMaterialAssetWriter::Save(root / "MissingTypeMaterial.kbmat", missingTypeMaterial),
        "KBMAT-GRAPH-0305: Could not write missing-type material fixture");

    RenderMaterialAssetData incompatibleMaterial = validMaterial;
    incompatibleMaterial.materialTypeAssetPath = "/Game/MaterialTypes/GraphOther.kbmaterialtype";
    Require(RenderMaterialAssetWriter::Save(root / "IncompatibleTypeMaterial.kbmat", incompatibleMaterial),
        "KBMAT-GRAPH-0305: Could not write incompatible-type material fixture");

    RenderMaterialAssetData versionMismatchMaterial = validMaterial;
    versionMismatchMaterial.materialTypeAssetPath = "/Game/MaterialTypes/GraphSurfaceV2.kbmaterialtype";
    Require(RenderMaterialAssetWriter::Save(root / "VersionMismatchMaterial.kbmat", versionMismatchMaterial),
        "KBMAT-GRAPH-0305: Could not write version-mismatch material fixture");

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "KBMAT-GRAPH-0305: Could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialTypeAssetLoader>()), "KBMAT-GRAPH-0305: Could not register material type loader");
    Require(manager.Mounts().Mount("Game", root), "KBMAT-GRAPH-0305: Could not mount temp root");
    Require(manager.DiscoverMountedAssets() >= 7U, "KBMAT-GRAPH-0305: Asset discovery missed material/type fixtures");

    const kb::assets::AssetMetadata* validMetadata = manager.Registry().FindByPath("/Game/ValidGraphMaterial.kbmat");
    const kb::assets::AssetMetadata* missingMetadata = manager.Registry().FindByPath("/Game/MissingTypeMaterial.kbmat");
    const kb::assets::AssetMetadata* incompatibleMetadata = manager.Registry().FindByPath("/Game/IncompatibleTypeMaterial.kbmat");
    const kb::assets::AssetMetadata* versionMetadata = manager.Registry().FindByPath("/Game/VersionMismatchMaterial.kbmat");
    Require(validMetadata != nullptr && missingMetadata != nullptr && incompatibleMetadata != nullptr && versionMetadata != nullptr,
        "KBMAT-GRAPH-0305: Material validation fixtures were not discovered");

    const std::optional<RenderMaterialAssetData> validLoaded = RenderMaterialAssetLoader::LoadMaterial(validMetadata->physicalPath);
    Require(validLoaded.has_value(), "KBMAT-GRAPH-0305: Valid graph material did not parse");
    const RenderMaterialTypeReferenceValidationResult validReference =
        ValidateRenderMaterialTypeReference(*validLoaded, *validMetadata, manager);
    Require(validReference.Succeeded() && validReference.materialType.has_value(),
        "KBMAT-GRAPH-0305: Valid Material Type reference should pass validation");

    const std::optional<RenderMaterialAssetData> missingLoaded = RenderMaterialAssetLoader::LoadMaterial(missingMetadata->physicalPath);
    const RenderMaterialTypeReferenceValidationResult missingReference =
        ValidateRenderMaterialTypeReference(*missingLoaded, *missingMetadata, manager);
    Require(!missingReference.Succeeded() &&
            missingReference.diagnostics[0].code == RenderMaterialTypeReferenceDiagnosticCode::MissingMaterialTypeAsset,
        "KBMAT-GRAPH-0305: Missing Material Type asset should produce a typed diagnostic");

    const std::optional<RenderMaterialAssetData> incompatibleLoaded = RenderMaterialAssetLoader::LoadMaterial(incompatibleMetadata->physicalPath);
    const RenderMaterialTypeReferenceValidationResult incompatibleReference =
        ValidateRenderMaterialTypeReference(*incompatibleLoaded, *incompatibleMetadata, manager);
    Require(!incompatibleReference.Succeeded() &&
            std::ranges::any_of(incompatibleReference.diagnostics, [](const RenderMaterialTypeReferenceDiagnostic& diagnostic) {
                return diagnostic.code == RenderMaterialTypeReferenceDiagnosticCode::IncompatibleMaterialType;
            }),
        "KBMAT-GRAPH-0305: Stable Material Type mismatch should produce an incompatible type diagnostic");

    const std::optional<RenderMaterialAssetData> versionLoaded = RenderMaterialAssetLoader::LoadMaterial(versionMetadata->physicalPath);
    const RenderMaterialTypeReferenceValidationResult versionReference =
        ValidateRenderMaterialTypeReference(*versionLoaded, *versionMetadata, manager);
    Require(!versionReference.Succeeded() &&
            std::ranges::any_of(versionReference.diagnostics, [](const RenderMaterialTypeReferenceDiagnostic& diagnostic) {
                return diagnostic.code == RenderMaterialTypeReferenceDiagnosticCode::IncompatibleMaterialTypeVersion;
            }),
        "KBMAT-GRAPH-0305: Material Type version mismatch should produce an incompatible version diagnostic");

    RuntimeMaterialResolver resolver;
    const ResolvedRuntimeMaterialAsset validResolved = resolver.ResolveAsset(manager, *validMetadata);
    Require(validResolved.status == RuntimeMaterialResolveStatus::Resolved && validResolved.diagnostics.empty(),
        "KBMAT-GRAPH-0305: Valid graph-backed Material Type reference should resolve normally");

    const ResolvedRuntimeMaterialAsset missingResolved = resolver.ResolveAsset(manager, *missingMetadata);
    Require(missingResolved.status == RuntimeMaterialResolveStatus::ErrorMaterial &&
            missingResolved.material.desc.baseColor[0] == RuntimeMaterialResolver::ErrorMaterialDesc().baseColor[0] &&
            std::ranges::any_of(missingResolved.diagnostics, [](const RuntimeMaterialResolveDiagnostic& diagnostic) {
                return diagnostic.kind == RuntimeMaterialResolveDiagnosticKind::MaterialTypeReferenceValidationFailed &&
                    diagnostic.message.find("missing_material_type_asset") != std::string::npos;
            }),
        "KBMAT-GRAPH-0305: Missing Material Type reference should force runtime error material with diagnostics");

    const ResolvedRuntimeMaterialAsset incompatibleResolved = resolver.ResolveAsset(manager, *incompatibleMetadata);
    Require(incompatibleResolved.status == RuntimeMaterialResolveStatus::ErrorMaterial &&
            std::ranges::any_of(incompatibleResolved.diagnostics, [](const RuntimeMaterialResolveDiagnostic& diagnostic) {
                return diagnostic.kind == RuntimeMaterialResolveDiagnosticKind::MaterialTypeReferenceValidationFailed &&
                    diagnostic.message.find("incompatible_material_type") != std::string::npos;
            }),
        "KBMAT-GRAPH-0305: Incompatible Material Type reference should force runtime error material with diagnostics");

    const ResolvedRuntimeMaterialAsset versionResolved = resolver.ResolveAsset(manager, *versionMetadata);
    Require(versionResolved.status == RuntimeMaterialResolveStatus::ErrorMaterial &&
            std::ranges::any_of(versionResolved.diagnostics, [](const RuntimeMaterialResolveDiagnostic& diagnostic) {
                return diagnostic.kind == RuntimeMaterialResolveDiagnosticKind::MaterialTypeReferenceValidationFailed &&
                    diagnostic.message.find("incompatible_material_type_version") != std::string::npos;
            }),
        "KBMAT-GRAPH-0305: Version-mismatched Material Type reference should force runtime error material with diagnostics");

    std::filesystem::remove_all(root, error);
}

void RunMaterialCookPayloadContainsParamsTextureDepsTypeVersionAndHashTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_material_cook_payload";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "Textures", error);
    Require(!error, "Material cook payload test could not create temp root");

    {
        std::ofstream output{ root / "Textures" / "albedo.kbtex", std::ios::trunc };
        output << "size 1 1\nrgba8 255 255 255 255\n";
    }
    {
        std::ofstream output{ root / "Textures" / "normal.kbtex", std::ios::trunc };
        output << "size 1 1\nrgba8 128 128 255 255\n";
    }
    {
        std::ofstream output{ root / "Textures" / "graph_mask.kbtex", std::ios::trunc };
        output << "size 1 1\nrgba8 32 64 128 255\n";
    }

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Material cook payload test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderTextureAssetLoader>()), "Material cook payload test could not register texture loader");
    Require(manager.Mounts().Mount("Game", root), "Material cook payload test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 3U, "Material cook payload test did not discover textures");

    const kb::assets::AssetMetadata* albedoMetadata = manager.Registry().FindByPath("/Game/Textures/albedo.kbtex");
    const kb::assets::AssetMetadata* normalMetadata = manager.Registry().FindByPath("/Game/Textures/normal.kbtex");
    const kb::assets::AssetMetadata* graphMaskMetadata = manager.Registry().FindByPath("/Game/Textures/graph_mask.kbtex");
    Require(albedoMetadata != nullptr && normalMetadata != nullptr && graphMaskMetadata != nullptr, "Material cook payload test lost texture metadata");
    const kb::assets::AssetId albedoId = albedoMetadata->id;
    const kb::assets::AssetId normalId = normalMetadata->id;
    const kb::assets::AssetId graphMaskId = graphMaskMetadata->id;

    RenderMaterialAssetData material{};
    material.materialType = kRenderMaterialAssetBuiltInPbrType;
    material.materialTypeVersion = kRenderMaterialAssetBuiltInPbrTypeVersion;
    material.desc.baseColor[0] = 0.3F;
    material.desc.baseColor[1] = 0.4F;
    material.desc.baseColor[2] = 0.5F;
    material.desc.baseColor[3] = 1.0F;
    material.desc.roughnessFactor = 0.42F;
    material.desc.albedoTextureAssetId = albedoId.value;
    material.normalTexturePath = "Textures/normal.kbtex";
    Require(RenderMaterialAssetWriter::Save(root / "cookable.kbmat", material), "Material cook payload test could not write material");
    Require(manager.DiscoverMountedAssets() >= 3U, "Material cook payload test did not discover material");

    const kb::assets::AssetMetadata* materialMetadataPtr = manager.Registry().FindByPath("/Game/cookable.kbmat");
    Require(materialMetadataPtr != nullptr && materialMetadataPtr->type == "RenderMaterial", "Material cook payload test did not discover material metadata");
    const kb::assets::AssetMetadata materialMetadata = *materialMetadataPtr;
    const std::optional<RenderMaterialAssetData> loaded = RenderMaterialAssetLoader::LoadMaterial(materialMetadata.physicalPath);
    Require(loaded.has_value(), "Material cook payload test could not load material");

    const RenderMaterialCookPayload payload = RenderMaterialCookPayloadBuilder::Build(*loaded, materialMetadata, manager.Registry());
    Require(payload.materialType == kRenderMaterialAssetBuiltInPbrType, "Material cook payload lost material type");
    Require(payload.materialTypeVersion == kRenderMaterialAssetBuiltInPbrTypeVersion, "Material cook payload lost material type version");
    Require(!payload.materialTypeAssetId.IsValid() && payload.materialTypeAssetPath.empty(), "Material cook payload should keep built-in PBR free of material type asset references");
    Require(payload.sourceContentHash == materialMetadata.contentHash && payload.sourceContentHash != 0U, "Material cook payload lost source content hash");
    Require(payload.payloadHash != 0U, "Material cook payload did not compute payload hash");
    Require(NearlyEqual(payload.params.baseColor[0], 0.3F), "Material cook payload lost base color params");
    Require(NearlyEqual(payload.params.roughnessFactor, 0.42F), "Material cook payload lost roughness params");
    Require(payload.textureDependencies.size() == 2U, "Material cook payload did not collect texture dependencies");
    Require(payload.textureDependencies[0].value < payload.textureDependencies[1].value, "Material cook payload texture dependencies should be deterministic");
    Require(ContainsDependency(payload.textureDependencies, albedoId), "Material cook payload lost direct texture dependency");
    Require(ContainsDependency(payload.textureDependencies, normalId), "Material cook payload lost path texture dependency");

    const RenderMaterialCookPayload repeated = RenderMaterialCookPayloadBuilder::Build(*loaded, materialMetadata, manager.Registry());
    Require(repeated.payloadHash == payload.payloadHash, "Material cook payload hash should be deterministic for identical inputs");
    RenderMaterialAssetData changed = *loaded;
    changed.desc.roughnessFactor = 0.75F;
    const RenderMaterialCookPayload changedPayload = RenderMaterialCookPayloadBuilder::Build(changed, materialMetadata, manager.Registry());
    Require(changedPayload.payloadHash != payload.payloadHash, "Material cook payload hash should change when material params change");

    RenderMaterialAssetData editorOnlyChange = *loaded;
    editorOnlyChange.graph = MakeDefaultRenderMaterialGraphDocument();
    editorOnlyChange.graph.nodes.front().positionX += 120;
    kb::assets::AssetMetadata editorOnlyMetadata = materialMetadata;
    editorOnlyMetadata.contentHash += 1000U;
    const RenderMaterialCookPayload editorOnlyPayload = RenderMaterialCookPayloadBuilder::Build(editorOnlyChange, editorOnlyMetadata, manager.Registry());
    Require(editorOnlyPayload.sourceContentHash == editorOnlyMetadata.contentHash, "KBMAT-0905: Material cook payload should preserve source hash separately");
    Require(editorOnlyPayload.payloadHash == payload.payloadHash, "KBMAT-0905: Editor-only material metadata must not change runtime payload hash");

    RenderMaterialAssetData graphBackedMaterial = *loaded;
    graphBackedMaterial.materialType = "graph.surface";
    graphBackedMaterial.materialTypeVersion = 3U;
    graphBackedMaterial.materialTypeAssetId = 0x4700U;
    graphBackedMaterial.materialTypeAssetPath = "/Game/MaterialTypes/GraphSurface.kbmaterialtype";
    graphBackedMaterial.graph = MakeDefaultRenderMaterialGraphDocument();
    graphBackedMaterial.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 120,
        .positionY = 80,
    });
    RenderMaterialGraphLink graphBaseColorLink{
        .fromNodeId = 2U,
        .fromPinId = RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::ConstantColor, "rgba", true),
        .fromPin = "rgba",
        .toNodeId = 1U,
        .toPinId = RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::MaterialOutput, "baseColor", false),
        .toPin = "baseColor",
    };
    graphBaseColorLink.id = MakeRenderMaterialGraphLinkId(graphBaseColorLink);
    graphBackedMaterial.graph.links.push_back(graphBaseColorLink);
    graphBackedMaterial.graphParameterValues.push_back(RenderMaterialGraphParameterValue{
        .stableId = "graphMask",
        .type = RenderMaterialParameterType::Texture,
        .assetId = graphMaskId.value,
    });
    const RenderMaterialCookPayload graphPayload = RenderMaterialCookPayloadBuilder::Build(graphBackedMaterial, materialMetadata, manager.Registry());
    Require(graphPayload.materialType == "graph.surface" && graphPayload.materialTypeVersion == 3U, "KBMAT-GRAPH-0003: Cook payload lost graph-backed material type identity");
    Require(graphPayload.materialTypeAssetId.value == 0x4700U && graphPayload.materialTypeAssetPath == "/Game/MaterialTypes/GraphSurface.kbmaterialtype", "KBMAT-GRAPH-0003: Cook payload lost material type asset reference");
    Require(graphPayload.payloadHash != payload.payloadHash, "KBMAT-GRAPH-0003: Cook payload hash should include material type asset reference");
    Require(ContainsDependency(graphPayload.textureDependencies, graphMaskId), "KBMAT-GRAPH-0206: Cook payload lost graph texture parameter dependency");
    Require(graphPayload.graphBacked && graphPayload.graphCompileSucceeded, "KBMAT-GRAPH-0206: Graph-backed cook payload should contain a successful graph compile artifact");
    Require(graphPayload.graphCompileKey.combinedHash != 0U && graphPayload.graphShader.sourceHash != 0U, "KBMAT-GRAPH-0206: Graph-backed cook payload should carry graph compile/cache hashes");
    Require(graphPayload.graphShader.source.find("EvaluateMaterialGraph") != std::string::npos, "KBMAT-GRAPH-0206: Graph-backed cook payload should carry generated shader source for runtime/cook consumers");
    Require(graphPayload.graphDiagnostics.empty(), "KBMAT-GRAPH-0206: Valid graph-backed cook payload should not carry graph compile diagnostics");
    const std::vector<kb::assets::AssetId> graphDependencies =
        RenderMaterialAssetLoader::DiscoverMaterialDependencies(graphBackedMaterial, materialMetadata, manager.Registry());
    Require(ContainsDependency(graphDependencies, graphMaskId), "KBMAT-GRAPH-0405: Material dependency discovery lost graph texture parameter dependency");
    RenderMaterialAssetData changedGraphTexture = graphBackedMaterial;
    changedGraphTexture.graphParameterValues.front().assetId = albedoId.value;
    const RenderMaterialCookPayload changedGraphTexturePayload = RenderMaterialCookPayloadBuilder::Build(changedGraphTexture, materialMetadata, manager.Registry());
    Require(changedGraphTexturePayload.payloadHash != graphPayload.payloadHash,
        "KBMAT-GRAPH-0405: Graph texture parameter changes must invalidate the cooked material payload hash");

    RenderMaterialAssetData inlineTextureSampleMaterial = *loaded;
    inlineTextureSampleMaterial.graph = MakeDefaultRenderMaterialGraphDocument();
    inlineTextureSampleMaterial.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::TextureSample,
        .positionX = 120,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "textureSample2",
            .displayName = "Texture Sample 2",
            .textureRole = "normal",
            .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Linear,
            .overrideSupported = true,
        },
    });
    RenderMaterialGraphLink inlineTextureBaseColorLink{
        .fromNodeId = 2U,
        .fromPinId = RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::TextureSample, "color", true),
        .fromPin = "color",
        .toNodeId = 1U,
        .toPinId = RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::MaterialOutput, "baseColor", false),
        .toPin = "baseColor",
    };
    inlineTextureBaseColorLink.id = MakeRenderMaterialGraphLinkId(inlineTextureBaseColorLink);
    inlineTextureSampleMaterial.graph.links.push_back(inlineTextureBaseColorLink);
    inlineTextureSampleMaterial.graphParameterValues.push_back(RenderMaterialGraphParameterValue{
        .stableId = "textureSample2",
        .type = RenderMaterialParameterType::Texture,
        .assetId = graphMaskId.value,
    });
    Require(RenderMaterialAssetWriter::Save(root / "inline_texture_sample_preview.kbmat", inlineTextureSampleMaterial),
        "KBMAT-GRAPH-0406: Inline TextureSample material could not be written");
    static_cast<void>(manager.DiscoverMountedAssets());
    const kb::assets::AssetMetadata* inlineTextureMetadata = manager.Registry().FindByPath("/Game/inline_texture_sample_preview.kbmat");
    Require(inlineTextureMetadata != nullptr && inlineTextureMetadata->type == "RenderMaterial",
        "KBMAT-GRAPH-0406: Inline TextureSample material metadata missing");
    const ResolvedRuntimeMaterialAsset inlineTextureResolved = RuntimeMaterialResolver{}.ResolveAsset(manager, inlineTextureMetadata->id);
    Require(inlineTextureResolved.resolved && inlineTextureResolved.material.desc.albedoTextureAssetId == graphMaskId.value,
        "KBMAT-GRAPH-0406: Runtime resolver should map Material Output BaseColor links to PBR albedo texture for previews");

    // [PERF] Time the synchronous runtime resolve of this graph-backed material with a texture sample - the same
    // work the editor does as "preview resolve after edit" when a node is added. Cold = first resolve (disk read
    // + parse + shader-source codegen + program binding); warm = second resolve (codegen served from the process
    // memo). If neither is ~1-2s, the reported "adding a node lags" is NOT the resolve.
    {
        RuntimeMaterialResolver perfResolver;
        const auto coldStart = std::chrono::steady_clock::now();
        const ResolvedRuntimeMaterialAsset coldResolve = perfResolver.ResolveAsset(manager, inlineTextureMetadata->id);
        const double coldMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - coldStart).count();
        const auto warmStart = std::chrono::steady_clock::now();
        const ResolvedRuntimeMaterialAsset warmResolve = perfResolver.ResolveAsset(manager, inlineTextureMetadata->id);
        const double warmMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - warmStart).count();
        std::fprintf(stderr, "[PERF-RESOLVE] graph material (texture sample) resolve: cold=%.3f ms  warm=%.3f ms\n", coldMs, warmMs);
        Require(coldResolve.resolved && warmResolve.resolved, "[PERF-RESOLVE] graph material resolves both times");
    }
    Require(NearlyEqual(inlineTextureResolved.material.desc.baseColor[0], 1.0F) &&
            NearlyEqual(inlineTextureResolved.material.desc.baseColor[1], 1.0F) &&
            NearlyEqual(inlineTextureResolved.material.desc.baseColor[2], 1.0F) &&
            NearlyEqual(inlineTextureResolved.material.desc.baseColor[3], 1.0F),
        "KBMAT-GRAPH-0406: Texture Sample BaseColor output should use a neutral white base factor so preview textures are not tinted");

    RenderMaterialAssetData disconnectedBaseColorMaterial = inlineTextureSampleMaterial;
    disconnectedBaseColorMaterial.graph.links.clear();
    const std::vector<RenderMaterialGraphDiagnostic> disconnectedDiagnostics =
        ValidateRenderMaterialAssetGraphDiagnostics(disconnectedBaseColorMaterial);
    Require(!std::ranges::any_of(disconnectedDiagnostics, [](const RenderMaterialGraphDiagnostic& diagnostic) {
        return diagnostic.severity == RenderMaterialGraphDiagnosticSeverity::Error &&
            diagnostic.kind == RenderMaterialGraphDiagnosticKind::DisconnectedRequiredOutput &&
            diagnostic.pin == "baseColor";
    }), "KBMAT-GRAPH-0407: Disconnected Material Output Base Color should use MaterialSurface default fallback instead of graph validation error");
    Require(RenderMaterialAssetWriter::Save(root / "disconnected_basecolor_preview.kbmat", disconnectedBaseColorMaterial),
        "KBMAT-GRAPH-0407: Disconnected BaseColor material could not be written");
    static_cast<void>(manager.DiscoverMountedAssets());
    const kb::assets::AssetMetadata* disconnectedBaseColorMetadata = manager.Registry().FindByPath("/Game/disconnected_basecolor_preview.kbmat");
    Require(disconnectedBaseColorMetadata != nullptr && disconnectedBaseColorMetadata->type == "RenderMaterial",
        "KBMAT-GRAPH-0407: Disconnected BaseColor material metadata missing");
    const ResolvedRuntimeMaterialAsset disconnectedBaseColorResolved = RuntimeMaterialResolver{}.ResolveAsset(manager, disconnectedBaseColorMetadata->id);
    Require(disconnectedBaseColorResolved.resolved &&
            NearlyEqual(disconnectedBaseColorResolved.material.desc.baseColor[0], 1.0F) &&
            NearlyEqual(disconnectedBaseColorResolved.material.desc.baseColor[1], 1.0F) &&
            NearlyEqual(disconnectedBaseColorResolved.material.desc.baseColor[2], 1.0F) &&
            NearlyEqual(disconnectedBaseColorResolved.material.desc.baseColor[3], 1.0F) &&
            disconnectedBaseColorResolved.material.desc.albedoTextureAssetId == 0U,
        "KBMAT-GRAPH-0407: Runtime resolver should preview disconnected Material Output Base Color using MaterialSurface white default");

    RenderMaterialAssetData secondMaterial = *loaded;
    secondMaterial.desc.baseColor[0] = 0.9F;
    kb::assets::AssetMetadata secondMetadata = materialMetadata;
    secondMetadata.id = kb::assets::AssetId{ materialMetadata.id.value + 17U };
    secondMetadata.contentHash += 17U;
    const std::array<RenderMaterialCookManifestInput, 2U> orderedInputs{{
        RenderMaterialCookManifestInput{ .material = &secondMaterial, .metadata = &secondMetadata },
        RenderMaterialCookManifestInput{ .material = &*loaded, .metadata = &materialMetadata },
    }};
    const std::array<RenderMaterialCookManifestInput, 2U> reversedInputs{{
        RenderMaterialCookManifestInput{ .material = &*loaded, .metadata = &materialMetadata },
        RenderMaterialCookManifestInput{ .material = &secondMaterial, .metadata = &secondMetadata },
    }};
    const RenderMaterialCookManifest orderedManifest = RenderMaterialCookManifestBuilder::Build(orderedInputs, manager.Registry());
    const RenderMaterialCookManifest reversedManifest = RenderMaterialCookManifestBuilder::Build(reversedInputs, manager.Registry());
    Require(orderedManifest.entries.size() == 2U && reversedManifest.entries.size() == 2U, "KBMAT-0906: Material cook manifest should keep valid material entries");
    Require(orderedManifest.entries[0].materialAssetId.value < orderedManifest.entries[1].materialAssetId.value, "KBMAT-0906: Material cook manifest entries should be sorted by material asset id");
    Require(orderedManifest.manifestHash == reversedManifest.manifestHash, "KBMAT-0906: Material cook manifest hash should not depend on input order");
    Require(orderedManifest.entries[0].payloadHash == reversedManifest.entries[0].payloadHash &&
            orderedManifest.entries[1].payloadHash == reversedManifest.entries[1].payloadHash,
        "KBMAT-0906: Material cook manifest entries should be deterministic");

    const std::array<RenderMaterialCookManifestInput, 1U> graphInputs{{
        RenderMaterialCookManifestInput{ .material = &graphBackedMaterial, .metadata = &materialMetadata },
    }};
    const RenderMaterialCookManifest graphManifest = RenderMaterialCookManifestBuilder::Build(graphInputs, manager.Registry());
    Require(graphManifest.entries.size() == 1U && graphManifest.entries[0].materialTypeAssetId.value == 0x4700U, "KBMAT-GRAPH-0003: Cook manifest lost material type asset id");
    Require(graphManifest.entries[0].materialTypeAssetPath == "/Game/MaterialTypes/GraphSurface.kbmaterialtype", "KBMAT-GRAPH-0003: Cook manifest lost material type asset path");

    std::filesystem::remove_all(root, error);
}

void RunRenderMaterialAssetWriterRoundTripsThroughParserTest() {
    RenderMaterialAssetData source{};
    source.desc.baseColor[0] = 0.12F;
    source.desc.baseColor[1] = 0.34F;
    source.desc.baseColor[2] = 0.56F;
    source.desc.baseColor[3] = 0.78F;
    source.desc.emissiveColor[0] = 0.9F;
    source.desc.emissiveColor[1] = 0.8F;
    source.desc.emissiveColor[2] = 0.7F;
    source.desc.metallicFactor = 0.25F;
    source.desc.roughnessFactor = 0.65F;
    source.desc.normalScale = 1.5F;
    source.desc.occlusionStrength = 0.45F;
    source.desc.emissiveStrength = 3.0F;
    source.desc.alphaCutoff = 0.33F;
    source.desc.clearcoatFactor = 0.22F;
    source.desc.clearcoatRoughnessFactor = 0.44F;
    source.desc.sheenColor[0] = 0.11F;
    source.desc.sheenColor[1] = 0.22F;
    source.desc.sheenColor[2] = 0.33F;
    source.desc.sheenRoughnessFactor = 0.58F;
    source.desc.transmissionFactor = 0.18F;
    source.desc.thicknessFactor = 0.27F;
    source.desc.attenuationColor[0] = 0.66F;
    source.desc.attenuationColor[1] = 0.77F;
    source.desc.attenuationColor[2] = 0.88F;
    source.desc.attenuationDistance = 12.0F;
    source.desc.subsurfaceColor[0] = 0.19F;
    source.desc.subsurfaceColor[1] = 0.29F;
    source.desc.subsurfaceColor[2] = 0.39F;
    source.desc.subsurfaceFactor = 0.49F;
    source.desc.anisotropyStrength = 0.59F;
    source.desc.anisotropyRotation = 0.69F;
    source.desc.layerWeight = 0.79F;
    source.desc.alphaMode = RenderMaterialAlphaMode::Blend;
    source.desc.decalBlendMode = RenderMaterialDecalBlendMode::Normal;
    source.desc.layerBlendMode = RenderMaterialLayerBlendMode::Add;
    source.desc.doubleSided = true;
    source.desc.albedoTextureAssetId = 101U;
    source.desc.normalTextureAssetId = 102U;
    source.desc.metallicRoughnessTextureAssetId = 103U;
    source.desc.occlusionTextureAssetId = 104U;
    source.desc.emissiveTextureAssetId = 105U;
    source.desc.clearcoatTextureAssetId = 106U;
    source.desc.clearcoatRoughnessTextureAssetId = 107U;
    source.desc.sheenColorTextureAssetId = 108U;
    source.desc.transmissionTextureAssetId = 109U;
    source.desc.thicknessTextureAssetId = 110U;
    source.desc.anisotropyTextureAssetId = 111U;
    source.desc.decalTextureAssetId = 112U;
    source.desc.layerMaskTextureAssetId = 113U;
    source.albedoTexturePath = "Textures/albedo.kbtex";
    source.normalTexturePath = "Textures/normal.kbtex";
    source.metallicRoughnessTexturePath = "Textures/mr.kbtex";
    source.occlusionTexturePath = "Textures/ao.kbtex";
    source.emissiveTexturePath = "Textures/emissive.kbtex";
    source.clearcoatTexturePath = "Textures/clearcoat.kbtex";
    source.clearcoatRoughnessTexturePath = "Textures/clearcoat-roughness.kbtex";
    source.sheenColorTexturePath = "Textures/sheen.kbtex";
    source.transmissionTexturePath = "Textures/transmission.kbtex";
    source.thicknessTexturePath = "Textures/thickness.kbtex";
    source.anisotropyTexturePath = "Textures/anisotropy.kbtex";
    source.decalTexturePath = "Textures/decal.kbtex";
    source.layerMaskTexturePath = "Textures/layer.kbtex";

    std::ostringstream output;
    RenderMaterialAssetWriter::Write(output, source);
    Require(output.str().find("# KB material\nversion 1\nmaterialType builtin.pbr\nmaterialTypeVersion 1\nbaseColor") == 0U, "Material writer did not emit canonical material header");
    std::istringstream input{ output.str() };
    const std::optional<RenderMaterialAssetData> loaded = RenderMaterialAssetLoader::LoadMaterial(input);
    Require(loaded.has_value(), "RenderMaterialAssetWriter produced material text the parser rejected");
    Require(loaded->documentVersion == kRenderMaterialAssetDocumentVersion, "Material writer roundtrip lost document version");
    Require(loaded->hasExplicitDocumentVersion, "Material writer roundtrip lost explicit document version metadata");
    Require(loaded->materialType == kRenderMaterialAssetBuiltInPbrType, "Material writer roundtrip lost material type");
    Require(loaded->materialTypeVersion == kRenderMaterialAssetBuiltInPbrTypeVersion, "Material writer roundtrip lost material type version");
    Require(loaded->hasExplicitMaterialType, "Material writer roundtrip lost explicit material type metadata");
    Require(loaded->hasExplicitMaterialTypeVersion, "Material writer roundtrip lost explicit material type version metadata");
    Require(NearlyEqual(loaded->desc.baseColor[2], source.desc.baseColor[2]), "Material writer roundtrip lost base color");
    Require(NearlyEqual(loaded->desc.emissiveColor[0], source.desc.emissiveColor[0]), "Material writer roundtrip lost emissive color");
    Require(NearlyEqual(loaded->desc.roughnessFactor, source.desc.roughnessFactor), "Material writer roundtrip lost roughness");
    Require(NearlyEqual(loaded->desc.clearcoatRoughnessFactor, source.desc.clearcoatRoughnessFactor), "Material writer roundtrip lost clearcoat roughness");
    Require(NearlyEqual(loaded->desc.attenuationColor[2], source.desc.attenuationColor[2]), "Material writer roundtrip lost attenuation color");
    Require(NearlyEqual(loaded->desc.layerWeight, source.desc.layerWeight), "Material writer roundtrip lost layer weight");
    Require(loaded->desc.alphaMode == RenderMaterialAlphaMode::Blend, "Material writer roundtrip lost alpha mode");
    Require(loaded->desc.decalBlendMode == RenderMaterialDecalBlendMode::Normal, "Material writer roundtrip lost decal mode");
    Require(loaded->desc.layerBlendMode == RenderMaterialLayerBlendMode::Add, "Material writer roundtrip lost layer mode");
    Require(loaded->desc.doubleSided, "Material writer roundtrip lost double-sided state");
    Require(loaded->desc.albedoTextureAssetId == source.desc.albedoTextureAssetId, "Material writer roundtrip lost albedo texture asset id");
    Require(loaded->desc.normalTextureAssetId == source.desc.normalTextureAssetId, "Material writer roundtrip lost normal texture asset id");
    Require(loaded->desc.metallicRoughnessTextureAssetId == source.desc.metallicRoughnessTextureAssetId, "Material writer roundtrip lost metallic-roughness texture asset id");
    Require(loaded->desc.occlusionTextureAssetId == source.desc.occlusionTextureAssetId, "Material writer roundtrip lost occlusion texture asset id");
    Require(loaded->desc.emissiveTextureAssetId == source.desc.emissiveTextureAssetId, "Material writer roundtrip lost emissive texture asset id");
    Require(loaded->desc.decalTextureAssetId == source.desc.decalTextureAssetId, "Material writer roundtrip lost decal texture asset id");
    Require(loaded->desc.layerMaskTextureAssetId == source.desc.layerMaskTextureAssetId, "Material writer roundtrip lost layer mask texture asset id");
    Require(loaded->clearcoatRoughnessTexturePath == source.clearcoatRoughnessTexturePath, "Material writer roundtrip lost clearcoat roughness path");
    Require(loaded->layerMaskTexturePath == source.layerMaskTexturePath, "Material writer roundtrip lost layer mask path");

    RenderMaterialAssetData graphBackedSource{};
    graphBackedSource.materialType = "graph.surface";
    graphBackedSource.materialTypeVersion = 2U;
    graphBackedSource.materialTypeAssetId = 0x4D545950U;
    graphBackedSource.materialTypeAssetPath = "/Game/MaterialTypes/GraphSurface.kbmaterialtype";
    graphBackedSource.graphSourceAssetId = 0x4752415048U;
    graphBackedSource.graphSourceAssetPath = "/Game/Graphs/Surface.kbmaterialgraph";
    std::ostringstream graphOutput;
    RenderMaterialAssetWriter::Write(graphOutput, graphBackedSource);
    const std::string expectedMaterialTypeHeader =
        "materialType graph.surface\nmaterialTypeVersion 2\nmaterialTypeAssetId " +
        std::to_string(graphBackedSource.materialTypeAssetId) +
        "\nmaterialTypeAsset /Game/MaterialTypes/GraphSurface.kbmaterialtype\n"
        "graphSourceAssetId " +
        std::to_string(graphBackedSource.graphSourceAssetId) +
        "\ngraphSourceAsset /Game/Graphs/Surface.kbmaterialgraph\n";
    Require(graphOutput.str().find(expectedMaterialTypeHeader) != std::string::npos,
        "KBMAT-GRAPH-0304: Material writer did not emit graph-backed type and source graph asset references in canonical header");
    std::istringstream graphInput{ graphOutput.str() };
    const RenderMaterialAssetParseResult graphLoaded = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(graphInput);
    Require(graphLoaded.asset.has_value() && graphLoaded.diagnostics.empty(), "KBMAT-GRAPH-0003: Parser rejected graph-backed material type asset reference");
    Require(graphLoaded.asset->materialType == "graph.surface" && graphLoaded.asset->materialTypeVersion == 2U, "KBMAT-GRAPH-0003: Parser lost graph-backed material type identity");
    Require(graphLoaded.asset->materialTypeAssetId == 0x4D545950U && graphLoaded.asset->materialTypeAssetPath == "/Game/MaterialTypes/GraphSurface.kbmaterialtype", "KBMAT-GRAPH-0003: Parser lost graph-backed material type asset reference");
    Require(graphLoaded.asset->graphSourceAssetId == 0x4752415048U && graphLoaded.asset->graphSourceAssetPath == "/Game/Graphs/Surface.kbmaterialgraph",
        "KBMAT-GRAPH-0304: Parser lost graph-backed source graph asset reference");

    graphBackedSource.graphParameterValues.push_back(RenderMaterialGraphParameterValue{
        .stableId = "tintColor",
        .type = RenderMaterialParameterType::Color,
        .numbers = { 0.2F, 0.4F, 0.6F, 1.0F },
    });
    graphBackedSource.graphParameterValues.push_back(RenderMaterialGraphParameterValue{
        .stableId = "obsoleteParam",
        .type = RenderMaterialParameterType::Scalar,
        .numbers = { 0.75F, 0.0F, 0.0F, 0.0F },
    });
    graphBackedSource.graphParameterValues.push_back(RenderMaterialGraphParameterValue{
        .stableId = "wear",
        .type = RenderMaterialParameterType::Scalar,
        .numbers = { 0.9F, 0.0F, 0.0F, 0.0F },
    });
    std::ostringstream graphParameterOutput;
    RenderMaterialAssetWriter::Write(graphParameterOutput, graphBackedSource);
    Require(graphParameterOutput.str().find("graphParameterValue tintColor Color 0.200000003 0.400000006 0.600000024 1\n") != std::string::npos,
        "KBMAT-GRAPH-0303: Material writer did not emit graph parameter values");
    std::istringstream graphParameterInput{ graphParameterOutput.str() };
    const RenderMaterialAssetParseResult graphParameterLoaded = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(graphParameterInput);
    Require(graphParameterLoaded.asset.has_value() && graphParameterLoaded.asset->graphParameterValues.size() == 3U,
        "KBMAT-GRAPH-0303: Parser lost graph parameter values");
    Require(graphParameterLoaded.asset->graphParameterValues[0].stableId == "tintColor" &&
            graphParameterLoaded.asset->graphParameterValues[0].type == RenderMaterialParameterType::Color &&
            NearlyEqual(graphParameterLoaded.asset->graphParameterValues[0].numbers[1], 0.4F),
        "KBMAT-GRAPH-0303: Graph parameter value round-trip lost stable id/type/value");

    RenderMaterialTypeDocument refreshedType = GetBuiltInPbrMaterialTypeDocument();
    refreshedType.stableTypeId = "graph.surface";
    refreshedType.version = 3U;
    refreshedType.schema.typeName = refreshedType.stableTypeId;
    refreshedType.schema.typeVersion = refreshedType.version;
    refreshedType.schema.parameters = {
        RenderMaterialParameterSchema{
            .name = "tintColor",
            .displayName = "Tint Color",
            .type = RenderMaterialParameterType::Color,
            .group = RenderMaterialParameterGroup::Surface,
            .defaultValueHint = "1 1 1 1",
        },
        RenderMaterialParameterSchema{
            .name = "edgeWear",
            .displayName = "Edge Wear",
            .type = RenderMaterialParameterType::Scalar,
            .group = RenderMaterialParameterGroup::Surface,
            .range = RenderMaterialParameterRange{ .min = 0.0F, .max = 1.0F },
            .defaultValueHint = "0.25",
        },
        RenderMaterialParameterSchema{
            .name = "wear",
            .displayName = "Wear Color",
            .type = RenderMaterialParameterType::Color,
            .group = RenderMaterialParameterGroup::Surface,
            .defaultValueHint = "0.1 0.2 0.3 1",
        },
        RenderMaterialParameterSchema{
            .name = "roughnessFactor",
            .displayName = "Roughness",
            .type = RenderMaterialParameterType::Scalar,
            .group = RenderMaterialParameterGroup::Core,
            .defaultValueHint = "0.5",
        },
    };
    const RenderMaterialSchemaRefreshResult refresh = RefreshRenderMaterialGraphBackedMaterialSchema(*graphParameterLoaded.asset, refreshedType);
    Require(refresh.material.materialType == "graph.surface" && refresh.material.materialTypeVersion == 3U,
        "KBMAT-GRAPH-0303: Schema refresh did not update material type identity");
    Require(refresh.material.graphParameterValues.size() == 3U, "KBMAT-GRAPH-0303: Schema refresh should keep matching custom params, reset changed types and add new defaults");
    Require(refresh.material.graphParameterValues[0].stableId == "tintColor" && NearlyEqual(refresh.material.graphParameterValues[0].numbers[2], 0.6F),
        "KBMAT-GRAPH-0303: Schema refresh did not preserve matching stable parameter value");
    Require(refresh.material.graphParameterValues[1].stableId == "edgeWear" && NearlyEqual(refresh.material.graphParameterValues[1].numbers[0], 0.25F),
        "KBMAT-GRAPH-0303: Schema refresh did not add default for new graph parameter");
    Require(refresh.material.graphParameterValues[2].stableId == "wear" &&
            refresh.material.graphParameterValues[2].type == RenderMaterialParameterType::Color &&
            NearlyEqual(refresh.material.graphParameterValues[2].numbers[2], 0.3F),
        "KBMAT-GRAPH-0504: Schema refresh did not reset changed-type graph parameter to schema default");
    Require(std::ranges::any_of(refresh.diagnostics, [](const RenderMaterialSchemaRefreshDiagnostic& diagnostic) {
            return diagnostic.kind == RenderMaterialSchemaRefreshDiagnosticKind::RemovedUnknownParameter && diagnostic.stableId == "obsoleteParam";
        }),
        "KBMAT-GRAPH-0303: Schema refresh did not diagnose removed/unknown graph parameter values");
    Require(std::ranges::any_of(refresh.diagnostics, [](const RenderMaterialSchemaRefreshDiagnostic& diagnostic) {
            return diagnostic.kind == RenderMaterialSchemaRefreshDiagnosticKind::ChangedParameterType && diagnostic.stableId == "wear";
        }),
        "KBMAT-GRAPH-0504: Schema refresh did not diagnose changed graph parameter type");

    kb::assets::AssetManager dependencyManager;
    kb::assets::AssetMetadata graphMaterialMetadata{};
    graphMaterialMetadata.id = kb::assets::AssetId{ 0xA551U };
    graphMaterialMetadata.type = "RenderMaterial";
    graphMaterialMetadata.virtualPath = "/Game/Materials/GraphSurfaceUse.kbmat";
    const std::vector<kb::assets::AssetId> graphDependencies =
        RenderMaterialAssetLoader::DiscoverMaterialDependencies(*graphLoaded.asset, graphMaterialMetadata, dependencyManager.Registry());
    Require(ContainsDependency(graphDependencies, kb::assets::AssetId{ 0x4D545950U }), "KBMAT-GRAPH-0003: Material dependency discovery lost material type asset id");
    Require(ContainsDependency(graphDependencies, kb::assets::AssetId{ 0x4752415048U }), "KBMAT-GRAPH-0304: Material dependency discovery lost source graph asset id");

    std::istringstream shuffledInput{
        "normalTexture Textures/normal.kbtex\n"
        "doubleSided true\n"
        "materialTypeVersion 1\n"
        "roughnessFactor 0.25\n"
        "version 1\n"
        "baseColorTextureAssetId 10\n"
        "alphaCutoff 0.25\n"
        "emissiveStrength 4\n"
        "materialType builtin.pbr\n"
        "metallicFactor 0.5\n"
        "normalTextureAssetId 20\n"
        "baseColorFactor 0.25 0.5 0.75 1\n"
        "emissiveColor 0 0.25 0.5\n"
        "alphaMode MASK\n"
        "baseColorTexture Textures/base.kbtex\n"
        "normalScale 2\n"
        "emissiveTextureAssetId 50\n"
        "occlusionStrength 0.75\n"
    };
    const std::optional<RenderMaterialAssetData> shuffled = RenderMaterialAssetLoader::LoadMaterial(shuffledInput);
    Require(shuffled.has_value(), "Canonical material writer fixture did not parse");

    std::ostringstream canonicalOutput;
    RenderMaterialAssetWriter::Write(canonicalOutput, *shuffled);
    const std::string expectedCanonical =
        "# KB material\n"
        "version 1\n"
        "materialType builtin.pbr\n"
        "materialTypeVersion 1\n"
        "baseColor 0.25 0.5 0.75 1\n"
        "metallicFactor 0.5\n"
        "roughnessFactor 0.25\n"
        "normalScale 2\n"
        "occlusionStrength 0.75\n"
        "emissiveColor 0 0.25 0.5\n"
        "emissiveStrength 4\n"
        "alphaMode MASK\n"
        "alphaCutoff 0.25\n"
        "translucencyBlend ALPHA\n"
        "doubleSided true\n"
        "writesDepth true\n"
        "tiling 1 1\n"
        "offset 0 0\n"
        "albedoTextureAssetId 10\n"
        "normalTextureAssetId 20\n"
        "emissiveTextureAssetId 50\n"
        "albedoTexture Textures/base.kbtex\n"
        "normalTexture Textures/normal.kbtex\n"
        "clearcoatFactor 0\n"
        "clearcoatRoughnessFactor 0\n"
        "sheenColor 0 0 0\n"
        "sheenRoughnessFactor 0\n"
        "transmissionFactor 0\n"
        "thicknessFactor 0\n"
        "attenuationColor 1 1 1\n"
        "attenuationDistance 0\n"
        "subsurfaceColor 1 1 1\n"
        "subsurfaceFactor 0\n"
        "anisotropyStrength 0\n"
        "anisotropyRotation 0\n"
        "layerWeight 1\n"
        "decalBlendMode DISABLED\n"
        "layerBlendMode REPLACE\n"
        "graphVersion 3\n"
        "graphMaterialDomain surface\n"
        "graphShadingModel defaultLit\n"
        "graphBlendMode opaque\n"
        "graphStorageModel inline-kbmat\n"
        "graphDiagnosticSchemaVersion 1\n"
        "graphPersistCompileDiagnostics true\n"
        "graphArtifactFailurePolicy LastGoodThenErrorMaterial\n"
        "graphNode 1 MaterialOutput 640 240\n";
    Require(canonicalOutput.str() == expectedCanonical, "Material writer did not emit deterministic canonical field ordering");

    std::ostringstream canonicalOutputAgain;
    RenderMaterialAssetWriter::Write(canonicalOutputAgain, *shuffled);
    Require(canonicalOutputAgain.str() == canonicalOutput.str(), "Material writer emitted different output for the same material instance data");
}

void RunRenderMaterialAssetParserReportsReadableErrorsTest() {
    {
        std::istringstream input{
            "metallicFactor nope\n"
            "unknownMaterialField 1\n"
            "alphaMode TRANSPARENT\n"
        };
        const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
        Require(!result.asset.has_value(), "Invalid material parser diagnostics should not return an asset");
        Require(result.diagnostics.size() == 3U, "Invalid material parser diagnostics should report each invalid line");
        Require(result.diagnostics[0].code == RenderMaterialAssetParseDiagnosticCode::InvalidFloat, "Invalid material parser diagnostics lost the invalid float code");
        Require(result.diagnostics[0].line == 1U, "Invalid material parser diagnostics lost the invalid value line number");
        Require(!result.diagnostics[0].assetId.IsValid(), "Stream material parser diagnostics should not invent an asset id");
        Require(result.diagnostics[0].path.empty(), "Stream material parser diagnostics should not invent a file path");
        Require(result.diagnostics[0].field == "metallicFactor", "Invalid material parser diagnostics lost the invalid value field name");
        Require(result.diagnostics[0].message.find("Invalid float value") != std::string::npos, "Invalid material parser diagnostics did not identify an invalid float value");
        Require(result.diagnostics[1].code == RenderMaterialAssetParseDiagnosticCode::UnknownField, "Invalid material parser diagnostics lost the unknown field code");
        Require(result.diagnostics[1].line == 2U, "Invalid material parser diagnostics lost the unknown field line number");
        Require(result.diagnostics[1].field == "unknownMaterialField", "Invalid material parser diagnostics lost the unknown field name");
        Require(result.diagnostics[1].message.find("Unknown material field") != std::string::npos, "Invalid material parser diagnostics did not identify an unknown field");
        Require(result.diagnostics[2].code == RenderMaterialAssetParseDiagnosticCode::InvalidEnum, "Invalid material parser diagnostics lost the invalid enum code");
        Require(result.diagnostics[2].field == "alphaMode", "Invalid material parser diagnostics lost the invalid enum field name");
        Require(result.diagnostics[2].message.find("Invalid enum value") != std::string::npos, "Invalid material parser diagnostics did not identify an invalid enum value");
        Require(result.ErrorMessage().find("code invalid_float") != std::string::npos, "Invalid material parser error message did not include the invalid float diagnostic code");
        Require(result.ErrorMessage().find("code invalid_enum") != std::string::npos, "Invalid material parser error message did not include the invalid enum diagnostic code");
        Require(result.ErrorMessage().find("line 1") != std::string::npos, "Invalid material parser error message did not include line numbers");
        Require(result.ErrorMessage().find("metallicFactor nope") != std::string::npos, "Invalid material parser error message did not include source text");
    }
    {
        std::istringstream input{
            "metallicFactor nan\n"
            "tiling inf 1\n"
            "baseColor 1 1 1 1 trailing\n"
            "graphParameterValue unsafe Scalar 1 trailing\n"
        };
        const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
        Require(!result.Succeeded() && result.HasErrors() && !result.asset.has_value(),
            "P2.9: non-finite or non-exact material numeric fields must be fatal");
        Require(result.diagnostics.size() == 4U,
            "P2.9: parser must diagnose every non-finite, trailing or excess numeric field");
        Require(result.diagnostics[0].field == "metallicFactor" &&
                result.diagnostics[1].field == "tiling" &&
                result.diagnostics[2].field == "baseColor" &&
                result.diagnostics[3].field == "graphParameterValue",
            "P2.9: numeric parser diagnostics must identify the rejected production fields");
    }
    {
        std::istringstream input{
            "baseColor 1.2 0.5 0.5 1\n"
            "roughnessFactor -0.1\n"
            "normalScale 9\n"
        };
        const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
        Require(!result.asset.has_value(), "Out-of-range material parser diagnostics should not return an asset");
        Require(result.diagnostics.size() == 3U, "Out-of-range material parser diagnostics should report each invalid range");
        Require(result.diagnostics[0].code == RenderMaterialAssetParseDiagnosticCode::OutOfRange, "Out-of-range material parser diagnostics lost base color code");
        Require(result.diagnostics[0].field == "baseColor", "Out-of-range material parser diagnostics lost base color field");
        Require(result.diagnostics[1].code == RenderMaterialAssetParseDiagnosticCode::OutOfRange, "Out-of-range material parser diagnostics lost roughness code");
        Require(result.diagnostics[1].field == "roughnessFactor", "Out-of-range material parser diagnostics lost roughness field");
        Require(result.diagnostics[2].code == RenderMaterialAssetParseDiagnosticCode::OutOfRange, "Out-of-range material parser diagnostics lost normal scale code");
        Require(result.ErrorMessage().find("code out_of_range") != std::string::npos, "Out-of-range material parser error message did not include the diagnostic code");
    }
    {
        std::istringstream input{
            "baseColor 0.25 0.5 0.75 1\n"
            "clearcoatFactor 0.5\n"
            "transmissionTexture Textures/transmission.kbtex\n"
            "decalBlendMode PBR\n"
        };
        const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
        Require(result.asset.has_value(), "Unsupported advanced material field warnings should keep the parsed asset available");
        Require(result.Succeeded() && result.HasWarnings(), "Unsupported advanced material field warnings should keep the asset loadable and diagnostics visible");
        Require(result.diagnostics.size() == 3U, "Unsupported advanced material field diagnostics should report each active advanced field");
        Require(result.diagnostics[0].code == RenderMaterialAssetParseDiagnosticCode::UnsupportedAdvancedField, "Unsupported advanced material diagnostics lost clearcoat code");
        Require(result.diagnostics[0].severity == RenderMaterialAssetParseDiagnosticSeverity::Warning, "Unsupported advanced material diagnostics should be warnings");
        Require(result.diagnostics[0].field == "clearcoatFactor", "Unsupported advanced material diagnostics lost clearcoat field");
        Require(result.diagnostics[1].code == RenderMaterialAssetParseDiagnosticCode::UnsupportedAdvancedField, "Unsupported advanced material diagnostics lost transmission texture code");
        Require(result.diagnostics[1].field == "transmissionTexture", "Unsupported advanced material diagnostics lost transmission texture field");
        Require(result.diagnostics[2].code == RenderMaterialAssetParseDiagnosticCode::UnsupportedAdvancedField, "Unsupported advanced material diagnostics lost decal code");
        Require(result.diagnostics[2].field == "decalBlendMode", "Unsupported advanced material diagnostics lost decal field");
        Require(result.DiagnosticMessage().find("code unsupported_advanced_field") != std::string::npos, "Unsupported advanced material diagnostics did not include the warning code");
    }
    {
        std::istringstream input{
            "version nope\n"
            "version 999\n"
            "baseColor 1 1 1 1\n"
        };
        const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
        Require(!result.asset.has_value(), "Invalid material document version diagnostics should not return an asset");
        Require(result.diagnostics.size() == 2U, "Invalid material document version diagnostics should report each invalid version line");
        Require(result.diagnostics[0].code == RenderMaterialAssetParseDiagnosticCode::InvalidDocumentVersion, "Invalid material document version diagnostics lost invalid version code");
        Require(result.diagnostics[0].line == 1U && result.diagnostics[0].field == "version", "Invalid material document version diagnostics lost line or field");
        Require(result.diagnostics[0].message.find("Invalid value") != std::string::npos, "Invalid material document version diagnostics did not identify invalid value");
        Require(result.diagnostics[1].code == RenderMaterialAssetParseDiagnosticCode::UnsupportedDocumentVersion, "Invalid material document version diagnostics lost unsupported version code");
        Require(result.diagnostics[1].message.find("Unsupported material document version") != std::string::npos, "Invalid material document version diagnostics did not identify unsupported future version");
    }
    {
        std::istringstream input{
            "materialType raw.graph\n"
            "materialTypeVersion 2\n"
            "baseColor 1 1 1 1\n"
        };
        const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
        Require(!result.asset.has_value(), "Unsupported material type diagnostics should not return an asset");
        Require(result.diagnostics.size() == 2U, "Unsupported material type diagnostics should report type and version failures");
        Require(result.diagnostics[0].code == RenderMaterialAssetParseDiagnosticCode::UnsupportedMaterialType, "Unsupported material type diagnostics lost type code");
        Require(result.diagnostics[0].line == 1U && result.diagnostics[0].field == "materialType", "Unsupported material type diagnostics lost line or field");
        Require(result.diagnostics[0].message.find("Unsupported material type") != std::string::npos, "Unsupported material type diagnostics did not identify the type");
        Require(result.diagnostics[1].code == RenderMaterialAssetParseDiagnosticCode::UnsupportedMaterialTypeVersion, "Unsupported material type diagnostics lost version code");
        Require(result.diagnostics[1].field == "materialTypeVersion", "Unsupported material type diagnostics lost the version field");
        Require(result.diagnostics[1].message.find("Unsupported material type version") != std::string::npos, "Unsupported material type diagnostics did not identify the version");
    }
    {
        std::istringstream input{
            "materialTypeVersion nope\n"
            "materialTypeVersion 999\n"
            "baseColor 1 1 1 1\n"
        };
        const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
        Require(!result.asset.has_value(), "Invalid material type version diagnostics should not return an asset");
        Require(result.diagnostics.size() == 2U, "Invalid material type version diagnostics should report each invalid line");
        Require(result.diagnostics[0].code == RenderMaterialAssetParseDiagnosticCode::InvalidMaterialTypeVersion, "Invalid material type version diagnostics lost invalid version code");
        Require(result.diagnostics[0].field == "materialTypeVersion", "Invalid material type version diagnostics lost the field name");
        Require(result.diagnostics[0].message.find("Invalid value") != std::string::npos, "Invalid material type version diagnostics did not identify invalid value");
        Require(result.diagnostics[1].code == RenderMaterialAssetParseDiagnosticCode::UnsupportedMaterialTypeVersion, "Invalid material type version diagnostics lost unsupported version code");
        Require(result.diagnostics[1].message.find("Unsupported material type version") != std::string::npos, "Invalid material type version diagnostics did not identify unsupported future version");
    }
    {
        std::istringstream input{
            "baseColor 0.25 0.5 0.75 1\n"
        };
        const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
        Require(result.asset.has_value(), "Unversioned legacy material asset should remain loadable");
        Require(result.asset->documentVersion == kRenderMaterialAssetDocumentVersion, "Unversioned legacy material asset should use current in-memory document version");
        Require(!result.asset->hasExplicitDocumentVersion, "Unversioned legacy material asset should record missing explicit document version metadata");
        Require(result.asset->materialType == kRenderMaterialAssetBuiltInPbrType, "Legacy material asset should use built-in PBR material type");
        Require(result.asset->materialTypeVersion == kRenderMaterialAssetBuiltInPbrTypeVersion, "Legacy material asset should use current built-in PBR material type version");
        Require(!result.asset->hasExplicitMaterialType, "Legacy material asset should record missing explicit material type metadata");
        Require(!result.asset->hasExplicitMaterialTypeVersion, "Legacy material asset should record missing explicit material type version metadata");
    }
    {
        // KBMAT-0112: a legacy unversioned .kbmat migrates to a versioned Material Instance on save.
        std::istringstream legacyInput{
            "baseColor 0.25 0.5 0.75 1\n"
            "metallicFactor 0.3\n"
            "roughnessFactor 0.7\n"
        };
        const RenderMaterialAssetParseResult legacy = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(legacyInput);
        Require(legacy.asset.has_value(), "Legacy material asset should load for migration");
        Require(!legacy.asset->hasExplicitDocumentVersion, "Legacy material asset should lack explicit document version before migration");
        Require(!legacy.asset->hasExplicitMaterialType, "Legacy material asset should lack explicit material type before migration");
        Require(!legacy.asset->hasExplicitMaterialTypeVersion, "Legacy material asset should lack explicit material type version before migration");

        const std::filesystem::path tmpFile = std::filesystem::temp_directory_path() / "kbmat_legacy_migration.kbmat";
        Require(RenderMaterialAssetWriter::Save(tmpFile, *legacy.asset), "Migrated material save should succeed");

        const RenderMaterialAssetParseResult migrated = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(tmpFile);
        Require(migrated.asset.has_value(), "Migrated material asset should load after save");
        Require(migrated.Succeeded(), "Migrated material asset should have no diagnostics after explicit version and type are written");
        Require(migrated.asset->hasExplicitDocumentVersion, "Migrated material asset should record explicit document version");
        Require(migrated.asset->documentVersion == kRenderMaterialAssetDocumentVersion, "Migrated material asset should use the current document version");
        Require(migrated.asset->hasExplicitMaterialType, "Migrated material asset should record explicit material type");
        Require(migrated.asset->materialType == kRenderMaterialAssetBuiltInPbrType, "Migrated material asset should use the built-in PBR material type");
        Require(migrated.asset->hasExplicitMaterialTypeVersion, "Migrated material asset should record explicit material type version");
        Require(migrated.asset->materialTypeVersion == kRenderMaterialAssetBuiltInPbrTypeVersion, "Migrated material asset should use the current built-in PBR material type version");
        Require(NearlyEqual(migrated.asset->desc.baseColor[2], 0.75F), "Migrated material asset should preserve base color");
        Require(NearlyEqual(migrated.asset->desc.metallicFactor, 0.3F), "Migrated material asset should preserve metallic factor");
        Require(NearlyEqual(migrated.asset->desc.roughnessFactor, 0.7F), "Migrated material asset should preserve roughness factor");

        std::error_code removeError;
        std::filesystem::remove(tmpFile, removeError);
    }
    {
        std::istringstream input{ "# comment only\n\n" };
        const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
        Require(!result.asset.has_value(), "Empty material parser diagnostics should not return an asset");
        Require(result.diagnostics.size() == 1U, "Empty material parser diagnostics should report one error");
        Require(result.diagnostics[0].code == RenderMaterialAssetParseDiagnosticCode::EmptyDocument, "Empty material parser diagnostics lost empty document code");
        Require(result.diagnostics[0].message.find("does not contain any material properties") != std::string::npos, "Empty material parser diagnostics should explain the missing material properties");
    }
    {
        const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_material_diagnostics";
        std::error_code error;
        std::filesystem::remove_all(root, error);
        std::filesystem::create_directories(root, error);
        Require(!error, "Material diagnostics test could not create temp root");

        const std::filesystem::path materialPath = root / "broken.kbmat";
        {
            std::ofstream output{ materialPath, std::ios::trunc };
            output << "roughnessFactor broken\n";
        }

        const kb::assets::AssetId materialAssetId{ 77U };
        const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(materialPath, materialAssetId);
        Require(!result.asset.has_value(), "Path material parser diagnostics should not return an asset for invalid data");
        Require(result.diagnostics.size() == 1U, "Path material parser diagnostics should report the invalid field");
        Require(result.diagnostics[0].code == RenderMaterialAssetParseDiagnosticCode::InvalidFloat, "Path material parser diagnostics lost invalid float code");
        Require(result.diagnostics[0].assetId == materialAssetId, "Path material parser diagnostics lost asset id context");
        Require(result.diagnostics[0].path == materialPath, "Path material parser diagnostics lost file path context");
        Require(result.ErrorMessage().find("asset 77") != std::string::npos, "Path material parser error message did not include asset id context");
        Require(result.ErrorMessage().find(materialPath.generic_string()) != std::string::npos, "Path material parser error message did not include file path context");

        const std::filesystem::path missingPath = root / "missing.kbmat";
        const RenderMaterialAssetParseResult missing = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(missingPath, materialAssetId);
        Require(!missing.asset.has_value(), "Missing material file diagnostics should not return an asset");
        Require(missing.diagnostics.size() == 1U, "Missing material file diagnostics should report one error");
        Require(missing.diagnostics[0].code == RenderMaterialAssetParseDiagnosticCode::FileOpenFailed, "Missing material file diagnostics lost file open failure code");
        Require(missing.diagnostics[0].assetId == materialAssetId, "Missing material file diagnostics lost asset id context");
        Require(missing.diagnostics[0].path == missingPath, "Missing material file diagnostics lost file path context");

        std::filesystem::remove_all(root, error);
    }
}

void RunRenderTextureAssetLoaderDiscoversAndLoadsTextureThroughAssetManagerTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_texture_asset_loader";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Render texture asset loader test could not create temp root");

    const std::filesystem::path texturePath = root / "blue.kbtex";
    {
        std::ofstream output{ texturePath, std::ios::trunc };
        output
            << "size 2 2\n"
            << "rgba8 10 20 30 255\n";
    }

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderTextureAssetLoader>()), "AssetManager rejected RenderTextureAssetLoader");
    Require(manager.Mounts().Mount("Game", root), "AssetManager could not mount texture asset test root");
    Require(manager.DiscoverMountedAssets() == 1U, "AssetManager did not discover the texture asset");

    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath("/Game/blue.kbtex");
    Require(metadata != nullptr && metadata->type == "RenderTexture", "AssetManager registered the texture with the wrong type");
    const kb::assets::AssetHandle<RenderTextureAssetData> asset = manager.Load<RenderTextureAssetData>(metadata->id);
    Require(asset.IsLoaded(), "AssetManager did not load RenderTextureAssetData through RenderTextureAssetLoader");
    Require(asset->width == 2U && asset->height == 2U, "Loaded texture did not preserve dimensions");
    Require(asset->rgba8.size() == 16U, "Loaded texture did not allocate RGBA8 pixels");
    Require(asset->rgba8[0] == 10U && asset->rgba8[1] == 20U && asset->rgba8[2] == 30U && asset->rgba8[3] == 255U, "Loaded texture did not preserve RGBA8 fill color");

    const std::string snapshotText = "size 1 1\nrgba8 90 80 70 255\n";
    const std::vector<std::uint8_t> snapshotBytes{ snapshotText.begin(), snapshotText.end() };
    RenderTextureAssetLoader snapshotLoader;
    kb::assets::AssetMetadata snapshotMetadata = *metadata;
    snapshotMetadata.sourceExtension = ".kbtex";
    const kb::assets::AssetLoadResult snapshot = snapshotLoader.Load(kb::assets::AssetLoadRequest{
        .metadata = snapshotMetadata,
        .resolvedPath = root / "missing.kbtex",
        .sourceBytes = std::span<const std::uint8_t>{ snapshotBytes },
    });
    Require(snapshot.Succeeded(), "Render texture loader did not consume an immutable source snapshot");
    const auto snapshotTexture = std::static_pointer_cast<RenderTextureAssetData>(snapshot.asset);
    Require(snapshotTexture->width == 1U && snapshotTexture->height == 1U &&
            snapshotTexture->rgba8 == std::vector<std::uint8_t>({ 90U, 80U, 70U, 255U }),
        "Render texture loader reread the filesystem instead of its source snapshot");

    std::filesystem::remove_all(root, error);
}

[[nodiscard]] std::filesystem::path ResolveFixturePath(const std::filesystem::path& relativePath) {
    std::filesystem::path current = std::filesystem::current_path();
    while (!current.empty()) {
        const std::filesystem::path candidate = current / relativePath;
        if (std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }

        const std::filesystem::path parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }

    return {};
}

void RunRenderTextureAssetLoaderLoadsImportedTextureContainerTest() {
    const std::filesystem::path imagePath = ResolveFixturePath("third_party/bgfx.cmake/bgfx/examples/runtime/images/SplashScreen.png");
    Require(!imagePath.empty(), "Imported texture PNG fixture was not found");

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_imported_texture_asset_loader";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Imported texture loader test could not create temp root");

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<kb::assets::ImportedAssetLoader>()), "Imported texture loader test could not register imported asset loader");
    Require(manager.Mounts().Mount("Game", root), "Imported texture loader test could not mount temp root");

    const std::array sourceFiles{ imagePath };
    const kb::assets::AssetImportResult imported = kb::assets::AssetImportService::ImportFiles(manager, sourceFiles, "/Game");
    Require(imported.Succeeded() && imported.items.size() == 1U, "PNG fixture did not import into a .21kb texture container");

    const kb::assets::AssetImportItemResult& item = imported.items.front();
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(item.id);
    Require(metadata != nullptr && metadata->type == "ImportedAsset" && metadata->importCategory == "Texture",
        "Imported PNG did not register as an imported texture container");

    const std::optional<RenderTextureAssetData> texture = RenderTextureAssetLoader::LoadTexture(item.assetPhysicalPath);
    Require(texture.has_value(), "RenderTextureAssetLoader did not decode the imported .21kb texture payload");
    Require(texture->width > 0U && texture->height > 0U, "Imported .21kb texture decoded with invalid dimensions");
    Require(texture->rgba8.size() == static_cast<std::size_t>(texture->width) * static_cast<std::size_t>(texture->height) * 4U,
        "Imported .21kb texture was not converted to RGBA8");

    std::filesystem::remove_all(root, error);
}

void RunRenderTextureAssetLoaderLoadsImageThroughAssetManagerTest(
    const std::filesystem::path& relativePath,
    const std::filesystem::path& virtualPath,
    const char* label) {
    const std::filesystem::path imagePath = ResolveFixturePath(relativePath);
    Require(!imagePath.empty(), label);

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderTextureAssetLoader>()), "AssetManager rejected RenderTextureAssetLoader for image");
    Require(manager.Mounts().Mount("Game", imagePath.parent_path()), "AssetManager could not mount image fixture root");
    Require(manager.DiscoverMountedAssets() > 0U, "AssetManager did not discover image fixture assets");

    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(virtualPath);
    Require(metadata != nullptr && metadata->type == "RenderTexture", "AssetManager registered image with the wrong type");
    const kb::assets::AssetHandle<RenderTextureAssetData> asset = manager.Load<RenderTextureAssetData>(metadata->id);
    Require(asset.IsLoaded(), "AssetManager did not load image through RenderTextureAssetLoader");
    Require(asset->width > 0U && asset->height > 0U, "Loaded image has invalid dimensions");
    Require(asset->dimension == RenderTextureDimension::Texture2D && asset->depth == 1U && asset->layers == 1U,
        "Legacy image asset must remain a 2D texture");
    Require(asset->rgba8.size() == static_cast<std::size_t>(asset->width) * static_cast<std::size_t>(asset->height) * 4U,
        "Loaded image did not preserve exactly the complete RGBA8 LOD0 payload");
    Require(asset->mipCount == 1U,
        "Legacy image loading must keep its release-safe LOD0-only runtime contract");
}

void RunRenderTextureAssetLoaderLoadsPngJpgAndDdsThroughAssetManagerTest() {
    RunRenderTextureAssetLoaderLoadsImageThroughAssetManagerTest(
        "third_party/bgfx.cmake/bgfx/examples/runtime/images/SplashScreen.png",
        "/Game/SplashScreen.png",
        "Render texture PNG fixture was not found");
    RunRenderTextureAssetLoaderLoadsImageThroughAssetManagerTest(
        "third_party/bgfx.cmake/bgfx/examples/runtime/images/image1.jpg",
        "/Game/image1.jpg",
        "Render texture JPG fixture was not found");
    RunRenderTextureAssetLoaderLoadsImageThroughAssetManagerTest(
        "third_party/bgfx.cmake/bgfx/examples/runtime/textures/fieldstone-rgba.dds",
        "/Game/fieldstone-rgba.dds",
        "Render texture DDS fixture was not found");
}

// The decoded-texture cache: a file must be decoded to RGBA8 at most once while unchanged on disk, so the many
// scenes that reference the same texture (main scene + material preview + thumbnails) stop each paying the full
// bimg decode - the multi-second stall on opening a Material Editor. Proven via the decode counter, with a
// changed-file negative control that must re-decode (hot reload stays correct).
void RunRenderTextureAssetLoaderDecodeCacheTest() {
    const std::filesystem::path pngFixture = ResolveFixturePath("third_party/bgfx.cmake/bgfx/examples/runtime/images/SplashScreen.png");
    const std::filesystem::path ddsFixture = ResolveFixturePath("third_party/bgfx.cmake/bgfx/examples/runtime/textures/fieldstone-rgba.dds");
    Require(!pngFixture.empty() && !ddsFixture.empty(), "Decode cache test fixtures (png/dds) were not found");

    std::error_code error;
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_texture_decode_cache";
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Decode cache test could not create temp root");
    // Staged copy so the file can be mutated without touching the shared fixture. bimg sniffs format from the
    // bytes, so the .png name is irrelevant once we overwrite it with DDS content below.
    const std::filesystem::path texturePath = root / "cached.png";
    std::filesystem::copy_file(pngFixture, texturePath, std::filesystem::copy_options::overwrite_existing, error);
    Require(!error, "Decode cache test could not stage the png fixture");

    const std::uint64_t before = RenderTextureAssetLoader::DebugDecodeCount();
    const std::optional<RenderTextureAssetData> first = RenderTextureAssetLoader::LoadTexture(texturePath);
    Require(first.has_value() && !first->rgba8.empty(), "Decode cache: first load decoded the texture");
    const std::uint64_t afterFirst = RenderTextureAssetLoader::DebugDecodeCount();
    Require(afterFirst == before + 1U, "Decode cache: a first, never-seen file performs exactly one real decode");

    const std::optional<RenderTextureAssetData> second = RenderTextureAssetLoader::LoadTexture(texturePath);
    Require(second.has_value(), "Decode cache: second load returned data");
    Require(RenderTextureAssetLoader::DebugDecodeCount() == afterFirst,
        "Decode cache: a repeated load of an unchanged file must be served from cache, not re-decoded");
    Require(second->width == first->width && second->height == first->height && second->rgba8 == first->rgba8,
        "Decode cache: the cached texture bytes are identical to the freshly decoded ones");

    // Negative control: overwrite with a different image (different byte size => different freshness key). The
    // entry must be invalidated and the new content decoded, proving the cache is not a permanent freeze.
    std::filesystem::copy_file(ddsFixture, texturePath, std::filesystem::copy_options::overwrite_existing, error);
    Require(!error, "Decode cache: could not overwrite the staged texture with a different image");
    const std::uint64_t beforeReload = RenderTextureAssetLoader::DebugDecodeCount();
    const std::optional<RenderTextureAssetData> reloaded = RenderTextureAssetLoader::LoadTexture(texturePath);
    Require(reloaded.has_value() && !reloaded->rgba8.empty(), "Decode cache: reloaded the changed file");
    Require(RenderTextureAssetLoader::DebugDecodeCount() == beforeReload + 1U,
        "Decode cache negative control: a changed file is re-decoded, never served stale");

    std::filesystem::remove_all(root, error);
}

// Texture streaming: the render thread must never block on a first-time decode. TryAcquireDecodedTexture
// returns null immediately for an unseen texture (no synchronous decode = no 1-2s freeze on texture pick), and
// a background worker streams it into the cache so a later frame binds it.
void RunRenderTextureAsyncDecodeStreamsInTest() {
    const std::filesystem::path pngFixture = ResolveFixturePath("third_party/bgfx.cmake/bgfx/examples/runtime/images/SplashScreen.png");
    Require(!pngFixture.empty(), "Async decode test fixture (png) was not found");
    std::error_code error;
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_async_decode";
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path texturePath = root / "async_stream.png";
    std::filesystem::copy_file(pngFixture, texturePath, std::filesystem::copy_options::overwrite_existing, error);
    Require(!error, "Async decode test could not stage the fixture");

    Require(RenderTextureAssetLoader::TryAcquireDecodedTexture(texturePath) == nullptr,
        "Async decode: an unseen texture is NOT available synchronously (the render thread does not block-decode it)");
    RenderTextureAssetLoader::RequestAsyncTextureDecode(texturePath);
    std::shared_ptr<const RenderTextureAssetData> streamed;
    for (int attempt = 0; attempt < 500 && streamed == nullptr; ++attempt) {
        streamed = RenderTextureAssetLoader::TryAcquireDecodedTexture(texturePath);
        if (streamed == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    Require(streamed != nullptr && streamed->width > 0U && !streamed->rgba8.empty(),
        "Async decode: the background worker streams the texture into the cache so a later frame can bind it");
    std::filesystem::remove_all(root, error);
}

void RunRenderTextureAssetLoaderPreservesBimgCubeMetadataTest() {
    const std::filesystem::path cubePath = ResolveFixturePath("third_party/bgfx.cmake/bgfx/examples/runtime/textures/uffizi.ktx");
    Require(!cubePath.empty(), "Render texture cube KTX fixture was not found");
    const std::optional<RenderTextureAssetData> cube = RenderTextureAssetLoader::LoadTexture(cubePath);
    Require(cube.has_value(), "Render texture loader did not decode the cube KTX fixture");
    Require(cube->dimension == RenderTextureDimension::TextureCube && cube->width == cube->height &&
            cube->depth == 1U && cube->layers == 1U,
        "Render texture loader did not derive TextureCube from bimg metadata");
    Require(cube->mipCount == 1U &&
            cube->rgba8.size() == static_cast<std::size_t>(cube->width) * cube->height * 6U * 4U,
        "Render texture loader did not preserve exactly LOD0 for all cube faces");
}

void RunMeshAssetDataKeepsUint32IndicesForLargeMeshesTest() {
    RenderMeshAssetData asset{};
    asset.vertices.resize(70'000U);
    asset.indices32 = { 0U, 65'536U, 69'999U };
    asset.RefreshDesc();
    Require(asset.desc.indexFormat == RenderIndexFormat::Uint32, "RenderMeshAssetData did not expose uint32 indices for a large mesh");
    Require(asset.desc.indices32 != nullptr && asset.desc.indices == nullptr, "RenderMeshAssetData exposed the wrong index pointer for uint32 mesh data");
}

void RunSceneRendererTicksRegistryDeferredDestroyTest() {
    SceneRenderer renderer;
    RenderMaterialDesc desc{};
    const RenderMaterialHandle first = renderer.Resources().RegisterMaterial(desc);
    Require(first.IsValid(), "SceneRenderer resource registry did not allocate a material");
    renderer.ResourceMap().BindMaterial(7U, first);
    Require(renderer.ResourceMap().ResolveMaterial(7U).IsValid(), "SceneRenderer resource map did not bind a material before destroy");

    renderer.Resources().DestroyMaterial(first);
    Require(renderer.Resources().Stats().pendingDestroyCount == 1U, "Destroyed material did not enter SceneRenderer deferred queue");

    renderer.TickFrame();
    Require(!renderer.ResourceMap().ResolveMaterial(7U).IsValid(), "SceneRenderer did not prune stale material binding after destroy");

    for (int frame = 0; frame < 4; ++frame) {
        renderer.TickFrame();
    }

    Require(renderer.Resources().Stats().pendingDestroyCount == 0U, "SceneRenderer did not tick resource registry deferred destroy");
    const RenderMaterialHandle second = renderer.Resources().RegisterMaterial(desc);
    Require(second.Index() == first.Index(), "SceneRenderer registry did not reuse a drained material slot");
    Require(second.Generation() != first.Generation(), "SceneRenderer registry reused a material slot without generation bump");
}

void RunRendererTypedAssetReferenceSaveRoundTripTest() {
    static_assert(std::is_same_v<MeshRef, kb::library::AssetRef<RenderMeshAssetData>>);
    static_assert(std::is_same_v<MaterialRef, kb::library::AssetRef<RenderMaterialAssetData>>);
    static_assert(std::is_same_v<TextureRef, kb::library::AssetRef<RenderTextureAssetData>>);

    const MeshRef mesh{ kb::assets::AssetId{ 201U }, std::shared_ptr<const RenderMeshAssetData>{} };
    const MaterialRef material{ kb::assets::AssetId{ 202U }, std::shared_ptr<const RenderMaterialAssetData>{} };
    const TextureRef texture{ kb::assets::AssetId{ 203U }, std::shared_ptr<const RenderTextureAssetData>{} };
    kb::save::SaveGame save;
    save.SetAssetRef("mesh", mesh.Id());
    save.SetAssetRef("material", material.Id());
    save.SetAssetRef("texture", texture.Id());

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "21kb_renderer_typed_asset_refs.kbsave";
    Require(kb::save::SaveGameService::Save(path, save), "Renderer typed asset refs could not be serialized");
    const kb::save::SaveGameLoadResult loaded = kb::save::SaveGameService::Load(path);
    kb::assets::AssetId loadedMesh;
    kb::assets::AssetId loadedMaterial;
    kb::assets::AssetId loadedTexture;
    Require(loaded.Succeeded() &&
            loaded.save.GetAssetRef("mesh", loadedMesh) && loadedMesh == mesh.Id() &&
            loaded.save.GetAssetRef("material", loadedMaterial) && loadedMaterial == material.Id() &&
            loaded.save.GetAssetRef("texture", loadedTexture) && loadedTexture == texture.Id(),
        "Renderer mesh/material/texture refs did not round-trip their stable AssetIds");
    std::error_code removeError;
    std::filesystem::remove(path, removeError);
}

} // namespace

void RunRuntimeAssetShaderProviderTests() {
    RunRuntimeShaderProviderRejectsTrailingQualifierFieldTest();
}

void RunPackagedMaterialRuntimeTests() {
    RunPackagedMaterialDependencyMemoryTest();
}

void RunRenderResourceRegistryTests() {
    RunMaterialHandlesAreGenerationalTest();
    RunGraphBlendModeDrivesResourceRenderStateTest();
    RunMaterialReloadInvalidatesStaleSceneBindingTest();
    RunInvalidHandlesAreIgnoredTest();
    RunShutdownInvalidatesLiveHandlesTest();
    RunReserveAndStatsReportPoolPressureTest();
    RunStaticMeshVertexFormatsExposeExpectedStridesTest();
    RunMeshResourceBuildsSectionLocalVertexRangesTest();
    RunSkinnedMeshRegistrationContractTest();
    RunSkeletalMeshLodResourceUsesStablePaletteTest();
    RunSkeletalMeshValidatedBuildMatchesCheckedBuildTest();
    RunSkinningPaletteAllocatorLifetimeTest();
    RunObjImporterBuildsRenderMeshDescWithSectionsAndSlotsTest();
    RunObjImporterGeneratesMissingNormalsTest();
    RunFbxImporterBuildsSectionsForMaterialSlotsTest();
    RunFbxImporterStopsAtFooterAfterNullTerminatorTest();
    RunRenderMeshAssetLoaderDiscoversAndLoadsObjThroughAssetManagerTest();
    RunRenderMeshAssetLoaderLoadsImportedObjContainerTest();
    RunRenderMeshAssetLoaderLoadsWorkspaceImportedFbxCubeWhenPresentTest();
    RunRenderMeshAssetLoaderDiscoversAndLoadsGltfThroughAssetManagerTest();
    RunGltfImporterKeepsDefaultUvTransformForUnsupportedTextureRotationTest();
    RunGltfImporterBlocksTraversalTextureUrisTest();
    RunRuntimeMaterialResolverBlocksTraversalTexturePathsTest();
    RunGltfImporterRejectsOutOfRangeIndicesTest();
    RunGltfImporterKeepsUint32IndicesForLargeTangentMeshesTest();
    RunRenderMaterialAssetLoaderDiscoversAndLoadsMaterialThroughAssetManagerTest();
    RunMaterialAssetDiscoveryBuildsMaterialDependencyGraphTest();
    RunMaterialGraphAndTypeAssetDiscoveryTest();
    RunPackagedMaterialDependencyMemoryTest();
    RunStandaloneMaterialGraphCodecParityAndAtomicSaveTest();
    RunMaterialTypeReferenceValidationDrivesRuntimeErrorMaterialTest();
    RunMaterialCookPayloadContainsParamsTextureDepsTypeVersionAndHashTest();
    RunRenderMaterialAssetWriterRoundTripsThroughParserTest();
    RunRenderMaterialAssetParserReportsReadableErrorsTest();
    RunRenderTextureColorSpaceDescTest();
    RunRuntimeTextureMipChainGenerationTest();
    RunRenderTextureTextAssetPreservesDimensionTest();
    RunRenderTextureAssetLoaderDiscoversAndLoadsTextureThroughAssetManagerTest();
    RunRenderTextureAssetLoaderLoadsImportedTextureContainerTest();
    RunRenderTextureAssetLoaderLoadsPngJpgAndDdsThroughAssetManagerTest();
    RunRenderTextureAssetLoaderDecodeCacheTest();
    RunRenderTextureAsyncDecodeStreamsInTest();
    RunRenderTextureAssetLoaderPreservesBimgCubeMetadataTest();
    RunMeshAssetDataKeepsUint32IndicesForLargeMeshesTest();
    RunSceneRendererTicksRegistryDeferredDestroyTest();
    RunRendererTypedAssetReferenceSaveRoundTripTest();

}

} // namespace kb::render::tests
