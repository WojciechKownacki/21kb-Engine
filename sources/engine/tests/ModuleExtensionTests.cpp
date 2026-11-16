#include "ModuleExtensionFixture.hpp"
#include "TestSupport.hpp"
#include "TestSuites.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/bake/AssetPackWriter.hpp"
#include "engine/assets/bake/RuntimeAssetManifest.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "engine/library/EngineLibraryComponentInspectorDesc.hpp"
#include "engine/modules/EngineModuleHost.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneModuleComponents.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>

namespace kb::tests {
namespace {

using Component = ModuleExtensionComponent;
using kb::scene::Scene;
using kb::scene::SceneEntity;
namespace bake = kb::assets::bake;

constexpr const char* kModule = "Tests.ModuleExtensions";
constexpr std::string_view kComponentName = "ModuleExtension";

std::filesystem::path Fresh(const char* name) {
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    return folder;
}

std::vector<std::uint8_t> ReadAll(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    return { std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

void WriteAll(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{ path, std::ios::binary }.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::uint8_t> AssetFile(std::uint64_t dependency, std::string_view text) {
    std::vector<std::uint8_t> bytes(sizeof(dependency));
    std::memcpy(bytes.data(), &dependency, sizeof(dependency));
    bytes.insert(bytes.end(), text.begin(), text.end());
    return bytes;
}

// The fixture module loaded from a copy of its library, attached to one scene.
class Attached {
public:
    Attached(Scene& scene, const std::filesystem::path& library)
        : scene_(scene), host_(Project(library)) {
        host_.Load(scene.Runtime().EcsWorld());
        Require(host_.IsActive(kModule), "The extension fixture module did not load");
        try { host_.AttachScene(scene); } catch (const std::exception& e) { Require(false, e.what()); }
    }
    ~Attached() {
        host_.DetachScene(scene_);
        host_.Unload();
    }
    void Reload() {
        std::array<Scene*, 1> scenes{ &scene_ };
        host_.Reload(scene_.Runtime().EcsWorld(), scenes);
        Require(host_.Diagnostics().empty() && host_.IsActive(kModule), "The extension fixture module did not reload");
    }

private:
    static kb::project::ProjectDescriptor Project(const std::filesystem::path& library) {
        kb::project::ProjectDescriptor project;
        project.disableEnginePluginsByDefault = true;
        project.plugins.push_back({ .name = kModule, .binaryPath = library.string(), .enabled = true });
        return project;
    }

    Scene& scene_;
    kb::modules::EngineModuleHost host_;
};

std::filesystem::path LibraryCopy(const std::filesystem::path& folder) {
    const std::filesystem::path copy = folder / "extension_fixture.dll";
    std::filesystem::copy_file(KB_MODULE_EXTENSION_PLUGIN, copy, std::filesystem::copy_options::overwrite_existing);
    return copy;
}

Component ReadComponent(Scene& scene, SceneEntity entity) {
    const Component* value = scene.Runtime().EcsWorld().TryGet<Component>(entity);
    Require(value != nullptr, "The module component is missing");
    return *value;
}

// ---- assets ----------------------------------------------------------------------------------------------------

void ModuleAssetTypeLoadsAndCooks() {
    const std::filesystem::path folder = Fresh("21kb_module_extension_assets");
    const std::filesystem::path game = folder / "Game";
    const kb::assets::AssetId dependencyId = kb::assets::MakeAssetId("/Game/Dep.kbtest:ModuleExtensionAsset");
    const std::vector<std::uint8_t> mainBytes = AssetFile(dependencyId.value, "main payload");
    const std::vector<std::uint8_t> dependencyBytes = AssetFile(0U, "dependency payload");
    WriteAll(game / "Main.kbtest", mainBytes);
    WriteAll(game / "Dep.kbtest", dependencyBytes);
    const std::filesystem::path library = LibraryCopy(folder);

    Scene scene;
    Attached attached{ scene, library };
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.Mounts().Mount("Game", game) && manager.DiscoverMountedAssets() == 2U, "The module asset type was not discovered by its extension");
    const kb::assets::AssetMetadata* main = manager.Registry().FindByPath("/Game/Main.kbtest");
    Require(main != nullptr && main->type == "ModuleExtensionAsset" && main->dependencies == std::vector<kb::assets::AssetId>{ dependencyId },
        "The module asset type lost its type or its declared dependency");
    const kb::assets::AssetId mainId = main->id;

    auto loaded = manager.Load<ModuleExtensionAsset>(mainId);
    Require(loaded.IsLoaded() && loaded->bytes == mainBytes, "The module loader did not load a source file synchronously");

    Require(manager.LoadAsync<ModuleExtensionAsset>(dependencyId), "The module asset type refused an asynchronous load");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 10 };
    while (!manager.IsLoaded(dependencyId) && std::chrono::steady_clock::now() < deadline) {
        manager.PumpAsyncLoads();
        std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
    }
    auto asynchronous = manager.AcquireLoaded<ModuleExtensionAsset>(dependencyId);
    Require(asynchronous.IsLoaded() && asynchronous->bytes == dependencyBytes, "The module loader did not load a source file asynchronously");

    // Cook: the source bytes pass through to a package, which names the dependency, and a game loads them back.
    Scene source;
    const std::filesystem::path scenePath = folder / "Main.21kbscene";
    Require(kb::scene::SceneDocumentService::Save(source, scenePath, "Main"), "The package scene was not written");
    const std::vector<std::uint8_t> sceneBytes = ReadAll(scenePath);
    const auto profile = bake::WindowsX64BakeTargetProfile();
    const std::filesystem::path packPath = folder / "Extension.kbpack";
    bake::AssetPackWriter writer{ packPath, profile };
    const auto store = [&](std::span<const std::uint8_t> bytes, std::string_view type) {
        const bake::AssetBakeKey key{ .sourceContentHash = bake::HashBakeBytes(bytes), .bakerId = "ModuleExtensionTest", .bakerVersion = "1",
            .targetProfileId = std::string{ profile.identifier }, .targetProfileHash = bake::BakeTargetProfileFingerprint(profile) };
        Require(writer.BeginAsset({ .key = key, .assetTypeId = std::string{ type } }) == bake::BakedAssetSinkStatus::Success &&
            writer.WritePrimaryBlock(bytes, profile.packageBlockAlignmentBytes) == bake::BakedAssetSinkStatus::Success &&
            writer.CommitAsset() == bake::BakedAssetSinkStatus::Success, "A package artifact was not stored");
        return key.Digest();
    };
    bake::RuntimeAssetManifest manifest{ .targetProfileId = std::string{ profile.identifier }, .targetProfileHash = bake::BakeTargetProfileFingerprint(profile) };
    manifest.descriptor.targetPlatforms = { "Windows" };
    manifest.settings.name = "ModuleExtensionTest";
    manifest.settings.defaultMap = "/Game/Main.21kbscene";
    const auto add = [&](std::string_view path, std::string_view type, std::string_view extension, const std::vector<std::uint8_t>& bytes,
                         std::vector<kb::assets::AssetId> dependencies) {
        std::vector<std::uint8_t> blob;
        Require(bake::EncodeRuntimeSourceBlob(bytes, blob), "A package source could not be encoded");
        manifest.assets.push_back({ .id = kb::assets::MakeAssetId(std::string{ path } + ':' + std::string{ type }), .type = std::string{ type },
            .name = std::filesystem::path{ path }.stem().string(), .virtualPath = std::string{ path }, .sourceExtension = std::string{ extension },
            .contentHash = bake::HashBakeBytes(bytes), .dependencies = std::move(dependencies),
            .artifacts = { { .digest = store(blob, bake::kSourceAssetTypeId), .encoding = bake::RuntimeArtifactEncoding::SourceBytes } } });
    };
    add("/Game/Main.21kbscene", "Scene", ".21kbscene", sceneBytes, {});
    add("/Game/Main.kbtest", "ModuleExtensionAsset", ".kbtest", mainBytes, { dependencyId });
    add("/Game/Dep.kbtest", "ModuleExtensionAsset", ".kbtest", dependencyBytes, {});
    std::vector<std::uint8_t> manifestBytes;
    Require(bake::EncodeRuntimeAssetManifest(manifest, manifestBytes) == bake::RuntimeAssetManifestStatus::Success, "The package manifest was refused");
    static_cast<void>(store(manifestBytes, bake::kRuntimeManifestAssetTypeId));
    Require(writer.Finish() == bake::BakedAssetSinkStatus::Success, "The package was not published");

    Scene game2;
    Attached packaged{ game2, library };
    auto pack = std::make_shared<bake::RuntimeAssetPack>();
    Require(pack->Mount(packPath, profile) == bake::RuntimeAssetPackStatus::Success && game2.Assets().Manager().MountRuntimePack(pack),
        "The package with a module asset type did not mount");
    auto fromPack = game2.Assets().Manager().Load<ModuleExtensionAsset>(mainId);
    Require(fromPack.IsLoaded() && fromPack->bytes == mainBytes, "A module asset read from a package differs from the cooked source");
    fromPack = {};
    pack->Unmount();
    std::filesystem::remove_all(folder);
}

// ---- components ------------------------------------------------------------------------------------------------

void ModuleComponentLivesInSceneAndPrefab() {
    const std::filesystem::path folder = Fresh("21kb_module_extension_components");
    const std::filesystem::path library = LibraryCopy(folder);
    const Component expected{ 2.5F, 7, true };

    Scene scene;
    Attached attached{ scene, library };
    kb::ecs::World& world = scene.Runtime().EcsWorld();
    const kb::scene::SceneObject object = scene.Entities().CreateObject({ .name = "Probe" });
    const SceneEntity entity = object.Entity();
    world.Set<Component>(entity, expected);

    std::size_t found = 0U;
    world.CreateQuery<Component>().ForEach([](SceneEntity, const Component& value, void* context) {
        if (value.count == 7) ++*static_cast<std::size_t*>(context);
    }, &found);
    Require(found == 1U, "An ECS query did not find the module component");

    const auto read = [&](std::string_view property) { return kb::script::ScriptSceneComponentApi::GetProperty(scene, entity, kComponentName, property); };
    Require(kb::script::ScriptSceneComponentApi::HasComponent(scene, entity, kComponentName) && read("strength").succeeded &&
            read("strength").value.AsFloat() == 2.5F && read("count").value.AsInt() == 7 && read("armed").value.AsBool(),
        "A script could not read the module component by name");
    Require(kb::script::ScriptSceneComponentApi::SetProperty(scene, entity, kComponentName, "count", kb::script::ScriptValue{ 9 }).succeeded &&
            ReadComponent(scene, entity).count == 9 &&
            !kb::script::ScriptSceneComponentApi::SetProperty(scene, entity, kComponentName, "count", kb::script::ScriptValue{ true }).succeeded,
        "A script could not write the module component, or a wrong type was accepted");
    world.Set<Component>(entity, expected);

    // The description the Inspector shows is made from the registered fields.
    const kb::library::LibraryComponentInspectorDesc* desc = kb::library::EngineLibraryComponentInspectorRegistry::Find(kComponentName);
    const kb::library::LibraryComponentInspectorFieldDesc* strength = kb::library::EngineLibraryComponentInspectorRegistry::FindField(kComponentName, "strength");
    const auto properties = kb::script::ScriptSceneComponentApi::ComponentProperties(kComponentName);
    Require(desc != nullptr && desc->displayName == "Module Extension" && desc->category == "Tests" && desc->fields.size() == 3U &&
            strength != nullptr && strength->displayName == "Strength" && strength->tooltip == "How hard it hits." && properties.size() == 3U &&
            properties[0].type == kb::script::ScriptValueType::Float && properties[2].type == kb::script::ScriptValueType::Bool,
        "The module component is not described to the Inspector from its registered fields");

    // A scene file keeps the values.
    const std::filesystem::path scenePath = folder / "Probe.21kbscene";
    Require(kb::scene::SceneDocumentService::Save(scene, scenePath, "Probe"), "The scene with a module component was not saved");
    {
        Scene reloaded;
        Attached reloadedModule{ reloaded, library };
        Require(kb::scene::SceneDocumentService::LoadFileIntoScene(reloaded, scenePath), "The scene with a module component was not loaded");
        std::size_t equal = 0U;
        reloaded.Runtime().EcsWorld().CreateQuery<Component>().ForEach([](SceneEntity, const Component& value, void* context) {
            if (value.strength == 2.5F && value.count == 7 && value.armed) ++*static_cast<std::size_t*>(context);
        }, &equal);
        Require(equal == 1U, "A module component changed in a scene round trip");
    }

    // A prefab keeps them too, and an instance gets its own copy.
    const std::filesystem::path prefabPath = folder / "Probe.kbprefab";
    const auto created = scene.Prefabs().CreateAsset(object, "Probe", prefabPath);
    Require(created.IsValid() && std::filesystem::exists(prefabPath) && scene.Prefabs().Unload(created), "The prefab with a module component was not saved");
    const auto loadedPrefab = scene.Prefabs().Load(prefabPath);
    Require(loadedPrefab.IsValid(), "The prefab with a module component was not loaded");
    const auto instance = scene.Prefabs().Instantiate(loadedPrefab);
    Require(!instance.Empty(), "The prefab with a module component did not instantiate");
    std::size_t instances = 0U;
    world.CreateQuery<Component>().ForEach([](SceneEntity, const Component& value, void* context) {
        if (value.strength == 2.5F && value.count == 7 && value.armed) ++*static_cast<std::size_t*>(context);
    }, &instances);
    Require(instances == 2U, "A prefab did not give its instance the module component");
    std::filesystem::remove_all(folder);
}

void UnknownComponentSurvivesTheRoundTrip() {
    const std::filesystem::path folder = Fresh("21kb_module_extension_unknown");
    const std::filesystem::path library = LibraryCopy(folder);
    const std::filesystem::path original = folder / "Original.21kbscene";
    {
        Scene scene;
        Attached attached{ scene, library };
        const SceneEntity entity = scene.Entities().CreateObject({ .name = "Probe" }).Entity();
        scene.Runtime().EcsWorld().Set<Component>(entity, { 4.0F, -3, true });
        Require(kb::scene::SceneDocumentService::Save(scene, original, "Original"), "The scene with a module component was not saved");
    }
    const auto sectionsOf = [](const std::filesystem::path& path) {
        const kb::scene::SceneDocumentLoadResult loaded = kb::scene::SceneDocumentService::Load(path);
        Require(loaded.succeeded, "A scene file did not load");
        std::vector<kb::scene::SceneModuleComponentData> sections;
        for (const kb::scene::ScenePrefabNodeDesc& node : loaded.document.worldPrefab.Nodes()) {
            sections.insert(sections.end(), node.components.moduleComponents.begin(), node.components.moduleComponents.end());
        }
        return sections;
    };
    const auto before = sectionsOf(original);
    Require(before.size() == 1U && before[0].name == kComponentName && !before[0].bytes.empty(), "The saved scene does not hold the module component");

    // No module is loaded here: the scene loads, and what it holds for the unknown type is written back as it was.
    Scene bare;
    Require(kb::scene::SceneDocumentService::LoadFileIntoScene(bare, original), "A scene with an unknown module component did not load");
    const std::filesystem::path resaved = folder / "Resaved.21kbscene";
    Require(kb::scene::SceneDocumentService::Save(bare, resaved, "Original"), "A scene with an unknown module component was not saved");
    Require(sectionsOf(resaved) == before, "An unknown module component changed while its module was not loaded");
    std::filesystem::remove_all(folder);
}

void ReloadKeepsModuleComponentData() {
    const std::filesystem::path folder = Fresh("21kb_module_extension_reload");
    const std::filesystem::path library = LibraryCopy(folder);
    Scene scene;
    Attached attached{ scene, library };
    const SceneEntity entity = scene.Entities().CreateObject({ .name = "Probe" }).Entity();
    scene.Runtime().EcsWorld().Set<Component>(entity, { 1.5F, 11, true });

    // Unloading holds the values as saved data; loading the module again turns them back into the component.
    attached.Reload();
    const Component reloaded = ReadComponent(scene, entity);
    Require(reloaded.strength == 1.5F && reloaded.count == 11 && reloaded.armed && kb::scene::SceneModuleComponents{ scene }.Diagnostics().empty(),
        "A hot reload lost the values of a module component");
    attached.Reload();
    Require(ReadComponent(scene, entity).count == 11, "A second hot reload lost the values of a module component");
    std::filesystem::remove_all(folder);
}

} // namespace

void RunModuleExtensionTests() {
    ModuleAssetTypeLoadsAndCooks();
    ModuleComponentLivesInSceneAndPrefab();
    UnknownComponentSurvivesTheRoundTrip();
    ReloadKeepsModuleComponentData();
    std::cout << "Module extension tests passed\n";
}

} // namespace kb::tests
