#include "scene/audio/EditorAudioMixerRuntime.hpp"
#include "app/EditorAudioAssetPreview.hpp"
#include "engine/assets/AssetKind.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/audio/AudioMixerAsset.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/audio/AudioMixerPreview.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"
#include "localization/EditorLocalization.hpp"

#include <chrono>
#include <filesystem>

namespace kb::editor {
namespace {
struct Preferences {
    std::uint64_t sceneId = 0U;
    kb::assets::AssetId clip;
    kb::assets::AssetId playingMixer;
    bool failed = false;
    bool synchronizationFailed = false;
    std::chrono::steady_clock::time_point refresh{};
};
Preferences& State(const kb::scene::Scene& scene) {
    static Preferences value;
    if (value.sceneId != scene.Id()) value = {.sceneId = scene.Id()};
    return value;
}
bool Active(const kb::scene::Scene& scene, kb::assets::AssetId mixer) {
    return mixer.IsValid() && kb::scene::SceneAudioMixerAccess::ActiveMixer(scene) == mixer.value;
}
bool Result(kb::scene::Scene& scene, bool succeeded) {
    State(scene).failed = !succeeded;
    return succeeded;
}
} // namespace

EditorAudioMixerRuntimeState EditorAudioMixerRuntime::Query(const kb::scene::Scene& scene, kb::assets::AssetId mixer) {
    auto& state = State(scene);
    EditorAudioMixerRuntimeState result;
    result.active = Active(scene, mixer);
    auto& mutableScene = const_cast<kb::scene::Scene&>(scene); // Public reports are owner-thread-only observations.
    result.meters = kb::audio::AudioMetering::Report(mutableScene);
    result.canCapture = result.active && result.meters.supported && result.meters.initialized;
    result.canPreview = result.canCapture && kb::audio::AudioMixerPreview::IsSupported(mutableScene);
    const auto* clip = scene.Assets().Manager().Registry().Find(state.clip);
    if (clip != nullptr) {
        result.previewClip = kb::assets::NormalizeAssetPath(clip->virtualPath);
        result.previewPlaying = state.playingMixer == mixer && EditorAudioAssetPreview::IsPlaying(scene, state.clip);
    }
    if (state.failed || state.synchronizationFailed) result.diagnostic = std::string{EditorText("inspector.audioMixer.runtimeFailed",
        "Audio preview or metering failed. Check the active mixer, clip and audio provider.")};
    if (!result.active) result.diagnostic = std::string{EditorText("inspector.audioMixer.assignForMeters",
        "Assign this mixer to the scene to use meters and preview.")};
    else if (!result.meters.supported || !result.meters.initialized) result.diagnostic = std::string{EditorText(
        "inspector.audioMixer.metersUnavailable", "Signal meters are unavailable in the active audio provider.")};
    else if (!result.canPreview) result.diagnostic = std::string{EditorText("inspector.audioMixer.livePreviewUnavailable",
        "Live preview is unavailable in the active audio provider.")};
    else {
        const auto dsp = kb::audio::AudioPlayback::DspReport(mutableScene);
        if (dsp.mixerAssetId == mixer.value) {
            for (const auto& bus : dsp.buses) {
                if (!bus.initialized || !bus.settingsCurrent) {
                    result.diagnostic = bus.bus + ": " + std::string{EditorText("inspector.audioMixer.effectsNotCurrent",
                        "Some effects could not be applied. Meters show the installed audio chain.")};
                    break;
                }
            }
        }
    }
    // Never present an installed graph from another asset, even for equal bus names.
    if (!result.active || result.meters.mixerAssetId != mixer.value) {
        result.meters.master = {}; result.meters.buses.clear(); result.meters.captureEnabled = false;
    }
    return result;
}

bool EditorAudioMixerRuntime::ConfigureCapture(kb::scene::Scene& scene, kb::assets::AssetId mixer, bool enabled) {
    return Result(scene, Active(scene, mixer) && kb::audio::AudioMetering::Configure(scene, enabled));
}

bool EditorAudioMixerRuntime::ResetCapture(kb::scene::Scene& scene, kb::assets::AssetId mixer) {
    const auto report = kb::audio::AudioMetering::Report(scene);
    return Result(scene, Active(scene, mixer) && report.mixerAssetId == mixer.value && kb::audio::AudioMetering::Reset(scene));
}

bool EditorAudioMixerRuntime::SetPreviewClip(kb::scene::Scene& scene, kb::assets::AssetId mixer, std::string_view reference) {
    const auto* metadata = scene.Assets().Manager().Registry().Find(mixer);
    if (metadata == nullptr || metadata->type != kb::audio::kAudioMixerAssetType) return Result(scene, false);
    kb::assets::AssetId id;
    if (!reference.empty()) {
        const auto& registry = scene.Assets().Manager().Registry();
        const kb::assets::AssetMetadata* clip = nullptr;
        if (kb::assets::TryParseAssetId(reference, id)) clip = registry.Find(id);
        else {
            try { clip = registry.FindByPath(std::filesystem::path{std::u8string{reference.begin(), reference.end()}}); }
            catch (const std::filesystem::filesystem_error&) { return Result(scene, false); }
        }
        if (clip == nullptr || !kb::assets::AssetMatchesKind(*clip, kb::assets::AssetKind::Audio)) return Result(scene, false);
        id = clip->id;
    }
    Stop(scene);
    State(scene).clip = id;
    return Result(scene, true);
}

bool EditorAudioMixerRuntime::Play(kb::scene::Scene& scene, kb::assets::AssetId mixer, std::string_view bus) {
    if (!Active(scene, mixer) || !State(scene).clip.IsValid()) return Result(scene, false);
    const auto asset = scene.Assets().Manager().Load<kb::audio::AudioMixerAsset>(mixer);
    if (!asset.IsLoaded() || (!bus.empty() && asset->FindBus(bus) == nullptr)) return Result(scene, false);
    if (!kb::audio::AudioMixerPreview::Synchronize(scene) || !ConfigureCapture(scene, mixer, true)) return Result(scene, false);
    const bool succeeded = EditorAudioAssetPreview::PlayOnBus(scene, State(scene).clip, bus);
    State(scene).playingMixer = succeeded ? mixer : kb::assets::AssetId{};
    return Result(scene, succeeded);
}

void EditorAudioMixerRuntime::Stop(kb::scene::Scene& scene) noexcept {
    EditorAudioAssetPreview::Stop(scene);
    State(scene).playingMixer = {};
    State(scene).failed = false;
    State(scene).synchronizationFailed = false;
}

bool EditorAudioMixerRuntime::ShouldRefresh(kb::scene::Scene& scene) {
    auto& state = State(scene);
    const auto now = std::chrono::steady_clock::now();
    if (now < state.refresh) return false;
    state.refresh = now + std::chrono::milliseconds{33};
    if (!kb::audio::AudioMetering::Report(scene).captureEnabled) return false;
    state.synchronizationFailed = !kb::audio::AudioMixerPreview::Synchronize(scene);
    return true;
}
} // namespace kb::editor
