#pragma once

#include "engine/assets/AssetId.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace kb::visual {

struct VisualGraphDebugBreakpoint {
    kb::assets::AssetId assetId{};
    std::uint32_t nodeId = 0U;
    bool enabled = true;
};

enum class VisualGraphDebugPauseReason : std::uint8_t {
    Breakpoint,
    Step,
};

struct VisualGraphDebugPauseSnapshot {
    bool valid = false;
    kb::assets::AssetId assetId{};
    std::uint32_t eventNodeId = 0U;
    std::uint32_t nodeId = 0U;
    VisualGraphDebugPauseReason reason = VisualGraphDebugPauseReason::Breakpoint;
};

// Runtime-owned debugger state for Visual Graph execution. It is deliberately
// single-threaded: graph execution and editor Play Mode both run on the scene
// thread, so synchronisation here would only obscure ordering guarantees.
class VisualGraphDebugSession final {
public:
    void SetBreakpoints(std::vector<VisualGraphDebugBreakpoint> breakpoints) {
        breakpoints_ = std::move(breakpoints);
    }
    [[nodiscard]] const std::vector<VisualGraphDebugBreakpoint>& Breakpoints() const noexcept { return breakpoints_; }

    void RequestStepInto() noexcept { stepRequested_ = true; stepStarted_ = false; SkipPausedNodeOnce(); }
    void Resume() noexcept { stepRequested_ = false; stepStarted_ = false; SkipPausedNodeOnce(); }
    void ClearPause() noexcept { lastPause_ = {}; skipNodeOnce_ = 0U; }
    [[nodiscard]] const VisualGraphDebugPauseSnapshot& LastPause() const noexcept { return lastPause_; }

    [[nodiscard]] bool ShouldPause(kb::assets::AssetId assetId, std::uint32_t eventNodeId, std::uint32_t nodeId) noexcept {
        if (nodeId == 0U) return false;
        if (skipNodeOnce_ == nodeId && skipAssetOnce_ == assetId.value) {
            skipNodeOnce_ = 0U;
            skipAssetOnce_ = 0U;
            if (stepRequested_) stepStarted_ = true;
            return false;
        }
        if (stepRequested_) {
            if (!stepStarted_) {
                stepStarted_ = true;
                return false;
            }
            stepRequested_ = false;
            RecordPause(assetId, eventNodeId, nodeId, VisualGraphDebugPauseReason::Step);
            return true;
        }
        for (const VisualGraphDebugBreakpoint& breakpoint : breakpoints_) {
            if (breakpoint.enabled && breakpoint.assetId == assetId && breakpoint.nodeId == nodeId) {
                RecordPause(assetId, eventNodeId, nodeId, VisualGraphDebugPauseReason::Breakpoint);
                return true;
            }
        }
        return false;
    }

private:
    void RecordPause(kb::assets::AssetId assetId, std::uint32_t eventNodeId, std::uint32_t nodeId, VisualGraphDebugPauseReason reason) noexcept {
        lastPause_ = { .valid = true, .assetId = assetId, .eventNodeId = eventNodeId, .nodeId = nodeId, .reason = reason };
    }
    void SkipPausedNodeOnce() noexcept {
        if (lastPause_.valid) {
            skipAssetOnce_ = lastPause_.assetId.value;
            skipNodeOnce_ = lastPause_.nodeId;
        }
    }

    std::vector<VisualGraphDebugBreakpoint> breakpoints_;
    VisualGraphDebugPauseSnapshot lastPause_;
    std::uint64_t skipAssetOnce_ = 0U;
    std::uint32_t skipNodeOnce_ = 0U;
    bool stepRequested_ = false;
    bool stepStarted_ = false;
};

} // namespace kb::visual
