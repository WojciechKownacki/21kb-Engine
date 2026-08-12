#pragma once

#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "runtime/MiniaudioSound.hpp"

#include <miniaudio.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace kb::scene {

class Scene;
class SceneSystemContext;

} // namespace kb::scene

namespace kb::audio_miniaudio {

class MiniaudioBusRegistry;
class MiniaudioClipResolver;
class MiniaudioOcclusionSampler;

class MiniaudioSourceRegistry final {
public:
    // LIB-151: `occlusionSampler` + `listenerPosition` drive the per-source occlusion
    // volume scale (spatial sources only; sampler is budget-capped, see
    // MiniaudioOcclusionSampler.hpp). nullptr sampler = occlusion disabled.
    void Sync(
        ma_engine& engine,
        kb::scene::SceneSystemContext& context,
        const MiniaudioClipResolver& clipResolver,
        MiniaudioBusRegistry& busRegistry,
        MiniaudioOcclusionSampler* occlusionSampler,
        const kb::scene::Vec3& listenerPosition,
        bool playbackAvailable);

    void StopAll() noexcept;
    void ReleaseNativeResources() noexcept;

    [[nodiscard]] kb::audio::AudioSourceControlResult PlaySource(
        ma_engine& engine,
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        const MiniaudioClipResolver& clipResolver,
        MiniaudioBusRegistry& busRegistry,
        bool playbackAvailable);
    [[nodiscard]] kb::audio::AudioSourceControlResult PauseSource(kb::scene::Scene& scene, kb::scene::SceneEntity entity, bool playbackAvailable);
    [[nodiscard]] kb::audio::AudioSourceControlResult ResumeSource(kb::scene::Scene& scene, kb::scene::SceneEntity entity, bool playbackAvailable);
    [[nodiscard]] kb::audio::AudioSourceControlResult StopSource(kb::scene::Scene& scene, kb::scene::SceneEntity entity, bool playbackAvailable);
    [[nodiscard]] kb::audio::AudioSourceControlResult IsSourcePlaying(kb::scene::Scene& scene, kb::scene::SceneEntity entity, bool playbackAvailable);

private:
    // LIB-147: busName/busGeneration are part of the identity - re-routing a source to a
    // different bus, or a mixer topology rebuild invalidating every ma_sound_group,
    // recreates the ma_sound exactly like a clip path change already does.
    struct SoundSignature {
        std::uint64_t clipAssetId = 0U;
        std::filesystem::path path;
        std::string busName;
        std::uint64_t busGeneration = 0U;
        bool spatial = true;

        [[nodiscard]] friend bool operator==(const SoundSignature&, const SoundSignature&) noexcept = default;
    };

    struct SoundRecord {
        enum class PlaybackState : std::uint8_t {
            Stopped,
            Playing,
            Paused,
        };

        SoundSignature signature{};
        std::unique_ptr<MiniaudioSound> sound;
        PlaybackState playbackState = PlaybackState::Stopped;
        ma_uint64 resumeFrame = 0U;
        kb::scene::Vec3 previousPosition{};
        bool hasPreviousPosition = false;
    };

    struct SourceSyncContext {
        MiniaudioSourceRegistry* registry = nullptr;
        ma_engine* engine = nullptr;
        kb::scene::Scene* scene = nullptr;
        const MiniaudioClipResolver* clipResolver = nullptr;
        MiniaudioBusRegistry* busRegistry = nullptr;
        MiniaudioOcclusionSampler* occlusionSampler = nullptr;
        kb::scene::Vec3 listenerPosition{};
        bool playbackAvailable = false;
        float deltaSeconds = 0.0F;
    };

    static void SyncSourceFromTransform(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, void* context);

    void SyncSource(
        ma_engine& engine,
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        const kb::scene::TransformComponent& transform,
        const MiniaudioClipResolver& clipResolver,
        MiniaudioBusRegistry& busRegistry,
        MiniaudioOcclusionSampler* occlusionSampler,
        const kb::scene::Vec3& listenerPosition,
        bool playbackAvailable,
        float deltaSeconds);

    [[nodiscard]] SoundRecord* EnsureSound(
        ma_engine& engine,
        std::uint64_t entityId,
        const SoundSignature& signature,
        const kb::scene::AudioSourceComponent& source,
        const kb::scene::TransformComponent& transform,
        const kb::scene::Vec3& velocity,
        ma_sound_group* group);

    [[nodiscard]] static kb::audio::AudioSourceControlResult ValidateSource(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        bool playbackAvailable) noexcept;
    static void SwapSoundRecords(SoundRecord& first, SoundRecord& second) noexcept;

#if defined(KB_AUDIO_MINIAUDIO_TESTING)
public:
    [[nodiscard]] std::size_t SoundCountForTesting() const noexcept { return sounds_.size(); }
    [[nodiscard]] std::size_t NativeSoundCountForTesting() const noexcept {
        std::size_t count = 0U;
        for (const auto& [_, record] : sounds_) {
            count += record.sound != nullptr ? 1U : 0U;
        }
        return count;
    }
    [[nodiscard]] MiniaudioSound* SoundForTesting(std::uint64_t entityId) noexcept {
        const auto iterator = sounds_.find(entityId);
        return iterator == sounds_.end() ? nullptr : iterator->second.sound.get();
    }
#endif

    void RemoveUnseenSounds();
    void RemoveSound(std::uint64_t entityId);

    std::unordered_map<std::uint64_t, SoundRecord> sounds_;
    std::unordered_set<std::uint64_t> seenEntities_;
};

} // namespace kb::audio_miniaudio
