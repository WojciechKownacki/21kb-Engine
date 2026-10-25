#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "console/EditorConsoleState.hpp"
#include "commands/EditorCommandStack.hpp"
#include "engine/assets/AssetImportService.hpp"
#include "engine/audio/AudioMixerAssetIO.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/audio/AudioMixerPreview.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "inspection/InspectorAudioMixerRuntimeInteraction.hpp"
#include "localization/EditorLocalization.hpp"
#include "rendering/EditorPanelStyle.hpp"
#include "rendering/InspectorAudioMixerAssetView.hpp"
#include "scene/audio/EditorAudioMixerAuthoring.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <chrono>
#include <memory>
#include <span>
#include <vector>
#include <thread>

namespace kb::editor::tests {
void RunEditorAudioMixerRuntimeTests() {
#if defined(KB_EDITOR_TEST_AUDIO_PLUGIN) && defined(_WIN32)
    using Property = InspectorPropertyId;
    using Kind = InspectorAudioMixerRowKind;
    using View = InspectorAudioMixerAssetView;
    using Runtime = EditorAudioMixerRuntime;
    using Interaction = InspectorAudioMixerRuntimeInteraction;
    using Playback = kb::audio::AudioPlayback;
    const auto root = std::filesystem::temp_directory_path() / "kb_editor_audio_runtime_tests";
    std::filesystem::create_directories(root / "Project");
    const auto tone = root / "Tone.wav";
    {
        std::ofstream file{tone, std::ios::binary | std::ios::trunc};
        const auto number = [&](std::uint32_t value, unsigned bytes) {
            for (unsigned i = 0U; i < bytes; ++i) file.put(static_cast<char>((value >> (i * 8U)) & 255U));
        };
        file.write("RIFF", 4); number(96036U, 4); file.write("WAVEfmt ", 8); number(16U, 4); number(1U, 2); number(1U, 2);
        number(48000U, 4); number(96000U, 4); number(2U, 2); number(16U, 2); file.write("data", 4); number(96000U, 4);
        for (unsigned i = 0U; i < 48000U; ++i) number(static_cast<std::uint16_t>(static_cast<std::int16_t>(
            3276.0 * std::sin(6.283185307179586 * 440.0 * i / 48000.0))), 2);
        Require(file.good(), "Editor runtime PCM fixture did not write");
    }
    kb::project::ProjectDescriptor descriptor;
    descriptor.disableEnginePluginsByDefault = true;
    descriptor.plugins.push_back({.name = "Audio.Miniaudio", .binaryPath = KB_EDITOR_TEST_AUDIO_PLUGIN});
    std::size_t gameplayUpdates = 0U;
    struct GameplayProbe final : kb::scene::SceneSystem {
        explicit GameplayProbe(std::size_t& count) : updates(count) {}
        void OnUpdate(kb::scene::SceneSystemContext&) override { ++updates; }
        std::size_t& updates;
    };
    kb::scene::Scene scene{descriptor};
    Require(scene.IsModuleActive("Audio.Miniaudio") && Playback::ConfigureOffline(scene), "Editor test did not load an actual offline audio provider DLL");
    Require(kb::audio::AudioMixerPreview::IsSupported(scene)
        && scene.Runtime().AddSceneSystem(std::make_unique<GameplayProbe>(gameplayUpdates)).IsValid(), "Actual provider lacks paused mixer synchronization");
    Require(scene.Assets().MountProject(root / "Project"), "Editor runtime project did not mount");
    const auto imported = kb::assets::AssetImportService::ImportFiles(scene.Assets().Manager(), std::array{tone}, "/Game/Audio");
    Require(imported.Succeeded() && imported.items.size() == 1U, "Editor preview clip did not import through the asset pipeline");
    EditorAssetBrowserState browser; EditorConsoleState console; EditorCommandStack history; InspectorPanelState inspector;
    EditorAudioMixerAuthoring authoring{scene, browser, console, &history};
    Require(authoring.Create("/Game/Mixers"), "Editor runtime mixer did not create");
    const auto id = browser.InspectorAsset();
    Require(authoring.AddBus(id, "Output") && authoring.AddBus(id, "Effects") && authoring.SetBusParent(id, "Effects", "Output")
        && authoring.SetBusVolume(id, "Output", 0.25F)
        && authoring.AddEffect(id, "Effects", "Boost", kb::audio::AudioDspKind::Compressor)
        && authoring.SetEffectField(id, "Effects", "Boost", "ratio", "1")
        && authoring.SetEffectField(id, "Effects", "Boost", "makeupDb", "24"), "Editor preview mixer did not author");
    const auto metadata = *scene.Assets().Manager().Registry().Find(id);
    auto mixer = View::LoadCached(scene.Assets().Manager(), id);
    Require(mixer.IsLoaded(), "Editor runtime mixer cache did not load");
    const auto bytes = [&] { std::ifstream input{metadata.physicalPath, std::ios::binary}; return std::vector<char>{std::istreambuf_iterator<char>{input}, {}}; };
    const auto original = bytes(); const auto originalHistory = history.UndoCount();
    std::size_t actions = 0U, pixelsVerified = 0U;
    const auto row = [&](Property property, std::string_view bus = {}, std::string_view field = {}) {
        const auto runtime = Runtime::Query(scene, id); const InspectorAudioMixerAssetModel model{*mixer, &runtime};
        const auto found = std::find_if(model.Rows().begin(), model.Rows().end(), [&](const auto& item) {
            return item.property == property && item.busName == bus && (field.empty() || item.effectField == field);
        });
        Require(found != model.Rows().end(), "Editor runtime row is missing"); return *found;
    };
    const auto hit = [&](const InspectorAudioMixerRow& item) {
        const auto runtime = Runtime::Query(scene, id); const RECT content{0, 0, 600, 320};
        const auto bounds = View::RowBounds(content, inspector, *mixer, item.flatIndex, &runtime);
        Require(bounds.has_value(), "Editor runtime row has no actual geometry");
        const int maximum = std::max(0, View::ContentHeight(inspector, *mixer, &runtime) - 320);
        static_cast<void>(inspector.SetScrollOffset(std::clamp(static_cast<int>(bounds->top) - 48, 0, maximum), maximum));
        const RECT scrolled{0, -inspector.ScrollOffset(), 600, 320 - inspector.ScrollOffset()};
        const auto visible = View::RowBounds(scrolled, inspector, *mixer, item.flatIndex, &runtime);
        Require(visible.has_value() && visible->top >= 0 && visible->bottom <= 320, "Editor runtime row did not scroll into view");
        const int x = item.kind == Kind::Bool ? panel_style::InspectorLabelColumnRight(*visible) + 8
            : item.kind == Kind::Text ? 480 : 300;
        const auto target = View::HitTest(scrolled, inspector, *mixer, x, (visible->top + visible->bottom) / 2, &runtime);
        Require(target.index == item.flatIndex && target.property == item.property && target.section == item.section,
            "Editor runtime hit-test does not match painted geometry");
        return InspectorAudioMixerAssetTarget{.kind = target.kind, .section = target.section, .property = target.property, .index = target.index};
    };
    const auto click = [&](Property property, std::string_view bus = {}, std::string_view field = {}) {
        const auto item = row(property, bus, field); const auto target = hit(item); const auto runtime = Runtime::Query(scene, id);
        const bool handled = item.section == InspectorSectionId::AudioMixerRuntime
            ? Interaction::HandlePointerDown(scene, inspector, *mixer, target, id, runtime)
            : InspectorAudioMixerAssetInteraction::HandlePointerDown(inspector, *mixer, target, authoring, id);
        Require(handled, "Editor runtime pointer action was not routed"); ++actions;
    };
    const auto clip = [&](std::string_view reference, bool expected) {
        click(Property::AudioMixerPreviewClip); inspector.ClearText(); inspector.InsertText(reference);
        const auto runtime = Runtime::Query(scene, id);
        Require(Interaction::CommitTextEdit(scene, inspector, *mixer, id, runtime) == expected, "Editor preview clip commit result changed"); ++actions;
    };
    const auto render = [&](std::uint32_t frames) {
        auto pcm = Playback::RenderOffline(scene, frames);
        Require(pcm.succeeded && pcm.frames == frames && pcm.channels == 2U && pcm.sampleRate == 48000U,
            "Editor preview did not render actual complete PCM"); return pcm;
    };
    const auto paintMeter = [&](std::string_view bus, std::uint32_t expected) {
        const auto item = row(Property::AudioMixerMeterPeak, bus); static_cast<void>(hit(item));
        const auto runtime = Runtime::Query(scene, id); const RECT content{0, -inspector.ScrollOffset(), 600, 320 - inspector.ScrollOffset()};
        const auto bounds = View::RowBounds(content, inspector, *mixer, item.flatIndex, &runtime);
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = 600;
        info.bmiHeader.biHeight = -320; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr; HBITMAP image = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0); HDC dc = CreateCompatibleDC(nullptr);
        Require(image != nullptr && pixels != nullptr && dc != nullptr, "Editor meter readback surface did not initialize");
        const auto old = SelectObject(dc, image); View::Paint(dc, content, MakeEditorDarkTheme(), inspector, metadata, *mixer, &runtime); GdiFlush();
        std::size_t matches = 0U; const auto* data = static_cast<const std::uint32_t*>(pixels);
        for (int y = bounds->top; y < bounds->bottom; ++y) for (int x = 0; x < 600; ++x)
            if ((data[static_cast<std::size_t>(y) * 600U + static_cast<std::size_t>(x)] & 0x00FFFFFFU) == expected) ++matches;
        SelectObject(dc, old); DeleteDC(dc); DeleteObject(image);
        Require(expected == 0U || matches > 4U, "Actual meter signal did not produce its expected bar/clipping pixels");
        if (expected != 0U) ++pixelsVerified;
    };
    Require(!Runtime::Query(scene, id).active && !Runtime::Query(scene, id).meters.master.available, "Inactive mixer presented another runtime graph");
    clip(kb::assets::NormalizeAssetPath(scene.Assets().Manager().Registry().Find(imported.items.front().id)->virtualPath), true);
    const auto selectedClip = Runtime::Query(scene, id).previewClip;
    clip("/Game/Audio/Missing.21kb", false); Require(Runtime::Query(scene, id).previewClip == selectedClip, "Invalid clip replaced a valid preview selection");
    click(Property::AudioMixerPreviewClip); inspector.ClearText(); inspector.InsertText("/Game/Audio/Missing.21kb");
    auto stale = inspector;
    clip({}, true); const auto changed = Runtime::Query(scene, id);
    Require(!Interaction::CommitTextEdit(scene, stale, *mixer, id, changed), "Stale clip edit overwrote a later preview selection"); ++actions;
    clip(selectedClip, true);
    kb::scene::SceneAudioMixerAccess::SetActiveMixer(scene, id.value);
    click(Property::AudioMixerCapture); Require(Runtime::Query(scene, id).meters.captureEnabled, "Capture checkbox did not enable the actual provider");
    click(Property::AudioMixerPreviewBus, "Effects"); Require(Runtime::Query(scene, id).previewPlaying, "Bus preview action did not start the imported clip");
    const auto playingPcm = render(8192U); auto runtime = Runtime::Query(scene, id);
    const auto effects = std::find_if(runtime.meters.buses.begin(), runtime.meters.buses.end(), [](const auto& bus) { return bus.bus == "Effects"; });
    Require(effects != runtime.meters.buses.end() && effects->channel[0].heldPeak > 1.5F
        && runtime.meters.master.channel[0].heldPeak > 0.37F
        && std::any_of(playingPcm.samples.begin(), playingPcm.samples.end(), [](float value) { return std::abs(value) > 0.37F; }),
        "Editor preview did not execute the independently expected DSP gain");
    Require(row(Property::AudioMixerMeterPeak, "Effects").meterClipped && !row(Property::AudioMixerMeterPeak).meterClipped
        && row(Property::AudioMixerMeterPeak, "Effects").meterLevel == effects->channel[0].peak
        && row(Property::AudioMixerMeterPeak).meterLevel == runtime.meters.master.channel[0].peak,
        "Editor meters did not distinguish clipped child and attenuated master");
    paintMeter("Effects", 0x00C64040U); paintMeter({}, 0x00309866U);
    click(Property::AudioMixerEffectParameter, "Effects", "makeupDb"); inspector.ClearText(); inspector.InsertText("12");
    Require(InspectorAudioMixerAssetInteraction::CommitTextEdit(inspector, *mixer, authoring, id), "Live preview effect edit did not commit through its field"); ++actions;
    mixer = View::LoadCached(scene.Assets().Manager(), id);
    const auto pausedFrames = kb::audio::AudioMetering::Report(scene).master.processedFrames;
    Require(Playback::DspReport(scene).buses[1].appliedEffects[0].parameters.makeupDb == 24.0,
        "An observation silently installed a pending effect edit");
    Require(Runtime::ShouldRefresh(scene) && Playback::DspReport(scene).buses[1].appliedEffects[0].parameters.makeupDb == 12.0
        && kb::audio::AudioMetering::Report(scene).master.processedFrames == pausedFrames && gameplayUpdates == 0U
        && Runtime::Query(scene, id).previewPlaying,
        "Paused editor refresh did not apply DSP independently of PCM, or advanced gameplay/clock/voice state");
    static_cast<void>(scene.Runtime().Update(0.0F)); Require(gameplayUpdates == 1U, "Gameplay update canary was not attached to the real scene runtime");
    static_cast<void>(render(512U)); const auto changedPcm = render(4096U);
    const auto gainPeak = std::max_element(changedPcm.samples.begin(), changedPcm.samples.end(), [](float a, float b) { return std::abs(a) < std::abs(b); });
    Require(gainPeak != changedPcm.samples.end() && std::abs(std::abs(*gainPeak) - 0.09959F) < 0.002F,
        "Live editor DSP edit did not change actual preview PCM");
    Require(history.Undo(kb::editor::EditorCommandHistoryKey::AudioMixerAsset(id.value)), "Live effect edit was not undoable");
    mixer = View::LoadCached(scene.Assets().Manager(), id);
    bool refreshed = false; const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{250};
    while (!refreshed && std::chrono::steady_clock::now() < deadline) {
        refreshed = Runtime::ShouldRefresh(scene); if (!refreshed) std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    Require(refreshed && Playback::DspReport(scene).buses[1].appliedEffects[0].parameters.makeupDb == 24.0
        && gameplayUpdates == 1U, "Paused editor refresh did not apply Undo without gameplay");
    static_cast<void>(render(512U));
    click(Property::AudioMixerResetMeters); Require(Runtime::Query(scene, id).meters.master.processedFrames == 0U && Runtime::Query(scene, id).previewPlaying,
        "Reset action did not clear capture or restarted transport");
    click(Property::AudioMixerBusMute, "Output"); mixer = View::LoadCached(scene.Assets().Manager(), id);
    static_cast<void>(render(512U)); click(Property::AudioMixerResetMeters); static_cast<void>(render(2048U));
    runtime = Runtime::Query(scene, id);
    const auto mutedChild = std::find_if(runtime.meters.buses.begin(), runtime.meters.buses.end(), [](const auto& bus) { return bus.bus == "Effects"; });
    Require(row(Property::AudioMixerMeterPeak).meterLevel == 0.0F && mutedChild != runtime.meters.buses.end() && mutedChild->channel[0].heldPeak > 1.5F,
        "Painted parent mute did not preserve the child's actual meter");
    Require(history.Undo(kb::editor::EditorCommandHistoryKey::AudioMixerAsset(id.value)), "Meter UI mute was not undoable");
    mixer = View::LoadCached(scene.Assets().Manager(), id); static_cast<void>(render(512U));
    Require(bytes() == original && history.UndoCount() == originalHistory, "Undo did not restore the exact authored mixer");
    click(Property::AudioMixerCapture); const auto pcm = render(1024U);
    Require(!Runtime::Query(scene, id).meters.captureEnabled && std::any_of(pcm.samples.begin(), pcm.samples.end(), [](float value) { return std::abs(value) > 0.3F; }),
        "Disabling capture muted actual editor preview playback");
    click(Property::AudioMixerStopPreview); static_cast<void>(render(512U)); const auto stopped = render(1024U);
    Require(!Runtime::Query(scene, id).previewPlaying && std::all_of(stopped.samples.begin(), stopped.samples.end(), [](float value) { return value == 0.0F; }),
        "Stop preview left a native voice rendering");
    Require(bytes() == original && history.UndoCount() == originalHistory, "Transient preview/capture altered authored data or Undo history");
    Require(authoring.Create("/Game/Mixers") && authoring.AddBus(browser.InspectorAsset(), "Effects"), "Second same-name-bus fixture did not author");
    const auto other = browser.InspectorAsset(); const auto wrong = Runtime::Query(scene, other);
    Require(!wrong.active && !wrong.meters.master.available && wrong.meters.buses.empty()
        && !Runtime::ConfigureCapture(scene, other, true) && kb::scene::SceneAudioMixerAccess::ActiveMixer(scene) == id.value,
        "Selecting another mixer exposed or replaced the active graph");
    std::size_t languages = 0U; std::string error;
    for (const auto& language : EditorLocalization::Languages()) {
        if (language.code == "en") continue;
        Require(EditorLocalization::SetLanguage(language.code, KB_EDITOR_LOCALIZATION_DIR, error), "Runtime meter catalog did not load");
        Require(row(Property::AudioMixerCapture).label != "Capture signal"
            && Runtime::Query(scene, other).diagnostic != "Assign this mixer to the scene to use meters and preview.",
            "Runtime meter controls fell back to English");
        paintMeter({}, 0U); ++languages;
    }
    Require(EditorLocalization::SetLanguage("en", {}, error) && languages == 32U, "Runtime meter language coverage is incomplete");
    std::cout << "Editor runtime audio uiActions=" << actions << " actualDLL=1 PCM=verified meterPixels=" << pixelsVerified
        << " languages=" << languages << " pausedDSPBeforePCM=1 gameplayNotAdvanced=1 clippedChild=1 mutedMaster=0 stopSilent=1 transientBytes=exact undo=exact staleClipRejected=1 otherMixerHidden=1\n";
#else
    Require(false, "Editor audio runtime test requires a native provider DLL and GDI readback");
#endif
}
} // namespace kb::editor::tests
