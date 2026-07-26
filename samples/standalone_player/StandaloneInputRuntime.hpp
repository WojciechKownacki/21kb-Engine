#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::input {

class Win32InputCollector;

} // namespace kb::input

// Runs an OS-level verification against the same collector and Scene runtime
// update path used by the standalone player's normal frame.
[[nodiscard]] bool RunStandaloneInputRuntimeVerification(
    kb::scene::Scene& scene,
    kb::input::Win32InputCollector& collector,
    HWND window);

#endif
