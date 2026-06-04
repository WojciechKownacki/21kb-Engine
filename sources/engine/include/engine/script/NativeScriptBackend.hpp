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
    [[nodiscard]] bool RegisterEvent(kb::assets::AssetId assetId, std::string eventName, NativeScriptEventCallback callback);
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

    std::unordered_map<LifecycleKey, NativeScriptLifecycleCallback, LifecycleKeyHasher> lifecycleCallbacks_;
    std::unordered_map<std::string, NativeScriptEventCallback> eventCallbacks_;
};

} // namespace kb::script
