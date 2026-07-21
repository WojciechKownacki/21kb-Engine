#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>

namespace kb::editor {

// Environment switch for the editor's diagnostic logging, so a developer can turn a trace on without a
// rebuild and a shipping editor pays nothing for it.
//
// Treated as ON when the variable exists and its value is neither empty nor "0". Reads the environment on
// every call - callers that sit in a hot path are expected to cache the result in a function-local static,
// which also means a process picks its answer once and keeps it.
[[nodiscard]] inline bool EditorDebugLogVariableEnabled(const char* name) noexcept {
    std::array<char, 16U> value{};
    const DWORD length = GetEnvironmentVariableA(name, value.data(), static_cast<DWORD>(value.size()));
    if (length == 0U) {
        return false; // unset (or unreadable)
    }
    if (length >= value.size()) {
        return true; // set to something longer than the buffer, which is certainly not "0"
    }
    return value[0] != '\0' && !(value[0] == '0' && length == 1U);
}

} // namespace kb::editor

#endif
