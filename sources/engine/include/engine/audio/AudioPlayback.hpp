#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <cstdint>
#include <string>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::audio {

struct AudioPlayDesc {
    std::uint64_t clipAssetId = 0U;
    float volume = 1.0F;
    float pitch = 1.0F;
    bool loop = false;
    bool spatial = true;
    kb::scene::Vec3 position{};
};

struct AudioPlayResult {
    bool started = false;
    std::uint64_t voiceId = 0U;
    std::string error;

    [[nodiscard]] bool Succeeded() const noexcept {
        return started && voiceId != 0U && error.empty();
    }
};

class IAudioPlaybackBackend {
public:
    virtual ~IAudioPlaybackBackend() = default;

    [[nodiscard]] virtual AudioPlayResult PlayOneShot(kb::scene::Scene& scene, const AudioPlayDesc& desc) = 0;
    virtual void StopAll(kb::scene::Scene& scene) noexcept = 0;
};

class AudioPlayback final {
public:
    AudioPlayback() = delete;

    static void RegisterBackend(kb::scene::Scene& scene, IAudioPlaybackBackend& backend);
    static void UnregisterBackend(kb::scene::Scene& scene, IAudioPlaybackBackend& backend) noexcept;
    [[nodiscard]] static bool HasBackend(kb::scene::Scene& scene) noexcept;
    [[nodiscard]] static AudioPlayResult PlayOneShot(kb::scene::Scene& scene, const AudioPlayDesc& desc);
    static void StopAll(kb::scene::Scene& scene) noexcept;
};

} // namespace kb::audio
