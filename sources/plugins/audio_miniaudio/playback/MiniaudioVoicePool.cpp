#include "playback/MiniaudioVoicePool.hpp"

#include "assets/MiniaudioClipResolver.hpp"
#include "runtime/MiniaudioSound.hpp"

#include <algorithm>
#include <filesystem>

namespace kb::audio_miniaudio {

kb::audio::AudioPlayResult MiniaudioVoicePool::PlayOneShot(
    ma_engine& engine,
    kb::scene::Scene& scene,
    const kb::audio::AudioPlayDesc& desc,
    const MiniaudioClipResolver& clipResolver) {
    const std::filesystem::path path = clipResolver.Resolve(scene, desc.clipAssetId);
    if (path.empty()) {
        return kb::audio::AudioPlayResult{ .started = false, .voiceId = 0U, .error = "audio clip file could not be resolved" };
    }

    PruneVoicesForClip(desc.clipAssetId);
    PruneVoiceCapacity();

    auto sound = std::make_unique<MiniaudioSound>();
    if (sound->InitializeFromFile(engine, path, desc.spatial) != MA_SUCCESS) {
        return kb::audio::AudioPlayResult{ .started = false, .voiceId = 0U, .error = "miniaudio sound could not be created" };
    }

    sound->Apply(MiniaudioSoundSettings{
        .volume = desc.volume,
        .pitch = desc.pitch,
        .loop = desc.loop,
        .spatial = desc.spatial,
        .position = desc.position,
    });

    if (sound->Start() != MA_SUCCESS) {
        return kb::audio::AudioPlayResult{ .started = false, .voiceId = 0U, .error = "miniaudio sound could not be started" };
    }

    const std::uint64_t voiceId = AllocateVoiceId();
    voices_.push_back(VoiceRecord{
        .voiceId = voiceId,
        .clipAssetId = desc.clipAssetId,
        .looping = desc.loop,
        .sound = std::move(sound),
    });
    return kb::audio::AudioPlayResult{ .started = true, .voiceId = voiceId, .error = {} };
}

void MiniaudioVoicePool::RemoveFinishedVoices() noexcept {
    for (auto iter = voices_.begin(); iter != voices_.end();) {
        if (iter->looping || iter->sound == nullptr || !iter->sound->AtEnd()) {
            ++iter;
            continue;
        }
        iter = voices_.erase(iter);
    }
}

void MiniaudioVoicePool::StopAll() noexcept {
    voices_.clear();
}

std::uint64_t MiniaudioVoicePool::AllocateVoiceId() noexcept {
    const std::uint64_t voiceId = nextVoiceId_++;
    if (nextVoiceId_ == 0U) {
        nextVoiceId_ = 1U;
    }
    return voiceId;
}

void MiniaudioVoicePool::PruneVoicesForClip(std::uint64_t clipAssetId) noexcept {
    std::size_t count = 0U;
    for (const VoiceRecord& voice : voices_) {
        if (voice.clipAssetId == clipAssetId) {
            ++count;
        }
    }

    while (count >= kMaxOneShotVoicesPerClip) {
        const auto iter = std::ranges::find_if(voices_, [clipAssetId](const VoiceRecord& voice) {
            return voice.clipAssetId == clipAssetId;
        });
        if (iter == voices_.end()) {
            return;
        }
        voices_.erase(iter);
        --count;
    }
}

void MiniaudioVoicePool::PruneVoiceCapacity() noexcept {
    while (voices_.size() >= kMaxOneShotVoices) {
        voices_.pop_front();
    }
}

} // namespace kb::audio_miniaudio
