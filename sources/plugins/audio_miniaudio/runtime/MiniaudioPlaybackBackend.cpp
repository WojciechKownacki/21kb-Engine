#include "runtime/MiniaudioPlaybackBackend.hpp"

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
    // LIB-147: bus groups sync FIRST (sources route into them below). A topology rebuild
    // invalidates every ma_sound_group, so entity sounds must recreate (their signatures
    // carry the bus generation) and one-shot voices must stop - both BEFORE any of them
    // would touch a destroyed group.
    if (busRegistry_.Sync(engine_.Native(), context.GetScene(), engine_.IsPlaybackAvailable())) {
        sourceRegistry_.StopAll();
        voicePool_.StopAll();
    }
    sourceRegistry_.Sync(engine_.Native(), context, clipResolver_, busRegistry_, engine_.IsPlaybackAvailable());
    voicePool_.RemoveFinishedVoices();
}

void MiniaudioPlaybackBackend::Shutdown() noexcept {
    sourceRegistry_.StopAll();
    voicePool_.StopAll();
    busRegistry_.StopAll();
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
