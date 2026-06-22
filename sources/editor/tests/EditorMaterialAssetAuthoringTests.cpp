#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "assets/EditorAssetBrowserState.hpp"
#include "commands/EditorCommandStack.hpp"
#include "console/EditorConsoleState.hpp"
#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderResources.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "scene/material/EditorEmbeddedMaterialExtractor.hpp"
#include "scene/material/EditorMaterialAssetAuthoring.hpp"
#include "scene/material/EditorMaterialAssetEditCommand.hpp"
#include "scene/material/EditorMaterialAssetGateway.hpp"

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

[[nodiscard]] std::filesystem::path TempRoot() {
    return std::filesystem::temp_directory_path() / "21kb_editor_material_asset_authoring_tests";
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

void WriteEmbeddedMaterialGltfFixture(const std::filesystem::path& folder) {
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const std::filesystem::path binPath = folder / "mesh.bin";
    {
        const std::vector<float> positions{
            0.0F, 0.0F, 0.0F,
            1.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F,
        };
        const std::vector<float> normals{
            0.0F, 0.0F, 1.0F,
            0.0F, 0.0F, 1.0F,
            0.0F, 0.0F, 1.0F,
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
        << "  \"nodes\": [{ \"mesh\": 0 }],\n"
        << "  \"materials\": [{\n"
        << "    \"name\": \"painted metal\",\n"
        << "    \"pbrMetallicRoughness\": {\n"
        << "      \"baseColorFactor\": [0.2, 0.4, 0.8, 0.6],\n"
        << "      \"metallicFactor\": 0.7,\n"
        << "      \"roughnessFactor\": 0.35,\n"
        << "      \"baseColorTexture\": { \"index\": 0 }\n"
        << "    },\n"
        << "    \"normalTexture\": { \"index\": 1, \"scale\": 0.75 },\n"
        << "    \"emissiveFactor\": [0.1, 0.2, 0.3],\n"
        << "    \"alphaMode\": \"BLEND\",\n"
        << "    \"doubleSided\": true\n"
        << "  }],\n"
        << "  \"textures\": [{ \"source\": 0 }, { \"source\": 1 }],\n"
        << "  \"images\": [{ \"uri\": \"Textures/albedo.kbtex\" }, { \"uri\": \"Textures/missing_normal.kbtex\" }],\n"
        << "  \"meshes\": [{ \"primitives\": [{ \"attributes\": { \"POSITION\": 0, \"NORMAL\": 1, \"TEXCOORD_0\": 2 }, \"indices\": 3, \"material\": 0 }] }],\n"
        << "  \"buffers\": [{ \"uri\": \"mesh.bin\", \"byteLength\": 102 }],\n"
        << "  \"bufferViews\": [\n"
        << "    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 36, \"target\": 34962 },\n"
        << "    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 36, \"target\": 34962 },\n"
        << "    { \"buffer\": 0, \"byteOffset\": 72, \"byteLength\": 24, \"target\": 34962 },\n"
        << "    { \"buffer\": 0, \"byteOffset\": 96, \"byteLength\": 6, \"target\": 34963 }\n"
        << "  ],\n"
        << "  \"accessors\": [\n"
        << "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\", \"min\": [0, 0, 0], \"max\": [1, 1, 0] },\n"
        << "    { \"bufferView\": 1, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        << "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC2\" },\n"
        << "    { \"bufferView\": 3, \"componentType\": 5123, \"count\": 3, \"type\": \"SCALAR\" }\n"
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
    const kb::assets::AssetHandle<kb::render::RenderMaterialAssetData> loadedBeforeEdit = scene.Assets().Manager().Load<kb::render::RenderMaterialAssetData>(metadata->id);
    kb::editor::tests::Require(loadedBeforeEdit.IsLoaded(), "Material edit test could not load created material into runtime cache");

    kb::editor::tests::Require(authoring.SetBaseColor(metadata->id, 0, 1.25F), "Material edit test could not set base color red");
    kb::editor::tests::Require(authoring.SetBaseColor(metadata->id, 1, 0.5F), "Material edit test could not set base color green");
    kb::editor::tests::Require(authoring.SetBaseColor(metadata->id, 2, -0.25F), "Material edit test could not set base color blue");
    kb::editor::tests::Require(authoring.SetBaseColor(metadata->id, 3, 0.75F), "Material edit test could not set base color alpha");
    kb::editor::tests::Require(authoring.SetMetallicFactor(metadata->id, 2.0F), "Material edit test could not set metallic factor");
    kb::editor::tests::Require(authoring.SetRoughnessFactor(metadata->id, -2.0F), "Material edit test could not set roughness factor");
    kb::editor::tests::Require(authoring.SetNormalScale(metadata->id, -3.0F), "Material edit test could not set normal scale");
    kb::editor::tests::Require(authoring.SetOcclusionStrength(metadata->id, 0.25F), "Material edit test could not set occlusion strength");
    kb::editor::tests::Require(authoring.SetEmissiveColor(metadata->id, 0, 0.1F), "Material edit test could not set emissive red");
    kb::editor::tests::Require(authoring.SetEmissiveColor(metadata->id, 1, 0.2F), "Material edit test could not set emissive green");
    kb::editor::tests::Require(authoring.SetEmissiveColor(metadata->id, 2, 0.3F), "Material edit test could not set emissive blue");
    kb::editor::tests::Require(authoring.SetEmissiveStrength(metadata->id, -1.0F), "Material edit test could not set emissive strength");
    kb::editor::tests::Require(authoring.SetAlphaCutoff(metadata->id, 0.45F), "Material edit test could not set alpha cutoff");
    kb::editor::tests::Require(authoring.SetAlphaMode(metadata->id, kb::render::RenderMaterialAlphaMode::Mask), "Material edit test could not set alpha mode");
    kb::editor::tests::Require(authoring.CycleAlphaMode(metadata->id), "Material edit test could not cycle alpha mode");
    kb::editor::tests::Require(authoring.ToggleDoubleSided(metadata->id), "Material edit test could not toggle double-sided flag");

    kb::editor::tests::Require(!scene.Assets().Manager().IsLoaded(metadata->id), "Material edit should unload stale runtime cache after save");
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
    stack.PushExecuted(kb::editor::EditorMaterialAssetEditCommand::CreateRecorded(scene, materialId, "Drag Material", *beforeDrag, *afterDrag));
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
    kb::editor::tests::Require(scene.Assets().Discover() >= 2U, "Embedded material extraction test did not discover mesh and texture assets");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/Meshes/embedded.gltf");
    const kb::assets::AssetMetadata* albedoMetadata = manager.Registry().FindByPath("/Game/Meshes/Textures/albedo.kbtex");
    kb::editor::tests::Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Embedded material extraction test did not discover glTF mesh");
    kb::editor::tests::Require(albedoMetadata != nullptr && albedoMetadata->type == "RenderTexture", "Embedded material extraction test did not discover albedo texture");
    const kb::assets::AssetId meshAssetId = meshMetadata->id;
    const kb::assets::AssetId albedoAssetId = albedoMetadata->id;

    kb::editor::EditorEmbeddedMaterialExtractor extractor{ scene, browser, console };
    const kb::editor::EditorEmbeddedMaterialExtractionResult result = extractor.Extract(meshAssetId);
    kb::editor::tests::Require(result.Succeeded(), "Embedded material extraction did not produce a material asset");
    kb::editor::tests::Require(result.slots.size() == 1U && result.slots[0].slotIndex == 0U, "Embedded material extraction returned wrong slot mapping");
    kb::editor::tests::Require(result.diagnostics.size() == 1U, "Embedded material extraction should report one unresolved texture path");
    kb::editor::tests::Require(console.Count(kb::editor::EditorConsoleLevel::Info) == 1U, "Embedded material extraction should log one success message");
    kb::editor::tests::Require(console.Count(kb::editor::EditorConsoleLevel::Warning) == 1U, "Embedded material extraction should log one unresolved texture warning");
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
    kb::editor::tests::Require(material->desc.alphaMode == kb::render::RenderMaterialAlphaMode::Blend, "Extracted material lost alpha mode");
    kb::editor::tests::Require(material->desc.doubleSided, "Extracted material lost double-sided flag");
    kb::editor::tests::Require(material->desc.albedoTextureAssetId == albedoAssetId.value, "Extracted material did not map albedo texture path to asset id");
    kb::editor::tests::Require(material->albedoTexturePath.empty(), "Extracted material kept resolved albedo texture path");
    kb::editor::tests::Require(material->normalTexturePath == "Textures/missing_normal.kbtex", "Extracted material did not preserve unresolved normal texture path");

    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
}

} // namespace

namespace kb::editor::tests {

void RunEditorMaterialAssetAuthoringTests() {
    RunCreateMaterialAssetThroughEditorAuthoringTest();
    RunEditMaterialAssetThroughEditorAuthoringTest();
    RunMaterialTextureSlotAuthoringTest();
    RunMaterialAssetEditCommandUndoRedoTest();
    RunExtractEmbeddedMaterialToMaterialAssetTest();
}

} // namespace kb::editor::tests
