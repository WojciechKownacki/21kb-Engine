#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/script/ScriptBackend.hpp"
#include "engine/script/ScriptLifecycle.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace kb::script {

using NativeScriptLifecycleCallback = std::function<void(ScriptExecutionContext&)>;
using NativeScriptEventCallback = std::function<void(ScriptExecutionContext&, const ScriptEvent&)>;

class NativeScriptBackend final : public IScriptBackend {
public:
    [[nodiscard]] kb::scene::BehaviourBackend Backend() const noexcept override;

    [[nodiscard]] bool RegisterLifecycle(kb::assets::AssetId assetId, ScriptLifecycleEvent event, NativeScriptLifecycleCallback callback);
    [[nodiscard]] bool RegisterLifecycleSymbol(std::string symbol, ScriptLifecycleEvent event, NativeScriptLifecycleCallback callback);
    [[nodiscard]] bool RegisterEvent(kb::assets::AssetId assetId, std::string eventName, NativeScriptEventCallback callback);
    [[nodiscard]] bool RegisterEventSymbol(std::string symbol, std::string eventName, NativeScriptEventCallback callback);
    [[nodiscard]] bool BindAssetSymbol(kb::assets::AssetId assetId, std::string symbol);
    void UnregisterSymbolCallbacks(std::string_view symbol) noexcept;
    void UnregisterSymbol(std::string_view symbol) noexcept;
    void Clear() noexcept;

    [[nodiscard]] ScriptBackendExecutionResult ExecuteLifecycle(const kb::scene::BehaviourComponent& behaviour, ScriptExecutionContext& context) override;
    [[nodiscard]] ScriptBackendExecutionResult ExecuteEvent(const kb::scene::BehaviourComponent& behaviour, const ScriptEvent& event, EventId eventId, ScriptExecutionContext& context) override;

private:
    struct LifecycleKey {
        std::uint64_t assetId = 0;
        ScriptLifecycleEvent event = ScriptLifecycleEvent::Tick;

        [[nodiscard]] friend constexpr bool operator==(LifecycleKey lhs, LifecycleKey rhs) noexcept = default;
    };

    struct LifecycleKeyHasher {
        [[nodiscard]] std::size_t operator()(LifecycleKey key) const noexcept;
    };

    struct SymbolLifecycleKey {
        std::string symbol;
        ScriptLifecycleEvent event = ScriptLifecycleEvent::Tick;

        [[nodiscard]] bool operator==(const SymbolLifecycleKey& other) const noexcept {
            return symbol == other.symbol && event == other.event;
        }
    };

    struct SymbolLifecycleKeyHasher {
        [[nodiscard]] std::size_t operator()(const SymbolLifecycleKey& key) const noexcept;
    };

    // LIB-104: EventId (not the event name string) is the hot-path lookup
    // key — see ScriptEventId.hpp. The string is only ever hashed once, at
    // RegisterEvent/RegisterEventSymbol (startup registration), never on
    // the per-behaviour ExecuteEvent path.
    struct EventKey {
        std::uint64_t assetId = 0;
        EventId eventId = 0;

        [[nodiscard]] friend constexpr bool operator==(EventKey lhs, EventKey rhs) noexcept = default;
    };

    struct EventKeyHasher {
        [[nodiscard]] std::size_t operator()(EventKey key) const noexcept;
    };

    struct SymbolEventKey {
        std::string symbol;
        EventId eventId = 0;

        [[nodiscard]] bool operator==(const SymbolEventKey& other) const noexcept {
            return symbol == other.symbol && eventId == other.eventId;
        }
    };

    struct SymbolEventKeyHasher {
        [[nodiscard]] std::size_t operator()(const SymbolEventKey& key) const noexcept;
    };

    std::unordered_map<LifecycleKey, NativeScriptLifecycleCallback, LifecycleKeyHasher> lifecycleCallbacks_;
    std::unordered_map<EventKey, NativeScriptEventCallback, EventKeyHasher> eventCallbacks_;
    std::unordered_map<SymbolLifecycleKey, NativeScriptLifecycleCallback, SymbolLifecycleKeyHasher> symbolLifecycleCallbacks_;
    std::unordered_map<SymbolEventKey, NativeScriptEventCallback, SymbolEventKeyHasher> symbolEventCallbacks_;
    std::unordered_map<std::uint64_t, std::string> assetSymbols_;
};

} // namespace kb::script
