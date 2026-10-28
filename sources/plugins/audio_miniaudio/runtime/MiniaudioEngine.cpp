#include "runtime/MiniaudioEngine.hpp"

#include <algorithm>
#include <chrono>
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
    // Device initialization can prime buffers before ma_engine_init has built
    // its node graph. Do not enter that graph until owner-thread setup finishes.
    if (!self.graphReady_.load(std::memory_order_acquire)) {
        ma_silence_pcm_frames(output, frames, device->playback.format, device->playback.channels);
        return;
    }
    const auto start = std::chrono::steady_clock::now();
    ma_uint64 read = 0U;
    const auto result = self.ReadPcm(static_cast<float*>(output), frames, &read);
    if (read < frames) {
        // The miniaudio endpoint reports a successful zero-frame read when it
        // has no attachments. Attached inputs always fill the endpoint block,
        // including stopped sounds. Idle silence is therefore not a dropout.
        if (read != 0U || result != MA_SUCCESS) self.shortBlocks_.fetch_add(1U, std::memory_order_relaxed);
        const auto channels = ma_engine_get_channels(&engine);
        ma_silence_pcm_frames(static_cast<float*>(output) + read * channels, frames - read, ma_format_f32, channels);
    }
    const auto elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count());
    const auto rate = ma_engine_get_sample_rate(&engine);
    self.deviceBlocks_.fetch_add(1U, std::memory_order_relaxed);
    self.deviceFrames_.fetch_add(frames, std::memory_order_relaxed);
    self.periodFrames_.store(frames, std::memory_order_relaxed);
    if (rate != 0U && elapsed > 1000000000ULL * frames / rate) self.deadlineMisses_.fetch_add(1U, std::memory_order_relaxed);
    auto worst = self.worstCallbackNs_.load(std::memory_order_relaxed);
    while (worst < elapsed && !self.worstCallbackNs_.compare_exchange_weak(worst, elapsed, std::memory_order_relaxed)) {}
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

    ++generation_;
    allocations_.ResetPeak();
    deviceBlocks_.store(0U); deviceFrames_.store(0U); shortBlocks_.store(0U);
    deadlineMisses_.store(0U); worstCallbackNs_.store(0U); periodFrames_.store(0U);
    renderedFrames_.store(0U, std::memory_order_relaxed);
    ma_engine_config config = ma_engine_config_init();
    config.allocationCallbacks = allocations_.Callbacks();
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
        config.allocationCallbacks = allocations_.Callbacks();
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
        // The scene is left-handed (+x is to the right of a listener that faces +z, which is the direction the
        // listener synchronizer passes), while miniaudio assumes a right-handed system by default. Without this
        // a source on the scene's right would sound on the left.
        for (ma_uint32 listener = 0U; listener < ma_engine_get_listener_count(&engine_); ++listener) {
            engine_.listeners[listener].config.handedness = ma_handedness_left;
        }
        graphReady_.store(true, std::memory_order_release);
    }
}

void MiniaudioEngine::Shutdown() noexcept {
    graphReady_.store(false, std::memory_order_release);
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

kb::audio::AudioRuntimeDiagnostics MiniaudioEngine::Diagnostics() const {
    kb::audio::AudioRuntimeDiagnostics report;
    report.supported = true; report.playbackAvailable = IsPlaybackAvailable(); report.generation = generation_;
    report.deviceBlocks = deviceBlocks_.load(); report.deviceFrames = deviceFrames_.load();
    report.shortBlocks = shortBlocks_.load(); report.callbackDeadlineMisses = deadlineMisses_.load();
    report.worstCallbackNanoseconds = worstCallbackNs_.load(); report.periodFrames = periodFrames_.load();
    report.allocatedBytes = allocations_.current.load(); report.peakAllocatedBytes = std::max(allocations_.peak.load(), report.allocatedBytes);
    report.allocationCount = allocations_.count.load();
    if (initialized_) {
        report.sampleRate = ma_engine_get_sample_rate(&engine_);
        const auto* device = ma_engine_get_device(const_cast<ma_engine*>(&engine_));
        if (device != nullptr && device->pContext != nullptr) report.backend = ma_get_backend_name(device->pContext->backend);
    }
    return report;
}

ma_engine& MiniaudioEngine::Native() noexcept {
    return engine_;
}

} // namespace kb::audio_miniaudio
