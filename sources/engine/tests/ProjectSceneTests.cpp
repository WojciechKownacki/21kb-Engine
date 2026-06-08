#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/project/ProjectManager.hpp"
#include "engine/assets/AssetHandle.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneDocument.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneObjectDesc.hpp"

#include <array>
#include <filesystem>
#include <fstream>

namespace kb::tests {
namespace {

constexpr std::array<std::uint8_t, 8U> kSceneMagic{ '2', '1', 'K', 'B', 'S', 'C', 'N', 0 };
constexpr std::array<std::uint8_t, 8U> kSceneMetaMagic{ '2', '1', 'K', 'B', 'S', 'M', 'T', 0 };

[[nodiscard]] std::filesystem::path TempRoot() {
    return std::filesystem::temp_directory_path() / "21kb_engine_project_scene_tests";
}

void CleanTempRoot() {
    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
    std::filesystem::create_directories(TempRoot(), error);
}

void RunProjectDescriptorRoundTripTest() {
    CleanTempRoot();
    const std::filesystem::path projectFile = TempRoot() / "Sample.21kbproject";

    kb::project::ProjectDescriptor descriptor;
    descriptor.name = "Sample";
    descriptor.category = "Game";
    descriptor.description = "Roundtrip descriptor";
    descriptor.defaultScene = "/Game/Scenes/Test.21kbscene";
    descriptor.targetPlatforms = { "Windows", "Linux" };
    descriptor.modules.push_back(kb::project::ProjectModuleDescriptor{ .name = "SampleRuntime", .type = "Runtime", .loadingPhase = "Default" });
    descriptor.plugins.push_back(kb::project::ProjectPluginReference{ .name = "GameplayTools", .enabled = true });

    Require(kb::project::ProjectManager::CreateProject(projectFile, descriptor), "Project descriptor was not created");
    const kb::project::ProjectDescriptorReadResult loaded = kb::project::ProjectManager::LoadProject(projectFile);
    Require(loaded.succeeded, "Project descriptor did not load");
    Require(loaded.descriptor.name == "Sample", "Project descriptor name did not roundtrip");
    Require(loaded.descriptor.defaultScene == "/Game/Scenes/Test.21kbscene", "Project descriptor default scene did not roundtrip");
    Require(loaded.descriptor.targetPlatforms.size() == 2, "Project descriptor target platforms did not roundtrip");
    Require(!loaded.descriptor.modules.empty() && loaded.descriptor.modules.front().name == "SampleRuntime", "Project descriptor modules did not roundtrip");
    Require(!loaded.descriptor.plugins.empty() && loaded.descriptor.plugins.front().name == "GameplayTools", "Project descriptor plugins did not roundtrip");
}

void RunSceneDocumentRoundTripTest() {
    CleanTempRoot();
    const std::filesystem::path sceneFile = TempRoot() / "Scenes" / "RoundTrip.21kbscene";

    kb::scene::Scene source;
    const kb::scene::SceneEntity root = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Root" });
    const kb::scene::SceneEntity child = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Child", .parent = source.Entities().Object(root) });
    const kb::scene::SceneEntity secondRoot = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "SecondRoot" });
    source.Components().MeshRenderers().Set(root, kb::scene::MeshRendererComponent{
        .meshAssetId = 41,
        .materialAssetId = 42,
        .castsShadow = false,
    });
    source.Components().Lights().Set(child, kb::scene::LightComponent{
        .kind = kb::scene::LightKind::Directional,
        .intensity = 3.0F,
    });
    source.Components().Cameras().Set(secondRoot, kb::scene::CameraComponent{
        .orthographicHeight = 16.0F,
        .primary = true,
    });

    Require(kb::scene::SceneDocumentService::Save(source, sceneFile, "RoundTrip"), "Scene document was not saved");

    kb::scene::Scene target;
    static_cast<void>(target.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "OldRoot" }));
    Require(kb::scene::SceneDocumentService::LoadFileIntoScene(target, sceneFile), "Scene document was not loaded into target scene");

    const std::vector<kb::scene::SceneEntity> roots = target.Hierarchy().RootEntities();
    Require(roots.size() == 2, "Scene document should restore both root entities");
    Require(target.Entities().Name(roots[0]) == "Root", "Scene document first root name did not roundtrip");
    const std::vector<kb::scene::SceneEntity> restoredChildren = target.Hierarchy().ChildEntities(roots[0]);
    Require(restoredChildren.size() == 1, "Scene document child hierarchy did not roundtrip");
    Require(target.Entities().Name(roots[1]) == "SecondRoot", "Scene document second root name did not roundtrip");
    const kb::scene::MeshRendererComponent* meshRenderer = target.Components().MeshRenderers().TryGet(roots[0]);
    const kb::scene::LightComponent* light = target.Components().Lights().TryGet(restoredChildren[0]);
    const kb::scene::CameraComponent* camera = target.Components().Cameras().TryGet(roots[1]);
    Require(meshRenderer != nullptr && meshRenderer->meshAssetId == 41 && !meshRenderer->castsShadow, "Scene document mesh renderer did not roundtrip");
    Require(light != nullptr && light->kind == kb::scene::LightKind::Directional && NearlyEqual(light->intensity, 3.0F), "Scene document light did not roundtrip");
    Require(camera != nullptr && camera->primary && NearlyEqual(camera->orthographicHeight, 16.0F), "Scene document camera did not roundtrip");
}

