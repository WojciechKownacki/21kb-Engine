#include "engine/script/NativeScriptBackend.hpp"

#include <algorithm>
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
void InvokeNativeCallback(
    Invoke&& invoke,
    ScriptExecutionContext& context,
    kb::assets::AssetId assetId,
    std::string_view module,
    std::string_view function,
    ScriptBackendExecutionResult& result) {
    try {
        invoke();
        result.executed = true;
    } catch (const std::exception& exception) {
        result.diagnostics.push_back(ScriptDiagnostic{
            .entity = context.Self(),
            .assetId = assetId,
            .backend = context.Backend(),
            .module = std::string{ module },
            .function = std::string{ function },
            .lifecyclePhase = context.Lifecycle(),
            .message = std::string{ "native script callback threw an exception: " } + exception.what(),
        });
    } catch (...) {
        result.diagnostics.push_back(ScriptDiagnostic{
            .entity = context.Self(),
            .assetId = assetId,
            .backend = context.Backend(),
            .module = std::string{ module },
            .function = std::string{ function },
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

std::size_t NativeScriptBackend::EventKeyHasher::operator()(EventKey key) const noexcept {
    return static_cast<std::size_t>(key.assetId ^ (key.eventId + 0x9e3779b97f4a7c15ULL + (key.assetId << 6U) + (key.assetId >> 2U)));
}

std::size_t NativeScriptBackend::SymbolEventKeyHasher::operator()(const SymbolEventKey& key) const noexcept {
    std::size_t hash = std::hash<std::string>{}(key.symbol);
    hash ^= static_cast<std::size_t>(key.eventId) + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

NativeScriptBackend::~NativeScriptBackend() {
    Clear();
}

kb::scene::BehaviourBackend NativeScriptBackend::Backend() const noexcept {
    return kb::scene::BehaviourBackend::Native;
}

bool NativeScriptBackend::RegisterLifecycle(kb::assets::AssetId assetId, ScriptLifecycleEvent event, NativeScriptLifecycleCallback callback) {
    if (!assetId.IsValid() || callback == nullptr) {
        return false;
    }
    lifecycleCallbacks_[LifecycleKey{.assetId = assetId.value, .event = event}] = std::move(callback);
    ++epoch_;
    return true;
}

bool NativeScriptBackend::RegisterLifecycleSymbol(std::string symbol, ScriptLifecycleEvent event, NativeScriptLifecycleCallback callback) {
    if (symbol.empty() || callback == nullptr) {
        return false;
    }
    symbolLifecycleCallbacks_[SymbolLifecycleKey{ .symbol = std::move(symbol), .event = event }] = std::move(callback);
    ++epoch_;
    return true;
}

bool NativeScriptBackend::RegisterEvent(kb::assets::AssetId assetId, std::string eventName, NativeScriptEventCallback callback) {
    if (!assetId.IsValid() || eventName.empty() || callback == nullptr) {
        return false;
    }
    eventCallbacks_[EventKey{ .assetId = assetId.value, .eventId = ComputeEventId(eventName) }] = std::move(callback);
    ++epoch_;
    return true;
}

bool NativeScriptBackend::RegisterEventSymbol(std::string symbol, std::string eventName, NativeScriptEventCallback callback) {
    if (symbol.empty() || eventName.empty() || callback == nullptr) {
        return false;
    }
    symbolEventCallbacks_[SymbolEventKey{ .symbol = std::move(symbol), .eventId = ComputeEventId(eventName) }] = std::move(callback);
    ++epoch_;
    return true;
}

bool NativeScriptBackend::RegisterClassSymbol(std::string symbol, const NativeScriptClassApi& script) {
    if (symbol.empty() || script.create == nullptr || script.destroy == nullptr || script.call == nullptr || script.hooks == 0U) {
        return false;
    }
    classes_[std::move(symbol)] = script;
    ++epoch_;
    return true;
}

bool NativeScriptBackend::BindAssetSymbol(kb::assets::AssetId assetId, std::string symbol) {
    if (!assetId.IsValid() || symbol.empty()) {
        return false;
    }
    assetSymbols_[assetId.value] = std::move(symbol);
    ++epoch_;
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
    // The objects of the class stay: each carries the functions of the code that made it.
    classes_.erase(std::string{ symbol });
    ++epoch_;
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

void NativeScriptBackend::DestroyInstance(Instance& instance) noexcept {
    if (instance.object == nullptr) {
        return;
    }
    try {
        instance.script.destroy(instance.object);
    } catch (...) {
        // A destructor that throws has nobody to tell: the object is gone either way.
    }
    instance.object = nullptr;
}

void NativeScriptBackend::Clear() noexcept {
    for (auto& [assetId, entry] : entries_) {
        static_cast<void>(assetId);
        for (auto& [entity, instance] : entry.instances) {
            static_cast<void>(entity);
            DestroyInstance(instance);
        }
    }
    entries_.clear();
    savedInstances_.clear();
    lifecycleCallbacks_.clear();
    eventCallbacks_.clear();
    symbolLifecycleCallbacks_.clear();
    symbolEventCallbacks_.clear();
    classes_.clear();
    assetSymbols_.clear();
    ++epoch_;
}

void NativeScriptBackend::ReleaseInstances(std::span<const std::string> symbols) {
    savedInstances_.clear();
    for (auto& [assetId, entry] : entries_) {
        for (auto iter = entry.instances.begin(); iter != entry.instances.end();) {
            Instance& instance = iter->second;
            if (std::ranges::find(symbols, instance.symbol) == symbols.end()) {
                ++iter;
                continue;
            }
            SavedInstance saved{ .assetId = assetId, .entity = iter->first, .symbol = instance.symbol, .fields = {} };
            if (instance.script.fields != nullptr && instance.object != nullptr) {
                NativeScriptArchive archive{ saved.fields, false };
                try {
                    instance.script.fields(instance.object, &archive);
                } catch (...) {
                    saved.fields.clear();
                }
            }
            DestroyInstance(instance);
            savedInstances_.push_back(std::move(saved));
            iter = entry.instances.erase(iter);
        }
    }
}

void NativeScriptBackend::RestoreInstances() {
    std::vector<SavedInstance> saved;
    saved.swap(savedInstances_);
    for (SavedInstance& item : saved) {
        const auto script = classes_.find(item.symbol);
        if (script == classes_.end()) {
            continue;
        }
        Instance instance{ .object = nullptr, .script = script->second, .symbol = item.symbol };
        try {
            instance.object = instance.script.create();
            if (instance.script.fields != nullptr && !item.fields.empty()) {
                NativeScriptArchive archive{ item.fields, true };
                instance.script.fields(instance.object, &archive);
            }
        } catch (...) {
            DestroyInstance(instance);
            continue;
        }
        entries_[item.assetId].instances[item.entity] = std::move(instance);
    }
}

void NativeScriptBackend::DestroyInstances(std::span<const std::string> symbols) noexcept {
    for (auto& [assetId, entry] : entries_) {
        static_cast<void>(assetId);
        for (auto iter = entry.instances.begin(); iter != entry.instances.end();) {
            if (std::ranges::find(symbols, iter->second.symbol) == symbols.end()) {
                ++iter;
                continue;
            }
            DestroyInstance(iter->second);
            iter = entry.instances.erase(iter);
        }
    }
}

std::size_t NativeScriptBackend::InstanceCount(std::string_view symbol) const noexcept {
    std::size_t count = 0U;
    for (const auto& [assetId, entry] : entries_) {
        static_cast<void>(assetId);
        for (const auto& [entity, instance] : entry.instances) {
            static_cast<void>(entity);
            count += instance.symbol == symbol ? 1U : 0U;
        }
    }
    return count;
}

NativeScriptBackend::AssetEntry& NativeScriptBackend::Resolve(std::uint64_t assetId) {
    AssetEntry& entry = entries_[assetId];
    if (entry.epoch == epoch_) {
        return entry;
    }
    entry.epoch = epoch_;
    entry.symbol.clear();
    entry.lifecycle.fill(nullptr);
    entry.events.clear();
    entry.script = nullptr;
    if (const auto symbol = assetSymbols_.find(assetId); symbol != assetSymbols_.end()) {
        entry.symbol = symbol->second;
    }
    for (std::size_t index = 0U; index < kLifecycleCount; ++index) {
        const auto event = static_cast<ScriptLifecycleEvent>(index);
        if (const auto bound = lifecycleCallbacks_.find(LifecycleKey{ .assetId = assetId, .event = event }); bound != lifecycleCallbacks_.end()) {
            entry.lifecycle[index] = &bound->second;
        } else if (!entry.symbol.empty()) {
            if (const auto named = symbolLifecycleCallbacks_.find(SymbolLifecycleKey{ .symbol = entry.symbol, .event = event }); named != symbolLifecycleCallbacks_.end()) {
                entry.lifecycle[index] = &named->second;
            }
        }
    }
    for (const auto& [key, callback] : eventCallbacks_) {
        if (key.assetId == assetId) {
            entry.events.emplace_back(key.eventId, &callback);
        }
    }
    if (!entry.symbol.empty()) {
        for (const auto& [key, callback] : symbolEventCallbacks_) {
            if (key.symbol == entry.symbol && std::ranges::none_of(entry.events, [&key](const auto& known) { return known.first == key.eventId; })) {
                entry.events.emplace_back(key.eventId, &callback);
            }
        }
        if (const auto script = classes_.find(entry.symbol); script != classes_.end()) {
            entry.script = &script->second;
        }
    }
    return entry;
}

void NativeScriptBackend::ExecuteClassLifecycle(
    AssetEntry& entry,
    std::uint64_t assetId,
    ScriptExecutionContext& context,
    ScriptBackendExecutionResult& result) {
    const ScriptLifecycleEvent event = context.Lifecycle();
    const std::uint64_t entity = context.Self().Id();
    const std::string_view function = ToString(event);
    auto found = entry.instances.find(entity);
    if (event == ScriptLifecycleEvent::Created && found != entry.instances.end()) {
        // The behaviour starts over: nothing an earlier start left is carried into it.
        DestroyInstance(found->second);
        entry.instances.erase(found);
        found = entry.instances.end();
    }
    if (found == entry.instances.end()) {
        if (event == ScriptLifecycleEvent::Destroyed) {
            return;
        }
        Instance instance{ .object = nullptr, .script = *entry.script, .symbol = entry.symbol };
        InvokeNativeCallback([&] { instance.object = instance.script.create(); }, context, kb::assets::AssetId{ assetId }, entry.symbol, "constructor", result);
        if (instance.object == nullptr) {
            return;
        }
        found = entry.instances.emplace(entity, std::move(instance)).first;
    }
    Instance& instance = found->second;
    if ((instance.script.hooks & (1U << static_cast<unsigned>(event))) != 0U) {
        InvokeNativeCallback([&] { instance.script.call(instance.object, event, &context); }, context, kb::assets::AssetId{ assetId }, entry.symbol, function, result);
    }
    if (event == ScriptLifecycleEvent::Destroyed) {
        // Destroyed is the last call: the object goes with the behaviour.
        DestroyInstance(found->second);
        entry.instances.erase(entity);
    }
}

ScriptBackendExecutionResult NativeScriptBackend::ExecuteLifecycle(const kb::scene::BehaviourComponent& behaviour, ScriptExecutionContext& context) {
    ScriptBackendExecutionResult result{};
    const kb::assets::AssetId assetId{behaviour.behaviourAssetId};
    AssetEntry& entry = Resolve(assetId.value);
    if (entry.script != nullptr) {
        ExecuteClassLifecycle(entry, assetId.value, context, result);
        return result;
    }
    const NativeScriptLifecycleCallback* const callback = entry.lifecycle[static_cast<std::size_t>(context.Lifecycle())];
    if (callback == nullptr) {
        return result;
    }
    const std::string_view module = entry.symbol.empty() ? "Native" : std::string_view{ entry.symbol };
    InvokeNativeCallback([&] { (*callback)(context); }, context, assetId, module, ToString(context.Lifecycle()), result);
    return result;
}

ScriptBackendExecutionResult NativeScriptBackend::ExecuteEvent(const kb::scene::BehaviourComponent& behaviour, const ScriptEvent& event, EventId eventId, ScriptExecutionContext& context) {
    ScriptBackendExecutionResult result{};
    const kb::assets::AssetId assetId{behaviour.behaviourAssetId};
    AssetEntry& entry = Resolve(assetId.value);
    const auto handler = std::ranges::find_if(entry.events, [eventId](const auto& known) { return known.first == eventId; });
    if (handler == entry.events.end()) {
        return result;
    }
    const std::string_view module = entry.symbol.empty() ? "Native" : std::string_view{ entry.symbol };
    InvokeNativeCallback([&] { (*handler->second)(context, event); }, context, assetId, module, event.name, result);
    return result;
}

bool NativeScriptBackend::HasEntry(const kb::scene::BehaviourComponent& behaviour, std::string_view name) {
    const AssetEntry& entry = Resolve(behaviour.behaviourAssetId);
    for (std::size_t index = 0U; index < kLifecycleCount; ++index) {
        if (name == ToString(static_cast<ScriptLifecycleEvent>(index))) {
            return entry.lifecycle[index] != nullptr || (entry.script != nullptr && (entry.script->hooks & (1U << index)) != 0U);
        }
    }
    const EventId eventId = ComputeEventId(name);
    return std::ranges::any_of(entry.events, [eventId](const auto& known) { return known.first == eventId; });
}

} // namespace kb::script
