#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/script/NativeScriptPlugin.hpp"
#include "engine/script/ScriptBackend.hpp"
#include "engine/script/ScriptEventId.hpp"
#include "engine/script/ScriptLifecycle.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kb::script {

using NativeScriptLifecycleCallback = std::function<void(ScriptExecutionContext&)>;
using NativeScriptEventCallback = std::function<void(ScriptExecutionContext&, const ScriptEvent&)>;

class NativeScriptBackend final : public IScriptBackend {
public:
    NativeScriptBackend() = default;
    ~NativeScriptBackend() override;
    NativeScriptBackend(const NativeScriptBackend&) = delete;
    NativeScriptBackend& operator=(const NativeScriptBackend&) = delete;

    [[nodiscard]] kb::scene::BehaviourBackend Backend() const noexcept override;

    [[nodiscard]] bool RegisterLifecycle(kb::assets::AssetId assetId, ScriptLifecycleEvent event, NativeScriptLifecycleCallback callback);
    [[nodiscard]] bool RegisterLifecycleSymbol(std::string symbol, ScriptLifecycleEvent event, NativeScriptLifecycleCallback callback);
    [[nodiscard]] bool RegisterEvent(kb::assets::AssetId assetId, std::string eventName, NativeScriptEventCallback callback);
    [[nodiscard]] bool RegisterEventSymbol(std::string symbol, std::string eventName, NativeScriptEventCallback callback);
    // A script class with per-object state (NativeScriptClassApi): an object of it is made when the first hook
    // of a behaviour runs, kept until its Destroyed hook has run, and carried over a module reload.
    [[nodiscard]] bool RegisterClassSymbol(std::string symbol, const NativeScriptClassApi& script);
    [[nodiscard]] bool BindAssetSymbol(kb::assets::AssetId assetId, std::string symbol);
    void UnregisterSymbolCallbacks(std::string_view symbol) noexcept;
    void UnregisterSymbol(std::string_view symbol) noexcept;
    void Clear() noexcept;

    // Module reload: the objects of the classes of `symbols` are saved (their Serialize member) and destroyed by the code
    // that made them - call this while that module is still loaded - and RestoreInstances makes them again with
    // the classes now registered and puts the saved values back. Objects whose class is no longer there are dropped.
    void ReleaseInstances(std::span<const std::string> symbols);
    void RestoreInstances();
    // Destroys the objects of the classes of `symbols` without keeping them (the module goes away).
    void DestroyInstances(std::span<const std::string> symbols) noexcept;
    [[nodiscard]] std::size_t InstanceCount(std::string_view symbol) const noexcept;
    // The registered class has data members but lists none in Serialize, so a rebuilt module starts them over.
    [[nodiscard]] bool ClassLosesStateOnReload(std::string_view symbol) const;

    [[nodiscard]] ScriptBackendExecutionResult ExecuteLifecycle(const kb::scene::BehaviourComponent& behaviour, ScriptExecutionContext& context) override;
    [[nodiscard]] ScriptBackendExecutionResult ExecuteEvent(const kb::scene::BehaviourComponent& behaviour, const ScriptEvent& event, EventId eventId, ScriptExecutionContext& context) override;
    [[nodiscard]] bool HasEntry(const kb::scene::BehaviourComponent& behaviour, std::string_view name) override;
    // The object of a class script, or the registered function, is what the call runs: no lookup is left for the frame.
    [[nodiscard]] ScriptPhaseCall ResolvePhaseCall(
        kb::scene::SceneEntity entity,
        const kb::scene::BehaviourComponent& behaviour,
        ScriptLifecycleEvent event) override;
    [[nodiscard]] std::uint64_t PhaseCallRevision() const noexcept override;

private:
    static constexpr std::size_t kLifecycleCount = static_cast<std::size_t>(ScriptLifecycleEvent::Destroyed) + 1U;

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

    // One object of a script class; it keeps the class's functions it was made with, so it can be
    // destroyed by them even after the class is registered again by a newer module.
    struct Instance {
        void* object = nullptr;
        NativeScriptClassApi script;
        std::string symbol;
    };

    // A registered lifecycle function with the name its failures are reported under; the target of a resolved phase call.
    struct CallbackTarget {
        const NativeScriptLifecycleCallback* callback = nullptr;
        const std::string* symbol = nullptr;
    };

    // What a behaviour asset runs, found once: the callbacks by lifecycle event and by event id, and the
    // class, instead of once per call from the registration tables (which hash the symbol text each time).
    // It is looked at again when a registration changed (epoch_). The objects are kept across that.
    struct AssetEntry {
        std::uint64_t epoch = 0;
        std::string symbol;
        std::array<const NativeScriptLifecycleCallback*, kLifecycleCount> lifecycle{};
        std::array<CallbackTarget, kLifecycleCount> lifecycleTargets{};
        std::vector<std::pair<EventId, const NativeScriptEventCallback*>> events;
        const NativeScriptClassApi* script = nullptr;
        std::unordered_map<std::uint64_t, Instance> instances;
    };

    struct SavedInstance {
        std::uint64_t assetId = 0;
        std::uint64_t entity = 0;
        std::string symbol;
        std::vector<NativeScriptSavedField> fields;
    };

    [[nodiscard]] AssetEntry& Resolve(std::uint64_t assetId);
    [[nodiscard]] static bool InvokeClassHook(void* target, const kb::scene::BehaviourComponent& behaviour, ScriptExecutionContext& context, std::vector<ScriptDiagnostic>& diagnostics);
    [[nodiscard]] static bool InvokeLifecycleCallback(void* target, const kb::scene::BehaviourComponent& behaviour, ScriptExecutionContext& context, std::vector<ScriptDiagnostic>& diagnostics);
    void ExecuteClassLifecycle(AssetEntry& entry, std::uint64_t assetId, ScriptExecutionContext& context, ScriptBackendExecutionResult& result);
    static void DestroyInstance(Instance& instance) noexcept;

    std::unordered_map<LifecycleKey, NativeScriptLifecycleCallback, LifecycleKeyHasher> lifecycleCallbacks_;
    std::unordered_map<EventKey, NativeScriptEventCallback, EventKeyHasher> eventCallbacks_;
    std::unordered_map<SymbolLifecycleKey, NativeScriptLifecycleCallback, SymbolLifecycleKeyHasher> symbolLifecycleCallbacks_;
    std::unordered_map<SymbolEventKey, NativeScriptEventCallback, SymbolEventKeyHasher> symbolEventCallbacks_;
    std::unordered_map<std::string, NativeScriptClassApi> classes_;
    std::unordered_map<std::uint64_t, std::string> assetSymbols_;
    std::unordered_map<std::uint64_t, AssetEntry> entries_;
    std::vector<SavedInstance> savedInstances_;
    std::uint64_t epoch_ = 1;
    // Every object made or destroyed; with epoch_ it is what PhaseCallRevision reports.
    std::uint64_t instanceChanges_ = 0;
};

} // namespace kb::script
