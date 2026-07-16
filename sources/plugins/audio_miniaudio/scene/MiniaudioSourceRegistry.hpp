#pragma once

#include "engine/scene/AudioSourceComponent.hpp"
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

class MiniaudioSourceRegistry final {
public:
    void Sync(
        ma_engine& engine,
        kb::scene::SceneSystemContext& context,
        const MiniaudioClipResolver& clipResolver,
        MiniaudioBusRegistry& busRegistry,
        bool playbackAvailable);

    void StopAll() noexcept;

private:
    // LIB-147: busName/busGeneration are part of the identity - re-routing a source to a
    // different bus, or a mixer topology rebuild invalidating every ma_sound_group,
    // recreates the ma_sound exactly like a clip path change already does.
    struct SoundSignature {
        std::uint64_t clipAssetId = 0U;
        std::filesystem::path path;
        std::string busName;
        std::uint64_t busGeneration = 0U;

        [[nodiscard]] friend bool operator==(const SoundSignature&, const SoundSignature&) noexcept = default;
    };

    struct SoundRecord {
        SoundSignature signature{};
        std::unique_ptr<MiniaudioSound> sound;
    };

    struct SourceSyncContext {
        MiniaudioSourceRegistry* registry = nullptr;
        ma_engine* engine = nullptr;
        kb::scene::Scene* scene = nullptr;
        const MiniaudioClipResolver* clipResolver = nullptr;
        MiniaudioBusRegistry* busRegistry = nullptr;
        bool playbackAvailable = false;
    };

    static void SyncSourceFromTransform(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, void* context);

    void SyncSource(
        ma_engine& engine,
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        const kb::scene::TransformComponent& transform,
        const MiniaudioClipResolver& clipResolver,
        MiniaudioBusRegistry& busRegistry,
        bool playbackAvailable);

    [[nodiscard]] SoundRecord* EnsureSound(
        ma_engine& engine,
        std::uint64_t entityId,
        const SoundSignature& signature,
        const kb::scene::AudioSourceComponent& source,
        const kb::scene::TransformComponent& transform,
        ma_sound_group* group);

    void RemoveUnseenSounds();
    void RemoveSound(std::uint64_t entityId);

    std::unordered_map<std::uint64_t, SoundRecord> sounds_;
    std::unordered_set<std::uint64_t> seenEntities_;
};

} // namespace kb::audio_miniaudio
