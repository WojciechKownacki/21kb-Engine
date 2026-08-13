#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "app/EditorAssetBrowserNativeCommandMap.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "assets/EditorAssetBrowserHitTester.hpp"
#include "assets/EditorAssetBrowserLayout.hpp"
#include "console/EditorConsoleState.hpp"
#include "engine/audio/AudioMixerAsset.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "scene/audio/EditorAudioMixerAssetGateway.hpp"
#include "scene/audio/EditorAudioMixerAuthoring.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path TestRoot() {
    return std::filesystem::temp_directory_path() / "21kb_editor_audio_mixer_authoring_tests";
}

void ResetTestRoot() {
    std::error_code error;
    std::filesystem::remove_all(TestRoot(), error);
    std::filesystem::create_directories(TestRoot() / "Project" / "Assets" / "Mixers", error);
}

[[nodiscard]] std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    return std::vector<std::uint8_t>{
        std::istreambuf_iterator<char>{ input },
        std::istreambuf_iterator<char>{},
    };
}

void RunAudioMixerProjectFilesCreationTest() {
    ResetTestRoot();
    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(scene.Assets().MountProject(TestRoot() / "Project"),
        "Audio mixer authoring project mount failed");
    static_cast<void>(browser.SelectFolder("/Game/Mixers", scene.Assets().Manager()));

    kb::editor::EditorAudioMixerAuthoring authoring{ scene, browser, console };
    kb::editor::tests::Require(authoring.Create("/Game/Mixers"),
        "Project Files must create an audio mixer through the authoring service");
    const std::filesystem::path firstPath = TestRoot() / "Project" / "Assets" / "Mixers" / "NewAudioMixer.kbmixer";
    kb::editor::tests::Require(std::filesystem::exists(firstPath) && std::filesystem::file_size(firstPath) > 0U,
        "A newly created audio mixer must be a non-empty discoverable file");
    const kb::assets::AssetId firstId = browser.InspectorAsset();
    const kb::assets::AssetMetadata* firstMetadata = scene.Assets().Manager().Registry().Find(firstId);
    kb::editor::tests::Require(firstMetadata != nullptr && firstMetadata->type == kb::audio::kAudioMixerAssetType
            && scene.Assets().Manager().IsLoaded(firstId),
        "A newly created audio mixer must be selected in the Inspector and loaded through the runtime cache");
    const std::optional<kb::audio::AudioMixerAsset> first = authoring.Read(firstId);
    kb::editor::tests::Require(first.has_value() && first->buses.empty() && first->snapshots.empty(),
        "The default Project Files audio mixer must load as a valid empty graph");

    kb::editor::tests::Require(authoring.Create("/Game/Mixers"),
        "A second audio mixer creation must choose a unique path");
    kb::editor::tests::Require(std::filesystem::exists(
            TestRoot() / "Project" / "Assets" / "Mixers" / "NewAudioMixer1.kbmixer"),
        "Audio mixer creation must use the deterministic numbered suffix");

    kb::editor::EditorAudioMixerAssetGateway gateway{ scene, browser };
    const std::filesystem::path outsideMount = TestRoot() / "Outside" / "Rejected.kbmixer";
    std::error_code outsideError;
    std::filesystem::create_directories(outsideMount.parent_path(), outsideError);
    kb::editor::tests::Require(!outsideError, "The orphan-cleanup fixture folder must be creatable");
    kb::editor::tests::Require(!gateway.Create(outsideMount, {}),
        "Creation outside a mounted project folder must fail");
    kb::editor::tests::Require(!std::filesystem::exists(outsideMount),
        "A failed discovery or selection must remove only its newly created orphan file");
    kb::editor::tests::Require(scene.Assets().Manager().Registry().FindByPath(outsideMount) == nullptr,
        "A failed mixer creation must not leave registry or cache state for the orphan path");
}

void RunAudioMixerGraphAuthoringTest() {
    ResetTestRoot();
    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(scene.Assets().MountProject(TestRoot() / "Project"),
        "Audio mixer graph authoring project mount failed");
    kb::editor::EditorAudioMixerAuthoring authoring{ scene, browser, console };
    kb::editor::tests::Require(authoring.Create("/Game/Mixers"),
        "Audio mixer graph fixture creation failed");
    const kb::assets::AssetId id = browser.InspectorAsset();
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(id);
    kb::editor::tests::Require(metadata != nullptr, "Audio mixer graph fixture metadata was not discovered");
    const std::filesystem::path path = metadata->physicalPath;

    kb::editor::tests::Require(authoring.AddBus(id, "Root")
            && authoring.AddBus(id, "Child")
            && authoring.AddBus(id, "Leaf"),
        "Audio mixer bus insertion failed");
    kb::editor::tests::Require(authoring.SetBusParent(id, "Child", "Root")
            && authoring.SetBusParent(id, "Leaf", "Child"),
        "Audio mixer parent routing failed");
    kb::editor::tests::Require(authoring.SetBusVolume(id, "Root", 0.123456791F)
            && authoring.SetBusMute(id, "Root", true),
        "Audio mixer bus value authoring failed");
    kb::editor::tests::Require(authoring.AddSnapshot(id, "Default")
            && authoring.AddSnapshot(id, "Alternate"),
        "Audio mixer snapshot insertion failed");
    kb::editor::tests::Require(authoring.AddSnapshotOverride(id, "Default", "Root", 0.5F)
            && authoring.AddSnapshotOverride(id, "Default", "Child", 0.6F)
            && authoring.SetSnapshotOverrideVolume(id, "Default", "Child", 0.7F),
        "Audio mixer snapshot override authoring failed");
    kb::editor::tests::Require(authoring.RenameBus(id, "Root", "Main")
            && authoring.RenameSnapshot(id, "Default", "Gameplay"),
        "Audio mixer rename authoring failed");

    std::optional<kb::audio::AudioMixerAsset> mixer = authoring.Read(id);
    kb::editor::tests::Require(mixer.has_value() && mixer->buses.size() == 3U
            && mixer->buses[0].name == "Main" && mixer->buses[1].parentBus == "Main"
            && mixer->buses[0].volume == 0.123456791F && mixer->buses[0].mute,
        "Bus rename must preserve deterministic order and update child parent references");
    const kb::audio::AudioMixerSnapshot* gameplay = mixer->FindSnapshot("Gameplay");
    kb::editor::tests::Require(gameplay != nullptr && gameplay->busVolumes.size() == 2U
            && gameplay->busVolumes[0].bus == "Main" && gameplay->busVolumes[1].volume == 0.7F,
        "Bus and snapshot renames must update overrides without reordering them");

    const std::vector<std::uint8_t> stableBytes = ReadBytes(path);
    const std::uint64_t stableGeneration = scene.Assets().Manager().LoadGeneration(id);
    const auto stableHandle = scene.Assets().Manager().AcquireLoaded<kb::audio::AudioMixerAsset>(id);
    const auto unchanged = [&]() {
        return ReadBytes(path) == stableBytes
            && scene.Assets().Manager().LoadGeneration(id) == stableGeneration
            && scene.Assets().Manager().AcquireLoaded<kb::audio::AudioMixerAsset>(id).Shared().get()
                == stableHandle.Shared().get();
    };
    kb::editor::tests::Require(!authoring.AddBus(id, "Main")
            && !authoring.AddBus(id, "bad name")
            && !authoring.RemoveBus(id, "Missing")
            && !authoring.RenameBus(id, "Missing", "Renamed")
            && !authoring.RenameBus(id, "Main", "Child")
            && !authoring.RenameBus(id, "Main", "bad name")
            && !authoring.RenameBus(id, "Main", "Main")
            && !authoring.SetBusParent(id, "Main", "Leaf")
            && !authoring.SetBusParent(id, "Leaf", "Missing")
            && !authoring.SetBusParent(id, "Main", "Main")
            && !authoring.SetBusParent(id, "Main", {})
            && !authoring.SetBusVolume(id, "Main", std::numeric_limits<float>::quiet_NaN())
            && !authoring.SetBusVolume(id, "Main", std::numeric_limits<float>::infinity())
            && !authoring.SetBusVolume(id, "Main", -1.0F)
            && !authoring.SetBusVolume(id, "Missing", 1.0F)
            && !authoring.SetBusVolume(id, "Main", 0.123456791F)
            && !authoring.SetBusMute(id, "Main", true)
            && !authoring.SetBusMute(id, "Missing", true)
            && !authoring.AddSnapshot(id, "Gameplay")
            && !authoring.AddSnapshot(id, "bad name")
            && !authoring.RemoveSnapshot(id, "Missing")
            && !authoring.RenameSnapshot(id, "Missing", "Renamed")
            && !authoring.RenameSnapshot(id, "Gameplay", "Alternate")
            && !authoring.RenameSnapshot(id, "Gameplay", "bad name")
            && !authoring.RenameSnapshot(id, "Gameplay", "Gameplay")
            && !authoring.AddSnapshotOverride(id, "Gameplay", "Missing", 1.0F)
            && !authoring.AddSnapshotOverride(id, "Missing", "Main", 1.0F)
            && !authoring.AddSnapshotOverride(id, "Gameplay", "Main", 1.0F)
            && !authoring.AddSnapshotOverride(id, "Gameplay", "Child", std::numeric_limits<float>::quiet_NaN())
            && !authoring.AddSnapshotOverride(id, "Gameplay", "Child", std::numeric_limits<float>::infinity())
            && !authoring.AddSnapshotOverride(id, "Gameplay", "Child", -1.0F)
            && !authoring.RemoveSnapshotOverride(id, "Missing", "Child")
            && !authoring.RemoveSnapshotOverride(id, "Gameplay", "Missing")
            && !authoring.SetSnapshotOverrideVolume(id, "Missing", "Child", 1.0F)
            && !authoring.SetSnapshotOverrideVolume(id, "Gameplay", "Missing", 1.0F)
            && !authoring.SetSnapshotOverrideVolume(id, "Gameplay", "Child", std::numeric_limits<float>::quiet_NaN())
            && !authoring.SetSnapshotOverrideVolume(id, "Gameplay", "Child", std::numeric_limits<float>::infinity())
            && !authoring.SetSnapshotOverrideVolume(id, "Gameplay", "Child", -1.0F)
            && !authoring.SetSnapshotOverrideVolume(id, "Gameplay", "Child", 0.7F),
        "Invalid, duplicate, cyclic, and no-op mixer authoring must be rejected");
    kb::editor::tests::Require(unchanged(),
        "Rejected mixer authoring must leave file bytes and cache state unchanged");

    kb::editor::tests::Require(authoring.RemoveSnapshotOverride(id, "Gameplay", "Child"),
        "Audio mixer snapshot override removal failed");
    kb::editor::tests::Require(authoring.RemoveBus(id, "Main"),
        "Audio mixer bus removal failed");
    mixer = authoring.Read(id);
    gameplay = mixer.has_value() ? mixer->FindSnapshot("Gameplay") : nullptr;
    kb::editor::tests::Require(mixer.has_value() && mixer->buses.size() == 2U
            && mixer->buses[0].name == "Child" && mixer->buses[0].parentBus.empty()
            && mixer->buses[1].name == "Leaf" && mixer->buses[1].parentBus == "Child"
            && gameplay != nullptr && gameplay->busVolumes.empty(),
        "Removing a bus must reparent direct children and remove its snapshot overrides atomically");
    kb::editor::tests::Require(authoring.RemoveSnapshot(id, "Alternate")
            && !authoring.RemoveSnapshot(id, "Missing")
            && !authoring.RemoveSnapshotOverride(id, "Gameplay", "Missing"),
        "Snapshot and override removal must report real mutations only");

    const std::uint64_t generationAfterEdits = scene.Assets().Manager().LoadGeneration(id);
    static_cast<void>(scene.Assets().Manager().Unload(id));
    static_cast<void>(scene.Assets().Discover());
    const auto reloaded = scene.Assets().Manager().Load<kb::audio::AudioMixerAsset>(id);
    kb::editor::tests::Require(reloaded.IsLoaded() && reloaded->FindBus("Child") != nullptr
            && reloaded->FindSnapshot("Alternate") == nullptr
            && scene.Assets().Manager().LoadGeneration(id) > generationAfterEdits,
        "Unload, discovery, and runtime load must expose the persisted authored mixer content");
}

