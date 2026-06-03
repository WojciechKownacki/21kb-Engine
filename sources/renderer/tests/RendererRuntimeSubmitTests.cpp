#include "RendererTestSupport.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <vector>

namespace kb::render::tests {
namespace {

class HeadlessSurface final : public RenderSurface {
public:
    [[nodiscard]] std::uint32_t Width() const noexcept override {
        return 64U;
    }

    [[nodiscard]] std::uint32_t Height() const noexcept override {
        return 64U;
    }

    [[nodiscard]] void* NativeWindowHandle() const noexcept override {
        return nullptr;
    }

    [[nodiscard]] void* NativeDisplayHandle() const noexcept override {
        return nullptr;
    }
};

[[nodiscard]] kb::scene::TransformComponent TransformAt(float x, float y, float z) {
    return kb::scene::TransformComponent{
        .localPosition = kb::scene::Vec3{ x, y, z },
        .worldPosition = kb::scene::Vec3{ x, y, z },
        .worldDirty = false,
    };
}

[[nodiscard]] SceneRenderCamera IdentityCamera() noexcept {
    return SceneRenderCamera{
        .view = {
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F,
        },
        .projection = {
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F,
        },
    };
}

void WriteTriangleObj(const std::filesystem::path& path) {
    std::ofstream output{ path, std::ios::trunc };
    output
        << "v -0.1 -0.1 0.0\n"
        << "v 0.1 -0.1 0.0\n"
        << "v 0.0 0.1 0.0\n"
        << "vt 0 0\n"
        << "vt 1 0\n"
        << "vt 0.5 1\n"
        << "vn 0 0 1\n"
        << "f 1/1/1 2/2/1 3/3/1\n";
}

void WriteTexture(const std::filesystem::path& path, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    std::ofstream output{ path, std::ios::trunc };
    output
        << "size 1 1\n"
        << "rgba8 "
        << static_cast<std::uint32_t>(r) << " "
        << static_cast<std::uint32_t>(g) << " "
        << static_cast<std::uint32_t>(b) << " 255\n";
}

void WriteEmbeddedMaterialTriangleGltf(const std::filesystem::path& root) {
    const std::filesystem::path binPath = root / "embedded_mesh.bin";
    {
        const std::vector<float> positions{
            -0.1F, -0.1F, 0.0F,
            0.1F, -0.1F, 0.0F,
            0.0F, 0.1F, 0.0F,
        };
        const std::vector<float> normals{
            0.0F, 0.0F, 1.0F,
            0.0F, 0.0F, 1.0F,
            0.0F, 0.0F, 1.0F,
        };
        const std::vector<float> tangents{
            1.0F, 0.0F, 0.0F, 1.0F,
            1.0F, 0.0F, 0.0F, 1.0F,
            1.0F, 0.0F, 0.0F, 1.0F,
        };
        const std::vector<float> texCoords{
            0.0F, 0.0F,
            1.0F, 0.0F,
            0.5F, 1.0F,
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

    std::ofstream output{ root / "embedded_triangle.gltf", std::ios::trunc };
    output
        << "{\n"
        << "  \"asset\": { \"version\": \"2.0\" },\n"
        << "  \"scene\": 0,\n"
        << "  \"scenes\": [{ \"nodes\": [0] }],\n"
        << "  \"nodes\": [{ \"mesh\": 0 }],\n"
        << "  \"materials\": [{\n"
        << "    \"name\": \"embedded_surface\",\n"
        << "    \"pbrMetallicRoughness\": {\n"
        << "      \"baseColorFactor\": [0.7, 0.8, 0.9, 0.5],\n"
        << "      \"metallicFactor\": 0.25,\n"
        << "      \"roughnessFactor\": 0.45,\n"
        << "      \"baseColorTexture\": { \"index\": 0 },\n"
        << "      \"metallicRoughnessTexture\": { \"index\": 1 }\n"
        << "    },\n"
        << "    \"normalTexture\": { \"index\": 2, \"scale\": 0.8 },\n"
        << "    \"occlusionTexture\": { \"index\": 3, \"strength\": 0.7 },\n"
        << "    \"emissiveFactor\": [0.05, 0.1, 0.2],\n"
        << "    \"emissiveTexture\": { \"index\": 4 },\n"
        << "    \"alphaMode\": \"BLEND\"\n"
        << "  }],\n"
        << "  \"textures\": [{ \"source\": 0 }, { \"source\": 1 }, { \"source\": 2 }, { \"source\": 3 }, { \"source\": 4 }],\n"
        << "  \"images\": [{ \"uri\": \"embedded_albedo.kbtex\" }, { \"uri\": \"embedded_mr.kbtex\" }, { \"uri\": \"embedded_normal.kbtex\" }, { \"uri\": \"embedded_ao.kbtex\" }, { \"uri\": \"embedded_emissive.kbtex\" }],\n"
        << "  \"meshes\": [{ \"primitives\": [{ \"attributes\": { \"POSITION\": 0, \"NORMAL\": 1, \"TANGENT\": 2, \"TEXCOORD_0\": 3 }, \"indices\": 4, \"material\": 0 }] }],\n"
        << "  \"buffers\": [{ \"uri\": \"embedded_mesh.bin\", \"byteLength\": 152 }],\n"
        << "  \"bufferViews\": [\n"
        << "    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 36, \"target\": 34962 },\n"
        << "    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 36, \"target\": 34962 },\n"
        << "    { \"buffer\": 0, \"byteOffset\": 72, \"byteLength\": 48, \"target\": 34962 },\n"
        << "    { \"buffer\": 0, \"byteOffset\": 120, \"byteLength\": 24, \"target\": 34962 },\n"
        << "    { \"buffer\": 0, \"byteOffset\": 144, \"byteLength\": 6, \"target\": 34963 }\n"
        << "  ],\n"
        << "  \"accessors\": [\n"
        << "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\", \"min\": [-0.1, -0.1, 0], \"max\": [0.1, 0.1, 0] },\n"
        << "    { \"bufferView\": 1, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        << "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC4\" },\n"
        << "    { \"bufferView\": 3, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC2\" },\n"
        << "    { \"bufferView\": 4, \"componentType\": 5123, \"count\": 3, \"type\": \"SCALAR\" }\n"
        << "  ]\n"
        << "}\n";
}

void WriteMaterial(
    const std::filesystem::path& path,
    std::uint64_t albedoTextureId,
    std::uint64_t normalTextureId,
    std::uint64_t metallicRoughnessTextureId,
    std::uint64_t occlusionTextureId,
    std::uint64_t emissiveTextureId,
    const char* alphaMode = "OPAQUE",
    float alpha = 1.0F) {
    std::ofstream output{ path, std::ios::trunc };
    output
        << "baseColor 0.8 0.7 0.6 " << alpha << "\n"
        << "emissiveColor 0.1 0.2 0.3\n"
        << "metallicFactor 0.2\n"
        << "roughnessFactor 0.55\n"
        << "normalScale 0.9\n"
        << "occlusionStrength 0.85\n"
        << "emissiveStrength 1.5\n"
        << "alphaMode " << alphaMode << "\n"
        << "albedoTextureAssetId " << albedoTextureId << "\n"
        << "normalTextureAssetId " << normalTextureId << "\n"
        << "metallicRoughnessTextureAssetId " << metallicRoughnessTextureId << "\n"
        << "occlusionTextureAssetId " << occlusionTextureId << "\n"
        << "emissiveTextureAssetId " << emissiveTextureId << "\n";
}

void WriteMaterialWithTexturePaths(
    const std::filesystem::path& path,
    const char* alphaMode = "OPAQUE",
    float alpha = 1.0F,
    const char* emissiveTexturePath = "emissive.kbtex") {
    std::ofstream output{ path, std::ios::trunc };
    output
        << "baseColor 0.8 0.7 0.6 " << alpha << "\n"
        << "emissiveColor 0.1 0.2 0.3\n"
        << "metallicFactor 0.2\n"
        << "roughnessFactor 0.55\n"
        << "normalScale 0.9\n"
        << "occlusionStrength 0.85\n"
        << "emissiveStrength 1.5\n"
        << "alphaMode " << alphaMode << "\n"
        << "baseColorTexture albedo.kbtex\n"
        << "normalTexture normal.kbtex\n"
        << "metallicRoughnessTexture metallic_roughness.kbtex\n"
        << "occlusionTexture occlusion.kbtex\n"
        << "emissiveTexture " << emissiveTexturePath << "\n";
}

void RunRendererSubmitsRuntimeMeshAssetInHeadlessNoopTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_runtime_submit";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Runtime submit test could not create temp root");
    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path albedoPath = root / "albedo.kbtex";
    const std::filesystem::path normalPath = root / "normal.kbtex";
    const std::filesystem::path metallicRoughnessPath = root / "metallic_roughness.kbtex";
    const std::filesystem::path occlusionPath = root / "occlusion.kbtex";
    const std::filesystem::path emissivePath = root / "emissive.kbtex";
    const std::filesystem::path materialPath = root / "paint.kbmat";
    const std::filesystem::path transparentMaterialPath = root / "glass.kbmat";
    WriteTriangleObj(meshPath);
    WriteTexture(albedoPath, 180U, 160U, 140U);
    WriteTexture(normalPath, 128U, 128U, 255U);
    WriteTexture(metallicRoughnessPath, 0U, 180U, 80U);
    WriteTexture(occlusionPath, 192U, 192U, 192U);
    WriteTexture(emissivePath, 16U, 32U, 64U);

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Runtime submit test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Runtime submit test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderTextureAssetLoader>()), "Runtime submit test could not register texture loader");
    Require(manager.Mounts().Mount("Game", root), "Runtime submit test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 5U, "Runtime submit test did not discover mesh and texture assets");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Runtime submit test discovered wrong mesh metadata");
    const kb::assets::AssetMetadata* albedoMetadata = manager.Registry().FindByPath("/Game/albedo.kbtex");
    const kb::assets::AssetMetadata* normalMetadata = manager.Registry().FindByPath("/Game/normal.kbtex");
    const kb::assets::AssetMetadata* metallicRoughnessMetadata = manager.Registry().FindByPath("/Game/metallic_roughness.kbtex");
    const kb::assets::AssetMetadata* occlusionMetadata = manager.Registry().FindByPath("/Game/occlusion.kbtex");
    const kb::assets::AssetMetadata* emissiveMetadata = manager.Registry().FindByPath("/Game/emissive.kbtex");
    Require(albedoMetadata != nullptr && normalMetadata != nullptr && metallicRoughnessMetadata != nullptr && occlusionMetadata != nullptr && emissiveMetadata != nullptr, "Runtime submit test did not discover texture metadata");
    WriteMaterial(materialPath, albedoMetadata->id.value, normalMetadata->id.value, metallicRoughnessMetadata->id.value, occlusionMetadata->id.value, emissiveMetadata->id.value);
    WriteMaterialWithTexturePaths(transparentMaterialPath, "BLEND", 0.5F, "missing_emissive.kbtex");
    Require(manager.DiscoverMountedAssets() >= 7U, "Runtime submit test did not discover material assets");
    meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Runtime submit test lost mesh metadata after material discovery");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/paint.kbmat");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Runtime submit test discovered wrong material metadata");
    const kb::assets::AssetMetadata* transparentMaterialMetadata = manager.Registry().FindByPath("/Game/glass.kbmat");
    Require(transparentMaterialMetadata != nullptr && transparentMaterialMetadata->type == "RenderMaterial", "Runtime submit test discovered wrong transparent material metadata");

    constexpr std::uint32_t instanceCount = 16U;
    for (std::uint32_t index = 0U; index < instanceCount; ++index) {
        const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
            .name = "Runtime Mesh",
            .transform = TransformAt(0.0F, 0.0F, 0.0F),
        });
        scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
            .meshAssetId = meshMetadata->id.value,
            .materialAssetId = materialMetadata->id.value,
        });
    }
    const kb::scene::SceneEntity transparentEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Transparent Runtime Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(transparentEntity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialAssetId = transparentMaterialMetadata->id.value,
    });
    const kb::scene::SceneEntity light = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Runtime Directional Light",
        .transform = TransformAt(0.0F, 10.0F, -10.0F),
    });
    scene.Components().Lights().Set(light, kb::scene::LightComponent{
        .kind = kb::scene::LightKind::Directional,
        .intensity = 1.0F,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 4U,
        .cachedMaterials = 4U,
        .cachedTextures = 8U,
        .frameReferencedMeshes = 4U,
        .frameReferencedMaterials = 4U,
        .frameReferencedTextures = 8U,
        .scenePassSubmitStats = 1U,
        .renderSceneMeshProxies = 32U,
        .renderSceneDrawGroupKeys = 8U,
        .meshResourceSlots = 4U,
        .materialResourceSlots = 4U,
        .textureResourceSlots = 8U,
        .meshBindings = 4U,
        .materialBindings = 4U,
        .textureBindings = 8U,
        .syncMeshProxies = 32U,
        .syncTransformCacheEntries = 32U,
        .syncTransformResolvingEntries = 32U,
    });
    Require(renderer.Initialize(surface, &config), "Renderer did not initialize in explicit headless Noop mode");
    Require(renderer.BeginFrame(), "Renderer did not begin headless runtime frame");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 8U,
            .maxVisibleInstances = 64U,
        },
    };
    Require(renderer.SubmitScene(scene, desc), "Renderer did not submit runtime mesh asset scene");

    const SceneRenderSubmitStats submitStats = renderer.LastSceneSubmitStats();
    Require(submitStats.visibleMeshCount == instanceCount * 2U + 1U, "Runtime submit did not keep shadow, opaque, and transparent mesh instances visible");
    Require(submitStats.submittedMeshCount == instanceCount * 2U + 1U, "Runtime submit did not submit shadow, opaque, and transparent mesh instances");
    Require(submitStats.submittedDrawCallCount == 3U, "Runtime submit did not split shadow, opaque, and transparent material passes");
    Require(submitStats.shadowCasterCount == instanceCount, "Runtime submit did not count shadow casters");
    Require(submitStats.submittedShadowCasterCount == instanceCount, "Runtime submit did not submit shadow casters");
    Require(submitStats.submittedShadowDrawCallCount == 1U, "Runtime submit did not draw one shadow caster batch");
    Require(submitStats.shadowFilterSampleCount == 9U, "Runtime submit did not report PCF shadow filter sample count");
    Require(submitStats.shadowLightEntityId == light.Id(), "Runtime submit did not report selected shadow light entity");
    Require(submitStats.shadowMapAllocationBytes == 1024ULL * 1024ULL * 4ULL, "Runtime submit did not report shadow map allocation bytes");
    Require(submitStats.submittedEnvironmentLightingCount == 3U, "Runtime submit did not report environment lighting for every mesh pass");
    Require(submitStats.environmentLightingMode == static_cast<std::uint32_t>(SceneRenderEnvironmentMode::Constant) + 1U, "Runtime submit did not report default environment lighting mode");
    Require(submitStats.environmentLightingSampleCount == 1U, "Runtime submit did not report default environment sample count");
    Require(!submitStats.HasMissingResources(), "Runtime submit reported missing resources for discovered mesh asset");
    const std::span<const SceneRenderPassSubmitStats> passStats = renderer.LastScenePassSubmitStats();
    Require(passStats.size() == 3U, "Runtime submit did not report shadow, opaque, and transparent pass stats");
    Require(passStats[0].pass == MeshPassType::ShadowDepth && passStats[0].stats.submittedShadowCasterCount == instanceCount, "Runtime submit shadow pass stats are wrong");
    Require(passStats[0].stats.shadowFilterSampleCount == 9U, "Runtime submit shadow pass did not report PCF filter sample count");
    Require(passStats[0].stats.shadowLightEntityId == light.Id(), "Runtime submit shadow pass did not report selected shadow light entity");
    Require(passStats[0].stats.shadowMapAllocationBytes == 1024ULL * 1024ULL * 4ULL, "Runtime submit shadow pass did not report shadow map allocation bytes");
    Require(passStats[1].pass == MeshPassType::BaseOpaque && passStats[1].stats.submittedMeshCount == instanceCount, "Runtime submit opaque pass stats are wrong");
    Require(passStats[2].pass == MeshPassType::BaseTransparent && passStats[2].stats.submittedMeshCount == 1U, "Runtime submit transparent pass stats are wrong");
    Require(renderer.LastSceneExposureStats().empty(), "Runtime submit without post-process unexpectedly reported exposure stats");

    const Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
    Require(runtimeStats.cachedMeshCount == 1U, "Runtime submit did not cache exactly one mesh resource");
    Require(runtimeStats.cachedMaterialCount == 2U, "Runtime submit did not cache opaque and transparent material resources");
    Require(runtimeStats.cachedTextureCount == 5U, "Runtime submit did not cache every referenced material texture");
    Require(runtimeStats.referencedMeshAssetCount == 1U, "Runtime submit did not reference exactly one mesh asset");
    Require(runtimeStats.referencedMaterialAssetCount == 2U, "Runtime submit did not reference opaque and transparent material assets");
    Require(runtimeStats.referencedTextureAssetCount == 5U, "Runtime submit did not reference every material texture asset");
    Require(runtimeStats.unresolvedMaterialTexturePathCount == 1U, "Runtime submit did not report unresolved material texture paths");
    Require(runtimeStats.shadowMapAllocated && runtimeStats.shadowMapSize == 1024U, "Runtime submit did not allocate the configured runtime shadow map");
    Require(runtimeStats.shadowMapAllocationBytes == 1024ULL * 1024ULL * 4ULL, "Runtime submit did not expose shadow map allocation bytes");
    Require(runtimeStats.defaultEnvironmentLightingMode == static_cast<std::uint32_t>(SceneRenderEnvironmentMode::Constant) + 1U, "Runtime submit did not expose default environment lighting mode");
    Require(runtimeStats.defaultEnvironmentLightingSampleCount == 1U, "Runtime submit did not expose default environment sample count");
    Require(runtimeStats.defaultShadowFilterSampleCount == 9U, "Runtime submit did not expose default shadow filter sample count");
    bool foundUnresolvedTexturePathDiagnostic = false;
    for (const SceneRenderDiagnosticEvent& event : renderer.LastSceneDiagnostics().events) {
        if (event.severity == SceneRenderDiagnosticSeverity::Warning &&
            event.kind == SceneRenderDiagnosticKind::UnresolvedMaterialTexturePath &&
            event.materialAssetId == transparentMaterialMetadata->id.value &&
            event.instanceCount == 1U) {
            foundUnresolvedTexturePathDiagnostic = true;
        }
    }
    Require(foundUnresolvedTexturePathDiagnostic, "Runtime submit did not emit unresolved material texture path diagnostic");
    Require(runtimeStats.renderSceneMeshProxyCount == instanceCount + 1U, "Runtime submit did not keep scene mesh proxies");
    Require(runtimeStats.meshResourceSlotCapacity >= 4U, "Runtime submit did not apply mesh resource slot reserve");
    Require(runtimeStats.materialResourceSlotCapacity >= 4U, "Runtime submit did not apply material resource slot reserve");
    Require(runtimeStats.textureResourceSlotCapacity >= 8U, "Runtime submit did not apply texture resource slot reserve");
    Require(runtimeStats.meshBindingCapacity >= 4U, "Runtime submit did not apply mesh binding reserve");
    Require(runtimeStats.materialBindingCapacity >= 4U, "Runtime submit did not apply material binding reserve");
    Require(runtimeStats.textureBindingCapacity >= 8U, "Runtime submit did not apply texture binding reserve");
    Require(runtimeStats.renderSceneMeshProxyCapacity >= 32U, "Runtime submit did not apply render scene mesh proxy reserve");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunRendererSubmitsGltfEmbeddedMaterialInHeadlessNoopTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_runtime_embedded_material";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Embedded material runtime test could not create temp root");

    WriteEmbeddedMaterialTriangleGltf(root);
    WriteTexture(root / "embedded_albedo.kbtex", 160U, 180U, 200U);
    WriteTexture(root / "embedded_normal.kbtex", 128U, 128U, 255U);
    WriteTexture(root / "embedded_mr.kbtex", 64U, 128U, 192U);
    WriteTexture(root / "embedded_ao.kbtex", 192U, 192U, 192U);
    WriteTexture(root / "embedded_emissive.kbtex", 10U, 20U, 30U);

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Embedded material runtime test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderTextureAssetLoader>()), "Embedded material runtime test could not register texture loader");
    Require(manager.Mounts().Mount("Game", root), "Embedded material runtime test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 5U, "Embedded material runtime test did not discover mesh and texture assets");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/embedded_triangle.gltf");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Embedded material runtime test discovered wrong mesh metadata");

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Embedded Material Runtime Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 2U,
        .cachedMaterials = 4U,
        .cachedTextures = 5U,
        .frameReferencedMeshes = 2U,
        .frameReferencedMaterials = 4U,
        .frameReferencedTextures = 5U,
        .scenePassSubmitStats = 2U,
        .renderSceneMeshProxies = 4U,
        .renderSceneDrawGroupKeys = 4U,
        .meshResourceSlots = 2U,
        .materialResourceSlots = 4U,
        .textureResourceSlots = 5U,
        .meshBindings = 2U,
        .materialBindings = 4U,
        .textureBindings = 5U,
        .syncMeshProxies = 4U,
        .syncTransformCacheEntries = 4U,
        .syncTransformResolvingEntries = 4U,
    });
    Require(renderer.Initialize(surface, &config), "Renderer did not initialize in embedded material runtime test");
    Require(renderer.BeginFrame(), "Renderer did not begin embedded material runtime frame");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 4U,
            .maxVisibleInstances = 8U,
        },
    };
    Require(renderer.SubmitScene(scene, desc), "Renderer did not submit embedded material runtime scene");

    const SceneRenderSubmitStats submitStats = renderer.LastSceneSubmitStats();
    Require(submitStats.visibleMeshCount == 1U, "Embedded material runtime test did not keep mesh visible");
    Require(submitStats.submittedMeshCount == 1U, "Embedded material runtime test did not submit mesh");
    Require(submitStats.submittedDrawCallCount == 1U, "Embedded material runtime test did not draw one embedded material batch");
    Require(!submitStats.HasMissingResources(), "Embedded material runtime test reported missing resources");
    const std::span<const SceneRenderPassSubmitStats> passStats = renderer.LastScenePassSubmitStats();
    Require(passStats.size() == 2U, "Embedded material runtime test did not report both scene passes");
    Require(passStats[0].pass == MeshPassType::BaseOpaque && passStats[0].stats.submittedMeshCount == 0U, "Embedded material runtime opaque pass stats are wrong");
    Require(passStats[1].pass == MeshPassType::BaseTransparent && passStats[1].stats.submittedMeshCount == 1U, "Embedded material runtime transparent pass stats are wrong");

    const Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
    Require(runtimeStats.cachedMeshCount == 1U, "Embedded material runtime test did not cache one mesh resource");
    Require(runtimeStats.cachedMaterialCount == 1U, "Embedded material runtime test did not cache one embedded material resource");
    Require(runtimeStats.cachedTextureCount == 5U, "Embedded material runtime test did not cache embedded material textures");
    Require(runtimeStats.referencedMeshAssetCount == 1U, "Embedded material runtime test did not reference one mesh asset");
    Require(runtimeStats.referencedMaterialAssetCount == 1U, "Embedded material runtime test did not reference one embedded material asset");
    Require(runtimeStats.referencedTextureAssetCount == 5U, "Embedded material runtime test did not reference embedded material textures");
    Require(runtimeStats.unresolvedMaterialTexturePathCount == 0U, "Embedded material runtime test reported unresolved texture paths for valid embedded textures");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunRendererSubmitsDockedAndDetachedViewportsInSameFrameTest() {
    kb::scene::Scene scene;

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Renderer did not initialize for same-frame multi-viewport test");
    Require(renderer.BeginFrame(), "Renderer did not begin same-frame multi-viewport test");

    const RenderSceneSubmitDesc docked{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .meshPassMode = SceneRenderMeshPassMode::OpaqueAndTransparent,
    };
    const RenderSceneSubmitDesc detached{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 2U },
                .extent = RenderExtent{ 96U, 72U },
                .viewportIndex = 1U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .meshPassMode = SceneRenderMeshPassMode::OpaqueAndTransparent,
    };
    const std::array<Renderer::SceneFrameSubmission, 2U> submissions{
        Renderer::SceneFrameSubmission{ .scene = &scene, .desc = docked },
        Renderer::SceneFrameSubmission{ .scene = &scene, .desc = detached },
    };

    Require(renderer.SubmitScenes(submissions), "Renderer rejected docked and detached viewport submissions in one frame");
    const std::span<const SceneRenderPassSubmitStats> passStats = renderer.LastScenePassSubmitStats();
    Require(passStats.size() == 4U, "Same-frame multi-viewport test did not report both scene passes for both viewports");
    Require(passStats[0].viewportId == 1U && passStats[0].viewportIndex == 0U, "Docked viewport opaque pass metadata is wrong");
    Require(passStats[1].viewportId == 1U && passStats[1].viewportIndex == 0U, "Docked viewport transparent pass metadata is wrong");
    Require(passStats[2].viewportId == 2U && passStats[2].viewportIndex == 1U, "Detached viewport opaque pass metadata is wrong");
    Require(passStats[3].viewportId == 2U && passStats[3].viewportIndex == 1U, "Detached viewport transparent pass metadata is wrong");
    Require(!renderer.LastSceneSubmitStats().HasMissingResources(), "Same-frame multi-viewport test reported missing resources for an empty scene");

    renderer.EndFrame();
    renderer.Shutdown();
}

} // namespace

void RunRendererRuntimeSubmitTests() {
    RunRendererSubmitsRuntimeMeshAssetInHeadlessNoopTest();
    RunRendererSubmitsGltfEmbeddedMaterialInHeadlessNoopTest();
    RunRendererSubmitsDockedAndDetachedViewportsInSameFrameTest();
}

} // namespace kb::render::tests
