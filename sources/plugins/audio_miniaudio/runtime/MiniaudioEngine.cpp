#include "runtime/MiniaudioEngine.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

namespace kb::audio_miniaudio {

#if defined(__EMSCRIPTEN__)
void MiniaudioEngine::ObserveOutput(void* user, float*, ma_uint64 frameCount) noexcept {
    if (frameCount == 0U) {
        return;
    }
    auto& self = *static_cast<MiniaudioEngine*>(user);
    const double blockMs = 1000.0 * static_cast<double>(frameCount) / ma_engine_get_sample_rate(&self.engine_);
    // Allow ordinary browser callback jitter and at least four output blocks.
    const double graceMs = blockMs * 4.0 > 250.0 ? blockMs * 4.0 : 250.0;
    self.outputActiveUntilMs_.store(emscripten_get_now() + graceMs, std::memory_order_relaxed);
}
#endif

MiniaudioEngine::~MiniaudioEngine() {
    Shutdown();
}

void MiniaudioEngine::Initialize(bool forceNoDevice) {
    if (initialized_) {
        return;
    }

    ma_engine_config config = ma_engine_config_init();
    ma_result result = MA_ERROR;
    if (!forceNoDevice) {
#if defined(__EMSCRIPTEN__)
        config.onProcess = &MiniaudioEngine::ObserveOutput;
        config.pProcessUserData = this;
#endif
        result = ma_engine_init(&config, &engine_);
        playbackAvailable_ = result == MA_SUCCESS;
    }
    if (forceNoDevice || result != MA_SUCCESS) {
        config = ma_engine_config_init();
        config.noDevice = MA_TRUE;
        config.channels = 2U;
        config.sampleRate = 48000U;
        result = ma_engine_init(&config, &engine_);
    }

    initialized_ = result == MA_SUCCESS;
    initializationFailed_ = !initialized_;
    if (!initialized_) {
        playbackAvailable_ = false;
    }
}

void MiniaudioEngine::Shutdown() noexcept {
    if (initialized_) {
        ma_engine_uninit(&engine_);
        initialized_ = false;
    }
    playbackAvailable_ = false;
    initializationFailed_ = false;
#if defined(__EMSCRIPTEN__)
    outputActiveUntilMs_.store(0.0, std::memory_order_relaxed);
#endif
}

bool MiniaudioEngine::IsInitialized() const noexcept {
    return initialized_;
}

bool MiniaudioEngine::IsPlaybackAvailable() const noexcept {
    if (!initialized_ || !playbackAvailable_) {
        return false;
    }
    ma_device* device = ma_engine_get_device(const_cast<ma_engine*>(&engine_));
    if (device == nullptr) {
        return false;
    }
    const ma_device_state state = ma_device_get_state(device);
    return state == ma_device_state_started || state == ma_device_state_starting;
}

kb::audio::AudioDeviceStatus MiniaudioEngine::Status() const noexcept {
    if (initializationFailed_) {
        return kb::audio::AudioDeviceStatus::Uninitialized;
    }
    if (!initialized_) {
        return kb::audio::AudioDeviceStatus::Uninitialized;
    }
    if (!IsPlaybackAvailable()) {
        return kb::audio::AudioDeviceStatus::NoPlaybackDevice;
    }
#if defined(__EMSCRIPTEN__)
    // A started WebAudio device can still await browser activation or stop
    // processing during suspension. Observe the public mixer callback rather
    // than treating native transport state as proof of output activity.
    if (emscripten_get_now() >= outputActiveUntilMs_.load(std::memory_order_relaxed)) {
        return kb::audio::AudioDeviceStatus::PlaybackSuspended;
    }
#endif
    return kb::audio::AudioDeviceStatus::PlaybackAvailable;
}

ma_engine& MiniaudioEngine::Native() noexcept {
    return engine_;
}

} // namespace kb::audio_miniaudio
