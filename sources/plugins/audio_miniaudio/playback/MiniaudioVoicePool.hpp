#pragma once

#include "engine/audio/AudioPlayback.hpp"
#include "runtime/MiniaudioSound.hpp"

#include <miniaudio.h>

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace kb::audio_miniaudio {

class MiniaudioClipResolver;
class MiniaudioOcclusionSampler;

class MiniaudioVoicePool final {
public:
    // LIB-147: `group` attaches the voice to its AudioPlayDesc::outputBus mixer bus
    // (nullptr = implicit master). One-shots do NOT survive a mixer topology rebuild -
    // the backend stops the pool then (a dangling ma_sound_group would be worse).
    [[nodiscard]] kb::audio::AudioPlayResult PlayOneShot(
        ma_engine& engine,
        kb::scene::Scene& scene,
        const kb::audio::AudioPlayDesc& desc,
        const MiniaudioClipResolver& clipResolver,
        ma_sound_group* group);

    void RemoveFinishedVoices() noexcept;
    void StopAll() noexcept;
    // LIB-149: per-tick sync for owner-attached voices (AudioPlayDesc::ownerEntityId != 0):
    // a live, active owner drives the voice's position from its world transform; a
    // destroyed/deactivated owner releases the voice immediately - even a looping or
    // paused voice can never leak past its owner. LIB-151: a non-null `occlusionSampler`
    // additionally scales each spatial attached voice's volume by its budget-capped
    // occlusion sample (keyed by voiceId, the owner's collider excluded).
    void SyncAttachedVoices(
        kb::scene::Scene& scene,
        MiniaudioOcclusionSampler* occlusionSampler,
        const kb::scene::Vec3& listenerPosition,
        float deltaSeconds);

    // LIB-148: per-voice control - false for a voiceId that is 0, never existed, was
    // stolen, or has already been reclaimed after finishing. Stop REMOVES the record
    // immediately (a stopped ma_sound never reaches at_end, so RemoveFinishedVoices
    // would leak it otherwise);
    // Pause succeeds only for an actively playing sound and keeps the record alive
    // (guarded by `paused` - a paused sound is not playing and not at_end, but must not
    // be reclaimed) until a successful Resume or Stop.
    [[nodiscard]] bool StopVoice(std::uint64_t voiceId) noexcept;
    [[nodiscard]] bool PauseVoice(std::uint64_t voiceId) noexcept;
    [[nodiscard]] bool ResumeVoice(std::uint64_t voiceId) noexcept;
    [[nodiscard]] bool SeekVoice(std::uint64_t voiceId, float positionSeconds) noexcept;
    [[nodiscard]] bool SetVoiceVolume(std::uint64_t voiceId, float volume) noexcept;
    [[nodiscard]] bool SetVoiceMute(std::uint64_t voiceId, bool mute) noexcept;
    [[nodiscard]] bool SetVoicePan(std::uint64_t voiceId, float pan) noexcept;
    [[nodiscard]] bool SetVoicePitch(std::uint64_t voiceId, float pitch) noexcept;
    [[nodiscard]] bool SetVoiceLoop(std::uint64_t voiceId, bool loop) noexcept;
    [[nodiscard]] bool IsVoicePlaying(std::uint64_t voiceId) noexcept;
    // LIB-152: audio-clock playback position (negative for a dead voice) and named
    // markers. Fired markers queue "OnAudioMarker" events through
    // AudioPlayback::QueueMarkerEvent inside DispatchMarkers (called once per tick).
    [[nodiscard]] float VoicePlaybackSeconds(std::uint64_t voiceId) noexcept;
    [[nodiscard]] bool AddVoiceMarker(std::uint64_t voiceId, std::string_view marker, float positionSeconds, kb::scene::SceneEntity target);
    void DispatchMarkers(kb::scene::Scene& scene);

private:
    struct VoiceMarker {
        std::string name;
        float positionSeconds = 0.0F;
        kb::scene::SceneEntity target{};
        bool fired = false;
    };

    struct VoiceRecord {
        std::uint64_t voiceId = 0U;
        std::uint64_t clipAssetId = 0U;
        bool looping = false;
        // LIB-148: voice-stealing priority (AudioPlayDesc::priority) and the paused guard.
        std::uint8_t priority = 128U;
        bool paused = false;
        // LIB-149: 0 = free-standing; otherwise the owning entity this voice follows and
        // dies with (see SyncAttachedVoices).
        std::uint64_t ownerEntityId = 0U;
        // LIB-151: the caller-authored volume/mute/spatial - the base occlusion scales
        // from (SetVoiceVolume updates baseVolume, so a scripted change survives it).
        float baseVolume = 1.0F;
        bool muted = false;
        bool spatial = true;
        kb::scene::Vec3 previousOwnerPosition{};
        bool hasPreviousOwnerPosition = false;
        // LIB-152: named playback markers (see AddVoiceMarker/DispatchMarkers).
        std::vector<VoiceMarker> markers;
        std::unique_ptr<MiniaudioSound> sound;
    };

    [[nodiscard]] std::uint64_t AllocateVoiceId() noexcept;
    [[nodiscard]] VoiceRecord* FindVoice(std::uint64_t voiceId) noexcept;
    [[nodiscard]] bool PruneVoicesForClip(std::uint64_t clipAssetId, std::uint8_t incomingPriority) noexcept;
    // LIB-148: evicts the lowest-priority (ties: oldest) voice while at capacity. Returns
    // false when every live voice outranks `incomingPriority` - the caller must then
    // honestly refuse the new voice instead of stealing a higher-priority one.
    [[nodiscard]] bool PruneVoiceCapacity(std::uint8_t incomingPriority) noexcept;

#if defined(KB_AUDIO_MINIAUDIO_TESTING)
public:
    [[nodiscard]] MiniaudioSound* SoundForTesting(std::uint64_t voiceId) noexcept {
        VoiceRecord* voice = FindVoice(voiceId);
        return voice == nullptr ? nullptr : voice->sound.get();
    }
    [[nodiscard]] std::size_t VoiceCountForTesting() const noexcept { return voices_.size(); }
#endif

    static constexpr std::size_t kMaxOneShotVoices = 64U;
    static constexpr std::size_t kMaxOneShotVoicesPerClip = 8U;

    std::list<VoiceRecord> voices_;
    std::uint64_t nextVoiceId_ = 1U;
};

} // namespace kb::audio_miniaudio
