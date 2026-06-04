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
    void Clear() noexcept;

    [[nodiscard]] ScriptBackendExecutionResult ExecuteLifecycle(const kb::scene::BehaviourComponent& behaviour, ScriptExecutionContext& context) override;
    [[nodiscard]] ScriptBackendExecutionResult ExecuteEvent(const kb::scene::BehaviourComponent& behaviour, const ScriptEvent& event, ScriptExecutionContext& context) override;

private:
    struct LifecycleKey {
        std::uint64_t assetId = 0;
        ScriptLifecycleEvent event = ScriptLifecycleEvent::Tick;

        [[nodiscard]] friend constexpr bool operator==(LifecycleKey lhs, LifecycleKey rhs) noexcept = default;
    };

    struct LifecycleKeyHasher {
        [[nodiscard]] std::size_t operator()(LifecycleKey key) const noexcept;
    };

    [[nodiscard]] static std::string EventKey(kb::assets::AssetId assetId, std::string_view eventName);
    [[nodiscard]] static std::string SymbolLifecycleKey(std::string_view symbol, ScriptLifecycleEvent event);
    [[nodiscard]] static std::string SymbolEventKey(std::string_view symbol, std::string_view eventName);

    std::unordered_map<LifecycleKey, NativeScriptLifecycleCallback, LifecycleKeyHasher> lifecycleCallbacks_;
    std::unordered_map<std::string, NativeScriptEventCallback> eventCallbacks_;
    std::unordered_map<std::string, NativeScriptLifecycleCallback> symbolLifecycleCallbacks_;
    std::unordered_map<std::string, NativeScriptEventCallback> symbolEventCallbacks_;
    std::unordered_map<std::uint64_t, std::string> assetSymbols_;
};

} // namespace kb::script