void RunEmptySceneDocumentClearsRuntimeSceneTest() {
    kb::scene::Scene scene;
    static_cast<void>(scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "StaleRoot" }));
    Require(scene.Hierarchy().RootEntities().size() == 1, "Empty document clear setup failed");

    kb::scene::SceneDocument emptyScene;
    emptyScene.guid = "scene:empty";
    emptyScene.name = "Empty";
    Require(kb::scene::SceneDocumentService::LoadIntoScene(scene, emptyScene), "Empty scene document did not load");
    Require(scene.Hierarchy().RootEntities().empty(), "Empty scene document did not clear runtime roots");
}

void RunSceneDocumentAssetDiscoveryTest() {
    CleanTempRoot();
    const std::filesystem::path projectRoot = TempRoot() / "Project";
    const std::filesystem::path sceneFile = projectRoot / "Assets" / "Scenes" / "Discovered.21kbscene";

    kb::scene::Scene source;
    static_cast<void>(source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "DiscoveredRoot" }));
    Require(kb::scene::SceneDocumentService::Save(source, sceneFile, "Discovered"), "Scene document discovery fixture was not saved");

    kb::scene::Scene scene;
    Require(scene.Assets().MountProject(projectRoot), "Project assets did not mount for Scene document discovery");
    Require(scene.Assets().Discover() == 1, "Scene document asset was not discovered");
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().FindByPath("/Game/Scenes/Discovered.21kbscene");
    Require(metadata != nullptr, "Scene document metadata was not registered");
    Require(metadata->type == "Scene", "Scene document asset type was not registered");
    const kb::assets::AssetHandle<kb::scene::SceneDocument> loaded = scene.Assets().Manager().Load<kb::scene::SceneDocument>(metadata->id);
    Require(loaded.IsLoaded(), "Scene document asset did not load through the asset manager");
    Require(loaded->name == "Discovered", "Scene document asset name did not load through the asset manager");
}

void RunSceneAssetWritesMetaAndLoadsThroughSceneSystemTest() {
    CleanTempRoot();
    const std::filesystem::path sceneFile = TempRoot() / "Project" / "Assets" / "Scenes" / "Main.21kbscene";

    kb::scene::Scene source;
    const kb::scene::SceneEntity root = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "SceneRoot" });
    source.Components().MeshRenderers().Set(root, kb::scene::MeshRendererComponent{
        .meshAssetId = 101,
        .materialAssetId = 202,
    });

    Require(kb::scene::SceneDocumentService::Save(source, sceneFile, "Main"), "Scene asset was not saved");
    Require(std::filesystem::is_regular_file(sceneFile), "Scene asset file was not written");
    Require(std::filesystem::is_regular_file(sceneFile.parent_path() / "Main.meta"), "Scene asset meta file was not written");
    {
        std::ifstream sceneInput{ sceneFile, std::ios::binary };
        std::array<std::uint8_t, kSceneMagic.size()> magic{};
        sceneInput.read(reinterpret_cast<char*>(magic.data()), static_cast<std::streamsize>(magic.size()));
        Require(magic == kSceneMagic, "Scene asset was not written with the binary scene magic");
    }
    {
        std::ifstream metaInput{ sceneFile.parent_path() / "Main.meta", std::ios::binary };
        std::array<std::uint8_t, kSceneMetaMagic.size()> magic{};
        metaInput.read(reinterpret_cast<char*>(magic.data()), static_cast<std::streamsize>(magic.size()));
        Require(magic == kSceneMetaMagic, "Scene meta was not written with the binary meta magic");
    }

    const kb::scene::SceneDocumentLoadResult loaded = kb::scene::SceneDocumentService::Load(sceneFile);
    Require(loaded.succeeded, "Scene asset did not load through SceneDocumentService");
    Require(loaded.document.guid == "scene:Main", "Scene asset guid should use the scene namespace");
    Require(loaded.document.name == "Main", "Scene asset name did not roundtrip");
    Require(loaded.document.worldPrefab.NodeCount() == 1U, "Scene asset node count did not roundtrip");

    kb::scene::Scene runtime;
    Require(runtime.Assets().MountProject(TempRoot() / "Project"), "Scene asset project mount failed");
    Require(runtime.Assets().Discover() == 1U, "Scene asset discovery did not find the scene");
    const kb::assets::AssetMetadata* metadata = runtime.Assets().Manager().Registry().FindByPath("/Game/Scenes/Main.21kbscene");
    Require(metadata != nullptr, "Scene asset metadata was not registered");
    Require(metadata->type == "Scene", "Scene asset type was not registered");
    const kb::assets::AssetHandle<kb::scene::SceneDocument> sceneAsset = runtime.Assets().Manager().Load<kb::scene::SceneDocument>(metadata->id);
    Require(sceneAsset.IsLoaded(), "Scene asset did not load through the asset manager");
}

void RunSceneAssetRejectsChecksumMismatchTest() {
    CleanTempRoot();
    const std::filesystem::path sceneFile = TempRoot() / "Project" / "Assets" / "Scenes" / "Tamper.21kbscene";

    kb::scene::Scene source;
    static_cast<void>(source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "TamperRoot" }));
    Require(kb::scene::SceneDocumentService::Save(source, sceneFile, "Tamper"), "Scene asset checksum fixture was not saved");

    std::ofstream output{ sceneFile, std::ios::binary | std::ios::app };
    output << "#tampered\n";
    output.close();
    Require(!kb::scene::SceneDocumentService::Load(sceneFile).succeeded, "Scene asset accepted mismatched integrity metadata");
}

} // namespace

void RunProjectSceneTests() {
    RunProjectDescriptorRoundTripTest();
    RunSceneDocumentRoundTripTest();
    RunEmptySceneDocumentClearsRuntimeSceneTest();
    RunSceneDocumentAssetDiscoveryTest();
    RunSceneAssetWritesMetaAndLoadsThroughSceneSystemTest();
    RunSceneAssetRejectsChecksumMismatchTest();
}

} // namespace kb::tests
