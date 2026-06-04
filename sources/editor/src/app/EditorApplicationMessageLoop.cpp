#include "app/EditorApplicationMessageLoop.hpp"

#if defined(_WIN32)

#include "engine/scene/SceneRuntime.hpp"

#include <algorithm>
#include <chrono>

namespace kb::editor {
namespace {

constexpr float kMaximumRuntimeDeltaSeconds = 1.0F / 15.0F;
constexpr DWORD kPausedToolbarAnimationIntervalMs = 33;

[[nodiscard]] float RuntimeDeltaSeconds(std::chrono::steady_clock::time_point previous, std::chrono::steady_clock::time_point current) noexcept {
    const std::chrono::duration<float> delta = current - previous;
    return std::clamp(delta.count(), 0.0F, kMaximumRuntimeDeltaSeconds);
}

void TickPlayMode(EditorApplicationState& state, float deltaSeconds) {
    if (!state.playMode.IsPlaying()) {
        return;
    }
    static_cast<void>(state.sceneContext.Scene().Runtime().Update(deltaSeconds));
    if (state.sceneContext.Scene().Runtime().ShouldQuit()) {
        state.playMode.Stop();
    }
    if (state.window != nullptr) {
        InvalidateRect(state.window, nullptr, FALSE);
    }
}

} // namespace

void EditorApplicationMessageLoop::Run(EditorApplicationState& state) {
    MSG message{};
    auto previousTick = std::chrono::steady_clock::now();
    while (state.running) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
            if (message.message == WM_QUIT) {
                state.running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!state.running) {
            break;
        }

        const auto currentTick = std::chrono::steady_clock::now();
        const float deltaSeconds = RuntimeDeltaSeconds(previousTick, currentTick);
        previousTick = currentTick;
        TickPlayMode(state, deltaSeconds);

        if (state.playMode.IsPaused()) {
            if (state.window != nullptr) {
                InvalidateRect(state.window, nullptr, FALSE);
            }
            static_cast<void>(MsgWaitForMultipleObjects(0, nullptr, FALSE, kPausedToolbarAnimationIntervalMs, QS_ALLINPUT));
        } else if (!state.playMode.IsPlaying()) {
            WaitMessage();
        } else {
            Sleep(1);
        }
    }
}

} // namespace kb::editor

#endif
