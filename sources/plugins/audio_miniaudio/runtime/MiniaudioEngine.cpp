#include "runtime/MiniaudioEngine.hpp"

#include <algorithm>
#include <limits>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

namespace kb::audio_miniaudio {

void MiniaudioEngine::ProcessDevice(ma_device* device, void* output, const void* input, ma_uint32 frames) {
    static_cast<void>(input);
    if (output == nullptr) return;
    auto& engine = *static_cast<ma_engine*>(device->pUserData);
    auto& self = *static_cast<MiniaudioEngine*>(engine.pProcessUserData);
    ma_uint64 read = 0U;
    static_cast<void>(self.ReadPcm(static_cast<float*>(output), frames, &read));
    if (read < frames) {
        const auto channels = ma_engine_get_channels(&engine);
        ma_silence_pcm_frames(static_cast<float*>(output) + read * channels, frames - read, ma_format_f32, channels);
    }
}

ma_result MiniaudioEngine::ReadPcm(float* output, ma_uint64 frames, ma_uint64* framesRead) {
    const auto lock = LockGraph();
    ma_uint64 read = 0U;
    const auto result = ma_engine_read_pcm_frames(&engine_, output, frames, &read);
    if (output != nullptr) {
        for (ma_uint64 offset = 0U; offset < read;) {
            const auto count = static_cast<std::uint32_t>(std::min(read - offset,
                static_cast<ma_uint64>(std::numeric_limits<std::uint32_t>::max())));
            masterMeter_.Observe(output + offset * ma_engine_get_channels(&engine_), count);
            offset += count;
        }
    }
    renderedFrames_.fetch_add(read, std::memory_order_relaxed);
    if (framesRead != nullptr) *framesRead = read;
    return result;
}

std::unique_lock<std::mutex> MiniaudioEngine::LockGraph() { return std::unique_lock{graphMutex_}; }

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

void MiniaudioEngine::Initialize(bool forceNoDevice, ma_context* context) {
    if (initialized_) {
        return;
    }

    renderedFrames_.store(0U, std::memory_order_relaxed);
    ma_engine_config config = ma_engine_config_init();
    config.pContext = context;
    config.dataCallback = &MiniaudioEngine::ProcessDevice;
    config.pProcessUserData = this;
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
        config.pProcessUserData = this;
        config.noDevice = MA_TRUE;
        config.channels = 2U;
        config.sampleRate = 48000U;
        result = ma_engine_init(&config, &engine_);
    }

    initialized_ = result == MA_SUCCESS;
    initializationFailed_ = !initialized_;
    if (!initialized_) {
        playbackAvailable_ = false;
    } else {
        const auto lock = LockGraph();
        static_cast<void>(masterMeter_.Configure(ma_engine_get_channels(&engine_), false));
    }
}

void MiniaudioEngine::Shutdown() noexcept {
    if (initialized_) {
        ma_engine_uninit(&engine_);
        initialized_ = false;
    }
    playbackAvailable_ = false;
    initializationFailed_ = false;
    const auto lock = LockGraph();
    static_cast<void>(masterMeter_.Configure(1U, false));
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
