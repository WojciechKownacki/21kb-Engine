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
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
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
#include "scene/EditorSceneMaterialAssetActions.hpp"
#include "scene/material/MaterialEditorState.hpp"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
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
        .positionX = -180,
        .positionY = 72,
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

    materialEditor.Open(materialId, before);
    kb::editor::tests::Require(!materialEditor.Dirty(), "KBMAT-GRAPH-0108: Graph command test should start from a clean working copy");
    kb::editor::tests::Require(commandStack.Execute(kb::editor::EditorMaterialWorkingCopyEditCommand::Create(
                                  materialEditor,
                                  materialId,
                                  "Create Material Graph Node",
                                  before,
                                  after,
                                  0U,
                                  2U)),
        "KBMAT-GRAPH-0108: Graph working-copy command should execute through command stack");
    kb::editor::tests::Require(!commandStack.LastCompletedCommandAffectsOpenMaterialSource(), "KBMAT-GRAPH-0108: Graph working-copy command must not reload source material on undo/redo");
    kb::editor::tests::Require(materialEditor.Dirty(), "KBMAT-GRAPH-0108: Graph working-copy command should mark Material Editor dirty");
    kb::editor::tests::Require(materialEditor.SelectedNodeId() == 2U, "KBMAT-GRAPH-0108: Graph working-copy command should restore after-selection");
    kb::editor::tests::Require(materialEditor.WorkingCopy().has_value() && materialEditor.WorkingCopy()->graph.nodes.size() == 2U && materialEditor.WorkingCopy()->graph.links.size() == 1U,
        "KBMAT-GRAPH-0108: Graph working-copy command should apply node/link runtime state");

    kb::editor::tests::Require(commandStack.Undo(), "KBMAT-GRAPH-0108: Graph working-copy command should undo");
    kb::editor::tests::Require(!commandStack.LastCompletedCommandAffectsOpenMaterialSource(), "KBMAT-GRAPH-0108: Undo graph working-copy command must not request source reload");
    kb::editor::tests::Require(!materialEditor.Dirty(), "KBMAT-GRAPH-0108: Undo graph working-copy command should restore clean snapshot");
    kb::editor::tests::Require(materialEditor.SelectedNodeId() == 0U, "KBMAT-GRAPH-0108: Undo graph working-copy command should restore before-selection");
    kb::editor::tests::Require(materialEditor.WorkingCopy().has_value() && materialEditor.WorkingCopy()->graph.nodes.size() == 1U && materialEditor.WorkingCopy()->graph.links.empty(),
        "KBMAT-GRAPH-0108: Undo graph working-copy command should restore before graph");

    kb::editor::tests::Require(commandStack.Redo(), "KBMAT-GRAPH-0108: Graph working-copy command should redo");
    kb::editor::tests::Require(materialEditor.Dirty(), "KBMAT-GRAPH-0108: Redo graph working-copy command should restore dirty state");
    kb::editor::tests::Require(materialEditor.SelectedNodeId() == 2U, "KBMAT-GRAPH-0108: Redo graph working-copy command should restore after-selection");
    kb::editor::tests::Require(materialEditor.WorkingCopy().has_value() && materialEditor.WorkingCopy()->graph.nodes.size() == 2U && materialEditor.WorkingCopy()->graph.links.size() == 1U,
        "KBMAT-GRAPH-0108: Redo graph working-copy command should restore after graph");
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
    kb::editor::tests::Require(renderer.SubmitScene(scene, desc), "KBMAT-1006: Runtime render path rejected the Material Editor assigned scene");
    const kb::render::SceneRenderSubmitStats submitStats = renderer.LastSceneSubmitStats();
    if (submitStats.visibleMeshCount != 1U) {
        const kb::render::Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
        std::cerr
            << "KBMAT-1006 stats: visible=" << submitStats.visibleMeshCount
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
    if (submitStats.submittedMeshCount != 1U || submitStats.submittedDrawCallCount != 1U) {
        const kb::render::Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
        std::cerr
            << "KBMAT-1006 submit stats: submittedMesh=" << submitStats.submittedMeshCount
            << " drawCalls=" << submitStats.submittedDrawCallCount
            << " visible=" << submitStats.visibleMeshCount
            << " missingMeshBinding=" << submitStats.missingMeshBindingCount
            << " missingMaterialBinding=" << submitStats.missingMaterialBindingCount
            << " missingMaterialResource=" << submitStats.missingMaterialResourceCount
            << " unsupportedVertex=" << submitStats.unsupportedMeshVertexFormatCount
            << " cachedMeshes=" << runtimeStats.cachedMeshCount
            << " cachedMaterials=" << runtimeStats.cachedMaterialCount << '\n';
    }
    kb::editor::tests::Require(submitStats.submittedMeshCount == 1U && submitStats.submittedDrawCallCount == 1U, "KBMAT-1006: Runtime render path did not submit the assigned mesh/material");
    kb::editor::tests::Require(!submitStats.HasMissingResources(), "KBMAT-1006: Runtime render path reported missing resources for Material Editor assignment");

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

} // namespace

namespace kb::editor::tests {

void RunEditorMaterialAssetAuthoringTests() {
    RunCreateMaterialAssetThroughEditorAuthoringTest();
    RunCreateMaterialGraphAndTypeThroughEditorAuthoringTest();
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
    RunMaterialEditorGraphWorkingCopyCommandUndoRedoTest();
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
