#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "assets/EditorAssetBrowserState.hpp"
#include "commands/EditorCommandStack.hpp"
#include "console/EditorConsoleState.hpp"
#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphProgramBindingBuilder.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"
#include "kb/render/resources/RenderMaterialFunctionAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"
#include "kb/render/resources/RenderMaterialTypeAssetLoader.hpp"
#include "kb/render/resources/RenderResources.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "kb/render/runtime/RuntimeMaterialResolver.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "scene/material/EditorEmbeddedMaterialAssetWriter.hpp"
#include "scene/material/EditorEmbeddedMaterialExtractor.hpp"
#include "scene/material/EditorMaterialAssetAuthoring.hpp"
#include "scene/material/EditorMaterialAssetEditCommand.hpp"
#include "scene/material/EditorMaterialAssetGateway.hpp"
#include "scene/material/EditorMaterialTextureSlotValidation.hpp"
#include "scene/material_preview/EditorMaterialPreviewScene.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "scene/EditorSceneMaterialAssetActions.hpp"
#include "scene/material/MaterialEditorState.hpp"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

class MaterialAuthoringHeadlessSurface final : public kb::render::RenderSurface {
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

[[nodiscard]] std::filesystem::path TempRoot() {
    return std::filesystem::temp_directory_path() / "21kb_editor_material_asset_authoring_tests";
}

[[nodiscard]] kb::scene::TransformComponent TransformAt(float x, float y, float z) {
    return kb::scene::TransformComponent{
        .localPosition = kb::scene::Vec3{ x, y, z },
        .worldPosition = kb::scene::Vec3{ x, y, z },
        .worldDirty = false,
    };
}

[[nodiscard]] kb::render::SceneRenderCamera IdentityCamera() noexcept {
    return kb::render::SceneRenderCamera{
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

[[nodiscard]] kb::render::RenderSceneSubmitDesc MaterialAuthoringSubmitDesc(std::uint16_t viewportIndex) noexcept {
    return kb::render::RenderSceneSubmitDesc{
        .target = kb::render::RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = kb::render::RenderViewportDesc{
                .id = kb::render::RenderViewportId{ static_cast<std::uint32_t>(viewportIndex + 1U) },
                .extent = kb::render::RenderExtent{ 64U, 64U },
                .viewportIndex = viewportIndex,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = kb::render::SceneRenderDrawBudget{
            .maxDrawCommands = 4U,
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = kb::render::SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
        .postProcessEnabled = false,
        .selectionMaskEnabled = false,
        .selectionOutlineEnabled = false,
        .gpuDrivenRuntimeDispatchEnabled = false,
    };
}

void ReserveMaterialAuthoringRuntimeResources(kb::render::Renderer& renderer) {
    renderer.ReserveRuntimeSceneResources(kb::render::Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 2U,
        .cachedMeshes = 4U,
        .cachedMaterials = 4U,
        .cachedTextures = 4U,
        .frameReferencedMeshes = 4U,
        .frameReferencedMaterials = 4U,
        .frameReferencedTextures = 4U,
        .scenePassSubmitStats = 4U,
        .renderSceneMeshProxies = 8U,
        .renderSceneDrawGroupKeys = 4U,
        .meshResourceSlots = 4U,
        .materialResourceSlots = 4U,
        .textureResourceSlots = 4U,
        .meshBindings = 4U,
        .materialBindings = 4U,
        .textureBindings = 4U,
        .syncMeshProxies = 8U,
        .syncTransformCacheEntries = 8U,
        .syncTransformResolvingEntries = 8U,
    });
}

void CleanTempRoot() {
    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
    std::filesystem::create_directories(TempRoot() / "Project" / "Assets" / "Materials", error);
}

void WriteTextFile(const std::filesystem::path& path, std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output << text;
}

void WriteTexture(const std::filesystem::path& path, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output{ path, std::ios::trunc };
    output
        << "size 1 1\n"
        << "rgba8 "
        << static_cast<std::uint32_t>(r) << " "
        << static_cast<std::uint32_t>(g) << " "
        << static_cast<std::uint32_t>(b) << " 255\n";
}

void WriteTriangleObj(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
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

void WriteEmbeddedMaterialGltfFixture(const std::filesystem::path& folder, std::string_view alphaMode = "BLEND") {
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const std::filesystem::path binPath = folder / "mesh.bin";
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

    std::ofstream output{ folder / "embedded.gltf", std::ios::trunc };
    output
        << "{\n"
        << "  \"asset\": { \"version\": \"2.0\" },\n"
        << "  \"scene\": 0,\n"
        << "  \"scenes\": [{ \"nodes\": [0] }],\n"
        << "  \"extensionsUsed\": [\"KHR_materials_emissive_strength\", \"KHR_texture_transform\"],\n"
        << "  \"nodes\": [{ \"mesh\": 0 }],\n"
        << "  \"materials\": [{\n"
        << "    \"name\": \"painted metal\",\n"
        << "    \"pbrMetallicRoughness\": {\n"
        << "      \"baseColorFactor\": [0.2, 0.4, 0.8, 0.6],\n"
        << "      \"metallicFactor\": 0.7,\n"
        << "      \"roughnessFactor\": 0.35,\n"
        << "      \"baseColorTexture\": { \"index\": 0, \"extensions\": { \"KHR_texture_transform\": { \"offset\": [0.25, 0.5], \"scale\": [2.0, 3.0] } } },\n"
        << "      \"metallicRoughnessTexture\": { \"index\": 1 }\n"
        << "    },\n"
        << "    \"normalTexture\": { \"index\": 2, \"scale\": 0.75 },\n"
        << "    \"occlusionTexture\": { \"index\": 3, \"strength\": 0.6 },\n"
        << "    \"emissiveTexture\": { \"index\": 4 },\n"
        << "    \"emissiveFactor\": [0.1, 0.2, 0.3],\n"
        << "    \"extensions\": { \"KHR_materials_emissive_strength\": { \"emissiveStrength\": 2.5 } },\n"
        << "    \"alphaMode\": \"" << alphaMode << "\",\n"
        << "    \"doubleSided\": true\n"
        << "  }],\n"
        << "  \"textures\": [{ \"source\": 0 }, { \"source\": 1 }, { \"source\": 2 }, { \"source\": 3 }, { \"source\": 4 }],\n"
        << "  \"images\": [\n"
        << "    { \"uri\": \"Textures/albedo.kbtex\" },\n"
        << "    { \"uri\": \"Textures/metallic_roughness.kbtex\" },\n"
        << "    { \"uri\": \"Textures/normal.kbtex\" },\n"
        << "    { \"uri\": \"Textures/occlusion.kbtex\" },\n"
        << "    { \"uri\": \"Textures/emissive.kbtex\" }\n"
        << "  ],\n"
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
        << "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\", \"min\": [-0.1, -0.1, 0], \"max\": [0.1, 0.1, 0] },\n"
        << "    { \"bufferView\": 1, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        << "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC4\" },\n"
        << "    { \"bufferView\": 3, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC2\" },\n"
        << "    { \"bufferView\": 4, \"componentType\": 5123, \"count\": 3, \"type\": \"SCALAR\" }\n"
        << "  ]\n"
        << "}\n";
}

[[nodiscard]] kb::assets::AssetMetadata Metadata(std::string name, std::string type, std::filesystem::path path) {
    return kb::assets::AssetMetadata{
        .type = std::move(type),
        .name = std::move(name),
        .virtualPath = std::move(path),
        .runtimeLoadable = true,
    };
}

[[nodiscard]] kb::assets::AssetId RequireAssetId(kb::assets::AssetManager& manager, const std::filesystem::path& path, const char* message) {
    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(path);
    kb::editor::tests::Require(metadata != nullptr, message);
    return metadata->id;
}

[[nodiscard]] kb::render::RenderMaterialGraphLink MakeMaterialGraphLink(
    kb::render::RenderMaterialGraphNodeKind fromKind,
    std::uint32_t fromNodeId,
    std::string fromPin,
    kb::render::RenderMaterialGraphNodeKind toKind,
    std::uint32_t toNodeId,
    std::string toPin) {
    kb::render::RenderMaterialGraphLink link{
        .fromNodeId = fromNodeId,
        .fromPinId = kb::render::RenderMaterialGraphStablePinId(fromKind, fromPin, true),
        .fromPin = std::move(fromPin),
        .toNodeId = toNodeId,
        .toPinId = kb::render::RenderMaterialGraphStablePinId(toKind, toPin, false),
        .toPin = std::move(toPin),
    };
    link.id = kb::render::MakeRenderMaterialGraphLinkId(link);
    return link;
}

[[nodiscard]] const kb::render::RenderMaterialGraphParameterValue* FindGraphParameterValue(
    const kb::render::RenderMaterialAssetData& material,
    std::string_view stableId) noexcept {
    const auto found = std::ranges::find_if(material.graphParameterValues, [stableId](const kb::render::RenderMaterialGraphParameterValue& value) {
        return value.stableId == stableId;
    });
    return found == material.graphParameterValues.end() ? nullptr : &*found;
}

void RunCreateMaterialAssetThroughEditorAuthoringTest() {
    CleanTempRoot();

    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(scene.Assets().MountProject(TempRoot() / "Project"), "Material authoring test could not mount project assets");

    kb::editor::EditorMaterialAssetAuthoring authoring{ scene, browser, console };
    kb::editor::tests::Require(authoring.Create("/Game/Materials"), "Editor material authoring did not create a material asset");

    const std::filesystem::path materialPath = TempRoot() / "Project" / "Assets" / "Materials" / "NewMaterial.kbmat";
    kb::editor::tests::Require(std::filesystem::exists(materialPath), "Editor material authoring did not write NewMaterial.kbmat");

    const kb::assets::AssetManager& manager = scene.Assets().Manager();
    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath("/Game/Materials/NewMaterial.kbmat");
    kb::editor::tests::Require(metadata != nullptr, "Editor material authoring did not register the created material");
    kb::editor::tests::Require(metadata->type == "RenderMaterial", "Editor material authoring registered the wrong asset type");
    kb::editor::tests::Require(metadata->runtimeLoadable, "Editor material authoring registered a non-runtime-loadable material");
    kb::editor::tests::Require(browser.SelectedAsset() == metadata->id, "Editor material authoring did not select the created material asset");

    const std::optional<kb::render::RenderMaterialAssetData> loaded = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(loaded.has_value(), "Editor material authoring wrote a material file that could not be loaded");
    kb::editor::tests::Require(loaded->documentVersion == kb::render::kRenderMaterialAssetDocumentVersion, "Editor material authoring did not write the current material document version");
    kb::editor::tests::Require(loaded->hasExplicitDocumentVersion, "Editor material authoring did not write explicit material document version metadata");
    kb::editor::tests::Require(loaded->materialType == kb::render::kRenderMaterialAssetBuiltInPbrType, "Editor material authoring did not write the built-in PBR material type");
    kb::editor::tests::Require(loaded->materialTypeVersion == kb::render::kRenderMaterialAssetBuiltInPbrTypeVersion, "Editor material authoring did not write the built-in PBR material type version");
    kb::editor::tests::Require(loaded->hasExplicitMaterialType, "Editor material authoring did not write explicit material type metadata");
    kb::editor::tests::Require(loaded->hasExplicitMaterialTypeVersion, "Editor material authoring did not write explicit material type version metadata");
    kb::editor::tests::Require(loaded->graph.hasExplicitDocumentVersion, "Editor material authoring did not write explicit material graph version metadata");
    kb::editor::tests::Require(loaded->graph.nodes.size() == 1U, "Editor material authoring did not create the default material graph node");
    kb::editor::tests::Require(loaded->graph.nodes.front().kind == kb::render::RenderMaterialGraphNodeKind::MaterialOutput, "Editor material authoring did not create a material output node");
    const kb::assets::AssetHandle<kb::render::RenderMaterialAssetData> runtimeLoaded = scene.Assets().Manager().Load<kb::render::RenderMaterialAssetData>(metadata->id);
    kb::editor::tests::Require(runtimeLoaded.IsLoaded(), "Editor material authoring did not make the created material loadable through AssetManager");
    kb::editor::tests::Require(loaded->desc.baseColor[0] == 1.0F && loaded->desc.baseColor[1] == 1.0F && loaded->desc.baseColor[2] == 1.0F && loaded->desc.baseColor[3] == 1.0F, "Editor material authoring did not preserve default white base color");
    kb::editor::tests::Require(loaded->desc.metallicFactor == 0.0F, "Editor material authoring did not preserve default metallic factor");
    kb::editor::tests::Require(loaded->desc.roughnessFactor == 1.0F, "Editor material authoring did not preserve default roughness factor");
    kb::editor::tests::Require(loaded->desc.alphaMode == kb::render::RenderMaterialAlphaMode::Opaque, "Editor material authoring did not preserve default alpha mode");
    kb::editor::tests::Require(!loaded->desc.doubleSided, "Editor material authoring did not preserve default double-sided flag");
    kb::editor::tests::Require(console.Count(kb::editor::EditorConsoleLevel::Info) == 1U, "Editor material authoring should report one successful creation message");

    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
}

void RunCreateMaterialGraphAndTypeThroughEditorAuthoringTest() {
    CleanTempRoot();

    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(scene.Assets().MountProject(TempRoot() / "Project"), "Material graph/type authoring test could not mount project assets");

    kb::editor::EditorMaterialAssetAuthoring authoring{ scene, browser, console };
    kb::editor::tests::Require(authoring.CreateGraph("/Game/Materials"), "KBMAT-GRAPH-0005: Editor authoring did not create a Material Graph asset");
    const std::filesystem::path graphPath = TempRoot() / "Project" / "Assets" / "Materials" / "NewMaterialGraph.kbmaterialgraph";
    kb::editor::tests::Require(std::filesystem::exists(graphPath), "KBMAT-GRAPH-0005: Editor authoring did not write NewMaterialGraph.kbmaterialgraph");

    const kb::assets::AssetManager& manager = scene.Assets().Manager();
    const kb::assets::AssetMetadata* graphMetadata = manager.Registry().FindByPath("/Game/Materials/NewMaterialGraph.kbmaterialgraph");
    kb::editor::tests::Require(graphMetadata != nullptr && graphMetadata->type == kb::render::kRenderMaterialGraphAssetType,
        "KBMAT-GRAPH-0005: Material Graph authoring registered wrong metadata");
    kb::editor::tests::Require(browser.SelectedAsset() == graphMetadata->id, "KBMAT-GRAPH-0005: Material Graph authoring did not select the created asset");
    const std::optional<kb::render::RenderMaterialGraphDocument> graph = kb::render::RenderMaterialGraphAssetLoader::LoadGraph(graphPath);
    kb::editor::tests::Require(graph.has_value() && graph->storageModel == "material-graph-asset" && graph->nodes.size() == 1U,
        "KBMAT-GRAPH-0005: Created Material Graph asset is not a valid runtime graph document");
    const kb::assets::AssetHandle<kb::render::RenderMaterialGraphDocument> graphHandle =
        scene.Assets().Manager().Load<kb::render::RenderMaterialGraphDocument>(graphMetadata->id);
    kb::editor::tests::Require(graphHandle.IsLoaded(), "KBMAT-GRAPH-0005: Created Material Graph asset should load through AssetManager");

    kb::editor::tests::Require(authoring.CreateMaterialType("/Game/Materials"), "KBMAT-GRAPH-0005: Editor authoring did not create a Material Type asset");
    const std::filesystem::path typePath = TempRoot() / "Project" / "Assets" / "Materials" / "NewMaterialType.kbmaterialtype";
    kb::editor::tests::Require(std::filesystem::exists(typePath), "KBMAT-GRAPH-0005: Editor authoring did not write NewMaterialType.kbmaterialtype");
    const kb::assets::AssetMetadata* typeMetadata = manager.Registry().FindByPath("/Game/Materials/NewMaterialType.kbmaterialtype");
    kb::editor::tests::Require(typeMetadata != nullptr && typeMetadata->type == kb::render::kRenderMaterialTypeAssetType,
        "KBMAT-GRAPH-0005: Material Type authoring registered wrong metadata");
    kb::editor::tests::Require(browser.SelectedAsset() == typeMetadata->id, "KBMAT-GRAPH-0005: Material Type authoring did not select the created asset");
    const std::optional<kb::render::RenderMaterialTypeDocument> type = kb::render::RenderMaterialTypeAssetLoader::LoadType(typePath);
    kb::editor::tests::Require(type.has_value() && type->stableTypeId == "graph.surface" && !type->schema.parameters.empty() && !type->schema.migrations.empty(),
        "KBMAT-GRAPH-0005: Created Material Type asset is not a complete runtime schema document");
    const kb::assets::AssetHandle<kb::render::RenderMaterialTypeDocument> typeHandle =
        scene.Assets().Manager().Load<kb::render::RenderMaterialTypeDocument>(typeMetadata->id);
    kb::editor::tests::Require(typeHandle.IsLoaded(), "KBMAT-GRAPH-0005: Created Material Type asset should load through AssetManager");

    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
}

void RunCreateMaterialFunctionAssetThroughEditorAuthoringTest() {
    CleanTempRoot();

    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(scene.Assets().MountProject(TempRoot() / "Project"), "KBMAT-MAT42: Material function authoring test could not mount project assets");

    kb::editor::EditorMaterialAssetAuthoring authoring{ scene, browser, console };
    kb::editor::tests::Require(authoring.CreateFunction("/Game/Materials"), "KBMAT-MAT42: Editor authoring did not create a Material Function asset");

    const std::filesystem::path functionPath = TempRoot() / "Project" / "Assets" / "Materials" / "NewMaterialFunction.kbmatfn";
    kb::editor::tests::Require(std::filesystem::exists(functionPath), "KBMAT-MAT42: Editor authoring did not write NewMaterialFunction.kbmatfn");

    const kb::assets::AssetManager& manager = scene.Assets().Manager();
    const kb::assets::AssetMetadata* functionMetadata = manager.Registry().FindByPath("/Game/Materials/NewMaterialFunction.kbmatfn");
    kb::editor::tests::Require(functionMetadata != nullptr && functionMetadata->type == kb::render::kRenderMaterialFunctionAssetType,
        "KBMAT-MAT42: Material Function authoring registered wrong metadata");
    kb::editor::tests::Require(functionMetadata->runtimeLoadable, "KBMAT-MAT42: Material Function asset must be runtime-loadable");
    kb::editor::tests::Require(browser.SelectedAsset() == functionMetadata->id,
        "KBMAT-MAT42: Material Function authoring did not select the created asset");

    const std::optional<kb::render::RenderMaterialFunctionAssetData> function = kb::render::RenderMaterialFunctionAssetLoader::LoadFunction(functionPath);
    kb::editor::tests::Require(function.has_value() && function->graph.storageModel == "material-function-asset",
        "KBMAT-MAT42: Created Material Function asset is not a function graph document");
    const kb::render::RenderMaterialGraphNode* input = kb::render::FindRenderMaterialGraphNode(function->graph, 1U);
    const kb::render::RenderMaterialGraphNode* output = kb::render::FindRenderMaterialGraphNode(function->graph, 2U);
    kb::editor::tests::Require(input != nullptr && input->kind == kb::render::RenderMaterialGraphNodeKind::FunctionInput &&
            output != nullptr && output->kind == kb::render::RenderMaterialGraphNodeKind::FunctionOutput,
        "KBMAT-MAT42: Created Material Function must contain FunctionInput and FunctionOutput nodes");
    kb::editor::tests::Require(function->graph.links.size() == 1U &&
            function->graph.links.front().fromNodeId == 1U &&
            function->graph.links.front().fromPin == "value" &&
            function->graph.links.front().toNodeId == 2U &&
            function->graph.links.front().toPin == "value",
        "KBMAT-MAT42: Created Material Function must wire FunctionInput to FunctionOutput");

    const kb::assets::AssetHandle<kb::render::RenderMaterialFunctionAssetData> handle =
        scene.Assets().Manager().Load<kb::render::RenderMaterialFunctionAssetData>(functionMetadata->id);
    kb::editor::tests::Require(handle.IsLoaded(), "KBMAT-MAT42: Created Material Function should load through AssetManager");

    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
}

void RunCreateMaterialFromGraphAndMaterialTypeThroughEditorAuthoringTest() {
    CleanTempRoot();

    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(scene.Assets().MountProject(TempRoot() / "Project"), "KBMAT-GRAPH-0301: Material graph authoring test could not mount project assets");

    kb::editor::EditorMaterialAssetAuthoring authoring{ scene, browser, console };
    kb::editor::tests::Require(authoring.CreateGraph("/Game/Materials"), "KBMAT-GRAPH-0301: Could not create source Material Graph asset");

    const std::filesystem::path graphPath = TempRoot() / "Project" / "Assets" / "Materials" / "NewMaterialGraph.kbmaterialgraph";
    kb::render::RenderMaterialGraphDocument graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    graph.storageModel = "material-graph-asset";
    graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ParameterColor,
        .positionX = -220,
        .positionY = 40,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "tintColor",
            .displayName = "Tint Color",
            .defaultValueHint = "1 1 1 1",
            .editorOrder = 10U,
        },
    });
    graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 3U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ParameterScalar,
        .positionX = -220,
        .positionY = 150,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "roughnessFactor",
            .displayName = "Roughness",
            .defaultValueHint = "0.42",
            .hasRange = true,
            .rangeMin = 0.0F,
            .rangeMax = 1.0F,
            .editorOrder = 20U,
        },
    });
    graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 4U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ParameterTexture,
        .positionX = -220,
        .positionY = 260,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "albedo",
            .displayName = "Albedo",
            .textureRole = "baseColor",
            .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
            .editorOrder = 30U,
        },
    });
    graph.links.push_back(MakeMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::ParameterColor,
        2U,
        "rgba",
        kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "baseColor"));
    graph.links.push_back(MakeMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::ParameterScalar,
        3U,
        "value",
        kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "roughness"));
    kb::editor::tests::Require(kb::render::RenderMaterialGraphAssetLoader::SaveGraph(graphPath, graph), "KBMAT-GRAPH-0301: Could not write source Material Graph fixture");
    static_cast<void>(scene.Assets().Discover());

    const kb::assets::AssetMetadata* graphMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Materials/NewMaterialGraph.kbmaterialgraph");
    kb::editor::tests::Require(graphMetadata != nullptr && graphMetadata->type == kb::render::kRenderMaterialGraphAssetType,
        "KBMAT-GRAPH-0301: Source Material Graph metadata was not discovered");
    const kb::assets::AssetId sourceGraphId = graphMetadata->id;
    const std::string sourceGraphPath = graphMetadata->virtualPath.generic_string();
    kb::editor::tests::Require(authoring.CreateMaterialFromGraph(sourceGraphId), "KBMAT-GRAPH-0301: Create Material From Graph command did not produce runtime assets");

    const std::filesystem::path generatedTypePath = TempRoot() / "Project" / "Assets" / "Materials" / "NewMaterialGraphType.kbmaterialtype";
    const std::filesystem::path graphMaterialPath = TempRoot() / "Project" / "Assets" / "Materials" / "NewMaterialGraphMaterial.kbmat";
    kb::editor::tests::Require(std::filesystem::exists(generatedTypePath), "KBMAT-GRAPH-0301: Create Material From Graph did not write generated Material Type");
    kb::editor::tests::Require(std::filesystem::exists(graphMaterialPath), "KBMAT-GRAPH-0301: Create Material From Graph did not write graph-backed Material");

    const kb::assets::AssetMetadata* generatedTypeMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Materials/NewMaterialGraphType.kbmaterialtype");
    const kb::assets::AssetMetadata* graphMaterialMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Materials/NewMaterialGraphMaterial.kbmat");
    kb::editor::tests::Require(generatedTypeMetadata != nullptr && generatedTypeMetadata->type == kb::render::kRenderMaterialTypeAssetType,
        "KBMAT-GRAPH-0301: Generated Material Type metadata is missing");
    kb::editor::tests::Require(graphMaterialMetadata != nullptr && graphMaterialMetadata->type == "RenderMaterial",
        "KBMAT-GRAPH-0301: Graph-backed Material metadata is missing");
    kb::editor::tests::Require(browser.SelectedAsset() == graphMaterialMetadata->id, "KBMAT-GRAPH-0301: Graph-backed Material should be selected after creation");
    const kb::assets::AssetId generatedTypeId = generatedTypeMetadata->id;
    const std::uint64_t generatedTypeRawId = generatedTypeMetadata->id.value;
    const kb::assets::AssetId graphMaterialId = graphMaterialMetadata->id;

    const std::optional<kb::render::RenderMaterialTypeDocument> generatedType = kb::render::RenderMaterialTypeAssetLoader::LoadType(generatedTypePath);
    kb::editor::tests::Require(generatedType.has_value(), "KBMAT-GRAPH-0301: Generated Material Type could not be loaded");
    kb::editor::tests::Require(generatedType->stableTypeId == "graph.NewMaterialGraph" && generatedType->schema.typeName == generatedType->stableTypeId,
        "KBMAT-GRAPH-0301: Generated Material Type did not preserve stable graph type id");
    const auto hasParameter = [&generatedType](std::string_view name) {
        return std::ranges::any_of(generatedType->schema.parameters, [name](const kb::render::RenderMaterialParameterSchema& parameter) {
            return parameter.name == name;
        });
    };
    const auto hasTextureSlot = [&generatedType](std::string_view name) {
        return std::ranges::any_of(generatedType->schema.textureSlots, [name](const kb::render::RenderMaterialTextureSlotSchema& slot) {
            return slot.assetIdFieldName == name;
        });
    };
    kb::editor::tests::Require(hasParameter("tintColor") && hasParameter("roughnessFactor") && hasTextureSlot("albedoTextureAssetId"),
        "KBMAT-GRAPH-0301: Generated Material Type schema did not include graph parameter and texture slots");
    kb::editor::tests::Require(!generatedType->renderPasses.empty() && !generatedType->permutationKeys.empty() && !generatedType->requiredResources.empty(),
        "KBMAT-GRAPH-0301: Generated Material Type is missing runtime render passes, permutations, or resources");

    const std::optional<kb::render::RenderMaterialAssetData> graphMaterial = kb::render::RenderMaterialAssetLoader::LoadMaterial(graphMaterialPath);
    kb::editor::tests::Require(graphMaterial.has_value(), "KBMAT-GRAPH-0301: Graph-backed Material could not be loaded");
    kb::editor::tests::Require(graphMaterial->materialType == generatedType->stableTypeId && graphMaterial->materialTypeVersion == generatedType->version,
        "KBMAT-GRAPH-0301: Graph-backed Material did not reference generated Material Type schema");
    kb::editor::tests::Require(graphMaterial->materialTypeAssetId == generatedTypeRawId &&
            graphMaterial->materialTypeAssetPath == "/Game/Materials/NewMaterialGraphType.kbmaterialtype",
        "KBMAT-GRAPH-0301: Graph-backed Material did not persist generated Material Type asset reference");
    kb::editor::tests::Require(graphMaterial->graphSourceAssetId == sourceGraphId.value &&
            graphMaterial->graphSourceAssetPath == sourceGraphPath,
        "KBMAT-GRAPH-0304: Graph-backed Material did not persist source Material Graph asset reference");
    kb::editor::tests::Require(graphMaterial->graph.storageModel == "inline-kbmat" && graphMaterial->graph.nodes.size() == graph.nodes.size() && graphMaterial->graph.links.size() == graph.links.size(),
        "KBMAT-GRAPH-0301: Graph-backed Material did not inline the source graph document");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(graphMaterial->desc.roughnessFactor, 0.42F),
        "KBMAT-GRAPH-0301: Graph-backed Material did not apply scalar schema defaults");

    kb::editor::tests::Require(authoring.CreateMaterialFromMaterialType(generatedTypeId), "KBMAT-GRAPH-0301: Create Material From Material Type command failed");
    const std::filesystem::path typeMaterialPath = TempRoot() / "Project" / "Assets" / "Materials" / "NewMaterialGraphTypeMaterial.kbmat";
    kb::editor::tests::Require(std::filesystem::exists(typeMaterialPath), "KBMAT-GRAPH-0301: Create Material From Material Type did not write a Material asset");
    const std::optional<kb::render::RenderMaterialAssetData> typeMaterial = kb::render::RenderMaterialAssetLoader::LoadMaterial(typeMaterialPath);
    kb::editor::tests::Require(typeMaterial.has_value(), "KBMAT-GRAPH-0301: Material From Material Type could not be loaded");
    kb::editor::tests::Require(typeMaterial->materialType == generatedType->stableTypeId &&
            typeMaterial->materialTypeAssetId == generatedTypeRawId &&
            typeMaterial->materialTypeAssetPath == "/Game/Materials/NewMaterialGraphType.kbmaterialtype",
        "KBMAT-GRAPH-0301: Material From Material Type did not persist parent Material Type reference");
    kb::editor::tests::Require(typeMaterial->graph.nodes.size() == 1U && typeMaterial->graph.links.empty(),
        "KBMAT-GRAPH-0301: Material From Material Type should use a default editable graph document");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(typeMaterial->desc.roughnessFactor, 0.42F),
        "KBMAT-GRAPH-0301: Material From Material Type did not apply schema defaults");

    kb::editor::tests::Require(authoring.CreateInstance(graphMaterialId), "KBMAT-GRAPH-0301: Create Material Instance command failed for graph-backed material");
    const std::filesystem::path graphInstancePath = TempRoot() / "Project" / "Assets" / "Materials" / "NewMaterialGraphMaterialInstance.kbmatinst";
    const std::optional<kb::render::RenderMaterialInstanceAssetData> instance = kb::render::RenderMaterialInstanceAssetLoader::LoadInstance(graphInstancePath);
    kb::editor::tests::Require(instance.has_value() && instance->parentMaterialAssetId == graphMaterialId,
        "KBMAT-GRAPH-0301: Material Instance did not persist the graph-backed parent material id");
    const kb::assets::AssetHandle<kb::render::RenderMaterialAssetData> runtimeMaterial =
        scene.Assets().Manager().Load<kb::render::RenderMaterialAssetData>(graphMaterialId);
    kb::editor::tests::Require(runtimeMaterial.IsLoaded(), "KBMAT-GRAPH-0301: Graph-backed Material should load through runtime AssetManager");

    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
}

void RunCreateMaterialInstanceAssetThroughEditorAuthoringTest() {
    CleanTempRoot();

    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(scene.Assets().MountProject(TempRoot() / "Project"), "Material instance authoring test could not mount project assets");

    kb::editor::EditorMaterialAssetAuthoring authoring{ scene, browser, console };
    kb::editor::tests::Require(authoring.Create("/Game/Materials"), "Material instance authoring test could not create parent material");

    const kb::assets::AssetMetadata* parent = scene.Assets().Manager().Registry().FindByPath("/Game/Materials/NewMaterial.kbmat");
    kb::editor::tests::Require(parent != nullptr && parent->type == "RenderMaterial", "Material instance authoring test did not discover parent material metadata");
    const kb::assets::AssetId parentId = parent->id;
    kb::editor::tests::Require(authoring.CreateInstance(parentId), "Editor material authoring did not create a material instance asset");

    const std::filesystem::path instancePath = TempRoot() / "Project" / "Assets" / "Materials" / "NewMaterialInstance.kbmatinst";
    kb::editor::tests::Require(std::filesystem::exists(instancePath), "Editor material authoring did not write NewMaterialInstance.kbmatinst");

    const kb::assets::AssetMetadata* instanceMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Materials/NewMaterialInstance.kbmatinst");
    kb::editor::tests::Require(instanceMetadata != nullptr, "Editor material authoring did not register the created material instance");
    kb::editor::tests::Require(instanceMetadata->type == "RenderMaterialInstance", "Editor material authoring registered the wrong material instance asset type");
    kb::editor::tests::Require(browser.SelectedAsset() == instanceMetadata->id, "Editor material authoring did not select the created material instance asset");

    const std::optional<kb::render::RenderMaterialInstanceAssetData> loaded = kb::render::RenderMaterialInstanceAssetLoader::LoadInstance(instancePath);
    kb::editor::tests::Require(loaded.has_value(), "Editor material authoring wrote a material instance file that could not be loaded");
    kb::editor::tests::Require(loaded->documentVersion == kb::render::kRenderMaterialInstanceAssetDocumentVersion, "Material instance did not preserve document version");
    kb::editor::tests::Require(loaded->hasExplicitDocumentVersion, "Material instance did not write explicit document version metadata");
    kb::editor::tests::Require(loaded->parentMaterialAssetId == parentId, "Material instance did not reference the selected parent material asset id");

    const kb::assets::AssetHandle<kb::render::RenderMaterialInstanceAssetData> runtimeLoaded = scene.Assets().Manager().Load<kb::render::RenderMaterialInstanceAssetData>(instanceMetadata->id);
    kb::editor::tests::Require(runtimeLoaded.IsLoaded(), "Editor material authoring did not make the created material instance loadable through AssetManager");

    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
}

void RunEditMaterialAssetThroughEditorAuthoringTest() {
    CleanTempRoot();

    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(scene.Assets().MountProject(TempRoot() / "Project"), "Material edit test could not mount project assets");

    kb::editor::EditorMaterialAssetAuthoring authoring{ scene, browser, console };
    kb::editor::tests::Require(authoring.Create("/Game/Materials"), "Material edit test could not create a material asset");
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().FindByPath("/Game/Materials/NewMaterial.kbmat");
    kb::editor::tests::Require(metadata != nullptr, "Material edit test did not discover created material metadata");
    const kb::assets::AssetId materialId = metadata->id;
    const std::uint64_t contentHashBeforeEdit = metadata->contentHash;
    const kb::assets::AssetHandle<kb::render::RenderMaterialAssetData> loadedBeforeEdit = scene.Assets().Manager().Load<kb::render::RenderMaterialAssetData>(materialId);
    kb::editor::tests::Require(loadedBeforeEdit.IsLoaded(), "Material edit test could not load created material into runtime cache");

    kb::editor::tests::Require(authoring.SetBaseColor(materialId, 0, 1.25F), "Material edit test could not set base color red");
    kb::editor::tests::Require(authoring.SetBaseColor(materialId, 1, 0.5F), "Material edit test could not set base color green");
    kb::editor::tests::Require(authoring.SetBaseColor(materialId, 2, -0.25F), "Material edit test could not set base color blue");
    kb::editor::tests::Require(authoring.SetBaseColor(materialId, 3, 0.75F), "Material edit test could not set base color alpha");
    kb::editor::tests::Require(authoring.SetMetallicFactor(materialId, 2.0F), "Material edit test could not set metallic factor");
    kb::editor::tests::Require(authoring.SetRoughnessFactor(materialId, -2.0F), "Material edit test could not set roughness factor");
    kb::editor::tests::Require(authoring.SetNormalScale(materialId, -3.0F), "Material edit test could not set normal scale");
    kb::editor::tests::Require(authoring.SetOcclusionStrength(materialId, 0.25F), "Material edit test could not set occlusion strength");
    kb::editor::tests::Require(authoring.SetEmissiveColor(materialId, 0, 0.1F), "Material edit test could not set emissive red");
    kb::editor::tests::Require(authoring.SetEmissiveColor(materialId, 1, 0.2F), "Material edit test could not set emissive green");
    kb::editor::tests::Require(authoring.SetEmissiveColor(materialId, 2, 0.3F), "Material edit test could not set emissive blue");
    kb::editor::tests::Require(authoring.SetEmissiveStrength(materialId, -1.0F), "Material edit test could not set emissive strength");
    kb::editor::tests::Require(authoring.SetAlphaCutoff(materialId, 0.45F), "Material edit test could not set alpha cutoff");
    kb::editor::tests::Require(authoring.SetAlphaMode(materialId, kb::render::RenderMaterialAlphaMode::Mask), "Material edit test could not set alpha mode");
    kb::editor::tests::Require(authoring.CycleAlphaMode(materialId), "Material edit test could not cycle alpha mode");
    kb::editor::tests::Require(authoring.ToggleDoubleSided(materialId), "Material edit test could not toggle double-sided flag");

    const kb::assets::AssetMetadata* editedMetadata = scene.Assets().Manager().Registry().Find(materialId);
    kb::editor::tests::Require(editedMetadata != nullptr, "Material edit test lost material metadata after save");
    kb::editor::tests::Require(editedMetadata->contentHash != contentHashBeforeEdit, "Material edit should update registry content hash for Project Files thumbnail refresh");
    kb::editor::tests::Require(!scene.Assets().Manager().IsLoaded(materialId), "Material edit should unload stale runtime cache after save");
    const std::filesystem::path materialPath = TempRoot() / "Project" / "Assets" / "Materials" / "NewMaterial.kbmat";
    const std::optional<kb::render::RenderMaterialAssetData> edited = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(edited.has_value(), "Material edit test wrote an unreadable material file");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(edited->desc.baseColor[0], 1.0F) && kb::editor::tests::NearlyEqual(edited->desc.baseColor[1], 0.5F) && kb::editor::tests::NearlyEqual(edited->desc.baseColor[2], 0.0F) && kb::editor::tests::NearlyEqual(edited->desc.baseColor[3], 0.75F), "Material edit test did not clamp/persist base color");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(edited->desc.metallicFactor, 1.0F), "Material edit test did not clamp metallic factor");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(edited->desc.roughnessFactor, 0.0F), "Material edit test did not clamp roughness factor");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(edited->desc.normalScale, 0.0F), "Material edit test did not clamp normal scale");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(edited->desc.occlusionStrength, 0.25F), "Material edit test did not persist occlusion strength");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(edited->desc.emissiveColor[0], 0.1F) && kb::editor::tests::NearlyEqual(edited->desc.emissiveColor[1], 0.2F) && kb::editor::tests::NearlyEqual(edited->desc.emissiveColor[2], 0.3F), "Material edit test did not persist emissive color");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(edited->desc.emissiveStrength, 0.0F), "Material edit test did not clamp emissive strength");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(edited->desc.alphaCutoff, 0.45F), "Material edit test did not persist alpha cutoff");
    kb::editor::tests::Require(edited->desc.alphaMode == kb::render::RenderMaterialAlphaMode::Blend, "Material edit test did not persist cycled alpha mode");
    kb::editor::tests::Require(edited->desc.doubleSided, "Material edit test did not persist double-sided flag");

    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
}

void RunMaterialCreateEditSaveReopenE2ETest() {
    CleanTempRoot();

    const std::filesystem::path projectRoot = TempRoot() / "Project";
    const std::filesystem::path materialPath = projectRoot / "Assets" / "Materials" / "NewMaterial.kbmat";
    kb::assets::AssetId createdMaterialId{};
    {
        kb::scene::Scene scene;
        kb::editor::EditorAssetBrowserState browser;
        kb::editor::EditorConsoleState console;
        kb::editor::tests::Require(scene.Assets().MountProject(projectRoot), "Material E2E test could not mount project assets");

        kb::editor::EditorMaterialAssetAuthoring authoring{ scene, browser, console };
        kb::editor::tests::Require(authoring.Create("/Game/Materials"), "Material E2E test could not create a material asset");
        const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().FindByPath("/Game/Materials/NewMaterial.kbmat");
        kb::editor::tests::Require(metadata != nullptr, "Material E2E test did not discover created material metadata");
        createdMaterialId = metadata->id;

        kb::editor::tests::Require(authoring.SetBaseColor(createdMaterialId, 0, 0.125F), "Material E2E test could not set base color red");
        kb::editor::tests::Require(authoring.SetBaseColor(createdMaterialId, 1, 0.5F), "Material E2E test could not set base color green");
        kb::editor::tests::Require(authoring.SetBaseColor(createdMaterialId, 2, 0.875F), "Material E2E test could not set base color blue");
        kb::editor::tests::Require(authoring.SetBaseColor(createdMaterialId, 3, 0.625F), "Material E2E test could not set base color alpha");
        kb::editor::tests::Require(authoring.SetRoughnessFactor(createdMaterialId, 0.375F), "Material E2E test could not set roughness");

        kb::editor::tests::Require(std::filesystem::exists(materialPath), "Material E2E test did not save the edited material file");
        kb::editor::tests::Require(!scene.Assets().Manager().IsLoaded(createdMaterialId), "Material E2E edit should unload stale runtime cache after save");
    }

    kb::scene::Scene reopenedScene;
    kb::editor::tests::Require(reopenedScene.Assets().MountProject(projectRoot), "Material E2E reopen test could not mount project assets");
    kb::editor::tests::Require(reopenedScene.Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()), "Material E2E reopen test could not register material loader");
    kb::editor::tests::Require(reopenedScene.Assets().Discover() >= 1U, "Material E2E reopen test did not discover saved assets");
    const kb::assets::AssetMetadata* reopenedMetadata = reopenedScene.Assets().Manager().Registry().FindByPath("/Game/Materials/NewMaterial.kbmat");
    kb::editor::tests::Require(reopenedMetadata != nullptr, "Material E2E reopen test did not rediscover material metadata");
    kb::editor::tests::Require(reopenedMetadata->type == "RenderMaterial", "Material E2E reopen test rediscovered material with wrong type");

    const kb::assets::AssetHandle<kb::render::RenderMaterialAssetData> reopened = reopenedScene.Assets().Manager().Load<kb::render::RenderMaterialAssetData>(reopenedMetadata->id);
    kb::editor::tests::Require(reopened.IsLoaded(), "Material E2E reopen test could not load material through AssetManager");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(reopened->desc.baseColor[0], 0.125F) &&
            kb::editor::tests::NearlyEqual(reopened->desc.baseColor[1], 0.5F) &&
            kb::editor::tests::NearlyEqual(reopened->desc.baseColor[2], 0.875F) &&
            kb::editor::tests::NearlyEqual(reopened->desc.baseColor[3], 0.625F),
        "Material E2E reopen test did not preserve base color");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(reopened->desc.roughnessFactor, 0.375F), "Material E2E reopen test did not preserve roughness");

    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
}

void RunDuplicateMaterialAssetPreservesParametersTest() {
    CleanTempRoot();

    const std::filesystem::path projectRoot = TempRoot() / "Project";
    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(scene.Assets().MountProject(projectRoot), "Material duplicate test could not mount project assets");

    kb::editor::EditorMaterialAssetAuthoring authoring{ scene, browser, console };
    kb::editor::tests::Require(authoring.Create("/Game/Materials"), "Material duplicate test could not create source material");
    const kb::assets::AssetMetadata* sourceMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Materials/NewMaterial.kbmat");
    kb::editor::tests::Require(sourceMetadata != nullptr, "Material duplicate test did not discover source material");
    const kb::assets::AssetId sourceId = sourceMetadata->id;
    kb::editor::tests::Require(authoring.SetBaseColor(sourceId, 0, 0.25F), "Material duplicate test could not set red");
    kb::editor::tests::Require(authoring.SetBaseColor(sourceId, 1, 0.5F), "Material duplicate test could not set green");
    kb::editor::tests::Require(authoring.SetBaseColor(sourceId, 2, 0.75F), "Material duplicate test could not set blue");
    kb::editor::tests::Require(authoring.SetBaseColor(sourceId, 3, 0.875F), "Material duplicate test could not set alpha");
    kb::editor::tests::Require(authoring.SetMetallicFactor(sourceId, 0.625F), "Material duplicate test could not set metallic");
    kb::editor::tests::Require(authoring.SetRoughnessFactor(sourceId, 0.375F), "Material duplicate test could not set roughness");

    kb::editor::EditorMaterialAssetGateway gateway{ scene, browser };
    const std::optional<std::filesystem::path> duplicatePath = gateway.DuplicateMaterial(sourceId);
    kb::editor::tests::Require(duplicatePath.has_value(), "Material duplicate test could not duplicate material");
    kb::editor::tests::Require(duplicatePath->filename() == "NewMaterialCopy.kbmat", "Material duplicate should use a stable copy filename");

    const kb::assets::AssetMetadata* duplicateMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Materials/NewMaterialCopy.kbmat");
    kb::editor::tests::Require(duplicateMetadata != nullptr, "Material duplicate test did not discover duplicate metadata");
    kb::editor::tests::Require(duplicateMetadata->id != sourceId, "Material duplicate should have a different asset id");
    kb::editor::tests::Require(duplicateMetadata->type == "RenderMaterial", "Material duplicate should remain a RenderMaterial asset");

    const std::optional<kb::render::RenderMaterialAssetData> duplicate = kb::render::RenderMaterialAssetLoader::LoadMaterial(duplicateMetadata->physicalPath);
    kb::editor::tests::Require(duplicate.has_value(), "Material duplicate test wrote an unreadable duplicate file");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(duplicate->desc.baseColor[0], 0.25F) &&
            kb::editor::tests::NearlyEqual(duplicate->desc.baseColor[1], 0.5F) &&
            kb::editor::tests::NearlyEqual(duplicate->desc.baseColor[2], 0.75F) &&
            kb::editor::tests::NearlyEqual(duplicate->desc.baseColor[3], 0.875F),
        "Material duplicate should preserve base color parameters");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(duplicate->desc.metallicFactor, 0.625F), "Material duplicate should preserve metallic factor");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(duplicate->desc.roughnessFactor, 0.375F), "Material duplicate should preserve roughness factor");

    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
}

void RunMaterialTextureSlotAuthoringTest() {
    CleanTempRoot();

    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(scene.Assets().MountProject(TempRoot() / "Project"), "Material texture slot test could not mount project assets");

    kb::editor::EditorMaterialAssetAuthoring authoring{ scene, browser, console };
    kb::editor::tests::Require(authoring.Create("/Game/Materials"), "Material texture slot test could not create a material asset");
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    const kb::assets::AssetMetadata* material = manager.Registry().FindByPath("/Game/Materials/NewMaterial.kbmat");
    kb::editor::tests::Require(material != nullptr, "Material texture slot test did not discover created material metadata");
    const kb::assets::AssetId materialId = material->id;

    const std::filesystem::path sourceRoot = TempRoot() / "Sources";
    WriteTextFile(sourceRoot / "Albedo.png", "albedo");
    WriteTextFile(sourceRoot / "Normal.png", "normal");
    WriteTextFile(sourceRoot / "MR.png", "metallic roughness");
    WriteTextFile(sourceRoot / "AO.png", "occlusion");
    WriteTextFile(sourceRoot / "Emissive.png", "emissive");
    const std::array<std::filesystem::path, 5U> sourceTextures{
        sourceRoot / "Albedo.png",
        sourceRoot / "Normal.png",
        sourceRoot / "MR.png",
        sourceRoot / "AO.png",
        sourceRoot / "Emissive.png",
    };
    const kb::assets::AssetImportResult imported = kb::assets::AssetImportService::ImportFiles(manager, sourceTextures, "/Game/Textures");
    kb::editor::tests::Require(imported.Succeeded(), "Material texture slot test could not import source textures");
    static_cast<void>(manager.RegisterAsset(Metadata("Cube", "RenderMesh", "/Game/Meshes/Cube.21kb")));

    const kb::assets::AssetId albedo = RequireAssetId(manager, imported.items[0].virtualPath, "Material texture slot test did not register albedo texture");
    const kb::assets::AssetId normal = RequireAssetId(manager, imported.items[1].virtualPath, "Material texture slot test did not register normal texture");
    const kb::assets::AssetId metallicRoughness = RequireAssetId(manager, imported.items[2].virtualPath, "Material texture slot test did not register metallic-roughness texture");
    const kb::assets::AssetId occlusion = RequireAssetId(manager, imported.items[3].virtualPath, "Material texture slot test did not register occlusion texture");
    const kb::assets::AssetId emissive = RequireAssetId(manager, imported.items[4].virtualPath, "Material texture slot test did not register emissive texture");
    const kb::assets::AssetId mesh = RequireAssetId(manager, "/Game/Meshes/Cube.21kb", "Material texture slot test did not register mesh asset");

    kb::editor::tests::Require(!authoring.SetTextureAsset(materialId, kb::editor::EditorMaterialTextureSlot::Albedo, mesh), "Material texture slot test accepted a non-texture asset");
    kb::editor::tests::Require(authoring.SetTextureAsset(materialId, kb::editor::EditorMaterialTextureSlot::Albedo, albedo), "Material texture slot test could not set albedo texture");
    kb::editor::tests::Require(authoring.SetTextureAsset(materialId, kb::editor::EditorMaterialTextureSlot::Normal, normal), "Material texture slot test could not set normal texture");
    kb::editor::tests::Require(authoring.SetTextureAsset(materialId, kb::editor::EditorMaterialTextureSlot::MetallicRoughness, metallicRoughness), "Material texture slot test could not set metallic-roughness texture");
    kb::editor::tests::Require(authoring.SetTextureAsset(materialId, kb::editor::EditorMaterialTextureSlot::Occlusion, occlusion), "Material texture slot test could not set occlusion texture");
    kb::editor::tests::Require(authoring.SetTextureAsset(materialId, kb::editor::EditorMaterialTextureSlot::Emissive, emissive), "Material texture slot test could not set emissive texture");

    const std::filesystem::path materialPath = TempRoot() / "Project" / "Assets" / "Materials" / "NewMaterial.kbmat";
    std::optional<kb::render::RenderMaterialAssetData> edited = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(edited.has_value(), "Material texture slot test wrote an unreadable material file");
    kb::editor::tests::Require(edited->desc.albedoTextureAssetId == albedo.value, "Material texture slot test did not persist albedo texture id");
    kb::editor::tests::Require(edited->desc.normalTextureAssetId == normal.value, "Material texture slot test did not persist normal texture id");
    kb::editor::tests::Require(edited->desc.metallicRoughnessTextureAssetId == metallicRoughness.value, "Material texture slot test did not persist metallic-roughness texture id");
    kb::editor::tests::Require(edited->desc.occlusionTextureAssetId == occlusion.value, "Material texture slot test did not persist occlusion texture id");
    kb::editor::tests::Require(edited->desc.emissiveTextureAssetId == emissive.value, "Material texture slot test did not persist emissive texture id");

    kb::editor::tests::Require(authoring.SetTextureAsset(materialId, kb::editor::EditorMaterialTextureSlot::Emissive, {}), "Material texture slot test could not clear emissive texture");
    edited = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(edited.has_value() && edited->desc.emissiveTextureAssetId == 0U, "Material texture slot test did not clear emissive texture id");

    kb::editor::tests::Require(authoring.CycleTextureAsset(materialId, kb::editor::EditorMaterialTextureSlot::Emissive), "Material texture slot test could not cycle emissive texture picker");
    edited = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(edited.has_value() && edited->desc.emissiveTextureAssetId != 0U, "Material texture slot picker did not assign an available texture");

    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
}

void RunMaterialTextureSlotValidationTest() {
    const kb::assets::AssetMetadata normal = Metadata("Stone_Normal", "RenderTexture", "/Game/Textures/Stone_Normal.png");
    const kb::assets::AssetMetadata baseColor = Metadata("Stone_BaseColor", "RenderTexture", "/Game/Textures/Stone_BaseColor.png");
    const kb::assets::AssetMetadata metallicRoughness = Metadata("Stone_MetallicRoughness", "RenderTexture", "/Game/Textures/Stone_MetallicRoughness.png");
    const kb::assets::AssetMetadata occlusion = Metadata("Stone_AO", "RenderTexture", "/Game/Textures/Stone_AO.png");
    const kb::assets::AssetMetadata custom = Metadata("StoneLayerA", "RenderTexture", "/Game/Textures/StoneLayerA.png");

    const kb::editor::EditorMaterialTextureSlotValidationResult acceptedNormal =
        kb::editor::EditorMaterialTextureSlotValidation::Validate(normal, kb::editor::EditorMaterialTextureSlot::Normal);
    kb::editor::tests::Require(acceptedNormal.accepted, "Material texture validation should accept a normal map in the Normal slot");
    kb::editor::tests::Require(acceptedNormal.expectedColorSpace == kb::editor::EditorMaterialTextureColorSpace::Linear, "Normal texture slot should expect linear data");

    const kb::editor::EditorMaterialTextureSlotValidationResult rejectedNormalAsBaseColor =
        kb::editor::EditorMaterialTextureSlotValidation::Validate(normal, kb::editor::EditorMaterialTextureSlot::Albedo);
    kb::editor::tests::Require(!rejectedNormalAsBaseColor.accepted, "Material texture validation should reject a normal map in the Base Color slot");
    kb::editor::tests::Require(rejectedNormalAsBaseColor.expectedColorSpace == kb::editor::EditorMaterialTextureColorSpace::Srgb, "Base Color texture slot should expect sRGB data");
    const std::string rejection = kb::editor::EditorMaterialTextureSlotValidation::RejectionMessage(normal, rejectedNormalAsBaseColor);
    kb::editor::tests::Require(rejection.find("Stone_Normal") != std::string::npos &&
            rejection.find("Normal/linear") != std::string::npos &&
            rejection.find("Base Color") != std::string::npos &&
            rejection.find("sRGB") != std::string::npos,
        "KBMAT-UE-0006: Material texture validation diagnostics should include inferred and expected color-space policy");

    const kb::editor::EditorMaterialTextureSlotValidationResult acceptedBaseColor =
        kb::editor::EditorMaterialTextureSlotValidation::Validate(baseColor, kb::editor::EditorMaterialTextureSlot::Albedo);
    kb::editor::tests::Require(acceptedBaseColor.accepted, "Material texture validation should accept base color textures in the Base Color slot");
    const kb::editor::EditorMaterialTextureSlotValidationResult rejectedBaseColorAsNormal =
        kb::editor::EditorMaterialTextureSlotValidation::Validate(baseColor, kb::editor::EditorMaterialTextureSlot::Normal);
    kb::editor::tests::Require(!rejectedBaseColorAsNormal.accepted &&
            rejectedBaseColorAsNormal.inferredColorSpace == kb::editor::EditorMaterialTextureColorSpace::Srgb &&
            rejectedBaseColorAsNormal.expectedColorSpace == kb::editor::EditorMaterialTextureColorSpace::Linear,
        "KBMAT-UE-0014: Normal texture validation should reject sRGB-looking base-color textures because the slot is linear");
    const kb::editor::EditorMaterialTextureSlotValidationResult rejectedBaseColorAsMetallicRoughness =
        kb::editor::EditorMaterialTextureSlotValidation::Validate(baseColor, kb::editor::EditorMaterialTextureSlot::MetallicRoughness);
    kb::editor::tests::Require(!rejectedBaseColorAsMetallicRoughness.accepted &&
            rejectedBaseColorAsMetallicRoughness.inferredColorSpace == kb::editor::EditorMaterialTextureColorSpace::Srgb &&
            rejectedBaseColorAsMetallicRoughness.expectedColorSpace == kb::editor::EditorMaterialTextureColorSpace::Linear,
        "KBMAT-UE-0014: Metallic-Roughness texture validation should reject sRGB-looking base-color textures because the slot is linear");
    const kb::editor::EditorMaterialTextureSlotValidationResult rejectedBaseColorAsOcclusion =
        kb::editor::EditorMaterialTextureSlotValidation::Validate(baseColor, kb::editor::EditorMaterialTextureSlot::Occlusion);
    kb::editor::tests::Require(!rejectedBaseColorAsOcclusion.accepted &&
            rejectedBaseColorAsOcclusion.inferredColorSpace == kb::editor::EditorMaterialTextureColorSpace::Srgb &&
            rejectedBaseColorAsOcclusion.expectedColorSpace == kb::editor::EditorMaterialTextureColorSpace::Linear,
        "KBMAT-UE-0014: Occlusion texture validation should reject sRGB-looking base-color textures because the slot is linear");
    kb::editor::tests::Require(kb::editor::EditorMaterialTextureSlotValidation::Validate(metallicRoughness, kb::editor::EditorMaterialTextureSlot::MetallicRoughness).accepted,
        "KBMAT-UE-0014: Metallic-Roughness texture validation should accept metallic-roughness textures");
    kb::editor::tests::Require(kb::editor::EditorMaterialTextureSlotValidation::Validate(occlusion, kb::editor::EditorMaterialTextureSlot::Occlusion).accepted,
        "KBMAT-UE-0014: Occlusion texture validation should accept AO textures");

    const kb::editor::EditorMaterialTextureSlotValidationResult acceptedUnknown =
        kb::editor::EditorMaterialTextureSlotValidation::Validate(custom, kb::editor::EditorMaterialTextureSlot::Occlusion);
    kb::editor::tests::Require(acceptedUnknown.accepted, "Material texture validation should allow ambiguous texture names");
    kb::editor::tests::Require(acceptedUnknown.inferredSemantic == kb::editor::EditorMaterialTextureSemantic::Unknown, "Ambiguous texture names should remain unclassified");
}

void RunMaterialAssetEditCommandUndoRedoTest() {
    CleanTempRoot();

    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(scene.Assets().MountProject(TempRoot() / "Project"), "Material command test could not mount project assets");

    kb::editor::EditorMaterialAssetAuthoring authoring{ scene, browser, console };
    kb::editor::tests::Require(authoring.Create("/Game/Materials"), "Material command test could not create a material asset");
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().FindByPath("/Game/Materials/NewMaterial.kbmat");
    kb::editor::tests::Require(metadata != nullptr, "Material command test did not discover created material metadata");
    const kb::assets::AssetId materialId = metadata->id;
    const std::filesystem::path materialPath = TempRoot() / "Project" / "Assets" / "Materials" / "NewMaterial.kbmat";

    kb::editor::EditorCommandStack stack;
    std::unique_ptr<kb::editor::EditorMaterialAssetEditCommand> command = kb::editor::EditorMaterialAssetEditCommand::Create(
        scene,
        materialId,
        std::make_unique<kb::editor::EditorMaterialMetallicFactorEdit>(2.0F));
    kb::editor::tests::Require(command != nullptr, "Material command test could not create edit command");
    kb::editor::tests::Require(stack.Execute(std::move(command)), "Material command test could not execute material edit command");
    kb::editor::tests::Require(!stack.LastCompletedCommandAffectsSceneDocument(), "Material command should not dirty the scene document");
    kb::editor::tests::Require(!stack.LastCompletedCommandAffectsHierarchySelection(), "Material command should not normalize hierarchy selection");

    std::optional<kb::render::RenderMaterialAssetData> edited = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(edited.has_value(), "Material command test wrote an unreadable edited material file");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(edited->desc.metallicFactor, 1.0F), "Material command test did not clamp/persist metallic factor");

    kb::editor::tests::Require(stack.Undo(), "Material command test could not undo material edit");
    edited = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(edited.has_value(), "Material command test wrote an unreadable undo material file");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(edited->desc.metallicFactor, 0.0F), "Material command undo did not restore metallic factor");

    kb::editor::tests::Require(stack.Redo(), "Material command test could not redo material edit");
    edited = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(edited.has_value(), "Material command test wrote an unreadable redo material file");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(edited->desc.metallicFactor, 1.0F), "Material command redo did not restore edited metallic factor");

    const std::optional<kb::render::RenderMaterialAssetData> beforeDrag = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(beforeDrag.has_value(), "Material command drag test could not read pre-drag material");
    kb::render::RenderMaterialAssetData liveDrag = *beforeDrag;
    liveDrag.desc.roughnessFactor = 0.25F;
    kb::editor::tests::Require(kb::editor::EditorMaterialAssetGateway::WriteExisting(scene, materialId, liveDrag), "Material command drag test could not write live drag material");
    const std::optional<kb::render::RenderMaterialAssetData> afterDrag = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(afterDrag.has_value(), "Material command drag test could not read post-drag material");
    kb::editor::tests::Require(
        stack.Execute(kb::editor::EditorMaterialAssetEditCommand::CreateRecorded(scene, materialId, "Drag Material", *beforeDrag, *afterDrag)),
        "KBMAT-1003: committed material drag edits must write through the command path");
    kb::editor::tests::Require(stack.Undo(), "Material command drag test could not undo grouped drag edit");
    edited = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(edited.has_value(), "Material command drag undo wrote an unreadable material file");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(edited->desc.roughnessFactor, beforeDrag->desc.roughnessFactor), "Material command drag undo did not restore roughness");
    kb::editor::tests::Require(stack.Redo(), "Material command drag test could not redo grouped drag edit");
    edited = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(edited.has_value(), "Material command drag redo wrote an unreadable material file");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(edited->desc.roughnessFactor, 0.25F), "Material command drag redo did not restore grouped roughness edit");

    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
}

void RunMaterialEditorWorkingCopySaveRevertUndoRedoTest() {
    CleanTempRoot();

    kb::scene::Scene scene;
    kb::editor::EditorCommandStack commandStack;
    kb::editor::MaterialEditorState materialEditor;
    kb::editor::tests::Require(scene.Assets().MountProject(TempRoot() / "Project"), "KBMAT-UE-0005: Working-copy test could not mount project assets");
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    kb::editor::tests::Require(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()), "KBMAT-UE-0005: Working-copy test could not register material loader");
    const std::filesystem::path materialPath = TempRoot() / "Project" / "Assets" / "Materials" / "WorkingCopy.kbmat";
    kb::render::RenderMaterialAssetData material{};
    material.desc.roughnessFactor = 0.2F;
    {
        std::ofstream output{ materialPath, std::ios::binary | std::ios::trunc };
        kb::render::RenderMaterialAssetWriter::Write(output, material);
    }

    kb::editor::tests::Require(scene.Assets().Discover() >= 1U, "KBMAT-UE-0005: Material working-copy test could not discover material asset");
    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath("/Game/Materials/WorkingCopy.kbmat");
    kb::editor::tests::Require(metadata != nullptr && metadata->type == "RenderMaterial", "KBMAT-UE-0005: Material working-copy test discovered wrong metadata");
    const kb::assets::AssetId materialId = metadata->id;

    materialEditor.Open(materialId, kb::editor::EditorMaterialAssetGateway::Read(scene, materialId));
    kb::editor::tests::Require(materialEditor.WorkingCopy().has_value() && !materialEditor.Dirty(), "KBMAT-UE-0005: Material Editor should open a clean working copy");

    kb::render::RenderMaterialAssetData patched = *materialEditor.WorkingCopy();
    kb::editor::EditorMaterialRoughnessFactorEdit roughnessPatch{ 0.35F };
    roughnessPatch.Apply(patched);
    materialEditor.SetWorkingCopy(patched);
    kb::editor::tests::Require(materialEditor.Dirty(), "KBMAT-UE-0005: Working-copy patch should mark the material dirty");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(materialEditor.WorkingCopy()->desc.roughnessFactor, 0.35F), "KBMAT-UE-0005: Working copy did not receive the roughness patch");
    std::optional<kb::render::RenderMaterialAssetData> onDisk = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(onDisk.has_value() && kb::editor::tests::NearlyEqual(onDisk->desc.roughnessFactor, 0.2F), "KBMAT-UE-0005: Working-copy patch wrote to disk before Save");

    materialEditor.RevertToCleanSnapshot();
    kb::editor::tests::Require(!materialEditor.Dirty() && kb::editor::tests::NearlyEqual(materialEditor.WorkingCopy()->desc.roughnessFactor, 0.2F), "KBMAT-UE-0005: Revert did not restore the clean snapshot");

    patched = *materialEditor.WorkingCopy();
    kb::editor::EditorMaterialRoughnessFactorEdit savePatch{ 0.5F };
    savePatch.Apply(patched);
    materialEditor.SetWorkingCopy(patched);
    kb::editor::tests::Require(commandStack.Execute(kb::editor::EditorMaterialAssetEditCommand::CreateRecorded(scene, materialId, "Save Material", *materialEditor.CleanSnapshot(), *materialEditor.WorkingCopy())),
        "KBMAT-UE-0005: Save command did not copy the working material to the source asset");
    kb::editor::tests::Require(commandStack.LastCompletedCommandAffectsOpenMaterialSource(), "KBMAT-GRAPH-0108: Source material save command should request Material Editor source reload on undo/redo");
    materialEditor.MarkSaved();
    kb::editor::tests::Require(!materialEditor.Dirty(), "KBMAT-UE-0005: Save did not clear material dirty state");
    onDisk = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(onDisk.has_value() && kb::editor::tests::NearlyEqual(onDisk->desc.roughnessFactor, 0.5F), "KBMAT-UE-0005: Save did not persist the working roughness");

    kb::editor::tests::Require(commandStack.Undo(), "KBMAT-UE-0005: Save command could not be undone");
    onDisk = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(onDisk.has_value() && kb::editor::tests::NearlyEqual(onDisk->desc.roughnessFactor, 0.2F), "KBMAT-UE-0005: Undo did not restore source material");
    materialEditor.SetWorkingCopy(*onDisk);
    materialEditor.MarkSaved();
    kb::editor::tests::Require(materialEditor.WorkingCopy().has_value() && kb::editor::tests::NearlyEqual(materialEditor.WorkingCopy()->desc.roughnessFactor, 0.2F) && !materialEditor.Dirty(),
        "KBMAT-UE-0005: Undo did not refresh the Material Editor working copy");

    kb::editor::tests::Require(commandStack.Redo(), "KBMAT-UE-0005: Save command could not be redone");
    onDisk = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(onDisk.has_value() && kb::editor::tests::NearlyEqual(onDisk->desc.roughnessFactor, 0.5F), "KBMAT-UE-0005: Redo did not restore saved source material");
    materialEditor.SetWorkingCopy(*onDisk);
    materialEditor.MarkSaved();
    kb::editor::tests::Require(materialEditor.WorkingCopy().has_value() && kb::editor::tests::NearlyEqual(materialEditor.WorkingCopy()->desc.roughnessFactor, 0.5F) && !materialEditor.Dirty(),
        "KBMAT-UE-0005: Redo did not refresh the Material Editor working copy");

    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
}

void RunMaterialEditorGraphWorkingCopyRuntimeTest() {
    CleanTempRoot();

    kb::scene::Scene scene;
    kb::editor::EditorCommandStack commandStack;
    kb::editor::MaterialEditorState materialEditor;
    kb::editor::tests::Require(scene.Assets().MountProject(TempRoot() / "Project"), "KBMAT-GRAPH-0101: Graph working-copy test could not mount project assets");
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    kb::editor::tests::Require(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()), "KBMAT-GRAPH-0101: Graph working-copy test could not register material loader");
    const std::filesystem::path materialPath = TempRoot() / "Project" / "Assets" / "Materials" / "GraphEdit.kbmat";
    kb::render::RenderMaterialAssetData material{};
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    {
        std::ofstream output{ materialPath, std::ios::binary | std::ios::trunc };
        kb::render::RenderMaterialAssetWriter::Write(output, material);
    }

    kb::editor::tests::Require(scene.Assets().Discover() >= 1U, "KBMAT-GRAPH-0101: Material graph working-copy test could not discover material asset");
    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath("/Game/Materials/GraphEdit.kbmat");
    kb::editor::tests::Require(metadata != nullptr && metadata->type == "RenderMaterial", "KBMAT-GRAPH-0101: Graph working-copy test discovered wrong metadata");
    const kb::assets::AssetId materialId = metadata->id;

    materialEditor.Open(materialId, kb::editor::EditorMaterialAssetGateway::Read(scene, materialId));
    kb::editor::tests::Require(materialEditor.WorkingCopy().has_value() && !materialEditor.Dirty(), "KBMAT-GRAPH-0101: Material Editor should open graph document as clean");

    std::uint32_t colorNodeId = 0U;
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantColor, -240, 64, &colorNodeId), "KBMAT-GRAPH-0101: Graph editor should create a color node in the working copy");
    kb::editor::tests::Require(colorNodeId != 0U && materialEditor.SelectedNodeId() == colorNodeId, "KBMAT-GRAPH-0101: Created graph node should be selected");
    kb::editor::tests::Require(materialEditor.MoveGraphNode(colorNodeId, -160, 96), "KBMAT-GRAPH-0101: Graph editor should persist node drag into working-copy position");
    kb::editor::tests::Require(materialEditor.ConnectGraphPins(colorNodeId, "rgba", 1U, "baseColor"), "KBMAT-GRAPH-0101: Graph editor should connect compatible output/input pins");
    kb::editor::tests::Require(materialEditor.WorkingCopy()->graph.links.size() == 1U, "KBMAT-GRAPH-0101: Graph editor should create one material graph link");
    kb::editor::tests::Require(materialEditor.Dirty(), "KBMAT-GRAPH-0101: Graph create/move/connect should mark working copy dirty");
    std::optional<kb::render::RenderMaterialAssetData> onDisk = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(onDisk.has_value() && onDisk->graph.nodes.size() == 1U, "KBMAT-GRAPH-0101: Graph working-copy edit wrote to disk before Save");

    kb::editor::tests::Require(materialEditor.DisconnectGraphInputPin(1U, "baseColor"), "KBMAT-GRAPH-0101: Graph editor should disconnect an input pin");
    kb::editor::tests::Require(materialEditor.WorkingCopy()->graph.links.empty(), "KBMAT-GRAPH-0101: Disconnect input pin should remove the material graph link");
    kb::editor::tests::Require(materialEditor.ConnectGraphPins(colorNodeId, "rgba", 1U, "baseColor"), "KBMAT-GRAPH-0101: Graph editor should reconnect the input pin after disconnect");
    materialEditor.RevertToCleanSnapshot();
    kb::editor::tests::Require(!materialEditor.Dirty() && materialEditor.WorkingCopy()->graph.nodes.size() == 1U && materialEditor.WorkingCopy()->graph.links.empty(),
        "KBMAT-GRAPH-0101: Revert should restore the clean graph snapshot");

    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantColor, -240, 64, &colorNodeId), "KBMAT-GRAPH-0101: Graph editor should recreate a color node before Save");
    kb::editor::tests::Require(materialEditor.MoveGraphNode(colorNodeId, -160, 96), "KBMAT-GRAPH-0101: Graph editor should move recreated node before Save");
    kb::editor::tests::Require(!materialEditor.SetGraphConstantValue(colorNodeId, "broken"), "KBMAT-GRAPH-0101: Constant Color should reject invalid authoring text");
    kb::editor::tests::Require(materialEditor.SetGraphConstantValue(colorNodeId, "0.25, 0.5, 0.75, 1"), "KBMAT-GRAPH-0101: Constant Color should accept comma or space separated authoring text");
    kb::editor::tests::Require(materialEditor.ConnectGraphPins(colorNodeId, "rgba", 1U, "baseColor"), "KBMAT-GRAPH-0101: Graph editor should reconnect before Save");
    kb::editor::tests::Require(commandStack.Execute(kb::editor::EditorMaterialAssetEditCommand::CreateRecorded(scene, materialId, "Save Material Graph", *materialEditor.CleanSnapshot(), *materialEditor.WorkingCopy())),
        "KBMAT-GRAPH-0101: Graph working-copy Save should execute through material command stack");
    materialEditor.MarkSaved();
    onDisk = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(onDisk.has_value() && onDisk->graph.nodes.size() == 2U && onDisk->graph.links.size() == 1U, "KBMAT-GRAPH-0101: Saved graph should persist node and link");
    const kb::render::RenderMaterialGraphNode* savedNode = kb::render::FindRenderMaterialGraphNode(onDisk->graph, colorNodeId);
    kb::editor::tests::Require(savedNode != nullptr && savedNode->positionX == -160 && savedNode->positionY == 96, "KBMAT-GRAPH-0101: Saved graph should persist dragged node position");
    kb::editor::tests::Require(savedNode != nullptr && savedNode->parameter.defaultValueHint == "0.25 0.5 0.75 1",
        "KBMAT-GRAPH-0101: Saved graph should persist authored Constant Color value");
    const kb::render::ResolvedRuntimeMaterialDesc resolvedConstant =
        kb::render::RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, *metadata, *onDisk);
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(resolvedConstant.desc.baseColor[0], 0.25F) &&
            kb::editor::tests::NearlyEqual(resolvedConstant.desc.baseColor[1], 0.5F) &&
            kb::editor::tests::NearlyEqual(resolvedConstant.desc.baseColor[2], 0.75F) &&
            kb::editor::tests::NearlyEqual(resolvedConstant.desc.baseColor[3], 1.0F),
        "KBMAT-GRAPH-0101: Authored Constant Color should drive Material Output Base Color at runtime");

    kb::editor::tests::Require(materialEditor.DeleteGraphNode(colorNodeId), "KBMAT-GRAPH-0101: Graph editor should delete non-output nodes");
    kb::editor::tests::Require(materialEditor.WorkingCopy()->graph.nodes.size() == 1U && materialEditor.WorkingCopy()->graph.links.empty(), "KBMAT-GRAPH-0101: Deleting a graph node should remove dependent links");
    kb::editor::tests::Require(commandStack.Execute(kb::editor::EditorMaterialAssetEditCommand::CreateRecorded(scene, materialId, "Delete Material Graph Node", *materialEditor.CleanSnapshot(), *materialEditor.WorkingCopy())),
        "KBMAT-GRAPH-0101: Graph delete should save through material command stack");
    materialEditor.MarkSaved();
    onDisk = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(onDisk.has_value() && onDisk->graph.nodes.size() == 1U && onDisk->graph.links.empty(), "KBMAT-GRAPH-0101: Saved graph delete did not persist");

    kb::editor::tests::Require(commandStack.Undo(), "KBMAT-GRAPH-0101: Graph delete command could not be undone");
    onDisk = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(onDisk.has_value() && onDisk->graph.nodes.size() == 2U && onDisk->graph.links.size() == 1U, "KBMAT-GRAPH-0101: Undo graph delete should restore node and link");
    kb::editor::tests::Require(commandStack.Undo(), "KBMAT-GRAPH-0101: Graph save command could not be undone");
    onDisk = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(onDisk.has_value() && onDisk->graph.nodes.size() == 1U && onDisk->graph.links.empty(), "KBMAT-GRAPH-0101: Undo graph save should restore original graph");
    kb::editor::tests::Require(commandStack.Redo() && commandStack.Redo(), "KBMAT-GRAPH-0101: Graph commands could not be redone");
    onDisk = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(onDisk.has_value() && onDisk->graph.nodes.size() == 1U && onDisk->graph.links.empty(), "KBMAT-GRAPH-0101: Redo graph delete should restore deleted state");

    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
}

void RunMaterialEditorVariantSwitchAuthoringTest() {
    kb::editor::MaterialEditorState materialEditor;
    kb::render::RenderMaterialAssetData material{};
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    materialEditor.Open(kb::assets::AssetId{ 0x0520U }, material);

    const std::vector<kb::editor::MaterialEditorGraphMenuCommand> switchCommands =
        kb::editor::MaterialEditorGraphContextMenuCommands(8U);
    const auto hasCommand = [&switchCommands](kb::editor::MaterialEditorGraphMenuCommand command) {
        return std::find(switchCommands.begin(), switchCommands.end(), command) != switchCommands.end();
    };
    kb::editor::tests::Require(hasCommand(kb::editor::MaterialEditorGraphMenuCommand::CreateQualitySwitch) &&
            hasCommand(kb::editor::MaterialEditorGraphMenuCommand::CreateFeatureLevelSwitch) &&
            hasCommand(kb::editor::MaterialEditorGraphMenuCommand::CreateShadingPathSwitch) &&
            hasCommand(kb::editor::MaterialEditorGraphMenuCommand::CreateShaderStageSwitch),
        "KBMAT-MAT52: Material Editor Switches menu must expose MAT-52 variant switch nodes");
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphContextMenuCommandName(kb::editor::MaterialEditorGraphMenuCommand::CreateQualitySwitch) == "Quality Switch",
        "KBMAT-MAT52: QualitySwitch menu command must have a production display name");

    std::uint32_t redNodeId = 0U;
    std::uint32_t blueNodeId = 0U;
    std::uint32_t switchNodeId = 0U;
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantColor, -420, 32, &redNodeId),
        "KBMAT-MAT52: Material Editor should create the low quality color branch");
    kb::editor::tests::Require(materialEditor.SetGraphConstantValue(redNodeId, "1 0 0 1"),
        "KBMAT-MAT52: Material Editor should edit the low quality branch color");
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantColor, -420, 144, &blueNodeId),
        "KBMAT-MAT52: Material Editor should create the high quality color branch");
    kb::editor::tests::Require(materialEditor.SetGraphConstantValue(blueNodeId, "0 0 1 1"),
        "KBMAT-MAT52: Material Editor should edit the high quality branch color");
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::QualitySwitch, -180, 80, &switchNodeId),
        "KBMAT-MAT52: Material Editor should create a QualitySwitch node");
    const kb::render::RenderMaterialGraphNode* switchNode = kb::render::FindRenderMaterialGraphNode(materialEditor.WorkingCopy()->graph, switchNodeId);
    kb::editor::tests::Require(switchNode != nullptr && switchNode->parameter.displayName == "Quality Switch",
        "KBMAT-MAT52: QualitySwitch should get editor metadata when authored from the graph");
    kb::editor::tests::Require(materialEditor.ConnectGraphPins(redNodeId, "rgba", switchNodeId, "low"),
        "KBMAT-MAT52: Material Editor should connect the low branch into QualitySwitch.low");
    kb::editor::tests::Require(materialEditor.ConnectGraphPins(blueNodeId, "rgba", switchNodeId, "high"),
        "KBMAT-MAT52: Material Editor should connect the high branch into QualitySwitch.high");
    kb::editor::tests::Require(materialEditor.ConnectGraphPins(switchNodeId, "result", 1U, "baseColor"),
        "KBMAT-MAT52: Material Editor should connect QualitySwitch.result into MaterialOutput.baseColor");

    const kb::render::RenderMaterialGraphCompileResult lowResult = kb::render::CompileRenderMaterialGraphToShaderSource(
        materialEditor.WorkingCopy()->graph,
        kb::render::RenderMaterialGraphBuildContext{ .assetId = 0x0520U, .qualityLevel = kb::render::RenderMaterialGraphQualityLevel::Low });
    const kb::render::RenderMaterialGraphCompileResult highResult = kb::render::CompileRenderMaterialGraphToShaderSource(
        materialEditor.WorkingCopy()->graph,
        kb::render::RenderMaterialGraphBuildContext{ .assetId = 0x0521U, .qualityLevel = kb::render::RenderMaterialGraphQualityLevel::High });
    kb::editor::tests::Require(lowResult.Succeeded() && highResult.Succeeded() && lowResult.shader.sourceHash != highResult.shader.sourceHash,
        "KBMAT-MAT52: QualitySwitch authored in the Material Editor must compile to distinct quality variants");
}

void RunMaterialEditorGraphWorkingCopyCommandUndoRedoTest() {
    kb::editor::EditorCommandStack commandStack;
    kb::editor::MaterialEditorState materialEditor;
    const kb::assets::AssetId materialId{ 0x0108U };

    kb::render::RenderMaterialAssetData before{};
    before.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    kb::render::RenderMaterialAssetData after = before;
    after.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = -96,
        .positionY = 104,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .defaultValueHint = "0.125 0.25 0.5 1",
        },
    });
    after.graph.links.push_back(kb::render::RenderMaterialGraphLink{
        .id = 0xABCDEFU,
        .fromNodeId = 2U,
        .fromPinId = kb::render::RenderMaterialGraphStablePinId(kb::render::RenderMaterialGraphNodeKind::ConstantColor, "rgba", true),
        .fromPin = "rgba",
        .toNodeId = 1U,
        .toPinId = kb::render::RenderMaterialGraphStablePinId(kb::render::RenderMaterialGraphNodeKind::MaterialOutput, "baseColor", false),
        .toPin = "baseColor",
    });
    after.graphParameterValues.push_back(kb::render::RenderMaterialGraphParameterValue{
        .stableId = "tintOverride",
        .type = kb::render::RenderMaterialParameterType::Color,
        .numbers = { 0.125F, 0.25F, 0.5F, 1.0F },
    });

    materialEditor.Open(materialId, before);
    kb::editor::tests::Require(!materialEditor.Dirty(), "KBMAT-GRAPH-0108: Graph command test should start from a clean working copy");
    kb::editor::tests::Require(commandStack.Execute(kb::editor::EditorMaterialWorkingCopyEditCommand::Create(
                                  materialEditor,
                                  materialId,
                                  "Create And Wire Material Graph Node",
                                  before,
                                  after,
                                  0U,
                                  2U)),
        "KBMAT-GRAPH-0108: Graph working-copy command should execute through command stack");
    kb::editor::tests::Require(commandStack.UndoCount() == 1U && commandStack.RedoCount() == 0U,
        "KBMAT-MAT53: A grouped graph transaction must occupy exactly one undo slot");
    kb::editor::tests::Require(!commandStack.LastCompletedCommandAffectsOpenMaterialSource(), "KBMAT-GRAPH-0108: Graph working-copy command must not reload source material on undo/redo");
    kb::editor::tests::Require(materialEditor.Dirty(), "KBMAT-GRAPH-0108: Graph working-copy command should mark Material Editor dirty");
    kb::editor::tests::Require(materialEditor.SelectedNodeId() == 2U, "KBMAT-GRAPH-0108: Graph working-copy command should restore after-selection");
    kb::editor::tests::Require(materialEditor.WorkingCopy().has_value() && materialEditor.WorkingCopy()->graph.nodes.size() == 2U && materialEditor.WorkingCopy()->graph.links.size() == 1U,
        "KBMAT-GRAPH-0108: Graph working-copy command should apply node/link runtime state");
    const kb::render::RenderMaterialGraphNode* movedNode = kb::render::FindRenderMaterialGraphNode(materialEditor.WorkingCopy()->graph, 2U);
    kb::editor::tests::Require(movedNode != nullptr && movedNode->positionX == -96 && movedNode->positionY == 104 &&
            movedNode->parameter.defaultValueHint == "0.125 0.25 0.5 1",
        "KBMAT-MAT53: A grouped graph transaction must preserve the final node position and constant value");
    const kb::render::RenderMaterialGraphParameterValue* authoredParameter = FindGraphParameterValue(*materialEditor.WorkingCopy(), "tintOverride");
    kb::editor::tests::Require(authoredParameter != nullptr && authoredParameter->numbers[0] == 0.125F && authoredParameter->numbers[1] == 0.25F &&
            authoredParameter->numbers[2] == 0.5F && authoredParameter->numbers[3] == 1.0F,
        "KBMAT-MAT53: A grouped graph transaction must preserve edited graph parameter values");

    kb::editor::tests::Require(commandStack.Undo(), "KBMAT-GRAPH-0108: Graph working-copy command should undo");
    kb::editor::tests::Require(commandStack.UndoCount() == 0U && commandStack.RedoCount() == 1U,
        "KBMAT-MAT53: Undoing a grouped graph transaction must move the single command to redo");
    kb::editor::tests::Require(!commandStack.LastCompletedCommandAffectsOpenMaterialSource(), "KBMAT-GRAPH-0108: Undo graph working-copy command must not request source reload");
    kb::editor::tests::Require(!materialEditor.Dirty(), "KBMAT-GRAPH-0108: Undo graph working-copy command should restore clean snapshot");
    kb::editor::tests::Require(materialEditor.SelectedNodeId() == 0U, "KBMAT-GRAPH-0108: Undo graph working-copy command should restore before-selection");
    kb::editor::tests::Require(materialEditor.WorkingCopy().has_value() && materialEditor.WorkingCopy()->graph.nodes.size() == 1U && materialEditor.WorkingCopy()->graph.links.empty(),
        "KBMAT-GRAPH-0108: Undo graph working-copy command should restore before graph");
    kb::editor::tests::Require(materialEditor.WorkingCopy()->graphParameterValues.empty(),
        "KBMAT-MAT53: Undoing a grouped graph transaction must restore graph parameter values");

    kb::editor::tests::Require(commandStack.Redo(), "KBMAT-GRAPH-0108: Graph working-copy command should redo");
    kb::editor::tests::Require(commandStack.UndoCount() == 1U && commandStack.RedoCount() == 0U,
        "KBMAT-MAT53: Redoing a grouped graph transaction must restore the single undo entry");
    kb::editor::tests::Require(materialEditor.Dirty(), "KBMAT-GRAPH-0108: Redo graph working-copy command should restore dirty state");
    kb::editor::tests::Require(materialEditor.SelectedNodeId() == 2U, "KBMAT-GRAPH-0108: Redo graph working-copy command should restore after-selection");
    kb::editor::tests::Require(materialEditor.WorkingCopy().has_value() && materialEditor.WorkingCopy()->graph.nodes.size() == 2U && materialEditor.WorkingCopy()->graph.links.size() == 1U,
        "KBMAT-GRAPH-0108: Redo graph working-copy command should restore after graph");
    authoredParameter = FindGraphParameterValue(*materialEditor.WorkingCopy(), "tintOverride");
    kb::editor::tests::Require(authoredParameter != nullptr && authoredParameter->numbers[0] == 0.125F && authoredParameter->numbers[1] == 0.25F &&
            authoredParameter->numbers[2] == 0.5F && authoredParameter->numbers[3] == 1.0F,
        "KBMAT-MAT53: Redoing a grouped graph transaction must restore graph parameter values");
}

void RunMaterialEditorGraphMultiSelectCopyPasteDuplicateTest() {
    kb::editor::MaterialEditorState materialEditor;
    kb::render::RenderMaterialAssetData source{};
    source.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    materialEditor.Open(kb::assets::AssetId{ 0x3600U }, source);

    std::uint32_t textureNodeId = 0U;
    std::uint32_t sampleNodeId = 0U;
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ParameterTexture, -420, 40, &textureNodeId),
        "KBMAT-MAT54: Source graph should create a texture parameter node");
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::TextureSample, -120, 24, &sampleNodeId),
        "KBMAT-MAT54: Source graph should create a texture sample node");
    kb::editor::tests::Require(materialEditor.ConnectGraphPins(textureNodeId, "texture", sampleNodeId, "texture"),
        "KBMAT-MAT54: Source graph should connect texture parameter to texture sample");

    kb::render::RenderMaterialAssetData withTextureValue = *materialEditor.WorkingCopy();
    const kb::render::RenderMaterialGraphNode* textureNode = kb::render::FindRenderMaterialGraphNode(withTextureValue.graph, textureNodeId);
    kb::editor::tests::Require(textureNode != nullptr && !textureNode->parameter.stableId.empty(),
        "KBMAT-MAT54: Texture parameter node should have a stable id for clipboard references");
    const std::string textureStableId = textureNode->parameter.stableId;
    withTextureValue.graphParameterValues.push_back(kb::render::RenderMaterialGraphParameterValue{
        .stableId = textureStableId,
        .type = kb::render::RenderMaterialParameterType::Texture,
        .assetId = 0xA770U,
    });
    materialEditor.SetWorkingCopy(std::move(withTextureValue));
    kb::editor::tests::Require(materialEditor.SetNodeSelection({ textureNodeId, sampleNodeId }, sampleNodeId) && materialEditor.SelectedNodeCount() == 2U,
        "KBMAT-MAT54: Material Editor should support deterministic multi-select");
    kb::editor::tests::Require(materialEditor.CopySelectedGraphNodes(),
        "KBMAT-MAT54: Copy should accept a multi-selected material graph subgraph");

    std::vector<std::uint32_t> pastedIds;
    kb::editor::tests::Require(materialEditor.PasteGraphClipboard(48, 32, &pastedIds),
        "KBMAT-MAT54: Paste should create a remapped subgraph in the same material");
    kb::editor::tests::Require(pastedIds.size() == 2U && std::ranges::find(pastedIds, textureNodeId) == pastedIds.end() && std::ranges::find(pastedIds, sampleNodeId) == pastedIds.end(),
        "KBMAT-MAT54: Pasted subgraph in the same material must allocate new node ids");
    const kb::render::RenderMaterialGraphNode* pastedTexture = nullptr;
    const kb::render::RenderMaterialGraphNode* pastedSample = nullptr;
    for (std::uint32_t nodeId : pastedIds) {
        const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(materialEditor.WorkingCopy()->graph, nodeId);
        if (node != nullptr && node->kind == kb::render::RenderMaterialGraphNodeKind::ParameterTexture) {
            pastedTexture = node;
        } else if (node != nullptr && node->kind == kb::render::RenderMaterialGraphNodeKind::TextureSample) {
            pastedSample = node;
        }
    }
    kb::editor::tests::Require(pastedTexture != nullptr && pastedSample != nullptr &&
            pastedTexture->positionX == -372 && pastedTexture->positionY == 72 &&
            pastedSample->positionX == -72 && pastedSample->positionY == 56,
        "KBMAT-MAT54: Paste should preserve node kinds and apply the requested offset");
    const bool pastedLinkRemapped = std::ranges::any_of(materialEditor.WorkingCopy()->graph.links, [pastedTexture, pastedSample](const kb::render::RenderMaterialGraphLink& link) {
        return link.fromNodeId == pastedTexture->id && link.fromPin == "texture" && link.toNodeId == pastedSample->id && link.toPin == "texture";
    });
    kb::editor::tests::Require(pastedLinkRemapped,
        "KBMAT-MAT54: Pasted subgraph must remap internal links to the new node ids");

    std::vector<std::uint32_t> duplicateIds;
    kb::editor::tests::Require(materialEditor.DuplicateSelectedGraphNodes(24, 24, &duplicateIds) && duplicateIds.size() == 2U && materialEditor.SelectedNodeCount() == 2U,
        "KBMAT-MAT54: Duplicate should copy the selected subgraph and select the duplicate nodes");

    kb::render::RenderMaterialAssetData target{};
    target.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    materialEditor.Open(kb::assets::AssetId{ 0x3601U }, target);
    std::vector<std::uint32_t> crossMaterialIds;
    kb::editor::tests::Require(materialEditor.PasteGraphClipboard(12, 8, &crossMaterialIds),
        "KBMAT-MAT54: Clipboard should paste into another open material");
    const kb::render::RenderMaterialGraphParameterValue* pastedTextureValue = FindGraphParameterValue(*materialEditor.WorkingCopy(), textureStableId);
    kb::editor::tests::Require(pastedTextureValue != nullptr && pastedTextureValue->type == kb::render::RenderMaterialParameterType::Texture && pastedTextureValue->assetId == 0xA770U,
        "KBMAT-MAT54: Cross-material paste must preserve graph parameter texture references");
    kb::editor::tests::Require(std::ranges::any_of(materialEditor.WorkingCopy()->graph.links, [&crossMaterialIds](const kb::render::RenderMaterialGraphLink& link) {
            return std::ranges::find(crossMaterialIds, link.fromNodeId) != crossMaterialIds.end() &&
                std::ranges::find(crossMaterialIds, link.toNodeId) != crossMaterialIds.end();
        }),
        "KBMAT-MAT54: Cross-material paste must preserve subgraph topology with remapped links");
}

void RunMaterialEditorGraphSelectionLayoutCommandsTest() {
    kb::editor::MaterialEditorState materialEditor;
    kb::render::RenderMaterialAssetData source{};
    source.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    materialEditor.Open(kb::assets::AssetId{ 0x3602U }, source);

    std::uint32_t scalarAId = 0U;
    std::uint32_t scalarBId = 0U;
    std::uint32_t addId = 0U;
    std::uint32_t multiplyId = 0U;
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantScalar, -600, 40, &scalarAId),
        "KBMAT-MAT54B: Selection layout test should create the first upstream node");
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantScalar, -240, 300, &scalarBId),
        "KBMAT-MAT54B: Selection layout test should create the second upstream node");
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::Add, 160, 160, &addId),
        "KBMAT-MAT54B: Selection layout test should create the join node");
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::Multiply, 620, -80, &multiplyId),
        "KBMAT-MAT54B: Selection layout test should create the downstream node");
    kb::editor::tests::Require(materialEditor.ConnectGraphPins(scalarAId, "value", addId, "a") &&
            materialEditor.ConnectGraphPins(scalarBId, "value", addId, "b") &&
            materialEditor.ConnectGraphPins(addId, "value", multiplyId, "a") &&
            materialEditor.ConnectGraphPins(scalarBId, "value", multiplyId, "b") &&
            materialEditor.ConnectGraphPins(multiplyId, "value", 1U, "baseColor"),
        "KBMAT-MAT54B: Selection layout test should wire a real editable graph");

    kb::editor::tests::Require(materialEditor.SetNodeSelection({ addId }, addId),
        "KBMAT-MAT54B: The join node should be selectable before upstream expansion");
    kb::editor::tests::Require(materialEditor.SelectGraphUpstream(),
        "KBMAT-MAT54B: Select Upstream should expand selection through incoming links");
    kb::editor::tests::Require(materialEditor.SelectedNodeCount() == 3U &&
            materialEditor.IsNodeSelected(scalarAId) &&
            materialEditor.IsNodeSelected(scalarBId) &&
            materialEditor.IsNodeSelected(addId) &&
            !materialEditor.IsNodeSelected(multiplyId) &&
            materialEditor.SelectedNodeId() == addId,
        "KBMAT-MAT54B: Select Upstream should include all upstream nodes and preserve the primary node");

    kb::editor::tests::Require(materialEditor.SetNodeSelection({ addId }, addId),
        "KBMAT-MAT54B: The join node should be selectable before downstream expansion");
    kb::editor::tests::Require(materialEditor.SelectGraphDownstream(),
        "KBMAT-MAT54B: Select Downstream should expand selection through outgoing links");
    kb::editor::tests::Require(materialEditor.SelectedNodeCount() == 3U &&
            materialEditor.IsNodeSelected(addId) &&
            materialEditor.IsNodeSelected(multiplyId) &&
            materialEditor.IsNodeSelected(1U) &&
            !materialEditor.IsNodeSelected(scalarAId) &&
            materialEditor.SelectedNodeId() == addId,
        "KBMAT-MAT54B: Select Downstream should include downstream nodes through Material Output and preserve the primary node");

    kb::editor::tests::Require(materialEditor.SetNodeSelection({ scalarAId, scalarBId, addId }, addId),
        "KBMAT-MAT54B: Three graph nodes should be multi-selectable before layout commands");
    kb::editor::tests::Require(materialEditor.AlignSelectedGraphNodes(kb::editor::MaterialEditorGraphAlignMode::Left),
        "KBMAT-MAT54B: Align Left should move selected graph nodes as an undoable state mutation");
    const std::optional<std::pair<std::int32_t, std::int32_t>> alignedA = materialEditor.GraphNodePosition(scalarAId);
    const std::optional<std::pair<std::int32_t, std::int32_t>> alignedB = materialEditor.GraphNodePosition(scalarBId);
    const std::optional<std::pair<std::int32_t, std::int32_t>> alignedAdd = materialEditor.GraphNodePosition(addId);
    kb::editor::tests::Require(alignedA.has_value() && alignedB.has_value() && alignedAdd.has_value() &&
            alignedA->first == -600 && alignedB->first == -600 && alignedAdd->first == -600,
        "KBMAT-MAT54B: Align Left should place every selected node on the leftmost graph column");

    kb::editor::tests::Require(materialEditor.MoveGraphNodes({
            { scalarAId, { 0, 20 } },
            { addId, { 200, 100 } },
            { scalarBId, { 500, 300 } },
        }),
        "KBMAT-MAT54B: Layout test should reset positions before distribution");
    kb::editor::tests::Require(materialEditor.SetNodeSelection({ scalarAId, addId, scalarBId }, addId),
        "KBMAT-MAT54B: Distribution should preserve a deterministic primary node");
    kb::editor::tests::Require(materialEditor.DistributeSelectedGraphNodes(kb::editor::MaterialEditorGraphDistributeAxis::Horizontal),
        "KBMAT-MAT54B: Distribute Horizontal should space selected nodes between the left and right endpoints");
    const std::optional<std::pair<std::int32_t, std::int32_t>> distributedAdd = materialEditor.GraphNodePosition(addId);
    kb::editor::tests::Require(distributedAdd.has_value() && distributedAdd->first == 250 && distributedAdd->second == 100,
        "KBMAT-MAT54B: Distribute Horizontal should move only the graph X coordinate of interior nodes");

    kb::editor::tests::Require(materialEditor.MoveGraphNodes({
            { scalarAId, { 0, 0 } },
            { addId, { 250, 240 } },
            { scalarBId, { 500, 600 } },
        }),
        "KBMAT-MAT54B: Layout test should reset vertical positions before distribution");
    kb::editor::tests::Require(materialEditor.DistributeSelectedGraphNodes(kb::editor::MaterialEditorGraphDistributeAxis::Vertical),
        "KBMAT-MAT54B: Distribute Vertical should space selected nodes between the top and bottom endpoints");
    const std::optional<std::pair<std::int32_t, std::int32_t>> verticalAdd = materialEditor.GraphNodePosition(addId);
    kb::editor::tests::Require(verticalAdd.has_value() && verticalAdd->first == 250 && verticalAdd->second == 300,
        "KBMAT-MAT54B: Distribute Vertical should move only the graph Y coordinate of interior nodes");
    kb::editor::tests::Require(materialEditor.SelectedNodeId() == addId && materialEditor.SelectedNodeCount() == 3U,
        "KBMAT-MAT54B: Layout commands should keep the selected graph nodes active");
}

void RunMaterialEditorGraphPromoteToParameterTest() {
    kb::editor::MaterialEditorState materialEditor;
    kb::render::RenderMaterialAssetData source{};
    source.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    materialEditor.Open(kb::assets::AssetId{ 0x3603U }, source);

    std::uint32_t scalarId = 0U;
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantScalar, -360, 80, &scalarId),
        "KBMAT-MAT57B: Promote test should create a scalar constant");
    kb::editor::tests::Require(materialEditor.SetGraphConstantValue(scalarId, "0.42"),
        "KBMAT-MAT57B: Promote test should set the scalar constant value");
    kb::editor::tests::Require(materialEditor.ConnectGraphPins(scalarId, "value", 1U, "metallic"),
        "KBMAT-MAT57B: Promote test should connect the scalar before promotion");
    kb::editor::tests::Require(materialEditor.SetNodeSelection({ scalarId }, scalarId) && materialEditor.CanPromoteSelectedGraphNodeToParameter(),
        "KBMAT-MAT57B: A single selected scalar constant should be promotable");
    kb::editor::tests::Require(materialEditor.PromoteSelectedGraphNodeToParameter(),
        "KBMAT-MAT57B: Promote to Parameter should convert the selected scalar node");
    const kb::render::RenderMaterialGraphNode* promotedScalar =
        kb::render::FindRenderMaterialGraphNode(materialEditor.WorkingCopy()->graph, scalarId);
    kb::editor::tests::Require(promotedScalar != nullptr &&
            promotedScalar->kind == kb::render::RenderMaterialGraphNodeKind::ParameterScalar &&
            promotedScalar->parameter.stableId == "scalar" + std::to_string(scalarId) &&
            promotedScalar->parameter.defaultValueHint == "0.42" &&
            promotedScalar->parameter.overrideSupported,
        "KBMAT-MAT57B: Promoted scalar should become a real scalar parameter with the authored default");
    kb::editor::tests::Require(std::ranges::any_of(materialEditor.WorkingCopy()->graph.links, [scalarId](const kb::render::RenderMaterialGraphLink& link) {
            return link.fromNodeId == scalarId && link.fromPin == "value" && link.toNodeId == 1U && link.toPin == "metallic";
        }),
        "KBMAT-MAT57B: Promoting a scalar should preserve existing graph links");

    std::uint32_t vector2Id = 0U;
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantVector2, -360, 220, &vector2Id),
        "KBMAT-MAT57B: Promote test should create a vector2 constant");
    kb::editor::tests::Require(materialEditor.SetGraphConstantValue(vector2Id, "0.25 0.75"),
        "KBMAT-MAT57B: Promote test should set the vector2 constant value");
    kb::editor::tests::Require(materialEditor.ConnectGraphPins(vector2Id, "xy", 1U, "baseColor"),
        "KBMAT-MAT57B: Promote test should connect vector2 to a compatible material output");
    kb::editor::tests::Require(materialEditor.SetNodeSelection({ vector2Id }, vector2Id) && materialEditor.PromoteSelectedGraphNodeToParameter(),
        "KBMAT-MAT57B: Promote to Parameter should convert vector2 constants through the vector parameter node");
    const kb::render::RenderMaterialGraphNode* promotedVector =
        kb::render::FindRenderMaterialGraphNode(materialEditor.WorkingCopy()->graph, vector2Id);
    kb::editor::tests::Require(promotedVector != nullptr &&
            promotedVector->kind == kb::render::RenderMaterialGraphNodeKind::ParameterVector &&
            promotedVector->parameter.defaultValueHint == "0.25 0.75 0",
        "KBMAT-MAT57B: Promoted vector2 should become a vector parameter with a deterministic zero Z component");
    kb::editor::tests::Require(std::ranges::any_of(materialEditor.WorkingCopy()->graph.links, [vector2Id](const kb::render::RenderMaterialGraphLink& link) {
            return link.fromNodeId == vector2Id && link.fromPin == "xyz" && link.fromPinId != 0U && link.toNodeId == 1U && link.toPin == "baseColor";
        }),
        "KBMAT-MAT57B: Promoting vector2 should remap outgoing links from xy to the parameter xyz pin");

    const kb::render::RenderMaterialGraphCompileResult compile =
        kb::render::CompileRenderMaterialGraphToShaderSource(materialEditor.WorkingCopy()->graph, kb::render::RenderMaterialGraphBuildContext{ .assetId = 0x3603U });
    kb::editor::tests::Require(compile.Succeeded(),
        "KBMAT-MAT57B: A graph with promoted parameters should compile through the real material graph compiler");
}

void RunMaterialEditorGraphCommentBoxSerializationGroupMoveTest() {
    kb::editor::MaterialEditorState materialEditor;
    kb::render::RenderMaterialAssetData material{};
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    materialEditor.Open(kb::assets::AssetId{ 0x3700U }, material);

    std::uint32_t insideNodeId = 0U;
    std::uint32_t outsideNodeId = 0U;
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantScalar, 16, 24, &insideNodeId),
        "KBMAT-MAT55: Comment test should create a node inside the future comment box");
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantColor, 460, 420, &outsideNodeId),
        "KBMAT-MAT55: Comment test should create an outside node");

    std::uint32_t commentId = 0U;
    kb::editor::tests::Require(materialEditor.AddGraphComment("Surface note #1", 0, 0, 200, 120, 0x4A6385U, &commentId),
        "KBMAT-MAT55: Material Editor should create a comment box");
    kb::editor::tests::Require(materialEditor.SelectedCommentId() == commentId && materialEditor.SelectedNodeId() == 0U,
        "KBMAT-MAT55: Creating a comment box should select the comment and clear node selection");

    const std::vector<std::uint32_t> containedBefore = materialEditor.GraphNodeIdsInsideComment(commentId);
    kb::editor::tests::Require(std::ranges::find(containedBefore, insideNodeId) != containedBefore.end() &&
            std::ranges::find(containedBefore, outsideNodeId) == containedBefore.end(),
        "KBMAT-MAT55: Comment box containment should be deterministic before group move");

    kb::editor::tests::Require(materialEditor.MoveGraphCommentGroup(commentId, 32, 48),
        "KBMAT-MAT55: Moving a comment box should move the group");
    const std::optional<kb::render::RenderMaterialGraphCommentBox> movedComment = materialEditor.GraphComment(commentId);
    const std::optional<std::pair<std::int32_t, std::int32_t>> movedInside = materialEditor.GraphNodePosition(insideNodeId);
    const std::optional<std::pair<std::int32_t, std::int32_t>> movedOutside = materialEditor.GraphNodePosition(outsideNodeId);
    kb::editor::tests::Require(movedComment.has_value() && movedComment->positionX == 32 && movedComment->positionY == 48,
        "KBMAT-MAT55: Comment box position did not update");
    kb::editor::tests::Require(movedInside.has_value() && movedInside->first == 48 && movedInside->second == 72,
        "KBMAT-MAT55: Group-move should carry nodes whose top-left starts inside the comment");
    kb::editor::tests::Require(movedOutside.has_value() && movedOutside->first == 460 && movedOutside->second == 420,
        "KBMAT-MAT55: Group-move should not move nodes outside the comment");

    std::ostringstream serialized;
    kb::render::RenderMaterialAssetWriter::Write(serialized, *materialEditor.WorkingCopy());
    kb::editor::tests::Require(serialized.str().find("graphComment 1 32 48 200 120 4875141 Surface%20note%20%231\n") != std::string::npos,
        "KBMAT-MAT55: Comment box should serialize with encoded text");

    std::istringstream input{ serialized.str() };
    const kb::render::RenderMaterialAssetParseResult parsed = kb::render::RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    kb::editor::tests::Require(parsed.Succeeded() && parsed.asset.has_value() && parsed.asset->graph.comments.size() == 1U,
        "KBMAT-MAT55: Comment box material should parse after serialization");
    materialEditor.Open(kb::assets::AssetId{ 0x3701U }, parsed.asset);
    const std::optional<kb::render::RenderMaterialGraphCommentBox> reopenedComment = materialEditor.GraphComment(commentId);
    kb::editor::tests::Require(reopenedComment.has_value() &&
            reopenedComment->positionX == 32 &&
            reopenedComment->positionY == 48 &&
            reopenedComment->width == 200 &&
            reopenedComment->height == 120 &&
            reopenedComment->color == 0x4A6385U &&
            reopenedComment->text == "Surface note #1",
        "KBMAT-MAT55: Comment box should survive save/reopen deterministically");
}

void RunMaterialEditorGraphCompositeRerouteAuthoringTest() {
    kb::editor::MaterialEditorState materialEditor;
    kb::render::RenderMaterialAssetData material{};
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    materialEditor.Open(kb::assets::AssetId{ 0x3800U }, material);

    std::uint32_t colorNodeId = 0U;
    std::uint32_t compositeInputId = 0U;
    std::uint32_t compositeOutputId = 0U;
    std::uint32_t rerouteNodeId = 0U;
    std::uint32_t namedDeclarationId = 0U;
    std::uint32_t namedUsageId = 0U;
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantColor, -420, 64, &colorNodeId),
        "KBMAT-MAT56: Material Editor should create the source color node");
    kb::editor::tests::Require(materialEditor.SetGraphConstantValue(colorNodeId, "0.25 0.5 0.75 1"),
        "KBMAT-MAT56: Material Editor should author the source color value");
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::CompositeInput, -180, 64, &compositeInputId),
        "KBMAT-MAT56: Material Editor should create a Composite Input node");
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::CompositeOutput, 40, 64, &compositeOutputId),
        "KBMAT-MAT56: Material Editor should create a Composite Output node");
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::Reroute, 260, 64, &rerouteNodeId),
        "KBMAT-MAT56: Material Editor should create a Reroute node");
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::NamedRerouteDeclaration, 460, 64, &namedDeclarationId),
        "KBMAT-MAT56: Material Editor should create a Named Reroute declaration");
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::NamedRerouteUsage, 660, 64, &namedUsageId),
        "KBMAT-MAT56: Material Editor should create a Named Reroute usage");

    kb::editor::tests::Require(materialEditor.ConnectGraphPins(colorNodeId, "rgba", compositeInputId, "input"),
        "KBMAT-MAT56: Composite Input should accept the source color");
    kb::editor::tests::Require(materialEditor.ConnectGraphPins(compositeInputId, "output", compositeOutputId, "input"),
        "KBMAT-MAT56: Composite Input should tunnel into Composite Output");
    kb::editor::tests::Require(materialEditor.ConnectGraphPins(compositeOutputId, "output", rerouteNodeId, "input"),
        "KBMAT-MAT56: Composite Output should connect into a Reroute");
    kb::editor::tests::Require(materialEditor.ConnectGraphPins(rerouteNodeId, "output", namedDeclarationId, "input"),
        "KBMAT-MAT56: Reroute output should feed a Named Reroute declaration");
    kb::editor::tests::Require(materialEditor.ConnectGraphPins(namedUsageId, "output", 1U, "baseColor"),
        "KBMAT-MAT56: Named Reroute usage should feed Material Output without a physical wire to the declaration");

    kb::editor::tests::Require(materialEditor.SetNodeSelection({ compositeInputId, compositeOutputId, rerouteNodeId }, compositeOutputId),
        "KBMAT-MAT56: Material Editor should select nodes for composite creation");
    std::uint32_t compositeId = 0U;
    kb::editor::tests::Require(materialEditor.CreateGraphCompositeFromSelection("Nested Surface", -220, 20, 560, 180, &compositeId),
        "KBMAT-MAT56: Material Editor should collapse a selected region into composite metadata");
    std::optional<kb::render::RenderMaterialGraphCompositeSubgraph> composite = materialEditor.GraphCompositeSubgraph(compositeId);
    kb::editor::tests::Require(composite.has_value() && composite->name == "Nested Surface" && composite->nodeIds.size() == 3U &&
            std::ranges::find(composite->nodeIds, compositeInputId) != composite->nodeIds.end() &&
            std::ranges::find(composite->nodeIds, compositeOutputId) != composite->nodeIds.end() &&
            std::ranges::find(composite->nodeIds, rerouteNodeId) != composite->nodeIds.end(),
        "KBMAT-MAT56: Composite metadata should own the selected node ids");
    kb::editor::tests::Require(materialEditor.ToggleGraphCompositeCollapsed(compositeId),
        "KBMAT-MAT56: Material Editor should collapse composite subgraphs");
    composite = materialEditor.GraphCompositeSubgraph(compositeId);
    kb::editor::tests::Require(composite.has_value() && composite->collapsed,
        "KBMAT-MAT56: Composite collapsed state should be visible in editor state");
    kb::editor::tests::Require(materialEditor.SetGraphCompositeCollapsed(compositeId, false),
        "KBMAT-MAT56: Material Editor should expand composite subgraphs");
    composite = materialEditor.GraphCompositeSubgraph(compositeId);
    kb::editor::tests::Require(composite.has_value() && !composite->collapsed,
        "KBMAT-MAT56: Composite expanded state should be visible in editor state");
    kb::editor::tests::Require(materialEditor.SetGraphCompositeCollapsed(compositeId, true),
        "KBMAT-MAT56: Material Editor should persist an explicitly collapsed composite");

    const kb::render::RenderMaterialGraphCompileResult compile = kb::render::CompileRenderMaterialGraphToShaderSource(
        materialEditor.WorkingCopy()->graph,
        kb::render::RenderMaterialGraphBuildContext{ .assetId = 0x3800U, .sourcePath = "/Game/Materials/CompositeReroute.kbmat" });
    kb::editor::tests::Require(compile.Succeeded() && compile.shader.source.find("material.baseColor = vec4(0.25, 0.5, 0.75, 1.0);") != std::string::npos,
        "KBMAT-MAT56: Composite/reroute/named reroute editor graph should compile into the routed base color");

    std::ostringstream serialized;
    kb::render::RenderMaterialAssetWriter::Write(serialized, *materialEditor.WorkingCopy());
    const std::string expectedComposite = "graphComposite " + std::to_string(compositeId) + " -220 20 560 180 " +
        std::to_string(0x425B4AU) + " true Nested%20Surface " +
        std::to_string(compositeInputId) + "," + std::to_string(compositeOutputId) + "," + std::to_string(rerouteNodeId) + "\n";
    kb::editor::tests::Require(serialized.str().find(expectedComposite) != std::string::npos,
        "KBMAT-MAT56: Composite metadata should serialize with collapsed state and selected node ids");

    std::istringstream input{ serialized.str() };
    const kb::render::RenderMaterialAssetParseResult parsed = kb::render::RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    kb::editor::tests::Require(parsed.Succeeded() && parsed.asset.has_value() && parsed.asset->graph.composites.size() == 1U,
        "KBMAT-MAT56: Composite material should parse after serialization");
    materialEditor.Open(kb::assets::AssetId{ 0x3801U }, parsed.asset);
    composite = materialEditor.GraphCompositeSubgraph(compositeId);
    kb::editor::tests::Require(composite.has_value() && composite->collapsed && composite->name == "Nested Surface" &&
            composite->nodeIds.size() == 3U,
        "KBMAT-MAT56: Composite should survive save/reopen with collapse state");

    kb::editor::tests::Require(materialEditor.DeleteGraphNode(rerouteNodeId),
        "KBMAT-MAT56: Deleting a graph node should update composite membership");
    composite = materialEditor.GraphCompositeSubgraph(compositeId);
    kb::editor::tests::Require(composite.has_value() && std::ranges::find(composite->nodeIds, rerouteNodeId) == composite->nodeIds.end(),
        "KBMAT-MAT56: Composite should not keep stale node references after deletion");
}

void RunMaterialEditorGraphNodeCreationUxModelTest() {
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateTextureSample, "tex sample"),
        "KBMAT-MAT57: Palette search should find Texture Sample by a fragmented query");
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateTextureObject, "texture object parameter"),
        "KBMAT-MAT57: Palette search should expose Texture Object by catalog alias");
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateTextureSampleCube, "cubemap environment"),
        "KBMAT-MAT57: Palette search should expose Texture Sample Cube by cubemap aliases");
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateTextureSampleVolume, "texture3d voxel"),
        "KBMAT-MAT57: Palette search should expose Texture Sample Volume by 3D texture aliases");
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateTextureSample2DArray, "array layer"),
        "KBMAT-MAT57: Palette search should expose Texture Sample 2D Array by array/layer aliases");
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateDeltaTime, "time delta"),
        "KBMAT-MAT57: Palette search should expose Delta Time by time-delta aliases");
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateDynamicParameter, "dynamic parameters rgba"),
        "KBMAT-MAT57: Palette search should expose Dynamic Parameter by runtime parameter aliases");
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreatePerInstanceCustomData, "custom data instance"),
        "KBMAT-MAT57: Palette search should expose Per Instance Custom Data by instance-data aliases");
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreatePreSkinnedNormal, "pre skinned local normal"),
        "KBMAT-MAT57: Palette search should expose Pre-Skinned Normal by skinning aliases");
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateTwoSidedSign, "twosidedsign"),
        "KBMAT-MAT57: Palette search should expose TwoSidedSign by catalog alias");
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateSceneColor, "opaque snapshot"),
        "KBMAT-MAT57: Palette search should expose Scene Color by scene-snapshot aliases");
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateSceneTexture, "post process scene texture"),
        "KBMAT-MAT57: Palette search should expose Scene Texture by post-process scene aliases");
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateArcTangent2Fast, "atan2 fast"),
        "KBMAT-MAT57: Palette search should expose fast inverse trig nodes by catalog alias");
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateSwitch, "runtime switch"),
        "KBMAT-MAT57: Palette search should expose Runtime Switch by catalog alias");
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateSobol, "low discrepancy"),
        "KBMAT-MAT57: Palette search should expose Sobol by low-discrepancy catalog alias");
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateColor, "rgba"),
        "KBMAT-MAT57: Palette search should include node pin aliases");
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateColor, "alpha"),
        "KBMAT-MAT57: Palette search should include Constant Color channel pin aliases");
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateBool, "constant bool"),
        "KBMAT-MAT57: Palette search should expose Constant Bool");
    kb::editor::tests::Require(!kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateTextureParameter, "world position"),
        "KBMAT-MAT57: Palette search should reject unrelated commands");

    const std::vector<kb::editor::MaterialEditorGraphMenuCommand> favorites{
        kb::editor::MaterialEditorGraphMenuCommand::CreateMultiply,
        kb::editor::MaterialEditorGraphMenuCommand::CreateReroute,
    };
    kb::editor::tests::Require(
        kb::editor::MaterialEditorGraphContextMenuCategoryName(0U) == "Textures" &&
            kb::editor::MaterialEditorGraphContextMenuCategoryName(1U) == "Inputs" &&
            kb::editor::MaterialEditorGraphContextMenuCategoryName(2U) == "Parameter Inputs" &&
            kb::editor::MaterialEditorGraphContextMenuCategoryName(4U) == "Parameters" &&
            kb::editor::MaterialEditorGraphContextMenuCategoryName(kb::editor::MaterialEditorGraphContextMenuFavoritesCategoryIndex()) == "Favorites",
        "KBMAT-MAT57: Material graph palette category labels must match their command groups");
    const std::vector<kb::editor::MaterialEditorGraphMenuCommand> favoriteCategory =
        kb::editor::MaterialEditorGraphContextMenuCommands(kb::editor::MaterialEditorGraphContextMenuFavoritesCategoryIndex(), favorites);
    kb::editor::tests::Require(favoriteCategory == favorites,
        "KBMAT-MAT57: Palette favorites category should expose the stored favorite commands in order");
    const std::vector<kb::editor::MaterialEditorGraphMenuCommand> textureCommands =
        kb::editor::MaterialEditorGraphContextMenuCommands(0U);
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphCommandInList(textureCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateTextureObject),
        "KBMAT-MAT57: Texture Object must be available from the Textures palette category");
    kb::editor::tests::Require(
        kb::editor::MaterialEditorGraphCommandInList(textureCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateTextureSampleCube) &&
            kb::editor::MaterialEditorGraphCommandInList(textureCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateTextureObjectCube) &&
            kb::editor::MaterialEditorGraphCommandInList(textureCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateTextureSampleVolume) &&
            kb::editor::MaterialEditorGraphCommandInList(textureCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateTextureObjectVolume) &&
            kb::editor::MaterialEditorGraphCommandInList(textureCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateTextureSample2DArray) &&
            kb::editor::MaterialEditorGraphCommandInList(textureCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateTextureObject2DArray),
        "KBMAT-MAT57: Advanced cube/volume/array texture sample and object nodes must be available from the Textures palette category");
    const std::vector<kb::editor::MaterialEditorGraphMenuCommand> inputCommands =
        kb::editor::MaterialEditorGraphContextMenuCommands(1U);
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphCommandInList(inputCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateTwoSidedSign),
        "KBMAT-MAT57: TwoSidedSign must be available from the Inputs palette category");
    kb::editor::tests::Require(
        kb::editor::MaterialEditorGraphCommandInList(inputCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateSceneColor) &&
            kb::editor::MaterialEditorGraphCommandInList(inputCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateSceneTexture),
        "KBMAT-MAT57: Scene Color and Scene Texture must be available from the Inputs palette category");
    kb::editor::tests::Require(
        kb::editor::MaterialEditorGraphCommandInList(inputCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateDeltaTime) &&
            kb::editor::MaterialEditorGraphCommandInList(inputCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateDynamicParameter) &&
            kb::editor::MaterialEditorGraphCommandInList(inputCommands, kb::editor::MaterialEditorGraphMenuCommand::CreatePerInstanceFadeAmount) &&
            kb::editor::MaterialEditorGraphCommandInList(inputCommands, kb::editor::MaterialEditorGraphMenuCommand::CreatePerInstanceCustomData) &&
            kb::editor::MaterialEditorGraphCommandInList(inputCommands, kb::editor::MaterialEditorGraphMenuCommand::CreatePreSkinnedPosition) &&
            kb::editor::MaterialEditorGraphCommandInList(inputCommands, kb::editor::MaterialEditorGraphMenuCommand::CreatePreSkinnedNormal),
        "KBMAT-MAT57: Time, dynamic parameter, per-instance and pre-skinned data nodes must be available from the Inputs palette category");
    const std::vector<kb::editor::MaterialEditorGraphMenuCommand> mathCommands =
        kb::editor::MaterialEditorGraphContextMenuCommands(5U);
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphCommandInList(mathCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateSwitch),
        "KBMAT-MAT57: Runtime Switch must be available from the Math palette category with Step/If");
    const std::vector<kb::editor::MaterialEditorGraphMenuCommand> utilityCommands =
        kb::editor::MaterialEditorGraphContextMenuCommands(6U);
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphCommandInList(utilityCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateArcSineFast) &&
            kb::editor::MaterialEditorGraphCommandInList(utilityCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateArcCosineFast) &&
            kb::editor::MaterialEditorGraphCommandInList(utilityCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateArcTangentFast) &&
            kb::editor::MaterialEditorGraphCommandInList(utilityCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateArcTangent2Fast),
        "KBMAT-MAT57: Fast inverse trig nodes must be available from the Utility palette category");
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphCommandInList(utilityCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateSobol),
        "KBMAT-MAT57: Sobol must be available from the Utility palette category");
    const std::vector<kb::editor::MaterialEditorGraphMenuCommand> actionCommands =
        kb::editor::MaterialEditorGraphContextMenuCommands(10U);
    kb::editor::tests::Require(
        kb::editor::MaterialEditorGraphCommandInList(actionCommands, kb::editor::MaterialEditorGraphMenuCommand::FrameSelected) &&
            kb::editor::MaterialEditorGraphCommandInList(actionCommands, kb::editor::MaterialEditorGraphMenuCommand::SelectUpstream) &&
            kb::editor::MaterialEditorGraphCommandInList(actionCommands, kb::editor::MaterialEditorGraphMenuCommand::SelectDownstream) &&
            kb::editor::MaterialEditorGraphCommandInList(actionCommands, kb::editor::MaterialEditorGraphMenuCommand::AlignLeft) &&
            kb::editor::MaterialEditorGraphCommandInList(actionCommands, kb::editor::MaterialEditorGraphMenuCommand::DistributeHorizontal) &&
            kb::editor::MaterialEditorGraphCommandInList(actionCommands, kb::editor::MaterialEditorGraphMenuCommand::PromoteToParameter),
        "KBMAT-MAT54B: Canvas action menu must expose frame, traversal, align and distribute commands");
    kb::editor::tests::Require(
        kb::editor::MaterialEditorGraphMenuCommandIsAction(kb::editor::MaterialEditorGraphMenuCommand::FrameSelected) &&
            !kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::FrameSelected).has_value() &&
            kb::editor::MaterialEditorGraphContextMenuCommandName(kb::editor::MaterialEditorGraphMenuCommand::AlignMiddle) == "Align Middle" &&
            kb::editor::MaterialEditorGraphContextMenuCommandName(kb::editor::MaterialEditorGraphMenuCommand::PromoteToParameter) == "Promote to Parameter",
        "KBMAT-MAT54B: Canvas action commands should be named actions, not graph node creation commands");
    kb::editor::tests::Require(
        !kb::editor::MaterialEditorGraphContextMenuCommandEnabled(kb::editor::MaterialEditorGraphMenuCommand::FrameSelected, 0U, false) &&
            kb::editor::MaterialEditorGraphContextMenuCommandEnabled(kb::editor::MaterialEditorGraphMenuCommand::FrameSelected, 0U, true) &&
            !kb::editor::MaterialEditorGraphContextMenuCommandEnabled(kb::editor::MaterialEditorGraphMenuCommand::AlignLeft, 1U, false) &&
            kb::editor::MaterialEditorGraphContextMenuCommandEnabled(kb::editor::MaterialEditorGraphMenuCommand::AlignLeft, 2U, false) &&
            !kb::editor::MaterialEditorGraphContextMenuCommandEnabled(kb::editor::MaterialEditorGraphMenuCommand::DistributeHorizontal, 2U, false) &&
            kb::editor::MaterialEditorGraphContextMenuCommandEnabled(kb::editor::MaterialEditorGraphMenuCommand::DistributeHorizontal, 3U, false) &&
            kb::editor::MaterialEditorGraphContextMenuCommandEnabled(kb::editor::MaterialEditorGraphMenuCommand::PromoteToParameter, 1U, false) &&
            !kb::editor::MaterialEditorGraphContextMenuCommandEnabled(kb::editor::MaterialEditorGraphMenuCommand::PromoteToParameter, 2U, false),
        "KBMAT-MAT54B: Canvas action enablement should match selection requirements");
    kb::editor::tests::Require(
        kb::editor::MaterialEditorGraphMenuCommandCreatesCanvasObject(kb::editor::MaterialEditorGraphMenuCommand::CreateMultiply) &&
            kb::editor::MaterialEditorGraphMenuCommandCreatesCanvasObject(kb::editor::MaterialEditorGraphMenuCommand::CreateComment) &&
            !kb::editor::MaterialEditorGraphMenuCommandCreatesCanvasObject(kb::editor::MaterialEditorGraphMenuCommand::PromoteToParameter),
        "KBMAT-MAT57B: Palette drag-to-canvas should only start for commands that create canvas objects");
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::PromoteToParameter, "promote parameter"),
        "KBMAT-MAT57B: Global palette search should expose Promote to Parameter by name");

    const std::vector<kb::editor::MaterialEditorGraphMenuCommand> allPaletteCommands =
        kb::editor::MaterialEditorGraphPaletteAllCommands();
    kb::editor::tests::Require(allPaletteCommands.size() >= 150U,
        "KBMAT-MAT57C: Production palette coverage should include the complete material node catalog");
    std::vector<kb::render::RenderMaterialGraphNodeKind> paletteNodeKinds;
    for (const kb::editor::MaterialEditorGraphMenuCommand command : allPaletteCommands) {
        const std::optional<kb::render::RenderMaterialGraphNodeKind> kind =
            kb::editor::MaterialEditorGraphMenuCommandNodeKind(command);
        if (kind.has_value() && std::ranges::find(paletteNodeKinds, *kind) == paletteNodeKinds.end()) {
            paletteNodeKinds.push_back(*kind);
        }
    }
    for (const kb::render::RenderMaterialGraphNodeKind kind : kb::render::AllRenderMaterialGraphNodeKinds()) {
        if (kind == kb::render::RenderMaterialGraphNodeKind::MaterialOutput) {
            continue;
        }
        if (std::ranges::find(paletteNodeKinds, kind) == paletteNodeKinds.end()) {
            std::cerr << "KBMAT-MAT57C renderer node missing from Material Editor palette: "
                      << kb::render::RenderMaterialGraphNodeKindName(kind) << '\n';
        }
        kb::editor::tests::Require(std::ranges::find(paletteNodeKinds, kind) != paletteNodeKinds.end(),
            "KBMAT-MAT57C: Every renderer graph node kind except MaterialOutput must be reachable from the Material Editor palette");
    }
    for (const kb::render::RenderMaterialGraphNodeKind kind : paletteNodeKinds) {
        if (std::ranges::find(kb::render::AllRenderMaterialGraphNodeKinds(), kind) == kb::render::AllRenderMaterialGraphNodeKinds().end()) {
            std::cerr << "KBMAT-MAT57C palette exposes node outside renderer catalog: "
                      << kb::render::RenderMaterialGraphNodeKindName(kind) << '\n';
        }
        kb::editor::tests::Require(std::ranges::find(kb::render::AllRenderMaterialGraphNodeKinds(), kind) != kb::render::AllRenderMaterialGraphNodeKinds().end(),
            "KBMAT-MAT57C: Material Editor palette must not expose node kinds outside the renderer catalog");
    }
    for (std::size_t categoryIndex = 0U; categoryIndex < kb::editor::kMaterialEditorGraphBaseCategoryCount; ++categoryIndex) {
        const std::vector<kb::editor::MaterialEditorGraphMenuCommand> categoryCommands =
            kb::editor::MaterialEditorGraphContextMenuCommands(categoryIndex);
        kb::editor::tests::Require(!categoryCommands.empty(),
            "KBMAT-MAT57C: Every production palette category should expose commands");
        for (const kb::editor::MaterialEditorGraphMenuCommand command : categoryCommands) {
            kb::editor::tests::Require(command != kb::editor::MaterialEditorGraphMenuCommand::None,
                "KBMAT-MAT57C: Production palette categories must not expose placeholder commands");
            kb::editor::tests::Require(kb::editor::MaterialEditorGraphCommandInList(allPaletteCommands, command),
                "KBMAT-MAT57C: Flattened palette coverage must include every category command");
        }
    }
    for (const kb::editor::MaterialEditorGraphMenuCommand command : allPaletteCommands) {
        const std::string_view commandName = kb::editor::MaterialEditorGraphContextMenuCommandName(command);
        if (commandName.empty()) {
            std::cerr << "KBMAT-MAT57C unnamed palette command id=" << static_cast<std::uint32_t>(command) << '\n';
        }
        kb::editor::tests::Require(!commandName.empty(),
            "KBMAT-MAT57C: Every palette command must have a display name");
        kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(command, commandName),
            "KBMAT-MAT57C: Palette search must match every command's display name");

        const bool isAction = kb::editor::MaterialEditorGraphMenuCommandIsAction(command);
        const bool isCanvasOnlyObject = command == kb::editor::MaterialEditorGraphMenuCommand::CreateComment ||
            command == kb::editor::MaterialEditorGraphMenuCommand::CreateComposite;
        const std::optional<kb::render::RenderMaterialGraphNodeKind> kind =
            kb::editor::MaterialEditorGraphMenuCommandNodeKind(command);
        if (isAction) {
            kb::editor::tests::Require(!kind.has_value() && !kb::editor::MaterialEditorGraphMenuCommandCreatesCanvasObject(command),
                "KBMAT-MAT57C: Canvas actions must not masquerade as node-creation commands");
            continue;
        }
        if (isCanvasOnlyObject) {
            kb::editor::tests::Require(!kind.has_value() && kb::editor::MaterialEditorGraphMenuCommandCreatesCanvasObject(command),
                "KBMAT-MAT57C: Comment and composite commands should create canvas-only objects");
            continue;
        }
        if (!kind.has_value()) {
            std::cerr << "KBMAT-MAT57C missing node kind for palette command '" << commandName << "'\n";
        }
        kb::editor::tests::Require(kind.has_value() && kb::editor::MaterialEditorGraphMenuCommandCreatesCanvasObject(command),
            "KBMAT-MAT57C: Every node palette command must map to a runtime graph kind");

        kb::editor::MaterialEditorState coverageEditor;
        kb::render::RenderMaterialAssetData coverageMaterial{};
        coverageMaterial.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
        coverageEditor.Open(kb::assets::AssetId{ 0x57C000U + static_cast<std::uint64_t>(command) }, coverageMaterial);
        std::uint32_t nodeId = 0U;
        kb::editor::tests::Require(coverageEditor.AddGraphNode(*kind, -96, 48, &nodeId),
            "KBMAT-MAT57C: Every palette node command must create a graph node through MaterialEditorState");
        const kb::render::RenderMaterialGraphNode* node =
            kb::render::FindRenderMaterialGraphNode(coverageEditor.WorkingCopy()->graph, nodeId);
        kb::editor::tests::Require(node != nullptr &&
                node->kind == *kind &&
                node->positionX == -96 &&
                node->positionY == 48,
            "KBMAT-MAT57C: Palette-created nodes must persist with the requested kind and position");

        const std::vector<std::string> inputPins = kb::render::RenderMaterialGraphNodeInputPinNames(*node);
        const std::vector<std::string> outputPins = kb::render::RenderMaterialGraphNodeOutputPinNames(*node);
        if (inputPins.empty() && outputPins.empty()) {
            std::cerr << "KBMAT-MAT57C palette node has no pins: " << kb::render::RenderMaterialGraphNodeKindName(*kind) << '\n';
        }
        kb::editor::tests::Require(!inputPins.empty() || !outputPins.empty(),
            "KBMAT-MAT57C: Every palette-created graph node must expose runtime pins");
        for (const std::string& pin : inputPins) {
            const bool validPin = kb::render::IsRenderMaterialGraphInputPin(*node, pin);
            const std::uint32_t stablePinId = kb::render::RenderMaterialGraphStablePinId(*node, pin, false);
            if (!validPin || stablePinId == 0U) {
                std::cerr << "KBMAT-MAT57C invalid input pin node=" << kb::render::RenderMaterialGraphNodeKindName(*kind)
                          << " pin=" << pin
                          << " valid=" << validPin
                          << " stablePinId=" << stablePinId << '\n';
            }
            kb::editor::tests::Require(validPin && stablePinId != 0U,
                "KBMAT-MAT57C: Palette-created node input pins must be addressable by stable ids");
        }
        for (const std::string& pin : outputPins) {
            const bool validPin = kb::render::IsRenderMaterialGraphOutputPin(*node, pin);
            const std::uint32_t stablePinId = kb::render::RenderMaterialGraphStablePinId(*node, pin, true);
            if (!validPin || stablePinId == 0U) {
                std::cerr << "KBMAT-MAT57C invalid output pin node=" << kb::render::RenderMaterialGraphNodeKindName(*kind)
                          << " pin=" << pin
                          << " valid=" << validPin
                          << " stablePinId=" << stablePinId << '\n';
            }
            kb::editor::tests::Require(validPin && stablePinId != 0U,
                "KBMAT-MAT57C: Palette-created node output pins must be addressable by stable ids");
        }
    }

    kb::editor::MaterialEditorState paletteRoundTripEditor;
    kb::render::RenderMaterialAssetData paletteRoundTripMaterial{};
    paletteRoundTripMaterial.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    paletteRoundTripEditor.Open(kb::assets::AssetId{ 0x57C900U }, paletteRoundTripMaterial);
    std::vector<std::pair<std::uint32_t, kb::render::RenderMaterialGraphNodeKind>> paletteRoundTripNodes;
    for (const kb::editor::MaterialEditorGraphMenuCommand command : allPaletteCommands) {
        const std::optional<kb::render::RenderMaterialGraphNodeKind> kind =
            kb::editor::MaterialEditorGraphMenuCommandNodeKind(command);
        if (!kind.has_value()) {
            continue;
        }
        const std::int32_t column = static_cast<std::int32_t>(paletteRoundTripNodes.size() % 12U);
        const std::int32_t row = static_cast<std::int32_t>(paletteRoundTripNodes.size() / 12U);
        std::uint32_t nodeId = 0U;
        kb::editor::tests::Require(paletteRoundTripEditor.AddGraphNode(*kind, -900 + column * 180, -480 + row * 120, &nodeId),
            "KBMAT-MAT57C: Every palette node should join a complete material graph before serialization");
        paletteRoundTripNodes.emplace_back(nodeId, *kind);
    }
    std::ostringstream paletteRoundTripSerialized;
    kb::render::RenderMaterialAssetWriter::Write(paletteRoundTripSerialized, *paletteRoundTripEditor.WorkingCopy());
    std::istringstream paletteRoundTripInput{ paletteRoundTripSerialized.str() };
    const kb::render::RenderMaterialAssetParseResult paletteRoundTripParsed =
        kb::render::RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(paletteRoundTripInput);
    if (!paletteRoundTripParsed.asset.has_value()) {
        for (const auto& diagnostic : paletteRoundTripParsed.diagnostics) {
            std::cerr << "KBMAT-MAT57C palette round-trip diagnostic line=" << diagnostic.line
                      << " field=" << diagnostic.field
                      << " text=" << diagnostic.text
                      << ": " << diagnostic.message << '\n';
        }
    }
    kb::editor::tests::Require(paletteRoundTripParsed.asset.has_value(),
        "KBMAT-MAT57C: Complete palette graph should deserialize after writer round-trip");
    kb::editor::tests::Require(paletteRoundTripParsed.asset->graph.nodes.size() == paletteRoundTripEditor.WorkingCopy()->graph.nodes.size(),
        "KBMAT-MAT57C: Complete palette graph round-trip should preserve every graph node");
    for (const auto& [nodeId, kind] : paletteRoundTripNodes) {
        const kb::render::RenderMaterialGraphNode* reloadedNode =
            kb::render::FindRenderMaterialGraphNode(paletteRoundTripParsed.asset->graph, nodeId);
        if (reloadedNode == nullptr || reloadedNode->kind != kind) {
            std::cerr << "KBMAT-MAT57C palette round-trip missing node id=" << nodeId
                      << " kind=" << kb::render::RenderMaterialGraphNodeKindName(kind) << '\n';
        }
        kb::editor::tests::Require(reloadedNode != nullptr && reloadedNode->kind == kind,
            "KBMAT-MAT57C: Complete palette graph round-trip should preserve node ids and kinds");
        kb::editor::tests::Require(!kb::render::RenderMaterialGraphNodeInputPinNames(*reloadedNode).empty() ||
                !kb::render::RenderMaterialGraphNodeOutputPinNames(*reloadedNode).empty(),
            "KBMAT-MAT57C: Round-tripped palette nodes should keep runtime-visible pins");
    }

    std::size_t compiledPaletteNodeCount = 0U;
    for (const kb::render::RenderMaterialGraphNodeKind kind : paletteNodeKinds) {
        if (kind == kb::render::RenderMaterialGraphNodeKind::CollectionParameter ||
            kind == kb::render::RenderMaterialGraphNodeKind::FunctionInput ||
            kind == kb::render::RenderMaterialGraphNodeKind::FunctionOutput ||
            kind == kb::render::RenderMaterialGraphNodeKind::MaterialFunctionCall ||
            kind == kb::render::RenderMaterialGraphNodeKind::LayerStack) {
            continue;
        }
        kb::editor::MaterialEditorState compileEditor;
        kb::render::RenderMaterialAssetData compileMaterial{};
        compileMaterial.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
        compileMaterial.graph.shadingModel = "unlit";
        compileEditor.Open(kb::assets::AssetId{ 0x57CA00U + static_cast<std::uint64_t>(kind) }, compileMaterial);
        std::uint32_t subjectNodeId = 0U;
        kb::editor::tests::Require(compileEditor.AddGraphNode(kind, -260, 80, &subjectNodeId),
            "KBMAT-MAT57C: Palette-created node should be authorable before compile coverage");
        const kb::render::RenderMaterialGraphNode* subjectNode =
            kb::render::FindRenderMaterialGraphNode(compileEditor.WorkingCopy()->graph, subjectNodeId);
        kb::editor::tests::Require(subjectNode != nullptr,
            "KBMAT-MAT57C: Palette-created compile coverage node should exist in the working graph");

        const std::vector<std::string> outputPins = kb::render::RenderMaterialGraphNodeOutputPinNames(*subjectNode);
        if (outputPins.empty()) {
            continue;
        }
        const std::string outputPin = outputPins.front();
        const kb::render::RenderMaterialGraphPinType outputType =
            kb::render::RenderMaterialGraphPinDataType(*subjectNode, outputPin, true);
        if (kind == kb::render::RenderMaterialGraphNodeKind::Reroute ||
            kind == kb::render::RenderMaterialGraphNodeKind::CompositeInput ||
            kind == kb::render::RenderMaterialGraphNodeKind::CompositeOutput) {
            std::uint32_t colorNodeId = 0U;
            kb::editor::tests::Require(compileEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantColor, -520, 80, &colorNodeId) &&
                    compileEditor.ConnectGraphPins(colorNodeId, "rgba", subjectNodeId, "input"),
                "KBMAT-MAT57C: Palette-created pass-through nodes should compile with an editor-authored source input");
        } else if (kind == kb::render::RenderMaterialGraphNodeKind::NamedRerouteUsage) {
            std::uint32_t declarationNodeId = 0U;
            std::uint32_t colorNodeId = 0U;
            kb::editor::tests::Require(compileEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::NamedRerouteDeclaration, -360, 80, &declarationNodeId) &&
                    compileEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantColor, -620, 80, &colorNodeId) &&
                    compileEditor.ConnectGraphPins(colorNodeId, "rgba", declarationNodeId, "input"),
                "KBMAT-MAT57C: Palette-created named reroute usage should compile with an editor-authored declaration");
        }

        bool routed = false;
        switch (outputType) {
        case kb::render::RenderMaterialGraphPinType::Texture2D: {
            std::uint32_t sampleNodeId = 0U;
            routed = compileEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::TextureSample, -40, 80, &sampleNodeId) &&
                compileEditor.ConnectGraphPins(subjectNodeId, outputPin, sampleNodeId, "texture") &&
                compileEditor.ConnectGraphPins(sampleNodeId, "color", 1U, "baseColor");
            break;
        }
        case kb::render::RenderMaterialGraphPinType::TextureCube: {
            std::uint32_t sampleNodeId = 0U;
            routed = compileEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::TextureSampleCube, -40, 80, &sampleNodeId) &&
                compileEditor.ConnectGraphPins(subjectNodeId, outputPin, sampleNodeId, "texture") &&
                compileEditor.ConnectGraphPins(sampleNodeId, "color", 1U, "baseColor");
            break;
        }
        case kb::render::RenderMaterialGraphPinType::Texture3D: {
            std::uint32_t sampleNodeId = 0U;
            routed = compileEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::TextureSampleVolume, -40, 80, &sampleNodeId) &&
                compileEditor.ConnectGraphPins(subjectNodeId, outputPin, sampleNodeId, "texture") &&
                compileEditor.ConnectGraphPins(sampleNodeId, "color", 1U, "baseColor");
            break;
        }
        case kb::render::RenderMaterialGraphPinType::Texture2DArray: {
            std::uint32_t sampleNodeId = 0U;
            routed = compileEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::TextureSample2DArray, -40, 80, &sampleNodeId) &&
                compileEditor.ConnectGraphPins(subjectNodeId, outputPin, sampleNodeId, "texture") &&
                compileEditor.ConnectGraphPins(sampleNodeId, "color", 1U, "baseColor");
            break;
        }
        case kb::render::RenderMaterialGraphPinType::Float2: {
            std::uint32_t sampleNodeId = 0U;
            routed = compileEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::TextureSample, -40, 80, &sampleNodeId) &&
                compileEditor.ConnectGraphPins(subjectNodeId, outputPin, sampleNodeId, "uv") &&
                compileEditor.ConnectGraphPins(sampleNodeId, "color", 1U, "baseColor");
            break;
        }
        case kb::render::RenderMaterialGraphPinType::MaterialAttributes:
            routed = compileEditor.ConnectGraphPins(subjectNodeId, outputPin, 1U, "attributes");
            break;
        case kb::render::RenderMaterialGraphPinType::Float:
            routed = compileEditor.ConnectGraphPins(subjectNodeId, outputPin, 1U, "roughness");
            break;
        case kb::render::RenderMaterialGraphPinType::Normal:
            routed = compileEditor.ConnectGraphPins(subjectNodeId, outputPin, 1U, "normal");
            break;
        case kb::render::RenderMaterialGraphPinType::Float3:
            routed = compileEditor.ConnectGraphPins(subjectNodeId, outputPin, 1U, "emissive");
            break;
        case kb::render::RenderMaterialGraphPinType::Bool:
        case kb::render::RenderMaterialGraphPinType::Float4:
        case kb::render::RenderMaterialGraphPinType::Color:
        case kb::render::RenderMaterialGraphPinType::Unknown:
        case kb::render::RenderMaterialGraphPinType::Sampler:
        default:
            routed = compileEditor.ConnectGraphPins(subjectNodeId, outputPin, 1U, "baseColor");
            break;
        }
        if (!routed) {
            std::cerr << "KBMAT-MAT57C palette-created node failed compile routing: "
                      << kb::render::RenderMaterialGraphNodeKindName(kind)
                      << " output=" << outputPin << '\n';
        }
        kb::editor::tests::Require(routed,
            "KBMAT-MAT57C: Palette-created node output should route through public MaterialEditorState links");

        const kb::render::RenderMaterialGraphCompileResult compiled = kb::render::CompileRenderMaterialGraphToShaderSource(
            compileEditor.WorkingCopy()->graph,
            kb::render::RenderMaterialGraphBuildContext{ .assetId = 0x57CA00U + static_cast<std::uint64_t>(kind) });
        if (!compiled.Succeeded()) {
            std::cerr << "KBMAT-MAT57C palette-created node failed shader compile: "
                      << kb::render::RenderMaterialGraphNodeKindName(kind);
            if (!compiled.diagnostics.empty()) {
                std::cerr << " diagnostic=" << compiled.diagnostics.front().message;
            }
            std::cerr << '\n';
        }
        kb::editor::tests::Require(compiled.Succeeded() && !compiled.shader.source.empty(),
            "KBMAT-MAT57C: Palette-created node should compile through the real material graph shader generator");
        ++compiledPaletteNodeCount;
    }
    kb::editor::tests::Require(compiledPaletteNodeCount >= 140U,
        "KBMAT-MAT57C: Palette-created compile coverage should exercise the production node catalog");

    kb::render::RenderMaterialGraphDocument graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    const std::vector<kb::editor::MaterialEditorGraphMenuCommand> baseColorCompatible =
        kb::editor::MaterialEditorGraphCompatibleCommands(graph, 1U, "baseColor", false);
    kb::editor::tests::Require(
        kb::editor::MaterialEditorGraphCommandInList(baseColorCompatible, kb::editor::MaterialEditorGraphMenuCommand::CreateColor) &&
            kb::editor::MaterialEditorGraphCommandInList(baseColorCompatible, kb::editor::MaterialEditorGraphMenuCommand::CreateBool) &&
            kb::editor::MaterialEditorGraphCommandInList(baseColorCompatible, kb::editor::MaterialEditorGraphMenuCommand::CreateTextureSample) &&
            kb::editor::MaterialEditorGraphCommandInList(baseColorCompatible, kb::editor::MaterialEditorGraphMenuCommand::CreateReroute) &&
            !kb::editor::MaterialEditorGraphCommandInList(baseColorCompatible, kb::editor::MaterialEditorGraphMenuCommand::CreateTextureParameter),
        "KBMAT-MAT57: Drag-from-input palette should list only commands with compatible outputs");
    const std::optional<std::string> boolOutputPin = kb::editor::MaterialEditorGraphCompatibleCommandPin(
        graph,
        1U,
        "baseColor",
        false,
        kb::editor::MaterialEditorGraphMenuCommand::CreateBool);
    kb::editor::tests::Require(boolOutputPin.has_value() && *boolOutputPin == "value",
        "KBMAT-MAT57: Drag-from-input palette should select Constant Bool's value output pin for auto-connect");
    const std::optional<std::string> colorOutputPin = kb::editor::MaterialEditorGraphCompatibleCommandPin(
        graph,
        1U,
        "baseColor",
        false,
        kb::editor::MaterialEditorGraphMenuCommand::CreateColor);
    kb::editor::tests::Require(colorOutputPin.has_value() && *colorOutputPin == "rgba",
        "KBMAT-MAT57: Drag-from-input palette should select the compatible output pin for auto-connect");
    const std::vector<kb::editor::MaterialEditorGraphMenuCommand> alphaCompatible =
        kb::editor::MaterialEditorGraphCompatibleCommands(graph, 1U, "alpha", false);
    kb::editor::tests::Require(
        kb::editor::MaterialEditorGraphCommandInList(alphaCompatible, kb::editor::MaterialEditorGraphMenuCommand::CreateColor),
        "KBMAT-MAT57: Drag-from-alpha palette should list Constant Color because its alpha channel is a real output pin");
    const std::optional<std::string> colorAlphaOutputPin = kb::editor::MaterialEditorGraphCompatibleCommandPin(
        graph,
        1U,
        "alpha",
        false,
        kb::editor::MaterialEditorGraphMenuCommand::CreateColor);
    kb::editor::tests::Require(colorAlphaOutputPin.has_value() && *colorAlphaOutputPin == "a",
        "KBMAT-MAT57: Drag-from-alpha palette should prefer Constant Color's alpha output pin for auto-connect");

    kb::editor::MaterialEditorState materialEditor;
    kb::render::RenderMaterialAssetData material{};
    material.graph = graph;
    materialEditor.Open(kb::assets::AssetId{ 0x3900U }, material);
    const std::optional<kb::render::RenderMaterialGraphNodeKind> colorKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateColor);
    kb::editor::tests::Require(colorKind.has_value(), "KBMAT-MAT57: CreateColor command should map to a graph node kind");
    const std::optional<kb::render::RenderMaterialGraphNodeKind> boolKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateBool);
    kb::editor::tests::Require(boolKind.has_value() && *boolKind == kb::render::RenderMaterialGraphNodeKind::ConstantBool,
        "KBMAT-MAT57: CreateBool command should map to the ConstantBool graph node kind");
    const std::optional<kb::render::RenderMaterialGraphNodeKind> textureObjectKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateTextureObject);
    kb::editor::tests::Require(textureObjectKind.has_value() && *textureObjectKind == kb::render::RenderMaterialGraphNodeKind::TextureObject,
        "KBMAT-MAT57: CreateTextureObject command should map to the TextureObject graph node kind");
    const std::optional<kb::render::RenderMaterialGraphNodeKind> textureCubeSampleKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateTextureSampleCube);
    const std::optional<kb::render::RenderMaterialGraphNodeKind> textureCubeObjectKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateTextureObjectCube);
    const std::optional<kb::render::RenderMaterialGraphNodeKind> textureVolumeSampleKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateTextureSampleVolume);
    const std::optional<kb::render::RenderMaterialGraphNodeKind> textureVolumeObjectKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateTextureObjectVolume);
    const std::optional<kb::render::RenderMaterialGraphNodeKind> textureArraySampleKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateTextureSample2DArray);
    const std::optional<kb::render::RenderMaterialGraphNodeKind> textureArrayObjectKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateTextureObject2DArray);
    kb::editor::tests::Require(
        textureCubeSampleKind == kb::render::RenderMaterialGraphNodeKind::TextureSampleCube &&
            textureCubeObjectKind == kb::render::RenderMaterialGraphNodeKind::TextureObjectCube &&
            textureVolumeSampleKind == kb::render::RenderMaterialGraphNodeKind::TextureSampleVolume &&
            textureVolumeObjectKind == kb::render::RenderMaterialGraphNodeKind::TextureObjectVolume &&
            textureArraySampleKind == kb::render::RenderMaterialGraphNodeKind::TextureSample2DArray &&
            textureArrayObjectKind == kb::render::RenderMaterialGraphNodeKind::TextureObject2DArray,
        "KBMAT-MAT57: Advanced texture palette commands must map to the runtime graph node kinds");
    const std::optional<kb::render::RenderMaterialGraphNodeKind> twoSidedSignKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateTwoSidedSign);
    kb::editor::tests::Require(twoSidedSignKind.has_value() && *twoSidedSignKind == kb::render::RenderMaterialGraphNodeKind::TwoSidedSign,
        "KBMAT-MAT57: CreateTwoSidedSign command should map to the TwoSidedSign graph node kind");
    const std::optional<kb::render::RenderMaterialGraphNodeKind> sceneColorKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateSceneColor);
    kb::editor::tests::Require(sceneColorKind.has_value() && *sceneColorKind == kb::render::RenderMaterialGraphNodeKind::SceneColor,
        "KBMAT-MAT57: CreateSceneColor command should map to the SceneColor graph node kind");
    const std::optional<kb::render::RenderMaterialGraphNodeKind> sceneTextureKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateSceneTexture);
    kb::editor::tests::Require(sceneTextureKind.has_value() && *sceneTextureKind == kb::render::RenderMaterialGraphNodeKind::SceneTexture,
        "KBMAT-MAT57: CreateSceneTexture command should map to the SceneTexture graph node kind");
    const std::optional<kb::render::RenderMaterialGraphNodeKind> pixelDepthKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreatePixelDepth);
    kb::editor::tests::Require(pixelDepthKind.has_value() && *pixelDepthKind == kb::render::RenderMaterialGraphNodeKind::PixelDepth,
        "KBMAT-MAT57: CreatePixelDepth command should map to the PixelDepth graph node kind");
    const std::optional<kb::render::RenderMaterialGraphNodeKind> pixelPositionKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreatePixelPosition);
    kb::editor::tests::Require(pixelPositionKind.has_value() && *pixelPositionKind == kb::render::RenderMaterialGraphNodeKind::PixelPosition,
        "KBMAT-MAT57: CreatePixelPosition command should map to the PixelPosition graph node kind");
    const std::optional<kb::render::RenderMaterialGraphNodeKind> cameraDepthFadeKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateCameraDepthFade);
    kb::editor::tests::Require(cameraDepthFadeKind.has_value() && *cameraDepthFadeKind == kb::render::RenderMaterialGraphNodeKind::CameraDepthFade,
        "KBMAT-MAT57: CreateCameraDepthFade command should map to the CameraDepthFade graph node kind");
    const std::optional<kb::render::RenderMaterialGraphNodeKind> atan2FastKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateArcTangent2Fast);
    kb::editor::tests::Require(atan2FastKind.has_value() && *atan2FastKind == kb::render::RenderMaterialGraphNodeKind::ArcTangent2Fast,
        "KBMAT-MAT57: CreateArcTangent2Fast command should map to the ArcTangent2Fast graph node kind");
    const std::optional<kb::render::RenderMaterialGraphNodeKind> runtimeSwitchKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateSwitch);
    kb::editor::tests::Require(runtimeSwitchKind.has_value() && *runtimeSwitchKind == kb::render::RenderMaterialGraphNodeKind::RuntimeSwitch,
        "KBMAT-MAT57: CreateSwitch command should map to the Runtime Switch graph node kind");
    const std::optional<kb::render::RenderMaterialGraphNodeKind> sobolKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateSobol);
    kb::editor::tests::Require(sobolKind.has_value() && *sobolKind == kb::render::RenderMaterialGraphNodeKind::Sobol,
        "KBMAT-MAT57: CreateSobol command should map to the Sobol graph node kind");
    const std::optional<kb::render::RenderMaterialGraphNodeKind> deltaTimeKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateDeltaTime);
    const std::optional<kb::render::RenderMaterialGraphNodeKind> dynamicParameterKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateDynamicParameter);
    const std::optional<kb::render::RenderMaterialGraphNodeKind> perInstanceFadeKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreatePerInstanceFadeAmount);
    const std::optional<kb::render::RenderMaterialGraphNodeKind> distanceCullFadeKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateDistanceCullFade);
    const std::optional<kb::render::RenderMaterialGraphNodeKind> perInstanceCustomDataKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreatePerInstanceCustomData);
    const std::optional<kb::render::RenderMaterialGraphNodeKind> preSkinnedPositionKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreatePreSkinnedPosition);
    const std::optional<kb::render::RenderMaterialGraphNodeKind> preSkinnedNormalKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreatePreSkinnedNormal);
    kb::editor::tests::Require(
        deltaTimeKind == kb::render::RenderMaterialGraphNodeKind::DeltaTime &&
            dynamicParameterKind == kb::render::RenderMaterialGraphNodeKind::DynamicParameter &&
            perInstanceFadeKind == kb::render::RenderMaterialGraphNodeKind::PerInstanceFadeAmount &&
            distanceCullFadeKind == kb::render::RenderMaterialGraphNodeKind::DistanceCullFade &&
            perInstanceCustomDataKind == kb::render::RenderMaterialGraphNodeKind::PerInstanceCustomData &&
            preSkinnedPositionKind == kb::render::RenderMaterialGraphNodeKind::PreSkinnedPosition &&
            preSkinnedNormalKind == kb::render::RenderMaterialGraphNodeKind::PreSkinnedNormal,
        "KBMAT-MAT57: Runtime input palette commands must map to their renderer graph node kinds");
    std::uint32_t createdNodeId = 0U;
    kb::editor::tests::Require(materialEditor.AddGraphNode(*colorKind, -240, 96, &createdNodeId),
        "KBMAT-MAT57: Drag-from-pin should be able to create the selected compatible node");
    kb::editor::tests::Require(materialEditor.ConnectGraphPins(createdNodeId, *colorOutputPin, 1U, "baseColor"),
        "KBMAT-MAT57: Drag-from-pin should auto-connect the created node to the source input pin");
    kb::editor::tests::Require(materialEditor.WorkingCopy()->graph.links.size() == 1U &&
            materialEditor.WorkingCopy()->graph.links[0].fromNodeId == createdNodeId &&
            materialEditor.WorkingCopy()->graph.links[0].fromPin == "rgba" &&
            materialEditor.WorkingCopy()->graph.links[0].toNodeId == 1U &&
            materialEditor.WorkingCopy()->graph.links[0].toPin == "baseColor",
        "KBMAT-MAT57: Drag-from-pin auto-create should leave a real material graph link");

    std::uint32_t sceneColorNodeId = 0U;
    std::uint32_t sceneTextureNodeId = 0U;
    std::uint32_t pixelDepthNodeId = 0U;
    std::uint32_t pixelPositionNodeId = 0U;
    std::uint32_t cameraDepthFadeNodeId = 0U;
    kb::editor::tests::Require(
        materialEditor.AddGraphNode(*sceneColorKind, -120, 160, &sceneColorNodeId) &&
            materialEditor.AddGraphNode(*sceneTextureKind, 120, 160, &sceneTextureNodeId) &&
            materialEditor.AddGraphNode(*pixelDepthKind, 320, 160, &pixelDepthNodeId) &&
            materialEditor.AddGraphNode(*pixelPositionKind, 520, 160, &pixelPositionNodeId) &&
            materialEditor.AddGraphNode(*cameraDepthFadeKind, 720, 160, &cameraDepthFadeNodeId),
        "KBMAT-MAT57: MaterialEditorState must create SceneColor, SceneTexture, PixelDepth, PixelPosition and CameraDepthFade nodes from palette commands");
    const kb::render::RenderMaterialGraphNode* sceneColorNode =
        kb::render::FindRenderMaterialGraphNode(materialEditor.WorkingCopy()->graph, sceneColorNodeId);
    const kb::render::RenderMaterialGraphNode* sceneTextureNode =
        kb::render::FindRenderMaterialGraphNode(materialEditor.WorkingCopy()->graph, sceneTextureNodeId);
    const kb::render::RenderMaterialGraphNode* pixelDepthNode =
        kb::render::FindRenderMaterialGraphNode(materialEditor.WorkingCopy()->graph, pixelDepthNodeId);
    const kb::render::RenderMaterialGraphNode* pixelPositionNode =
        kb::render::FindRenderMaterialGraphNode(materialEditor.WorkingCopy()->graph, pixelPositionNodeId);
    const kb::render::RenderMaterialGraphNode* cameraDepthFadeNode =
        kb::render::FindRenderMaterialGraphNode(materialEditor.WorkingCopy()->graph, cameraDepthFadeNodeId);
    kb::editor::tests::Require(sceneColorNode != nullptr &&
            sceneTextureNode != nullptr &&
            pixelDepthNode != nullptr &&
            pixelPositionNode != nullptr &&
            cameraDepthFadeNode != nullptr &&
            kb::render::RenderMaterialGraphPinDataType(*sceneColorNode, "color", true) == kb::render::RenderMaterialGraphPinType::Color &&
            kb::render::RenderMaterialGraphPinDataType(*sceneTextureNode, "color", true) == kb::render::RenderMaterialGraphPinType::Color &&
            kb::render::RenderMaterialGraphPinDataType(*pixelDepthNode, "value", true) == kb::render::RenderMaterialGraphPinType::Float &&
            kb::render::RenderMaterialGraphPinDataType(*pixelPositionNode, "xy", true) == kb::render::RenderMaterialGraphPinType::Float2 &&
            kb::render::RenderMaterialGraphPinDataType(*cameraDepthFadeNode, "value", true) == kb::render::RenderMaterialGraphPinType::Float,
        "KBMAT-MAT57: Scene/depth palette nodes must expose real typed output pins");

    const auto compilePaletteRuntimeInput = [](
        kb::editor::MaterialEditorGraphMenuCommand command,
        std::string_view outputPin,
        std::string_view materialInputPin,
        std::string_view expectedSourceToken,
        std::optional<std::string_view> expectedVarying,
        std::uint64_t assetId,
        const char* message) {
        kb::editor::MaterialEditorState editor;
        kb::render::RenderMaterialAssetData asset{};
        asset.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
        asset.graph.shadingModel = "unlit";
        editor.Open(kb::assets::AssetId{ assetId }, asset);
        const std::optional<kb::render::RenderMaterialGraphNodeKind> kind =
            kb::editor::MaterialEditorGraphMenuCommandNodeKind(command);
        std::uint32_t nodeId = 0U;
        kb::editor::tests::Require(kind.has_value() && editor.AddGraphNode(*kind, -160, 96, &nodeId), message);
        const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(editor.WorkingCopy()->graph, nodeId);
        kb::editor::tests::Require(node != nullptr && kb::render::IsRenderMaterialGraphOutputPin(*node, outputPin),
            "KBMAT-MAT57: Palette-created runtime input node must expose the selected output pin");
        kb::editor::tests::Require(editor.ConnectGraphPins(nodeId, outputPin, 1U, materialInputPin),
            "KBMAT-MAT57: Palette-created runtime input node must connect to a real material output pin");
        const kb::render::RenderMaterialGraphCompileResult compiled = kb::render::CompileRenderMaterialGraphToShaderSource(
            editor.WorkingCopy()->graph,
            kb::render::RenderMaterialGraphBuildContext{ .assetId = assetId });
        kb::editor::tests::Require(compiled.Succeeded(), message);
        kb::editor::tests::Require(compiled.shader.source.find(expectedSourceToken) != std::string::npos,
            "KBMAT-MAT57: Palette-created runtime input node must emit the expected shader context read");
        if (expectedVarying.has_value()) {
            kb::editor::tests::Require(
                std::ranges::find(compiled.shader.reflection.requiredVaryings, std::string{ *expectedVarying }) !=
                    compiled.shader.reflection.requiredVaryings.end(),
                "KBMAT-MAT57: Palette-created runtime input node must request its required vertex varying");
        } else {
            kb::editor::tests::Require(compiled.shader.reflection.requiredVaryings.empty(),
                "KBMAT-MAT57: Uniform-backed runtime input nodes must not request vertex varyings");
        }
    };
    const auto compilePaletteFloat2Input = [](
        kb::editor::MaterialEditorGraphMenuCommand command,
        std::string_view outputPin,
        std::string_view expectedSourceToken,
        std::uint64_t assetId,
        const char* message) {
        kb::editor::MaterialEditorState editor;
        kb::render::RenderMaterialAssetData asset{};
        asset.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
        asset.graph.shadingModel = "unlit";
        editor.Open(kb::assets::AssetId{ assetId }, asset);
        const std::optional<kb::render::RenderMaterialGraphNodeKind> kind =
            kb::editor::MaterialEditorGraphMenuCommandNodeKind(command);
        std::uint32_t nodeId = 0U;
        kb::editor::tests::Require(kind.has_value() && editor.AddGraphNode(*kind, -260, 96, &nodeId), message);
        const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(editor.WorkingCopy()->graph, nodeId);
        kb::editor::tests::Require(node != nullptr &&
                kb::render::RenderMaterialGraphPinDataType(*node, outputPin, true) == kb::render::RenderMaterialGraphPinType::Float2,
            "KBMAT-MAT57: Palette-created coordinate node must expose a float2 output pin");
        std::uint32_t sampleId = 0U;
        kb::editor::tests::Require(editor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::TextureSample, -60, 96, &sampleId),
            "KBMAT-MAT57: Palette-created coordinate graph must create a real texture sample node");
        const kb::render::RenderMaterialGraphNode* sampleNode = kb::render::FindRenderMaterialGraphNode(editor.WorkingCopy()->graph, sampleId);
        kb::editor::tests::Require(sampleNode != nullptr, "KBMAT-MAT57: Palette-created coordinate graph texture sample must be addressable");
        kb::editor::tests::Require(editor.ConnectGraphPins(nodeId, outputPin, sampleId, "uv"),
            "KBMAT-MAT57: Palette-created coordinate node must connect to a texture UV pin");
        kb::editor::tests::Require(editor.ConnectGraphPins(sampleId, "color", 1U, "baseColor"),
            "KBMAT-MAT57: Palette-created coordinate graph must connect sampled color to baseColor");
        const kb::render::RenderMaterialGraphCompileResult compiled = kb::render::CompileRenderMaterialGraphToShaderSource(
            editor.WorkingCopy()->graph,
            kb::render::RenderMaterialGraphBuildContext{ .assetId = assetId });
        kb::editor::tests::Require(compiled.Succeeded(), message);
        kb::editor::tests::Require(compiled.shader.source.find(expectedSourceToken) != std::string::npos,
            "KBMAT-MAT57: Palette-created coordinate node must emit the expected shader context read");
    };
    compilePaletteRuntimeInput(
        kb::editor::MaterialEditorGraphMenuCommand::CreateDeltaTime,
        "value",
        "alpha",
        "ctx.deltaTime",
        std::nullopt,
        0x3961U,
        "KBMAT-MAT57: MaterialEditorState must create DeltaTime nodes from the palette");
    compilePaletteRuntimeInput(
        kb::editor::MaterialEditorGraphMenuCommand::CreatePixelDepth,
        "value",
        "alpha",
        "ctx.fragmentDepth",
        std::nullopt,
        0x3967U,
        "KBMAT-MAT57: MaterialEditorState must create PixelDepth nodes from the palette");
    compilePaletteFloat2Input(
        kb::editor::MaterialEditorGraphMenuCommand::CreatePixelPosition,
        "xy",
        "ctx.screenPosition * ctx.viewSize",
        0x396aU,
        "KBMAT-MAT57: MaterialEditorState must create PixelPosition nodes from the palette");
    compilePaletteRuntimeInput(
        kb::editor::MaterialEditorGraphMenuCommand::CreateCameraDepthFade,
        "value",
        "alpha",
        "distance(ctx.cameraPosition, ctx.worldPos)",
        std::nullopt,
        0x3968U,
        "KBMAT-MAT57: MaterialEditorState must create CameraDepthFade nodes from the palette");
    compilePaletteRuntimeInput(
        kb::editor::MaterialEditorGraphMenuCommand::CreateDynamicParameter,
        "rgba",
        "baseColor",
        "ctx.dynamicParameter",
        std::nullopt,
        0x3962U,
        "KBMAT-MAT57: MaterialEditorState must create DynamicParameter nodes from the palette");
    compilePaletteRuntimeInput(
        kb::editor::MaterialEditorGraphMenuCommand::CreatePerInstanceFadeAmount,
        "value",
        "alpha",
        "ctx.perInstanceFadeAmount",
        "perInstanceFadeAmount",
        0x3963U,
        "KBMAT-MAT57: MaterialEditorState must create PerInstanceFadeAmount nodes from the palette");
    compilePaletteRuntimeInput(
        kb::editor::MaterialEditorGraphMenuCommand::CreateDistanceCullFade,
        "value",
        "alpha",
        "ctx.perInstanceFadeAmount",
        "perInstanceFadeAmount",
        0x3969U,
        "KBMAT-MAT57: MaterialEditorState must create DistanceCullFade nodes from the palette");
    compilePaletteRuntimeInput(
        kb::editor::MaterialEditorGraphMenuCommand::CreatePerInstanceCustomData,
        "value",
        "alpha",
        "ctx.perInstanceCustomData",
        "perInstanceCustomData0",
        0x3964U,
        "KBMAT-MAT57: MaterialEditorState must create PerInstanceCustomData nodes from the palette");
    compilePaletteRuntimeInput(
        kb::editor::MaterialEditorGraphMenuCommand::CreatePreSkinnedPosition,
        "value",
        "emissive",
        "ctx.preSkinnedPosition",
        "preSkinnedPosition",
        0x3965U,
        "KBMAT-MAT57: MaterialEditorState must create PreSkinnedPosition nodes from the palette");
    compilePaletteRuntimeInput(
        kb::editor::MaterialEditorGraphMenuCommand::CreatePreSkinnedNormal,
        "value",
        "normal",
        "ctx.preSkinnedNormal",
        "preSkinnedNormal",
        0x3966U,
        "KBMAT-MAT57: MaterialEditorState must create PreSkinnedNormal nodes from the palette");

    const auto compilePaletteTextureSample = [](
        kb::editor::MaterialEditorGraphMenuCommand command,
        kb::render::RenderMaterialGraphTextureDimension expectedDimension,
        std::string_view stableIdPrefix,
        std::uint64_t assetId,
        const char* message) {
        kb::editor::MaterialEditorState editor;
        kb::render::RenderMaterialAssetData asset{};
        asset.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
        asset.graph.shadingModel = "unlit";
        editor.Open(kb::assets::AssetId{ assetId }, asset);
        const std::optional<kb::render::RenderMaterialGraphNodeKind> kind =
            kb::editor::MaterialEditorGraphMenuCommandNodeKind(command);
        std::uint32_t nodeId = 0U;
        kb::editor::tests::Require(kind.has_value() && editor.AddGraphNode(*kind, -160, 96, &nodeId), message);
        const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(editor.WorkingCopy()->graph, nodeId);
        kb::editor::tests::Require(node != nullptr && node->parameter.stableId.rfind(std::string{ stableIdPrefix }, 0U) == 0U,
            "KBMAT-MAT57: Palette-created advanced texture sample must carry a stable sampler id");
        kb::editor::tests::Require(editor.ConnectGraphPins(nodeId, "color", 1U, "baseColor"),
            "KBMAT-MAT57: Palette-created advanced texture sample must connect to Material Output baseColor");
        const kb::render::RenderMaterialGraphCompileResult compiled = kb::render::CompileRenderMaterialGraphToShaderSource(
            editor.WorkingCopy()->graph,
            kb::render::RenderMaterialGraphBuildContext{ .assetId = assetId });
        kb::editor::tests::Require(compiled.Succeeded(), message);
        kb::editor::tests::Require(compiled.shader.reflection.textures.size() == 1U,
            "KBMAT-MAT57: Palette-created advanced texture sample must reflect one sampler");
        kb::editor::tests::Require(compiled.shader.reflection.textures[0].dimension == expectedDimension,
            "KBMAT-MAT57: Palette-created advanced texture sample must reflect the expected texture dimension");
    };
    compilePaletteTextureSample(
        kb::editor::MaterialEditorGraphMenuCommand::CreateTextureSampleCube,
        kb::render::RenderMaterialGraphTextureDimension::TextureCube,
        "textureCubeSample",
        0x3951U,
        "KBMAT-MAT57: MaterialEditorState must create TextureSampleCube nodes from the palette");
    compilePaletteTextureSample(
        kb::editor::MaterialEditorGraphMenuCommand::CreateTextureSampleVolume,
        kb::render::RenderMaterialGraphTextureDimension::Texture3D,
        "textureVolumeSample",
        0x3952U,
        "KBMAT-MAT57: MaterialEditorState must create TextureSampleVolume nodes from the palette");
    compilePaletteTextureSample(
        kb::editor::MaterialEditorGraphMenuCommand::CreateTextureSample2DArray,
        kb::render::RenderMaterialGraphTextureDimension::Texture2DArray,
        "textureArraySample",
        0x3953U,
        "KBMAT-MAT57: MaterialEditorState must create TextureSample2DArray nodes from the palette");

    const auto compilePaletteTextureObject = [](
        kb::editor::MaterialEditorGraphMenuCommand objectCommand,
        kb::render::RenderMaterialGraphNodeKind sampleKind,
        kb::render::RenderMaterialGraphTextureDimension expectedDimension,
        std::string_view stableIdPrefix,
        std::uint64_t assetId,
        const char* message) {
        kb::editor::MaterialEditorState editor;
        kb::render::RenderMaterialAssetData asset{};
        asset.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
        asset.graph.shadingModel = "unlit";
        editor.Open(kb::assets::AssetId{ assetId }, asset);
        const std::optional<kb::render::RenderMaterialGraphNodeKind> objectKind =
            kb::editor::MaterialEditorGraphMenuCommandNodeKind(objectCommand);
        std::uint32_t objectNodeId = 0U;
        std::uint32_t sampleNodeId = 0U;
        kb::editor::tests::Require(
            objectKind.has_value() &&
                editor.AddGraphNode(*objectKind, -300, 96, &objectNodeId) &&
                editor.AddGraphNode(sampleKind, -40, 96, &sampleNodeId),
            message);
        const kb::render::RenderMaterialGraphNode* objectNode = kb::render::FindRenderMaterialGraphNode(editor.WorkingCopy()->graph, objectNodeId);
        kb::editor::tests::Require(objectNode != nullptr && objectNode->parameter.stableId.rfind(std::string{ stableIdPrefix }, 0U) == 0U,
            "KBMAT-MAT57: Palette-created advanced texture object must carry a stable sampler id");
        kb::editor::tests::Require(
            editor.ConnectGraphPins(objectNodeId, "texture", sampleNodeId, "texture") &&
                editor.ConnectGraphPins(sampleNodeId, "color", 1U, "baseColor"),
            "KBMAT-MAT57: Palette-created advanced texture object must connect to its matching sample node");
        const kb::render::RenderMaterialGraphCompileResult compiled = kb::render::CompileRenderMaterialGraphToShaderSource(
            editor.WorkingCopy()->graph,
            kb::render::RenderMaterialGraphBuildContext{ .assetId = assetId });
        kb::editor::tests::Require(compiled.Succeeded(), message);
        kb::editor::tests::Require(compiled.shader.reflection.textures.size() == 1U,
            "KBMAT-MAT57: Palette-created advanced texture object must reflect one sampler");
        kb::editor::tests::Require(compiled.shader.reflection.textures[0].dimension == expectedDimension,
            "KBMAT-MAT57: Palette-created advanced texture object must reflect the expected texture dimension");
    };
    compilePaletteTextureObject(
        kb::editor::MaterialEditorGraphMenuCommand::CreateTextureObjectCube,
        kb::render::RenderMaterialGraphNodeKind::TextureSampleCube,
        kb::render::RenderMaterialGraphTextureDimension::TextureCube,
        "textureCubeObject",
        0x3954U,
        "KBMAT-MAT57: MaterialEditorState must create TextureObjectCube nodes from the palette");
    compilePaletteTextureObject(
        kb::editor::MaterialEditorGraphMenuCommand::CreateTextureObjectVolume,
        kb::render::RenderMaterialGraphNodeKind::TextureSampleVolume,
        kb::render::RenderMaterialGraphTextureDimension::Texture3D,
        "textureVolumeObject",
        0x3955U,
        "KBMAT-MAT57: MaterialEditorState must create TextureObjectVolume nodes from the palette");
    compilePaletteTextureObject(
        kb::editor::MaterialEditorGraphMenuCommand::CreateTextureObject2DArray,
        kb::render::RenderMaterialGraphNodeKind::TextureSample2DArray,
        kb::render::RenderMaterialGraphTextureDimension::Texture2DArray,
        "textureArrayObject",
        0x3956U,
        "KBMAT-MAT57: MaterialEditorState must create TextureObject2DArray nodes from the palette");
}

void RunMaterialEditorCollectionParameterNodeModelTest() {
    kb::editor::tests::Require(
        kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateCollectionParameter, "collection parameter") &&
            kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateCollectionParameter, "mpc rgba"),
        "KBMAT-MAT50: Palette search must expose Collection Parameter by name and output pins");

    const std::vector<kb::editor::MaterialEditorGraphMenuCommand> inputCommands =
        kb::editor::MaterialEditorGraphContextMenuCommands(2U);
    const std::vector<kb::editor::MaterialEditorGraphMenuCommand> parameterCommands =
        kb::editor::MaterialEditorGraphContextMenuCommands(4U);
    kb::editor::tests::Require(
        kb::editor::MaterialEditorGraphCommandInList(inputCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateCollectionParameter) &&
            kb::editor::MaterialEditorGraphCommandInList(parameterCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateCollectionParameter),
        "KBMAT-MAT50: Context menu must include Collection Parameter in Inputs and Parameters categories");

    const std::optional<kb::render::RenderMaterialGraphNodeKind> collectionKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateCollectionParameter);
    kb::editor::tests::Require(collectionKind.has_value() && *collectionKind == kb::render::RenderMaterialGraphNodeKind::CollectionParameter,
        "KBMAT-MAT50: Collection Parameter command must map to the runtime graph node kind");

    kb::render::RenderMaterialAssetData material{};
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    kb::editor::MaterialEditorState editor;
    editor.Open(kb::assets::AssetId{ 0x5050U }, material);

    std::uint32_t collectionNodeId = 0U;
    kb::editor::tests::Require(editor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::CollectionParameter, -340, 72, &collectionNodeId),
        "KBMAT-MAT50: MaterialEditorState must create CollectionParameter nodes");
    const kb::render::RenderMaterialGraphNode* collectionNode =
        kb::render::FindRenderMaterialGraphNode(editor.WorkingCopy()->graph, collectionNodeId);
    kb::editor::tests::Require(collectionNode != nullptr &&
            collectionNode->parameter.stableId == "collectionParam" + std::to_string(collectionNodeId) &&
            collectionNode->parameter.displayName == "Collection Parameter " + std::to_string(collectionNodeId) &&
            collectionNode->parameter.defaultValueHint == "0" &&
            !collectionNode->parameter.overrideSupported,
        "KBMAT-MAT50: Created CollectionParameter must carry global-collection metadata defaults");

    const std::vector<std::string> outputPins = kb::render::RenderMaterialGraphNodeOutputPinNames(*collectionNode);
    kb::editor::tests::Require(outputPins.size() == 8U &&
            outputPins[0] == "value" &&
            outputPins[1] == "scalar" &&
            outputPins[2] == "xyz" &&
            outputPins[3] == "rgba" &&
            kb::render::RenderMaterialGraphPinDataType(*collectionNode, "scalar", true) == kb::render::RenderMaterialGraphPinType::Float &&
            kb::render::RenderMaterialGraphPinDataType(*collectionNode, "xyz", true) == kb::render::RenderMaterialGraphPinType::Float3 &&
            kb::render::RenderMaterialGraphPinDataType(*collectionNode, "rgba", true) == kb::render::RenderMaterialGraphPinType::Color,
        "KBMAT-MAT50: CollectionParameter editor node must expose typed scalar/vector/color output pins");
    const SIZE nodeSize = kb::editor::MaterialEditorPanelGraphNodeSize(kb::render::RenderMaterialGraphNodeKind::CollectionParameter);
    kb::editor::tests::Require(nodeSize.cx >= 196 && nodeSize.cy >= 112,
        "KBMAT-MAT50: CollectionParameter UI node must reserve enough space for its output pins");

    constexpr std::uint64_t collectionAssetId = 0x50500001ULL;
    kb::editor::tests::Require(!editor.SetGraphNodeTextProperty(collectionNodeId, "collection.assetId", "not-a-number"),
        "KBMAT-MAT50: CollectionParameter editor property must reject non-numeric collection asset ids");
    kb::editor::tests::Require(editor.SetGraphNodeTextProperty(collectionNodeId, "collection.assetId", std::to_string(collectionAssetId)),
        "KBMAT-MAT50: CollectionParameter editor property must set a real collection asset id");
    kb::editor::tests::Require(!editor.SetGraphNodeTextProperty(collectionNodeId, "collection.parameter", "Global Tint"),
        "KBMAT-MAT50: CollectionParameter editor property must reject non-stable parameter ids");
    kb::editor::tests::Require(editor.SetGraphNodeTextProperty(collectionNodeId, "collection.parameter", "GlobalTint"),
        "KBMAT-MAT50: CollectionParameter editor property must set the collection parameter stable id");
    const std::vector<kb::editor::MaterialEditorGraphNodeProperty> collectionProperties = editor.GraphNodeProperties(collectionNodeId);
    kb::editor::tests::Require(std::ranges::any_of(collectionProperties, [collectionAssetId](const kb::editor::MaterialEditorGraphNodeProperty& property) {
            return property.stableId == "collection.assetId" &&
                property.kind == kb::editor::MaterialEditorGraphNodePropertyKind::Text &&
                property.value.text == std::to_string(collectionAssetId);
        }) &&
            std::ranges::any_of(collectionProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
                return property.stableId == "collection.parameter" &&
                    property.kind == kb::editor::MaterialEditorGraphNodePropertyKind::Text &&
                    property.value.text == "GlobalTint";
            }),
        "KBMAT-MAT50: Details panel model must expose editable CollectionParameter asset and stable-id fields");
    kb::editor::tests::Require(editor.ConnectGraphPins(collectionNodeId, "rgba", 1U, "baseColor"),
        "KBMAT-MAT50: Configured CollectionParameter output must connect to Material Output baseColor");

    std::ostringstream serializedConfigured;
    kb::render::RenderMaterialAssetWriter::Write(serializedConfigured, *editor.WorkingCopy());
    std::istringstream configuredInput{ serializedConfigured.str() };
    const kb::render::RenderMaterialAssetParseResult parsedConfigured =
        kb::render::RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(configuredInput);
    kb::editor::tests::Require(parsedConfigured.Succeeded() && parsedConfigured.asset.has_value(),
        "KBMAT-MAT50: Editor-configured CollectionParameter material must serialize and load");
    const kb::render::RenderMaterialGraphCompileResult collectionCompile =
        kb::render::CompileRenderMaterialGraphToShaderSource(
            parsedConfigured.asset->graph,
            kb::render::RenderMaterialGraphBuildContext{ .assetId = 0x50500002ULL });
    if (!collectionCompile.Succeeded()) {
        std::cerr << "KBMAT-MAT50 editor-configured CollectionParameter failed shader compile";
        if (!collectionCompile.diagnostics.empty()) {
            std::cerr << " diagnostic=" << collectionCompile.diagnostics.front().message;
        }
        std::cerr << '\n';
    }
    kb::editor::tests::Require(collectionCompile.Succeeded() &&
            collectionCompile.shader.reflection.uniforms.size() == 1U &&
            collectionCompile.shader.reflection.uniforms[0].source == kb::render::RenderMaterialGraphReflectionUniformSource::ParameterCollection &&
            collectionCompile.shader.reflection.uniforms[0].collectionAssetId == collectionAssetId &&
            collectionCompile.shader.reflection.uniforms[0].collectionParameterStableId == "GlobalTint",
        "KBMAT-MAT50: Editor-configured CollectionParameter material must compile with MPC reflection metadata");

    kb::render::RenderMaterialParameterCollectionData collection{};
    collection.displayName = "Editor Scene Globals";
    collection.parameters.push_back(kb::render::RenderMaterialParameterCollectionParameter{
        .stableId = "GlobalTint",
        .displayName = "Global Tint",
        .type = kb::render::RenderMaterialParameterCollectionValueType::Vector,
        .defaultValue = { 0.35F, 0.45F, 0.55F, 1.0F },
        .editorOrder = 0U,
        .description = "Editor-configured scene tint",
    });
    kb::render::RenderMaterialParameterCollectionRuntimeStore& store =
        kb::render::GlobalRenderMaterialParameterCollectionStore();
    store.Clear();
    kb::editor::tests::Require(store.LoadDefaults(collectionAssetId, collection),
        "KBMAT-MAT50: Runtime MPC store must load defaults for an editor-configured CollectionParameter graph");
    const std::array<kb::render::RenderMaterialGraphParameterValue, 0U> noMaterialOverrides{};
    const kb::render::RenderMaterialGraphProgramBindingResult collectionBinding =
        kb::render::BuildRenderMaterialGraphProgramBinding(0x50500003ULL, 3U, collectionCompile.shader, noMaterialOverrides);
    kb::editor::tests::Require(collectionBinding.binding.active &&
            collectionBinding.binding.uniforms.size() == 1U &&
            collectionBinding.binding.uniforms[0].source == kb::render::RenderMaterialGraphUniformBindingSource::ParameterCollection &&
            collectionBinding.binding.uniforms[0].collectionAssetId == collectionAssetId &&
            collectionBinding.binding.uniforms[0].collectionParameterStableId == "GlobalTint" &&
            collectionBinding.binding.uniforms[0].value[0] == 0.35F &&
            collectionBinding.binding.uniforms[0].value[2] == 0.55F,
        "KBMAT-MAT50: Editor-configured CollectionParameter material must bind runtime MPC uniform values");
    store.Clear();
}

void RunMaterialEditorFunctionNodeModelTest() {
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateMaterialFunctionCall, "function"),
        "KBMAT-MAT42: Palette search should expose Material Function Call");
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateFunctionInput, "function input"),
        "KBMAT-MAT42: Palette search should expose Function Input");
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateFunctionOutput, "function output"),
        "KBMAT-MAT42: Palette search should expose Function Output");

    const std::vector<kb::editor::MaterialEditorGraphMenuCommand> materialCommands =
        kb::editor::MaterialEditorGraphContextMenuCommands(7U);
    kb::editor::tests::Require(
        kb::editor::MaterialEditorGraphCommandInList(materialCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateFunctionInput) &&
            kb::editor::MaterialEditorGraphCommandInList(materialCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateFunctionOutput) &&
            kb::editor::MaterialEditorGraphCommandInList(materialCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateMaterialFunctionCall),
        "KBMAT-MAT42: Material graph palette category must include function endpoint and call nodes");
    const std::optional<kb::render::RenderMaterialGraphNodeKind> inputKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateFunctionInput);
    const std::optional<kb::render::RenderMaterialGraphNodeKind> outputKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateFunctionOutput);
    const std::optional<kb::render::RenderMaterialGraphNodeKind> callKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateMaterialFunctionCall);
    kb::editor::tests::Require(
        inputKind.has_value() && *inputKind == kb::render::RenderMaterialGraphNodeKind::FunctionInput &&
            outputKind.has_value() && *outputKind == kb::render::RenderMaterialGraphNodeKind::FunctionOutput &&
            callKind.has_value() && *callKind == kb::render::RenderMaterialGraphNodeKind::MaterialFunctionCall,
        "KBMAT-MAT42: Function palette commands must map to their runtime graph node kinds");

    kb::render::RenderMaterialAssetData material{};
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    kb::editor::MaterialEditorState editor;
    editor.Open(kb::assets::AssetId{ 0x4208U }, material);

    std::uint32_t functionInputId = 0U;
    std::uint32_t functionOutputId = 0U;
    std::uint32_t functionCallId = 0U;
    std::uint32_t colorId = 0U;
    kb::editor::tests::Require(editor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::FunctionInput, -420, -80, &functionInputId),
        "KBMAT-MAT42: MaterialEditorState must create FunctionInput nodes");
    kb::editor::tests::Require(editor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::FunctionOutput, -160, -80, &functionOutputId),
        "KBMAT-MAT42: MaterialEditorState must create FunctionOutput nodes");
    kb::editor::tests::Require(editor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::MaterialFunctionCall, -40, 120, &functionCallId),
        "KBMAT-MAT42: MaterialEditorState must create MaterialFunctionCall nodes");
    kb::editor::tests::Require(editor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantColor, -300, 120, &colorId),
        "KBMAT-MAT42: MaterialEditorState fixture must create a color node for dynamic-pin wiring");

    const kb::render::RenderMaterialGraphNode* functionInput =
        kb::render::FindRenderMaterialGraphNode(editor.WorkingCopy()->graph, functionInputId);
    const kb::render::RenderMaterialGraphNode* functionOutput =
        kb::render::FindRenderMaterialGraphNode(editor.WorkingCopy()->graph, functionOutputId);
    const kb::render::RenderMaterialGraphNode* functionCall =
        kb::render::FindRenderMaterialGraphNode(editor.WorkingCopy()->graph, functionCallId);
    kb::editor::tests::Require(functionInput != nullptr && functionInput->parameter.stableId == "Input" &&
            functionInput->parameter.defaultValueHint == "float4" &&
            kb::render::IsRenderMaterialGraphOutputPin(*functionInput, "value"),
        "KBMAT-MAT42: Created FunctionInput must carry endpoint metadata and an output value pin");
    kb::editor::tests::Require(functionOutput != nullptr && functionOutput->parameter.stableId == "Output" &&
            functionOutput->parameter.defaultValueHint == "float4" &&
            kb::render::IsRenderMaterialGraphInputPin(*functionOutput, "value"),
        "KBMAT-MAT42: Created FunctionOutput must carry endpoint metadata and an input value pin");
    kb::editor::tests::Require(functionCall != nullptr &&
            kb::render::RenderMaterialGraphNodeInputPinNames(*functionCall).size() == 1U &&
            kb::render::RenderMaterialGraphNodeInputPinNames(*functionCall)[0] == "Input" &&
            kb::render::RenderMaterialGraphNodeOutputPinNames(*functionCall).size() == 1U &&
            kb::render::RenderMaterialGraphNodeOutputPinNames(*functionCall)[0] == "Output" &&
            kb::render::RenderMaterialGraphPinDataType(*functionCall, "Input", false) == kb::render::RenderMaterialGraphPinType::Float4 &&
            kb::render::RenderMaterialGraphPinDataType(*functionCall, "Output", true) == kb::render::RenderMaterialGraphPinType::Float4,
        "KBMAT-MAT42: Created MaterialFunctionCall must carry editable dynamic pins");

    kb::editor::tests::Require(editor.ConnectGraphPins(colorId, "rgba", functionCallId, "Input"),
        "KBMAT-MAT42: MaterialFunctionCall dynamic input pin must accept a compatible graph connection");
    kb::editor::tests::Require(editor.ConnectGraphPins(functionCallId, "Output", 1U, "baseColor"),
        "KBMAT-MAT42: MaterialFunctionCall dynamic output pin must connect to material output");
    kb::editor::tests::Require(editor.WorkingCopy()->graph.links.size() == 2U &&
            editor.WorkingCopy()->graph.links[0].toPin == "Input" &&
            editor.WorkingCopy()->graph.links[1].fromPin == "Output",
        "KBMAT-MAT42: Function call dynamic links must be persisted in the working graph");

    std::ostringstream serialized;
    kb::render::RenderMaterialAssetWriter::Write(serialized, *editor.WorkingCopy());
    const std::string expectedCustomPins =
        "graphCustomCode " + std::to_string(functionCallId) + " float4 Input:float4 Output:float4";
    kb::editor::tests::Require(serialized.str().find(expectedCustomPins) != std::string::npos,
        "KBMAT-MAT42: MaterialFunctionCall dynamic pin schema must serialize with the graph");

    constexpr std::uint64_t functionAssetId = 0x42080001ULL;
    kb::editor::tests::Require(!editor.SetGraphNodeTextProperty(functionCallId, "function.assetId", "TintFunction"),
        "KBMAT-MAT42: MaterialFunctionCall editor property must reject non-numeric function asset ids");
    kb::editor::tests::Require(editor.SetGraphNodeTextProperty(functionCallId, "function.assetId", std::to_string(functionAssetId)),
        "KBMAT-MAT42: MaterialFunctionCall editor property must set the function asset id used by runtime inlining");
    const std::vector<kb::editor::MaterialEditorGraphNodeProperty> functionProperties = editor.GraphNodeProperties(functionCallId);
    kb::editor::tests::Require(std::ranges::any_of(functionProperties, [functionAssetId](const kb::editor::MaterialEditorGraphNodeProperty& property) {
            return property.stableId == "function.assetId" &&
                property.kind == kb::editor::MaterialEditorGraphNodePropertyKind::Text &&
                property.value.text == std::to_string(functionAssetId);
        }),
        "KBMAT-MAT42: Details panel model must expose editable MaterialFunctionCall asset id");

    kb::render::RenderMaterialGraphDocument functionGraph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    functionGraph.storageModel = "material-function-asset";
    functionGraph.shadingModel = "unlit";
    functionGraph.nodes.clear();
    const kb::render::RenderMaterialGraphNode passthroughInput{
        .id = 1U,
        .kind = kb::render::RenderMaterialGraphNodeKind::FunctionInput,
        .positionX = -220,
        .positionY = 80,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "Tint",
            .displayName = "Tint",
            .defaultValueHint = "float4",
            .overrideSupported = false,
        },
    };
    const kb::render::RenderMaterialGraphNode passthroughOutput{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::FunctionOutput,
        .positionX = 80,
        .positionY = 80,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "Result",
            .displayName = "Result",
            .defaultValueHint = "float4",
            .overrideSupported = false,
        },
    };
    functionGraph.nodes.push_back(passthroughInput);
    functionGraph.nodes.push_back(passthroughOutput);
    functionGraph.links.push_back(MakeMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::FunctionInput,
        1U,
        "value",
        kb::render::RenderMaterialGraphNodeKind::FunctionOutput,
        2U,
        "value"));
    kb::editor::tests::Require(editor.SetGraphMaterialFunctionCallSignature(functionCallId, functionAssetId, functionGraph),
        "KBMAT-MAT42: MaterialFunctionCall editor sync must rebuild call pins from the selected function graph");
    const kb::render::RenderMaterialGraphNode* syncedFunctionCall =
        kb::render::FindRenderMaterialGraphNode(editor.WorkingCopy()->graph, functionCallId);
    kb::editor::tests::Require(syncedFunctionCall != nullptr &&
            kb::render::RenderMaterialGraphNodeInputPinNames(*syncedFunctionCall).size() == 1U &&
            kb::render::RenderMaterialGraphNodeInputPinNames(*syncedFunctionCall)[0] == "Tint" &&
            kb::render::RenderMaterialGraphNodeOutputPinNames(*syncedFunctionCall).size() == 1U &&
            kb::render::RenderMaterialGraphNodeOutputPinNames(*syncedFunctionCall)[0] == "Result" &&
            kb::render::RenderMaterialGraphPinDataType(*syncedFunctionCall, "Tint", false) == kb::render::RenderMaterialGraphPinType::Float4 &&
            kb::render::RenderMaterialGraphPinDataType(*syncedFunctionCall, "Result", true) == kb::render::RenderMaterialGraphPinType::Float4,
        "KBMAT-MAT42: MaterialFunctionCall editor sync must expose the function endpoint names and types");
    kb::editor::tests::Require(editor.WorkingCopy()->graph.links.empty(),
        "KBMAT-MAT42: MaterialFunctionCall editor sync must remove links to stale dynamic pins");
    kb::editor::tests::Require(editor.ConnectGraphPins(colorId, "rgba", functionCallId, "Tint"),
        "KBMAT-MAT42: Synced MaterialFunctionCall input pin must accept compatible graph connections");
    kb::editor::tests::Require(editor.ConnectGraphPins(functionCallId, "Result", 1U, "baseColor"),
        "KBMAT-MAT42: Synced MaterialFunctionCall output pin must connect to material output");
    kb::render::RenderMaterialGraphFunctionLibrary library{};
    library.entries.push_back(kb::render::RenderMaterialGraphFunctionLibraryEntry{
        .assetId = functionAssetId,
        .contentHash = 0x42080002ULL,
        .name = "/Game/Functions/EditorPassthrough.kbmatfn",
        .graph = functionGraph,
    });
    std::ostringstream configuredFunctionSerialized;
    kb::render::RenderMaterialAssetWriter::Write(configuredFunctionSerialized, *editor.WorkingCopy());
    std::istringstream configuredFunctionInput{ configuredFunctionSerialized.str() };
    const kb::render::RenderMaterialAssetParseResult parsedFunctionMaterial =
        kb::render::RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(configuredFunctionInput);
    kb::editor::tests::Require(parsedFunctionMaterial.Succeeded() && parsedFunctionMaterial.asset.has_value(),
        "KBMAT-MAT42: Editor-configured MaterialFunctionCall graph must serialize and load");
    const kb::render::RenderMaterialGraphCompileResult functionCompile =
        kb::render::CompileRenderMaterialGraphToShaderSource(
            parsedFunctionMaterial.asset->graph,
            kb::render::RenderMaterialGraphBuildContext{ .assetId = 0x42080003ULL, .functionLibrary = &library });
    kb::editor::tests::Require(functionCompile.Succeeded() &&
            functionCompile.shader.source.find("MaterialFunctionCall") == std::string::npos &&
            functionCompile.shader.sourceHash != 0U,
        "KBMAT-MAT42: Editor-configured MaterialFunctionCall graph must inline and compile to shader source");
}

void RunMaterialEditorLayerStackNodeModelTest() {
    kb::editor::tests::Require(kb::editor::MaterialEditorGraphPaletteCommandMatches(kb::editor::MaterialEditorGraphMenuCommand::CreateLayerStack, "layer stack"),
        "KBMAT-MAT43: Palette search should expose Layer Stack");
    const std::vector<kb::editor::MaterialEditorGraphMenuCommand> materialCommands =
        kb::editor::MaterialEditorGraphContextMenuCommands(7U);
    kb::editor::tests::Require(
        kb::editor::MaterialEditorGraphCommandInList(materialCommands, kb::editor::MaterialEditorGraphMenuCommand::CreateLayerStack),
        "KBMAT-MAT43: Material graph palette category must include Layer Stack");

    const std::optional<kb::render::RenderMaterialGraphNodeKind> stackKind =
        kb::editor::MaterialEditorGraphMenuCommandNodeKind(kb::editor::MaterialEditorGraphMenuCommand::CreateLayerStack);
    kb::editor::tests::Require(stackKind.has_value() && *stackKind == kb::render::RenderMaterialGraphNodeKind::LayerStack,
        "KBMAT-MAT43: Layer Stack palette command must map to the runtime LayerStack node kind");

    kb::render::RenderMaterialAssetData material{};
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    kb::editor::MaterialEditorState editor;
    editor.Open(kb::assets::AssetId{ 0x4308U }, material);

    std::uint32_t stackNodeId = 0U;
    kb::editor::tests::Require(editor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::LayerStack, -240, 80, &stackNodeId),
        "KBMAT-MAT43: MaterialEditorState must create LayerStack nodes");
    const kb::render::RenderMaterialGraphNode* stack =
        kb::render::FindRenderMaterialGraphNode(editor.WorkingCopy()->graph, stackNodeId);
    kb::editor::tests::Require(stack != nullptr &&
            stack->parameter.stableId == "surfaceLayers" &&
            stack->parameter.displayName == "Layer Stack" &&
            kb::render::IsRenderMaterialGraphOutputPin(*stack, "attributes") &&
            kb::render::RenderMaterialGraphPinDataType(*stack, "attributes", true) == kb::render::RenderMaterialGraphPinType::MaterialAttributes,
        "KBMAT-MAT43: Created LayerStack must carry editor metadata and a MaterialAttributes output");

    kb::render::RenderMaterialGraphLayerStackEntry baseLayer{
        .layerFunctionAssetId = 0x43000001ULL,
        .enabled = true,
        .layerName = "Base",
        .linkState = "base-linked",
        .layerParameters = {
            kb::render::RenderMaterialGraphLayerStackParameter{ .pinName = "Tint", .type = kb::render::RenderMaterialGraphPinType::Color, .valueHint = "1 0 0 1" },
        },
    };
    kb::render::RenderMaterialGraphLayerStackEntry coatLayer{
        .layerFunctionAssetId = 0x43000002ULL,
        .blendFunctionAssetId = 0x43000003ULL,
        .enabled = true,
        .layerName = "Coat",
        .blendName = "Half",
        .linkState = "coat-linked",
        .layerParameters = {
            kb::render::RenderMaterialGraphLayerStackParameter{ .pinName = "Tint", .type = kb::render::RenderMaterialGraphPinType::Color, .valueHint = "0 0 1 1" },
        },
        .blendParameters = {
            kb::render::RenderMaterialGraphLayerStackParameter{ .pinName = "Factor", .type = kb::render::RenderMaterialGraphPinType::Float, .valueHint = "0.5" },
        },
    };
    kb::editor::tests::Require(editor.AddLayerStackEntry(stackNodeId, baseLayer),
        "KBMAT-MAT60: Layer tree model must add a base layer entry");
    kb::editor::tests::Require(editor.AddLayerStackEntry(stackNodeId, coatLayer),
        "KBMAT-MAT60: Layer tree model must add a blended layer entry");

    std::vector<kb::editor::MaterialEditorLayerTreeRow> layerRows = editor.LayerTreeRows();
    kb::editor::tests::Require(layerRows.size() == 2U &&
            layerRows[0].nodeId == stackNodeId &&
            layerRows[0].index == 0U &&
            layerRows[0].layerFunctionAssetId == 0x43000001ULL &&
            layerRows[0].blendFunctionAssetId == 0ULL &&
            layerRows[0].layerParameterCount == 1U &&
            layerRows[1].index == 1U &&
            layerRows[1].layerFunctionAssetId == 0x43000002ULL &&
            layerRows[1].blendFunctionAssetId == 0x43000003ULL &&
            layerRows[1].blendParameterCount == 1U &&
            layerRows[1].linkState == "coat-linked",
        "KBMAT-MAT60: Layer tree rows must expose stack order, function selection, blend selection, parameters, and link state");

    kb::render::RenderMaterialGraphLayerStackEntry mutedCoat = coatLayer;
    mutedCoat.enabled = false;
    mutedCoat.layerName = "Coat Muted";
    mutedCoat.linkState = "coat-muted";
    kb::editor::tests::Require(editor.SetLayerStackEntry(stackNodeId, 1U, mutedCoat),
        "KBMAT-MAT60: Layer tree model must edit an existing layer entry");
    layerRows = editor.LayerTreeRows();
    kb::editor::tests::Require(layerRows.size() == 2U && !layerRows[1].enabled &&
            layerRows[1].layerName == "Coat Muted" &&
            layerRows[1].linkState == "coat-muted",
        "KBMAT-MAT60: Layer tree rows must update after changing enabled/name/link state");
    kb::editor::tests::Require(editor.RemoveLayerStackEntry(stackNodeId, 1U),
        "KBMAT-MAT60: Layer tree model must remove a layer entry");
    layerRows = editor.LayerTreeRows();
    kb::editor::tests::Require(layerRows.size() == 1U && layerRows[0].layerName == "Base",
        "KBMAT-MAT60: Layer tree rows must shrink after removing a layer");
    kb::editor::tests::Require(editor.AddLayerStackEntry(stackNodeId, coatLayer),
        "KBMAT-MAT60: Layer tree model must re-add a layer entry after removal");

    kb::editor::MaterialEditorPanelDetailsRows layerDetails =
        kb::editor::MaterialEditorPanelRenderer::DetailsRows(editor.Parameters(), editor.SelectedNodeId(), {});
    layerDetails.layerTreeRows = editor.LayerTreeRows();
    kb::editor::tests::Require(layerDetails.layerTreeRows.size() == 2U && layerDetails.layerTreeRows[1].blendName == "Half",
        "KBMAT-MAT60: Details panel model must consume layer tree rows");
    kb::editor::tests::Require(editor.ConnectGraphPins(stackNodeId, "attributes", 1U, "attributes"),
        "KBMAT-MAT43: Editor-created LayerStack attributes output must connect to Material Output");

    std::ostringstream serialized;
    kb::render::RenderMaterialAssetWriter::Write(serialized, *editor.WorkingCopy());
    const std::string text = serialized.str();
    kb::editor::tests::Require(text.find("graphLayerStackEntry " + std::to_string(stackNodeId) + " 1 1124073474 1124073475 true Coat Half coat-linked") != std::string::npos &&
            text.find("graphLayerStackParameter " + std::to_string(stackNodeId) + " 1 layer Tint color 0%200%201%201") != std::string::npos &&
            text.find("graphLayerStackParameter " + std::to_string(stackNodeId) + " 1 blend Factor float 0.5") != std::string::npos,
        "KBMAT-MAT43: LayerStack entries and parameters must serialize from the editor working copy");

    const auto makeLayerFunction = [] {
        kb::render::RenderMaterialGraphDocument graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
        graph.storageModel = "material-function-asset";
        graph.shadingModel = "unlit";
        graph.nodes.clear();
        const kb::render::RenderMaterialGraphNode tint{
            .id = 1U,
            .kind = kb::render::RenderMaterialGraphNodeKind::FunctionInput,
            .parameter = kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "Tint",
                .displayName = "Tint",
                .defaultValueHint = "color",
            },
        };
        const kb::render::RenderMaterialGraphNode makeAttributes{
            .id = 2U,
            .kind = kb::render::RenderMaterialGraphNodeKind::MakeMaterialAttributes,
        };
        const kb::render::RenderMaterialGraphNode output{
            .id = 3U,
            .kind = kb::render::RenderMaterialGraphNodeKind::FunctionOutput,
            .parameter = kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "Attributes",
                .displayName = "Attributes",
                .defaultValueHint = "materialAttributes",
            },
        };
        graph.nodes.push_back(tint);
        graph.nodes.push_back(makeAttributes);
        graph.nodes.push_back(output);
        graph.links.push_back(MakeMaterialGraphLink(
            kb::render::RenderMaterialGraphNodeKind::FunctionInput,
            1U,
            "value",
            kb::render::RenderMaterialGraphNodeKind::MakeMaterialAttributes,
            2U,
            "baseColor"));
        graph.links.push_back(MakeMaterialGraphLink(
            kb::render::RenderMaterialGraphNodeKind::MakeMaterialAttributes,
            2U,
            "attributes",
            kb::render::RenderMaterialGraphNodeKind::FunctionOutput,
            3U,
            "value"));
        return graph;
    };
    const auto makeBlendFunction = [] {
        kb::render::RenderMaterialGraphDocument graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
        graph.storageModel = "material-function-asset";
        graph.shadingModel = "unlit";
        graph.nodes.clear();
        const kb::render::RenderMaterialGraphNode a{
            .id = 1U,
            .kind = kb::render::RenderMaterialGraphNodeKind::FunctionInput,
            .parameter = kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "A",
                .displayName = "A",
                .defaultValueHint = "materialAttributes",
            },
        };
        const kb::render::RenderMaterialGraphNode b{
            .id = 2U,
            .kind = kb::render::RenderMaterialGraphNodeKind::FunctionInput,
            .parameter = kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "B",
                .displayName = "B",
                .defaultValueHint = "materialAttributes",
            },
        };
        const kb::render::RenderMaterialGraphNode factor{
            .id = 3U,
            .kind = kb::render::RenderMaterialGraphNodeKind::FunctionInput,
            .parameter = kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "Factor",
                .displayName = "Factor",
                .defaultValueHint = "float",
            },
        };
        const kb::render::RenderMaterialGraphNode blend{
            .id = 4U,
            .kind = kb::render::RenderMaterialGraphNodeKind::BlendMaterialAttributes,
        };
        const kb::render::RenderMaterialGraphNode output{
            .id = 5U,
            .kind = kb::render::RenderMaterialGraphNodeKind::FunctionOutput,
            .parameter = kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "Attributes",
                .displayName = "Attributes",
                .defaultValueHint = "materialAttributes",
            },
        };
        graph.nodes.push_back(a);
        graph.nodes.push_back(b);
        graph.nodes.push_back(factor);
        graph.nodes.push_back(blend);
        graph.nodes.push_back(output);
        graph.links.push_back(MakeMaterialGraphLink(
            kb::render::RenderMaterialGraphNodeKind::FunctionInput,
            1U,
            "value",
            kb::render::RenderMaterialGraphNodeKind::BlendMaterialAttributes,
            4U,
            "a"));
        graph.links.push_back(MakeMaterialGraphLink(
            kb::render::RenderMaterialGraphNodeKind::FunctionInput,
            2U,
            "value",
            kb::render::RenderMaterialGraphNodeKind::BlendMaterialAttributes,
            4U,
            "b"));
        graph.links.push_back(MakeMaterialGraphLink(
            kb::render::RenderMaterialGraphNodeKind::FunctionInput,
            3U,
            "value",
            kb::render::RenderMaterialGraphNodeKind::BlendMaterialAttributes,
            4U,
            "factor"));
        graph.links.push_back(MakeMaterialGraphLink(
            kb::render::RenderMaterialGraphNodeKind::BlendMaterialAttributes,
            4U,
            "attributes",
            kb::render::RenderMaterialGraphNodeKind::FunctionOutput,
            5U,
            "value"));
        return graph;
    };

    kb::render::RenderMaterialGraphFunctionLibrary library{};
    library.entries.push_back(kb::render::RenderMaterialGraphFunctionLibraryEntry{
        .assetId = 0x43000001ULL,
        .contentHash = 0x43081001ULL,
        .name = "/Game/Layers/EditorBase.kbmatfn",
        .graph = makeLayerFunction(),
    });
    library.entries.push_back(kb::render::RenderMaterialGraphFunctionLibraryEntry{
        .assetId = 0x43000002ULL,
        .contentHash = 0x43081002ULL,
        .name = "/Game/Layers/EditorCoat.kbmatfn",
        .graph = makeLayerFunction(),
    });
    library.entries.push_back(kb::render::RenderMaterialGraphFunctionLibraryEntry{
        .assetId = 0x43000003ULL,
        .contentHash = 0x43081003ULL,
        .name = "/Game/Layers/EditorHalfBlend.kbmatfn",
        .graph = makeBlendFunction(),
    });
    std::istringstream serializedInput{ serialized.str() };
    const kb::render::RenderMaterialAssetParseResult parsedLayerMaterial =
        kb::render::RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(serializedInput);
    kb::editor::tests::Require(parsedLayerMaterial.Succeeded() && parsedLayerMaterial.asset.has_value(),
        "KBMAT-MAT43: Editor-configured LayerStack material must serialize and load before shader compile");
    const kb::render::RenderMaterialGraphCompileResult layerCompile =
        kb::render::CompileRenderMaterialGraphToShaderSource(
            parsedLayerMaterial.asset->graph,
            kb::render::RenderMaterialGraphBuildContext{ .assetId = 0x43080001ULL, .functionLibrary = &library });
    kb::editor::tests::Require(layerCompile.Succeeded() &&
            layerCompile.shader.source.find("mix(") != std::string::npos &&
            layerCompile.shader.source.find("LayerStack") == std::string::npos,
        "KBMAT-MAT43: Editor-configured LayerStack material must inline layer/blend functions and compile to shader source");
}

void RunMaterialEditorTypedNodePropertyModelTest() {
    kb::render::RenderMaterialAssetData material{};
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    constexpr std::uint32_t scalarNodeId = 40U;
    constexpr std::uint32_t textureNodeId = 41U;
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = scalarNodeId,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = -320,
        .positionY = 64,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Range Scalar",
            .defaultValueHint = "0",
            .hasRange = true,
            .rangeMin = -2.0F,
            .rangeMax = 2.0F,
            .overrideSupported = false,
        },
    });
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = textureNodeId,
        .kind = kb::render::RenderMaterialGraphNodeKind::TextureSample,
        .positionX = -80,
        .positionY = 64,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "textureSample41",
            .displayName = "Albedo Texture",
            .textureRole = "baseColor",
            .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
            .overrideSupported = true,
        },
    });
    material.graphParameterValues.push_back(kb::render::RenderMaterialGraphParameterValue{
        .stableId = "textureSample41",
        .type = kb::render::RenderMaterialParameterType::Texture,
        .assetId = 0x5800U,
    });

    kb::editor::MaterialEditorState materialEditor;
    materialEditor.Open(kb::assets::AssetId{ 0x4000U }, material);

    std::uint32_t colorNodeId = 0U;
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantColor, -560, 64, &colorNodeId),
        "KBMAT-MAT58: Material Editor should create a color constant for typed property editing");
    std::vector<kb::editor::MaterialEditorGraphNodeProperty> colorProperties = materialEditor.GraphNodeProperties(colorNodeId);
    const auto colorProperty = std::ranges::find_if(colorProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.kind == kb::editor::MaterialEditorGraphNodePropertyKind::Color;
    });
    kb::editor::tests::Require(colorProperty != colorProperties.end() && colorProperty->range.has_value(),
        "KBMAT-MAT58: Color constants should expose a typed color property with metadata range");
    kb::editor::tests::Require(materialEditor.SetGraphConstantColorValue(colorNodeId, std::array<float, 4U>{ 1.4F, -0.25F, 0.5F, 2.0F }),
        "KBMAT-MAT58: Color picker result should update the selected color node");
    colorProperties = materialEditor.GraphNodeProperties(colorNodeId);
    const auto updatedColor = std::ranges::find_if(colorProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.kind == kb::editor::MaterialEditorGraphNodePropertyKind::Color;
    });
    kb::editor::tests::Require(updatedColor != colorProperties.end() &&
            updatedColor->value.numbers[0] == 1.0F &&
            updatedColor->value.numbers[1] == 0.0F &&
            updatedColor->value.numbers[2] == 0.5F &&
            updatedColor->value.numbers[3] == 1.0F,
        "KBMAT-MAT58: Color picker values should be clamped through node metadata");

    std::vector<kb::editor::MaterialEditorGraphNodeProperty> scalarProperties = materialEditor.GraphNodeProperties(scalarNodeId);
    const auto scalarProperty = std::ranges::find_if(scalarProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.kind == kb::editor::MaterialEditorGraphNodePropertyKind::Numeric;
    });
    kb::editor::tests::Require(scalarProperty != scalarProperties.end() && scalarProperty->range.has_value() &&
            scalarProperty->range->min == -2.0F && scalarProperty->range->max == 2.0F,
        "KBMAT-MAT58: Slider properties should read their min/max from graph metadata");
    kb::editor::tests::Require(materialEditor.SetGraphConstantComponentValue(scalarNodeId, 0U, 7.5F),
        "KBMAT-MAT58: Slider setter should accept a numeric edit request");
    const std::optional<float> clampedScalar = materialEditor.GraphConstantComponentValue(scalarNodeId, 0U);
    kb::editor::tests::Require(clampedScalar.has_value() && *clampedScalar == 2.0F,
        "KBMAT-MAT58: Slider edits should clamp to the metadata max");

    std::uint32_t boolNodeId = 0U;
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantBool, -420, 188, &boolNodeId),
        "KBMAT-MAT58: Material Editor should create a bool constant for typed property editing");
    std::vector<kb::editor::MaterialEditorGraphNodeProperty> boolProperties = materialEditor.GraphNodeProperties(boolNodeId);
    auto boolProperty = std::ranges::find_if(boolProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "constant.bool";
    });
    kb::editor::tests::Require(boolProperty != boolProperties.end() &&
            boolProperty->kind == kb::editor::MaterialEditorGraphNodePropertyKind::Enum &&
            boolProperty->type == kb::render::RenderMaterialParameterType::Bool &&
            boolProperty->value.text == "false" &&
            boolProperty->options.size() == 2U &&
            boolProperty->options[0].value == "false" &&
            boolProperty->options[1].value == "true",
        "KBMAT-MAT58: ConstantBool should expose a typed False/True property model");
    materialEditor.ToggleGraphNodeEnumDropdown(boolNodeId, "constant.bool");
    boolProperties = materialEditor.GraphNodeProperties(boolNodeId);
    boolProperty = std::ranges::find_if(boolProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "constant.bool";
    });
    kb::editor::tests::Require(boolProperty != boolProperties.end() && boolProperty->dropdownOpen,
        "KBMAT-MAT58: ConstantBool property should track dropdown open state");
    kb::editor::tests::Require(materialEditor.SetGraphNodeEnumValue(boolNodeId, "constant.bool", "true"),
        "KBMAT-MAT58: ConstantBool enum edit should update node metadata");
    boolProperties = materialEditor.GraphNodeProperties(boolNodeId);
    boolProperty = std::ranges::find_if(boolProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "constant.bool";
    });
    const kb::render::RenderMaterialGraphNode* boolNode =
        kb::render::FindRenderMaterialGraphNode(materialEditor.WorkingCopy()->graph, boolNodeId);
    kb::editor::tests::Require(boolNode != nullptr &&
            boolNode->parameter.defaultValueHint == "true" &&
            boolProperty != boolProperties.end() &&
            boolProperty->value.text == "true",
        "KBMAT-MAT58: ConstantBool enum edit should persist the selected bool value");

    kb::editor::MaterialEditorState staticAuthoringEditor;
    kb::render::RenderMaterialAssetData staticAuthoringAsset{};
    staticAuthoringAsset.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    staticAuthoringAsset.graph.shadingModel = "unlit";
    staticAuthoringEditor.Open(kb::assets::AssetId{ 0x5814U }, staticAuthoringAsset);

    std::uint32_t staticSwitchNodeId = 0U;
    kb::editor::tests::Require(staticAuthoringEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::StaticSwitch, -280, 32, &staticSwitchNodeId),
        "KBMAT-MAT84: Material Editor should create a StaticSwitch node for typed property editing");
    std::vector<kb::editor::MaterialEditorGraphNodeProperty> staticSwitchProperties =
        staticAuthoringEditor.GraphNodeProperties(staticSwitchNodeId);
    auto staticSwitchProperty = std::ranges::find_if(staticSwitchProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "staticSwitch.selector";
    });
    kb::editor::tests::Require(staticSwitchProperty != staticSwitchProperties.end() &&
            staticSwitchProperty->kind == kb::editor::MaterialEditorGraphNodePropertyKind::Enum &&
            staticSwitchProperty->type == kb::render::RenderMaterialParameterType::Bool &&
            staticSwitchProperty->value.text == "false" &&
            staticSwitchProperty->options.size() == 2U,
        "KBMAT-MAT84: StaticSwitch should expose its default branch as a typed bool property");
    kb::editor::tests::Require(staticAuthoringEditor.SetGraphNodeEnumValue(staticSwitchNodeId, "staticSwitch.selector", "true"),
        "KBMAT-MAT84: StaticSwitch default branch property should update node metadata");
    const kb::render::RenderMaterialGraphNode* staticSwitchNode =
        kb::render::FindRenderMaterialGraphNode(staticAuthoringEditor.WorkingCopy()->graph, staticSwitchNodeId);
    kb::editor::tests::Require(staticSwitchNode != nullptr && staticSwitchNode->parameter.defaultValueHint == "true",
        "KBMAT-MAT84: StaticSwitch property edit should persist the selected branch");

    std::uint32_t switchTrueColorId = 0U;
    std::uint32_t switchFalseColorId = 0U;
    kb::editor::tests::Require(staticAuthoringEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantColor, -560, -40, &switchTrueColorId) &&
            staticAuthoringEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantColor, -560, 120, &switchFalseColorId) &&
            staticAuthoringEditor.SetGraphConstantColorValue(switchTrueColorId, std::array<float, 4U>{ 1.0F, 0.0F, 0.0F, 1.0F }) &&
            staticAuthoringEditor.SetGraphConstantColorValue(switchFalseColorId, std::array<float, 4U>{ 0.0F, 0.0F, 1.0F, 1.0F }) &&
            staticAuthoringEditor.ConnectGraphPins(switchTrueColorId, "rgba", staticSwitchNodeId, "true") &&
            staticAuthoringEditor.ConnectGraphPins(switchFalseColorId, "rgba", staticSwitchNodeId, "false") &&
            staticAuthoringEditor.ConnectGraphPins(staticSwitchNodeId, "result", 1U, "baseColor"),
        "KBMAT-MAT84: StaticSwitch edited from the details panel should route through graph links");
    const kb::render::RenderMaterialGraphCompileResult staticSwitchCompiled =
        kb::render::CompileRenderMaterialGraphToShaderSource(
            staticAuthoringEditor.WorkingCopy()->graph,
            kb::render::RenderMaterialGraphBuildContext{ .assetId = 0x5814U });
    kb::editor::tests::Require(staticSwitchCompiled.Succeeded() &&
            staticSwitchCompiled.shader.source.find("vec4(1.0, 0.0, 0.0, 1.0)") != std::string::npos &&
            staticSwitchCompiled.shader.source.find("vec4(0.0, 0.0, 1.0, 1.0)") == std::string::npos,
        "KBMAT-MAT84: Edited StaticSwitch should compile only the selected branch");

    kb::editor::MaterialEditorState staticMaskEditor;
    kb::render::RenderMaterialAssetData staticMaskAsset{};
    staticMaskAsset.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    staticMaskAsset.graph.shadingModel = "unlit";
    staticMaskEditor.Open(kb::assets::AssetId{ 0x5815U }, staticMaskAsset);

    std::uint32_t staticMaskColorId = 0U;
    std::uint32_t staticMaskNodeId = 0U;
    kb::editor::tests::Require(staticMaskEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantColor, -520, 80, &staticMaskColorId) &&
            staticMaskEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::StaticComponentMask, -260, 80, &staticMaskNodeId),
        "KBMAT-MAT84: Material Editor should create a StaticComponentMask node for typed property editing");
    std::vector<kb::editor::MaterialEditorGraphNodeProperty> staticMaskProperties =
        staticMaskEditor.GraphNodeProperties(staticMaskNodeId);
    const auto staticMaskRed = std::ranges::find_if(staticMaskProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "staticComponentMask.r";
    });
    const auto staticMaskGreen = std::ranges::find_if(staticMaskProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "staticComponentMask.g";
    });
    const auto staticMaskBlue = std::ranges::find_if(staticMaskProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "staticComponentMask.b";
    });
    const auto staticMaskAlpha = std::ranges::find_if(staticMaskProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "staticComponentMask.a";
    });
    kb::editor::tests::Require(staticMaskRed != staticMaskProperties.end() &&
            staticMaskGreen != staticMaskProperties.end() &&
            staticMaskBlue != staticMaskProperties.end() &&
            staticMaskAlpha != staticMaskProperties.end() &&
            staticMaskRed->value.text == "true" &&
            staticMaskGreen->value.text == "true" &&
            staticMaskBlue->value.text == "true" &&
            staticMaskAlpha->value.text == "true",
        "KBMAT-MAT84: StaticComponentMask should expose typed R/G/B/A channel toggles");
    kb::editor::tests::Require(staticMaskEditor.SetGraphNodeEnumValue(staticMaskNodeId, "staticComponentMask.g", "false") &&
            staticMaskEditor.SetGraphNodeEnumValue(staticMaskNodeId, "staticComponentMask.a", "false"),
        "KBMAT-MAT84: StaticComponentMask channel toggles should update node metadata");
    const kb::render::RenderMaterialGraphNode* staticMaskNode =
        kb::render::FindRenderMaterialGraphNode(staticMaskEditor.WorkingCopy()->graph, staticMaskNodeId);
    kb::editor::tests::Require(staticMaskNode != nullptr && staticMaskNode->parameter.defaultValueHint == "rb",
        "KBMAT-MAT84: StaticComponentMask should persist enabled channels in runtime mask format");
    kb::editor::tests::Require(staticMaskEditor.ConnectGraphPins(staticMaskColorId, "rgba", staticMaskNodeId, "input") &&
            staticMaskEditor.ConnectGraphPins(staticMaskNodeId, "result", 1U, "baseColor"),
        "KBMAT-MAT84: Edited StaticComponentMask should route through graph links");
    const kb::render::RenderMaterialGraphCompileResult staticMaskCompiled =
        kb::render::CompileRenderMaterialGraphToShaderSource(
            staticMaskEditor.WorkingCopy()->graph,
            kb::render::RenderMaterialGraphBuildContext{ .assetId = 0x5815U });
    kb::editor::tests::Require(staticMaskCompiled.Succeeded() &&
            staticMaskCompiled.shader.source.find(").x, 0.0, ") != std::string::npos &&
            staticMaskCompiled.shader.source.find(").z, 0.0)") != std::string::npos,
        "KBMAT-MAT84: Edited StaticComponentMask should compile selected channels and zero disabled channels");

    kb::editor::MaterialEditorState transformSpaceEditor;
    kb::render::RenderMaterialAssetData transformSpaceAsset{};
    transformSpaceAsset.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    transformSpaceAsset.graph.shadingModel = "unlit";
    transformSpaceEditor.Open(kb::assets::AssetId{ 0x5816U }, transformSpaceAsset);

    std::uint32_t transformNodeId = 0U;
    std::uint32_t transformPositionNodeId = 0U;
    kb::editor::tests::Require(transformSpaceEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::Transform, -300, 40, &transformNodeId) &&
            transformSpaceEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::TransformPosition, -300, 220, &transformPositionNodeId),
        "KBMAT-MAT85: Material Editor should create Transform and TransformPosition nodes for typed space editing");
    std::vector<kb::editor::MaterialEditorGraphNodeProperty> transformProperties =
        transformSpaceEditor.GraphNodeProperties(transformNodeId);
    auto transformFromSpace = std::ranges::find_if(transformProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "transform.fromSpace";
    });
    auto transformToSpace = std::ranges::find_if(transformProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "transform.toSpace";
    });
    kb::editor::tests::Require(transformFromSpace != transformProperties.end() &&
            transformToSpace != transformProperties.end() &&
            transformFromSpace->kind == kb::editor::MaterialEditorGraphNodePropertyKind::Enum &&
            transformToSpace->kind == kb::editor::MaterialEditorGraphNodePropertyKind::Enum &&
            transformFromSpace->value.text == "tangent" &&
            transformToSpace->value.text == "world" &&
            transformFromSpace->options.size() == 3U &&
            transformToSpace->options.size() == 3U,
        "KBMAT-MAT85: Transform should expose typed From/To space enum properties");
    kb::editor::tests::Require(transformSpaceEditor.SetGraphNodeEnumValue(transformNodeId, "transform.fromSpace", "world") &&
            transformSpaceEditor.SetGraphNodeEnumValue(transformNodeId, "transform.toSpace", "view"),
        "KBMAT-MAT85: Transform space enum properties should update node metadata");
    const kb::render::RenderMaterialGraphNode* transformNode =
        kb::render::FindRenderMaterialGraphNode(transformSpaceEditor.WorkingCopy()->graph, transformNodeId);
    kb::editor::tests::Require(transformNode != nullptr && transformNode->parameter.defaultValueHint == "world view",
        "KBMAT-MAT85: Transform should persist spaces as the runtime hint format");

    std::vector<kb::editor::MaterialEditorGraphNodeProperty> transformPositionProperties =
        transformSpaceEditor.GraphNodeProperties(transformPositionNodeId);
    const auto transformPositionFromSpace =
        std::ranges::find_if(transformPositionProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
            return property.stableId == "transform.fromSpace";
        });
    const auto transformPositionToSpace =
        std::ranges::find_if(transformPositionProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
            return property.stableId == "transform.toSpace";
        });
    kb::editor::tests::Require(transformPositionFromSpace != transformPositionProperties.end() &&
            transformPositionToSpace != transformPositionProperties.end() &&
            transformPositionFromSpace->value.text == "tangent" &&
            transformPositionToSpace->value.text == "world",
        "KBMAT-MAT85: TransformPosition should expose typed From/To space enum properties");
    kb::editor::tests::Require(transformSpaceEditor.SetGraphNodeEnumValue(transformPositionNodeId, "transform.fromSpace", "view") &&
            transformSpaceEditor.SetGraphNodeEnumValue(transformPositionNodeId, "transform.toSpace", "world"),
        "KBMAT-MAT85: TransformPosition space enum properties should update node metadata");
    const kb::render::RenderMaterialGraphNode* transformPositionNode =
        kb::render::FindRenderMaterialGraphNode(transformSpaceEditor.WorkingCopy()->graph, transformPositionNodeId);
    kb::editor::tests::Require(transformPositionNode != nullptr && transformPositionNode->parameter.defaultValueHint == "view world",
        "KBMAT-MAT85: TransformPosition should persist spaces as the runtime hint format");

    std::uint32_t transformColorNodeId = 0U;
    kb::editor::tests::Require(transformSpaceEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantColor, -560, 120, &transformColorNodeId) &&
            transformSpaceEditor.ConnectGraphPins(transformColorNodeId, "rgba", transformNodeId, "value") &&
            transformSpaceEditor.ConnectGraphPins(transformColorNodeId, "rgba", transformPositionNodeId, "value") &&
            transformSpaceEditor.ConnectGraphPins(transformNodeId, "value", 1U, "baseColor") &&
            transformSpaceEditor.ConnectGraphPins(transformPositionNodeId, "value", 1U, "emissive"),
        "KBMAT-MAT85: Edited Transform nodes should route through graph links");
    const kb::render::RenderMaterialGraphCompileResult transformSpaceCompiled =
        kb::render::CompileRenderMaterialGraphToShaderSource(
            transformSpaceEditor.WorkingCopy()->graph,
            kb::render::RenderMaterialGraphBuildContext{ .assetId = 0x5816U });
    kb::editor::tests::Require(transformSpaceCompiled.Succeeded() &&
            transformSpaceCompiled.shader.source.find("mul(u_view") != std::string::npos &&
            transformSpaceCompiled.shader.source.find(", 0.0)") != std::string::npos &&
            transformSpaceCompiled.shader.source.find("mul(u_invView") != std::string::npos &&
            transformSpaceCompiled.shader.source.find(", 1.0)") != std::string::npos,
        "KBMAT-MAT85: Edited Transform spaces should compile into vector and position shader transforms");

    std::uint32_t uvNodeId = 0U;
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::Uv, -220, 220, &uvNodeId),
        "KBMAT-MAT58: Material Editor should create a UV node for enum property editing");
    std::vector<kb::editor::MaterialEditorGraphNodeProperty> uvProperties = materialEditor.GraphNodeProperties(uvNodeId);
    auto uvProperty = std::ranges::find_if(uvProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "uvSet";
    });
    kb::editor::tests::Require(uvProperty != uvProperties.end() &&
            uvProperty->kind == kb::editor::MaterialEditorGraphNodePropertyKind::Enum &&
            uvProperty->options.size() == 2U &&
            uvProperty->options[0].label == "UV0" &&
            uvProperty->options[1].label == "UV1",
        "KBMAT-MAT58: Enum dropdown should expose its typed option list");
    materialEditor.ToggleGraphNodeEnumDropdown(uvNodeId, "uvSet");
    uvProperties = materialEditor.GraphNodeProperties(uvNodeId);
    uvProperty = std::ranges::find_if(uvProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "uvSet";
    });
    kb::editor::tests::Require(uvProperty != uvProperties.end() && uvProperty->dropdownOpen,
        "KBMAT-MAT58: Enum property model should track dropdown open state");
    kb::editor::tests::Require(materialEditor.SetGraphNodeEnumValue(uvNodeId, "uvSet", "1"),
        "KBMAT-MAT58: Enum dropdown option should update the node metadata");
    uvProperties = materialEditor.GraphNodeProperties(uvNodeId);
    uvProperty = std::ranges::find_if(uvProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "uvSet";
    });
    kb::editor::tests::Require(uvProperty != uvProperties.end() && uvProperty->value.text == "1",
        "KBMAT-MAT58: UV enum edit should persist the selected option value");

    std::uint32_t textureCoordinateNodeId = 0U;
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::TextureCoordinate, -220, 280, &textureCoordinateNodeId),
        "KBMAT-MAT58: Material Editor should create a TextureCoordinate node for enum property editing");
    const kb::render::RenderMaterialGraphNode* textureCoordinateNode =
        kb::render::FindRenderMaterialGraphNode(materialEditor.WorkingCopy()->graph, textureCoordinateNodeId);
    kb::editor::tests::Require(textureCoordinateNode != nullptr && textureCoordinateNode->parameter.defaultValueHint == "1 1 0",
        "KBMAT-MAT81: TextureCoordinate should default to UV0 with 1x tiling");
    std::vector<kb::editor::MaterialEditorGraphNodeProperty> textureCoordinateProperties =
        materialEditor.GraphNodeProperties(textureCoordinateNodeId);
    auto textureCoordinateUvProperty = std::ranges::find_if(textureCoordinateProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "uvSet";
    });
    const auto textureCoordinateUTiling = std::ranges::find_if(textureCoordinateProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "textureCoordinate.tiling.0";
    });
    const auto textureCoordinateVTiling = std::ranges::find_if(textureCoordinateProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "textureCoordinate.tiling.1";
    });
    kb::editor::tests::Require(textureCoordinateUvProperty != textureCoordinateProperties.end() &&
            textureCoordinateUvProperty->kind == kb::editor::MaterialEditorGraphNodePropertyKind::Enum &&
            textureCoordinateUvProperty->value.text == "0",
        "KBMAT-MAT81: TextureCoordinate should expose UV Set as an enum property");
    kb::editor::tests::Require(textureCoordinateUTiling != textureCoordinateProperties.end() &&
            textureCoordinateVTiling != textureCoordinateProperties.end() &&
            textureCoordinateUTiling->kind == kb::editor::MaterialEditorGraphNodePropertyKind::Numeric &&
            textureCoordinateVTiling->kind == kb::editor::MaterialEditorGraphNodePropertyKind::Numeric &&
            textureCoordinateUTiling->value.numbers[0] == 1.0F &&
            textureCoordinateVTiling->value.numbers[0] == 1.0F,
        "KBMAT-MAT81: TextureCoordinate should expose U/V tiling numeric properties");
    kb::editor::tests::Require(materialEditor.SetGraphConstantComponentValue(textureCoordinateNodeId, 0U, 2.0F) &&
            materialEditor.SetGraphConstantComponentValue(textureCoordinateNodeId, 1U, 3.0F),
        "KBMAT-MAT81: TextureCoordinate U/V tiling numeric properties should update node metadata");
    textureCoordinateNode = kb::render::FindRenderMaterialGraphNode(materialEditor.WorkingCopy()->graph, textureCoordinateNodeId);
    kb::editor::tests::Require(textureCoordinateNode != nullptr && textureCoordinateNode->parameter.defaultValueHint == "2 3 0",
        "KBMAT-MAT81: TextureCoordinate tiling edit should persist as uTile/vTile/uvSet");
    kb::editor::tests::Require(materialEditor.SetGraphNodeEnumValue(textureCoordinateNodeId, "uvSet", "1"),
        "KBMAT-MAT81: TextureCoordinate UV Set enum should update node metadata");
    textureCoordinateNode = kb::render::FindRenderMaterialGraphNode(materialEditor.WorkingCopy()->graph, textureCoordinateNodeId);
    textureCoordinateProperties = materialEditor.GraphNodeProperties(textureCoordinateNodeId);
    textureCoordinateUvProperty = std::ranges::find_if(textureCoordinateProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "uvSet";
    });
    kb::editor::tests::Require(textureCoordinateNode != nullptr &&
            textureCoordinateNode->parameter.defaultValueHint == "2 3 1" &&
            textureCoordinateUvProperty != textureCoordinateProperties.end() &&
            textureCoordinateUvProperty->value.text == "1",
        "KBMAT-MAT81: TextureCoordinate UV Set enum should preserve tiling while selecting UV1");

    kb::editor::MaterialEditorState tiledTextureCoordinateEditor;
    kb::render::RenderMaterialAssetData tiledTextureCoordinateAsset{};
    tiledTextureCoordinateAsset.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    tiledTextureCoordinateAsset.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::TextureCoordinate,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{ .displayName = "Texture Coordinate", .defaultValueHint = "2 3 0" },
    });
    tiledTextureCoordinateEditor.Open(kb::assets::AssetId{ 0x5811U }, tiledTextureCoordinateAsset);
    kb::editor::tests::Require(tiledTextureCoordinateEditor.SetGraphNodeEnumValue(2U, "uvSet", "1"),
        "KBMAT-MAT81: TextureCoordinate UV Set enum should update existing tiled node metadata");
    const kb::render::RenderMaterialGraphNode* tiledTextureCoordinateNode =
        kb::render::FindRenderMaterialGraphNode(tiledTextureCoordinateEditor.WorkingCopy()->graph, 2U);
    kb::editor::tests::Require(tiledTextureCoordinateNode != nullptr &&
            tiledTextureCoordinateNode->parameter.defaultValueHint == "2 3 1",
        "KBMAT-MAT81: TextureCoordinate UV Set enum should preserve existing U/V tiling");

    const auto requireUtilityNumericProperty =
        [](const std::vector<kb::editor::MaterialEditorGraphNodeProperty>& properties,
            std::string_view stableId,
            float expectedValue,
            const char* message) {
            const auto property = std::ranges::find_if(properties, [stableId](const kb::editor::MaterialEditorGraphNodeProperty& candidate) {
                return candidate.stableId == stableId;
            });
            kb::editor::tests::Require(property != properties.end() &&
                    property->kind == kb::editor::MaterialEditorGraphNodePropertyKind::Numeric &&
                    property->type == kb::render::RenderMaterialParameterType::Scalar &&
                    property->value.numbers[0] == expectedValue,
                message);
        };

    kb::editor::MaterialEditorState uvUtilityEditor;
    kb::render::RenderMaterialAssetData uvUtilityAsset{};
    uvUtilityAsset.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    uvUtilityAsset.graph.shadingModel = "unlit";
    uvUtilityEditor.Open(kb::assets::AssetId{ 0x5812U }, uvUtilityAsset);

    std::uint32_t pannerNodeId = 0U;
    kb::editor::tests::Require(uvUtilityEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::Panner, -520, -120, &pannerNodeId),
        "KBMAT-MAT82: Material Editor should create a Panner node for numeric utility property editing");
    std::vector<kb::editor::MaterialEditorGraphNodeProperty> pannerProperties = uvUtilityEditor.GraphNodeProperties(pannerNodeId);
    requireUtilityNumericProperty(pannerProperties, "panner.speedU", 0.1F, "KBMAT-MAT82: Panner should expose Speed U as a numeric property");
    requireUtilityNumericProperty(pannerProperties, "panner.speedV", 0.0F, "KBMAT-MAT82: Panner should expose Speed V as a numeric property");
    kb::editor::tests::Require(uvUtilityEditor.SetGraphConstantComponentValue(pannerNodeId, 0U, 0.25F) &&
            uvUtilityEditor.SetGraphConstantComponentValue(pannerNodeId, 1U, -0.5F),
        "KBMAT-MAT82: Panner numeric utility properties should update node metadata");
    const kb::render::RenderMaterialGraphNode* pannerNode =
        kb::render::FindRenderMaterialGraphNode(uvUtilityEditor.WorkingCopy()->graph, pannerNodeId);
    kb::editor::tests::Require(pannerNode != nullptr && pannerNode->parameter.defaultValueHint == "0.25 -0.5",
        "KBMAT-MAT82: Panner should persist speedU/speedV in the runtime hint format");

    std::uint32_t rotatorNodeId = 0U;
    kb::editor::tests::Require(uvUtilityEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::Rotator, -520, 40, &rotatorNodeId),
        "KBMAT-MAT82: Material Editor should create a Rotator node for numeric utility property editing");
    std::vector<kb::editor::MaterialEditorGraphNodeProperty> rotatorProperties = uvUtilityEditor.GraphNodeProperties(rotatorNodeId);
    requireUtilityNumericProperty(rotatorProperties, "rotator.speed", 1.0F, "KBMAT-MAT82: Rotator should expose Speed as a numeric property");
    requireUtilityNumericProperty(rotatorProperties, "rotator.centerU", 0.5F, "KBMAT-MAT82: Rotator should expose Center U as a numeric property");
    requireUtilityNumericProperty(rotatorProperties, "rotator.centerV", 0.5F, "KBMAT-MAT82: Rotator should expose Center V as a numeric property");
    kb::editor::tests::Require(uvUtilityEditor.SetGraphConstantComponentValue(rotatorNodeId, 0U, 2.0F) &&
            uvUtilityEditor.SetGraphConstantComponentValue(rotatorNodeId, 1U, 0.25F) &&
            uvUtilityEditor.SetGraphConstantComponentValue(rotatorNodeId, 2U, 0.75F),
        "KBMAT-MAT82: Rotator numeric utility properties should update node metadata");
    const kb::render::RenderMaterialGraphNode* rotatorNode =
        kb::render::FindRenderMaterialGraphNode(uvUtilityEditor.WorkingCopy()->graph, rotatorNodeId);
    kb::editor::tests::Require(rotatorNode != nullptr && rotatorNode->parameter.defaultValueHint == "2 0.25 0.75",
        "KBMAT-MAT82: Rotator should persist speed/centerU/centerV in the runtime hint format");

    std::uint32_t bumpOffsetNodeId = 0U;
    kb::editor::tests::Require(uvUtilityEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::BumpOffset, -520, 200, &bumpOffsetNodeId),
        "KBMAT-MAT82: Material Editor should create a BumpOffset node for numeric utility property editing");
    std::vector<kb::editor::MaterialEditorGraphNodeProperty> bumpOffsetProperties = uvUtilityEditor.GraphNodeProperties(bumpOffsetNodeId);
    requireUtilityNumericProperty(
        bumpOffsetProperties,
        "bumpOffset.heightRatio",
        0.05F,
        "KBMAT-MAT82: BumpOffset should expose Height Ratio as a numeric property");
    kb::editor::tests::Require(uvUtilityEditor.SetGraphConstantComponentValue(bumpOffsetNodeId, 0U, 0.125F),
        "KBMAT-MAT82: BumpOffset numeric utility property should update node metadata");
    const kb::render::RenderMaterialGraphNode* bumpOffsetNode =
        kb::render::FindRenderMaterialGraphNode(uvUtilityEditor.WorkingCopy()->graph, bumpOffsetNodeId);
    kb::editor::tests::Require(bumpOffsetNode != nullptr && bumpOffsetNode->parameter.defaultValueHint == "0.125",
        "KBMAT-MAT82: BumpOffset should persist heightRatio in the runtime hint format");

    std::uint32_t constantBiasScaleNodeId = 0U;
    kb::editor::tests::Require(
        uvUtilityEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantBiasScale, -520, 360, &constantBiasScaleNodeId),
        "KBMAT-MAT82: Material Editor should create a ConstantBiasScale node for numeric utility property editing");
    std::vector<kb::editor::MaterialEditorGraphNodeProperty> constantBiasScaleProperties =
        uvUtilityEditor.GraphNodeProperties(constantBiasScaleNodeId);
    requireUtilityNumericProperty(
        constantBiasScaleProperties,
        "constantBiasScale.bias",
        0.0F,
        "KBMAT-MAT82: ConstantBiasScale should expose Bias as a numeric property");
    requireUtilityNumericProperty(
        constantBiasScaleProperties,
        "constantBiasScale.scale",
        1.0F,
        "KBMAT-MAT82: ConstantBiasScale should expose Scale as a numeric property");
    kb::editor::tests::Require(uvUtilityEditor.SetGraphConstantComponentValue(constantBiasScaleNodeId, 0U, -0.25F) &&
            uvUtilityEditor.SetGraphConstantComponentValue(constantBiasScaleNodeId, 1U, 2.0F),
        "KBMAT-MAT82: ConstantBiasScale numeric utility properties should update node metadata");
    const kb::render::RenderMaterialGraphNode* constantBiasScaleNode =
        kb::render::FindRenderMaterialGraphNode(uvUtilityEditor.WorkingCopy()->graph, constantBiasScaleNodeId);
    kb::editor::tests::Require(constantBiasScaleNode != nullptr && constantBiasScaleNode->parameter.defaultValueHint == "-0.25 2",
        "KBMAT-MAT82: ConstantBiasScale should persist bias/scale in the runtime hint format");

    std::uint32_t pannerSampleNodeId = 0U;
    std::uint32_t rotatorSampleNodeId = 0U;
    std::uint32_t bumpSampleNodeId = 0U;
    kb::editor::tests::Require(uvUtilityEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::TextureSample, -260, -120, &pannerSampleNodeId) &&
            uvUtilityEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::TextureSample, -260, 40, &rotatorSampleNodeId) &&
            uvUtilityEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::TextureSample, -260, 200, &bumpSampleNodeId),
        "KBMAT-MAT82: Edited UV utility graph should create texture sample sinks");
    kb::editor::tests::Require(uvUtilityEditor.ConnectGraphPins(pannerNodeId, "uv", pannerSampleNodeId, "uv"),
        "KBMAT-MAT82: Edited Panner UV should route into TextureSample UV");
    kb::editor::tests::Require(uvUtilityEditor.ConnectGraphPins(rotatorNodeId, "uv", rotatorSampleNodeId, "uv"),
        "KBMAT-MAT82: Edited Rotator UV should route into TextureSample UV");
    kb::editor::tests::Require(uvUtilityEditor.ConnectGraphPins(bumpOffsetNodeId, "uv", bumpSampleNodeId, "uv"),
        "KBMAT-MAT82: Edited BumpOffset UV should route into TextureSample UV");
    kb::editor::tests::Require(uvUtilityEditor.ConnectGraphPins(pannerSampleNodeId, "color", 1U, "baseColor"),
        "KBMAT-MAT82: Panner-driven texture sample should route into Base Color");
    kb::editor::tests::Require(uvUtilityEditor.ConnectGraphPins(rotatorSampleNodeId, "color", 1U, "emissive"),
        "KBMAT-MAT82: Rotator-driven texture sample should route into Emissive");
    kb::editor::tests::Require(uvUtilityEditor.ConnectGraphPins(bumpSampleNodeId, "a", 1U, "roughness"),
        "KBMAT-MAT82: BumpOffset-driven texture sample alpha should route into Roughness");
    kb::editor::tests::Require(uvUtilityEditor.ConnectGraphPins(constantBiasScaleNodeId, "result", 1U, "thinTranslucentOutput"),
        "KBMAT-MAT82: ConstantBiasScale result should route into a color material output");
    const kb::render::RenderMaterialGraphCompileResult uvUtilityCompiled =
        kb::render::CompileRenderMaterialGraphToShaderSource(
            uvUtilityEditor.WorkingCopy()->graph,
            kb::render::RenderMaterialGraphBuildContext{ .assetId = 0x5812U });
    kb::editor::tests::Require(uvUtilityCompiled.Succeeded() &&
            uvUtilityCompiled.shader.source.find("vec2(0.25, -0.5)") != std::string::npos &&
            uvUtilityCompiled.shader.source.find("vec2(0.25, 0.75)") != std::string::npos &&
            uvUtilityCompiled.shader.source.find("* 2.0") != std::string::npos &&
            uvUtilityCompiled.shader.source.find("* 0.125") != std::string::npos &&
            uvUtilityCompiled.shader.source.find("vec4_splat(-0.25)") != std::string::npos &&
            uvUtilityCompiled.shader.source.find("vec4_splat(2.0)") != std::string::npos,
        "KBMAT-MAT82: Edited UV utility properties should compile into production shader expressions");

    kb::editor::MaterialEditorState proceduralMaskEditor;
    kb::render::RenderMaterialAssetData proceduralMaskAsset{};
    proceduralMaskAsset.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    proceduralMaskAsset.graph.shadingModel = "unlit";
    proceduralMaskEditor.Open(kb::assets::AssetId{ 0x5813U }, proceduralMaskAsset);

    std::uint32_t sphereMaskNodeId = 0U;
    kb::editor::tests::Require(proceduralMaskEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::SphereMask, -440, 80, &sphereMaskNodeId),
        "KBMAT-MAT83: Material Editor should create a SphereMask node for procedural mask property editing");
    std::vector<kb::editor::MaterialEditorGraphNodeProperty> sphereMaskProperties =
        proceduralMaskEditor.GraphNodeProperties(sphereMaskNodeId);
    requireUtilityNumericProperty(
        sphereMaskProperties,
        "sphereMask.radius",
        1.0F,
        "KBMAT-MAT83: SphereMask should expose Radius as a numeric property");
    requireUtilityNumericProperty(
        sphereMaskProperties,
        "sphereMask.hardness",
        0.5F,
        "KBMAT-MAT83: SphereMask should expose Hardness as a numeric property");
    kb::editor::tests::Require(proceduralMaskEditor.SetGraphConstantComponentValue(sphereMaskNodeId, 0U, 1.5F) &&
            proceduralMaskEditor.SetGraphConstantComponentValue(sphereMaskNodeId, 1U, 0.75F),
        "KBMAT-MAT83: SphereMask numeric properties should update node metadata");
    const kb::render::RenderMaterialGraphNode* sphereMaskNode =
        kb::render::FindRenderMaterialGraphNode(proceduralMaskEditor.WorkingCopy()->graph, sphereMaskNodeId);
    kb::editor::tests::Require(sphereMaskNode != nullptr && sphereMaskNode->parameter.defaultValueHint == "1.5 0.75",
        "KBMAT-MAT83: SphereMask should persist radius/hardness in the runtime hint format");

    std::uint32_t antialiasedTextureMaskNodeId = 0U;
    kb::editor::tests::Require(proceduralMaskEditor.AddGraphNode(
            kb::render::RenderMaterialGraphNodeKind::AntialiasedTextureMask,
            -440,
            240,
            &antialiasedTextureMaskNodeId),
        "KBMAT-MAT83: Material Editor should create an AntialiasedTextureMask node for procedural mask property editing");
    std::vector<kb::editor::MaterialEditorGraphNodeProperty> antialiasedTextureMaskProperties =
        proceduralMaskEditor.GraphNodeProperties(antialiasedTextureMaskNodeId);
    requireUtilityNumericProperty(
        antialiasedTextureMaskProperties,
        "antialiasedTextureMask.threshold",
        0.5F,
        "KBMAT-MAT83: AntialiasedTextureMask should expose Threshold as a numeric property");
    kb::editor::tests::Require(proceduralMaskEditor.SetGraphConstantComponentValue(antialiasedTextureMaskNodeId, 0U, 0.33F),
        "KBMAT-MAT83: AntialiasedTextureMask numeric property should update node metadata");
    const kb::render::RenderMaterialGraphNode* antialiasedTextureMaskNode =
        kb::render::FindRenderMaterialGraphNode(proceduralMaskEditor.WorkingCopy()->graph, antialiasedTextureMaskNodeId);
    kb::editor::tests::Require(antialiasedTextureMaskNode != nullptr && antialiasedTextureMaskNode->parameter.defaultValueHint == "0.33",
        "KBMAT-MAT83: AntialiasedTextureMask should persist threshold in the runtime hint format");

    kb::editor::tests::Require(proceduralMaskEditor.ConnectGraphPins(sphereMaskNodeId, "value", 1U, "baseColor"),
        "KBMAT-MAT83: Edited SphereMask should route into Base Color");
    kb::editor::tests::Require(proceduralMaskEditor.ConnectGraphPins(antialiasedTextureMaskNodeId, "value", 1U, "emissive"),
        "KBMAT-MAT83: Edited AntialiasedTextureMask should route into Emissive");
    const kb::render::RenderMaterialGraphCompileResult proceduralMaskCompiled =
        kb::render::CompileRenderMaterialGraphToShaderSource(
            proceduralMaskEditor.WorkingCopy()->graph,
            kb::render::RenderMaterialGraphBuildContext{ .assetId = 0x5813U });
    kb::editor::tests::Require(proceduralMaskCompiled.Succeeded() &&
            proceduralMaskCompiled.shader.source.find("smoothstep(0.375, 1.5") != std::string::npos &&
            proceduralMaskCompiled.shader.source.find("smoothstep(0.33 -") != std::string::npos,
        "KBMAT-MAT83: Edited procedural mask properties should compile into production shader expressions");

    kb::editor::MaterialEditorState colorRampEditor;
    kb::render::RenderMaterialAssetData colorRampAsset{};
    colorRampAsset.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    colorRampAsset.graph.shadingModel = "unlit";
    colorRampEditor.Open(kb::assets::AssetId{ 0x5817U }, colorRampAsset);

    std::uint32_t colorRampNodeId = 0U;
    kb::editor::tests::Require(colorRampEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ColorRamp, -320, 80, &colorRampNodeId),
        "KBMAT-MAT87: Material Editor should create a ColorRamp node for gradient property editing");
    const kb::render::RenderMaterialGraphNode* colorRampNode =
        kb::render::FindRenderMaterialGraphNode(colorRampEditor.WorkingCopy()->graph, colorRampNodeId);
    kb::editor::tests::Require(colorRampNode != nullptr && colorRampNode->parameter.defaultValueHint == "0 0 0 0 1 1 1 1",
        "KBMAT-MAT87: ColorRamp should default to a two-stop black-to-white gradient");
    std::vector<kb::editor::MaterialEditorGraphNodeProperty> colorRampProperties =
        colorRampEditor.GraphNodeProperties(colorRampNodeId);
    const auto colorRampStop0Position = std::ranges::find_if(colorRampProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "colorRamp.stop0.position";
    });
    const auto colorRampStop0Color = std::ranges::find_if(colorRampProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "colorRamp.stop0.color";
    });
    const auto colorRampStop1Position = std::ranges::find_if(colorRampProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "colorRamp.stop1.position";
    });
    const auto colorRampStop1Color = std::ranges::find_if(colorRampProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "colorRamp.stop1.color";
    });
    kb::editor::tests::Require(colorRampStop0Position != colorRampProperties.end() &&
            colorRampStop0Color != colorRampProperties.end() &&
            colorRampStop1Position != colorRampProperties.end() &&
            colorRampStop1Color != colorRampProperties.end() &&
            colorRampStop0Position->kind == kb::editor::MaterialEditorGraphNodePropertyKind::Numeric &&
            colorRampStop0Color->kind == kb::editor::MaterialEditorGraphNodePropertyKind::Color &&
            colorRampStop1Position->value.numbers[0] == 1.0F,
        "KBMAT-MAT87: ColorRamp should expose typed position and color properties for its first two stops");
    kb::editor::tests::Require(colorRampEditor.SetGraphConstantComponentValue(colorRampNodeId, 0U, 0.25F) &&
            colorRampEditor.SetGraphConstantComponentValue(colorRampNodeId, 4U, 0.75F) &&
            colorRampEditor.SetGraphNodeColorPropertyValue(colorRampNodeId, "colorRamp.stop0.color", std::array<float, 4U>{ 0.1F, 0.2F, 0.3F, 1.0F }) &&
            colorRampEditor.SetGraphNodeColorPropertyValue(colorRampNodeId, "colorRamp.stop1.color", std::array<float, 4U>{ 0.8F, 0.6F, 0.4F, 1.0F }),
        "KBMAT-MAT87: ColorRamp position and color properties should update node metadata");
    colorRampNode = kb::render::FindRenderMaterialGraphNode(colorRampEditor.WorkingCopy()->graph, colorRampNodeId);
    kb::editor::tests::Require(colorRampNode != nullptr && colorRampNode->parameter.defaultValueHint == "0.25 0.1 0.2 0.3 0.75 0.8 0.6 0.4",
        "KBMAT-MAT87: ColorRamp should persist edited stops in the runtime hint format");
    kb::editor::tests::Require(colorRampEditor.ConnectGraphPins(colorRampNodeId, "value", 1U, "baseColor"),
        "KBMAT-MAT87: Edited ColorRamp should route into Base Color");
    const kb::render::RenderMaterialGraphCompileResult colorRampCompiled =
        kb::render::CompileRenderMaterialGraphToShaderSource(
            colorRampEditor.WorkingCopy()->graph,
            kb::render::RenderMaterialGraphBuildContext{ .assetId = 0x5817U });
    kb::editor::tests::Require(colorRampCompiled.Succeeded() &&
            colorRampCompiled.shader.source.find("smoothstep(0.25, 0.75") != std::string::npos &&
            colorRampCompiled.shader.source.find("vec3(0.1, 0.2, 0.3)") != std::string::npos &&
            colorRampCompiled.shader.source.find("vec3(0.8, 0.6, 0.4)") != std::string::npos,
        "KBMAT-MAT87: Edited ColorRamp properties should compile into production gradient shader expressions");

    kb::editor::MaterialEditorState colorRampPreserveEditor;
    kb::render::RenderMaterialAssetData colorRampPreserveAsset{};
    colorRampPreserveAsset.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    colorRampPreserveAsset.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ColorRamp,
        .positionX = -220,
        .positionY = 80,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Color Ramp",
            .defaultValueHint = "0 0 0 0 0.5 0.5 0.5 0.5 1 1 0 0",
            .overrideSupported = false,
        },
    });
    colorRampPreserveEditor.Open(kb::assets::AssetId{ 0x5818U }, colorRampPreserveAsset);
    kb::editor::tests::Require(colorRampPreserveEditor.SetGraphNodeColorPropertyValue(2U, "colorRamp.stop0.color", std::array<float, 4U>{ 0.2F, 0.4F, 0.6F, 1.0F }),
        "KBMAT-MAT87: ColorRamp property edits should work on loaded multi-stop assets");
    const kb::render::RenderMaterialGraphNode* preservedColorRampNode =
        kb::render::FindRenderMaterialGraphNode(colorRampPreserveEditor.WorkingCopy()->graph, 2U);
    kb::editor::tests::Require(preservedColorRampNode != nullptr &&
            preservedColorRampNode->parameter.defaultValueHint == "0 0.2 0.4 0.6 0.5 0.5 0.5 0.5 1 1 0 0",
        "KBMAT-MAT87: Editing the first ColorRamp stops should preserve additional loaded stops");

    kb::editor::MaterialEditorState customCodeEditor;
    kb::render::RenderMaterialAssetData customCodeAsset{};
    customCodeAsset.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    customCodeAsset.graph.shadingModel = "unlit";
    customCodeEditor.Open(kb::assets::AssetId{ 0x5819U }, customCodeAsset);

    std::uint32_t customCodeNodeId = 0U;
    kb::editor::tests::Require(customCodeEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::CustomCode, -260, 80, &customCodeNodeId),
        "KBMAT-MAT88: Material Editor should create a CustomCode node for shader body editing");
    std::vector<kb::editor::MaterialEditorGraphNodeProperty> customCodeProperties =
        customCodeEditor.GraphNodeProperties(customCodeNodeId);
    const auto customBodyProperty = std::ranges::find_if(customCodeProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "customCode.body";
    });
    const auto customDefinesProperty = std::ranges::find_if(customCodeProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "customCode.defines";
    });
    const auto customIncludesProperty = std::ranges::find_if(customCodeProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "customCode.includes";
    });
    auto customOutputTypeProperty = std::ranges::find_if(customCodeProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "customCode.outputType";
    });
    kb::editor::tests::Require(customBodyProperty != customCodeProperties.end() &&
            customDefinesProperty != customCodeProperties.end() &&
            customIncludesProperty != customCodeProperties.end() &&
            customOutputTypeProperty != customCodeProperties.end() &&
            customBodyProperty->kind == kb::editor::MaterialEditorGraphNodePropertyKind::Text &&
            customBodyProperty->value.text == "return A * B;" &&
            customOutputTypeProperty->kind == kb::editor::MaterialEditorGraphNodePropertyKind::Enum &&
            customOutputTypeProperty->value.text == "float4" &&
            customOutputTypeProperty->options.size() == 4U,
        "KBMAT-MAT88: CustomCode should expose editable body/defines/includes and output type properties");
    kb::editor::tests::Require(!customCodeEditor.SetGraphNodeTextProperty(customCodeNodeId, "customCode.body", "   "),
        "KBMAT-MAT88: CustomCode body editing should reject an empty shader body");
    kb::editor::tests::Require(customCodeEditor.SetGraphNodeTextProperty(customCodeNodeId, "customCode.defines", "#define KB_CUSTOM_SCALE 0.5") &&
            customCodeEditor.SetGraphNodeTextProperty(customCodeNodeId, "customCode.includes", "// custom include hook") &&
            customCodeEditor.SetGraphNodeTextProperty(customCodeNodeId, "customCode.body", "return (A.xyz + B.xyz) * KB_CUSTOM_SCALE;") &&
            customCodeEditor.SetGraphNodeEnumValue(customCodeNodeId, "customCode.outputType", "float3"),
        "KBMAT-MAT88: CustomCode editor properties should update shader code metadata");
    customCodeProperties = customCodeEditor.GraphNodeProperties(customCodeNodeId);
    customOutputTypeProperty = std::ranges::find_if(customCodeProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "customCode.outputType";
    });
    const kb::render::RenderMaterialGraphNode* customCodeNode =
        kb::render::FindRenderMaterialGraphNode(customCodeEditor.WorkingCopy()->graph, customCodeNodeId);
    kb::editor::tests::Require(customCodeNode != nullptr &&
            customCodeNode->customCode.defines == "#define KB_CUSTOM_SCALE 0.5" &&
            customCodeNode->customCode.includes == "// custom include hook" &&
            customCodeNode->customCode.body == "return (A.xyz + B.xyz) * KB_CUSTOM_SCALE;" &&
            customCodeNode->customCode.outputType == kb::render::RenderMaterialGraphPinType::Float3 &&
            customOutputTypeProperty != customCodeProperties.end() &&
            customOutputTypeProperty->value.text == "float3",
        "KBMAT-MAT88: CustomCode property edits should persist in the runtime custom code schema");

    std::uint32_t customColorAId = 0U;
    std::uint32_t customColorBId = 0U;
    kb::editor::tests::Require(customCodeEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantColor, -520, 20, &customColorAId) &&
            customCodeEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantColor, -520, 140, &customColorBId) &&
            customCodeEditor.SetGraphConstantColorValue(customColorAId, std::array<float, 4U>{ 0.2F, 0.0F, 0.0F, 1.0F }) &&
            customCodeEditor.SetGraphConstantColorValue(customColorBId, std::array<float, 4U>{ 0.0F, 0.4F, 0.0F, 1.0F }) &&
            customCodeEditor.ConnectGraphPins(customColorAId, "rgba", customCodeNodeId, "A") &&
            customCodeEditor.ConnectGraphPins(customColorBId, "rgba", customCodeNodeId, "B") &&
            customCodeEditor.ConnectGraphPins(customCodeNodeId, "value", 1U, "baseColor"),
        "KBMAT-MAT88: Edited CustomCode should route through real graph links");
    const kb::render::RenderMaterialGraphCompileResult customCodeCompiled =
        kb::render::CompileRenderMaterialGraphToShaderSource(
            customCodeEditor.WorkingCopy()->graph,
            kb::render::RenderMaterialGraphBuildContext{ .assetId = 0x5819U });
    kb::editor::tests::Require(customCodeCompiled.Succeeded() &&
            customCodeCompiled.shader.source.find("#define KB_CUSTOM_SCALE 0.5") != std::string::npos &&
            customCodeCompiled.shader.source.find("return (A.xyz + B.xyz) * KB_CUSTOM_SCALE;") != std::string::npos &&
            customCodeCompiled.shader.source.find("vec3 custom") != std::string::npos,
        "KBMAT-MAT88: Edited CustomCode should compile into a production custom shader function");

    std::ostringstream customCodeSerialized;
    kb::render::RenderMaterialAssetWriter::Write(customCodeSerialized, *customCodeEditor.WorkingCopy());
    std::istringstream customCodeInput{ customCodeSerialized.str() };
    const kb::render::RenderMaterialAssetParseResult customCodeParsed =
        kb::render::RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(customCodeInput);
    kb::editor::tests::Require(customCodeParsed.Succeeded() && customCodeParsed.asset.has_value(),
        "KBMAT-MAT88: Edited CustomCode material should serialize and load");
    const kb::render::RenderMaterialGraphNode* loadedCustomCodeNode =
        kb::render::FindRenderMaterialGraphNode(customCodeParsed.asset->graph, customCodeNodeId);
    kb::editor::tests::Require(loadedCustomCodeNode != nullptr &&
            loadedCustomCodeNode->customCode.body == "return (A.xyz + B.xyz) * KB_CUSTOM_SCALE;" &&
            loadedCustomCodeNode->customCode.defines == "#define KB_CUSTOM_SCALE 0.5" &&
            loadedCustomCodeNode->customCode.includes == "// custom include hook" &&
            loadedCustomCodeNode->customCode.outputType == kb::render::RenderMaterialGraphPinType::Float3,
        "KBMAT-MAT88: Edited CustomCode schema should round-trip through material asset serialization");

    kb::editor::MaterialEditorState viewPropertyEditor;
    kb::render::RenderMaterialAssetData viewPropertyAsset{};
    viewPropertyAsset.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    viewPropertyAsset.graph.shadingModel = "unlit";
    viewPropertyEditor.Open(kb::assets::AssetId{ 0x5820U }, viewPropertyAsset);
    std::uint32_t viewPropertyNodeId = 0U;
    kb::editor::tests::Require(viewPropertyEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ViewProperty, -260, 80, &viewPropertyNodeId),
        "KBMAT-MAT58: Material Editor should create a ViewProperty node for enum property editing");
    std::vector<kb::editor::MaterialEditorGraphNodeProperty> viewProperties = viewPropertyEditor.GraphNodeProperties(viewPropertyNodeId);
    auto viewProperty = std::ranges::find_if(viewProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "viewProperty";
    });
    kb::editor::tests::Require(viewProperty != viewProperties.end() &&
            viewProperty->kind == kb::editor::MaterialEditorGraphNodePropertyKind::Enum &&
            viewProperty->options.size() == 4U &&
            viewProperty->value.text == "viewSize",
        "KBMAT-MAT58: ViewProperty must expose a typed property selector with a view-size default");
    viewPropertyEditor.ToggleGraphNodeEnumDropdown(viewPropertyNodeId, "viewProperty");
    viewProperties = viewPropertyEditor.GraphNodeProperties(viewPropertyNodeId);
    viewProperty = std::ranges::find_if(viewProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "viewProperty";
    });
    kb::editor::tests::Require(viewProperty != viewProperties.end() && viewProperty->dropdownOpen,
        "KBMAT-MAT58: ViewProperty enum property should track dropdown open state");
    kb::editor::tests::Require(viewPropertyEditor.SetGraphNodeEnumValue(viewPropertyNodeId, "viewProperty", "pixelPosition"),
        "KBMAT-MAT58: ViewProperty enum dropdown option should update node metadata");
    const kb::render::RenderMaterialGraphNode* viewPropertyNode =
        kb::render::FindRenderMaterialGraphNode(viewPropertyEditor.WorkingCopy()->graph, viewPropertyNodeId);
    kb::editor::tests::Require(viewPropertyNode != nullptr && viewPropertyNode->parameter.defaultValueHint == "pixelPosition",
        "KBMAT-MAT58: ViewProperty enum edit should persist the selected view property");
    std::uint32_t viewSampleNodeId = 0U;
    kb::editor::tests::Require(viewPropertyEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::TextureSample, -60, 80, &viewSampleNodeId) &&
            viewPropertyEditor.ConnectGraphPins(viewPropertyNodeId, "value", viewSampleNodeId, "uv") &&
            viewPropertyEditor.ConnectGraphPins(viewSampleNodeId, "color", 1U, "baseColor"),
        "KBMAT-MAT58: ViewProperty selection must connect through a real texture UV graph");
    const kb::render::RenderMaterialGraphCompileResult viewPropertyCompiled =
        kb::render::CompileRenderMaterialGraphToShaderSource(
            viewPropertyEditor.WorkingCopy()->graph,
            kb::render::RenderMaterialGraphBuildContext{ .assetId = 0x5820U });
    kb::editor::tests::Require(viewPropertyCompiled.Succeeded() &&
            viewPropertyCompiled.shader.source.find("ctx.screenPosition * ctx.viewSize") != std::string::npos,
        "KBMAT-MAT58: ViewProperty selected in the editor must compile to the selected runtime view expression");

    kb::editor::MaterialEditorState sceneTextureEditor;
    kb::render::RenderMaterialAssetData sceneTextureAsset{};
    sceneTextureAsset.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    sceneTextureAsset.graph.shadingModel = "unlit";
    sceneTextureEditor.Open(kb::assets::AssetId{ 0x5821U }, sceneTextureAsset);
    std::uint32_t sceneTextureNodeId = 0U;
    kb::editor::tests::Require(sceneTextureEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::SceneTexture, -260, 80, &sceneTextureNodeId),
        "KBMAT-MAT86: Material Editor should create a SceneTexture node for source property editing");
    std::vector<kb::editor::MaterialEditorGraphNodeProperty> sceneTextureProperties =
        sceneTextureEditor.GraphNodeProperties(sceneTextureNodeId);
    auto sceneTextureSource = std::ranges::find_if(sceneTextureProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "sceneTexture.source";
    });
    kb::editor::tests::Require(sceneTextureSource != sceneTextureProperties.end() &&
            sceneTextureSource->kind == kb::editor::MaterialEditorGraphNodePropertyKind::Enum &&
            sceneTextureSource->type == kb::render::RenderMaterialParameterType::Enum &&
            sceneTextureSource->value.text == "color" &&
            sceneTextureSource->options.size() == 2U &&
            sceneTextureSource->options[0].value == "color" &&
            sceneTextureSource->options[1].value == "depth",
        "KBMAT-MAT86: SceneTexture should expose a typed Scene Color/Scene Depth source selector");
    sceneTextureEditor.ToggleGraphNodeEnumDropdown(sceneTextureNodeId, "sceneTexture.source");
    sceneTextureProperties = sceneTextureEditor.GraphNodeProperties(sceneTextureNodeId);
    sceneTextureSource = std::ranges::find_if(sceneTextureProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "sceneTexture.source";
    });
    kb::editor::tests::Require(sceneTextureSource != sceneTextureProperties.end() && sceneTextureSource->dropdownOpen,
        "KBMAT-MAT86: SceneTexture source selector should track dropdown open state");
    kb::editor::tests::Require(sceneTextureEditor.ConnectGraphPins(sceneTextureNodeId, "color", 1U, "baseColor"),
        "KBMAT-MAT86: SceneTexture color output should connect to a real material output");
    const kb::render::RenderMaterialGraphCompileResult sceneTextureColorCompiled =
        kb::render::CompileRenderMaterialGraphToShaderSource(
            sceneTextureEditor.WorkingCopy()->graph,
            kb::render::RenderMaterialGraphBuildContext{ .assetId = 0x5821U });
    kb::editor::tests::Require(sceneTextureColorCompiled.Succeeded() &&
            sceneTextureColorCompiled.shader.reflection.usesSceneColor &&
            !sceneTextureColorCompiled.shader.reflection.usesSceneDepth &&
            sceneTextureColorCompiled.shader.source.find("SAMPLER2D(s_kbSceneColor, 4)") != std::string::npos,
        "KBMAT-MAT86: SceneTexture default source should compile as a scene-color sample");
    kb::editor::tests::Require(sceneTextureEditor.SetGraphNodeEnumValue(sceneTextureNodeId, "sceneTexture.source", "depth"),
        "KBMAT-MAT86: SceneTexture source selector should update node metadata");
    const kb::render::RenderMaterialGraphNode* sceneTextureNode =
        kb::render::FindRenderMaterialGraphNode(sceneTextureEditor.WorkingCopy()->graph, sceneTextureNodeId);
    kb::editor::tests::Require(sceneTextureNode != nullptr && sceneTextureNode->parameter.defaultValueHint == "depth",
        "KBMAT-MAT86: SceneTexture source edit should persist the runtime hint");
    sceneTextureProperties = sceneTextureEditor.GraphNodeProperties(sceneTextureNodeId);
    sceneTextureSource = std::ranges::find_if(sceneTextureProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "sceneTexture.source";
    });
    kb::editor::tests::Require(sceneTextureSource != sceneTextureProperties.end() && sceneTextureSource->value.text == "depth",
        "KBMAT-MAT86: SceneTexture source property should report the edited depth selection");
    const kb::render::RenderMaterialGraphCompileResult sceneTextureDepthCompiled =
        kb::render::CompileRenderMaterialGraphToShaderSource(
            sceneTextureEditor.WorkingCopy()->graph,
            kb::render::RenderMaterialGraphBuildContext{ .assetId = 0x5822U });
    kb::editor::tests::Require(sceneTextureDepthCompiled.Succeeded() &&
            !sceneTextureDepthCompiled.shader.reflection.usesSceneColor &&
            sceneTextureDepthCompiled.shader.reflection.usesSceneDepth &&
            sceneTextureDepthCompiled.shader.source.find("SAMPLER2D(s_kbSceneDepth, 5)") != std::string::npos &&
            sceneTextureDepthCompiled.shader.source.find("vec4_splat(texture2D(s_kbSceneDepth") != std::string::npos,
        "KBMAT-MAT86: SceneTexture depth source should compile as a scene-depth sample");

#if defined(_WIN32)
    const RECT content{ 0, 0, 960, 720 };
    materialEditor.ToggleGraphNodeEnumDropdown(uvNodeId, "uvSet");
    uvProperties = materialEditor.GraphNodeProperties(uvNodeId);
    const kb::editor::MaterialEditorPanelDetailsRows details =
        kb::editor::MaterialEditorPanelRenderer::DetailsRows(materialEditor.Parameters(), uvNodeId, uvProperties);
    kb::editor::tests::Require(std::ranges::any_of(details.nodePropertyRows, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "uvSet" && property.dropdownOpen;
    }),
        "KBMAT-MAT58: Details panel rows should be backed by typed node properties");
    const kb::editor::MaterialEditorPanelLayout layout = kb::editor::MaterialEditorPanelRenderer::ResolveLayout(content);
    const int optionY = layout.detailsPanel.top + 34 + 22 + (2 * kb::editor::MaterialEditorPanelMetrics::DetailsNodePropertyRowHeight) + 6;
    const std::optional<kb::editor::MaterialEditorGraphNodePropertyHit> optionHit =
        kb::editor::MaterialEditorPanelRenderer::GraphNodePropertyAt(content, uvProperties, layout.detailsPanel.left + 44, optionY);
    kb::editor::tests::Require(optionHit.has_value() &&
            optionHit->kind == kb::editor::MaterialEditorGraphNodePropertyHitKind::EnumOption &&
            optionHit->optionValue == "0",
        "KBMAT-MAT58: Details panel hit-test should resolve enum dropdown options");
#endif

    const std::vector<kb::editor::MaterialEditorGraphNodeProperty> textureProperties = materialEditor.GraphNodeProperties(textureNodeId);
    const auto textureProperty = std::ranges::find_if(textureProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.kind == kb::editor::MaterialEditorGraphNodePropertyKind::TextureAsset;
    });
    kb::editor::tests::Require(textureProperty != textureProperties.end() && textureProperty->value.assetId == 0x5800U,
        "KBMAT-MAT58: Texture nodes should expose an asset picker property backed by graph parameter values");

    std::uint32_t textureObjectNodeId = 0U;
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::TextureObject, -40, 220, &textureObjectNodeId),
        "KBMAT-MAT58: Material Editor should create TextureObject nodes from the palette/runtime kind");
    const kb::render::RenderMaterialGraphNode* textureObjectNode =
        kb::render::FindRenderMaterialGraphNode(materialEditor.WorkingCopy()->graph, textureObjectNodeId);
    kb::editor::tests::Require(textureObjectNode != nullptr &&
            textureObjectNode->parameter.stableId == "textureObject" + std::to_string(textureObjectNodeId) &&
            textureObjectNode->parameter.displayName == "Texture Object " + std::to_string(textureObjectNodeId) &&
            textureObjectNode->parameter.textureRole == "baseColor" &&
            textureObjectNode->parameter.expectedTextureColorSpace == kb::render::RenderMaterialTextureColorSpace::Srgb &&
            textureObjectNode->parameter.overrideSupported,
        "KBMAT-MAT58: Created TextureObject must carry runtime texture slot metadata defaults");
    const std::vector<kb::editor::MaterialEditorGraphNodeProperty> textureObjectProperties =
        materialEditor.GraphNodeProperties(textureObjectNodeId);
    const auto textureObjectProperty = std::ranges::find_if(textureObjectProperties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.kind == kb::editor::MaterialEditorGraphNodePropertyKind::TextureAsset;
    });
    kb::editor::tests::Require(textureObjectProperty != textureObjectProperties.end() &&
            textureObjectProperty->type == kb::render::RenderMaterialParameterType::Texture &&
            kb::render::RenderMaterialGraphPinDataType(*textureObjectNode, "texture", true) == kb::render::RenderMaterialGraphPinType::Texture2D,
        "KBMAT-MAT58: TextureObject must expose a texture picker property and typed Texture2D output");
}

void RunMaterialEditorGraphPinTypeUiModelTest() {
    kb::editor::MaterialEditorState materialEditor;
    kb::render::RenderMaterialAssetData material{};
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    materialEditor.Open(kb::assets::AssetId{ 0x4100U }, material);

    std::uint32_t colorNodeId = 0U;
    std::uint32_t textureNodeId = 0U;
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::ConstantColor, 220, 80, &colorNodeId),
        "KBMAT-MAT59: Material Editor should create a color node for pin compatibility tests");
    kb::editor::tests::Require(materialEditor.AddGraphNode(kb::render::RenderMaterialGraphNodeKind::TextureSample, 460, 80, &textureNodeId),
        "KBMAT-MAT59: Material Editor should create a texture node for pin compatibility tests");

    const kb::render::RenderMaterialGraphDocument& graph = materialEditor.WorkingCopy()->graph;
    kb::editor::tests::Require(
        kb::editor::MaterialEditorPanelRenderer::GraphPinDragState(graph, colorNodeId, "rgba", true, 1U, "baseColor", false) ==
            kb::editor::MaterialEditorGraphPinDragState::Compatible,
        "KBMAT-MAT59: Dragging a color output to Base Color should highlight as compatible");
    kb::editor::tests::Require(
        kb::editor::MaterialEditorPanelRenderer::GraphPinDragState(graph, colorNodeId, "rgba", true, textureNodeId, "texture", false) ==
            kb::editor::MaterialEditorGraphPinDragState::Incompatible,
        "KBMAT-MAT59: Dragging a color output to a Texture2D input should highlight as incompatible");
    kb::editor::tests::Require(
        kb::editor::MaterialEditorPanelRenderer::GraphPinDragState(graph, colorNodeId, "rgba", true, colorNodeId, "rgba", true) ==
            kb::editor::MaterialEditorGraphPinDragState::Source,
        "KBMAT-MAT59: Dragging from a pin should mark the source pin distinctly");
    kb::editor::tests::Require(!materialEditor.ConnectGraphPins(colorNodeId, "rgba", textureNodeId, "texture"),
        "KBMAT-MAT59: Incompatible highlighted pins should still be rejected by the graph model");
    kb::editor::tests::Require(materialEditor.ConnectGraphPins(colorNodeId, "rgba", 1U, "baseColor"),
        "KBMAT-MAT59: Compatible highlighted pins should connect through the graph model");

#if defined(_WIN32)
    const RECT content{ 0, 0, 960, 720 };
    const kb::render::RenderMaterialGraphDocument& graphAfterConnect = materialEditor.WorkingCopy()->graph;
    const std::optional<RECT> colorRect = kb::editor::MaterialEditorPanelRenderer::GraphNodeRect(content, graphAfterConnect, colorNodeId);
    kb::editor::tests::Require(colorRect.has_value(), "KBMAT-MAT59: Color node rect should resolve for pin hit-test");
    const std::size_t colorOutputPinCount = kb::editor::MaterialEditorPanelOutputPins(kb::render::RenderMaterialGraphNodeKind::ConstantColor).size();
    const POINT colorPin = kb::editor::MaterialEditorPanelOutputPinPoint(*colorRect, kb::render::RenderMaterialGraphNodeKind::ConstantColor, 0U, colorOutputPinCount);
    const std::optional<kb::editor::MaterialEditorGraphPinHit> colorHit =
        kb::editor::MaterialEditorPanelRenderer::GraphPinAt(content, graphAfterConnect, colorPin.x, colorPin.y);
    kb::editor::tests::Require(colorHit.has_value() &&
            colorHit->nodeId == colorNodeId &&
            colorHit->direction == kb::editor::MaterialEditorGraphPinDirection::Output &&
            colorHit->pin == "rgba" &&
            colorHit->type == kb::render::RenderMaterialGraphPinType::Color,
        "KBMAT-MAT59: Graph pin hit-test should return the color output pin and its type");
    const POINT colorAlphaPin = kb::editor::MaterialEditorPanelOutputPinPoint(*colorRect, kb::render::RenderMaterialGraphNodeKind::ConstantColor, 4U, colorOutputPinCount);
    const std::optional<kb::editor::MaterialEditorGraphPinHit> colorAlphaHit =
        kb::editor::MaterialEditorPanelRenderer::GraphPinAt(content, graphAfterConnect, colorAlphaPin.x, colorAlphaPin.y);
    kb::editor::tests::Require(colorAlphaHit.has_value() &&
            colorAlphaHit->nodeId == colorNodeId &&
            colorAlphaHit->direction == kb::editor::MaterialEditorGraphPinDirection::Output &&
            colorAlphaHit->pin == "a" &&
            colorAlphaHit->type == kb::render::RenderMaterialGraphPinType::Float,
        "KBMAT-MAT59: Graph pin hit-test should expose Constant Color's alpha channel output pin");

    const std::optional<RECT> textureRect = kb::editor::MaterialEditorPanelRenderer::GraphNodeRect(content, graphAfterConnect, textureNodeId);
    kb::editor::tests::Require(textureRect.has_value(), "KBMAT-MAT59: Texture node rect should resolve for pin hit-test");
    const POINT texturePin = kb::editor::MaterialEditorPanelInputPinPoint(*textureRect, kb::render::RenderMaterialGraphNodeKind::TextureSample, 0U);
    const std::optional<kb::editor::MaterialEditorGraphPinHit> textureHit =
        kb::editor::MaterialEditorPanelRenderer::GraphPinAt(content, graphAfterConnect, texturePin.x, texturePin.y);
    kb::editor::tests::Require(textureHit.has_value() &&
            textureHit->nodeId == textureNodeId &&
            textureHit->direction == kb::editor::MaterialEditorGraphPinDirection::Input &&
            textureHit->pin == "texture" &&
            textureHit->type == kb::render::RenderMaterialGraphPinType::Texture2D,
        "KBMAT-MAT59: Graph pin hit-test should return the texture input pin and its type");
#endif
}

void RunMaterialInstanceEditorOverrideModelAndSaveTest() {
    CleanTempRoot();

    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(scene.Assets().MountProject(TempRoot() / "Project"), "MAT-40 instance editor test could not mount project assets");
    kb::editor::tests::Require(scene.Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()),
        "MAT-40 instance editor test could not register material loader");
    kb::editor::tests::Require(scene.Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialInstanceAssetLoader>()),
        "MAT-40 instance editor test could not register material instance loader");

    const auto makeGraphLink = [](kb::render::RenderMaterialGraphNodeKind fromKind, std::uint32_t fromNode, std::string fromPin,
        kb::render::RenderMaterialGraphNodeKind toKind, std::uint32_t toNode, std::string toPin) {
        kb::render::RenderMaterialGraphLink link{
            .fromNodeId = fromNode,
            .fromPinId = kb::render::RenderMaterialGraphStablePinId(fromKind, fromPin, true),
            .fromPin = std::move(fromPin),
            .toNodeId = toNode,
            .toPinId = kb::render::RenderMaterialGraphStablePinId(toKind, toPin, false),
            .toPin = std::move(toPin),
        };
        link.id = kb::render::MakeRenderMaterialGraphLinkId(link);
        return link;
    };

    kb::render::RenderMaterialAssetData parent{};
    parent.materialType = kb::render::kRenderMaterialAssetBuiltInPbrType;
    parent.materialTypeVersion = kb::render::kRenderMaterialAssetBuiltInPbrTypeVersion;
    parent.hasExplicitMaterialType = true;
    parent.hasExplicitMaterialTypeVersion = true;
    parent.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    parent.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ParameterColor,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{ .stableId = "tint", .displayName = "Tint", .overrideSupported = true },
    });
    parent.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 3U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ParameterScalar,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{ .stableId = "roughness", .displayName = "Roughness", .overrideSupported = true },
    });
    parent.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 4U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ParameterTexture,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{ .stableId = "paint", .displayName = "Paint", .overrideSupported = true },
    });
    parent.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 5U,
        .kind = kb::render::RenderMaterialGraphNodeKind::StaticBoolParameter,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "useBlue",
            .displayName = "Use Blue",
            .defaultValueHint = "false",
            .overrideSupported = true,
        },
    });
    parent.graph.links.push_back(makeGraphLink(kb::render::RenderMaterialGraphNodeKind::ParameterColor, 2U, "rgba", kb::render::RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    parent.graphParameterValues.push_back(kb::render::RenderMaterialGraphParameterValue{
        .stableId = "tint",
        .type = kb::render::RenderMaterialParameterType::Color,
        .numbers = { 0.8F, 0.1F, 0.2F, 1.0F },
    });
    parent.graphParameterValues.push_back(kb::render::RenderMaterialGraphParameterValue{
        .stableId = "roughness",
        .type = kb::render::RenderMaterialParameterType::Scalar,
        .numbers = { 0.35F, 0.0F, 0.0F, 0.0F },
    });
    parent.graphParameterValues.push_back(kb::render::RenderMaterialGraphParameterValue{
        .stableId = "paint",
        .type = kb::render::RenderMaterialParameterType::Texture,
        .assetId = 0U,
    });

    const std::filesystem::path parentPath = TempRoot() / "Project" / "Assets" / "Materials" / "InstanceParent.kbmat";
    kb::editor::tests::Require(kb::render::RenderMaterialAssetWriter::Save(parentPath, parent), "MAT-40 instance editor test could not save parent material");
    kb::editor::tests::Require(scene.Assets().Discover() >= 1U, "MAT-40 instance editor test did not discover parent material");
    const kb::assets::AssetMetadata* parentMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Materials/InstanceParent.kbmat");
    kb::editor::tests::Require(parentMetadata != nullptr, "MAT-40 instance editor test did not find parent metadata");
    const kb::assets::AssetId parentAssetId = parentMetadata->id;

    kb::render::RenderMaterialInstanceAssetData instance{};
    instance.parentMaterialAssetId = parentAssetId;
    const std::filesystem::path instancePath = TempRoot() / "Project" / "Assets" / "Materials" / "InstanceParent_Inst.kbmatinst";
    kb::editor::tests::Require(kb::render::RenderMaterialInstanceAssetWriter::Save(instancePath, instance), "MAT-40 instance editor test could not save instance");
    kb::editor::tests::Require(scene.Assets().Discover() >= 2U, "MAT-40 instance editor test did not discover material instance");
    const kb::assets::AssetMetadata* instanceMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Materials/InstanceParent_Inst.kbmatinst");
    kb::editor::tests::Require(instanceMetadata != nullptr, "MAT-40 instance editor test did not find instance metadata");

    kb::render::RenderMaterialTypeSchema schema{};
    schema.typeName = parent.materialType;
    schema.typeVersion = parent.materialTypeVersion;
    schema.parameters.push_back(kb::render::RenderMaterialParameterSchema{
        .name = "tint",
        .displayName = "Tint",
        .type = kb::render::RenderMaterialParameterType::Color,
        .group = kb::render::RenderMaterialParameterGroup::Surface,
        .defaultValueHint = "0.8 0.1 0.2 1",
        .overrideSupported = true,
        .editorOrder = 1U,
    });
    schema.parameters.push_back(kb::render::RenderMaterialParameterSchema{
        .name = "roughness",
        .displayName = "Roughness",
        .type = kb::render::RenderMaterialParameterType::Scalar,
        .group = kb::render::RenderMaterialParameterGroup::Surface,
        .range = kb::render::RenderMaterialParameterRange{ .min = 0.0F, .max = 1.0F },
        .defaultValueHint = "0.35",
        .overrideSupported = true,
        .editorOrder = 2U,
    });
    schema.parameters.push_back(kb::render::RenderMaterialParameterSchema{
        .name = "paint",
        .displayName = "Paint",
        .type = kb::render::RenderMaterialParameterType::Texture,
        .group = kb::render::RenderMaterialParameterGroup::Surface,
        .overrideSupported = true,
        .editorOrder = 3U,
    });

    kb::editor::MaterialEditorState editor;
    editor.Open(instanceMetadata->id, parent, schema, instance);
    kb::editor::tests::Require(editor.IsMaterialInstanceOpen() && !editor.Dirty(), "MAT-40 instance editor should open a clean material instance document");

    const std::vector<kb::editor::MaterialEditorInstanceParentChainRow> parentRows = editor.InstanceParentChainRows();
    kb::editor::tests::Require(parentRows.size() == 2U &&
            parentRows[0].current &&
            parentRows[0].assetId == instanceMetadata->id &&
            !parentRows[1].current &&
            parentRows[1].assetId == parentAssetId,
        "KBMAT-MAT60: Instance panel must expose current instance and parent chain rows");

    const auto findParameter = [&editor](std::string_view stableId) -> const kb::editor::MaterialEditorParameter* {
        for (const kb::editor::MaterialEditorParameter& parameter : editor.Parameters()) {
            if (parameter.stableId == stableId) {
                return &parameter;
            }
        }
        return nullptr;
    };
    const kb::editor::MaterialEditorParameter* tintBefore = findParameter("tint");
    kb::editor::tests::Require(tintBefore != nullptr && !tintBefore->overrideActive &&
            kb::editor::tests::NearlyEqual(tintBefore->value.numbers[0], 0.8F) &&
            kb::editor::tests::NearlyEqual(tintBefore->defaultValue.numbers[0], 0.8F),
        "MAT-40 instance editor should show inherited parent tint before override");

    std::vector<kb::editor::MaterialEditorInstanceOverrideGroupRow> groups = editor.InstanceOverrideGroups();
    kb::editor::tests::Require(groups.size() == 1U &&
            groups[0].group == kb::editor::MaterialEditorParameterGroup::Surface &&
            groups[0].expanded &&
            groups[0].activeOverrideCount == 0U &&
            groups[0].totalParameterCount == 3U &&
            groups[0].parameters.size() == 3U &&
            groups[0].parameters[0].stableId == "tint" &&
            groups[0].parameters[1].stableId == "roughness" &&
            groups[0].parameters[2].stableId == "paint",
        "KBMAT-MAT60: Instance override groups must be sorted by editor order and expanded by default");
    kb::editor::tests::Require(!editor.ToggleInstanceOverrideGroup(kb::editor::MaterialEditorParameterGroup::Surface),
        "KBMAT-MAT60: Instance override group toggle must collapse an expanded group");
    groups = editor.InstanceOverrideGroups();
    kb::editor::tests::Require(groups.size() == 1U &&
            !groups[0].expanded &&
            groups[0].parameters.empty() &&
            groups[0].totalParameterCount == 3U,
        "KBMAT-MAT60: Collapsed instance override group must retain counts but hide rows");
    kb::editor::tests::Require(editor.ToggleInstanceOverrideGroup(kb::editor::MaterialEditorParameterGroup::Surface),
        "KBMAT-MAT60: Instance override group toggle must expand a collapsed group");

    std::vector<kb::editor::MaterialEditorInstanceStaticSwitchRow> staticRows = editor.InstanceStaticSwitchRows();
    kb::editor::tests::Require(staticRows.size() == 1U &&
            staticRows[0].nodeId == 5U &&
            staticRows[0].stableId == "useBlue" &&
            staticRows[0].displayName == "Use Blue" &&
            staticRows[0].parentValue == "false" &&
            staticRows[0].value == "false" &&
            !staticRows[0].overrideActive,
        "KBMAT-MAT60: Instance panel must expose inherited static switch values");

    kb::render::RenderMaterialInstanceAssetData edited = *editor.InstanceWorkingCopy();
    edited.hasOverrides = true;
    edited.overrides = parent;
    edited.overrides.graphParameterValues.clear();
    edited.overrides.graphParameterValues.push_back(kb::render::RenderMaterialGraphParameterValue{
        .stableId = "tint",
        .type = kb::render::RenderMaterialParameterType::Color,
        .numbers = { 0.05F, 0.9F, 0.25F, 1.0F },
    });
    edited.overrides.graphParameterValues.push_back(kb::render::RenderMaterialGraphParameterValue{
        .stableId = "paint",
        .type = kb::render::RenderMaterialParameterType::Texture,
        .assetId = 4242U,
    });
    editor.SetInstanceWorkingCopy(edited, kb::render::BuildEffectiveRenderMaterialInstanceAsset(parent, edited));
    kb::editor::tests::Require(editor.Dirty(), "MAT-40 instance editor should become dirty after an override edit");

    const kb::editor::MaterialEditorParameter* tintAfter = findParameter("tint");
    const kb::editor::MaterialEditorParameter* textureAfter = findParameter("paint");
    kb::editor::tests::Require(tintAfter != nullptr && tintAfter->overrideActive &&
            kb::editor::tests::NearlyEqual(tintAfter->value.numbers[1], 0.9F) &&
            kb::editor::tests::NearlyEqual(tintAfter->defaultValue.numbers[1], 0.1F),
        "MAT-40 instance editor should show override tint value and parent default");
    kb::editor::tests::Require(textureAfter != nullptr && textureAfter->overrideActive && textureAfter->value.assetId == 4242U,
        "MAT-40 instance editor should expose texture parameter overrides");

    groups = editor.InstanceOverrideGroups();
    kb::editor::tests::Require(groups.size() == 1U &&
            groups[0].activeOverrideCount == 2U &&
            groups[0].totalParameterCount == 3U,
        "KBMAT-MAT60: Instance override groups must count active color/texture overrides");

    const kb::render::RenderMaterialInstanceAssetData overrideSnapshot = *editor.InstanceWorkingCopy();
    kb::editor::tests::Require(editor.ClearInstanceParameterOverride("tint", kb::render::RenderMaterialParameterType::Color),
        "KBMAT-MAT60: Instance override toggle must clear an active graph parameter override");
    const kb::editor::MaterialEditorParameter* tintCleared = findParameter("tint");
    kb::editor::tests::Require(tintCleared != nullptr &&
            !tintCleared->overrideActive &&
            kb::editor::tests::NearlyEqual(tintCleared->value.numbers[0], 0.8F) &&
            kb::editor::tests::NearlyEqual(tintCleared->defaultValue.numbers[0], 0.8F),
        "KBMAT-MAT60: Clearing an instance override must restore the inherited render parameter value");
    editor.SetInstanceWorkingCopy(overrideSnapshot, kb::render::BuildEffectiveRenderMaterialInstanceAsset(parent, overrideSnapshot));

    kb::editor::tests::Require(!editor.SetInstanceStaticParameterOverride(
            "useBlue",
            kb::render::RenderMaterialGraphNodeKind::StaticBoolParameter,
            "maybe"),
        "KBMAT-MAT60: Static switch override must reject invalid bool text");
    kb::editor::tests::Require(editor.SetInstanceStaticParameterOverride(
            "useBlue",
            kb::render::RenderMaterialGraphNodeKind::StaticBoolParameter,
            "true"),
        "KBMAT-MAT60: Static switch toggle must set an active static override");
    staticRows = editor.InstanceStaticSwitchRows();
    kb::editor::tests::Require(staticRows.size() == 1U &&
            staticRows[0].value == "true" &&
            staticRows[0].overrideActive &&
            editor.InstanceWorkingCopy()->staticParameterOverrides.size() == 1U,
        "KBMAT-MAT60: Static switch rows must reflect active instance static overrides");
    kb::editor::tests::Require(editor.SetInstanceStaticParameterOverride(
            "useBlue",
            kb::render::RenderMaterialGraphNodeKind::StaticBoolParameter,
            "false"),
        "KBMAT-MAT60: Static switch toggle must clear override when set to the parent value");
    staticRows = editor.InstanceStaticSwitchRows();
    kb::editor::tests::Require(staticRows.size() == 1U &&
            staticRows[0].value == "false" &&
            !staticRows[0].overrideActive &&
            editor.InstanceWorkingCopy()->staticParameterOverrides.empty(),
        "KBMAT-MAT60: Static switch rows must fall back to inherited values after clearing");
    kb::editor::tests::Require(editor.SetInstanceStaticParameterOverride(
            "useBlue",
            kb::render::RenderMaterialGraphNodeKind::StaticBoolParameter,
            "true"),
        "KBMAT-MAT60: Static switch toggle must be reusable after clearing");

    const kb::editor::MaterialEditorPanelDetailsRows rows =
        kb::editor::MaterialEditorPanelRenderer::DetailsRows(editor.Parameters(), editor.SelectedNodeId(), {});
    const bool hasInstanceOverrideRow = std::ranges::any_of(rows.parameterRows, [](const std::string& row) {
        return row.find("Tint") != std::string::npos && row.find("instance override") != std::string::npos;
    });
    const bool hasTextureOverrideRow = std::ranges::any_of(rows.textureSlotRows, [](const std::string& row) {
        return row.find("Paint") != std::string::npos && row.find("instance override") != std::string::npos;
    });
    kb::editor::tests::Require(hasInstanceOverrideRow && hasTextureOverrideRow,
        "MAT-40 instance editor details panel should label active scalar/color/texture overrides");

    kb::editor::MaterialEditorPanelDetailsRows instanceDetails =
        kb::editor::MaterialEditorPanelRenderer::DetailsRows(editor.Parameters(), editor.SelectedNodeId(), {});
    instanceDetails.instanceParentRows = editor.InstanceParentChainRows();
    instanceDetails.instanceOverrideGroupRows = editor.InstanceOverrideGroups();
    instanceDetails.instanceStaticSwitchRows = editor.InstanceStaticSwitchRows();
    kb::editor::tests::Require(instanceDetails.instanceParentRows.size() == 2U &&
            instanceDetails.instanceOverrideGroupRows.size() == 1U &&
            instanceDetails.instanceOverrideGroupRows[0].activeOverrideCount == 2U &&
            instanceDetails.instanceStaticSwitchRows.size() == 1U &&
            instanceDetails.instanceStaticSwitchRows[0].overrideActive,
        "KBMAT-MAT60: Details panel model must consume parent chain, override groups, and static switch rows");

#if defined(_WIN32)
    const RECT content{ 0, 0, 960, 720 };
    const kb::editor::MaterialEditorPanelLayout layout = kb::editor::MaterialEditorPanelRenderer::ResolveLayout(content);
    const int textureRowY = layout.detailsPanel.top + 34 + 22 + (2 * 18) + 6 + 22 + 9;
    const std::optional<kb::editor::MaterialEditorPanelParameterHit> textureHit =
        kb::editor::MaterialEditorPanelRenderer::TextureParameterAt(content, editor.Parameters(), {}, 0U, layout.detailsPanel.left + 20, textureRowY);
    kb::editor::tests::Require(textureHit.has_value() && textureHit->stableId == "paint" && textureHit->type == kb::render::RenderMaterialParameterType::Texture,
        "MAT-40 instance editor texture row should be hit-testable for the picker");
#endif

    const std::unique_ptr<kb::editor::EditorMaterialInstanceEditCommand> command =
        kb::editor::EditorMaterialInstanceEditCommand::CreateRecorded(scene, instanceMetadata->id, "Save Material Instance", instance, *editor.InstanceWorkingCopy());
    kb::editor::tests::Require(command != nullptr && command->Execute(), "MAT-40 instance editor command should save .kbmatinst override document");
    const std::optional<kb::render::RenderMaterialInstanceAssetData> saved = kb::render::RenderMaterialInstanceAssetLoader::LoadInstance(instancePath);
    std::ifstream savedTextInput{ instancePath };
    std::ostringstream savedText;
    savedText << savedTextInput.rdbuf();
    kb::editor::tests::Require(saved.has_value() && saved->hasOverrides &&
            saved->staticParameterOverrides.size() == 1U &&
            saved->staticParameterOverrides[0].stableId == "useBlue" &&
            saved->staticParameterOverrides[0].value == "true" &&
            savedText.str().find("graphParameterValue tint") != std::string::npos &&
            savedText.str().find("graphParameterValue paint") != std::string::npos &&
            savedText.str().find("graphParameterValue roughness") == std::string::npos,
        "MAT-40 instance editor command should persist only explicit graph parameter override lines plus static switch overrides");
    const kb::render::RenderMaterialAssetData effective = kb::render::BuildEffectiveRenderMaterialInstanceAsset(parent, *saved);
    const auto savedTint = std::ranges::find_if(effective.graphParameterValues, [](const kb::render::RenderMaterialGraphParameterValue& value) {
        return value.stableId == "tint";
    });
    kb::editor::tests::Require(savedTint != effective.graphParameterValues.end() && kb::editor::tests::NearlyEqual(savedTint->numbers[1], 0.9F),
        "MAT-40 saved material instance should resolve to the edited tint override");
    kb::editor::tests::Require(command->Undo(), "MAT-40 instance editor command undo should restore source instance");
    const std::optional<kb::render::RenderMaterialInstanceAssetData> undone = kb::render::RenderMaterialInstanceAssetLoader::LoadInstance(instancePath);
    kb::editor::tests::Require(undone.has_value() && !undone->hasOverrides,
        "MAT-40 instance editor command undo should remove saved override body");
    kb::editor::tests::Require(command->Redo(), "MAT-40 instance editor command redo should restore saved overrides");

    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
}

void RunMaterialEditorMaterialStatsPanelModelTest() {
    kb::render::RenderMaterialAssetData material{};
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ParameterColor,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "tint",
            .displayName = "Tint",
            .defaultValueHint = "0.2 0.4 0.8 1",
        },
    });
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 3U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ParameterScalar,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "roughness",
            .displayName = "Roughness",
            .defaultValueHint = "0.45",
        },
    });
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 4U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ParameterTexture,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "albedoTex",
            .displayName = "Albedo Texture",
            .textureRole = "baseColor",
            .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
        },
    });
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 5U,
        .kind = kb::render::RenderMaterialGraphNodeKind::TextureSample,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "albedoSample",
            .displayName = "Albedo Sample",
        },
    });
    for (std::uint32_t index = 0U; index < 5U; ++index) {
        material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
            .id = 10U + index,
            .kind = kb::render::RenderMaterialGraphNodeKind::StaticBoolParameter,
            .parameter = kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "staticToggle" + std::to_string(index),
                .displayName = "Static Toggle " + std::to_string(index),
                .defaultValueHint = "false",
            },
        });
    }
    material.graph.links.push_back(MakeMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::ParameterTexture,
        4U,
        "texture",
        kb::render::RenderMaterialGraphNodeKind::TextureSample,
        5U,
        "texture"));
    material.graph.links.push_back(MakeMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::TextureSample,
        5U,
        "color",
        kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "baseColor"));
    material.graph.links.push_back(MakeMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::ParameterScalar,
        3U,
        "value",
        kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "roughness"));
    material.graph.links.push_back(MakeMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::ParameterColor,
        2U,
        "rgba",
        kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "emissive"));

    const kb::render::RenderMaterialGraphCompileResult compile =
        kb::render::CompileRenderMaterialGraphToShaderSource(material.graph, {});
    kb::editor::tests::Require(compile.Succeeded(), "KBMAT-MAT61: Stats test graph must compile before comparing reflection counts");

    kb::editor::MaterialEditorState editor;
    editor.Open(kb::assets::AssetId{ 0x6100U }, material);
    const kb::editor::MaterialEditorMaterialStatsModel& stats = editor.MaterialStats();
    kb::editor::tests::Require(stats.available &&
            stats.sourceHash == compile.shader.sourceHash &&
            stats.passRows.size() == 2U,
        "KBMAT-MAT61: Material Editor must expose current graph stats rows per pass");
    const kb::editor::MaterialEditorMaterialStatsPassRow& base = stats.passRows[0];
    kb::editor::tests::Require(base.passName == "BaseOpaque" &&
            base.graphProgram &&
            base.instructionEstimate > 0U &&
            base.textureSampleCount >= 1U &&
            base.samplerCount == compile.shader.reflection.textures.size() &&
            base.uniformCount == compile.shader.reflection.uniforms.size() &&
            base.varyingCount == compile.shader.reflection.requiredVaryings.size() &&
            base.staticVariantCount == 32U,
        "KBMAT-MAT61: Base pass stats must match graph reflection and variant estimate");
    const kb::editor::MaterialEditorMaterialStatsPassRow& shadow = stats.passRows[1];
    kb::editor::tests::Require(shadow.passName == "ShadowDepth" &&
            !shadow.graphProgram &&
            shadow.instructionEstimate == 0U &&
            shadow.samplerCount == 0U &&
            shadow.staticVariantCount == 32U,
        "KBMAT-MAT61: ShadowDepth stats must identify builtin shadow when the graph has no WPO");
    kb::editor::tests::Require(std::ranges::any_of(stats.warnings, [](const std::string& warning) {
            return warning.find("Variant count high") != std::string::npos;
        }),
        "KBMAT-MAT61: Material stats must surface budget warnings for variant explosion");

    kb::editor::MaterialEditorPanelDetailsRows details =
        kb::editor::MaterialEditorPanelRenderer::DetailsRows(editor.Parameters(), editor.SelectedNodeId(), {});
    details.materialStats = stats;
    kb::editor::tests::Require(details.materialStats.available &&
            details.materialStats.passRows[0].uniformCount == base.uniformCount &&
            !details.materialStats.warnings.empty(),
        "KBMAT-MAT61: Details panel model must carry material stats and warnings");
}

void RunMaterialEditorShaderViewerReflectionModelTest() {
    kb::render::RenderMaterialAssetData material{};
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ParameterTexture,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "viewerTex",
            .displayName = "Viewer Texture",
            .textureRole = "baseColor",
            .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
        },
    });
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 3U,
        .kind = kb::render::RenderMaterialGraphNodeKind::TextureSample,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "viewerSample",
            .displayName = "Viewer Sample",
        },
    });
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 4U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Viewer Emissive",
            .defaultValueHint = "0.1 0.2 0.3 1",
        },
    });
    material.graph.links.push_back(MakeMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::ParameterTexture,
        2U,
        "texture",
        kb::render::RenderMaterialGraphNodeKind::TextureSample,
        3U,
        "texture"));
    material.graph.links.push_back(MakeMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::TextureSample,
        3U,
        "color",
        kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "baseColor"));
    material.graph.links.push_back(MakeMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        4U,
        "rgba",
        kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "emissive"));

    const kb::render::RenderMaterialGraphCompileResult compile =
        kb::render::CompileRenderMaterialGraphToShaderSource(material.graph, {});
    kb::editor::tests::Require(compile.Succeeded(), "KBMAT-MAT62: Shader viewer test graph must compile");
    const std::string expectedWrapper = kb::render::BuildGraphFragmentWrapperSource(compile.shader, "BaseOpaque");

    kb::editor::MaterialEditorState editor;
    editor.Open(kb::assets::AssetId{ 0x6200U }, material);
    const kb::editor::MaterialEditorShaderViewerModel& viewer = editor.ShaderViewer();
    kb::editor::tests::Require(viewer.available &&
            viewer.sourceHash == compile.shader.sourceHash &&
            viewer.reflectionHash == kb::render::ComputeRenderMaterialGraphReflectionHash(compile.shader.reflection) &&
            viewer.sources.size() == 2U &&
            viewer.sources[0].passName == "BaseOpaque" &&
            viewer.sources[0].backendName == "shaderc-input" &&
            viewer.sources[0].stageName == "fragment" &&
            viewer.sources[0].source == expectedWrapper,
        "KBMAT-MAT62: Shader viewer must expose the current fragment wrapper source for the base pass");
    kb::editor::tests::Require(std::ranges::any_of(viewer.reflectionRows, [&compile](const kb::editor::MaterialEditorShaderReflectionRow& row) {
            return row.category == "texture" &&
                row.stableId == "viewerTex" &&
                row.name == compile.shader.reflection.textures[0].samplerName &&
                row.detail.find("slot " + std::to_string(compile.shader.reflection.textures[0].slot)) != std::string::npos &&
                row.detail.find("sRGB") != std::string::npos;
        }),
        "KBMAT-MAT62: Shader viewer reflection rows must expose texture sampler binding details");
    kb::editor::tests::Require(std::ranges::any_of(viewer.reflectionRows, [](const kb::editor::MaterialEditorShaderReflectionRow& row) {
            return row.category == "varying" && row.name == "uv0";
        }),
        "KBMAT-MAT62: Shader viewer reflection rows must expose required varyings");

    const std::string oldSource = viewer.sources[0].source;
    kb::render::RenderMaterialAssetData edited = *editor.WorkingCopy();
    for (kb::render::RenderMaterialGraphNode& node : edited.graph.nodes) {
        if (node.id == 4U) {
            node.parameter.defaultValueHint = "0.8 0.1 0.2 1";
            break;
        }
    }
    editor.SetWorkingCopy(std::move(edited));

    const kb::render::RenderMaterialGraphCompileResult editedCompile =
        kb::render::CompileRenderMaterialGraphToShaderSource(editor.WorkingCopy()->graph, {});
    kb::editor::tests::Require(editedCompile.Succeeded(), "KBMAT-MAT62: Edited shader viewer graph must compile");
    const kb::editor::MaterialEditorShaderViewerModel& editedViewer = editor.ShaderViewer();
    kb::editor::tests::Require(editedViewer.available &&
            editedViewer.sources[0].source == kb::render::BuildGraphFragmentWrapperSource(editedCompile.shader, "BaseOpaque") &&
            editedViewer.sources[0].source != oldSource &&
            editedViewer.sourceHash == editedCompile.shader.sourceHash,
        "KBMAT-MAT62: Shader viewer source must update after editing the graph");

    kb::editor::MaterialEditorPanelDetailsRows details =
        kb::editor::MaterialEditorPanelRenderer::DetailsRows(editor.Parameters(), editor.SelectedNodeId(), {});
    details.shaderViewer = editedViewer;
    kb::editor::tests::Require(details.shaderViewer.available &&
            !details.shaderViewer.sources[0].source.empty() &&
            !details.shaderViewer.reflectionRows.empty(),
        "KBMAT-MAT62: Details panel model must carry shader source and reflection rows");
}

void RunMaterialEditorFindInMaterialModelTest() {
    kb::render::RenderMaterialAssetData material{};
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ParameterScalar,
        .positionX = 420,
        .positionY = -120,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "roughGain",
            .displayName = "Roughness Gain",
            .defaultValueHint = "0.5",
            .description = "Controls rough detail intensity",
        },
    });
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 3U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = -200,
        .positionY = 60,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Deep Blue Constant",
            .defaultValueHint = "0 0 1 1",
        },
    });
    material.graph.comments.push_back(kb::render::RenderMaterialGraphCommentBox{
        .id = 1U,
        .positionX = -520,
        .positionY = 220,
        .width = 260,
        .height = 120,
        .text = "Foam detail notes",
    });
    material.graph.links.push_back(MakeMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::ParameterScalar,
        2U,
        "value",
        kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "roughness"));
    material.graph.links.push_back(MakeMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        3U,
        "rgba",
        kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "baseColor"));

    kb::editor::MaterialEditorState editor;
    editor.Open(kb::assets::AssetId{ 0x6300U }, material);
    kb::editor::tests::Require(!editor.InfoPanelVisible() && !editor.IsFindFocused(),
        "KBMAT-WORKFLOW-HOTKEYS: Find should open unfocused with the Material Editor info panel hidden");
    editor.FocusFind(true);
    kb::editor::tests::Require(editor.InfoPanelVisible() && editor.IsFindFocused(),
        "KBMAT-WORKFLOW-HOTKEYS: Ctrl+F target state must focus find and show the Material Editor info panel");
    editor.AppendFindText(L'r');
    editor.AppendFindText(L'o');
    editor.AppendFindText(L'u');
    editor.AppendFindText(L'g');
    editor.AppendFindText(L'h');
    kb::editor::tests::Require(editor.FindQuery() == "rough" && !editor.FindResults().empty(),
        "KBMAT-WORKFLOW-HOTKEYS: Focused Material Editor find input must append characters and refresh results");
    editor.BackspaceFind();
    kb::editor::tests::Require(editor.FindQuery() == "roug",
        "KBMAT-WORKFLOW-HOTKEYS: Focused Material Editor find input must support Backspace");
    editor.InsertFindText("h");
    kb::editor::tests::Require(editor.FindQuery() == "rough",
        "KBMAT-WORKFLOW-HOTKEYS: Focused Material Editor find input must support text insertion");
    editor.SetFindQuery("rough");
    kb::editor::tests::Require(std::ranges::any_of(editor.FindResults(), [](const kb::editor::MaterialEditorFindResult& result) {
            return result.nodeId == 2U &&
                result.kind == kb::editor::MaterialEditorFindResultKind::Parameter &&
                result.detail.find("roughGain") != std::string::npos;
        }),
        "KBMAT-MAT63: Find-in-material must search parameter display names, stable ids, and descriptions");

    editor.SetFindQuery("baseColor");
    kb::editor::tests::Require(std::ranges::any_of(editor.FindResults(), [](const kb::editor::MaterialEditorFindResult& result) {
            return result.nodeId == 1U &&
                result.kind == kb::editor::MaterialEditorFindResultKind::Pin &&
                result.detail == "input pin";
        }),
        "KBMAT-MAT63: Find-in-material must search graph pin names from the IR");

    editor.SetFindQuery("foam");
    const auto commentResult = std::ranges::find_if(editor.FindResults(), [](const kb::editor::MaterialEditorFindResult& result) {
        return result.commentId == 1U && result.kind == kb::editor::MaterialEditorFindResultKind::Comment;
    });
    kb::editor::tests::Require(commentResult != editor.FindResults().end(),
        "KBMAT-MAT63: Find-in-material must search comment text");
    const std::size_t commentIndex = static_cast<std::size_t>(std::distance(editor.FindResults().begin(), commentResult));
    kb::editor::tests::Require(editor.FocusFindResult(commentIndex) &&
            editor.SelectedCommentId() == 1U &&
            editor.SelectedNodeId() == 0U,
        "KBMAT-MAT63: Focusing a comment find result must select the comment");

    kb::editor::MaterialEditorPanelDetailsRows details =
        kb::editor::MaterialEditorPanelRenderer::DetailsRows(editor.Parameters(), editor.SelectedNodeId(), {});
    details.findQuery = std::string{ editor.FindQuery() };
    details.findFocused = editor.IsFindFocused();
    details.findResults = editor.FindResults();
    kb::editor::tests::Require(details.findQuery == "foam" && details.findFocused,
        "KBMAT-WORKFLOW-HOTKEYS: Details panel model must expose focused Material Editor find query");
    kb::editor::tests::Require(!details.findResults.empty() && details.findResults[0].commentId == 1U,
        "KBMAT-MAT63: Details panel model must carry find results");

    editor.SetFindQuery("deep");
    const auto nodeResult = std::ranges::find_if(editor.FindResults(), [](const kb::editor::MaterialEditorFindResult& result) {
        return result.nodeId == 3U && result.kind == kb::editor::MaterialEditorFindResultKind::Node;
    });
    kb::editor::tests::Require(nodeResult != editor.FindResults().end(),
        "KBMAT-MAT63: Find query must update material editor results for graph nodes");
    const std::size_t nodeIndex = static_cast<std::size_t>(std::distance(editor.FindResults().begin(), nodeResult));
    const std::optional<kb::editor::MaterialEditorFindFocusTarget> target =
        editor.FindResultFocusTarget(nodeIndex);
    kb::editor::tests::Require(target.has_value() &&
            target->graphX == -80 &&
            target->graphY == 140,
        "KBMAT-MAT63: Find result must expose a graph focus target suitable for centering");
    kb::editor::tests::Require(editor.FocusFindResult(nodeIndex) &&
            editor.SelectedNodeId() == 3U &&
            editor.SelectedCommentId() == 0U,
        "KBMAT-MAT63: Focusing a node find result must select the graph node");
}

void RunProjectFilesMaterialEditorMeshRendererRenderPathE2ETest() {
    CleanTempRoot();

    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(scene.Assets().MountProject(TempRoot() / "Project"), "KBMAT-1006: E2E test could not mount project assets");
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    kb::editor::tests::Require(manager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>()), "KBMAT-1006: E2E test could not register mesh loader");

    const std::filesystem::path meshPath = TempRoot() / "Project" / "Assets" / "Meshes" / "triangle.obj";
    std::error_code error;
    std::filesystem::create_directories(meshPath.parent_path(), error);
    kb::editor::tests::Require(!error, "KBMAT-1006: E2E test could not create mesh folder");
    WriteTriangleObj(meshPath);
    kb::editor::tests::Require(scene.Assets().Discover() >= 1U, "KBMAT-1006: Project Files did not discover the mesh asset");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/Meshes/triangle.obj");
    kb::editor::tests::Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "KBMAT-1006: Project Files discovered wrong mesh metadata");
    const kb::assets::AssetId meshAssetId = meshMetadata->id;
    const kb::assets::AssetHandle<kb::render::RenderMeshAssetData> loadedMesh = manager.Load<kb::render::RenderMeshAssetData>(meshMetadata->id);
    kb::editor::tests::Require(loadedMesh.IsLoaded(), "KBMAT-1006: Project Files mesh asset should load before runtime render");

    kb::editor::EditorMaterialAssetAuthoring authoring{ scene, browser, console };
    kb::editor::tests::Require(authoring.Create("/Game/Materials"), "KBMAT-1006: Project Files could not create a material asset");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/Materials/NewMaterial.kbmat");
    kb::editor::tests::Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "KBMAT-1006: Project Files did not discover the created material asset");
    const kb::assets::AssetId materialId = materialMetadata->id;
    kb::editor::tests::Require(browser.SelectedAsset() == materialId, "KBMAT-1006: Created material should be selected in Project Files");

    kb::editor::MaterialEditorState materialEditor;
    materialEditor.Open(browser.SelectedAsset(), kb::editor::EditorMaterialAssetGateway::Read(scene, browser.SelectedAsset()));
    kb::editor::tests::Require(materialEditor.OpenAssetId() == materialId && materialEditor.WorkingCopy().has_value(), "KBMAT-1006: Material Editor could not open the Project Files material");

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "KBMAT-1006 Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshAssetId.value,
    });
    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::AssignMaterial(scene, entity, materialEditor.OpenAssetId()), "KBMAT-1006: Material Editor material could not be assigned to selected Mesh Renderer");

    const kb::scene::MeshRendererComponent* assignedRenderer = scene.Components().MeshRenderers().TryGet(entity);
    kb::editor::tests::Require(assignedRenderer != nullptr && assignedRenderer->materialAssetId == materialId.value, "KBMAT-1006: Mesh Renderer did not receive the Material Editor asset");

    MaterialAuthoringHeadlessSurface surface;
    kb::render::DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    kb::render::Renderer renderer;
    renderer.ReserveRuntimeSceneResources(kb::render::Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 4U,
        .cachedMaterials = 4U,
        .cachedTextures = 4U,
        .frameReferencedMeshes = 4U,
        .frameReferencedMaterials = 4U,
        .frameReferencedTextures = 4U,
        .scenePassSubmitStats = 1U,
        .renderSceneMeshProxies = 8U,
        .renderSceneDrawGroupKeys = 4U,
        .meshResourceSlots = 4U,
        .materialResourceSlots = 4U,
        .textureResourceSlots = 4U,
        .meshBindings = 4U,
        .materialBindings = 4U,
        .textureBindings = 4U,
        .syncMeshProxies = 8U,
        .syncTransformCacheEntries = 8U,
        .syncTransformResolvingEntries = 8U,
    });
    kb::editor::tests::Require(renderer.Initialize(surface, &config), "KBMAT-1006: Renderer did not initialize for Material Editor E2E");
    kb::editor::tests::Require(renderer.BeginFrame(), "KBMAT-1006: Renderer did not begin frame for Material Editor E2E");

    const kb::render::RenderSceneSubmitDesc baseDesc{
        .target = kb::render::RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = kb::render::RenderViewportDesc{
                .id = kb::render::RenderViewportId{ 1U },
                .extent = kb::render::RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = kb::render::SceneRenderDrawBudget{
            .maxDrawCommands = 4U,
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = kb::render::SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
        .postProcessEnabled = false,
        .selectionMaskEnabled = false,
        .selectionOutlineEnabled = false,
        .gpuDrivenRuntimeDispatchEnabled = false,
    };

    const auto submitAndValidatePath = [&](kb::render::SceneRenderLightingPath lightingPath, const char* pathName) {
        kb::render::RenderSceneSubmitDesc desc = baseDesc;
        desc.lightingConfig = kb::render::SceneRenderLightingConfig{
            .maxForwardLights = lightingPath == kb::render::SceneRenderLightingPath::ClusteredForwardPlus
                ? kb::render::kMaxSceneForwardPlusLights
                : kb::render::kMaxSceneForwardLights,
            .lightingPath = lightingPath,
        };
        kb::editor::tests::Require(renderer.SubmitScene(scene, desc), "KBMAT-1006: Runtime render path rejected the Material Editor assigned scene");
        const kb::render::SceneRenderSubmitStats submitStats = renderer.LastSceneSubmitStats();
        if (submitStats.visibleMeshCount != 1U) {
            const kb::render::Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
            std::cerr
                << "KBMAT-1006 " << pathName << " stats: visible=" << submitStats.visibleMeshCount
                << " groups=" << submitStats.visibleDrawGroupCount
                << " culled=" << submitStats.culledInstanceCount
                << " submittedMesh=" << submitStats.submittedMeshCount
                << " drawCalls=" << submitStats.submittedDrawCallCount
                << " missingMeshBinding=" << submitStats.missingMeshBindingCount
                << " missingMeshResource=" << submitStats.missingMeshResourceCount
                << " unsupportedVertex=" << submitStats.unsupportedMeshVertexFormatCount
                << " missingMaterialBinding=" << submitStats.missingMaterialBindingCount
                << " missingMaterialResource=" << submitStats.missingMaterialResourceCount
                << " cachedMeshes=" << runtimeStats.cachedMeshCount
                << " cachedMaterials=" << runtimeStats.cachedMaterialCount
                << " referencedMeshes=" << runtimeStats.referencedMeshAssetCount
                << " referencedMaterials=" << runtimeStats.referencedMaterialAssetCount << '\n';
        }
        kb::editor::tests::Require(submitStats.visibleMeshCount == 1U, "KBMAT-1006: Runtime render path did not keep the assigned mesh visible");
        if (submitStats.submittedMeshCount != 1U || submitStats.submittedDrawCallCount == 0U) {
            const kb::render::Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
            std::cerr
                << "KBMAT-1006 " << pathName << " submit stats: submittedMesh=" << submitStats.submittedMeshCount
                << " drawCalls=" << submitStats.submittedDrawCallCount
                << " visible=" << submitStats.visibleMeshCount
                << " missingMeshBinding=" << submitStats.missingMeshBindingCount
                << " missingMaterialBinding=" << submitStats.missingMaterialBindingCount
                << " missingMaterialResource=" << submitStats.missingMaterialResourceCount
                << " unsupportedVertex=" << submitStats.unsupportedMeshVertexFormatCount
                << " cachedMeshes=" << runtimeStats.cachedMeshCount
                << " cachedMaterials=" << runtimeStats.cachedMaterialCount << '\n';
        }
        kb::editor::tests::Require(submitStats.submittedMeshCount == 1U && submitStats.submittedDrawCallCount > 0U,
            "KBMAT-1006: Runtime render path did not submit the assigned mesh/material");
        kb::editor::tests::Require(!submitStats.HasMissingResources(), "KBMAT-1006: Runtime render path reported missing resources for Material Editor assignment");
        kb::editor::tests::Require(!renderer.LastSceneDiagnostics().HasErrors(), "KBMAT-1006: Runtime render path reported diagnostics for Material Editor assignment");
        kb::editor::tests::Require(submitStats.lightingPath == static_cast<std::uint32_t>(lightingPath) + 1U && submitStats.lightingPathProduction,
            "KBMAT-1006: Runtime render path did not report a production lighting path");
        if (lightingPath == kb::render::SceneRenderLightingPath::ClusteredForwardPlus) {
            kb::editor::tests::Require(submitStats.forwardLightCapacity == kb::render::kMaxSceneForwardPlusLights,
                "KBMAT-1006: Forward+ Material Editor assignment did not use the expanded light budget");
        } else if (lightingPath == kb::render::SceneRenderLightingPath::Forward) {
            kb::editor::tests::Require(submitStats.forwardLightCapacity == kb::render::kMaxSceneForwardLights,
                "KBMAT-1006: Forward Material Editor assignment did not use the classic light budget");
        }

        const std::span<const kb::render::SceneRenderPassSubmitStats> passStats = renderer.LastScenePassSubmitStats();
        if (lightingPath == kb::render::SceneRenderLightingPath::Deferred) {
            kb::editor::tests::Require(passStats.size() >= 2U,
                "KBMAT-1006: Deferred Material Editor assignment must submit GBuffer and deferred lighting passes");
            kb::editor::tests::Require(passStats[0].renderPass == kb::render::RenderPassKind::GBufferGeometry &&
                    passStats[0].pass == kb::render::MeshPassType::GBuffer &&
                    passStats[0].stats.submittedMeshCount == 1U,
                "KBMAT-1006: Deferred Material Editor assignment did not submit the mesh through the GBuffer pass");
            kb::editor::tests::Require(passStats[1].renderPass == kb::render::RenderPassKind::DeferredLighting,
                "KBMAT-1006: Deferred Material Editor assignment did not submit the deferred lighting pass");
            if (passStats.size() > 2U) {
                kb::editor::tests::Require(passStats[2].renderPass == kb::render::RenderPassKind::TransparentScene &&
                        passStats[2].pass == kb::render::MeshPassType::BaseTransparent,
                    "KBMAT-1006: Deferred Material Editor assignment did not preserve the transparent forward pass");
            }
        } else {
            kb::editor::tests::Require(passStats.size() == 1U &&
                    passStats[0].renderPass == kb::render::RenderPassKind::OpaqueScene &&
                    passStats[0].pass == kb::render::MeshPassType::BaseOpaque &&
                    passStats[0].stats.submittedMeshCount == 1U,
                "KBMAT-1006: Forward Material Editor assignment did not submit through the opaque base pass");
        }
    };

    submitAndValidatePath(kb::render::SceneRenderLightingPath::Forward, "Forward");
    submitAndValidatePath(kb::render::SceneRenderLightingPath::ClusteredForwardPlus, "Forward+");
    submitAndValidatePath(kb::render::SceneRenderLightingPath::Deferred, "Deferred");

    const kb::render::SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const kb::render::RenderResourceRegistry* resources = renderer.SceneResources();
    kb::editor::tests::Require(resourceMap != nullptr && resources != nullptr, "KBMAT-1006: Runtime render path could not expose resource maps");
    const kb::render::RenderMaterialHandle materialHandle = resourceMap->ResolveMaterial(materialId.value);
    const kb::render::RenderMaterialResource* materialResource = resources->FindMaterial(materialHandle);
    kb::editor::tests::Require(materialHandle.IsValid() && materialResource != nullptr, "KBMAT-1006: Runtime render path did not bind the Material Editor asset");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(materialResource->baseColor[0], 1.0F) &&
            kb::editor::tests::NearlyEqual(materialResource->baseColor[1], 1.0F) &&
            kb::editor::tests::NearlyEqual(materialResource->baseColor[2], 1.0F),
        "KBMAT-1006: Runtime material resource did not preserve the created material defaults");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(TempRoot(), error);
}

void RunGraphMaterialCreateEditAssignRenderE2ETest() {
    CleanTempRoot();

    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(scene.Assets().MountProject(TempRoot() / "Project"), "KBMAT-GRAPH-0501: E2E test could not mount project assets");
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    kb::editor::tests::Require(manager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>()), "KBMAT-GRAPH-0501: E2E test could not register mesh loader");
    kb::editor::tests::Require(manager.RegisterLoader(std::make_unique<kb::render::RenderTextureAssetLoader>()), "KBMAT-GRAPH-0501: E2E test could not register texture loader");
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialGraphAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialTypeAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()));

    const std::filesystem::path materialFolder = TempRoot() / "Project" / "Assets" / "Materials";
    const std::filesystem::path meshPath = TempRoot() / "Project" / "Assets" / "Meshes" / "triangle.obj";
    const std::filesystem::path texturePath = TempRoot() / "Project" / "Assets" / "Textures" / "graph_albedo.kbtex";
    std::error_code error;
    std::filesystem::create_directories(materialFolder, error);
    std::filesystem::create_directories(meshPath.parent_path(), error);
    std::filesystem::create_directories(texturePath.parent_path(), error);
    kb::editor::tests::Require(!error, "KBMAT-GRAPH-0501: E2E test could not create project folders");
    WriteTriangleObj(meshPath);
    WriteTexture(texturePath, 220U, 80U, 40U);

    kb::render::RenderMaterialGraphDocument graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    graph.storageModel = "material-graph-asset";
    graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ParameterTexture,
        .positionX = -360,
        .positionY = 40,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "albedo",
            .displayName = "Albedo",
            .textureRole = "baseColor",
            .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
            .editorOrder = 10U,
        },
    });
    graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 3U,
        .kind = kb::render::RenderMaterialGraphNodeKind::TextureSample,
        .positionX = -120,
        .positionY = 40,
    });
    graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 4U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ParameterScalar,
        .positionX = -260,
        .positionY = 180,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "roughnessFactor",
            .displayName = "Roughness",
            .defaultValueHint = "0.37",
            .hasRange = true,
            .rangeMin = 0.0F,
            .rangeMax = 1.0F,
            .editorOrder = 20U,
        },
    });
    graph.links.push_back(MakeMaterialGraphLink(kb::render::RenderMaterialGraphNodeKind::ParameterTexture, 2U, "texture", kb::render::RenderMaterialGraphNodeKind::TextureSample, 3U, "texture"));
    graph.links.push_back(MakeMaterialGraphLink(kb::render::RenderMaterialGraphNodeKind::TextureSample, 3U, "color", kb::render::RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    graph.links.push_back(MakeMaterialGraphLink(kb::render::RenderMaterialGraphNodeKind::ParameterScalar, 4U, "value", kb::render::RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"));
    const kb::render::RenderMaterialGraphCompileResult compile = kb::render::CompileRenderMaterialGraphToShaderSource(
        graph,
        kb::render::RenderMaterialGraphBuildContext{
            .assetId = 0x0501U,
            .sourcePath = "/Game/Materials/GraphE2E.kbmaterialgraph",
        });
    kb::editor::tests::Require(compile.Succeeded() && compile.shader.sourceHash != 0U,
        "KBMAT-GRAPH-0501: graph with texture/baseColor/roughness nodes did not compile");

    const std::filesystem::path graphPath = materialFolder / "GraphE2E.kbmaterialgraph";
    kb::editor::tests::Require(kb::render::RenderMaterialGraphAssetLoader::SaveGraph(graphPath, graph),
        "KBMAT-GRAPH-0501: E2E test could not save source graph");
    static_cast<void>(scene.Assets().Discover());
    const kb::assets::AssetMetadata* graphMetadata = manager.Registry().FindByPath("/Game/Materials/GraphE2E.kbmaterialgraph");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/Meshes/triangle.obj");
    const kb::assets::AssetMetadata* textureMetadata = manager.Registry().FindByPath("/Game/Textures/graph_albedo.kbtex");
    kb::editor::tests::Require(graphMetadata != nullptr && graphMetadata->type == kb::render::kRenderMaterialGraphAssetType, "KBMAT-GRAPH-0501: source graph metadata missing");
    kb::editor::tests::Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "KBMAT-GRAPH-0501: mesh metadata missing");
    kb::editor::tests::Require(textureMetadata != nullptr && textureMetadata->type == "RenderTexture", "KBMAT-GRAPH-0501: texture metadata missing");
    const kb::assets::AssetId graphAssetId = graphMetadata->id;
    const kb::assets::AssetId meshAssetId = meshMetadata->id;
    const kb::assets::AssetId textureAssetId = textureMetadata->id;

    kb::editor::EditorMaterialAssetAuthoring authoring{ scene, browser, console };
    kb::editor::tests::Require(authoring.CreateMaterialFromGraph(graphAssetId), "KBMAT-GRAPH-0501: Create Material From Graph did not produce graph-backed material");
    const kb::assets::AssetMetadata* graphMaterialMetadata = manager.Registry().FindByPath("/Game/Materials/GraphE2EMaterial.kbmat");
    kb::editor::tests::Require(graphMaterialMetadata != nullptr && graphMaterialMetadata->type == "RenderMaterial", "KBMAT-GRAPH-0501: generated graph-backed material metadata missing");
    const kb::assets::AssetId graphMaterialId = graphMaterialMetadata->id;

    std::optional<kb::render::RenderMaterialAssetData> graphMaterial = kb::editor::EditorMaterialAssetGateway::Read(scene, graphMaterialId);
    kb::editor::tests::Require(graphMaterial.has_value(), "KBMAT-GRAPH-0501: generated graph-backed material could not be read");
    graphMaterial->desc.albedoTextureAssetId = 0U;
    const auto oldAlbedo = std::remove_if(graphMaterial->graphParameterValues.begin(), graphMaterial->graphParameterValues.end(), [](const kb::render::RenderMaterialGraphParameterValue& value) {
        return value.stableId == "albedo";
    });
    graphMaterial->graphParameterValues.erase(oldAlbedo, graphMaterial->graphParameterValues.end());
    graphMaterial->graphParameterValues.push_back(kb::render::RenderMaterialGraphParameterValue{
        .stableId = "albedo",
        .type = kb::render::RenderMaterialParameterType::Texture,
        .assetId = textureAssetId.value,
    });
    kb::editor::tests::Require(kb::editor::EditorMaterialAssetGateway::WriteExisting(scene, graphMaterialId, *graphMaterial), "KBMAT-GRAPH-0501: edited graph-backed material texture parameter could not be saved");
    static_cast<void>(scene.Assets().Discover());

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "KBMAT-GRAPH-0501 Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshAssetId.value,
    });
    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::AssignMaterial(scene, entity, graphMaterialId), "KBMAT-GRAPH-0501: graph-backed material could not be assigned as primary material");
    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::AssignMaterialSlotOverride(scene, entity, 0U, graphMaterialId), "KBMAT-GRAPH-0501: graph-backed material could not be assigned to Mesh Renderer slot");

    MaterialAuthoringHeadlessSurface surface;
    kb::render::DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    kb::render::Renderer renderer;
    renderer.ReserveRuntimeSceneResources(kb::render::Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 4U,
        .cachedMaterials = 4U,
        .cachedTextures = 4U,
        .frameReferencedMeshes = 4U,
        .frameReferencedMaterials = 4U,
        .frameReferencedTextures = 4U,
        .scenePassSubmitStats = 1U,
        .renderSceneMeshProxies = 8U,
        .renderSceneDrawGroupKeys = 4U,
        .meshResourceSlots = 4U,
        .materialResourceSlots = 4U,
        .textureResourceSlots = 4U,
        .meshBindings = 4U,
        .materialBindings = 4U,
        .textureBindings = 4U,
        .syncMeshProxies = 8U,
        .syncTransformCacheEntries = 8U,
        .syncTransformResolvingEntries = 8U,
    });
    kb::editor::tests::Require(renderer.Initialize(surface, &config), "KBMAT-GRAPH-0501: renderer did not initialize");
    kb::editor::tests::Require(renderer.BeginFrame(), "KBMAT-GRAPH-0501: renderer did not begin frame");
    const kb::render::RenderSceneSubmitDesc desc{
        .target = kb::render::RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = kb::render::RenderViewportDesc{
                .id = kb::render::RenderViewportId{ 1U },
                .extent = kb::render::RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = kb::render::SceneRenderDrawBudget{
            .maxDrawCommands = 4U,
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = kb::render::SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
        .postProcessEnabled = false,
        .selectionMaskEnabled = false,
        .selectionOutlineEnabled = false,
        .gpuDrivenRuntimeDispatchEnabled = false,
    };
    kb::editor::tests::Require(renderer.SubmitScene(scene, desc), "KBMAT-GRAPH-0501: runtime renderer rejected graph-backed material scene");
    const kb::render::SceneRenderSubmitStats submitStats = renderer.LastSceneSubmitStats();
    kb::editor::tests::Require(submitStats.submittedMeshCount == 1U && submitStats.submittedDrawCallCount == 1U, "KBMAT-GRAPH-0501: graph-backed material scene did not submit one mesh/draw call");
    kb::editor::tests::Require(!submitStats.HasMissingResources(), "KBMAT-GRAPH-0501: graph-backed material scene reported missing resources");
    const kb::render::SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const kb::render::RenderResourceRegistry* resources = renderer.SceneResources();
    kb::editor::tests::Require(resourceMap != nullptr && resources != nullptr, "KBMAT-GRAPH-0501: renderer did not expose resource maps");
    const kb::render::RenderMaterialHandle materialHandle = resourceMap->ResolveMaterial(graphMaterialId.value);
    const kb::render::RenderMaterialResource* materialResource = resources->FindMaterial(materialHandle);
    kb::editor::tests::Require(materialHandle.IsValid() && materialResource != nullptr, "KBMAT-GRAPH-0501: graph-backed material did not bind to runtime material resource");
    kb::editor::tests::Require(materialResource->albedoTextureAssetId == textureAssetId.value, "KBMAT-GRAPH-0501: edited graph texture parameter did not reach runtime material binding");
    const kb::render::RenderMaterialDesc errorMaterial = kb::render::RuntimeMaterialResolver::ErrorMaterialDesc();
    kb::editor::tests::Require(!kb::editor::tests::NearlyEqual(materialResource->baseColor[0], errorMaterial.baseColor[0]) ||
            !kb::editor::tests::NearlyEqual(materialResource->baseColor[1], errorMaterial.baseColor[1]),
        "KBMAT-GRAPH-0501: graph-backed material resolved to error material");

    renderer.EndFrame();

    graphMaterial->desc.albedoTextureAssetId = 0U;
    graphMaterial->graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    graphMaterial->graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::TextureSample,
        .positionX = -120,
        .positionY = 40,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "textureSample2",
            .displayName = "Texture Sample 2",
            .textureRole = "normal",
            .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Linear,
            .overrideSupported = true,
        },
    });
    graphMaterial->graph.links.push_back(MakeMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::TextureSample,
        2U,
        "color",
        kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "baseColor"));
    graphMaterial->graphParameterValues.clear();
    graphMaterial->graphParameterValues.push_back(kb::render::RenderMaterialGraphParameterValue{
        .stableId = "textureSample2",
        .type = kb::render::RenderMaterialParameterType::Texture,
        .assetId = textureAssetId.value,
    });
    kb::editor::tests::Require(kb::editor::EditorMaterialAssetGateway::WriteExisting(scene, graphMaterialId, *graphMaterial),
        "KBMAT-GRAPH-0501: local TextureSample preview material could not be saved");
    static_cast<void>(scene.Assets().Discover());

    kb::editor::EditorMaterialPreviewScene preview;
    const kb::scene::Scene& previewScene = preview.SceneFor(scene, graphMaterialId);
    kb::editor::tests::Require(preview.Telemetry().materialLoaded && preview.Telemetry().missingTextureCount == 0U,
        "KBMAT-GRAPH-0501: Material Preview telemetry did not accept local TextureSample texture");
    kb::editor::tests::Require(renderer.BeginFrame(), "KBMAT-GRAPH-0501: renderer did not begin local TextureSample preview frame");
    kb::editor::tests::Require(renderer.SubmitScene(previewScene, desc), "KBMAT-GRAPH-0501: renderer rejected local TextureSample preview scene");
    const kb::render::SceneRenderResourceMap* previewResourceMap = renderer.SceneResourceMap();
    const kb::render::RenderResourceRegistry* previewResources = renderer.SceneResources();
    kb::editor::tests::Require(previewResourceMap != nullptr && previewResources != nullptr, "KBMAT-GRAPH-0501: preview renderer did not expose resource maps");
    const kb::render::RenderMaterialHandle previewMaterialHandle = previewResourceMap->ResolveMaterial(graphMaterialId.value);
    const kb::render::RenderMaterialResource* previewMaterialResource = previewResources->FindMaterial(previewMaterialHandle);
    kb::editor::tests::Require(previewMaterialHandle.IsValid() && previewMaterialResource != nullptr, "KBMAT-GRAPH-0501: local TextureSample preview material did not bind");
    kb::editor::tests::Require(previewMaterialResource->albedoTextureAssetId == textureAssetId.value,
        "KBMAT-GRAPH-0501: Material Preview did not read TextureSample Color -> Material Output Base Color");
    renderer.EndFrame();

    renderer.Shutdown();
    std::filesystem::remove_all(TempRoot(), error);
}

void RunGraphMaterialParameterHotReloadsPreviewAndSceneE2ETest() {
    CleanTempRoot();

    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(scene.Assets().MountProject(TempRoot() / "Project"), "KBMAT-GRAPH-0503: hot reload test could not mount project assets");
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    kb::editor::tests::Require(manager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>()), "KBMAT-GRAPH-0503: hot reload test could not register mesh loader");
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialGraphAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialTypeAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()));

    const std::filesystem::path materialFolder = TempRoot() / "Project" / "Assets" / "Materials";
    const std::filesystem::path meshPath = TempRoot() / "Project" / "Assets" / "Meshes" / "triangle.obj";
    std::error_code error;
    std::filesystem::create_directories(materialFolder, error);
    std::filesystem::create_directories(meshPath.parent_path(), error);
    kb::editor::tests::Require(!error, "KBMAT-GRAPH-0503: hot reload test could not create project folders");
    WriteTriangleObj(meshPath);

    kb::render::RenderMaterialGraphDocument graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    graph.storageModel = "material-graph-asset";
    graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ParameterColor,
        .positionX = -320,
        .positionY = 40,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "baseColorFactor",
            .displayName = "Base Color",
            .defaultValueHint = "0.2 0.35 0.5 1",
            .editorOrder = 10U,
        },
    });
    graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 3U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ParameterScalar,
        .positionX = -320,
        .positionY = 180,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "roughnessFactor",
            .displayName = "Roughness",
            .defaultValueHint = "0.42",
            .hasRange = true,
            .rangeMin = 0.0F,
            .rangeMax = 1.0F,
            .editorOrder = 20U,
        },
    });
    graph.links.push_back(MakeMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::ParameterColor,
        2U,
        "rgba",
        kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "baseColor"));
    graph.links.push_back(MakeMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::ParameterScalar,
        3U,
        "value",
        kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "roughness"));
    const kb::render::RenderMaterialGraphCompileResult compile = kb::render::CompileRenderMaterialGraphToShaderSource(
        graph,
        kb::render::RenderMaterialGraphBuildContext{
            .assetId = 0x0503U,
            .sourcePath = "/Game/Materials/GraphHotReload.kbmaterialgraph",
        });
    kb::editor::tests::Require(compile.Succeeded(), "KBMAT-GRAPH-0503: hot reload graph did not compile");

    const std::filesystem::path graphPath = materialFolder / "GraphHotReload.kbmaterialgraph";
    kb::editor::tests::Require(kb::render::RenderMaterialGraphAssetLoader::SaveGraph(graphPath, graph),
        "KBMAT-GRAPH-0503: hot reload test could not save graph");
    static_cast<void>(scene.Assets().Discover());
    const kb::assets::AssetMetadata* graphMetadata = manager.Registry().FindByPath("/Game/Materials/GraphHotReload.kbmaterialgraph");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/Meshes/triangle.obj");
    kb::editor::tests::Require(graphMetadata != nullptr && graphMetadata->type == kb::render::kRenderMaterialGraphAssetType, "KBMAT-GRAPH-0503: graph metadata missing");
    kb::editor::tests::Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "KBMAT-GRAPH-0503: mesh metadata missing");
    const kb::assets::AssetId graphAssetId = graphMetadata->id;
    const kb::assets::AssetId meshAssetId = meshMetadata->id;

    kb::editor::EditorMaterialAssetAuthoring authoring{ scene, browser, console };
    kb::editor::tests::Require(authoring.CreateMaterialFromGraph(graphAssetId), "KBMAT-GRAPH-0503: Create Material From Graph did not produce material");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/Materials/GraphHotReloadMaterial.kbmat");
    kb::editor::tests::Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "KBMAT-GRAPH-0503: generated material metadata missing");
    const kb::assets::AssetId materialId = materialMetadata->id;

    std::optional<kb::render::RenderMaterialAssetData> material = kb::editor::EditorMaterialAssetGateway::Read(scene, materialId);
    kb::editor::tests::Require(material.has_value(), "KBMAT-GRAPH-0503: generated material could not be read");
    material->desc.baseColor[0] = 0.2F;
    material->desc.baseColor[1] = 0.35F;
    material->desc.baseColor[2] = 0.5F;
    material->desc.baseColor[3] = 1.0F;
    material->desc.roughnessFactor = 0.42F;
    kb::editor::tests::Require(kb::editor::EditorMaterialAssetGateway::WriteExisting(scene, materialId, *material),
        "KBMAT-GRAPH-0503: initial graph-backed material parameters could not be saved");

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "KBMAT-GRAPH-0503 Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshAssetId.value,
    });
    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::AssignMaterial(scene, entity, materialId),
        "KBMAT-GRAPH-0503: graph-backed material could not be assigned to scene mesh");

    MaterialAuthoringHeadlessSurface surface;
    kb::render::DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    kb::render::Renderer renderer;
    ReserveMaterialAuthoringRuntimeResources(renderer);
    kb::editor::tests::Require(renderer.Initialize(surface, &config), "KBMAT-GRAPH-0503: renderer did not initialize");
    const kb::render::RenderSceneSubmitDesc sceneDesc = MaterialAuthoringSubmitDesc(0U);
    const kb::render::RenderSceneSubmitDesc previewDesc = MaterialAuthoringSubmitDesc(0U);

    kb::editor::EditorMaterialPreviewScene preview;
    const kb::scene::Scene& firstPreviewScene = preview.SceneFor(scene, materialId);
    const std::uint64_t firstPreviewRevision = preview.Revision();

    kb::editor::tests::Require(renderer.BeginFrame(), "KBMAT-GRAPH-0503: renderer did not begin first scene frame");
    kb::editor::tests::Require(renderer.SubmitScene(scene, sceneDesc), "KBMAT-GRAPH-0503: scene renderer rejected first graph-backed frame");
    const kb::render::SceneRenderResourceMap* sceneResourceMap = renderer.SceneResourceMap();
    const kb::render::RenderResourceRegistry* sceneResources = renderer.SceneResources();
    kb::editor::tests::Require(sceneResourceMap != nullptr && sceneResources != nullptr, "KBMAT-GRAPH-0503: scene renderer did not expose resources");
    const kb::render::RenderMaterialHandle firstSceneHandle = sceneResourceMap->ResolveMaterial(materialId.value);
    const kb::render::RenderMaterialResource* firstSceneMaterial = sceneResources->FindMaterial(firstSceneHandle);
    kb::editor::tests::Require(firstSceneHandle.IsValid() && firstSceneMaterial != nullptr, "KBMAT-GRAPH-0503: scene material did not bind before edit");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(firstSceneMaterial->baseColor[0], 0.2F) &&
            kb::editor::tests::NearlyEqual(firstSceneMaterial->roughnessFactor, 0.42F),
        "KBMAT-GRAPH-0503: scene material did not use initial graph-backed parameters");
    renderer.EndFrame();

    kb::editor::tests::Require(renderer.BeginFrame(), "KBMAT-GRAPH-0503: renderer did not begin first preview frame");
    kb::editor::tests::Require(renderer.SubmitScene(firstPreviewScene, previewDesc), "KBMAT-GRAPH-0503: preview renderer rejected first graph-backed frame");
    const kb::render::SceneRenderResourceMap* previewResourceMap = renderer.SceneResourceMap();
    const kb::render::RenderResourceRegistry* previewResources = renderer.SceneResources();
    kb::editor::tests::Require(previewResourceMap != nullptr && previewResources != nullptr, "KBMAT-GRAPH-0503: preview renderer did not expose resources");
    const kb::render::RenderMaterialHandle firstPreviewHandle = previewResourceMap->ResolveMaterial(materialId.value);
    const kb::render::RenderMaterialResource* firstPreviewMaterial = previewResources->FindMaterial(firstPreviewHandle);
    kb::editor::tests::Require(firstPreviewHandle.IsValid() && firstPreviewMaterial != nullptr, "KBMAT-GRAPH-0503: preview material did not bind before edit");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(firstPreviewMaterial->baseColor[0], 0.2F) &&
            kb::editor::tests::NearlyEqual(firstPreviewMaterial->roughnessFactor, 0.42F),
        "KBMAT-GRAPH-0503: preview material did not use initial graph-backed parameters");
    renderer.EndFrame();

    material->desc.baseColor[0] = 0.72F;
    material->desc.baseColor[1] = 0.18F;
    material->desc.baseColor[2] = 0.31F;
    material->desc.roughnessFactor = 0.81F;
    kb::editor::tests::Require(kb::editor::EditorMaterialAssetGateway::WriteExisting(scene, materialId, *material),
        "KBMAT-GRAPH-0503: edited graph-backed material parameters could not be saved");

    const kb::scene::Scene& reloadedPreviewScene = preview.SceneFor(scene, materialId);
    kb::editor::tests::Require(preview.Revision() != firstPreviewRevision, "KBMAT-GRAPH-0503: Material Preview did not rebuild after graph-backed parameter save");

    kb::editor::tests::Require(renderer.BeginFrame(), "KBMAT-GRAPH-0503: renderer did not begin preview hot reload frame");
    kb::editor::tests::Require(renderer.SubmitScene(reloadedPreviewScene, previewDesc), "KBMAT-GRAPH-0503: preview renderer rejected hot reload frame");
    const kb::render::RenderMaterialHandle reloadedPreviewHandle = previewResourceMap->ResolveMaterial(materialId.value);
    const kb::render::RenderMaterialResource* reloadedPreviewMaterial = previewResources->FindMaterial(reloadedPreviewHandle);
    kb::editor::tests::Require(reloadedPreviewHandle.IsValid() && reloadedPreviewMaterial != nullptr && reloadedPreviewHandle != firstPreviewHandle,
        "KBMAT-GRAPH-0503: Material Preview material binding did not hot reload after graph-backed parameter save");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(reloadedPreviewMaterial->baseColor[0], 0.72F) &&
            kb::editor::tests::NearlyEqual(reloadedPreviewMaterial->roughnessFactor, 0.81F),
        "KBMAT-GRAPH-0503: Material Preview did not render edited graph-backed parameters");
    const kb::render::Renderer::RuntimeSceneResourceStats previewRuntimeStats = renderer.RuntimeResourceStats();
    kb::editor::tests::Require(previewRuntimeStats.materialErrorCount == 0U,
        "KBMAT-GRAPH-0503: Material Preview hot reload should stay free of material errors");
    renderer.EndFrame();

    kb::editor::tests::Require(renderer.BeginFrame(), "KBMAT-GRAPH-0503: renderer did not begin scene hot reload frame");
    kb::editor::tests::Require(renderer.SubmitScene(scene, sceneDesc), "KBMAT-GRAPH-0503: scene renderer rejected hot reload frame");
    const kb::render::RenderMaterialHandle reloadedSceneHandle = sceneResourceMap->ResolveMaterial(materialId.value);
    const kb::render::RenderMaterialResource* reloadedSceneMaterial = sceneResources->FindMaterial(reloadedSceneHandle);
    kb::editor::tests::Require(reloadedSceneHandle.IsValid() && reloadedSceneMaterial != nullptr,
        "KBMAT-GRAPH-0503: Scene View did not bind a live graph-backed material resource after hot reload");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(reloadedSceneMaterial->baseColor[0], 0.72F) &&
            kb::editor::tests::NearlyEqual(reloadedSceneMaterial->roughnessFactor, 0.81F),
        "KBMAT-GRAPH-0503: Scene View material resource did not use edited graph-backed parameters");
    const kb::render::Renderer::RuntimeSceneResourceStats sceneRuntimeStats = renderer.RuntimeResourceStats();
    kb::editor::tests::Require(sceneRuntimeStats.materialErrorCount == 0U,
        "KBMAT-GRAPH-0503: Scene View hot reload should stay free of material errors");
    renderer.EndFrame();

    renderer.Shutdown();
    std::filesystem::remove_all(TempRoot(), error);
}

void RunGraphMaterialSchemaMigrationEditorE2ETest() {
    CleanTempRoot();

    kb::scene::Scene scene;
    kb::editor::tests::Require(scene.Assets().MountProject(TempRoot() / "Project"), "KBMAT-GRAPH-0504: schema migration test could not mount project assets");
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialTypeAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()));

    const std::filesystem::path materialFolder = TempRoot() / "Project" / "Assets" / "Materials";
    std::error_code error;
    std::filesystem::create_directories(materialFolder, error);
    kb::editor::tests::Require(!error, "KBMAT-GRAPH-0504: schema migration test could not create material folder");
    const std::filesystem::path typePath = materialFolder / "SchemaMigrationType.kbmaterialtype";
    const std::filesystem::path materialPath = materialFolder / "SchemaMigrationMaterial.kbmat";

    auto makeType = [](std::uint32_t version) {
        kb::render::RenderMaterialTypeDocument document = kb::render::GetBuiltInPbrMaterialTypeDocument();
        document.stableTypeId = "graph.schemaMigration";
        document.version = version;
        document.displayName = "Schema Migration";
        document.schema.typeName = document.stableTypeId;
        document.schema.typeVersion = version;
        document.schema.parameters.clear();
        document.schema.textureSlots.clear();
        return document;
    };

    kb::render::RenderMaterialTypeDocument typeV1 = makeType(1U);
    typeV1.schema.parameters = {
        kb::render::RenderMaterialParameterSchema{
            .name = "tintColor",
            .displayName = "Tint Color",
            .type = kb::render::RenderMaterialParameterType::Color,
            .group = kb::render::RenderMaterialParameterGroup::Surface,
            .defaultValueHint = "1 1 1 1",
            .editorOrder = 10U,
        },
        kb::render::RenderMaterialParameterSchema{
            .name = "wear",
            .displayName = "Wear",
            .type = kb::render::RenderMaterialParameterType::Scalar,
            .group = kb::render::RenderMaterialParameterGroup::Surface,
            .defaultValueHint = "0.1",
            .editorOrder = 20U,
        },
        kb::render::RenderMaterialParameterSchema{
            .name = "obsoleteParam",
            .displayName = "Obsolete",
            .type = kb::render::RenderMaterialParameterType::Scalar,
            .group = kb::render::RenderMaterialParameterGroup::Advanced,
            .defaultValueHint = "0.0",
            .editorOrder = 30U,
        },
    };
    kb::editor::tests::Require(kb::render::RenderMaterialTypeAssetLoader::SaveType(typePath, typeV1),
        "KBMAT-GRAPH-0504: schema migration test could not save v1 Material Type");
    static_cast<void>(scene.Assets().Discover());
    const kb::assets::AssetMetadata* typeMetadata = manager.Registry().FindByPath("/Game/Materials/SchemaMigrationType.kbmaterialtype");
    kb::editor::tests::Require(typeMetadata != nullptr && typeMetadata->type == kb::render::kRenderMaterialTypeAssetType,
        "KBMAT-GRAPH-0504: schema migration test did not discover v1 Material Type");

    kb::render::RenderMaterialAssetData material{};
    material.materialType = typeV1.stableTypeId;
    material.materialTypeVersion = typeV1.version;
    material.hasExplicitMaterialType = true;
    material.hasExplicitMaterialTypeVersion = true;
    material.materialTypeAssetId = typeMetadata->id.value;
    material.materialTypeAssetPath = typeMetadata->virtualPath.generic_string();
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    material.graph.storageModel = "inline-kbmat";
    material.graphParameterValues = {
        kb::render::RenderMaterialGraphParameterValue{
            .stableId = "tintColor",
            .type = kb::render::RenderMaterialParameterType::Color,
            .numbers = { 0.2F, 0.4F, 0.6F, 1.0F },
        },
        kb::render::RenderMaterialGraphParameterValue{
            .stableId = "wear",
            .type = kb::render::RenderMaterialParameterType::Scalar,
            .numbers = { 0.7F, 0.0F, 0.0F, 0.0F },
        },
        kb::render::RenderMaterialGraphParameterValue{
            .stableId = "obsoleteParam",
            .type = kb::render::RenderMaterialParameterType::Scalar,
            .numbers = { 0.9F, 0.0F, 0.0F, 0.0F },
        },
    };
    kb::editor::tests::Require(kb::render::RenderMaterialAssetWriter::Save(materialPath, material),
        "KBMAT-GRAPH-0504: schema migration test could not save graph-backed material");
    static_cast<void>(scene.Assets().Discover());
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/Materials/SchemaMigrationMaterial.kbmat");
    kb::editor::tests::Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial",
        "KBMAT-GRAPH-0504: schema migration test did not discover material");
    const kb::assets::AssetId materialId = materialMetadata->id;

    kb::render::RenderMaterialTypeDocument typeV2 = makeType(2U);
    typeV2.schema.parameters = {
        kb::render::RenderMaterialParameterSchema{
            .name = "tintColor",
            .displayName = "Tint Color",
            .type = kb::render::RenderMaterialParameterType::Color,
            .group = kb::render::RenderMaterialParameterGroup::Surface,
            .defaultValueHint = "1 1 1 1",
            .editorOrder = 10U,
        },
        kb::render::RenderMaterialParameterSchema{
            .name = "wear",
            .displayName = "Wear Color",
            .type = kb::render::RenderMaterialParameterType::Color,
            .group = kb::render::RenderMaterialParameterGroup::Surface,
            .defaultValueHint = "0.1 0.2 0.3 1",
            .editorOrder = 20U,
        },
        kb::render::RenderMaterialParameterSchema{
            .name = "edgeWear",
            .displayName = "Edge Wear",
            .type = kb::render::RenderMaterialParameterType::Scalar,
            .group = kb::render::RenderMaterialParameterGroup::Surface,
            .defaultValueHint = "0.25",
            .editorOrder = 30U,
        },
    };
    kb::editor::tests::Require(kb::render::RenderMaterialTypeAssetLoader::SaveType(typePath, typeV2),
        "KBMAT-GRAPH-0504: schema migration test could not save v2 Material Type");
    static_cast<void>(scene.Assets().Discover());

    const kb::render::RenderMaterialSchemaRefreshResult refresh = kb::render::RefreshRenderMaterialGraphBackedMaterialSchema(material, typeV2);
    std::vector<std::string> refreshDiagnostics;
    refreshDiagnostics.reserve(refresh.diagnostics.size());
    for (const kb::render::RenderMaterialSchemaRefreshDiagnostic& diagnostic : refresh.diagnostics) {
        refreshDiagnostics.push_back("schema_refresh: " + diagnostic.message);
    }

    kb::editor::MaterialEditorState materialEditor;
    materialEditor.Open(materialId, material, typeV2.schema);
    materialEditor.SetWorkingCopy(refresh.material);
    materialEditor.SetDiagnostics(refreshDiagnostics, false);
    const kb::editor::MaterialEditorState& editor = materialEditor;
    kb::editor::tests::Require(editor.WorkingCopy().has_value(), "KBMAT-GRAPH-0504: Material Editor did not keep a migrated working copy");
    const kb::render::RenderMaterialAssetData& migrated = *editor.WorkingCopy();
    kb::editor::tests::Require(editor.Dirty(), "KBMAT-GRAPH-0504: schema refresh should mark Material Editor working copy dirty");
    kb::editor::tests::Require(migrated.materialType == "graph.schemaMigration" && migrated.materialTypeVersion == 2U,
        "KBMAT-GRAPH-0504: schema refresh did not update material type version");

    const kb::render::RenderMaterialGraphParameterValue* tint = FindGraphParameterValue(migrated, "tintColor");
    const kb::render::RenderMaterialGraphParameterValue* wear = FindGraphParameterValue(migrated, "wear");
    const kb::render::RenderMaterialGraphParameterValue* edgeWear = FindGraphParameterValue(migrated, "edgeWear");
    kb::editor::tests::Require(tint != nullptr && tint->type == kb::render::RenderMaterialParameterType::Color &&
            kb::editor::tests::NearlyEqual(tint->numbers[2], 0.6F),
        "KBMAT-GRAPH-0504: schema refresh did not preserve matching parameter by stable id");
    kb::editor::tests::Require(wear != nullptr && wear->type == kb::render::RenderMaterialParameterType::Color &&
            kb::editor::tests::NearlyEqual(wear->numbers[0], 0.1F) &&
            kb::editor::tests::NearlyEqual(wear->numbers[2], 0.3F),
        "KBMAT-GRAPH-0504: schema refresh did not reset changed-type parameter to v2 default");
    kb::editor::tests::Require(edgeWear != nullptr && edgeWear->type == kb::render::RenderMaterialParameterType::Scalar &&
            kb::editor::tests::NearlyEqual(edgeWear->numbers[0], 0.25F),
        "KBMAT-GRAPH-0504: schema refresh did not add default for new graph parameter");
    kb::editor::tests::Require(FindGraphParameterValue(migrated, "obsoleteParam") == nullptr,
        "KBMAT-GRAPH-0504: schema refresh did not remove obsolete graph parameter value");
    kb::editor::tests::Require(!editor.DiagnosticsHaveError() && editor.Diagnostics().size() >= 4U,
        "KBMAT-GRAPH-0504: Material Editor did not expose schema refresh warnings");
    const auto diagnosticContains = [&editor](std::string_view text) {
        return std::ranges::any_of(editor.Diagnostics(), [text](const std::string& diagnostic) {
            return diagnostic.find(text) != std::string::npos;
        });
    };
    kb::editor::tests::Require(diagnosticContains("version 2") && diagnosticContains("edgeWear") &&
            diagnosticContains("wear") && diagnosticContains("obsoleteParam"),
        "KBMAT-GRAPH-0504: Material Editor schema refresh diagnostics did not name changed, added and removed parameters");

    std::filesystem::remove_all(TempRoot(), error);
}

void RunExtractEmbeddedMaterialToMaterialAssetTest() {
    CleanTempRoot();

    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(scene.Assets().MountProject(TempRoot() / "Project"), "Embedded material extraction test could not mount project assets");

    kb::assets::AssetManager& manager = scene.Assets().Manager();
    kb::editor::tests::Require(manager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>()), "Embedded material extraction test could not register mesh loader");
    kb::editor::tests::Require(manager.RegisterLoader(std::make_unique<kb::render::RenderTextureAssetLoader>()), "Embedded material extraction test could not register texture loader");

    const std::filesystem::path meshFolder = TempRoot() / "Project" / "Assets" / "Meshes";
    WriteEmbeddedMaterialGltfFixture(meshFolder);
    WriteTexture(meshFolder / "Textures" / "albedo.kbtex", 20U, 40U, 80U);
    WriteTexture(meshFolder / "Textures" / "metallic_roughness.kbtex", 0U, 90U, 180U);
    WriteTexture(meshFolder / "Textures" / "normal.kbtex", 128U, 128U, 255U);
    WriteTexture(meshFolder / "Textures" / "occlusion.kbtex", 200U, 200U, 200U);
    WriteTexture(meshFolder / "Textures" / "emissive.kbtex", 10U, 20U, 30U);
    kb::editor::tests::Require(scene.Assets().Discover() >= 6U, "Embedded material extraction test did not discover mesh and texture assets");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/Meshes/embedded.gltf");
    const kb::assets::AssetMetadata* albedoMetadata = manager.Registry().FindByPath("/Game/Meshes/Textures/albedo.kbtex");
    const kb::assets::AssetMetadata* metallicRoughnessMetadata = manager.Registry().FindByPath("/Game/Meshes/Textures/metallic_roughness.kbtex");
    const kb::assets::AssetMetadata* normalMetadata = manager.Registry().FindByPath("/Game/Meshes/Textures/normal.kbtex");
    const kb::assets::AssetMetadata* occlusionMetadata = manager.Registry().FindByPath("/Game/Meshes/Textures/occlusion.kbtex");
    const kb::assets::AssetMetadata* emissiveMetadata = manager.Registry().FindByPath("/Game/Meshes/Textures/emissive.kbtex");
    kb::editor::tests::Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Embedded material extraction test did not discover glTF mesh");
    kb::editor::tests::Require(albedoMetadata != nullptr && albedoMetadata->type == "RenderTexture", "Embedded material extraction test did not discover albedo texture");
    kb::editor::tests::Require(metallicRoughnessMetadata != nullptr && metallicRoughnessMetadata->type == "RenderTexture", "Embedded material extraction test did not discover metallic-roughness texture");
    kb::editor::tests::Require(normalMetadata != nullptr && normalMetadata->type == "RenderTexture", "Embedded material extraction test did not discover normal texture");
    kb::editor::tests::Require(occlusionMetadata != nullptr && occlusionMetadata->type == "RenderTexture", "Embedded material extraction test did not discover occlusion texture");
    kb::editor::tests::Require(emissiveMetadata != nullptr && emissiveMetadata->type == "RenderTexture", "Embedded material extraction test did not discover emissive texture");
    const kb::assets::AssetId meshAssetId = meshMetadata->id;
    const kb::assets::AssetId albedoAssetId = albedoMetadata->id;
    const kb::assets::AssetId metallicRoughnessAssetId = metallicRoughnessMetadata->id;
    const kb::assets::AssetId normalAssetId = normalMetadata->id;
    const kb::assets::AssetId occlusionAssetId = occlusionMetadata->id;
    const kb::assets::AssetId emissiveAssetId = emissiveMetadata->id;

    kb::editor::EditorEmbeddedMaterialExtractor extractor{ scene, browser, console };
    const kb::editor::EditorEmbeddedMaterialExtractionResult result = extractor.Extract(meshAssetId);
    kb::editor::tests::Require(result.Succeeded(), "Embedded material extraction did not produce a material asset");
    kb::editor::tests::Require(result.slots.size() == 1U && result.slots[0].slotIndex == 0U, "Embedded material extraction returned wrong slot mapping");
    kb::editor::tests::Require(result.diagnostics.empty(), "Embedded material extraction should resolve all MVP texture paths");
    kb::editor::tests::Require(console.Count(kb::editor::EditorConsoleLevel::Info) == 1U, "Embedded material extraction should log one success message");
    kb::editor::tests::Require(console.Count(kb::editor::EditorConsoleLevel::Warning) == 0U, "Embedded material extraction should not warn when all MVP textures resolve");
    kb::editor::tests::Require(browser.SelectedAsset() == result.slots[0].materialAssetId, "Embedded material extraction did not select the extracted material");

    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().Find(result.slots[0].materialAssetId);
    kb::editor::tests::Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Embedded material extraction did not register a RenderMaterial asset");
    kb::editor::tests::Require(materialMetadata->virtualPath == "/Game/Meshes/painted_metal.kbmat", "Embedded material extraction wrote an unexpected material filename");

    const std::optional<kb::render::RenderMaterialAssetData> material = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialMetadata->physicalPath);
    kb::editor::tests::Require(material.has_value(), "Extracted material could not be loaded from disk");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(material->desc.baseColor[0], 0.2F), "Extracted material lost base color");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(material->desc.baseColor[3], 0.6F), "Extracted material lost alpha");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(material->desc.metallicFactor, 0.7F), "Extracted material lost metallic factor");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(material->desc.roughnessFactor, 0.35F), "Extracted material lost roughness factor");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(material->desc.normalScale, 0.75F), "Extracted material lost normal scale");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(material->desc.occlusionStrength, 0.6F), "Extracted material lost occlusion strength");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(material->desc.emissiveColor[0], 0.1F) && kb::editor::tests::NearlyEqual(material->desc.emissiveColor[1], 0.2F) && kb::editor::tests::NearlyEqual(material->desc.emissiveColor[2], 0.3F), "Extracted material lost emissive color");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(material->desc.emissiveStrength, 2.5F), "Extracted material lost emissive strength");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(material->desc.uvTiling[0], 2.0F) && kb::editor::tests::NearlyEqual(material->desc.uvTiling[1], 3.0F), "Extracted material lost embedded UV tiling");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(material->desc.uvOffset[0], 0.25F) && kb::editor::tests::NearlyEqual(material->desc.uvOffset[1], 0.5F), "Extracted material lost embedded UV offset");
    kb::editor::tests::Require(material->desc.alphaMode == kb::render::RenderMaterialAlphaMode::Blend, "Extracted material lost alpha mode");
    kb::editor::tests::Require(material->desc.doubleSided, "Extracted material lost double-sided flag");
    kb::editor::tests::Require(material->desc.albedoTextureAssetId == albedoAssetId.value, "Extracted material did not map albedo texture path to asset id");
    kb::editor::tests::Require(material->desc.metallicRoughnessTextureAssetId == metallicRoughnessAssetId.value, "Extracted material did not map metallic-roughness texture path to asset id");
    kb::editor::tests::Require(material->desc.normalTextureAssetId == normalAssetId.value, "Extracted material did not map normal texture path to asset id");
    kb::editor::tests::Require(material->desc.occlusionTextureAssetId == occlusionAssetId.value, "Extracted material did not map occlusion texture path to asset id");
    kb::editor::tests::Require(material->desc.emissiveTextureAssetId == emissiveAssetId.value, "Extracted material did not map emissive texture path to asset id");
    kb::editor::tests::Require(material->albedoTexturePath.empty(), "Extracted material kept resolved albedo texture path");
    kb::editor::tests::Require(material->metallicRoughnessTexturePath.empty(), "Extracted material kept resolved metallic-roughness texture path");
    kb::editor::tests::Require(material->normalTexturePath.empty(), "Extracted material kept resolved normal texture path");
    kb::editor::tests::Require(material->occlusionTexturePath.empty(), "Extracted material kept resolved occlusion texture path");
    kb::editor::tests::Require(material->emissiveTexturePath.empty(), "Extracted material kept resolved emissive texture path");

    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
}

void RunExtractedGltfMaterialRendersThroughRuntimeTest() {
    CleanTempRoot();

    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(scene.Assets().MountProject(TempRoot() / "Project"), "Extracted glTF material runtime render test could not mount project assets");

    kb::assets::AssetManager& manager = scene.Assets().Manager();
    kb::editor::tests::Require(manager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>()), "Extracted glTF material runtime render test could not register mesh loader");
    kb::editor::tests::Require(manager.RegisterLoader(std::make_unique<kb::render::RenderTextureAssetLoader>()), "Extracted glTF material runtime render test could not register texture loader");

    const std::filesystem::path meshFolder = TempRoot() / "Project" / "Assets" / "Meshes";
    WriteEmbeddedMaterialGltfFixture(meshFolder, "MASK");
    WriteTexture(meshFolder / "Textures" / "albedo.kbtex", 20U, 40U, 80U);
    WriteTexture(meshFolder / "Textures" / "metallic_roughness.kbtex", 0U, 90U, 180U);
    WriteTexture(meshFolder / "Textures" / "normal.kbtex", 128U, 128U, 255U);
    WriteTexture(meshFolder / "Textures" / "occlusion.kbtex", 200U, 200U, 200U);
    WriteTexture(meshFolder / "Textures" / "emissive.kbtex", 10U, 20U, 30U);
    kb::editor::tests::Require(scene.Assets().Discover() >= 6U, "Extracted glTF material runtime render test did not discover mesh and textures");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/Meshes/embedded.gltf");
    kb::editor::tests::Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Extracted glTF material runtime render test did not discover the glTF mesh");
    const kb::assets::AssetId meshAssetId = meshMetadata->id;

    kb::editor::EditorEmbeddedMaterialExtractor extractor{ scene, browser, console };
    const kb::editor::EditorEmbeddedMaterialExtractionResult extraction = extractor.Extract(meshAssetId);
    kb::editor::tests::Require(extraction.Succeeded(), "Extracted glTF material runtime render test could not extract a .kbmat");
    kb::editor::tests::Require(extraction.slots.size() == 1U, "Extracted glTF material runtime render test got an unexpected extraction slot count");
    kb::editor::tests::Require(extraction.diagnostics.empty(), "Extracted glTF material runtime render test produced extraction diagnostics");

    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().Find(extraction.slots[0].materialAssetId);
    kb::editor::tests::Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Extracted glTF material runtime render test did not register the extracted .kbmat");
    const std::optional<kb::render::RenderMaterialAssetData> extractedMaterial = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialMetadata->physicalPath);
    kb::editor::tests::Require(extractedMaterial.has_value(), "Extracted glTF material runtime render test could not reload the extracted .kbmat");
    kb::editor::tests::Require(extractedMaterial->desc.albedoTextureAssetId != 0U && extractedMaterial->desc.normalTextureAssetId != 0U &&
            extractedMaterial->desc.metallicRoughnessTextureAssetId != 0U && extractedMaterial->desc.occlusionTextureAssetId != 0U &&
            extractedMaterial->desc.emissiveTextureAssetId != 0U,
        "Extracted glTF material runtime render test did not persist resolved texture asset ids");

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Extracted Material Runtime Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshAssetId.value,
        .materialAssetId = extraction.slots[0].materialAssetId.value,
    });
    kb::editor::tests::Require(scene.Components().MeshRenderers().Has(entity), "Extracted glTF material runtime render test did not attach MeshRenderer");
    std::uint32_t meshRendererVisitorCount = 0U;
    scene.Components().Visitors().ForEachMeshRenderer(
        [](kb::scene::SceneEntity, const kb::scene::TransformComponent&, const kb::scene::MeshRendererComponent& renderer, void* context) {
            if (renderer.meshAssetId != 0U && renderer.materialAssetId != 0U) {
                ++*static_cast<std::uint32_t*>(context);
            }
        },
        &meshRendererVisitorCount);
    kb::editor::tests::Require(meshRendererVisitorCount == 1U, "Extracted glTF material runtime render test MeshRenderer is not visible to ECS iteration");

    MaterialAuthoringHeadlessSurface surface;
    kb::render::DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    kb::render::Renderer renderer;
    renderer.ReserveRuntimeSceneResources(kb::render::Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 2U,
        .cachedMaterials = 2U,
        .cachedTextures = 5U,
        .frameReferencedMeshes = 2U,
        .frameReferencedMaterials = 2U,
        .frameReferencedTextures = 5U,
        .scenePassSubmitStats = 2U,
        .renderSceneMeshProxies = 2U,
        .renderSceneDrawGroupKeys = 2U,
        .meshResourceSlots = 2U,
        .materialResourceSlots = 2U,
        .textureResourceSlots = 5U,
        .meshBindings = 2U,
        .materialBindings = 2U,
        .textureBindings = 5U,
        .syncMeshProxies = 2U,
        .syncTransformCacheEntries = 4U,
        .syncTransformResolvingEntries = 4U,
    });
    kb::editor::tests::Require(renderer.Initialize(surface, &config), "Extracted glTF material runtime renderer did not initialize");
    kb::editor::tests::Require(renderer.BeginFrame(), "Extracted glTF material runtime renderer did not begin a frame");

    const kb::render::RenderSceneSubmitDesc desc{
        .target = kb::render::RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = kb::render::RenderViewportDesc{
                .id = kb::render::RenderViewportId{ 1U },
                .extent = kb::render::RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .meshPassMode = kb::render::SceneRenderMeshPassMode::OpaqueAndTransparent,
        .shadowPassEnabled = false,
        .postProcessEnabled = false,
        .selectionMaskEnabled = false,
        .selectionOutlineEnabled = false,
        .gpuDrivenRuntimeDispatchEnabled = false,
    };
    kb::editor::tests::Require(renderer.SubmitScene(scene, desc), "Extracted glTF material runtime renderer rejected the extracted material scene");

    const kb::render::SceneRenderSubmitStats submitStats = renderer.LastSceneSubmitStats();
    if (submitStats.visibleMeshCount != 1U) {
        const kb::render::Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
        std::cerr
            << "Extracted glTF material runtime render stats: visible=" << submitStats.visibleMeshCount
            << " groups=" << submitStats.visibleDrawGroupCount
            << " culled=" << submitStats.culledInstanceCount
            << " submittedMesh=" << submitStats.submittedMeshCount
            << " drawCalls=" << submitStats.submittedDrawCallCount
            << " missingMeshBinding=" << submitStats.missingMeshBindingCount
            << " missingMeshResource=" << submitStats.missingMeshResourceCount
            << " unsupportedVertex=" << submitStats.unsupportedMeshVertexFormatCount
            << " missingMaterialBinding=" << submitStats.missingMaterialBindingCount
            << " missingMaterialResource=" << submitStats.missingMaterialResourceCount
            << " missingTextureBinding=" << submitStats.missingTextureBindingCount
            << " missingTextureResource=" << submitStats.missingTextureResourceCount
            << " renderSceneMeshes=" << runtimeStats.renderSceneMeshProxyCount
            << " renderSceneCount=" << runtimeStats.renderSceneCount
            << " cachedMeshes=" << runtimeStats.cachedMeshCount
            << " cachedMaterials=" << runtimeStats.cachedMaterialCount
            << " cachedTextures=" << runtimeStats.cachedTextureCount << '\n';
    }
    kb::editor::tests::Require(submitStats.visibleMeshCount == 1U, "Extracted glTF material runtime render did not keep the mesh visible");
    kb::editor::tests::Require(submitStats.submittedMeshCount == 1U, "Extracted glTF material runtime render did not submit the mesh");
    kb::editor::tests::Require(submitStats.submittedDrawCallCount == 1U, "Extracted glTF material runtime render did not emit one draw call");
    kb::editor::tests::Require(!submitStats.HasMissingResources(), "Extracted glTF material runtime render reported missing resources");

    const kb::render::SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const kb::render::RenderResourceRegistry* resources = renderer.SceneResources();
    kb::editor::tests::Require(resourceMap != nullptr && resources != nullptr, "Extracted glTF material runtime render could not inspect runtime resources");
    const kb::render::RenderMeshHandle meshHandle = resourceMap->ResolveMesh(meshAssetId.value);
    const kb::render::RenderMaterialHandle materialHandle = resourceMap->ResolveMaterial(extraction.slots[0].materialAssetId.value);
    const kb::render::RenderMaterialResource* materialResource = resources->FindMaterial(materialHandle);
    kb::editor::tests::Require(meshHandle.IsValid(), "Extracted glTF material runtime render did not bind the glTF mesh");
    kb::editor::tests::Require(materialHandle.IsValid() && materialResource != nullptr, "Extracted glTF material runtime render did not bind the extracted .kbmat");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(materialResource->baseColor[0], extractedMaterial->desc.baseColor[0]) &&
            kb::editor::tests::NearlyEqual(materialResource->baseColor[3], extractedMaterial->desc.baseColor[3]),
        "Extracted glTF material runtime resource did not preserve extracted base color");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(materialResource->uvTiling[0], 2.0F) &&
            kb::editor::tests::NearlyEqual(materialResource->uvTiling[1], 3.0F) &&
            kb::editor::tests::NearlyEqual(materialResource->uvOffset[0], 0.25F) &&
            kb::editor::tests::NearlyEqual(materialResource->uvOffset[1], 0.5F),
        "Extracted glTF material runtime resource did not preserve extracted UV transform");

    const kb::render::Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
    kb::editor::tests::Require(runtimeStats.cachedMeshCount == 1U, "Extracted glTF material runtime render did not cache one mesh");
    kb::editor::tests::Require(runtimeStats.cachedMaterialCount >= 1U, "Extracted glTF material runtime render did not cache the extracted material");
    kb::editor::tests::Require(runtimeStats.cachedTextureCount == 5U, "Extracted glTF material runtime render did not cache all extracted material textures");
    kb::editor::tests::Require(runtimeStats.referencedMeshAssetCount == 1U, "Extracted glTF material runtime render did not reference one mesh");
    kb::editor::tests::Require(runtimeStats.referencedMaterialAssetCount >= 1U, "Extracted glTF material runtime render did not reference the extracted material");
    kb::editor::tests::Require(runtimeStats.referencedTextureAssetCount == 5U, "Extracted glTF material runtime render did not reference all extracted textures");
    kb::editor::tests::Require(runtimeStats.unresolvedMaterialTexturePathCount == 0U, "Extracted glTF material runtime render reported unresolved texture paths");

    renderer.EndFrame();
    renderer.Shutdown();

    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
}

void RunExtractEmbeddedMaterialMissingTextureWarnsAndPreservesReferenceTest() {
    CleanTempRoot();

    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(scene.Assets().MountProject(TempRoot() / "Project"), "Missing embedded texture extraction test could not mount project assets");

    kb::assets::AssetManager& manager = scene.Assets().Manager();
    kb::editor::tests::Require(manager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>()), "Missing embedded texture extraction test could not register mesh loader");
    kb::editor::tests::Require(manager.RegisterLoader(std::make_unique<kb::render::RenderTextureAssetLoader>()), "Missing embedded texture extraction test could not register texture loader");

    const std::filesystem::path meshFolder = TempRoot() / "Project" / "Assets" / "Meshes";
    WriteEmbeddedMaterialGltfFixture(meshFolder);
    WriteTexture(meshFolder / "Textures" / "albedo.kbtex", 20U, 40U, 80U);
    WriteTexture(meshFolder / "Textures" / "metallic_roughness.kbtex", 0U, 90U, 180U);
    WriteTexture(meshFolder / "Textures" / "occlusion.kbtex", 200U, 200U, 200U);
    WriteTexture(meshFolder / "Textures" / "emissive.kbtex", 10U, 20U, 30U);
    kb::editor::tests::Require(scene.Assets().Discover() >= 5U, "Missing embedded texture extraction test did not discover mesh and present textures");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/Meshes/embedded.gltf");
    const kb::assets::AssetMetadata* albedoMetadata = manager.Registry().FindByPath("/Game/Meshes/Textures/albedo.kbtex");
    kb::editor::tests::Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Missing embedded texture extraction test did not discover glTF mesh");
    kb::editor::tests::Require(albedoMetadata != nullptr && albedoMetadata->type == "RenderTexture", "Missing embedded texture extraction test did not discover present albedo texture");
    const kb::assets::AssetId meshAssetId = meshMetadata->id;

    kb::editor::EditorEmbeddedMaterialExtractor extractor{ scene, browser, console };
    const kb::editor::EditorEmbeddedMaterialExtractionResult result = extractor.Extract(meshAssetId);
    kb::editor::tests::Require(result.Succeeded(), "Missing embedded texture extraction test should still produce a material asset");
    kb::editor::tests::Require(result.slots.size() == 1U, "Missing embedded texture extraction test returned wrong slot count");
    kb::editor::tests::Require(result.diagnostics.size() == 1U, "Missing embedded texture extraction test should report exactly one missing texture");
    kb::editor::tests::Require(result.diagnostics[0].find("Textures/normal.kbtex") != std::string::npos &&
            result.diagnostics[0].find("normal") != std::string::npos,
        "Missing embedded texture extraction test warning did not name the missing normal texture");
    kb::editor::tests::Require(console.Count(kb::editor::EditorConsoleLevel::Info) == 1U, "Missing embedded texture extraction test should still log extraction success");
    kb::editor::tests::Require(console.Count(kb::editor::EditorConsoleLevel::Warning) == 1U, "Missing embedded texture extraction test did not log one warning");

    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().Find(result.slots[0].materialAssetId);
    kb::editor::tests::Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Missing embedded texture extraction test did not register a RenderMaterial asset");
    const std::optional<kb::render::RenderMaterialAssetData> material = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialMetadata->physicalPath);
    kb::editor::tests::Require(material.has_value(), "Missing embedded texture extraction test could not load extracted material");
    kb::editor::tests::Require(material->desc.albedoTextureAssetId == albedoMetadata->id.value, "Missing embedded texture extraction test did not resolve present albedo texture");
    kb::editor::tests::Require(material->desc.normalTextureAssetId == 0U, "Missing embedded texture extraction test should not invent an asset id for a missing normal texture");
    kb::editor::tests::Require(material->normalTexturePath == "Textures/normal.kbtex", "Missing embedded texture extraction test did not preserve the unresolved normal texture reference");

    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
}

void RunExtractEmbeddedMaterialSanitizesMaterialNamesDeterministicallyTest() {
    CleanTempRoot();

    kb::assets::AssetManager manager;
    kb::editor::tests::Require(manager.Mounts().Mount("Game", TempRoot() / "Project" / "Assets"), "Embedded material sanitize test could not mount project assets");
    kb::editor::tests::Require(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()), "Embedded material sanitize test could not register material loader");

    const std::filesystem::path outputFolder = TempRoot() / "Project" / "Assets" / "Meshes";
    std::error_code error;
    std::filesystem::create_directories(outputFolder, error);
    kb::editor::tests::Require(!error, "Embedded material sanitize test could not create output folder");

    const kb::assets::AssetMetadata meshMetadata = Metadata("mesh", "RenderMesh", "/Game/Meshes/imported.gltf");
    std::vector<std::string> diagnostics;

    kb::render::RenderMeshEmbeddedMaterial first{};
    first.name = "../Paint:Layer? ";
    const std::optional<kb::editor::EditorExtractedMaterialSlot> firstSlot =
        kb::editor::EditorEmbeddedMaterialAssetWriter::Write(first, 0U, meshMetadata, outputFolder, manager, diagnostics);
    kb::editor::tests::Require(firstSlot.has_value() && firstSlot->virtualPath == "/Game/Meshes/Paint_Layer.kbmat", "Embedded material writer did not sanitize unsafe material name deterministically");

    kb::render::RenderMeshEmbeddedMaterial collision{};
    collision.name = "Paint/Layer";
    const std::optional<kb::editor::EditorExtractedMaterialSlot> collisionSlot =
        kb::editor::EditorEmbeddedMaterialAssetWriter::Write(collision, 1U, meshMetadata, outputFolder, manager, diagnostics);
    kb::editor::tests::Require(collisionSlot.has_value() && collisionSlot->virtualPath == "/Game/Meshes/Paint_Layer1.kbmat", "Embedded material writer did not deterministically suffix sanitized name collisions");

    kb::render::RenderMeshEmbeddedMaterial fallback{};
    fallback.name = "!!!";
    const std::optional<kb::editor::EditorExtractedMaterialSlot> fallbackSlot =
        kb::editor::EditorEmbeddedMaterialAssetWriter::Write(fallback, 2U, meshMetadata, outputFolder, manager, diagnostics);
    kb::editor::tests::Require(fallbackSlot.has_value() && fallbackSlot->virtualPath == "/Game/Meshes/EmbeddedMaterial2.kbmat", "Embedded material writer did not use deterministic fallback name for empty sanitized material name");
    kb::editor::tests::Require(diagnostics.empty(), "Embedded material sanitize test produced unexpected diagnostics");

    std::filesystem::remove_all(TempRoot(), error);
}

void RunMaterialEditorGraphRuntimeStateTest() {
    const auto makeLink = [](kb::render::RenderMaterialGraphNodeKind fromKind, std::uint32_t fromNode, std::string fromPin,
        kb::render::RenderMaterialGraphNodeKind toKind, std::uint32_t toNode, std::string toPin) {
        kb::render::RenderMaterialGraphLink link{
            .fromNodeId = fromNode,
            .fromPinId = kb::render::RenderMaterialGraphStablePinId(fromKind, fromPin, true),
            .fromPin = std::move(fromPin),
            .toNodeId = toNode,
            .toPinId = kb::render::RenderMaterialGraphStablePinId(toKind, toPin, false),
            .toPin = std::move(toPin),
        };
        link.id = kb::render::MakeRenderMaterialGraphLinkId(link);
        return link;
    };

    // A valid graph (ConstantColor -> BaseColor) resolves to the GPU graph runtime state.
    kb::render::RenderMaterialAssetData valid{};
    valid.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    valid.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{ .id = 2U, .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor, .positionX = -160, .positionY = 64 });
    valid.graph.links.push_back(makeLink(kb::render::RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", kb::render::RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    kb::editor::MaterialEditorState editor;
    editor.Open(kb::assets::AssetId{ 0x1401U }, valid);
    kb::editor::tests::Require(!editor.DiagnosticsHaveError() && editor.GraphRuntimeState() == kb::render::RenderMaterialGraphRuntimeState::UsingGpuGraph,
        "KBMAT-MAT14: A valid graph working copy must report the GPU graph runtime state");

    // An invalid graph (Float -> Color type mismatch) with no last-good artifact falls back to the error material.
    kb::render::RenderMaterialAssetData invalid{};
    invalid.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    invalid.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{ .id = 2U, .kind = kb::render::RenderMaterialGraphNodeKind::ConstantScalar, .positionX = -160, .positionY = 64 });
    invalid.graph.links.push_back(makeLink(kb::render::RenderMaterialGraphNodeKind::ConstantScalar, 2U, "value", kb::render::RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    editor.SetWorkingCopy(invalid);
    kb::editor::tests::Require(editor.DiagnosticsHaveError() && editor.GraphRuntimeState() == kb::render::RenderMaterialGraphRuntimeState::UsingErrorMaterial,
        "KBMAT-MAT14: An invalid graph without a last-good artifact must fall back to the error material");

    // The same broken graph with a last-good artifact keeps serving the last-good program.
    kb::render::RenderMaterialAssetData invalidWithLastGood = invalid;
    invalidWithLastGood.graph.lastGoodArtifact = kb::render::RenderMaterialGraphLastGoodArtifact{ .assetId = 0x99U, .contentHash = 0xABU };
    editor.SetWorkingCopy(invalidWithLastGood);
    kb::editor::tests::Require(editor.DiagnosticsHaveError() && editor.GraphRuntimeState() == kb::render::RenderMaterialGraphRuntimeState::UsingLastGood,
        "KBMAT-MAT14: A broken graph edit must keep the last-good program active instead of crashing");
    kb::editor::tests::Require(!editor.Diagnostics().empty(), "KBMAT-MAT14: A broken graph must surface diagnostics, not a silent black state");

    // Fixing the graph hot-reloads back to the GPU graph state without reopening.
    editor.SetWorkingCopy(valid);
    kb::editor::tests::Require(!editor.DiagnosticsHaveError() && editor.GraphRuntimeState() == kb::render::RenderMaterialGraphRuntimeState::UsingGpuGraph,
        "KBMAT-MAT14: Fixing a broken graph must hot-reload back to the GPU graph state");
}

} // namespace

namespace kb::editor::tests {

void RunEditorMaterialAssetAuthoringTests() {
    RunCreateMaterialAssetThroughEditorAuthoringTest();
    RunCreateMaterialGraphAndTypeThroughEditorAuthoringTest();
    RunCreateMaterialFunctionAssetThroughEditorAuthoringTest();
    RunCreateMaterialFromGraphAndMaterialTypeThroughEditorAuthoringTest();
    RunCreateMaterialInstanceAssetThroughEditorAuthoringTest();
    RunEditMaterialAssetThroughEditorAuthoringTest();
    RunMaterialCreateEditSaveReopenE2ETest();
    RunDuplicateMaterialAssetPreservesParametersTest();
    RunMaterialTextureSlotAuthoringTest();
    RunMaterialTextureSlotValidationTest();
    RunMaterialAssetEditCommandUndoRedoTest();
    RunMaterialEditorWorkingCopySaveRevertUndoRedoTest();
    RunMaterialEditorGraphWorkingCopyRuntimeTest();
    RunMaterialEditorVariantSwitchAuthoringTest();
    RunMaterialEditorGraphRuntimeStateTest();
    RunMaterialEditorGraphWorkingCopyCommandUndoRedoTest();
    RunMaterialEditorGraphMultiSelectCopyPasteDuplicateTest();
    RunMaterialEditorGraphSelectionLayoutCommandsTest();
    RunMaterialEditorGraphPromoteToParameterTest();
    RunMaterialEditorGraphCommentBoxSerializationGroupMoveTest();
    RunMaterialEditorGraphCompositeRerouteAuthoringTest();
    RunMaterialEditorGraphNodeCreationUxModelTest();
    RunMaterialEditorCollectionParameterNodeModelTest();
    RunMaterialEditorFunctionNodeModelTest();
    RunMaterialEditorLayerStackNodeModelTest();
    RunMaterialEditorTypedNodePropertyModelTest();
    RunMaterialEditorGraphPinTypeUiModelTest();
    RunMaterialInstanceEditorOverrideModelAndSaveTest();
    RunMaterialEditorMaterialStatsPanelModelTest();
    RunMaterialEditorShaderViewerReflectionModelTest();
    RunMaterialEditorFindInMaterialModelTest();
    RunProjectFilesMaterialEditorMeshRendererRenderPathE2ETest();
    RunGraphMaterialCreateEditAssignRenderE2ETest();
    RunGraphMaterialParameterHotReloadsPreviewAndSceneE2ETest();
    RunGraphMaterialSchemaMigrationEditorE2ETest();
    RunExtractEmbeddedMaterialToMaterialAssetTest();
    RunExtractedGltfMaterialRendersThroughRuntimeTest();
    RunExtractEmbeddedMaterialMissingTextureWarnsAndPreservesReferenceTest();
    RunExtractEmbeddedMaterialSanitizesMaterialNamesDeterministicallyTest();
}

} // namespace kb::editor::tests
