#include "scene/TimelineAudioPlayback.hpp"

#include "engine/audio/AudioPlayback.hpp"
#include "engine/audio/AudioRoutingContract.hpp"

#include <cmath>
#include <utility>

namespace kb::scene {

bool ValidateTimelineAudioTrack(const TimelineAudioTrack& track, float durationSeconds, std::string& error) {
    error.clear();
    if (track.binding.empty() || track.clipReference.empty() ||
        !std::isfinite(durationSeconds) || durationSeconds <= 0.0F ||
        !std::isfinite(track.startSeconds) || !std::isfinite(track.endSeconds) ||
        track.startSeconds < 0.0F || track.endSeconds <= track.startSeconds || track.endSeconds > durationSeconds ||
        !std::isfinite(track.clipOffsetSeconds) || track.clipOffsetSeconds < 0.0F ||
        !std::isfinite(track.volume) || track.volume < 0.0F ||
        (!track.outputBus.empty() && !kb::audio::IsAudioMixerNameTokenValid(track.outputBus))) {
        error = "Invalid timeline audio clip interval, binding or settings";
        return false;
    }
    return true;
}

void TimelineAudioPlayback::Stop(Scene& scene, TimelineAudioVoice& voice) noexcept {
    if (voice.id != 0) static_cast<void>(kb::audio::AudioPlayback::StopVoice(scene, voice.id));
    voice = {};
}

bool TimelineAudioPlayback::Synchronize(Scene& scene, const TimelineAudioTrack& track,
    std::uint64_t clipAssetId, SceneEntity target, float timeSeconds, float rate,
    bool playing, bool discontinuity, TimelineAudioVoice& voice, std::string& error) {
    using Audio = kb::audio::AudioPlayback;
    error.clear();
    const auto fail = [&](std::string message) {
        Stop(scene, voice);
        error = std::move(message);
        return false;
    };
    if (!std::isfinite(timeSeconds) || !std::isfinite(rate) || rate == 0.0F)
        return fail("Invalid timeline audio playback cursor or rate");
    if (timeSeconds < track.startSeconds || timeSeconds >= track.endSeconds || rate < 0.0F) {
        Stop(scene, voice);
        return true;
    }
    if (!Audio::HasBackend(scene)) {
        voice = {};
        return true;
    }
    if (!playing) {
        if (discontinuity) Stop(scene, voice);
        else if (voice.id != 0 && !voice.paused) {
            if (!Audio::PauseVoice(scene, voice.id)) Stop(scene, voice);
            else voice.paused = true;
        }
        return true;
    }
    const float offset = track.clipOffsetSeconds + timeSeconds - track.startSeconds;
    if (!std::isfinite(offset)) return fail("Timeline audio offset overflowed");
    // A clip can end before its authored interval. Keep its completed voice ID
    // until a jump or interval exit so it is not restarted every frame.
    if (voice.id != 0 && !voice.paused && !Audio::IsVoicePlaying(scene, voice.id) && !discontinuity)
        return true;
    bool started = false;
    if (voice.id == 0 || (!voice.paused && !Audio::IsVoicePlaying(scene, voice.id))) {
        Stop(scene, voice);
        kb::audio::AudioPlayDesc desc;
        desc.clipAssetId = clipAssetId;
        desc.outputBus = track.outputBus;
        desc.volume = track.volume;
        desc.pitch = rate;
        desc.spatial = track.spatial;
        desc.spatialBlend = track.spatial ? 1.0F : 0.0F;
        desc.ownerEntityId = target.Id();
        const auto result = Audio::PlayOneShot(scene, desc);
        if (!result.Succeeded()) return fail("Timeline audio could not start: " + result.error);
        voice.id = result.voiceId;
        started = true;
    }
    if ((started || discontinuity || voice.paused) && !Audio::SeekVoice(scene, voice.id, offset))
        return fail("Timeline audio could not seek to its clip offset");
    if (!Audio::SetVoicePitch(scene, voice.id, rate) || !Audio::SetVoiceVolume(scene, voice.id, track.volume))
        return fail("Timeline audio voice settings could not be applied");
    if (voice.paused && !Audio::ResumeVoice(scene, voice.id))
        return fail("Timeline audio voice could not resume");
    voice.paused = false;
    return true;
}

} // namespace kb::scene
