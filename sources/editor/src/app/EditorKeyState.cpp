#include "app/EditorKeyState.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <atomic>

namespace kb::editor {
namespace {

std::atomic<int> g_scriptedInputScopes{ 0 };
thread_local int g_scriptedKey = -1;

[[nodiscard]] bool Scripted() noexcept {
    return g_scriptedInputScopes.load(std::memory_order_acquire) > 0;
}

} // namespace

bool EditorKeyDown(int virtualKey) noexcept {
#if defined(_WIN32)
    return virtualKey == g_scriptedKey || (!Scripted() && (GetKeyState(virtualKey) & 0x8000) != 0);
#else
    static_cast<void>(virtualKey);
    return false;
#endif
}

bool EditorAsyncKeyDown(int virtualKey) noexcept {
#if defined(_WIN32)
    return virtualKey == g_scriptedKey || (!Scripted() && (GetAsyncKeyState(virtualKey) & 0x8000) != 0);
#else
    static_cast<void>(virtualKey);
    return false;
#endif
}

EditorScriptedKeyScope::EditorScriptedKeyScope(int key) noexcept : previous_(g_scriptedKey) { g_scriptedKey = key; }
EditorScriptedKeyScope::~EditorScriptedKeyScope() { g_scriptedKey = previous_; }

EditorScriptedInputScope::EditorScriptedInputScope() noexcept {
    g_scriptedInputScopes.fetch_add(1, std::memory_order_acq_rel);
}

EditorScriptedInputScope::~EditorScriptedInputScope() {
    g_scriptedInputScopes.fetch_sub(1, std::memory_order_acq_rel);
}

} // namespace kb::editor
