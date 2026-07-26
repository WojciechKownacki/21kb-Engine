#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/script/ScriptLifecycle.hpp"
#include "engine/script/ScriptValue.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::script {

struct ScriptFunctionPin {
    std::string name;
    ScriptValueType type = ScriptValueType::Void;
    bool required = true;
};

struct ScriptFunctionArgument {
    std::string name;
    ScriptValue value;
};

struct ScriptFunctionSignature {
    std::string name;
    // Required user-facing semantics for the callable. ScriptRuntimeHost
    // fills this from the built-in documentation catalog for engine-owned
    // functions; plugins must author it explicitly. It is retained by the
    // live registry so every frontend/export reads the same description.
    std::string description;
    std::vector<ScriptFunctionPin> inputs;
    std::vector<ScriptFunctionPin> outputs;
    // LIB-025: empty when the function is not deprecated. Set via
    // ScriptFunctionRegistry::MarkDeprecated (never at Register() time —
    // deprecation is authored as kb::library::LibraryFunctionDesc::
    // deprecation, a higher-level concept kb::script must not depend on;
    // kb::library applies it to the already-registered signature after the
    // fact instead). A plain string, not a richer type, so this stays a
    // kb::script-only concern: Call() below only needs to know THAT there
    // is a warning to surface, not why.
    std::string deprecationMessage;
};

struct ScriptFunctionCallContext {
    kb::scene::Scene* scene = nullptr;
    kb::scene::SceneEntity caller{};
    kb::assets::AssetId callerAsset{};
    kb::scene::BehaviourBackend callerBackend = kb::scene::BehaviourBackend::Native;
    ScriptLifecycleEvent lifecycle = ScriptLifecycleEvent::Tick;
    float deltaSeconds = 0.0F;
};

struct ScriptFunctionCallResult {
    bool executed = false;
    std::vector<ScriptFunctionArgument> outputs;
    std::vector<std::string> errors;
    // LIB-025: populated by Call() below when the invoked function's
    // signature carries a deprecationMessage — present on ANY actual
    // invocation attempt (successful or one that later fails output
    // validation), never on a call rejected before it ran (unknown
    // function, input type mismatch, reentrancy limit). A warning, not an
    // error: it never affects Succeeded().
    std::vector<std::string> warnings;

    [[nodiscard]] bool Succeeded() const noexcept {
        return errors.empty();
    }

    [[nodiscard]] std::optional<ScriptValue> Output(std::string_view name) const;
};

using ScriptFunctionCallback = std::function<ScriptFunctionCallResult(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument>)>;

struct ScriptFunctionDesc {
    ScriptFunctionSignature signature;
    ScriptFunctionCallback callback;
};

class ScriptFunctionRegistry final {
public:
    // Rejects the registration (returns false) once Lock() has been
    // called. A function registered after the world has started running
    // could be visible to some already-running dispatch paths but not
    // others (Lua sugar tables and compiled Visual Graph bindings are
    // generated/snapshotted at setup time, not re-derived per call), so
    // LIB-021 blocks it outright rather than allowing a partially-visible
    // function.
    [[nodiscard]] bool Register(ScriptFunctionDesc function);
    [[nodiscard]] const ScriptFunctionSignature* FindSignature(std::string_view name) const noexcept;
    [[nodiscard]] const std::vector<ScriptFunctionDesc>& Functions() const noexcept;
    // LIB-025: sets an already-registered function's deprecation warning
    // message (Call() surfaces it via ScriptFunctionCallResult::warnings on
    // every real invocation). Returns false if `name` is not registered.
    // Deliberately independent of Lock(): kb::library applies this once, at
    // ScriptRuntimeHost setup right after EngineLibraryModule::Install()
    // registers the module that owns `name`, which is itself before any
    // dispatch — but this is a signature-metadata update, not a new
    // function becoming callable, so it does not need Lock()'s "visible to
    // every already-snapshotted dispatch path" guarantee.
    [[nodiscard]] bool MarkDeprecated(std::string_view name, std::string message) noexcept;
    [[nodiscard]] ScriptFunctionCallResult Call(
        std::string_view name,
        std::span<const ScriptFunctionArgument> arguments,
        const ScriptFunctionCallContext& context) const;

    // Called once the owning ScriptRuntime dispatches its first lifecycle
    // phase or event (see ScriptRuntime::ExecuteLifecycle/
    // ExecuteLifecycleForBehaviour/DispatchEvent) — idempotent, safe to
    // call every dispatch.
    void Lock() noexcept;
    [[nodiscard]] bool IsLocked() const noexcept;

private:
    [[nodiscard]] static bool HasValidPins(const std::vector<ScriptFunctionPin>& pins);
    [[nodiscard]] static const ScriptFunctionPin* FindPin(std::span<const ScriptFunctionPin> pins, std::string_view name) noexcept;
    [[nodiscard]] static const ScriptFunctionArgument* FindArgument(std::span<const ScriptFunctionArgument> arguments, std::string_view name) noexcept;
    [[nodiscard]] static ScriptFunctionArgument CoerceArgument(const ScriptFunctionArgument& argument, ScriptValueType expectedType);
    [[nodiscard]] static bool IsCompatible(ScriptValue value, ScriptValueType expectedType) noexcept;
    static void ValidateInputs(
        const ScriptFunctionSignature& signature,
        std::span<const ScriptFunctionArgument> arguments,
        std::vector<ScriptFunctionArgument>& normalized,
        std::vector<std::string>& errors);
    static void ValidateOutputs(
        const ScriptFunctionSignature& signature,
        const ScriptFunctionCallResult& result,
        std::vector<std::string>& errors);

    std::vector<ScriptFunctionDesc> functions_;
    bool locked_ = false;
    // LIB-038: reentrancy guard. A callback that (directly, or through a
    // chain of other functions) calls back into Call() on the same
    // registry increments this; past kMaxCallDepth (defined in the .cpp),
    // Call() rejects the call with a diagnostic instead of recursing until
    // the stack overflows. mutable because Call() is logically const (it
    // does not mutate the registered function set) but must track depth
    // across the possibly-reentrant call it makes into a callback.
    mutable std::size_t callDepth_ = 0;
};

} // namespace kb::script
