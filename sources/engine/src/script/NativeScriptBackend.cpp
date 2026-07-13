#include "engine/script/NativeScriptBackend.hpp"

#include <exception>
#include <string>
#include <utility>

namespace kb::script {
namespace {

// A registered native callback can throw. DispatchSceneBehaviours
// (ScriptRuntime.cpp) walks every behaviour in a phase in one loop; letting
// an exception propagate out of here would abort every other behaviour's
// dispatch for the rest of the frame. Converting it into a diagnostic here
// keeps that loop resilient, the same way ScriptFunctionRegistry::Call does
// for function calls.
template <typename Invoke>
void InvokeNativeCallback(Invoke&& invoke, ScriptExecutionContext& context, kb::assets::AssetId assetId, ScriptBackendExecutionResult& result) {
    try {
        invoke();
        result.executed = true;
    } catch (const std::exception& exception) {
        result.diagnostics.push_back(ScriptDiagnostic{
            .entity = context.Self(),
            .assetId = assetId,
            .backend = context.Backend(),
            .lifecyclePhase = context.Lifecycle(),
            .message = std::string{ "native script callback threw an exception: " } + exception.what(),
        });
    } catch (...) {
        result.diagnostics.push_back(ScriptDiagnostic{
            .entity = context.Self(),
            .assetId = assetId,
            .backend = context.Backend(),
            .lifecyclePhase = context.Lifecycle(),
            .message = "native script callback threw a non-standard exception",
        });
    }
}

} // namespace

std::size_t NativeScriptBackend::LifecycleKeyHasher::operator()(LifecycleKey key) const noexcept {
    return static_cast<std::size_t>(key.assetId ^ (static_cast<std::uint64_t>(key.event) + 0x9e3779b97f4a7c15ULL + (key.assetId << 6U) + (key.assetId >> 2U)));
}

std::size_t NativeScriptBackend::SymbolLifecycleKeyHasher::operator()(const SymbolLifecycleKey& key) const noexcept {
    std::size_t hash = std::hash<std::string>{}(key.symbol);
    hash ^= static_cast<std::size_t>(key.event) + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

std::size_t NativeScriptBackend::SymbolEventKeyHasher::operator()(const SymbolEventKey& key) const noexcept {
    std::size_t hash = std::hash<std::string>{}(key.symbol);
    hash ^= std::hash<std::string>{}(key.eventName) + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

std::string NativeScriptBackend::EventKey(kb::assets::AssetId assetId, std::string_view eventName) {
    return std::to_string(assetId.value) + ":" + std::string{eventName};
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
    symbolLifecycleCallbacks_[SymbolLifecycleKey{ .symbol = std::move(symbol), .event = event }] = std::move(callback);
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
    symbolEventCallbacks_[SymbolEventKey{ .symbol = std::move(symbol), .eventName = std::move(eventName) }] = std::move(callback);
    return true;
}

bool NativeScriptBackend::BindAssetSymbol(kb::assets::AssetId assetId, std::string symbol) {
    if (!assetId.IsValid() || symbol.empty()) {
        return false;
    }
    assetSymbols_[assetId.value] = std::move(symbol);
    return true;
}

void NativeScriptBackend::UnregisterSymbolCallbacks(std::string_view symbol) noexcept {
    if (symbol.empty()) {
        return;
    }
    for (auto iter = symbolLifecycleCallbacks_.begin(); iter != symbolLifecycleCallbacks_.end();) {
        if (iter->first.symbol == symbol) {
            iter = symbolLifecycleCallbacks_.erase(iter);
        } else {
            ++iter;
        }
    }
    for (auto iter = symbolEventCallbacks_.begin(); iter != symbolEventCallbacks_.end();) {
        if (iter->first.symbol == symbol) {
            iter = symbolEventCallbacks_.erase(iter);
        } else {
            ++iter;
        }
    }
}

void NativeScriptBackend::UnregisterSymbol(std::string_view symbol) noexcept {
    UnregisterSymbolCallbacks(symbol);
    if (symbol.empty()) {
        return;
    }
    for (auto iter = assetSymbols_.begin(); iter != assetSymbols_.end();) {
        if (iter->second == symbol) {
            iter = assetSymbols_.erase(iter);
        } else {
            ++iter;
        }
    }
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
        const auto symbolCallback = symbolLifecycleCallbacks_.find(SymbolLifecycleKey{ .symbol = symbolIter->second, .event = context.Lifecycle() });
        if (symbolCallback == symbolLifecycleCallbacks_.end()) {
            return result;
        }
        InvokeNativeCallback([&] { symbolCallback->second(context); }, context, assetId, result);
        return result;
    }
    InvokeNativeCallback([&] { iter->second(context); }, context, assetId, result);
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
        const auto symbolCallback = symbolEventCallbacks_.find(SymbolEventKey{ .symbol = symbolIter->second, .eventName = event.name });
        if (symbolCallback == symbolEventCallbacks_.end()) {
            return result;
        }
        InvokeNativeCallback([&] { symbolCallback->second(context, event); }, context, assetId, result);
        return result;
    }
    InvokeNativeCallback([&] { iter->second(context, event); }, context, assetId, result);
    return result;
}

} // namespace kb::script
