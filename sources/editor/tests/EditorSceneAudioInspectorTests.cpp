#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "assets/EditorAssetBrowserState.hpp"
#include "app/EditorAudioInspectorDropPolicy.hpp"
#include "commands/EditorCommandStack.hpp"
#include "console/EditorConsoleState.hpp"
#include "engine/audio/AudioMixerAssetIO.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"
#include "engine/scene/SceneAudioOcclusionAccess.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "inspection/InspectorSceneAudioInteraction.hpp"
#include "inspection/InspectorSceneAudioModel.hpp"
#include "platform/win32/EditorAudioMixerAssetPickerDialog.hpp"
#include "rendering/InspectorSceneAudioView.hpp"
#include "scene/EditorHierarchyExpansionState.hpp"
#include "scene/EditorHierarchySearchState.hpp"
#include "scene/EditorHierarchySelectionState.hpp"
#include "scene/EditorSceneCommandController.hpp"
#include "scene/EditorSceneViewportStateStore.hpp"
#include "scene/EditorSceneDocumentIdentity.hpp"
#include "scene/audio/EditorSceneAudioSettingsService.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path TestRoot() {
    return std::filesystem::temp_directory_path() / "21kb_scene_audio_inspector_tests";
}

void ResetTestRoot() {
    std::error_code error;
    std::filesystem::remove_all(TestRoot(), error);
    std::filesystem::create_directories(TestRoot() / "Project" / "Assets" / "Mixers", error);
    kb::editor::tests::Require(!error, "Scene Audio Inspector fixture directory creation failed");
}

struct CommandFixture {
    kb::scene::Scene scene;
    kb::editor::EditorCommandStack stack;
    kb::editor::EditorConsoleState console;
    kb::editor::EditorSceneViewportStateStore viewport;
    kb::editor::EditorHierarchySelectionState selection;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorHierarchyExpansionState expansion;
    kb::editor::EditorHierarchySearchState search;
    std::optional<std::string> transaction;
    std::uint64_t renderRevision = 0U;
    std::uint64_t dirtyBase = 0U;
    std::vector<std::uint64_t> dirtyEntities;
    bool fullDirty = false;
    bool documentDirty = false;
    bool hierarchyDirty = false;

    [[nodiscard]] kb::editor::EditorSceneCommandController Controller() {
        return kb::editor::EditorSceneCommandController{
            scene,
            stack,
            console,
            viewport,
            selection,
            browser,
            expansion,
            search,
            transaction,
            renderRevision,
            dirtyBase,
            dirtyEntities,
            fullDirty,
            documentDirty,
            hierarchyDirty,
        };
    }

    [[nodiscard]] kb::editor::EditorSceneAudioSettingsService Service() {
        return kb::editor::EditorSceneAudioSettingsService{
            scene,
            [this](std::string label, kb::editor::EditorSceneAudioSettingsService::Mutation mutation) {
                return Controller().Execute(std::move(label), std::move(mutation));
            },
        };
    }
};

[[nodiscard]] const kb::editor::InspectorSceneAudioRow& FindRow(
    const kb::editor::InspectorSceneAudioModel& model,
    kb::editor::InspectorPropertyId property,
    std::string_view option = {}) {
    const auto found = std::ranges::find_if(model.Rows(), [&](const kb::editor::InspectorSceneAudioRow& row) {
        return row.property == property && (option.empty() || row.option == option);
    });
    kb::editor::tests::Require(found != model.Rows().end(), "Expected Scene Audio Inspector row is missing");
    return *found;
}

[[nodiscard]] kb::assets::AssetId CreateMixer(
    CommandFixture& fixture,
    const std::filesystem::path& path,
    kb::audio::AudioMixerAsset mixer) {
    kb::editor::tests::Require(kb::audio::AudioMixerAssetIO::Save(path, mixer),
        "Scene Audio Inspector mixer fixture save failed");
    static_cast<void>(fixture.scene.Assets().Discover());
    const auto metadataIt = std::ranges::find_if(
        fixture.scene.Assets().Manager().Registry().All(),
        [&path](const kb::assets::AssetMetadata& metadata) { return metadata.physicalPath == path; });
    const kb::assets::AssetMetadata* metadata = metadataIt == fixture.scene.Assets().Manager().Registry().All().end()
        ? nullptr
        : std::addressof(*metadataIt);
    kb::editor::tests::Require(metadata != nullptr && metadata->type == kb::audio::kAudioMixerAssetType,
        "Scene Audio Inspector mixer fixture was not discovered");
    const auto loaded = fixture.scene.Assets().Manager().Load<kb::audio::AudioMixerAsset>(metadata->id);
    kb::editor::tests::Require(loaded.IsLoaded(), "Scene Audio Inspector mixer fixture did not load");
    return metadata->id;
}

void ReplaceEditText(kb::editor::InspectorPanelState& state, std::string_view text) {
    state.SelectAllText();
    state.InsertText(text);
}

#if defined(_WIN32)
[[nodiscard]] kb::editor::InspectorSceneAudioTarget HitRow(
    const RECT& content,
    const kb::editor::InspectorPanelState& state,
    const kb::editor::InspectorSceneAudioModel& model,
    const kb::editor::InspectorSceneAudioRow& row) {
    const std::optional<RECT> bounds = kb::editor::InspectorSceneAudioView::RowBounds(
        content, state, model, row.flatIndex);
    kb::editor::tests::Require(bounds.has_value(), "Scene Audio Inspector row has no shared geometry");
    int x = (bounds->left + bounds->right) / 2;
    if (row.kind == kb::editor::InspectorSceneAudioRowKind::Text) {
        x = bounds->left + ((bounds->right - bounds->left) * 70 / 100);
    } else if (row.kind == kb::editor::InspectorSceneAudioRowKind::Bool) {
        x = bounds->left + ((bounds->right - bounds->left) * 36 / 100) + 8;
    }
    const int y = (bounds->top + bounds->bottom) / 2;
    const kb::editor::InspectorSceneAudioHit hit =
        kb::editor::InspectorSceneAudioView::HitTest(content, state, model, x, y);
    kb::editor::tests::Require(hit.index == row.flatIndex
            && hit.section == row.section
            && hit.property == row.property,
        "Scene Audio Inspector hit did not resolve the shared flat row");
    return kb::editor::InspectorSceneAudioTarget{
        .kind = hit.kind,
        .section = hit.section,
        .property = hit.property,
        .index = hit.index,
    };
}

void PaintSmoke(
    const RECT& content,
    const kb::editor::InspectorPanelState& state,
    const kb::editor::InspectorSceneAudioModel& model) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = content.right - content.left;
    info.bmiHeader.biHeight = -(content.bottom - content.top);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    kb::editor::tests::Require(bitmap != nullptr && pixels != nullptr && dc != nullptr,
        "Scene Audio Inspector test backbuffer creation failed");
    HGDIOBJ previous = SelectObject(dc, bitmap);
    kb::editor::InspectorSceneAudioView::Paint(
        dc, content, kb::editor::MakeEditorDarkTheme(), state, 41U, model);
    GdiFlush();
    const auto* bytes = static_cast<const std::uint8_t*>(pixels);
    const std::size_t size = static_cast<std::size_t>(info.bmiHeader.biWidth)
        * static_cast<std::size_t>(-info.bmiHeader.biHeight) * 4U;
    kb::editor::tests::Require(std::ranges::any_of(
            std::span<const std::uint8_t>{ bytes, size }, [](std::uint8_t value) { return value != 0U; }),
        "Scene Audio Inspector production paint produced an empty frame");
    SelectObject(dc, previous);
    DeleteDC(dc);
    DeleteObject(bitmap);
}
#endif

void RunSceneAudioInspectorTest() {
    ResetTestRoot();
    kb::editor::EditorSceneDocumentIdentity documentIdentity;
    const std::uint64_t initialDocumentGeneration = documentIdentity.Generation();
    kb::editor::tests::Require(initialDocumentGeneration != 0U
            && documentIdentity.Generation() == initialDocumentGeneration,
        "Same-document operations must keep a stable document generation");
    documentIdentity.Advance();
    kb::editor::tests::Require(documentIdentity.Generation() != initialDocumentGeneration,
        "Document replacement must advance the Inspector owner generation");

    CommandFixture fixture;
    kb::scene::AudioOcclusionSettings fixtureOcclusion =
        kb::scene::SceneAudioOcclusionAccess::Settings(fixture.scene);
    fixtureOcclusion.layerMask = 0x00FFFFFFU;
    kb::editor::tests::Require(
        kb::scene::SceneAudioOcclusionAccess::Configure(fixture.scene, fixtureOcclusion),
        "Scene Audio Inspector initial occlusion fixture is invalid");
    kb::editor::tests::Require(fixture.scene.Assets().MountProject(TestRoot() / "Project"),
        "Scene Audio Inspector project mount failed");
    const std::filesystem::path mixerPath =
        TestRoot() / "Project" / "Assets" / "Mixers" / "Scene.kbmixer";
    kb::audio::AudioMixerAsset primaryMixer{
        .buses = { kb::audio::AudioMixerBus{ .name = "World" } },
        .snapshots = {
            kb::audio::AudioMixerSnapshot{ .name = "Calm" },
            kb::audio::AudioMixerSnapshot{ .name = "Action" },
        },
    };
    for (int index = 0; index < 12; ++index) {
        primaryMixer.snapshots.push_back(kb::audio::AudioMixerSnapshot{
            .name = "State" + std::to_string(index),
        });
    }
    const kb::assets::AssetId mixerId = CreateMixer(
        fixture,
        mixerPath,
        primaryMixer);
    const kb::assets::AssetId secondMixerId = CreateMixer(
        fixture,
        TestRoot() / "Project" / "Assets" / "Mixers" / "Second.kbmixer",
        kb::audio::AudioMixerAsset{
            .buses = { kb::audio::AudioMixerBus{ .name = "Other" } },
            .snapshots = { kb::audio::AudioMixerSnapshot{ .name = "OtherState" } },
        });

    kb::editor::tests::Require(
        kb::editor::EditorAudioMixerAssetPickerDialog::MatchesFilter(
            *fixture.scene.Assets().Manager().Registry().Find(mixerId)),
        "Scene Audio picker must accept mixer metadata");
    kb::assets::AssetMetadata wrongMetadata;
    wrongMetadata.id = kb::assets::AssetId{ 8001U };
    wrongMetadata.type = "AudioClip";
    wrongMetadata.name = "Wrong";
    wrongMetadata.virtualPath = "/Game/Mixers/Wrong.wav";
    kb::editor::tests::Require(fixture.scene.Assets().Manager().Registry().Upsert(wrongMetadata),
        "Wrong-type mixer candidate fixture metadata could not be registered");
    kb::editor::tests::Require(
        !kb::editor::EditorAudioMixerAssetPickerDialog::MatchesFilter(wrongMetadata),
        "Scene Audio picker must reject non-mixer metadata");
    kb::editor::tests::Require(
        !kb::editor::InspectorSceneAudioInteraction::ResolvePicker(false, mixerId).has_value()
            && kb::editor::InspectorSceneAudioInteraction::ResolvePicker(true, mixerId)->mixer == mixerId,
        "Scene Audio picker cancel and accepted results must be resolved explicitly");
    const kb::assets::AssetMetadata mixerMetadata =
        *fixture.scene.Assets().Manager().Registry().Find(mixerId);
    kb::editor::tests::Require(
        kb::editor::EditorAudioInspectorDropPolicy::Accepts(
            mixerMetadata,
            kb::editor::InspectorSectionId::SceneAudioRouting,
            kb::editor::InspectorPropertyId::SceneAudioMixer)
            && !kb::editor::EditorAudioInspectorDropPolicy::Accepts(
                wrongMetadata,
                kb::editor::InspectorSectionId::SceneAudioRouting,
                kb::editor::InspectorPropertyId::SceneAudioMixer)
            && !kb::editor::EditorAudioInspectorDropPolicy::Accepts(
                mixerMetadata,
                kb::editor::InspectorSectionId::AudioSource,
                kb::editor::InspectorPropertyId::AudioSourceClip)
            && kb::editor::EditorAudioInspectorDropPolicy::Accepts(
                wrongMetadata,
                kb::editor::InspectorSectionId::AudioSource,
                kb::editor::InspectorPropertyId::AudioSourceClip),
        "Audio Inspector drop policy must enforce cross-type Scene Mixer and Audio Source targets");

    kb::assets::AssetMetadata corruptMetadata;
    corruptMetadata.id = kb::assets::AssetId{ 88001U };
    corruptMetadata.type = kb::audio::kAudioMixerAssetType;
    corruptMetadata.name = "Corrupt";
    corruptMetadata.physicalPath = TestRoot() / "Project" / "Assets" / "Mixers" / "Corrupt.kbmixer";
    corruptMetadata.virtualPath = "/Game/Mixers/Corrupt.kbmixer";
    {
        std::ofstream malformed{ corruptMetadata.physicalPath, std::ios::binary | std::ios::trunc };
        malformed << "kbmixer 1\nbus Broken - nan 0\n";
        kb::editor::tests::Require(malformed.good(),
            "Malformed mixer candidate fixture could not be written");
    }
    kb::editor::tests::Require(fixture.scene.Assets().Manager().Registry().Upsert(corruptMetadata),
        "Corrupt mixer candidate fixture metadata could not be registered");
    kb::editor::EditorSceneAudioSettingsService service = fixture.Service();
    const std::size_t candidateHistory = fixture.stack.UndoCount();
    const bool candidateDirty = fixture.documentDirty;
    const kb::scene::AudioOcclusionSettings candidateOcclusion =
        kb::scene::SceneAudioOcclusionAccess::Settings(fixture.scene);
    const auto applyPickerCandidate = [&](kb::assets::AssetId id) {
        const std::optional<kb::editor::InspectorSceneAudioCommand> command =
            kb::editor::InspectorSceneAudioInteraction::ResolvePicker(true, id);
        return command.has_value()
            && kb::editor::InspectorSceneAudioInteraction::Apply(service, *command);
    };
    kb::editor::tests::Require(!applyPickerCandidate(wrongMetadata.id)
            && !applyPickerCandidate(corruptMetadata.id)
            && !applyPickerCandidate(kb::assets::AssetId{ 999001U })
            && fixture.stack.UndoCount() == candidateHistory
            && fixture.documentDirty == candidateDirty
            && kb::scene::SceneAudioMixerAccess::ActiveMixer(fixture.scene) == 0U
            && kb::scene::SceneAudioMixerAccess::ActiveSnapshot(fixture.scene).empty()
            && kb::scene::SceneAudioOcclusionAccess::Settings(fixture.scene).enabled == candidateOcclusion.enabled
            && kb::scene::SceneAudioOcclusionAccess::Settings(fixture.scene).occludedVolumeScale == candidateOcclusion.occludedVolumeScale
            && kb::scene::SceneAudioOcclusionAccess::Settings(fixture.scene).maxDistance == candidateOcclusion.maxDistance
            && kb::scene::SceneAudioOcclusionAccess::Settings(fixture.scene).layerMask == candidateOcclusion.layerMask
            && kb::scene::SceneAudioOcclusionAccess::Settings(fixture.scene).maxRaycastsPerTick == candidateOcclusion.maxRaycastsPerTick,
        "Missing, wrong-type, and corrupt mixer candidates must not mutate history");

    static_cast<void>(fixture.scene.Assets().Manager().Unload(secondMixerId));
    kb::scene::SceneAudioMixerAccess::SetActiveMixer(fixture.scene, secondMixerId.value);
    static_cast<void>(fixture.scene.Assets().Manager().Load<kb::audio::AudioMixerAsset>(
        kb::assets::AssetId{ 991001U }));
    const std::string unloadedControlError = fixture.scene.Assets().Manager().LastError();
    const kb::editor::InspectorSceneAudioModel unloadedModelA{ fixture.scene };
    const kb::editor::InspectorSceneAudioModel unloadedModelB{ fixture.scene };
    kb::editor::tests::Require(!fixture.scene.Assets().Manager().IsLoaded(secondMixerId)
            && !unloadedControlError.empty()
            && fixture.scene.Assets().Manager().LastError() == unloadedControlError
            && unloadedModelA.Text().find("invalid") != std::string::npos
            && unloadedModelB.Text().find("invalid") != std::string::npos,
        "Repeated Scene Audio view models must not invoke the loader for an unloaded mixer");
    kb::editor::EditorSceneAudioSettingsService::PrepareDocument(fixture.scene);
    kb::editor::tests::Require(fixture.scene.Assets().Manager().IsLoaded(secondMixerId),
        "Explicit document preparation must load the active mixer outside the per-frame view path");

    kb::scene::SceneAudioMixerAccess::SetActiveMixer(fixture.scene, corruptMetadata.id.value);
    static_cast<void>(fixture.scene.Assets().Manager().Load<kb::audio::AudioMixerAsset>(
        kb::assets::AssetId{ 991002U }));
    const std::string corruptControlError = fixture.scene.Assets().Manager().LastError();
    const kb::editor::InspectorSceneAudioModel corruptModelA{ fixture.scene };
    const kb::editor::InspectorSceneAudioModel corruptModelB{ fixture.scene };
    kb::editor::tests::Require(!fixture.scene.Assets().Manager().IsLoaded(corruptMetadata.id)
            && !corruptControlError.empty()
            && fixture.scene.Assets().Manager().LastError() == corruptControlError
            && corruptModelA.Text().find("invalid") != std::string::npos
            && corruptModelB.Text().find("invalid") != std::string::npos,
        "Repeated Scene Audio view models must not retry IO for a corrupt mixer");
    kb::scene::SceneAudioMixerAccess::SetActiveMixer(fixture.scene, 0U);

    const std::optional<kb::editor::InspectorSceneAudioCommand> pickerAssignment =
        kb::editor::InspectorSceneAudioInteraction::ResolvePicker(true, mixerId);
    kb::editor::tests::Require(pickerAssignment.has_value()
            && kb::editor::InspectorSceneAudioInteraction::Apply(service, *pickerAssignment),
        "Accepted Scene Audio picker result did not execute the production mixer command");
    kb::editor::tests::Require(!service.SetSceneAudioMixer(mixerId), "Same Scene Audio mixer must be a no-op");
    kb::editor::tests::Require(fixture.stack.UndoCount() == 1U && fixture.documentDirty,
        "A mixer command must create exactly one dirty history entry");
    kb::editor::tests::Require(service.SetSceneAudioSnapshot("Calm")
            && kb::scene::SceneAudioMixerAccess::SetBusVolumeOverride(
                fixture.scene, "World", 0.75F)
            && kb::scene::SceneAudioMixerAccess::BeginSnapshotTransition(
                fixture.scene, "Action", 1.0F),
        "Mixer change contract fixture failed");
    const std::size_t sameMixerHistory = fixture.stack.UndoCount();
    kb::editor::tests::Require(!service.SetSceneAudioMixer(mixerId)
            && fixture.stack.UndoCount() == sameMixerHistory
            && kb::scene::SceneAudioMixerAccess::ActiveSnapshot(fixture.scene) == "Calm"
            && !kb::scene::SceneAudioMixerAccess::BusVolumeOverrides(fixture.scene).empty()
            && kb::scene::SceneAudioMixerAccess::SnapshotTransition(fixture.scene).IsActive(),
        "Same-id mixer command must preserve snapshot and runtime mixer state without history");
    kb::editor::tests::Require(service.SetSceneAudioMixer(secondMixerId)
            && kb::scene::SceneAudioMixerAccess::ActiveSnapshot(fixture.scene).empty()
            && kb::scene::SceneAudioMixerAccess::BusVolumeOverrides(fixture.scene).empty()
            && !kb::scene::SceneAudioMixerAccess::SnapshotTransition(fixture.scene).IsActive(),
        "Changed mixer command must clear snapshot and runtime mixer state");
    kb::editor::tests::Require(fixture.Controller().Undo()
            && kb::scene::SceneAudioMixerAccess::ActiveMixer(fixture.scene) == mixerId.value
            && kb::scene::SceneAudioMixerAccess::ActiveSnapshot(fixture.scene) == "Calm",
        "Undo must restore serialized mixer and snapshot selection");
    kb::editor::tests::Require(fixture.Controller().Redo()
            && kb::scene::SceneAudioMixerAccess::ActiveMixer(fixture.scene) == secondMixerId.value
            && kb::scene::SceneAudioMixerAccess::ActiveSnapshot(fixture.scene).empty(),
        "Redo must restore the changed serialized mixer selection");
    kb::editor::tests::Require(fixture.Controller().Undo(),
        "Mixer contract fixture could not restore its primary mixer");
    static_cast<void>(fixture.Service().SetSceneAudioSnapshot(""));

    const kb::scene::AudioOcclusionSettings originalOcclusion =
        kb::scene::SceneAudioOcclusionAccess::Settings(fixture.scene);
    const kb::scene::AudioOcclusionSettings historyOcclusion{
        true, 0.2F, 32.5F, 0x12345678U, 256U
    };
    kb::editor::tests::Require(fixture.Service().SetSceneAudioOcclusion(historyOcclusion)
            && fixture.Controller().Undo(),
        "Occlusion settings command or undo failed");
    const kb::scene::AudioOcclusionSettings& undoneOcclusion =
        kb::scene::SceneAudioOcclusionAccess::Settings(fixture.scene);
    kb::editor::tests::Require(undoneOcclusion.enabled == originalOcclusion.enabled
            && undoneOcclusion.occludedVolumeScale == originalOcclusion.occludedVolumeScale
            && undoneOcclusion.maxDistance == originalOcclusion.maxDistance
            && undoneOcclusion.layerMask == originalOcclusion.layerMask
            && undoneOcclusion.maxRaycastsPerTick == originalOcclusion.maxRaycastsPerTick,
        "Undo must restore every serialized occlusion field");
    kb::editor::tests::Require(fixture.Controller().Redo(),
        "Occlusion settings redo failed");
    const kb::scene::AudioOcclusionSettings& redoneOcclusion =
        kb::scene::SceneAudioOcclusionAccess::Settings(fixture.scene);
    kb::editor::tests::Require(redoneOcclusion.enabled == historyOcclusion.enabled
            && redoneOcclusion.occludedVolumeScale == historyOcclusion.occludedVolumeScale
            && redoneOcclusion.maxDistance == historyOcclusion.maxDistance
            && redoneOcclusion.layerMask == historyOcclusion.layerMask
            && redoneOcclusion.maxRaycastsPerTick == historyOcclusion.maxRaycastsPerTick,
        "Redo must restore every serialized occlusion field");
    kb::editor::tests::Require(fixture.Controller().Undo(),
        "Occlusion history fixture could not restore its original state");

    kb::editor::InspectorPanelState state;
    kb::editor::InspectorSceneAudioModel model{ fixture.scene };
    kb::editor::tests::Require(model.MixerId() == mixerId
            && model.Text().find("Scene Audio") != std::string::npos
            && model.Text().find("Calm") != std::string::npos,
        "Scene Audio shared model did not expose the current scene-global state");
    const auto& revealMixer = FindRow(model, kb::editor::InspectorPropertyId::SceneAudioMixer);
    kb::editor::tests::Require(
        kb::editor::InspectorSceneAudioInteraction::ResolveReveal(
            model,
            { kb::editor::InspectorHitKind::Row,
              revealMixer.section,
              revealMixer.property,
              revealMixer.flatIndex }) == mixerId,
        "Mixer asset-value action must reveal the exact assigned mixer");
    kb::editor::tests::Require(
        kb::editor::InspectorSceneAudioModel::ShouldDisplay(fixture.scene, {}, {})
            && !kb::editor::InspectorSceneAudioModel::ShouldDisplay(fixture.scene, mixerId, {}),
        "Scene Audio Inspector discoverability must require no selected asset");
    const kb::scene::SceneObject liveObject =
        fixture.scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Selected" });
    kb::editor::tests::Require(liveObject.IsValid()
            && !kb::editor::InspectorSceneAudioModel::ShouldDisplay(
                fixture.scene, {}, liveObject.Entity()),
        "A live entity selection must own the Inspector instead of Scene Audio");
    fixture.scene.Entities().Destroy(liveObject);

    const auto enabledRow = FindRow(model, kb::editor::InspectorPropertyId::SceneAudioOcclusionEnabled);

#if defined(_WIN32)
    const RECT content{ 0, 0, 390, 520 };
    kb::editor::tests::Require(
        kb::editor::InspectorSceneAudioView::ContentHeight(state, model) > 0,
        "Scene Audio Inspector content height must be positive");
    const int expandedHeight = kb::editor::InspectorSceneAudioView::ContentHeight(state, model);
    state.ToggleCollapsed(kb::editor::InspectorSectionId::SceneAudioRouting);
    kb::editor::tests::Require(
        kb::editor::InspectorSceneAudioView::ContentHeight(state, model) < expandedHeight
            && !kb::editor::InspectorSceneAudioView::RowBounds(
                content, state, model, revealMixer.flatIndex).has_value(),
        "Collapsing Scene Audio routing must reduce height and remove row hit geometry");
    state.ToggleCollapsed(kb::editor::InspectorSectionId::SceneAudioRouting);
    PaintSmoke(content, state, model);
    const auto pickerRow = FindRow(model, kb::editor::InspectorPropertyId::SceneAudioMixer);
    const std::optional<RECT> pickerBounds =
        kb::editor::InspectorSceneAudioView::RowBounds(content, state, model, pickerRow.flatIndex);
    kb::editor::tests::Require(pickerBounds.has_value(), "Mixer picker row has no geometry");
    const auto pickerHit = kb::editor::InspectorSceneAudioView::HitTest(
        content, state, model, pickerBounds->right - 24, (pickerBounds->top + pickerBounds->bottom) / 2);
    kb::editor::tests::Require(pickerHit.property == kb::editor::InspectorPropertyId::SceneAudioMixerPicker,
        "Mixer picker sub-rectangle must have its exact shared hit property");
    kb::editor::tests::Require(
        state.SetHover(pickerHit.kind, pickerHit.section, pickerHit.property, pickerHit.index),
        "Mixer picker hover state did not change");
    kb::editor::tests::Require(state.IsHovered(
            pickerHit.kind, pickerHit.section, kb::editor::InspectorPropertyId::SceneAudioMixerPicker, pickerHit.index),
        "Mixer picker hover identity must preserve the picker property");
    PaintSmoke(content, state, model);
    state.ClearHover();

    const auto& lastSnapshot = FindRow(
        model, kb::editor::InspectorPropertyId::SceneAudioSnapshotOption, "State11");
    const std::optional<RECT> lastBounds = kb::editor::InspectorSceneAudioView::RowBounds(
        content, state, model, lastSnapshot.flatIndex);
    kb::editor::tests::Require(lastBounds.has_value() && lastBounds->bottom > content.bottom,
        "Long Scene Audio snapshot options must produce scrollable shared geometry");
    RECT scrolledContent = content;
    const int scroll = lastBounds->bottom - content.bottom + 10;
    OffsetRect(&scrolledContent, 0, -scroll);
    const std::optional<RECT> scrolledBounds = kb::editor::InspectorSceneAudioView::RowBounds(
        scrolledContent, state, model, lastSnapshot.flatIndex);
    kb::editor::tests::Require(scrolledBounds.has_value(),
        "Scrolled Scene Audio row geometry is missing");
    const auto scrolledHit = kb::editor::InspectorSceneAudioView::HitTest(
        scrolledContent,
        state,
        model,
        (scrolledBounds->left + scrolledBounds->right) / 2,
        (scrolledBounds->top + scrolledBounds->bottom) / 2);
    kb::editor::tests::Require(scrolledHit.index == lastSnapshot.flatIndex
            && scrolledHit.property == kb::editor::InspectorPropertyId::SceneAudioSnapshotOption,
        "Scrolled Scene Audio HitTest must resolve the exact shared flat row");

    const std::optional<RECT> enabledBounds =
        kb::editor::InspectorSceneAudioView::RowBounds(content, state, model, enabledRow.flatIndex);
    kb::editor::tests::Require(enabledBounds.has_value(), "Occlusion enabled row has no geometry");
    const auto labelHit = kb::editor::InspectorSceneAudioView::HitTest(
        content, state, model, enabledBounds->left + 8, (enabledBounds->top + enabledBounds->bottom) / 2);
    kb::editor::tests::Require(labelHit.kind == kb::editor::InspectorHitKind::Row
            && !kb::editor::InspectorSceneAudioInteraction::ResolveAction(
                fixture.scene,
                model,
                { labelHit.kind, labelHit.section, labelHit.property, labelHit.index }).has_value(),
        "Occlusion label margin must not toggle its checkbox");

    const kb::editor::InspectorSceneAudioTarget enabledTarget = HitRow(content, state, model, enabledRow);
#else
    const kb::editor::InspectorSceneAudioTarget enabledTarget{
        .kind = kb::editor::InspectorHitKind::BoolField,
        .section = enabledRow.section,
        .property = enabledRow.property,
        .index = enabledRow.flatIndex,
    };
#endif
    kb::editor::tests::Require(
        kb::editor::InspectorSceneAudioInteraction::HandlePointerDown(
            state, fixture.scene, model, enabledTarget, service, 41U),
        "Occlusion enabled interaction was not handled");
    kb::editor::tests::Require(
        kb::scene::SceneAudioOcclusionAccess::Settings(fixture.scene).enabled,
        "Occlusion enabled interaction did not execute the production command");

    model = kb::editor::InspectorSceneAudioModel{ fixture.scene };
    const auto& clearMixer = FindRow(model, kb::editor::InspectorPropertyId::SceneAudioMixerClear);
#if defined(_WIN32)
    const std::optional<RECT> clearBounds =
        kb::editor::InspectorSceneAudioView::RowBounds(content, state, model, clearMixer.flatIndex);
    kb::editor::tests::Require(clearBounds.has_value()
            && kb::editor::InspectorSceneAudioView::HitTest(
                content, state, model, clearBounds->left + 2, (clearBounds->top + clearBounds->bottom) / 2).kind
                == kb::editor::InspectorHitKind::None,
        "Destructive mixer clear action must not hit outside its inset button");
#endif
    kb::editor::tests::Require(kb::editor::InspectorSceneAudioInteraction::HandlePointerDown(
            state,
            fixture.scene,
            model,
            { kb::editor::InspectorHitKind::Row, clearMixer.section, clearMixer.property, clearMixer.flatIndex },
            service,
            41U)
            && kb::scene::SceneAudioMixerAccess::ActiveMixer(fixture.scene) == 0U,
        "Clear mixer interaction did not execute its production command");
    kb::editor::tests::Require(fixture.Controller().Undo()
            && kb::scene::SceneAudioMixerAccess::ActiveMixer(fixture.scene) == mixerId.value,
        "Undo must restore the mixer cleared through the Inspector interaction");

    model = kb::editor::InspectorSceneAudioModel{ fixture.scene };
    for (const kb::audio::AudioMixerSnapshot& expected : primaryMixer.snapshots) {
        const std::string_view snapshot = expected.name;
        const auto& option = FindRow(model, kb::editor::InspectorPropertyId::SceneAudioSnapshotOption, snapshot);
        const kb::editor::InspectorSceneAudioTarget target{
            .kind = kb::editor::InspectorHitKind::Row,
            .section = option.section,
            .property = option.property,
            .index = option.flatIndex,
        };
        kb::editor::tests::Require(
            kb::editor::InspectorSceneAudioInteraction::HandlePointerDown(
                state, fixture.scene, model, target, service, 41U),
            "Snapshot option interaction was not handled");
        kb::editor::tests::Require(
            kb::scene::SceneAudioMixerAccess::ActiveSnapshot(fixture.scene) == snapshot,
            "Snapshot option did not select its exact mixer snapshot");
        model = kb::editor::InspectorSceneAudioModel{ fixture.scene };
    }
    const auto& useDefault = FindRow(model, kb::editor::InspectorPropertyId::SceneAudioSnapshotOption);
    kb::editor::tests::Require(
        kb::editor::InspectorSceneAudioInteraction::HandlePointerDown(
            state,
            fixture.scene,
            model,
            { kb::editor::InspectorHitKind::Row, useDefault.section, useDefault.property, useDefault.flatIndex },
            service,
            41U)
            && kb::scene::SceneAudioMixerAccess::ActiveSnapshot(fixture.scene).empty(),
        "Default snapshot action did not clear the active snapshot");
    kb::editor::tests::Require(!service.SetSceneAudioSnapshot("Unknown"),
        "Unknown snapshot must be rejected without history");

    const auto commitValid = [&](kb::editor::InspectorPropertyId property,
                                 std::string_view value,
                                 const char* baselineFailure,
                                 const char* resolutionFailure,
                                 const char* executionFailure,
                                 const char* historyFailure) {
        kb::editor::InspectorSceneAudioModel current{ fixture.scene };
        const auto& row = FindRow(current, property);
        const std::size_t historyBefore = fixture.stack.UndoCount();
        const kb::editor::InspectorSceneAudioTarget target{
            .kind = kb::editor::InspectorHitKind::TextField,
            .section = row.section,
            .property = row.property,
            .index = row.flatIndex,
        };
        kb::editor::tests::Require(
            kb::editor::InspectorSceneAudioInteraction::BeginTextEdit(state, current, target, 41U),
            "Scene Audio numeric edit did not begin");
        kb::editor::tests::Require(state.EditOriginalBuffer() == row.value
                && fixture.stack.UndoCount() == historyBefore,
            baselineFailure);
        ReplaceEditText(state, value);
        const kb::editor::InspectorSceneAudioModel commitModel{ fixture.scene };
        kb::editor::tests::Require(
            kb::editor::InspectorSceneAudioInteraction::ResolveCommit(
                state, fixture.scene, commitModel, 41U).has_value(),
            resolutionFailure);
        kb::editor::tests::Require(
            kb::editor::InspectorSceneAudioInteraction::CommitTextEdit(
                state, fixture.scene, commitModel, service, 41U),
            executionFailure);
        kb::editor::tests::Require(fixture.stack.UndoCount() == historyBefore + 1U,
            historyFailure);
    };
    commitValid(
        kb::editor::InspectorPropertyId::SceneAudioOcclusionVolumeScale,
        "0.625",
        "Occlusion volume scale edit changed its original value or history baseline",
        "Occlusion volume scale edit did not resolve to a command",
        "Occlusion volume scale command did not execute",
        "Occlusion volume scale command did not create exactly one history entry");
    commitValid(
        kb::editor::InspectorPropertyId::SceneAudioOcclusionMaxDistance,
        "250.5",
        "Occlusion max distance edit changed its original value or history baseline",
        "Occlusion max distance edit did not resolve to a command",
        "Occlusion max distance command did not execute",
        "Occlusion max distance command did not create exactly one history entry");
    commitValid(
        kb::editor::InspectorPropertyId::SceneAudioOcclusionLayerMask,
        "4294967295",
        "Occlusion layer mask edit changed its original value or history baseline",
        "Occlusion layer mask edit did not resolve to a command",
        "Occlusion layer mask command did not execute",
        "Occlusion layer mask command did not create exactly one history entry");
    commitValid(
        kb::editor::InspectorPropertyId::SceneAudioOcclusionMaxRaycasts,
        "4096",
        "Occlusion max raycasts edit changed its original value or history baseline",
        "Occlusion max raycasts edit did not resolve to a command",
        "Occlusion max raycasts command did not execute",
        "Occlusion max raycasts command did not create exactly one history entry");
    const kb::scene::AudioOcclusionSettings accepted =
        kb::scene::SceneAudioOcclusionAccess::Settings(fixture.scene);
    kb::editor::tests::Require(accepted.occludedVolumeScale == 0.625F
            && accepted.maxDistance == 250.5F
            && accepted.layerMask == std::numeric_limits<std::uint32_t>::max()
            && accepted.maxRaycastsPerTick == 4096U,
        "Scene Audio numeric values did not propagate exactly");

    const auto tryCommit = [&](kb::editor::InspectorPropertyId property, std::string_view value) {
        kb::editor::InspectorSceneAudioModel current{ fixture.scene };
        const auto& row = FindRow(current, property);
        const kb::editor::InspectorSceneAudioTarget target{
            .kind = kb::editor::InspectorHitKind::TextField,
            .section = row.section,
            .property = row.property,
            .index = row.flatIndex,
        };
        kb::editor::tests::Require(
            kb::editor::InspectorSceneAudioInteraction::BeginTextEdit(state, current, target, 41U),
            "Scene Audio numeric edit did not begin");
        ReplaceEditText(state, value);
        return kb::editor::InspectorSceneAudioInteraction::CommitTextEdit(
            state, fixture.scene, kb::editor::InspectorSceneAudioModel{ fixture.scene }, service, 41U);
    };

    const std::size_t invalidHistory = fixture.stack.UndoCount();
    const kb::scene::AudioOcclusionSettings invalidBaseline =
        kb::scene::SceneAudioOcclusionAccess::Settings(fixture.scene);
    const std::uint64_t invalidMixer = kb::scene::SceneAudioMixerAccess::ActiveMixer(fixture.scene);
    const std::string invalidSnapshot = kb::scene::SceneAudioMixerAccess::ActiveSnapshot(fixture.scene);
    const bool invalidDirty = fixture.documentDirty;
    for (const auto& [property, value] : std::vector<std::pair<kb::editor::InspectorPropertyId, std::string>>{
             { kb::editor::InspectorPropertyId::SceneAudioOcclusionVolumeScale, "nan" },
             { kb::editor::InspectorPropertyId::SceneAudioOcclusionVolumeScale, "inf" },
             { kb::editor::InspectorPropertyId::SceneAudioOcclusionVolumeScale, "1.1" },
             { kb::editor::InspectorPropertyId::SceneAudioOcclusionVolumeScale, "0.5 tail" },
             { kb::editor::InspectorPropertyId::SceneAudioOcclusionMaxDistance, "-1" },
             { kb::editor::InspectorPropertyId::SceneAudioOcclusionMaxDistance, "2 tail" },
             { kb::editor::InspectorPropertyId::SceneAudioOcclusionMaxDistance, "1e1000" },
             { kb::editor::InspectorPropertyId::SceneAudioOcclusionLayerMask, "4294967296" },
             { kb::editor::InspectorPropertyId::SceneAudioOcclusionLayerMask, "1 trailing" },
             { kb::editor::InspectorPropertyId::SceneAudioOcclusionLayerMask, "-1" },
             { kb::editor::InspectorPropertyId::SceneAudioOcclusionLayerMask, "" },
             { kb::editor::InspectorPropertyId::SceneAudioOcclusionMaxRaycasts, "4097" },
             { kb::editor::InspectorPropertyId::SceneAudioOcclusionMaxRaycasts, "-1" },
             { kb::editor::InspectorPropertyId::SceneAudioOcclusionMaxRaycasts, "" },
             { kb::editor::InspectorPropertyId::SceneAudioOcclusionMaxRaycasts, "word" },
         }) {
        kb::editor::tests::Require(!tryCommit(property, value),
            "Invalid Scene Audio numeric text must be rejected");
    }
    kb::editor::tests::Require(fixture.stack.UndoCount() == invalidHistory,
        "Invalid Scene Audio edits must not create history");
    const kb::scene::AudioOcclusionSettings& invalidAfter =
        kb::scene::SceneAudioOcclusionAccess::Settings(fixture.scene);
    kb::editor::tests::Require(invalidAfter.enabled == invalidBaseline.enabled
            && invalidAfter.occludedVolumeScale == invalidBaseline.occludedVolumeScale
            && invalidAfter.maxDistance == invalidBaseline.maxDistance
            && invalidAfter.layerMask == invalidBaseline.layerMask
            && invalidAfter.maxRaycastsPerTick == invalidBaseline.maxRaycastsPerTick
            && kb::scene::SceneAudioMixerAccess::ActiveMixer(fixture.scene) == invalidMixer
            && kb::scene::SceneAudioMixerAccess::ActiveSnapshot(fixture.scene) == invalidSnapshot
            && fixture.documentDirty == invalidDirty,
        "Invalid Scene Audio edits must leave the complete scene and dirty state unchanged");
    const kb::scene::AudioOcclusionSettings noOpBaseline =
        kb::scene::SceneAudioOcclusionAccess::Settings(fixture.scene);
    const std::size_t noOpHistory = fixture.stack.UndoCount();
    const bool noOpDirty = fixture.documentDirty;
    const std::uint64_t noOpMixer = kb::scene::SceneAudioMixerAccess::ActiveMixer(fixture.scene);
    const std::string noOpSnapshot = kb::scene::SceneAudioMixerAccess::ActiveSnapshot(fixture.scene);
    for (const auto& [property, value] : std::vector<std::pair<kb::editor::InspectorPropertyId, std::string>>{
             { kb::editor::InspectorPropertyId::SceneAudioOcclusionVolumeScale, "0.625" },
             { kb::editor::InspectorPropertyId::SceneAudioOcclusionMaxDistance, "250.5" },
             { kb::editor::InspectorPropertyId::SceneAudioOcclusionLayerMask, "4294967295" },
             { kb::editor::InspectorPropertyId::SceneAudioOcclusionMaxRaycasts, "4096" },
         }) {
        kb::editor::tests::Require(!tryCommit(property, value),
            "An exact Scene Audio text no-op must not commit");
    }
    const kb::scene::AudioOcclusionSettings& noOpAfter =
        kb::scene::SceneAudioOcclusionAccess::Settings(fixture.scene);
    kb::editor::tests::Require(fixture.stack.UndoCount() == noOpHistory
            && fixture.documentDirty == noOpDirty
            && kb::scene::SceneAudioMixerAccess::ActiveMixer(fixture.scene) == noOpMixer
            && kb::scene::SceneAudioMixerAccess::ActiveSnapshot(fixture.scene) == noOpSnapshot
            && noOpAfter.enabled == noOpBaseline.enabled
            && noOpAfter.occludedVolumeScale == noOpBaseline.occludedVolumeScale
            && noOpAfter.maxDistance == noOpBaseline.maxDistance
            && noOpAfter.layerMask == noOpBaseline.layerMask
            && noOpAfter.maxRaycastsPerTick == noOpBaseline.maxRaycastsPerTick,
        "Exact Scene Audio text no-ops must leave full scene, dirty, and history state unchanged");

    kb::editor::InspectorSceneAudioModel sameDocumentModel{ fixture.scene };
    const auto& sameDocumentRow = FindRow(
        sameDocumentModel, kb::editor::InspectorPropertyId::SceneAudioOcclusionMaxDistance);
    kb::editor::tests::Require(kb::editor::InspectorSceneAudioInteraction::BeginTextEdit(
            state,
            sameDocumentModel,
            { kb::editor::InspectorHitKind::TextField,
              sameDocumentRow.section,
              sameDocumentRow.property,
              sameDocumentRow.flatIndex },
            41U),
        "Same-document edit fixture did not begin");
    ReplaceEditText(state, "260");
    kb::scene::AudioOcclusionSettings unrelated =
        kb::scene::SceneAudioOcclusionAccess::Settings(fixture.scene);
    unrelated.layerMask = 9U;
    kb::editor::tests::Require(service.SetSceneAudioOcclusion(unrelated)
            && kb::editor::InspectorSceneAudioInteraction::CommitTextEdit(
                state,
                fixture.scene,
                kb::editor::InspectorSceneAudioModel{ fixture.scene },
                service,
                41U)
            && kb::scene::SceneAudioOcclusionAccess::Settings(fixture.scene).maxDistance == 260.0F,
        "A same-document mutation of another field must not invalidate a valid edit");

    kb::editor::InspectorSceneAudioModel staleModel{ fixture.scene };
    const auto& staleRow = FindRow(staleModel, kb::editor::InspectorPropertyId::SceneAudioOcclusionMaxDistance);
    kb::editor::tests::Require(kb::editor::InspectorSceneAudioInteraction::BeginTextEdit(
            state,
            staleModel,
            { kb::editor::InspectorHitKind::TextField, staleRow.section, staleRow.property, staleRow.flatIndex },
            41U),
        "Stale Scene Audio edit fixture did not begin");
    ReplaceEditText(state, "300");
    kb::scene::AudioOcclusionSettings externallyChanged = accepted;
    externallyChanged.maxDistance = 275.0F;
    kb::editor::tests::Require(service.SetSceneAudioOcclusion(externallyChanged),
        "Stale Scene Audio edit external mutation failed");
    const std::size_t staleHistory = fixture.stack.UndoCount();
    kb::editor::tests::Require(!kb::editor::InspectorSceneAudioInteraction::CommitTextEdit(
            state, fixture.scene, kb::editor::InspectorSceneAudioModel{ fixture.scene }, service, 41U)
            && kb::scene::SceneAudioOcclusionAccess::Settings(fixture.scene).maxDistance == 275.0F
            && fixture.stack.UndoCount() == staleHistory,
        "Optimistic concurrency must reject a stale same-field commit");

    kb::editor::InspectorSceneAudioModel ownerModel{ fixture.scene };
    const auto& ownerRow = FindRow(
        ownerModel, kb::editor::InspectorPropertyId::SceneAudioOcclusionMaxDistance);
    kb::editor::tests::Require(kb::editor::InspectorSceneAudioInteraction::BeginTextEdit(
            state,
            ownerModel,
            { kb::editor::InspectorHitKind::TextField, ownerRow.section, ownerRow.property, ownerRow.flatIndex },
            41U),
        "Document owner edit fixture did not begin");
    ReplaceEditText(state, "325");
    kb::editor::tests::Require(!kb::editor::InspectorSceneAudioInteraction::CommitTextEdit(
            state, fixture.scene, kb::editor::InspectorSceneAudioModel{ fixture.scene }, service, 42U),
        "A replaced document generation must reject the old edit buffer");

    const std::size_t undoBeforeTemporaryService = fixture.stack.UndoCount();
    {
        kb::editor::EditorSceneAudioSettingsService temporary = fixture.Service();
        kb::scene::AudioOcclusionSettings next =
            kb::scene::SceneAudioOcclusionAccess::Settings(fixture.scene);
        next.layerMask = 7U;
        kb::editor::tests::Require(temporary.SetSceneAudioOcclusion(next),
            "Temporary Scene Audio service command failed");
    }
    kb::editor::tests::Require(fixture.stack.UndoCount() == undoBeforeTemporaryService + 1U
            && fixture.Controller().Undo()
            && fixture.Controller().Redo()
            && kb::scene::SceneAudioOcclusionAccess::Settings(fixture.scene).layerMask == 7U,
        "Scene Audio history must remain valid after the temporary service is destroyed");

    kb::editor::tests::Require(kb::scene::SceneAudioMixerAccess::SetBusVolumeOverride(
            fixture.scene, "World", 0.4F),
        "Scene Audio runtime override fixture failed");
    kb::editor::tests::Require(kb::scene::SceneAudioMixerAccess::BeginSnapshotTransition(
            fixture.scene, "Calm", 3.0F),
        "Scene Audio runtime transition fixture failed");
    const auto overridesBeforeSame = kb::scene::SceneAudioMixerAccess::BusVolumeOverrides(fixture.scene).size();
    kb::scene::SceneAudioMixerAccess::SetActiveMixer(fixture.scene, mixerId.value);
    kb::editor::tests::Require(
        kb::scene::SceneAudioMixerAccess::BusVolumeOverrides(fixture.scene).size() == overridesBeforeSame
            && kb::scene::SceneAudioMixerAccess::SnapshotTransition(fixture.scene).IsActive(),
        "Same mixer id must preserve runtime mixer state");
    kb::scene::SceneAudioMixerAccess::SetActiveMixer(fixture.scene, 0U);
    kb::editor::tests::Require(
        kb::scene::SceneAudioMixerAccess::ActiveSnapshot(fixture.scene).empty()
            && kb::scene::SceneAudioMixerAccess::BusVolumeOverrides(fixture.scene).empty()
            && !kb::scene::SceneAudioMixerAccess::SnapshotTransition(fixture.scene).IsActive(),
        "Changed mixer id must clear snapshot and runtime mixer state");

    static_cast<void>(kb::scene::SceneAudioMixerAccess::SetActiveSnapshot(fixture.scene, "Stale"));
    kb::editor::tests::Require(kb::scene::SceneAudioMixerAccess::SetBusVolumeOverride(
            fixture.scene, "World", 0.2F),
        "New document runtime override fixture failed");
    static_cast<void>(kb::scene::SceneAudioMixerAccess::BeginSnapshotTransition(fixture.scene, "Other", 2.0F));
    kb::scene::AudioOcclusionSettings nonDefault{ true, 0.5F, 42.0F, 3U, 4U };
    static_cast<void>(kb::scene::SceneAudioOcclusionAccess::Configure(fixture.scene, nonDefault));
    kb::editor::EditorSceneAudioSettingsService::ResetForNewDocument(fixture.scene);
    kb::editor::tests::Require(kb::scene::SceneAudioMixerAccess::ActiveMixer(fixture.scene) == 0U
            && kb::scene::SceneAudioMixerAccess::ActiveSnapshot(fixture.scene).empty()
            && kb::scene::SceneAudioMixerAccess::BusVolumeOverrides(fixture.scene).empty()
            && !kb::scene::SceneAudioMixerAccess::SnapshotTransition(fixture.scene).IsActive()
            && !kb::scene::SceneAudioOcclusionAccess::Settings(fixture.scene).enabled,
        "New document reset must clear authored and transient Scene Audio state even at mixer id zero");

    kb::scene::SceneAudioMixerAccess::SetActiveMixer(fixture.scene, 999999U);
    static_cast<void>(kb::scene::SceneAudioMixerAccess::SetActiveSnapshot(fixture.scene, "Stale"));
    const std::size_t degradedHistory = fixture.stack.UndoCount();
    kb::editor::tests::Require(fixture.Service().SetSceneAudioSnapshot("")
            && fixture.stack.UndoCount() == degradedHistory + 1U
            && kb::scene::SceneAudioMixerAccess::ActiveSnapshot(fixture.scene).empty(),
        "A stale snapshot must remain clearable when the active mixer is missing");
    kb::editor::tests::Require(fixture.Controller().Undo()
            && kb::scene::SceneAudioMixerAccess::ActiveSnapshot(fixture.scene) == "Stale"
            && fixture.Controller().Redo()
            && kb::scene::SceneAudioMixerAccess::ActiveSnapshot(fixture.scene).empty(),
        "Missing-mixer snapshot repair must participate in scene history");
    const kb::editor::InspectorSceneAudioModel missingMixerModel{ fixture.scene };
    kb::editor::tests::Require(missingMixerModel.Text().find("invalid") != std::string::npos,
        "Missing mixer routing must remain visibly invalid in the headless Inspector");

    kb::editor::tests::Require(fixture.Service().SetSceneAudioMixer(mixerId),
        "Scene Audio round-trip mixer assignment failed");
    kb::editor::tests::Require(fixture.Service().SetSceneAudioSnapshot("Action"),
        "Scene Audio round-trip snapshot assignment failed");
    kb::scene::AudioOcclusionSettings roundTrip{ true, 0.25F, 64.5F, 0x55AA55AAU, 123U };
    kb::editor::tests::Require(fixture.Service().SetSceneAudioOcclusion(roundTrip),
        "Scene Audio round-trip occlusion assignment failed");
    const std::filesystem::path scenePath = TestRoot() / "Project" / "Scene.21kbscene";
    kb::editor::tests::Require(kb::scene::SceneDocumentService::Save(
            fixture.scene, scenePath, "SceneAudioInspector"),
        "Scene Audio Inspector v32 document save failed");
    kb::scene::Scene loaded;
    kb::editor::tests::Require(kb::scene::SceneDocumentService::LoadFileIntoScene(loaded, scenePath)
            && kb::scene::SceneAudioMixerAccess::ActiveMixer(loaded) == mixerId.value
            && kb::scene::SceneAudioMixerAccess::ActiveSnapshot(loaded) == "Action",
        "Scene Audio Inspector v32 mixer and snapshot did not round-trip");
    const kb::scene::AudioOcclusionSettings& loadedOcclusion =
        kb::scene::SceneAudioOcclusionAccess::Settings(loaded);
    kb::editor::tests::Require(loadedOcclusion.enabled
            && loadedOcclusion.occludedVolumeScale == roundTrip.occludedVolumeScale
            && loadedOcclusion.maxDistance == roundTrip.maxDistance
            && loadedOcclusion.layerMask == roundTrip.layerMask
            && loadedOcclusion.maxRaycastsPerTick == roundTrip.maxRaycastsPerTick,
        "Scene Audio Inspector v32 occlusion settings did not round-trip");

    const std::shared_ptr<const kb::audio::AudioMixerAsset> cached =
        fixture.scene.Assets().Manager().AcquireLoaded<kb::audio::AudioMixerAsset>(mixerId).Shared();
    std::error_code removeError;
    std::filesystem::remove(mixerPath, removeError);
    kb::editor::tests::Require(!removeError && cached != nullptr,
        "Scene Audio cache-only fixture did not remove the backing mixer file");
    const kb::editor::InspectorSceneAudioModel cacheOnly{ fixture.scene };
    kb::editor::tests::Require(cacheOnly.Text().find("Action") != std::string::npos
            && kb::editor::InspectorSceneAudioView::ContentHeight(state, cacheOnly) > 0,
        "Scene Audio view paths must use the confirmed loaded mixer cache without filesystem IO");
#if defined(_WIN32)
    PaintSmoke(content, state, cacheOnly);
    static_cast<void>(kb::editor::InspectorSceneAudioView::HitTest(content, state, cacheOnly, 200, 100));
#endif
}

} // namespace

namespace kb::editor::tests {

void RunEditorSceneAudioInspectorTests() {
    RunSceneAudioInspectorTest();
}

} // namespace kb::editor::tests
