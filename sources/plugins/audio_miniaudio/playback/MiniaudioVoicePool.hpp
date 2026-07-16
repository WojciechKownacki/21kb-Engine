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

private:
    struct VoiceRecord {
        std::uint64_t voiceId = 0U;
        std::uint64_t clipAssetId = 0U;
        bool looping = false;
        std::unique_ptr<MiniaudioSound> sound;
    };

    [[nodiscard]] std::uint64_t AllocateVoiceId() noexcept;
    void PruneVoicesForClip(std::uint64_t clipAssetId) noexcept;
    void PruneVoiceCapacity() noexcept;

    static constexpr std::size_t kMaxOneShotVoices = 64U;
    static constexpr std::size_t kMaxOneShotVoicesPerClip = 8U;

    std::list<VoiceRecord> voices_;
    std::uint64_t nextVoiceId_ = 1U;
};

} // namespace kb::audio_miniaudio
