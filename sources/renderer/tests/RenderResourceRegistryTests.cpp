#include "RendererTestSupport.hpp"

#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/ImportedAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "kb/render/scene/SceneRenderer.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <vector>

namespace kb::render::tests {
namespace {

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

void RunStaticMeshRegistryRejectsSkinnedFormatUntilSkinningRuntimeExistsTest() {
    const RenderStaticMeshVertexSkinned vertices[3]{};
    const std::uint16_t indices[]{ 0U, 1U, 2U };
    RenderResourceRegistry registry;
    const RenderMeshHandle handle = registry.RegisterMesh(RenderMeshDesc{
        .vertexData = vertices,
        .vertexCount = 3U,
        .indices = indices,
        .indexCount = 3U,
        .vertexFormat = RenderVertexFormat::SkinnedP3N3T4UV2J4W4,
        .indexFormat = RenderIndexFormat::Uint16,
    });
    Require(!handle.IsValid(), "Static mesh registry accepted a skinned vertex format without skinning runtime support");
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
    Require(asset->desc.vertexFormat == RenderVertexFormat::P3N3UV2, "OBJ importer did not choose the expected vertex format");
    Require(asset->desc.indexFormat == RenderIndexFormat::Uint16, "OBJ importer did not compact small OBJ indices to uint16");
    Require(asset->desc.vertexCount == 4U, "OBJ importer did not deduplicate shared vertex tuples");
    Require(asset->desc.indexCount == 9U, "OBJ importer did not triangulate face indices");
    Require(asset->sections.size() == 2U, "OBJ importer did not create material sections");
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
            << "vt 0 0\n"
            << "vt 1 0\n"
            << "vt 1 1\n"
            << "vn 0 0 1\n"
            << "f 1/1/1 2/2/1 3/3/1\n";
    }

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "AssetManager rejected RenderMeshAssetLoader");
    Require(manager.Mounts().Mount("Game", root), "AssetManager could not mount mesh asset test root");
    Require(manager.DiscoverMountedAssets() == 1U, "AssetManager did not discover the OBJ mesh asset");

    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath("/Game/quad.obj");
    Require(metadata != nullptr && metadata->type == "RenderMesh", "AssetManager registered the OBJ mesh with the wrong type");
    const kb::assets::AssetHandle<RenderMeshAssetData> asset = manager.Load<RenderMeshAssetData>(metadata->id);
    Require(asset.IsLoaded(), "AssetManager did not load RenderMeshAssetData through RenderMeshAssetLoader");
    Require(asset->desc.vertexCount == 3U && asset->desc.indexCount == 3U, "Loaded RenderMeshAssetData has the wrong geometry counts");

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
            << "      \"baseColorTexture\": { \"index\": 0 },\n"
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
            << "  \"extensionsUsed\": [\"KHR_materials_emissive_strength\"],\n"
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
    Require(NearlyEqual(asset->embeddedMaterials[0].desc.baseColor[0], 0.2F), "glTF importer did not preserve embedded material base color");
    Require(NearlyEqual(asset->embeddedMaterials[0].desc.baseColor[3], 0.5F), "glTF importer did not preserve embedded material alpha");
    Require(NearlyEqual(asset->embeddedMaterials[0].desc.metallicFactor, 0.7F), "glTF importer did not preserve embedded material metallic factor");
    Require(NearlyEqual(asset->embeddedMaterials[0].desc.roughnessFactor, 0.35F), "glTF importer did not preserve embedded material roughness factor");
    Require(NearlyEqual(asset->embeddedMaterials[0].desc.normalScale, 0.75F), "glTF importer did not preserve embedded material normal scale");
    Require(NearlyEqual(asset->embeddedMaterials[0].desc.occlusionStrength, 0.6F), "glTF importer did not preserve embedded material occlusion strength");
    Require(NearlyEqual(asset->embeddedMaterials[0].desc.emissiveColor[2], 0.3F), "glTF importer did not preserve embedded material emissive color");
    Require(NearlyEqual(asset->embeddedMaterials[0].desc.emissiveStrength, 2.5F), "glTF importer did not preserve embedded material emissive strength");
    Require(NearlyEqual(asset->embeddedMaterials[0].desc.alphaCutoff, 0.45F), "glTF importer did not preserve embedded material alpha cutoff");
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

void RunGltfImporterRejectsSkinnedMeshesUntilSkinningRuntimeExistsTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_skinned_gltf_reject";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Skinned glTF reject test could not create temp root");

    const std::filesystem::path binPath = root / "skinned.bin";
    {
        const std::vector<float> positions{
            0.0F, 0.0F, 0.0F,
            1.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F,
        };
        const std::uint16_t joints[]{
            0U, 1U, 0U, 0U,
            0U, 1U, 0U, 0U,
            0U, 1U, 0U, 0U,
        };
        const std::vector<float> weights{
            0.75F, 0.25F, 0.0F, 0.0F,
            0.75F, 0.25F, 0.0F, 0.0F,
            0.75F, 0.25F, 0.0F, 0.0F,
        };
        const std::uint16_t indices[]{ 0U, 1U, 2U };

        std::ofstream output{ binPath, std::ios::binary | std::ios::trunc };
        output.write(reinterpret_cast<const char*>(positions.data()), static_cast<std::streamsize>(positions.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(joints), static_cast<std::streamsize>(sizeof(joints)));
        output.write(reinterpret_cast<const char*>(weights.data()), static_cast<std::streamsize>(weights.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(indices), static_cast<std::streamsize>(sizeof(indices)));
        const std::uint16_t padding = 0U;
        output.write(reinterpret_cast<const char*>(&padding), static_cast<std::streamsize>(sizeof(padding)));
    }

    const std::filesystem::path gltfPath = root / "skinned.gltf";
    {
        std::ofstream output{ gltfPath, std::ios::trunc };
        output
            << "{\n"
            << "  \"asset\": { \"version\": \"2.0\" },\n"
            << "  \"scene\": 0,\n"
            << "  \"scenes\": [{ \"nodes\": [0] }],\n"
            << "  \"nodes\": [{ \"mesh\": 0 }],\n"
            << "  \"meshes\": [{ \"primitives\": [{ \"attributes\": { \"POSITION\": 0, \"JOINTS_0\": 1, \"WEIGHTS_0\": 2 }, \"indices\": 3 }] }],\n"
            << "  \"buffers\": [{ \"uri\": \"skinned.bin\", \"byteLength\": 112 }],\n"
            << "  \"bufferViews\": [\n"
            << "    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 36, \"target\": 34962 },\n"
            << "    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 24, \"target\": 34962 },\n"
            << "    { \"buffer\": 0, \"byteOffset\": 60, \"byteLength\": 48, \"target\": 34962 },\n"
            << "    { \"buffer\": 0, \"byteOffset\": 108, \"byteLength\": 6, \"target\": 34963 }\n"
            << "  ],\n"
            << "  \"accessors\": [\n"
            << "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\", \"min\": [0, 0, 0], \"max\": [1, 1, 0] },\n"
            << "    { \"bufferView\": 1, \"componentType\": 5123, \"count\": 3, \"type\": \"VEC4\" },\n"
            << "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC4\" },\n"
            << "    { \"bufferView\": 3, \"componentType\": 5123, \"count\": 3, \"type\": \"SCALAR\" }\n"
            << "  ]\n"
            << "}\n";
    }

    Require(!RenderMeshAssetBuilder::LoadGltf(gltfPath).has_value(), "Static glTF importer accepted a skinned mesh without skinning runtime support");
    std::filesystem::remove_all(root, error);
}

void RunGltfImporterRejectsSkinNodesUntilSkinningRuntimeExistsTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_skin_node_gltf_reject";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Skin node glTF reject test could not create temp root");

    const std::filesystem::path binPath = root / "skin_node.bin";
    {
        const std::vector<float> positions{
            0.0F, 0.0F, 0.0F,
            1.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F,
        };
        const std::uint16_t indices[]{ 0U, 1U, 2U };
        std::ofstream output{ binPath, std::ios::binary | std::ios::trunc };
        output.write(reinterpret_cast<const char*>(positions.data()), static_cast<std::streamsize>(positions.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(indices), static_cast<std::streamsize>(sizeof(indices)));
        const std::uint16_t padding = 0U;
        output.write(reinterpret_cast<const char*>(&padding), static_cast<std::streamsize>(sizeof(padding)));
    }

    const std::filesystem::path gltfPath = root / "skin_node.gltf";
    {
        std::ofstream output{ gltfPath, std::ios::trunc };
        output
            << "{\n"
            << "  \"asset\": { \"version\": \"2.0\" },\n"
            << "  \"scene\": 0,\n"
            << "  \"scenes\": [{ \"nodes\": [0] }],\n"
            << "  \"nodes\": [{ \"mesh\": 0, \"skin\": 0 }],\n"
            << "  \"skins\": [{ \"joints\": [] }],\n"
            << "  \"meshes\": [{ \"primitives\": [{ \"attributes\": { \"POSITION\": 0 }, \"indices\": 1 }] }],\n"
            << "  \"buffers\": [{ \"uri\": \"skin_node.bin\", \"byteLength\": 44 }],\n"
            << "  \"bufferViews\": [\n"
            << "    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 36, \"target\": 34962 },\n"
            << "    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 6, \"target\": 34963 }\n"
            << "  ],\n"
            << "  \"accessors\": [\n"
            << "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
            << "    { \"bufferView\": 1, \"componentType\": 5123, \"count\": 3, \"type\": \"SCALAR\" }\n"
            << "  ]\n"
            << "}\n";
    }

    Require(!RenderMeshAssetBuilder::LoadGltf(gltfPath).has_value(), "Static glTF importer accepted a skin node without skinning runtime support");
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
    Require(NearlyEqual(asset->desc.baseColor[0], 0.2F), "Loaded material did not preserve baseColor red");
    Require(NearlyEqual(asset->desc.baseColor[1], 0.4F), "Loaded material did not preserve baseColor green");
    Require(NearlyEqual(asset->desc.baseColor[2], 0.8F), "Loaded material did not preserve baseColor blue");
    Require(NearlyEqual(asset->desc.emissiveColor[2], 0.3F), "Loaded material did not preserve emissive color");
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
    std::istringstream input{ output.str() };
    const std::optional<RenderMaterialAssetData> loaded = RenderMaterialAssetLoader::LoadMaterial(input);
    Require(loaded.has_value(), "RenderMaterialAssetWriter produced material text the parser rejected");
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
    Require(loaded->desc.decalTextureAssetId == source.desc.decalTextureAssetId, "Material writer roundtrip lost decal texture asset id");
    Require(loaded->desc.layerMaskTextureAssetId == source.desc.layerMaskTextureAssetId, "Material writer roundtrip lost layer mask texture asset id");
    Require(loaded->clearcoatRoughnessTexturePath == source.clearcoatRoughnessTexturePath, "Material writer roundtrip lost clearcoat roughness path");
    Require(loaded->layerMaskTexturePath == source.layerMaskTexturePath, "Material writer roundtrip lost layer mask path");
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
    Require(asset->rgba8.size() == static_cast<std::size_t>(asset->width) * static_cast<std::size_t>(asset->height) * 4U, "Loaded image was not converted to RGBA8");
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

} // namespace

