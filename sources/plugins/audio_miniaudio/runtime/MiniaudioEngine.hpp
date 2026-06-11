#pragma once

#include <miniaudio.h>

namespace kb::audio_miniaudio {

class MiniaudioEngine final {
public:
    MiniaudioEngine() = default;
    ~MiniaudioEngine();

    MiniaudioEngine(const MiniaudioEngine&) = delete;
    MiniaudioEngine& operator=(const MiniaudioEngine&) = delete;
    MiniaudioEngine(MiniaudioEngine&&) = delete;
    MiniaudioEngine& operator=(MiniaudioEngine&&) = delete;

    void Initialize();
    void Shutdown() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool IsPlaybackAvailable() const noexcept;
    [[nodiscard]] ma_engine& Native() noexcept;

private:
    ma_engine engine_{};
    bool initialized_ = false;
    bool playbackAvailable_ = false;
};

} // namespace kb::audio_miniaudio
