#include "app/EditorKeyState.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>

namespace kb::editor {
namespace {

std::atomic<int> g_scriptedInputScopes{ 0 };
// The keys held down by scripted-key scopes: a shortcut can need a few at once (Ctrl and Shift).
constexpr std::size_t kScriptedKeySlots = 4;
thread_local std::array<int, kScriptedKeySlots> g_scriptedKeys = [] {
    std::array<int, kScriptedKeySlots> keys{};
    keys.fill(-1);
    return keys;
}();

[[nodiscard]] bool ScriptedKeyDown(int virtualKey) noexcept {
    return virtualKey >= 0 && std::ranges::find(g_scriptedKeys, virtualKey) != g_scriptedKeys.end();
}

[[nodiscard]] bool Scripted() noexcept {
    return g_scriptedInputScopes.load(std::memory_order_acquire) > 0;
}

} // namespace

bool EditorKeyDown(int virtualKey) noexcept {
#if defined(_WIN32)
    return ScriptedKeyDown(virtualKey) || (!Scripted() && (GetKeyState(virtualKey) & 0x8000) != 0);
#else
    static_cast<void>(virtualKey);
    return false;
#endif
}

bool EditorAsyncKeyDown(int virtualKey) noexcept {
#if defined(_WIN32)
    return ScriptedKeyDown(virtualKey) || (!Scripted() && (GetAsyncKeyState(virtualKey) & 0x8000) != 0);
#else
    static_cast<void>(virtualKey);
    return false;
#endif
}

EditorScriptedKeyScope::EditorScriptedKeyScope(int key) noexcept {
    for (std::size_t slot = 0; slot < g_scriptedKeys.size(); ++slot) {
        if (g_scriptedKeys[slot] < 0) {
            g_scriptedKeys[slot] = key;
            slot_ = static_cast<int>(slot);
            break;
        }
    }
}

EditorScriptedKeyScope::~EditorScriptedKeyScope() {
    if (slot_ >= 0) {
        g_scriptedKeys[static_cast<std::size_t>(slot_)] = -1;
    }
}

EditorScriptedInputScope::EditorScriptedInputScope() noexcept {
    g_scriptedInputScopes.fetch_add(1, std::memory_order_acq_rel);
}

EditorScriptedInputScope::~EditorScriptedInputScope() {
    g_scriptedInputScopes.fetch_sub(1, std::memory_order_acq_rel);
}

} // namespace kb::editor
