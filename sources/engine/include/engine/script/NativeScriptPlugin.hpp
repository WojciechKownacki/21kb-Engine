#pragma once

#include "engine/script/ScriptEvent.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/script/ScriptLifecycle.hpp"

#include <cstdint>

namespace kb::script {

inline constexpr std::uint32_t kNativeScriptPluginApiVersion = 6U;
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

// Registers the lifecycle hooks a script class defines, the way a Lua script's functions are found by
// name: a static member called Created, Activated, Ready, FixedTick, Tick, LateTick, BeforeRender,
// AfterRender, Deactivated or Destroyed that takes a ScriptExecutionContext* is called at that moment
// for every object the script is attached to. A class that defines none is refused.
template <typename Script>
[[nodiscard]] bool RegisterNativeScript(NativeScriptPluginApi* api, const char* symbol) {
    if (api == nullptr || api->registerLifecycle == nullptr || symbol == nullptr || symbol[0] == '\0') {
        return false;
    }
    bool registered = false;
    bool succeeded = true;
    const auto add = [&](ScriptLifecycleEvent event, NativeScriptPluginLifecycleProc callback) {
        registered = true;
        succeeded = api->registerLifecycle(api->user, symbol, event, callback) && succeeded;
    };
    ScriptExecutionContext* const probe = nullptr;
    if constexpr (requires { Script::Created(probe); }) add(ScriptLifecycleEvent::Created, &Script::Created);
    if constexpr (requires { Script::Activated(probe); }) add(ScriptLifecycleEvent::Activated, &Script::Activated);
    if constexpr (requires { Script::Ready(probe); }) add(ScriptLifecycleEvent::Ready, &Script::Ready);
    if constexpr (requires { Script::FixedTick(probe); }) add(ScriptLifecycleEvent::FixedTick, &Script::FixedTick);
    if constexpr (requires { Script::Tick(probe); }) add(ScriptLifecycleEvent::Tick, &Script::Tick);
    if constexpr (requires { Script::LateTick(probe); }) add(ScriptLifecycleEvent::LateTick, &Script::LateTick);
    if constexpr (requires { Script::BeforeRender(probe); }) add(ScriptLifecycleEvent::BeforeRender, &Script::BeforeRender);
    if constexpr (requires { Script::AfterRender(probe); }) add(ScriptLifecycleEvent::AfterRender, &Script::AfterRender);
    if constexpr (requires { Script::Deactivated(probe); }) add(ScriptLifecycleEvent::Deactivated, &Script::Deactivated);
    if constexpr (requires { Script::Destroyed(probe); }) add(ScriptLifecycleEvent::Destroyed, &Script::Destroyed);
    return registered && succeeded;
}

} // namespace kb::script

#if defined(_WIN32)
#define KB_NATIVE_SCRIPT_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define KB_NATIVE_SCRIPT_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif
