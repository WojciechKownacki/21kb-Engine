#pragma once

#include "engine/script/NativeScriptBackend.hpp"
#include "engine/script/NativeScriptPlugin.hpp"

#include <string>
#include <vector>

namespace kb::script {

class NativeScriptPluginRegistrar final {
public:
    explicit NativeScriptPluginRegistrar(NativeScriptBackend& backend) noexcept;

    [[nodiscard]] NativeScriptPluginApi CreateApi() noexcept;
    [[nodiscard]] bool RegisterLifecycle(const char* symbol, ScriptLifecycleEvent event, NativeScriptPluginLifecycleProc callback);
    [[nodiscard]] bool RegisterEvent(const char* symbol, const char* eventName, NativeScriptPluginEventProc callback);
    [[nodiscard]] const std::vector<std::string>& RegisteredSymbols() const noexcept;
    [[nodiscard]] const std::vector<std::string>& Errors() const noexcept;

private:
    static bool RegisterLifecycleThunk(void* user, const char* symbol, ScriptLifecycleEvent event, NativeScriptPluginLifecycleProc callback);
    static bool RegisterEventThunk(void* user, const char* symbol, const char* eventName, NativeScriptPluginEventProc callback);
    void TrackSymbol(std::string symbol);
    void AddError(std::string message);

    NativeScriptBackend& backend_;
    std::vector<std::string> registeredSymbols_;
    std::vector<std::string> errors_;
};

} // namespace kb::script