void RunAudioMixerProjectFilesCommandModelTest() {
    kb::assets::AssetManager manager;
    const kb::assets::AssetMetadata metadata{
        .id = kb::assets::AssetId{ 101U },
        .type = kb::audio::kAudioMixerAssetType,
        .name = "Existing",
        .virtualPath = "/Game/Mixers/Existing.kbmixer",
    };
    static_cast<void>(manager.RegisterAsset(metadata));
    kb::editor::EditorAssetBrowserState browser;
    static_cast<void>(browser.OpenContextMenuForBackground(10, 10));
    const std::vector<kb::editor::EditorAssetContextMenuItem> background = browser.ContextMenuItems(manager);
    const auto item = std::find_if(background.begin(), background.end(), [](const auto& candidate) {
        return candidate.command == kb::editor::EditorAssetContextCommand::NewAudioMixer;
    });
    kb::editor::tests::Require(item != background.end() && std::string_view{ item->label } == "New Audio Mixer",
        "Project Files background menu must expose New Audio Mixer");

    kb::editor::tests::Require(browser.OpenContextMenuForFolder(10, 10, "/Game/Mixers", manager)
            && browser.ContextMenuTargetKind() == kb::editor::EditorAssetContextTargetKind::Folder
            && browser.ContextMenuTargetFolder() == "/Game/Mixers",
        "Project Files folder context menu must open for the requested target path");
    const std::vector<kb::editor::EditorAssetContextMenuItem> folder = browser.ContextMenuItems(manager);
    const auto folderItem = std::find_if(folder.begin(), folder.end(), [](const auto& candidate) {
        return candidate.command == kb::editor::EditorAssetContextCommand::NewAudioMixer;
    });
    kb::editor::tests::Require(folderItem != folder.end(),
        "Project Files folder menu must expose New Audio Mixer");
#if defined(_WIN32)
    const std::size_t rowIndex = static_cast<std::size_t>(std::distance(folder.begin(), folderItem));
    const RECT content{ 0, 0, 640, 480 };
    const RECT menu = kb::editor::EditorAssetBrowserLayout::ContextMenuRect(
        content,
        browser.ContextMenuX(),
        browser.ContextMenuY(),
        static_cast<int>(folder.size()));
    const RECT row = kb::editor::EditorAssetBrowserLayout::ContextMenuItemRect(menu, static_cast<int>(rowIndex));
    const kb::editor::EditorAssetBrowserHit hit = kb::editor::EditorAssetBrowserHitTester::HitTest(
        content,
        (row.left + row.right) / 2,
        (row.top + row.bottom) / 2,
        browser,
        manager);
    kb::editor::tests::Require(hit.kind == kb::editor::EditorAssetBrowserHitKind::ContextMenuCommand
            && hit.command == kb::editor::EditorAssetContextCommand::NewAudioMixer,
        "The New Audio Mixer folder-menu row must map through layout and hit testing");
#endif

    const std::vector<kb::editor::EditorAssetContextCommand> mappedCommands{
        kb::editor::EditorAssetContextCommand::Import,
        kb::editor::EditorAssetContextCommand::NewFolder,
        kb::editor::EditorAssetContextCommand::NewLuaScript,
        kb::editor::EditorAssetContextCommand::NewMaterial,
        kb::editor::EditorAssetContextCommand::NewMaterialFunction,
        kb::editor::EditorAssetContextCommand::NewMaterialGraph,
        kb::editor::EditorAssetContextCommand::NewMaterialType,
        kb::editor::EditorAssetContextCommand::CreateMaterialInstance,
        kb::editor::EditorAssetContextCommand::CreateMaterialFromGraph,
        kb::editor::EditorAssetContextCommand::CreateMaterialFromMaterialType,
        kb::editor::EditorAssetContextCommand::NewInputAction,
        kb::editor::EditorAssetContextCommand::NewInputAxis,
        kb::editor::EditorAssetContextCommand::NewInputMappingContext,
        kb::editor::EditorAssetContextCommand::NewAudioMixer,
        kb::editor::EditorAssetContextCommand::ExtractMaterials,
        kb::editor::EditorAssetContextCommand::AddDirectionalLight,
        kb::editor::EditorAssetContextCommand::AddPointLight,
        kb::editor::EditorAssetContextCommand::AddSpotLight,
        kb::editor::EditorAssetContextCommand::Rename,
        kb::editor::EditorAssetContextCommand::Delete,
        kb::editor::EditorAssetContextCommand::Refresh,
    };
    for (const kb::editor::EditorAssetContextCommand command : mappedCommands) {
        const std::uint32_t id = kb::editor::EditorAssetBrowserNativeCommandMap::Id(command);
        kb::editor::tests::Require(id != 0U
                && kb::editor::EditorAssetBrowserNativeCommandMap::Command(id) == command,
            "Every native Project Files command must round-trip through the canonical command map");
    }
}

} // namespace

namespace kb::editor::tests {

void RunEditorAudioMixerAuthoringTests() {
    RunAudioMixerProjectFilesCreationTest();
    RunAudioMixerGraphAuthoringTest();
    RunAudioMixerProjectFilesCommandModelTest();
}

} // namespace kb::editor::tests
