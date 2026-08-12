#include "runtime/MiniaudioEngine.hpp"

namespace kb::audio_miniaudio {

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
    return IsPlaybackAvailable()
        ? kb::audio::AudioDeviceStatus::PlaybackAvailable
        : kb::audio::AudioDeviceStatus::NoPlaybackDevice;
}

ma_engine& MiniaudioEngine::Native() noexcept {
    return engine_;
}

} // namespace kb::audio_miniaudio
