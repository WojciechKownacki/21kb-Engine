#pragma once

#include "engine/audio/AudioPlayback.hpp"
#include "runtime/MiniaudioSound.hpp"

#include <miniaudio.h>

#include <cstdint>
#include <list>
#include <memory>

namespace kb::audio_miniaudio {

class MiniaudioClipResolver;

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

    // LIB-148: per-voice control - false for a voiceId that is 0, never existed, or
    // already finished/was stolen. Stop REMOVES the record immediately (a stopped
    // ma_sound never reaches at_end, so RemoveFinishedVoices would leak it otherwise);
    // Pause keeps the record alive (guarded by `paused` - a paused sound is not playing
    // and not at_end, but must not be reclaimed) until Resume or Stop.
    [[nodiscard]] bool StopVoice(std::uint64_t voiceId) noexcept;
    [[nodiscard]] bool PauseVoice(std::uint64_t voiceId) noexcept;
    [[nodiscard]] bool ResumeVoice(std::uint64_t voiceId) noexcept;
    [[nodiscard]] bool SeekVoice(std::uint64_t voiceId, float positionSeconds) noexcept;
    [[nodiscard]] bool SetVoiceVolume(std::uint64_t voiceId, float volume) noexcept;
    [[nodiscard]] bool SetVoicePitch(std::uint64_t voiceId, float pitch) noexcept;
    [[nodiscard]] bool SetVoiceLoop(std::uint64_t voiceId, bool loop) noexcept;
    [[nodiscard]] bool IsVoicePlaying(std::uint64_t voiceId) noexcept;

private:
    struct VoiceRecord {
        std::uint64_t voiceId = 0U;
        std::uint64_t clipAssetId = 0U;
        bool looping = false;
        // LIB-148: voice-stealing priority (AudioPlayDesc::priority) and the paused guard.
        std::uint8_t priority = 128U;
        bool paused = false;
        std::unique_ptr<MiniaudioSound> sound;
    };

    [[nodiscard]] std::uint64_t AllocateVoiceId() noexcept;
    [[nodiscard]] VoiceRecord* FindVoice(std::uint64_t voiceId) noexcept;
    void PruneVoicesForClip(std::uint64_t clipAssetId, std::uint8_t incomingPriority) noexcept;
    // LIB-148: evicts the lowest-priority (ties: oldest) voice while at capacity. Returns
    // false when every live voice outranks `incomingPriority` - the caller must then
    // honestly refuse the new voice instead of stealing a higher-priority one.
    [[nodiscard]] bool PruneVoiceCapacity(std::uint8_t incomingPriority) noexcept;

    static constexpr std::size_t kMaxOneShotVoices = 64U;
    static constexpr std::size_t kMaxOneShotVoicesPerClip = 8U;

    std::list<VoiceRecord> voices_;
    std::uint64_t nextVoiceId_ = 1U;
};

} // namespace kb::audio_miniaudio
