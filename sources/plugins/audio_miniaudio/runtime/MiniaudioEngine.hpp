#pragma once

#include "engine/audio/AudioPlayback.hpp"

#include <miniaudio.h>

#if defined(__EMSCRIPTEN__)
#include <atomic>
#endif

namespace kb::audio_miniaudio {

class MiniaudioEngine final {
public:
    MiniaudioEngine() = default;
    ~MiniaudioEngine();

    MiniaudioEngine(const MiniaudioEngine&) = delete;
    MiniaudioEngine& operator=(const MiniaudioEngine&) = delete;
    MiniaudioEngine(MiniaudioEngine&&) = delete;
    MiniaudioEngine& operator=(MiniaudioEngine&&) = delete;

    void Initialize(bool forceNoDevice = false);
    void Shutdown() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool IsPlaybackAvailable() const noexcept;
    [[nodiscard]] kb::audio::AudioDeviceStatus Status() const noexcept;
    [[nodiscard]] ma_engine& Native() noexcept;

private:
#if defined(__EMSCRIPTEN__)
    static void ObserveOutput(void* user, float* frames, ma_uint64 frameCount) noexcept;
    std::atomic<double> outputActiveUntilMs_{0.0};
#endif
    ma_engine engine_{};
    bool initialized_ = false;
    bool playbackAvailable_ = false;
    bool initializationFailed_ = false;
};

} // namespace kb::audio_miniaudio