void RunRenderResourceRegistryTests() {
    RunMaterialHandlesAreGenerationalTest();
    RunMaterialReloadInvalidatesStaleSceneBindingTest();
    RunInvalidHandlesAreIgnoredTest();
    RunShutdownInvalidatesLiveHandlesTest();
    RunReserveAndStatsReportPoolPressureTest();
    RunStaticMeshVertexFormatsExposeExpectedStridesTest();
    RunStaticMeshRegistryRejectsSkinnedFormatUntilSkinningRuntimeExistsTest();
    RunObjImporterBuildsRenderMeshDescWithSectionsAndSlotsTest();
    RunRenderMeshAssetLoaderDiscoversAndLoadsObjThroughAssetManagerTest();
    RunRenderMeshAssetLoaderLoadsImportedObjContainerTest();
    RunRenderMeshAssetLoaderLoadsWorkspaceImportedFbxCubeWhenPresentTest();
    RunRenderMeshAssetLoaderDiscoversAndLoadsGltfThroughAssetManagerTest();
    RunGltfImporterRejectsSkinnedMeshesUntilSkinningRuntimeExistsTest();
    RunGltfImporterRejectsSkinNodesUntilSkinningRuntimeExistsTest();
    RunGltfImporterRejectsOutOfRangeIndicesTest();
    RunGltfImporterKeepsUint32IndicesForLargeTangentMeshesTest();
    RunRenderMaterialAssetLoaderDiscoversAndLoadsMaterialThroughAssetManagerTest();
    RunRenderMaterialAssetWriterRoundTripsThroughParserTest();
    RunRenderTextureAssetLoaderDiscoversAndLoadsTextureThroughAssetManagerTest();
    RunRenderTextureAssetLoaderLoadsPngJpgAndDdsThroughAssetManagerTest();
    RunMeshAssetDataKeepsUint32IndicesForLargeMeshesTest();
    RunSceneRendererTicksRegistryDeferredDestroyTest();
}

} // namespace kb::render::tests
