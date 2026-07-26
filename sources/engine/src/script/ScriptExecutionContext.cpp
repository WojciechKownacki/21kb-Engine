#include "engine/script/ScriptExecutionContext.hpp"

#include "engine/input/InputSubsystem.hpp"
#include "engine/scene/Scene.hpp"

#include <array>
#include <utility>

namespace kb::script {

ScriptExecutionContext::ScriptExecutionContext(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity self,
    kb::assets::AssetId assetId,
    kb::scene::BehaviourBackend backend,
    ScriptLifecycleEvent lifecycle,
    float deltaSeconds,
    std::vector<ScriptEvent>* emittedEvents,
    const ScriptEvent* incomingEvent,
    ScriptSharedState* sharedState,
    ScriptFunctionRegistry* functions,
    ScriptEventBus* events) noexcept
    : scene_(scene)
    , self_(self)
    , assetId_(assetId)
    , backend_(backend)
    , lifecycle_(lifecycle)
    , deltaSeconds_(deltaSeconds)
    , emittedEvents_(emittedEvents)
    , incomingEvent_(incomingEvent)
    , sharedState_(sharedState)
    , functions_(functions)
    , events_(events) {}

kb::scene::Scene& ScriptExecutionContext::GetScene() noexcept {
    return scene_;
}

const kb::scene::Scene& ScriptExecutionContext::GetScene() const noexcept {
    return scene_;
}

kb::input::InputSubsystem& ScriptExecutionContext::Input() noexcept {
    return scene_.Input();
}

const kb::input::InputSubsystem& ScriptExecutionContext::Input() const noexcept {
    return scene_.Input();
}

kb::scene::SceneEntity ScriptExecutionContext::Self() const noexcept {
    return self_;
}

kb::assets::AssetId ScriptExecutionContext::Asset() const noexcept {
    return assetId_;
}

kb::scene::BehaviourBackend ScriptExecutionContext::Backend() const noexcept {
    return backend_;
}

ScriptLifecycleEvent ScriptExecutionContext::Lifecycle() const noexcept {
    return lifecycle_;
}

float ScriptExecutionContext::DeltaSeconds() const noexcept {
    return deltaSeconds_;
}

const ScriptEvent* ScriptExecutionContext::IncomingEvent() const noexcept {
    return incomingEvent_;
}

std::span<const ScriptEventArgument> ScriptExecutionContext::EventArguments() const noexcept {
    return incomingEvent_ == nullptr ? std::span<const ScriptEventArgument>{} : std::span<const ScriptEventArgument>{incomingEvent_->arguments};
}

ScriptSharedState* ScriptExecutionContext::SharedState() noexcept {
    return sharedState_;
}

const ScriptSharedState* ScriptExecutionContext::SharedState() const noexcept {
    return sharedState_;
}

bool ScriptExecutionContext::SetSharedValue(std::string key, ScriptValue value) {
    return sharedState_ != nullptr && sharedState_->Set(std::move(key), std::move(value));
}

bool ScriptExecutionContext::HasSharedValue(std::string_view key) const {
    return sharedState_ != nullptr && sharedState_->Has(key);
}

std::optional<ScriptValue> ScriptExecutionContext::GetSharedValue(std::string_view key) const {
    return sharedState_ == nullptr ? std::nullopt : sharedState_->Get(key);
}

bool ScriptExecutionContext::RemoveSharedValue(std::string_view key) {
    return sharedState_ != nullptr && sharedState_->Remove(key);
}

ScriptFunctionRegistry* ScriptExecutionContext::Functions() noexcept {
    return functions_;
}

const ScriptFunctionRegistry* ScriptExecutionContext::Functions() const noexcept {
    return functions_;
}

ScriptEventBus* ScriptExecutionContext::Events() noexcept {
    return events_;
}

const ScriptEventBus* ScriptExecutionContext::Events() const noexcept {
    return events_;
}

ScriptFunctionCallResult ScriptExecutionContext::CallFunction(std::string_view name, std::span<const ScriptFunctionArgument> arguments) {
    if (functions_ == nullptr) {
        return ScriptFunctionCallResult{
            .errors = {"script function registry is not available"},
        };
    }
    const ScriptFunctionCallContext context{
        .scene = &scene_,
        .caller = self_,
        .callerAsset = assetId_,
        .callerBackend = backend_,
        .lifecycle = lifecycle_,
        .deltaSeconds = deltaSeconds_,
    };
    ScriptFunctionCallResult result = functions_->Call(name, arguments, context);
    // LIB-025: this one method is the real, universal choke point every
    // calling path shares — the generic Lua CallFunction global, EVERY
    // individual Lua sugar wrapper (LuaWorldExists, LuaAudioPlay, ... —
    // they all delegate to this same context method rather than calling
    // ScriptFunctionRegistry::Call directly), Native backend calls, and
    // Visual Graph's generated CallNative code. Surfacing a deprecation
    // warning here, instead of in every individual wrapper, is what
    // actually makes it reach Lua and C++ callers alike without a
    // sprawling, easy-to-miss edit in dozens of places. Routed through the
    // SAME "Log" channel every script backend already observes — a no-op
    // when nothing has registered "Log" (headless hosts), exactly like
    // Log(...) itself (PucLuaFunctionApi.cpp's LuaLog). Calls functions_->
    // Call directly (not this->CallFunction) so a hypothetical deprecated
    // "Log" could never recurse into warning about itself.
    for (const std::string& warning : result.warnings) {
        static_cast<void>(functions_->Call(
            "Log", std::array{ ScriptFunctionArgument{ .name = "message", .value = ScriptValue{ warning } } }, context));
    }
    return result;
}

void ScriptExecutionContext::Emit(std::string eventName) {
    Emit(std::move(eventName), {});
}

void ScriptExecutionContext::Emit(std::string eventName, std::vector<ScriptEventArgument> arguments, bool observationAlreadyNotified) {
    EmitTo(kb::scene::SceneEntity{}, std::move(eventName), std::move(arguments), observationAlreadyNotified);
}

void ScriptExecutionContext::EmitTo(kb::scene::SceneEntity target, std::string eventName) {
    EmitTo(target, std::move(eventName), {});
}

void ScriptExecutionContext::EmitTo(
    kb::scene::SceneEntity target,
    std::string eventName,
    std::vector<ScriptEventArgument> arguments,
    bool observationAlreadyNotified) {
    if (emittedEvents_ == nullptr || eventName.empty()) {
        return;
    }
    emittedEvents_->push_back(ScriptEvent{
        .name = std::move(eventName),
        .sender = self_,
        .target = target,
        .senderAsset = assetId_,
        .arguments = std::move(arguments),
        .observationAlreadyNotified = observationAlreadyNotified,
    });
}

} // namespace kb::script
