#include "engine/script/NativeScriptPluginRegistrar.hpp"

#include <algorithm>
#include <utility>

namespace kb::script {

NativeScriptPluginRegistrar::NativeScriptPluginRegistrar(NativeScriptBackend& backend) noexcept
    : backend_(backend) {}

NativeScriptPluginApi NativeScriptPluginRegistrar::CreateApi() noexcept {
    return NativeScriptPluginApi{
        .version = kNativeScriptPluginApiVersion,
        .user = this,
        .registerLifecycle = &NativeScriptPluginRegistrar::RegisterLifecycleThunk,
        .registerEvent = &NativeScriptPluginRegistrar::RegisterEventThunk,
    };
}

bool NativeScriptPluginRegistrar::RegisterLifecycle(const char* symbol, ScriptLifecycleEvent event, NativeScriptPluginLifecycleProc callback) {
    if (symbol == nullptr || symbol[0] == '\0' || callback == nullptr) {
        AddError("native script plugin lifecycle registration is invalid");
        return false;
    }
    const std::string symbolName{ symbol };
    if (!backend_.RegisterLifecycleSymbol(symbolName, event, [callback](ScriptExecutionContext& context) {
            callback(&context);
        })) {
        AddError("native script plugin lifecycle callback could not be registered");
        return false;
    }
    TrackSymbol(symbolName);
    return true;
}

bool NativeScriptPluginRegistrar::RegisterEvent(const char* symbol, const char* eventName, NativeScriptPluginEventProc callback) {
    if (symbol == nullptr || symbol[0] == '\0' || eventName == nullptr || eventName[0] == '\0' || callback == nullptr) {
        AddError("native script plugin event registration is invalid");
        return false;
    }
    const std::string symbolName{ symbol };
    if (!backend_.RegisterEventSymbol(symbolName, eventName, [callback](ScriptExecutionContext& context, const ScriptEvent& event) {
            callback(&context, &event);
        })) {
        AddError("native script plugin event callback could not be registered");
        return false;
    }
    TrackSymbol(symbolName);
    return true;
}

const std::vector<std::string>& NativeScriptPluginRegistrar::RegisteredSymbols() const noexcept {
    return registeredSymbols_;
}

const std::vector<std::string>& NativeScriptPluginRegistrar::Errors() const noexcept {
    return errors_;
}

bool NativeScriptPluginRegistrar::RegisterLifecycleThunk(void* user, const char* symbol, ScriptLifecycleEvent event, NativeScriptPluginLifecycleProc callback) {
    auto* registrar = static_cast<NativeScriptPluginRegistrar*>(user);
    return registrar != nullptr && registrar->RegisterLifecycle(symbol, event, callback);
}

bool NativeScriptPluginRegistrar::RegisterEventThunk(void* user, const char* symbol, const char* eventName, NativeScriptPluginEventProc callback) {
    auto* registrar = static_cast<NativeScriptPluginRegistrar*>(user);
    return registrar != nullptr && registrar->RegisterEvent(symbol, eventName, callback);
}

void NativeScriptPluginRegistrar::TrackSymbol(std::string symbol) {
    if (std::ranges::find(registeredSymbols_, symbol) == registeredSymbols_.end()) {
        registeredSymbols_.push_back(std::move(symbol));
    }
}

void NativeScriptPluginRegistrar::AddError(std::string message) {
    errors_.push_back(std::move(message));
}

} // namespace kb::script
