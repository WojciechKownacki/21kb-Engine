#include "runtime/MiniaudioPlaybackBackend.hpp"

#include "engine/scene/SceneAudioMixerAccess.hpp"
#include "engine/scene/SceneSystemContext.hpp"

namespace kb::audio_miniaudio {

MiniaudioPlaybackBackend::~MiniaudioPlaybackBackend() {
    Shutdown();
}

void MiniaudioPlaybackBackend::OnCreate() {
    engine_.Initialize();
}

void MiniaudioPlaybackBackend::OnUpdate(kb::scene::SceneSystemContext& context) {
    if (!engine_.IsInitialized()) {
        return;
    }

    // A device can disappear between ticks. Use one availability snapshot for the whole
    // update so routing, source and voice decisions cannot disagree within this tick.
    const bool playbackAvailable = engine_.IsPlaybackAvailable();
    // LIB-150: advance the snapshot transition with the scene's own delta time BEFORE the
    // bus sync consumes it - deterministic, never wall clock.
    static_cast<void>(kb::scene::SceneAudioMixerAccess::AdvanceSnapshotTransition(context.GetScene(), context.DeltaSeconds()));
    if (!playbackAvailable) {
        // Native sounds must be detached before their groups and engine-owned endpoint.
        // Keep component transport records so an explicit device restart can rebuild
        // them deterministically, but one-shots have no owner that can safely replay them.
        sourceRegistry_.ReleaseNativeResources();
        voicePool_.StopAll();
        busRegistry_.StopAll();
        occlusionSampler_.Clear();
        listenerSynchronizer_.Disable(engine_.Native());
        kb::scene::SceneAudioOcclusionAccess::PublishRuntimeStats(
            context.GetScene(), kb::scene::AudioOcclusionRuntimeStats{});
        return;
    }

    const MiniaudioListenerSynchronizer::State listenerState = listenerSynchronizer_.Sync(engine_.Native(), context);
    // LIB-147: bus groups sync FIRST (sources route into them below). A topology rebuild
    // invalidates every ma_sound_group, so entity sounds must recreate (their signatures
    // carry the bus generation) and one-shot voices must stop - both BEFORE any of them
    // would touch a destroyed group.
    SyncRouting(context.GetScene(), playbackAvailable);
    // LIB-151: budget-capped audio occlusion against the engine's collider raycast
    // geometry - the listener position was just synced above, so reading it back from the
    // engine is exact. Disabled settings spend zero raycasts (nullptr sampler).
    const kb::scene::AudioOcclusionSettings& occlusionSettings = kb::scene::SceneAudioOcclusionAccess::Settings(context.GetScene());
    MiniaudioOcclusionSampler* occlusionSampler = nullptr;
    kb::scene::Vec3 listenerPosition{};
    if (occlusionSettings.enabled && listenerState.active) {
        listenerPosition = listenerState.position;
        occlusionSampler_.BeginTick(occlusionSettings);
        occlusionSampler = &occlusionSampler_;
    }
    sourceRegistry_.Sync(engine_.Native(), context, clipResolver_, busRegistry_, occlusionSampler, listenerPosition, playbackAvailable);
    // LIB-149: owner-attached voices follow their owner's transform and die with it -
    // BEFORE the finished sweep, so an owner-released voice never lingers a frame.
    voicePool_.SyncAttachedVoices(context.GetScene(), occlusionSampler, listenerPosition, context.DeltaSeconds());
    if (occlusionSampler != nullptr) {
        occlusionSampler_.EndTick();
    } else {
        occlusionSampler_.Clear();
    }
    kb::scene::SceneAudioOcclusionAccess::PublishRuntimeStats(
        context.GetScene(),
        occlusionSampler != nullptr
            ? occlusionSampler_.RuntimeStats()
            : kb::scene::AudioOcclusionRuntimeStats{});
    // LIB-152: fire crossed markers BEFORE the finished sweep, so markers at/near the end
    // of a clip still queue their event on the tick the voice completes.
    voicePool_.DispatchMarkers(context.GetScene());
    voicePool_.RemoveFinishedVoices();
}

void MiniaudioPlaybackBackend::Shutdown() noexcept {
    sourceRegistry_.StopAll();
    voicePool_.StopAll();
    busRegistry_.StopAll();
    occlusionSampler_.Clear();
    listenerSynchronizer_.Reset();
    engine_.Shutdown();
}

void MiniaudioPlaybackBackend::SyncRouting(kb::scene::Scene& scene, bool routingAvailable) {
    if (busRegistry_.RoutingWillChange(scene, routingAvailable)) {
        sourceRegistry_.ReleaseNativeResources();
        voicePool_.StopAll();
    }
    static_cast<void>(busRegistry_.Sync(engine_.Native(), scene, routingAvailable));
}

kb::audio::AudioPlayResult MiniaudioPlaybackBackend::PlayOneShot(kb::scene::Scene& scene, const kb::audio::AudioPlayDesc& desc) {
    if (!engine_.IsInitialized()) {
        return kb::audio::AudioPlayResult{ .started = false, .voiceId = 0U, .error = "audio backend is not initialized" };
    }
    if (!engine_.IsPlaybackAvailable()) {
        return kb::audio::AudioPlayResult{ .started = false, .voiceId = 0U, .error = "audio playback device is not available" };
    }
    if (desc.clipAssetId == 0U) {
        return kb::audio::AudioPlayResult{ .started = false, .voiceId = 0U, .error = "audio clip id is invalid" };
    }

    return PlayOneShotInternal(scene, desc, true);
}

kb::audio::AudioPlayResult MiniaudioPlaybackBackend::PlayOneShotInternal(
    kb::scene::Scene& scene,
    const kb::audio::AudioPlayDesc& desc,
    bool playbackAvailable) {
    SyncRouting(scene, playbackAvailable);
    const MiniaudioBusRegistry::Route route = busRegistry_.Resolve(desc.outputBus);
    if (!route.Succeeded()) {
        switch (route.status) {
        case MiniaudioBusRegistry::RouteStatus::MixerUnavailable:
            return { .started = false, .voiceId = 0U, .error = "audio mixer is not available" };
        case MiniaudioBusRegistry::RouteStatus::UnknownBus:
            return { .started = false, .voiceId = 0U, .error = "audio output bus is unknown" };
        case MiniaudioBusRegistry::RouteStatus::InitializationFailed:
            return { .started = false, .voiceId = 0U, .error = "audio output routing could not be initialized" };
        case MiniaudioBusRegistry::RouteStatus::Master:
        case MiniaudioBusRegistry::RouteStatus::Routed:
        default:
            break;
        }
    }
    return voicePool_.PlayOneShot(engine_.Native(), scene, desc, clipResolver_, route.group);
}

void MiniaudioPlaybackBackend::StopAll(kb::scene::Scene& scene) noexcept {
    static_cast<void>(scene);
    sourceRegistry_.StopAll();
    voicePool_.StopAll();
}

kb::audio::AudioSourceControlResult MiniaudioPlaybackBackend::PlaySource(kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    if (!engine_.IsInitialized()) {
        return { .status = kb::audio::AudioSourceControlStatus::DeviceUnavailable };
    }
    return PlaySourceInternal(scene, entity, engine_.IsPlaybackAvailable());
}

kb::audio::AudioSourceControlResult MiniaudioPlaybackBackend::PlaySourceInternal(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    bool playbackAvailable) {
    SyncRouting(scene, playbackAvailable);
    return sourceRegistry_.PlaySource(engine_.Native(), scene, entity, clipResolver_, busRegistry_, playbackAvailable);
}

kb::audio::AudioSourceControlResult MiniaudioPlaybackBackend::PauseSource(kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    return sourceRegistry_.PauseSource(scene, entity, engine_.IsPlaybackAvailable());
}

kb::audio::AudioSourceControlResult MiniaudioPlaybackBackend::ResumeSource(kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    return sourceRegistry_.ResumeSource(scene, entity, engine_.IsPlaybackAvailable());
}

kb::audio::AudioSourceControlResult MiniaudioPlaybackBackend::StopSource(kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    return sourceRegistry_.StopSource(scene, entity, engine_.IsPlaybackAvailable());
}

kb::audio::AudioSourceControlResult MiniaudioPlaybackBackend::IsSourcePlaying(kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    return sourceRegistry_.IsSourcePlaying(scene, entity, engine_.IsPlaybackAvailable());
}

kb::audio::AudioDeviceStatus MiniaudioPlaybackBackend::DeviceStatus() const noexcept {
    return engine_.Status();
}

kb::audio::AudioDeviceStatus MiniaudioPlaybackBackend::Reinitialize(kb::scene::Scene& scene) noexcept {
    return ReinitializeInternal(scene, false);
}

kb::audio::AudioDeviceStatus MiniaudioPlaybackBackend::ReinitializeInternal(kb::scene::Scene& scene, bool forceNoDevice) noexcept {
    static_cast<void>(scene);
    sourceRegistry_.ReleaseNativeResources();
    voicePool_.StopAll();
    busRegistry_.StopAll();
    occlusionSampler_.Clear();
    listenerSynchronizer_.Reset();
    engine_.Shutdown();
    engine_.Initialize(forceNoDevice);
    return engine_.Status();
}

#if defined(KB_AUDIO_MINIAUDIO_TESTING)
kb::audio::AudioDeviceStatus MiniaudioPlaybackBackend::ReinitializeForTesting(kb::scene::Scene& scene, bool forceNoDevice) noexcept {
    return ReinitializeInternal(scene, forceNoDevice);
}

kb::audio::AudioPlayResult MiniaudioPlaybackBackend::PlayOneShotForTesting(kb::scene::Scene& scene, const kb::audio::AudioPlayDesc& desc) {
    if (!engine_.IsInitialized() || desc.clipAssetId == 0U) {
        return { .started = false, .voiceId = 0U, .error = "audio backend test resource is invalid" };
    }
    return PlayOneShotInternal(scene, desc, true);
}

kb::audio::AudioSourceControlResult MiniaudioPlaybackBackend::PlaySourceForTesting(kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    return engine_.IsInitialized()
        ? PlaySourceInternal(scene, entity, true)
        : kb::audio::AudioSourceControlResult{ .status = kb::audio::AudioSourceControlStatus::DeviceUnavailable };
}

MiniaudioPlaybackBackend::ResourceStateForTesting MiniaudioPlaybackBackend::ResourcesForTesting() const noexcept {
    return ResourceStateForTesting{
        .sourceSounds = sourceRegistry_.NativeSoundCountForTesting(),
        .voices = voicePool_.VoiceCountForTesting(),
        .buses = busRegistry_.BusCount(),
    };
}

bool MiniaudioPlaybackBackend::StopPlaybackDeviceForTesting() noexcept {
    if (!engine_.IsPlaybackAvailable()) {
        return false;
    }
    ma_device* device = ma_engine_get_device(&engine_.Native());
    return device != nullptr && ma_device_stop(device) == MA_SUCCESS;
}
#endif

bool MiniaudioPlaybackBackend::StopVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept {
    static_cast<void>(scene);
    return voicePool_.StopVoice(voiceId);
}

bool MiniaudioPlaybackBackend::PauseVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept {
    static_cast<void>(scene);
    return voicePool_.PauseVoice(voiceId);
}

bool MiniaudioPlaybackBackend::ResumeVoice(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept {
    static_cast<void>(scene);
    return voicePool_.ResumeVoice(voiceId);
}

bool MiniaudioPlaybackBackend::SeekVoice(kb::scene::Scene& scene, std::uint64_t voiceId, float positionSeconds) noexcept {
    static_cast<void>(scene);
    return voicePool_.SeekVoice(voiceId, positionSeconds);
}

bool MiniaudioPlaybackBackend::SetVoiceVolume(kb::scene::Scene& scene, std::uint64_t voiceId, float volume) noexcept {
    static_cast<void>(scene);
    return voicePool_.SetVoiceVolume(voiceId, volume);
}

bool MiniaudioPlaybackBackend::SetVoiceMute(kb::scene::Scene& scene, std::uint64_t voiceId, bool mute) noexcept {
    static_cast<void>(scene);
    return voicePool_.SetVoiceMute(voiceId, mute);
}

bool MiniaudioPlaybackBackend::SetVoicePan(kb::scene::Scene& scene, std::uint64_t voiceId, float pan) noexcept {
    static_cast<void>(scene);
    return voicePool_.SetVoicePan(voiceId, pan);
}

bool MiniaudioPlaybackBackend::SetVoicePitch(kb::scene::Scene& scene, std::uint64_t voiceId, float pitch) noexcept {
    static_cast<void>(scene);
    return voicePool_.SetVoicePitch(voiceId, pitch);
}

bool MiniaudioPlaybackBackend::SetVoiceLoop(kb::scene::Scene& scene, std::uint64_t voiceId, bool loop) noexcept {
    static_cast<void>(scene);
    return voicePool_.SetVoiceLoop(voiceId, loop);
}

bool MiniaudioPlaybackBackend::IsVoicePlaying(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept {
    static_cast<void>(scene);
    return voicePool_.IsVoicePlaying(voiceId);
}

float MiniaudioPlaybackBackend::VoicePlaybackSeconds(kb::scene::Scene& scene, std::uint64_t voiceId) noexcept {
    static_cast<void>(scene);
    return voicePool_.VoicePlaybackSeconds(voiceId);
}

bool MiniaudioPlaybackBackend::AddVoiceMarker(kb::scene::Scene& scene, std::uint64_t voiceId, std::string_view marker, float positionSeconds, kb::scene::SceneEntity target) {
    static_cast<void>(scene);
    return voicePool_.AddVoiceMarker(voiceId, marker, positionSeconds, target);
}

} // namespace kb::audio_miniaudio
