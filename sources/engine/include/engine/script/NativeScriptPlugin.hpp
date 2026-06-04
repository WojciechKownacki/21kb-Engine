#pragma once

#include "engine/script/ScriptEvent.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/script/ScriptLifecycle.hpp"

#include <cstdint>

namespace kb::script {

inline constexpr std::uint32_t kNativeScriptPluginApiVersion = 1U;
inline constexpr const char* kNativeScriptPluginDefaultEntryPoint = "kb_register_native_scripts";

using NativeScriptPluginLifecycleProc = void (*)(ScriptExecutionContext* context);
using NativeScriptPluginEventProc = void (*)(ScriptExecutionContext* context, const ScriptEvent* event);

struct NativeScriptPluginApi {
    std::uint32_t version = kNativeScriptPluginApiVersion;
    void* user = nullptr;
    bool (*registerLifecycle)(void* user, const char* symbol, ScriptLifecycleEvent event, NativeScriptPluginLifecycleProc callback) = nullptr;
    bool (*registerEvent)(void* user, const char* symbol, const char* eventName, NativeScriptPluginEventProc callback) = nullptr;
};

using NativeScriptPluginRegisterProc = bool (*)(NativeScriptPluginApi* api);

} // namespace kb::script

#if defined(_WIN32)
#define KB_NATIVE_SCRIPT_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define KB_NATIVE_SCRIPT_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif
