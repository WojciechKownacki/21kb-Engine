#include "engine/script/NativeScriptBackend.hpp"

#include <utility>

namespace kb::script {

std::size_t NativeScriptBackend::LifecycleKeyHasher::operator()(LifecycleKey key) const noexcept {
    return static_cast<std::size_t>(key.assetId ^ (static_cast<std::uint64_t>(key.event) + 0x9e3779b97f4a7c15ULL + (key.assetId << 6U) + (key.assetId >> 2U)));
}

std::string NativeScriptBackend::EventKey(kb::assets::AssetId assetId, std::string_view eventName) {
    return std::to_string(assetId.value) + ":" + std::string{eventName};
}

std::string NativeScriptBackend::SymbolLifecycleKey(std::string_view symbol, ScriptLifecycleEvent event) {
    return std::string{symbol} + ":" + std::to_string(static_cast<int>(event));
}

std::string NativeScriptBackend::SymbolEventKey(std::string_view symbol, std::string_view eventName) {
    return std::string{symbol} + ":" + std::string{eventName};
}

kb::scene::BehaviourBackend NativeScriptBackend::Backend() const noexcept {
    return kb::scene::BehaviourBackend::Native;
}

bool NativeScriptBackend::RegisterLifecycle(kb::assets::AssetId assetId, ScriptLifecycleEvent event, NativeScriptLifecycleCallback callback) {
    if (!assetId.IsValid() || callback == nullptr) {
        return false;
    }
    lifecycleCallbacks_[LifecycleKey{.assetId = assetId.value, .event = event}] = std::move(callback);
    return true;
}

bool NativeScriptBackend::RegisterLifecycleSymbol(std::string symbol, ScriptLifecycleEvent event, NativeScriptLifecycleCallback callback) {
    if (symbol.empty() || callback == nullptr) {
        return false;
    }
    symbolLifecycleCallbacks_[SymbolLifecycleKey(symbol, event)] = std::move(callback);
    return true;
}

bool NativeScriptBackend::RegisterEvent(kb::assets::AssetId assetId, std::string eventName, NativeScriptEventCallback callback) {
    if (!assetId.IsValid() || eventName.empty() || callback == nullptr) {
        return false;
    }
    eventCallbacks_[EventKey(assetId, eventName)] = std::move(callback);
    return true;
}

bool NativeScriptBackend::RegisterEventSymbol(std::string symbol, std::string eventName, NativeScriptEventCallback callback) {
    if (symbol.empty() || eventName.empty() || callback == nullptr) {
        return false;
    }
    symbolEventCallbacks_[SymbolEventKey(symbol, eventName)] = std::move(callback);
    return true;
}

bool NativeScriptBackend::BindAssetSymbol(kb::assets::AssetId assetId, std::string symbol) {
    if (!assetId.IsValid() || symbol.empty()) {
        return false;
    }
    assetSymbols_[assetId.value] = std::move(symbol);
    return true;
}

void NativeScriptBackend::Clear() noexcept {
    lifecycleCallbacks_.clear();
    eventCallbacks_.clear();
    symbolLifecycleCallbacks_.clear();
    symbolEventCallbacks_.clear();
    assetSymbols_.clear();
}

ScriptBackendExecutionResult NativeScriptBackend::ExecuteLifecycle(const kb::scene::BehaviourComponent& behaviour, ScriptExecutionContext& context) {
    ScriptBackendExecutionResult result{};
    const kb::assets::AssetId assetId{behaviour.behaviourAssetId};
    const auto iter = lifecycleCallbacks_.find(LifecycleKey{.assetId = assetId.value, .event = context.Lifecycle()});
    if (iter == lifecycleCallbacks_.end()) {
        const auto symbolIter = assetSymbols_.find(assetId.value);
        if (symbolIter == assetSymbols_.end()) {
            return result;
        }
        const auto symbolCallback = symbolLifecycleCallbacks_.find(SymbolLifecycleKey(symbolIter->second, context.Lifecycle()));
        if (symbolCallback == symbolLifecycleCallbacks_.end()) {
            return result;
        }
        symbolCallback->second(context);
        result.executed = true;
        return result;
    }
    iter->second(context);
    result.executed = true;
    return result;
}

ScriptBackendExecutionResult NativeScriptBackend::ExecuteEvent(const kb::scene::BehaviourComponent& behaviour, const ScriptEvent& event, ScriptExecutionContext& context) {
    ScriptBackendExecutionResult result{};
    const kb::assets::AssetId assetId{behaviour.behaviourAssetId};
    const auto iter = eventCallbacks_.find(EventKey(assetId, event.name));
    if (iter == eventCallbacks_.end()) {
        const auto symbolIter = assetSymbols_.find(assetId.value);
        if (symbolIter == assetSymbols_.end()) {
            return result;
        }
        const auto symbolCallback = symbolEventCallbacks_.find(SymbolEventKey(symbolIter->second, event.name));
        if (symbolCallback == symbolEventCallbacks_.end()) {
            return result;
        }
        symbolCallback->second(context, event);
        result.executed = true;
        return result;
    }
    iter->second(context, event);
    result.executed = true;
    return result;
}

} // namespace kb::script
