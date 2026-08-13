#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "assets/EditorAssetBrowserState.hpp"
#include "console/EditorConsoleState.hpp"
#include "engine/audio/AudioMixerAssetIO.hpp"
#include "engine/library/EngineLibraryParsing.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "inspection/InspectorAudioMixerAssetInteraction.hpp"
#include "inspection/InspectorAudioMixerAssetModel.hpp"
#include "rendering/InspectorAudioMixerAssetView.hpp"
#include "scene/audio/EditorAudioMixerAuthoring.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path TestRoot() {
    return std::filesystem::temp_directory_path() / "kb_editor_audio_mixer_inspector_tests";
}

void ResetTestRoot() {
    std::error_code error;
    std::filesystem::remove_all(TestRoot(), error);
    std::filesystem::create_directories(TestRoot() / "Project" / "Assets" / "Mixers", error);
    kb::editor::tests::Require(!error, "Audio mixer Inspector fixture directory creation failed");
}

[[nodiscard]] std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    return std::vector<std::uint8_t>{
        std::istreambuf_iterator<char>{ input },
        std::istreambuf_iterator<char>{},
    };
}

[[nodiscard]] kb::assets::AssetHandle<kb::audio::AudioMixerAsset> CachedMixer(
    kb::scene::Scene& scene,
    kb::assets::AssetId id) {
    return kb::editor::InspectorAudioMixerAssetView::LoadCached(scene.Assets().Manager(), id);
}

[[nodiscard]] const kb::editor::InspectorAudioMixerRow& FindRow(
    const kb::editor::InspectorAudioMixerAssetModel& model,
    kb::editor::InspectorPropertyId property,
    std::string_view bus = {},
    std::string_view snapshot = {},
    std::string_view overrideBus = {}) {
    const auto found = std::ranges::find_if(model.Rows(), [&](const kb::editor::InspectorAudioMixerRow& row) {
        return row.property == property
            && (bus.empty() || row.busName == bus)
            && (snapshot.empty() || row.snapshotName == snapshot)
            && (overrideBus.empty() || row.overrideBusName == overrideBus);
    });
    kb::editor::tests::Require(found != model.Rows().end(), "Expected audio mixer Inspector row is missing");
    return *found;
}

#if defined(_WIN32)
[[nodiscard]] kb::editor::InspectorAudioMixerAssetTarget HitRow(
    const RECT& content,
    const kb::editor::InspectorPanelState& state,
    const kb::audio::AudioMixerAsset& asset,
    const kb::editor::InspectorAudioMixerRow& row) {
    const std::optional<RECT> bounds = kb::editor::InspectorAudioMixerAssetView::RowBounds(
        content, state, asset, row.flatIndex);
    kb::editor::tests::Require(bounds.has_value(), "Audio mixer Inspector row must have shared model geometry");
    int x = (bounds->left + bounds->right) / 2;
    if (row.kind == kb::editor::InspectorAudioMixerRowKind::Text) {
        x = bounds->left + ((bounds->right - bounds->left) * 70 / 100);
    } else if (row.kind == kb::editor::InspectorAudioMixerRowKind::Bool) {
        x = bounds->left + ((bounds->right - bounds->left) * 36 / 100) + 8;
    }
    const int y = (bounds->top + bounds->bottom) / 2;
    const kb::editor::InspectorAudioMixerAssetHit hit =
        kb::editor::InspectorAudioMixerAssetView::HitTest(content, state, asset, x, y);
    kb::editor::tests::Require(hit.index == row.flatIndex
            && hit.section == row.section
            && hit.property == row.property,
        "Audio mixer Inspector hit testing must resolve the exact shared flat row");
    const kb::editor::InspectorHitKind expectedKind = row.kind == kb::editor::InspectorAudioMixerRowKind::Text
        ? kb::editor::InspectorHitKind::TextField
        : row.kind == kb::editor::InspectorAudioMixerRowKind::Bool
            ? kb::editor::InspectorHitKind::BoolField
            : kb::editor::InspectorHitKind::Row;
    kb::editor::tests::Require(hit.kind == expectedKind,
        "Audio mixer Inspector row must expose the control kind declared by the shared model");
    return kb::editor::InspectorAudioMixerAssetTarget{
        .kind = hit.kind,
        .section = hit.section,
        .property = hit.property,
        .index = hit.index,
    };
}

void PaintSmoke(
    const RECT& content,
    const kb::editor::InspectorPanelState& state,
    const kb::assets::AssetMetadata& metadata,
    const kb::audio::AudioMixerAsset& asset) {
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
        "Audio mixer Inspector test backbuffer creation failed");
    HGDIOBJ previous = SelectObject(dc, bitmap);
    kb::editor::InspectorAudioMixerAssetView::Paint(
        dc, content, kb::editor::MakeEditorDarkTheme(), state, metadata, asset);
    GdiFlush();
    const std::uint32_t* words = static_cast<const std::uint32_t*>(pixels);
    const std::size_t count = static_cast<std::size_t>(content.right - content.left)
        * static_cast<std::size_t>(content.bottom - content.top);
    kb::editor::tests::Require(std::ranges::any_of(std::span<const std::uint32_t>{ words, count }, [](std::uint32_t value) {
            return value != 0U;
        }),
        "Audio mixer Inspector paint must write the real test backbuffer");
    SelectObject(dc, previous);
    DeleteDC(dc);
    DeleteObject(bitmap);
}
#endif

void SetEditedText(kb::editor::InspectorPanelState& state, std::string_view value) {
    state.ClearText();
    state.InsertText(value);
}

void RunAudioMixerInspectorTest() {
    ResetTestRoot();
    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::InspectorPanelState inspector;
    kb::editor::tests::Require(scene.Assets().MountProject(TestRoot() / "Project"),
        "Audio mixer Inspector project mount failed");
    static_cast<void>(browser.SelectFolder("/Game/Mixers", scene.Assets().Manager()));
    kb::editor::EditorAudioMixerAuthoring authoring{ scene, browser, console };
    kb::editor::tests::Require(authoring.Create("/Game/Mixers"),
        "Audio mixer Inspector fixture creation failed");
    const kb::assets::AssetId id = browser.InspectorAsset();
    const kb::assets::AssetMetadata* discovered = scene.Assets().Manager().Registry().Find(id);
    kb::editor::tests::Require(discovered != nullptr, "Created mixer metadata must be discoverable");
    const kb::assets::AssetMetadata metadata = *discovered;
    kb::editor::tests::Require(kb::editor::InspectorAudioMixerAssetView::Supports(metadata),
        "Selected audio mixer metadata must route to the dedicated Inspector");
    const std::filesystem::path path = metadata.physicalPath;
    const RECT content{ 0, 0, 420, 320 };

    kb::assets::AssetHandle<kb::audio::AudioMixerAsset> mixer = CachedMixer(scene, id);
    kb::editor::tests::Require(mixer.IsLoaded(), "Selected mixer must be available from the runtime cache");
    kb::editor::InspectorAudioMixerAssetModel model{ *mixer };
    kb::editor::tests::Require(model.Rows(kb::editor::InspectorSectionId::AudioMixerBuses).size() == 2U
            && model.Rows(kb::editor::InspectorSectionId::AudioMixerSnapshots).size() == 2U,
        "Empty mixer Inspector must expose honest empty states and live add actions");
#if defined(_WIN32)
    PaintSmoke(content, inspector, metadata, *mixer);
    const auto click = [&](const kb::editor::InspectorAudioMixerRow& row) {
        return kb::editor::InspectorAudioMixerAssetInteraction::HandlePointerDown(
            inspector, *mixer, HitRow(content, inspector, *mixer, row), authoring, id);
    };
#else
    const auto click = [&](const kb::editor::InspectorAudioMixerRow& row) {
        return kb::editor::InspectorAudioMixerAssetInteraction::HandlePointerDown(
            inspector,
            *mixer,
            kb::editor::InspectorAudioMixerAssetTarget{
                .kind = row.kind == kb::editor::InspectorAudioMixerRowKind::Text
                    ? kb::editor::InspectorHitKind::TextField
                    : row.kind == kb::editor::InspectorAudioMixerRowKind::Bool
                        ? kb::editor::InspectorHitKind::BoolField
                        : kb::editor::InspectorHitKind::Row,
                .section = row.section,
                .property = row.property,
                .index = row.flatIndex,
            },
            authoring,
            id);
    };
#endif
    const auto refresh = [&]() {
        mixer = CachedMixer(scene, id);
        kb::editor::tests::Require(mixer.IsLoaded(), "Mixer UI mutation must refresh the runtime cache");
        return kb::editor::InspectorAudioMixerAssetModel{ *mixer };
    };
    const auto edit = [&](const kb::editor::InspectorAudioMixerRow& row, std::string_view value) {
        kb::editor::tests::Require(click(row) && inspector.IsTextEditing()
                && inspector.EditIndex() == row.flatIndex
                && inspector.EditRowIdentity().has_value()
                && inspector.EditRowIdentity()->ownerAssetId == id.value,
            "Mixer text control must atomically capture flat index and structural identity");
        SetEditedText(inspector, value);
        const bool changed = kb::editor::InspectorAudioMixerAssetInteraction::CommitTextEdit(
            inspector, *mixer, authoring, id);
        kb::editor::tests::Require(!inspector.IsTextEditing() && !inspector.EditRowIdentity().has_value(),
            "Every mixer text commit must clear its structural edit identity");
        return changed;
    };

    kb::editor::tests::Require(click(FindRow(model, kb::editor::InspectorPropertyId::AudioMixerBusAdd)),
        "Add Bus Inspector action was not handled");
    model = refresh();
    kb::editor::tests::Require(mixer->FindBus("Bus") != nullptr, "First bus UI action must choose Bus");
    kb::editor::tests::Require(click(FindRow(model, kb::editor::InspectorPropertyId::AudioMixerBusAdd)),
        "Second Add Bus Inspector action was not handled");
    model = refresh();
    kb::editor::tests::Require(mixer->FindBus("Bus1") != nullptr, "Second bus UI action must choose Bus1");
    kb::editor::tests::Require(click(FindRow(model, kb::editor::InspectorPropertyId::AudioMixerBusAdd)),
        "Third Add Bus Inspector action was not handled");
    model = refresh();

    kb::editor::tests::Require(edit(FindRow(model, kb::editor::InspectorPropertyId::AudioMixerBusName, "Bus"), "Master"),
        "Bus Name Inspector edit failed");
    model = refresh();
    kb::editor::tests::Require(edit(FindRow(model, kb::editor::InspectorPropertyId::AudioMixerBusParent, "Bus1"), "Master"),
        "A legal bus named Master must remain a real parent, not the implicit output sentinel");
    model = refresh();
    kb::editor::tests::Require(mixer->FindBus("Bus1")->parentBus == "Master",
        "Master text must resolve to the authored bus with that name");
    kb::editor::tests::Require(edit(FindRow(model, kb::editor::InspectorPropertyId::AudioMixerBusParent, "Bus1"), "-"),
        "The dash sentinel must clear an authored bus parent");
    model = refresh();
    kb::editor::tests::Require(mixer->FindBus("Bus1")->parentBus.empty(),
        "Only the dash sentinel or empty input may select implicit master output");
    kb::editor::tests::Require(edit(FindRow(model, kb::editor::InspectorPropertyId::AudioMixerBusParent, "Bus1"), "Master"),
        "Bus parent restoration failed");
    model = refresh();
    constexpr float exactVolume = 0.123456791F;
    kb::editor::tests::Require(edit(FindRow(model, kb::editor::InspectorPropertyId::AudioMixerBusVolume, "Master"), "0.123456791"),
        "Bus Volume Inspector edit failed");
    model = refresh();
    const kb::editor::InspectorAudioMixerRow& volumeRow = FindRow(
        model, kb::editor::InspectorPropertyId::AudioMixerBusVolume, "Master");
    double displayedVolume = 0.0;
    kb::editor::tests::Require(mixer->FindBus("Master")->volume == exactVolume
            && kb::library::TryParseDouble(volumeRow.value, displayedVolume)
            && static_cast<float>(displayedVolume) == exactVolume,
        "Mixer volume text must round-trip the exact authored float");
    kb::editor::tests::Require(click(FindRow(model, kb::editor::InspectorPropertyId::AudioMixerBusMute, "Master")),
        "Bus Mute Inspector control was not handled");
    model = refresh();
    kb::editor::tests::Require(mixer->FindBus("Master")->mute,
        "Bus Mute Inspector control must persist through authoring");

    kb::editor::tests::Require(click(FindRow(model, kb::editor::InspectorPropertyId::AudioMixerSnapshotAdd)),
        "Add Snapshot Inspector action was not handled");
    model = refresh();
    kb::editor::tests::Require(click(FindRow(model, kb::editor::InspectorPropertyId::AudioMixerSnapshotAdd)),
        "Second Add Snapshot Inspector action was not handled");
    model = refresh();
    kb::editor::tests::Require(mixer->FindSnapshot("Snapshot") != nullptr
            && mixer->FindSnapshot("Snapshot1") != nullptr,
        "Snapshot actions must use deterministic unique names");
    kb::editor::tests::Require(edit(FindRow(model, kb::editor::InspectorPropertyId::AudioMixerSnapshotName, {}, "Snapshot"), "Gameplay"),
        "Snapshot Name Inspector edit failed");
    model = refresh();

    for (const std::string bus : { std::string{ "Master" }, std::string{ "Bus1" }, std::string{ "Bus2" } }) {
        const kb::editor::InspectorAudioMixerRow& addOverride = FindRow(
            model, kb::editor::InspectorPropertyId::AudioMixerOverrideAdd, bus, "Gameplay", bus);
        kb::editor::tests::Require(click(addOverride), "Every available bus must have a live Add Override action");
        model = refresh();
        kb::editor::tests::Require(std::ranges::none_of(model.Rows(), [&](const kb::editor::InspectorAudioMixerRow& row) {
                return row.property == kb::editor::InspectorPropertyId::AudioMixerOverrideAdd
                    && row.snapshotName == "Gameplay" && row.overrideBusName == bus;
            }),
            "An overridden bus must no longer expose a duplicate Add action");
    }
    const kb::editor::InspectorAudioMixerRow& readOnlyBus = FindRow(
        model, kb::editor::InspectorPropertyId::AudioMixerOverrideBus, {}, "Gameplay", "Master");
    const std::vector<std::uint8_t> beforeReadOnly = ReadBytes(path);
    kb::editor::tests::Require(click(readOnlyBus) && !inspector.IsTextEditing() && ReadBytes(path) == beforeReadOnly,
        "Override Bus must remain a real read-only Inspector row");
    kb::editor::tests::Require(edit(FindRow(model, kb::editor::InspectorPropertyId::AudioMixerOverrideVolume, {}, "Gameplay", "Master"), "0.625"),
        "Override Volume Inspector edit failed");
    model = refresh();
    kb::editor::tests::Require(mixer->FindSnapshot("Gameplay")->busVolumes.front().volume == 0.625F,
        "Override Volume Inspector edit must persist");
    kb::editor::tests::Require(click(FindRow(model, kb::editor::InspectorPropertyId::AudioMixerOverrideRemove, {}, "Gameplay", "Bus2")),
        "Remove Override Inspector action was not handled");
    model = refresh();
    kb::editor::tests::Require(mixer->FindSnapshot("Gameplay")->busVolumes.size() == 2U,
        "Remove Override Inspector action must mutate the selected override only");

    const std::string headless = model.Text();
    kb::editor::tests::Require(headless.find("Buses") != std::string::npos
            && headless.find("Master") != std::string::npos
            && headless.find("Snapshots") != std::string::npos
            && headless.find("Gameplay") != std::string::npos
            && headless.find("0.625") != std::string::npos,
        "Headless mixer Inspector representation must expose dynamic buses, snapshots, and overrides");
    const kb::assets::AssetMetadata genericMetadata{
        .id = kb::assets::AssetId{ 44U },
        .type = "GenericAsset",
    };
    kb::editor::tests::Require(!kb::editor::InspectorAudioMixerAssetView::Supports(genericMetadata),
        "Generic non-mixer assets must not route to the dedicated mixer Inspector");

    const int expandedHeight = kb::editor::InspectorAudioMixerAssetView::ContentHeight(inspector, *mixer);
    inspector.ToggleCollapsed(kb::editor::InspectorSectionId::AudioMixerBuses);
    const int collapsedHeight = kb::editor::InspectorAudioMixerAssetView::ContentHeight(inspector, *mixer);
    kb::editor::tests::Require(collapsedHeight < expandedHeight,
        "Mixer section collapse must reduce ContentHeight using the shared model");
#if defined(_WIN32)
    const kb::editor::InspectorAudioMixerAssetHit collapsedHeader =
        kb::editor::InspectorAudioMixerAssetView::HitTest(content, inspector, *mixer, 20, 82);
    kb::editor::tests::Require(collapsedHeader.kind == kb::editor::InspectorHitKind::SectionHeader
            && collapsedHeader.section == kb::editor::InspectorSectionId::AudioMixerBuses,
        "Collapsed mixer section header must remain hittable");
#endif
    inspector.ToggleCollapsed(kb::editor::InspectorSectionId::AudioMixerBuses);
    const kb::editor::InspectorAudioMixerRow& hoverRow = FindRow(
        model, kb::editor::InspectorPropertyId::AudioMixerBusMute, "Master");
    kb::editor::tests::Require(inspector.SetHover(
            kb::editor::InspectorHitKind::BoolField,
            hoverRow.section,
            hoverRow.property,
            hoverRow.flatIndex)
            && inspector.IsHovered(
                kb::editor::InspectorHitKind::BoolField,
                hoverRow.section,
                hoverRow.property,
                hoverRow.flatIndex),
        "Mixer hover must preserve the exact dynamic flat-row index");

    for (int index = 0; index < 8; ++index) {
        kb::editor::tests::Require(authoring.AddBus(id, "Long" + std::to_string(index)),
            "Long mixer fixture bus insertion failed");
    }
    model = refresh();
    const int longHeight = kb::editor::InspectorAudioMixerAssetView::ContentHeight(inspector, *mixer);
    kb::editor::tests::Require(longHeight > content.bottom - content.top,
        "Long dynamic mixer Inspector content must exceed its viewport for scrolling");
#if defined(_WIN32)
    const kb::editor::InspectorAudioMixerRow& lastBus = FindRow(
        model, kb::editor::InspectorPropertyId::AudioMixerBusVolume, "Long7");
    RECT scrolled = content;
    scrolled.top -= longHeight - (content.bottom - content.top);
    scrolled.bottom -= longHeight - (content.bottom - content.top);
    const std::optional<RECT> scrolledBounds = kb::editor::InspectorAudioMixerAssetView::RowBounds(
        scrolled, inspector, *mixer, lastBus.flatIndex);
    kb::editor::tests::Require(scrolledBounds.has_value()
            && kb::editor::InspectorAudioMixerAssetView::HitTest(
                scrolled,
                inspector,
                *mixer,
                scrolledBounds->left + ((scrolledBounds->right - scrolledBounds->left) * 70 / 100),
                (scrolledBounds->top + scrolledBounds->bottom) / 2).index == lastBus.flatIndex,
        "Scrolled mixer view must preserve flat-row geometry and exact hit mapping");
#endif

    const auto stable = [&]() {
        return std::tuple{
            ReadBytes(path),
            scene.Assets().Manager().LoadGeneration(id),
            scene.Assets().Manager().AcquireLoaded<kb::audio::AudioMixerAsset>(id).Shared(),
        };
    };
    const auto rejectEdit = [&](kb::editor::InspectorPropertyId property, std::string_view bus, std::string_view snapshot, std::string_view overrideBus, std::string_view value) {
        model = refresh();
        const kb::editor::InspectorAudioMixerRow& row = FindRow(model, property, bus, snapshot, overrideBus);
        const auto before = stable();
        kb::editor::tests::Require(!edit(row, value), "Invalid or no-op mixer Inspector edit must be rejected");
        const auto after = stable();
        kb::editor::tests::Require(std::get<0>(before) == std::get<0>(after)
                && std::get<1>(before) == std::get<1>(after)
                && std::get<2>(before).get() == std::get<2>(after).get(),
            "Rejected mixer Inspector edit must preserve file bits, generation, and shared cache pointer");
    };
    rejectEdit(kb::editor::InspectorPropertyId::AudioMixerBusName, "Master", {}, {}, "bad name");
    rejectEdit(kb::editor::InspectorPropertyId::AudioMixerBusParent, "Bus2", {}, {}, "Unknown");
    rejectEdit(kb::editor::InspectorPropertyId::AudioMixerBusParent, "Master", {}, {}, "Bus1");
    rejectEdit(kb::editor::InspectorPropertyId::AudioMixerBusVolume, "Master", {}, {}, "NaN");
    rejectEdit(kb::editor::InspectorPropertyId::AudioMixerBusVolume, "Master", {}, {}, "Inf");
    rejectEdit(kb::editor::InspectorPropertyId::AudioMixerBusVolume, "Master", {}, {}, "-1");
    rejectEdit(kb::editor::InspectorPropertyId::AudioMixerBusVolume, "Master", {}, {}, "number");
    rejectEdit(kb::editor::InspectorPropertyId::AudioMixerSnapshotName, {}, "Gameplay", {}, "bad name");
    rejectEdit(kb::editor::InspectorPropertyId::AudioMixerOverrideVolume, {}, "Gameplay", "Master", "-0.5");
    rejectEdit(kb::editor::InspectorPropertyId::AudioMixerBusVolume, "Master", {}, {},
        FindRow(model, kb::editor::InspectorPropertyId::AudioMixerBusVolume, "Master").value);

    model = refresh();
    const kb::editor::InspectorAudioMixerRow stale = FindRow(
        model, kb::editor::InspectorPropertyId::AudioMixerBusVolume, "Bus2");
    kb::editor::tests::Require(click(stale) && inspector.IsTextEditing(),
        "Stale edit fixture must begin a real indexed mixer text edit");
    SetEditedText(inspector, "0.25");
    kb::editor::tests::Require(authoring.RenameBus(id, "Bus2", "Renamed"),
        "Stale edit fixture external graph mutation failed");
    mixer = CachedMixer(scene, id);
    const auto afterExternalMutation = stable();
    kb::editor::tests::Require(!kb::editor::InspectorAudioMixerAssetInteraction::CommitTextEdit(
            inspector, *mixer, authoring, id),
        "Flat index with a mismatched structural row key must reject a stale edit");
    const auto afterStaleCommit = stable();
    kb::editor::tests::Require(std::get<0>(afterExternalMutation) == std::get<0>(afterStaleCommit)
            && std::get<1>(afterExternalMutation) == std::get<1>(afterStaleCommit)
            && std::get<2>(afterExternalMutation).get() == std::get<2>(afterStaleCommit).get(),
        "Stale mixer commit must not touch persisted or cached state");

    model = refresh();
    const kb::editor::InspectorAudioMixerRow sameRowChanged = FindRow(
        model, kb::editor::InspectorPropertyId::AudioMixerBusVolume, "Master");
    kb::editor::tests::Require(click(sameRowChanged) && inspector.IsTextEditing(),
        "Optimistic concurrency fixture must begin a real mixer value edit");
    SetEditedText(inspector, "0.5");
    kb::editor::tests::Require(authoring.SetBusVolume(id, "Master", 0.75F),
        "Optimistic concurrency fixture external value mutation failed");
    mixer = CachedMixer(scene, id);
    const auto newerValueState = stable();
    kb::editor::tests::Require(!kb::editor::InspectorAudioMixerAssetInteraction::CommitTextEdit(
            inspector, *mixer, authoring, id),
        "A mixer edit whose original value changed externally must be rejected");
    const auto afterOlderValueCommit = stable();
    kb::editor::tests::Require(mixer->FindBus("Master")->volume == 0.75F
            && std::get<0>(newerValueState) == std::get<0>(afterOlderValueCommit)
            && std::get<1>(newerValueState) == std::get<1>(afterOlderValueCommit)
            && std::get<2>(newerValueState).get() == std::get<2>(afterOlderValueCommit).get(),
        "Rejected older mixer value edit must preserve the newer file and cache state");

    model = refresh();
    const kb::editor::InspectorAudioMixerRow sameParentChanged = FindRow(
        model, kb::editor::InspectorPropertyId::AudioMixerBusParent, "Bus1");
    kb::editor::tests::Require(click(sameParentChanged) && inspector.IsTextEditing(),
        "Parent concurrency fixture must begin a real mixer text edit");
    SetEditedText(inspector, "Renamed");
    kb::editor::tests::Require(authoring.SetBusParent(id, "Bus1", {}),
        "Parent concurrency fixture external mutation failed");
    mixer = CachedMixer(scene, id);
    const auto newerParentState = stable();
    kb::editor::tests::Require(!kb::editor::InspectorAudioMixerAssetInteraction::CommitTextEdit(
            inspector, *mixer, authoring, id),
        "A mixer parent edit whose original value changed externally must be rejected");
    const auto afterOlderParentCommit = stable();
    kb::editor::tests::Require(mixer->FindBus("Bus1")->parentBus.empty()
            && std::get<0>(newerParentState) == std::get<0>(afterOlderParentCommit)
            && std::get<1>(newerParentState) == std::get<1>(afterOlderParentCommit)
            && std::get<2>(newerParentState).get() == std::get<2>(afterOlderParentCommit).get(),
        "Rejected older parent edit must preserve the newer routing state");

    model = refresh();
    const kb::editor::InspectorAudioMixerRow crossAssetRow = FindRow(
        model, kb::editor::InspectorPropertyId::AudioMixerBusVolume, "Master");
    kb::editor::tests::Require(click(crossAssetRow) && inspector.IsTextEditing(),
        "Cross-asset fixture must begin a real mixer edit on the first asset");
    kb::editor::tests::Require(kb::editor::InspectorAudioMixerAssetView::IsRowEditing(
            inspector, id, crossAssetRow),
        "The mixer view must render the active edit buffer for the owning asset row");
    SetEditedText(inspector, "0.33");
    kb::editor::tests::Require(authoring.Create("/Game/Mixers"),
        "Cross-asset fixture second mixer creation failed");
    const kb::assets::AssetId secondId = browser.InspectorAsset();
    kb::editor::tests::Require(authoring.AddBus(secondId, "Master")
            && authoring.SetBusVolume(secondId, "Master", 0.75F),
        "Cross-asset fixture must mirror the edited row structure and original value");
    const kb::assets::AssetMetadata* secondMetadata = scene.Assets().Manager().Registry().Find(secondId);
    kb::editor::tests::Require(secondMetadata != nullptr, "Second mixer metadata must be discoverable");
    const std::filesystem::path secondPath = secondMetadata->physicalPath;
    kb::assets::AssetHandle<kb::audio::AudioMixerAsset> secondMixer = CachedMixer(scene, secondId);
    const auto secondBytes = ReadBytes(secondPath);
    const std::uint64_t secondGeneration = scene.Assets().Manager().LoadGeneration(secondId);
    const auto secondPointer = secondMixer.Shared();
    const kb::editor::InspectorAudioMixerAssetModel secondModel{ *secondMixer };
    const kb::editor::InspectorAudioMixerRow& secondVolumeRow = FindRow(
        secondModel, kb::editor::InspectorPropertyId::AudioMixerBusVolume, "Master");
    kb::editor::tests::Require(!kb::editor::InspectorAudioMixerAssetView::IsRowEditing(
            inspector, secondId, secondVolumeRow),
        "An identical row on another mixer must never render the first mixer's edit buffer");
    kb::editor::tests::Require(!kb::editor::InspectorAudioMixerAssetInteraction::CommitTextEdit(
            inspector, *secondMixer, authoring, secondId),
        "An edit begun on one mixer must not commit to another mixer with an identical row key and value");
    kb::editor::tests::Require(ReadBytes(secondPath) == secondBytes
            && scene.Assets().Manager().LoadGeneration(secondId) == secondGeneration
            && scene.Assets().Manager().AcquireLoaded<kb::audio::AudioMixerAsset>(secondId).Shared().get()
                == secondPointer.get()
            && secondMixer->FindBus("Master")->volume == 0.75F,
        "Rejected cross-asset commit must preserve the second mixer file, generation, and cache pointer");
    kb::editor::tests::Require(browser.SelectAsset(id, scene.Assets().Manager()),
        "Cross-asset fixture must restore the first mixer selection");
    mixer = CachedMixer(scene, id);

    model = refresh();
    kb::editor::tests::Require(click(FindRow(model, kb::editor::InspectorPropertyId::AudioMixerSnapshotRemove, {}, "Snapshot1")),
        "Remove Snapshot Inspector action was not handled");
    model = refresh();
    kb::editor::tests::Require(mixer->FindSnapshot("Snapshot1") == nullptr,
        "Remove Snapshot Inspector action must refresh the dynamic model");
    kb::editor::tests::Require(click(FindRow(model, kb::editor::InspectorPropertyId::AudioMixerBusRemove, "Renamed")),
        "Remove Bus Inspector action was not handled");
    model = refresh();
    kb::editor::tests::Require(mixer->FindBus("Renamed") == nullptr
            && mixer->FindSnapshot("Gameplay")->busVolumes.size() == 2U,
        "Remove Bus Inspector action must expose the authoring cascade in the refreshed model");

    const std::vector<std::uint8_t> persisted = ReadBytes(path);
    const std::uint64_t generation = scene.Assets().Manager().LoadGeneration(id);
    static_cast<void>(scene.Assets().Manager().Unload(id));
    static_cast<void>(scene.Assets().Discover());
    mixer = CachedMixer(scene, id);
    kb::editor::tests::Require(mixer.IsLoaded()
            && mixer->FindBus("Master") != nullptr
            && mixer->FindSnapshot("Gameplay") != nullptr
            && scene.Assets().Manager().LoadGeneration(id) > generation,
        "Unload, discovery, and reload must reconstruct all mixer Inspector edits");

    const std::shared_ptr<const kb::audio::AudioMixerAsset> confirmedCache = mixer.Shared();
    std::error_code deleteError;
    std::filesystem::remove(path, deleteError);
    kb::editor::tests::Require(!deleteError && !std::filesystem::exists(path),
        "Cache-only view fixture must remove the backing file");
    const kb::assets::AssetHandle<kb::audio::AudioMixerAsset> cacheOnly =
        kb::editor::InspectorAudioMixerAssetView::LoadCached(scene.Assets().Manager(), id);
    kb::editor::tests::Require(cacheOnly.IsLoaded() && cacheOnly.Shared().get() == confirmedCache.get()
            && kb::editor::InspectorAudioMixerAssetView::ContentHeight(inspector, *cacheOnly) > 0,
        "Per-frame mixer Inspector reads must use the confirmed loaded cache without filesystem IO");
#if defined(_WIN32)
    PaintSmoke(content, inspector, metadata, *cacheOnly);
#endif
    kb::editor::tests::Require(kb::audio::AudioMixerAssetIO::Save(path, *cacheOnly)
            && ReadBytes(path) == persisted,
        "Cache-only view fixture must restore the exact persisted mixer file");
}

} // namespace

namespace kb::editor::tests {

void RunEditorAudioMixerInspectorTests() {
    RunAudioMixerInspectorTest();
}

} // namespace kb::editor::tests
