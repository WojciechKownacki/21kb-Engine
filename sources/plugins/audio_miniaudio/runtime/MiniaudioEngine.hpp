#pragma once

#include "engine/audio/AudioPlayback.hpp"
#include "runtime/MiniaudioMeterNode.hpp"
#include "runtime/MiniaudioAllocationTracker.hpp"
#include "engine/audio/AudioDiagnostics.hpp"

#include <miniaudio.h>
#include <mutex>
#include <atomic>

namespace kb::audio_miniaudio {

class MiniaudioEngine final {
public:
    MiniaudioEngine() = default;
    ~MiniaudioEngine();

    MiniaudioEngine(const MiniaudioEngine&) = delete;
    MiniaudioEngine& operator=(const MiniaudioEngine&) = delete;
    MiniaudioEngine(MiniaudioEngine&&) = delete;
    MiniaudioEngine& operator=(MiniaudioEngine&&) = delete;

    // An externally owned context, when supplied, outlives the engine/device.
    void Initialize(bool forceNoDevice = false, ma_context* context = nullptr);
    void Shutdown() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool IsPlaybackAvailable() const noexcept;
    [[nodiscard]] kb::audio::AudioDeviceStatus Status() const noexcept;
    [[nodiscard]] kb::audio::AudioRuntimeDiagnostics Diagnostics() const;
    [[nodiscard]] ma_engine& Native() noexcept;
    [[nodiscard]] ma_result ReadPcm(float* output, ma_uint64 frames, ma_uint64* framesRead);
    [[nodiscard]] std::unique_lock<std::mutex> LockGraph();
    [[nodiscard]] std::uint64_t RenderedFrames() const noexcept { return renderedFrames_.load(std::memory_order_relaxed); }
    [[nodiscard]] MiniaudioMeterAccumulator& MasterMeter() noexcept { return masterMeter_; }
    [[nodiscard]] const MiniaudioMeterAccumulator& MasterMeter() const noexcept { return masterMeter_; }

private:
    static void ProcessDevice(ma_device* device, void* output, const void* input, ma_uint32 frames);
#if defined(__EMSCRIPTEN__)
    static void ObserveOutput(void* user, float* frames, ma_uint64 frameCount) noexcept;
    std::atomic<double> outputActiveUntilMs_{0.0};
#endif
    std::atomic<bool> graphReady_{false};
    std::mutex graphMutex_;
    MiniaudioMeterAccumulator masterMeter_;
    std::atomic<std::uint64_t> renderedFrames_{0U};
    MiniaudioAllocationTracker allocations_;
    std::atomic<std::uint64_t> deviceBlocks_{0U}, deviceFrames_{0U}, shortBlocks_{0U}, deadlineMisses_{0U}, worstCallbackNs_{0U};
    std::atomic<std::uint32_t> periodFrames_{0U};
    std::uint64_t generation_ = 0U;
    ma_engine engine_{};
    bool initialized_ = false;
    bool playbackAvailable_ = false;
    bool initializationFailed_ = false;
};

} // namespace kb::audio_miniaudio
