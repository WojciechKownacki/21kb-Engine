#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/audio/AudioClipAsset.hpp"
#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/ImportedAsset.hpp"
#include "engine/assets/ImportedAssetLoader.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/script/ScriptAsset.hpp"
#include "engine/script/ScriptBehaviourAsset.hpp"
#include "engine/script/ScriptBehaviourBindingService.hpp"
#include "engine/visual/VisualGraphTypes.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

namespace {

class TextAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override {
        return "Text";
    }

    [[nodiscard]] std::type_index PayloadType() const noexcept override {
        return typeid(std::string);
    }

    [[nodiscard]] std::vector<std::string> Extensions() const override {
        return { ".txt", ".text" };
    }

    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override {
        std::ifstream input{ request.resolvedPath, std::ios::binary };
        if (!input.is_open()) {
            return kb::assets::AssetLoadResult{ .asset = {}, .error = "Text file could not be opened" };
        }

        std::string content{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
        return kb::assets::AssetLoadResult{ .asset = std::make_shared<std::string>(std::move(content)), .error = {} };
    }
};

[[nodiscard]] std::filesystem::path TestRoot() {
    return std::filesystem::temp_directory_path() / "21kb_engine_asset_runtime_tests";
}

void ResetTestRoot() {
    std::error_code error;
    std::filesystem::remove_all(TestRoot(), error);
    std::filesystem::create_directories(TestRoot(), error);
    kb::tests::Require(!error, "Asset runtime test root could not be prepared");
}

void WriteTextFile(const std::filesystem::path& path, std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    kb::tests::Require(!error, "Asset runtime test directory could not be created");

    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    kb::tests::Require(output.is_open(), "Asset runtime test file could not be opened");
    output << text;
    kb::tests::Require(output.good(), "Asset runtime test file could not be written");
}

void RunAssetManagerDiscoveryCacheAndManifestTest() {
    ResetTestRoot();

    const std::filesystem::path projectRoot = TestRoot() / "Project";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    WriteTextFile(assetsRoot / "Text" / "Greeting.txt", "hello runtime asset");

    kb::assets::AssetManager manager;
    kb::tests::Require(manager.RegisterLoader(std::make_unique<TextAssetLoader>()), "Text asset loader registration failed");
    kb::tests::Require(manager.Mounts().Mount("Game", assetsRoot), "Game asset mount failed");
    kb::tests::Require(manager.DiscoverMountedAssets() == 1, "Mounted asset discovery did not find the text asset");
    kb::tests::Require(manager.Registry().Count() == 1, "Asset registry did not store the discovered asset");

    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath("/Game/Text/Greeting.txt");
    kb::tests::Require(metadata != nullptr, "Discovered asset could not be resolved by virtual path");
    kb::tests::Require(metadata->id.IsValid(), "Discovered asset did not receive a stable id");
    kb::tests::Require(metadata->type == "Text", "Discovered asset type was not set from its loader");
    kb::tests::Require(metadata->contentHash != 0, "Discovered asset did not receive a content hash");

    const kb::assets::AssetHandle<std::string> loaded = manager.Load<std::string>(metadata->id);
    kb::tests::Require(loaded.IsLoaded(), "Text asset did not load through the runtime asset manager");
    kb::tests::Require(*loaded.Get() == "hello runtime asset", "Text asset payload was not preserved");
    kb::tests::Require(manager.LoadedCount() == 1, "Runtime asset cache did not retain the loaded asset");

    const kb::assets::AssetId textAssetId = metadata->id;
    const std::uint64_t oldContentHash = metadata->contentHash;
    WriteTextFile(assetsRoot / "Text" / "Greeting.txt", "updated runtime asset");
    kb::tests::Require(manager.DiscoverMountedAssets() == 1, "Mounted asset rediscovery did not update the changed text asset");
    metadata = manager.Registry().Find(textAssetId);
    kb::tests::Require(metadata != nullptr && metadata->contentHash != oldContentHash, "Asset rediscovery did not refresh the content hash");
    kb::tests::Require(!manager.IsLoaded(textAssetId), "Asset rediscovery did not invalidate cached payload after content change");
    const kb::assets::AssetHandle<std::string> reloaded = manager.Load<std::string>(textAssetId);
    kb::tests::Require(reloaded.IsLoaded() && *reloaded.Get() == "updated runtime asset", "Asset manager did not reload changed file content");

    const kb::assets::AssetHandle<int> wrongType = manager.Load<int>(metadata->id);
    kb::tests::Require(!wrongType.IsLoaded(), "Asset manager accepted a mismatched typed load");
    kb::tests::Require(!manager.LastError().empty(), "Asset manager did not report a mismatched typed load error");

    kb::tests::Require(manager.Unload(metadata->id), "Runtime asset cache did not unload the asset");
    kb::tests::Require(!manager.IsLoaded(metadata->id), "Runtime asset cache still reported the asset as loaded after unload");

    const std::filesystem::path manifestPath = TestRoot() / "AssetManifest.kbassets";
    kb::tests::Require(kb::assets::AssetManifest::Save(manifestPath, manager.Registry()), "Asset manifest save failed");

    kb::assets::AssetRegistry restored;
    kb::tests::Require(kb::assets::AssetManifest::Load(manifestPath, restored), "Asset manifest load failed");
    const kb::assets::AssetMetadata* restoredMetadata = restored.FindByPath("/Game/Text/Greeting.txt");
    kb::tests::Require(restoredMetadata != nullptr, "Restored asset manifest did not index the virtual path");
    kb::tests::Require(restoredMetadata->id == metadata->id, "Restored asset manifest did not preserve stable asset id");
    kb::tests::Require(restoredMetadata->contentHash == metadata->contentHash, "Restored asset manifest did not preserve content hash");
    kb::tests::Require(restoredMetadata->importCategory == metadata->importCategory, "Restored asset manifest did not preserve import category");
}

void RunAssetManagerFolderAndRenameOperationsTest() {
    ResetTestRoot();

    const std::filesystem::path assetsRoot = TestRoot() / "Project" / "Assets";
    WriteTextFile(assetsRoot / "Text" / "Greeting.txt", "hello");

    kb::assets::AssetManager manager;
    kb::tests::Require(manager.RegisterLoader(std::make_unique<TextAssetLoader>()), "Text asset loader registration failed for operations test");
    kb::tests::Require(manager.Mounts().Mount("Game", assetsRoot), "Game asset mount failed for operations test");
    kb::tests::Require(manager.DiscoverMountedAssets() == 1, "Operations test discovery did not find the text asset");

    kb::tests::Require(manager.CreateFolder("/Game/NewFolder"), "Asset manager did not create a mounted folder");
    kb::tests::Require(std::filesystem::is_directory(assetsRoot / "NewFolder"), "Created mounted folder does not exist on disk");
    const std::optional<std::filesystem::path> uniqueFolder = manager.CreateUniqueFolder("/Game", "NewFolder");
    kb::tests::Require(uniqueFolder.has_value() && *uniqueFolder == "/Game/NewFolder_2", "Asset manager did not create the expected unique folder path");
    kb::tests::Require(std::filesystem::is_directory(assetsRoot / "NewFolder_2"), "Unique mounted folder does not exist on disk");
    kb::tests::Require(manager.RenameFolder("/Game/NewFolder", "RenamedFolder"), "Asset manager did not rename a mounted folder");
    kb::tests::Require(std::filesystem::is_directory(assetsRoot / "RenamedFolder"), "Renamed mounted folder does not exist on disk");
    kb::tests::Require(manager.DeleteFolder("/Game/RenamedFolder"), "Asset manager did not delete an empty mounted folder");
    kb::tests::Require(!std::filesystem::exists(assetsRoot / "RenamedFolder"), "Deleted mounted folder still exists on disk");

    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath("/Game/Text/Greeting.txt");
    kb::tests::Require(metadata != nullptr, "Operations test asset was not indexed before rename");
    const kb::assets::AssetId oldId = metadata->id;
    WriteTextFile(assetsRoot / "Text" / "Greeting.meta", "meta");
    kb::tests::Require(manager.RenameAsset(oldId, "RenamedGreeting"), "Asset manager did not rename an asset file");
    kb::tests::Require(std::filesystem::is_regular_file(assetsRoot / "Text" / "RenamedGreeting.meta"), "Asset manager did not rename the sidecar meta file");
    kb::tests::Require(manager.Registry().FindByPath("/Game/Text/Greeting.txt") == nullptr, "Old asset virtual path remained indexed after rename");
    metadata = manager.Registry().FindByPath("/Game/Text/RenamedGreeting.txt");
    kb::tests::Require(metadata != nullptr, "Renamed asset virtual path was not indexed");
    kb::tests::Require(manager.CreateFolder("/Game/Moved"), "Asset manager did not create a move destination folder");
    WriteTextFile(assetsRoot / "Moved" / "RenamedGreeting.txt", "existing collision");
    const kb::assets::AssetMoveResult movedAsset = manager.MoveAssetIntoFolder(metadata->id, "/Game/Moved");
    kb::tests::Require(movedAsset.succeeded, "Asset manager did not move an asset file into a mounted folder");
    kb::tests::Require(movedAsset.virtualPath == "/Game/Moved/RenamedGreeting_1.txt", "Asset manager did not report the unique moved asset path");
    kb::tests::Require(manager.Registry().FindByPath("/Game/Text/RenamedGreeting.txt") == nullptr, "Old asset virtual path remained indexed after move");
    metadata = manager.Registry().FindByPath(movedAsset.virtualPath);
    kb::tests::Require(metadata != nullptr, "Moved asset virtual path was not indexed");
    kb::tests::Require(std::filesystem::is_regular_file(assetsRoot / "Moved" / "RenamedGreeting_1.txt"), "Moved asset file does not exist on disk");
    kb::tests::Require(std::filesystem::is_regular_file(assetsRoot / "Moved" / "RenamedGreeting_1.meta"), "Moved asset sidecar meta file does not exist on disk");
    kb::tests::Require(manager.DeleteAsset(metadata->id), "Asset manager did not delete an asset file");
    kb::tests::Require(manager.Registry().FindByPath(movedAsset.virtualPath) == nullptr, "Deleted asset remained indexed");
    kb::tests::Require(!std::filesystem::exists(assetsRoot / "Moved" / "RenamedGreeting_1.meta"), "Deleted asset sidecar meta file still exists");

    kb::tests::Require(manager.CreateFolder("/Game/FolderSource"), "Asset manager did not create a source folder for move");
    WriteTextFile(assetsRoot / "FolderSource" / "Nested" / "Inside.txt", "inside folder");
    std::filesystem::create_directories(assetsRoot / "Moved" / "FolderSource");
    const kb::assets::AssetMoveResult movedFolder = manager.MoveFolderIntoFolder("/Game/FolderSource", "/Game/Moved");
    kb::tests::Require(movedFolder.succeeded, "Asset manager did not move a folder into a mounted folder");
    kb::tests::Require(movedFolder.virtualPath == "/Game/Moved/FolderSource_1", "Asset manager did not report the unique moved folder path");
    kb::tests::Require(!std::filesystem::exists(assetsRoot / "FolderSource"), "Moved source folder still exists on disk");
    kb::tests::Require(std::filesystem::is_regular_file(assetsRoot / "Moved" / "FolderSource_1" / "Nested" / "Inside.txt"), "Moved folder contents were not preserved");
    kb::tests::Require(manager.Registry().FindByPath("/Game/Moved/FolderSource_1/Nested/Inside.txt") != nullptr, "Moved folder assets were not rediscovered");
    kb::tests::Require(!manager.MoveFolder(movedFolder.virtualPath, movedFolder.virtualPath / "Nested"), "Asset manager allowed a folder to move into its own child");
}

void RunAssetImportServiceBinaryContainerTest() {
    ResetTestRoot();

    const std::filesystem::path assetsRoot = TestRoot() / "Project" / "Assets";
    const std::filesystem::path sourceRoot = TestRoot() / "External";
    WriteTextFile(sourceRoot / "Albedo.png", "texture bytes");

    kb::assets::AssetManager manager;
    kb::tests::Require(manager.RegisterLoader(std::make_unique<kb::assets::ImportedAssetLoader>()), "Imported asset loader registration failed");
    kb::tests::Require(manager.Mounts().Mount("Game", assetsRoot), "Game asset mount failed for import test");

    const std::array<std::filesystem::path, 1> files{ sourceRoot / "Albedo.png" };
    const kb::assets::AssetImportResult result = kb::assets::AssetImportService::ImportFiles(manager, files, "/Game/Textures");
    kb::tests::Require(result.Succeeded() && result.ImportedCount() == 1U, "Asset import service did not import the source file");

    const kb::assets::AssetImportItemResult& item = result.items.front();
    kb::tests::Require(item.assetPhysicalPath.extension() == ".21kb", "Imported asset file should use the .21kb extension");
    kb::tests::Require(item.metaPhysicalPath.extension() == ".meta", "Imported asset meta file should use the .meta extension");
    kb::tests::Require(item.assetPhysicalPath.stem() == item.metaPhysicalPath.stem(), "Imported asset and meta should share the same base name");
    kb::tests::Require(std::filesystem::is_regular_file(item.assetPhysicalPath), "Imported .21kb file was not written");
    kb::tests::Require(std::filesystem::is_regular_file(item.metaPhysicalPath), "Imported .meta file was not written");
    kb::tests::Require(item.virtualPath == "/Game/Textures/Albedo.21kb", "Imported asset virtual path should point at the .21kb file");

    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(item.virtualPath);
    kb::tests::Require(metadata != nullptr && metadata->type == "ImportedAsset", "Imported asset metadata was not registered");
    kb::tests::Require(metadata != nullptr && metadata->importCategory == "Texture", "Imported asset metadata did not expose the import category");
    kb::tests::Require(metadata->contentHash == item.assetHash && metadata->contentHash != 0U, "Imported asset metadata did not store the container hash");

    const kb::assets::AssetHandle<kb::assets::ImportedAsset> loaded = manager.Load<kb::assets::ImportedAsset>(metadata->id);
    kb::tests::Require(loaded.IsLoaded(), "Imported .21kb asset did not load through the imported asset loader");
    kb::tests::Require(loaded->category == kb::assets::AssetImportCategory::Texture, "Imported asset category was not preserved");
    kb::tests::Require(loaded->sourceName == "Albedo.png", "Imported asset source name was not preserved");
    kb::tests::Require(loaded->sourceExtension == ".png", "Imported asset source extension was not preserved");
    kb::tests::Require(loaded->payload.size() == 13U, "Imported asset payload size was not preserved");

    kb::assets::AssetManager rediscovered;
    kb::tests::Require(rediscovered.RegisterLoader(std::make_unique<kb::assets::ImportedAssetLoader>()), "Imported asset rediscovery loader registration failed");
    kb::tests::Require(rediscovered.Mounts().Mount("Game", assetsRoot), "Game asset remount failed for imported asset rediscovery");
    kb::tests::Require(rediscovered.DiscoverMountedAssets() == 1U, "Imported asset rediscovery did not find the .21kb file");
    const kb::assets::AssetMetadata* rediscoveredMetadata = rediscovered.Registry().FindByPath(item.virtualPath);
    kb::tests::Require(rediscoveredMetadata != nullptr && rediscoveredMetadata->importCategory == "Texture", "Imported asset rediscovery did not read the binary category flag");
}

void RunScenePrefabRuntimeAssetTest() {
    ResetTestRoot();

    const std::filesystem::path projectRoot = TestRoot() / "PrefabProject";
    const std::filesystem::path prefabPath = projectRoot / "Assets" / "Prefabs" / "RuntimePrefab.kbprefab";
    const std::filesystem::path prefabCopyPath = projectRoot / "Assets" / "Prefabs" / "RuntimePrefabCopy.kbprefab";

    kb::scene::Scene source;
    kb::scene::SceneObject root = source.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Runtime Prefab Root" });
    const kb::scene::ScenePrefabHandle registered = source.Prefabs().CaptureRegistered(root, "RuntimePrefab");
    kb::tests::Require(registered.IsValid(), "Runtime prefab registration failed");
    kb::tests::Require(source.Prefabs().Save(registered, prefabPath), "Runtime prefab asset save failed");
    std::filesystem::copy_file(prefabPath, prefabCopyPath);

    kb::scene::Scene runtime;
    kb::tests::Require(runtime.Assets().MountProject(projectRoot), "Scene runtime asset project mount failed");
    kb::tests::Require(runtime.Assets().Discover() == 2, "Scene runtime asset discovery did not find every prefab file");

    const kb::assets::AssetHandle<kb::scene::ScenePrefab> prefab = runtime.Assets().LoadPrefab("/Game/Prefabs/RuntimePrefab.kbprefab");
    kb::tests::Require(prefab.IsLoaded(), "Scene prefab did not load through SceneAssets");
    kb::tests::Require(prefab->NodeCount() == 1, "Scene prefab runtime payload had an invalid node count");
    const kb::assets::AssetHandle<kb::scene::ScenePrefab> prefabCopy = runtime.Assets().LoadPrefab("/Game/Prefabs/RuntimePrefabCopy.kbprefab");
    kb::tests::Require(prefabCopy.IsLoaded(), "Scene prefab duplicate GUID copy did not load through SceneAssets");
    kb::tests::Require(prefabCopy->NodeCount() == 1, "Scene prefab duplicate GUID copy had an invalid node count");

    const kb::scene::ScenePrefabInstance instance = runtime.Prefabs().Instantiate(*prefab.Get());
    kb::tests::Require(instance.ObjectCount() == 1, "Runtime-loaded prefab did not instantiate");
    kb::tests::Require(runtime.Entities().Name(instance.ObjectAt(0)) == "Runtime Prefab Root", "Runtime-loaded prefab instance did not preserve the root name");
    const kb::scene::ScenePrefabInstance copyInstance = runtime.Prefabs().Instantiate(*prefabCopy.Get());
    kb::tests::Require(copyInstance.ObjectCount() == 1, "Runtime-loaded duplicate GUID prefab did not instantiate");
}

void RunSceneAudioClipAssetDiscoveryTest() {
    ResetTestRoot();

    const std::filesystem::path projectRoot = TestRoot() / "AudioProject";
    const std::filesystem::path clipPath = projectRoot / "Assets" / "Audio" / "Ping.wav";
    WriteTextFile(clipPath, "audio bytes");

    kb::scene::Scene scene;
    kb::tests::Require(scene.Assets().MountProject(projectRoot), "Scene audio asset project mount failed");
    kb::tests::Require(scene.Assets().Discover() == 1U, "Scene audio asset discovery did not find the wav file");

    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().FindByPath("/Game/Audio/Ping.wav");
    kb::tests::Require(metadata != nullptr, "Discovered audio asset could not be resolved by virtual path");
    kb::tests::Require(metadata->type == "AudioClip", "Discovered audio asset was not classified as AudioClip");

    const kb::assets::AssetHandle<kb::audio::AudioClipAsset> loaded = scene.Assets().Manager().Load<kb::audio::AudioClipAsset>(metadata->id);
    kb::tests::Require(loaded.IsLoaded(), "Audio clip asset did not load through the runtime asset manager");
    kb::tests::Require(std::filesystem::equivalent(loaded->path, clipPath), "Audio clip asset did not preserve the resolved physical path");
}

void RunScriptAssetPipelineTest() {
    ResetTestRoot();

    const std::filesystem::path projectRoot = TestRoot() / "ScriptProject";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    WriteTextFile(assetsRoot / "Logic" / "Player.lua",
        "-- @import Shared.Math\n"
        "-- @imported Shared.Wrong\n"
        "-- @expose speed Float = 5.5\n"
        "-- @expose lives Int = 3abc\n"
        "-- @exposed ignored Float = 1.0\n"
        "function Tick(self, dt)\nend\n");
    WriteTextFile(assetsRoot / "Logic" / "Door.native", "name DoorController\nsymbol gameplay.DoorController\napi = function Inventory.AddItem itemId:Int -> total:Int\n");
    WriteTextFile(assetsRoot / "Logic" / "Enemy.kbgraph", R"(kbgraph 1
name EnemyController
node 1 Event Tick
pin 1 Output then Void
)");

    kb::scene::Scene scene;
    kb::tests::Require(scene.Assets().MountProject(projectRoot), "Script asset project mount failed");
    kb::tests::Require(scene.Assets().Discover() == 3U, "Script asset discovery did not find Lua, native and visual graph assets");

    const kb::assets::AssetMetadata* luaMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/Player.lua");
    const kb::assets::AssetMetadata* nativeMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/Door.native");
    const kb::assets::AssetMetadata* graphMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/Enemy.kbgraph");
    kb::tests::Require(luaMetadata != nullptr && luaMetadata->type == "LuaScript", "Lua script asset metadata was not classified");
    kb::tests::Require(nativeMetadata != nullptr && nativeMetadata->type == "NativeBehaviour", "Native behaviour asset metadata was not classified");
    kb::tests::Require(graphMetadata != nullptr && graphMetadata->type == "VisualGraph", "Visual graph asset metadata was not classified");

    const kb::assets::AssetHandle<kb::script::LuaScriptAsset> luaAsset = scene.Assets().Manager().Load<kb::script::LuaScriptAsset>(luaMetadata->id);
    kb::tests::Require(luaAsset.IsLoaded() && luaAsset->source.find("Tick") != std::string::npos, "Lua script asset did not load source content");
    kb::tests::Require(luaAsset->imports.size() == 1U && luaAsset->imports[0] == "Shared.Math", "Lua script asset did not parse import metadata");
    kb::tests::Require(luaAsset->exposedVariables.size() == 2U &&
            luaAsset->exposedVariables[0].name == "speed" &&
            luaAsset->exposedVariables[0].type == kb::script::ScriptValueType::Float,
        "Lua script asset did not parse typed exposed variables");
    kb::tests::Require(luaAsset->exposedVariableDefaults.size() == 2U &&
            luaAsset->exposedVariableHasDefault.size() == 2U &&
            luaAsset->exposedVariableHasDefault[0] != 0U &&
            kb::tests::NearlyEqual(luaAsset->exposedVariableDefaults[0].AsFloat(), 5.5F),
        "Lua script asset did not parse exposed variable default value");
    kb::tests::Require(luaAsset->exposedVariables[1].name == "lives" &&
            luaAsset->exposedVariableDefaults.size() == 2U &&
            luaAsset->exposedVariableHasDefault.size() == 2U &&
            luaAsset->exposedVariableHasDefault[1] == 0U &&
            luaAsset->exposedVariableDefaults[1].AsInt() == 0,
        "Lua script asset accepted an invalid exposed variable default");

    const kb::assets::AssetHandle<kb::script::NativeBehaviourDescriptor> nativeAsset = scene.Assets().Manager().Load<kb::script::NativeBehaviourDescriptor>(nativeMetadata->id);
    kb::tests::Require(nativeAsset.IsLoaded() && nativeAsset->symbol == "gameplay.DoorController", "Native behaviour descriptor did not load symbol");
    kb::tests::Require(nativeAsset->apiDeclarations.size() == 1U && nativeAsset->apiDeclarations[0].name == "Inventory.AddItem",
        "Native behaviour descriptor did not parse API declarations");

    const kb::assets::AssetHandle<kb::visual::VisualGraphAsset> graphAsset = scene.Assets().Manager().Load<kb::visual::VisualGraphAsset>(graphMetadata->id);
    kb::tests::Require(graphAsset.IsLoaded() && graphAsset->name == "EnemyController", "Visual graph asset did not load through scene-registered loader");

    const std::optional<kb::scene::BehaviourComponent> luaBehaviour = kb::script::ScriptBehaviourAsset::CreateComponent(*luaMetadata);
    const std::optional<kb::scene::BehaviourComponent> nativeBehaviour = kb::script::ScriptBehaviourAsset::CreateComponent(*nativeMetadata);
    const std::optional<kb::scene::BehaviourComponent> graphBehaviour = kb::script::ScriptBehaviourAsset::CreateComponent(*graphMetadata);
    kb::tests::Require(luaBehaviour.has_value() && luaBehaviour->backend == kb::scene::BehaviourBackend::Lua, "Lua asset did not map to Lua behaviour");
    kb::tests::Require(nativeBehaviour.has_value() && nativeBehaviour->backend == kb::scene::BehaviourBackend::Native, "Native descriptor did not map to native behaviour");
    kb::tests::Require(graphBehaviour.has_value() && graphBehaviour->backend == kb::scene::BehaviourBackend::VisualGraph, "Visual graph did not map to visual graph behaviour");

    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Scripted" });
    const kb::script::ScriptBehaviourBindingResult bound = kb::script::ScriptBehaviourBindingService::AttachMetadata(
        scene,
        object.Entity(),
        *luaMetadata,
        kb::script::ScriptBehaviourBindingOptions{
            .enabled = true,
            .tickGroup = kb::scene::BehaviourTickGroup::Input,
            .executionOrder = -10,
            .prepareRuntimeAsset = false,
        });
    kb::tests::Require(bound.Succeeded(), "Script behaviour binding service did not attach a Lua asset");
    const kb::scene::BehaviourComponent* attached = scene.Components().Behaviours().TryGet(object.Entity());
    kb::tests::Require(attached != nullptr && attached->behaviourAssetId == luaMetadata->id.value, "Script behaviour component was not attached to the entity");
    kb::tests::Require(attached->backend == kb::scene::BehaviourBackend::Lua && attached->tickGroup == kb::scene::BehaviourTickGroup::Input && attached->executionOrder == -10,
        "Script behaviour binding service did not preserve component settings");
}

} // namespace

namespace kb::tests {

void RunAssetRuntimeTests() {
    RunAssetManagerDiscoveryCacheAndManifestTest();
    RunAssetManagerFolderAndRenameOperationsTest();
    RunAssetImportServiceBinaryContainerTest();
    RunScenePrefabRuntimeAssetTest();
    RunSceneAudioClipAssetDiscoveryTest();
    RunScriptAssetPipelineTest();
}

} // namespace kb::tests
