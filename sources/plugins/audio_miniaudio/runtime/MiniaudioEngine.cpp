#include "runtime/MiniaudioEngine.hpp"

namespace kb::audio_miniaudio {

MiniaudioEngine::~MiniaudioEngine() {
    Shutdown();
}

void MiniaudioEngine::Initialize() {
    if (initialized_) {
        return;
    }

    ma_engine_config config = ma_engine_config_init();
    ma_result result = ma_engine_init(&config, &engine_);
    playbackAvailable_ = result == MA_SUCCESS;
    if (result != MA_SUCCESS) {
        config = ma_engine_config_init();
        config.noDevice = MA_TRUE;
        result = ma_engine_init(&config, &engine_);
    }

    initialized_ = result == MA_SUCCESS;
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
}

bool MiniaudioEngine::IsInitialized() const noexcept {
    return initialized_;
}

bool MiniaudioEngine::IsPlaybackAvailable() const noexcept {
    return playbackAvailable_;
}

ma_engine& MiniaudioEngine::Native() noexcept {
    return engine_;
}

} // namespace kb::audio_miniaudio
