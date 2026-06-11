#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <miniaudio.h>

#include <filesystem>

namespace kb::audio_miniaudio {

struct MiniaudioSoundSettings {
    float volume = 1.0F;
    float pitch = 1.0F;
    bool loop = false;
    bool spatial = true;
    kb::scene::Vec3 position{};
};

class MiniaudioSound final {
public:
    MiniaudioSound() = default;
    ~MiniaudioSound();

    MiniaudioSound(const MiniaudioSound&) = delete;
    MiniaudioSound& operator=(const MiniaudioSound&) = delete;
    MiniaudioSound(MiniaudioSound&&) = delete;
    MiniaudioSound& operator=(MiniaudioSound&&) = delete;

    [[nodiscard]] ma_result InitializeFromFile(ma_engine& engine, const std::filesystem::path& path, bool spatial);
    void Reset() noexcept;

    void Apply(const MiniaudioSoundSettings& settings) noexcept;
    [[nodiscard]] ma_result Start() noexcept;
    [[nodiscard]] bool AtEnd() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    ma_sound sound_{};
    bool initialized_ = false;
};

} // namespace kb::audio_miniaudio
