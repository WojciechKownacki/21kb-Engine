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

    listenerSynchronizer_.Sync(engine_.Native(), context);
    // LIB-150: advance the snapshot transition with the scene's own delta time BEFORE the
    // bus sync consumes it - deterministic, never wall clock.
    static_cast<void>(kb::scene::SceneAudioMixerAccess::AdvanceSnapshotTransition(context.GetScene(), context.DeltaSeconds()));
    // LIB-147: bus groups sync FIRST (sources route into them below). A topology rebuild
    // invalidates every ma_sound_group, so entity sounds must recreate (their signatures
    // carry the bus generation) and one-shot voices must stop - both BEFORE any of them
    // would touch a destroyed group.
    if (busRegistry_.Sync(engine_.Native(), context.GetScene(), engine_.IsPlaybackAvailable())) {
        sourceRegistry_.StopAll();
        voicePool_.StopAll();
    }
    // LIB-151: budget-capped audio occlusion against the engine's collider raycast
    // geometry - the listener position was just synced above, so reading it back from the
    // engine is exact. Disabled settings spend zero raycasts (nullptr sampler).
    const kb::scene::AudioOcclusionSettings& occlusionSettings = kb::scene::SceneAudioOcclusionAccess::Settings(context.GetScene());
    MiniaudioOcclusionSampler* occlusionSampler = nullptr;
    kb::scene::Vec3 listenerPosition{};
    if (occlusionSettings.enabled && engine_.IsPlaybackAvailable()) {
        const ma_vec3f listener = ma_engine_listener_get_position(&engine_.Native(), 0U);
        listenerPosition = kb::scene::Vec3{ listener.x, listener.y, listener.z };
        occlusionSampler_.BeginTick(occlusionSettings);
        occlusionSampler = &occlusionSampler_;
    }
    sourceRegistry_.Sync(engine_.Native(), context, clipResolver_, busRegistry_, occlusionSampler, listenerPosition, engine_.IsPlaybackAvailable());
    // LIB-149: owner-attached voices follow their owner's transform and die with it -
    // BEFORE the finished sweep, so an owner-released voice never lingers a frame.
    voicePool_.SyncAttachedVoices(context.GetScene(), occlusionSampler, listenerPosition);
    voicePool_.RemoveFinishedVoices();
}

void MiniaudioPlaybackBackend::Shutdown() noexcept {
    sourceRegistry_.StopAll();
    voicePool_.StopAll();
    busRegistry_.StopAll();
    occlusionSampler_.Clear();
    engine_.Shutdown();
}

kb::audio::AudioPlayResult MiniaudioPlaybackBackend::PlayOneShot(kb::scene::Scene& scene, const kb::audio::AudioPlayDesc& desc) {
    if (!engine_.IsInitialized()) {
        return kb::audio::AudioPlayResult{ .started = false, .voiceId = 0U, .error = "miniaudio engine is not initialized" };
    }
    if (!engine_.IsPlaybackAvailable()) {
        return kb::audio::AudioPlayResult{ .started = false, .voiceId = 0U, .error = "miniaudio playback device is not available" };
    }
    if (desc.clipAssetId == 0U) {
        return kb::audio::AudioPlayResult{ .started = false, .voiceId = 0U, .error = "audio clip id is invalid" };
    }

    return voicePool_.PlayOneShot(engine_.Native(), scene, desc, clipResolver_, busRegistry_.FindGroup(desc.outputBus));
}

void MiniaudioPlaybackBackend::StopAll(kb::scene::Scene& scene) noexcept {
    static_cast<void>(scene);
    sourceRegistry_.StopAll();
    voicePool_.StopAll();
}

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

} // namespace kb::audio_miniaudio
