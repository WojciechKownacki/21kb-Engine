#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/script/ScriptEvent.hpp"
#include "engine/script/ScriptEventBus.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptLifecycle.hpp"
#include "engine/script/ScriptSharedState.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::input {

class InputSubsystem;

} // namespace kb::input

namespace kb::scene {

struct TransformComponent;
class SceneTransformWorkerWrites;

} // namespace kb::scene

namespace kb::script {

class ScriptExecutionContext final {
public:
    ScriptExecutionContext(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity self,
        kb::assets::AssetId assetId,
        kb::scene::BehaviourBackend backend,
        ScriptLifecycleEvent lifecycle,
        float deltaSeconds,
        std::vector<ScriptEvent>* emittedEvents,
        const ScriptEvent* incomingEvent = nullptr,
        ScriptSharedState* sharedState = nullptr,
        ScriptFunctionRegistry* functions = nullptr,
        ScriptEventBus* events = nullptr) noexcept;

    ~ScriptExecutionContext();
    ScriptExecutionContext(const ScriptExecutionContext&) = delete;
    ScriptExecutionContext& operator=(const ScriptExecutionContext&) = delete;

    // The next behaviour a phase loop runs: the same context, aimed at another object.
    void Rebind(kb::scene::SceneEntity self, kb::assets::AssetId assetId, kb::scene::BehaviourBackend backend) {
        FinishSelfTransform();
        self_ = self;
        assetId_ = assetId;
        backend_ = backend;
    }

    // The object's own transform, to read and change in place without a lookup per access: change the local
    // position, rotation or scale, and the host publishes the row (world transform, dirty flags) when the call
    // returns. Null when the object has no transform. Taking it counts as a write. Making or destroying entities can
    // move rows: take it again after that (every call hands out the row where it is now).
    [[nodiscard]] kb::scene::TransformComponent* SelfTransform() {
        if (!selfTransformOpen_ || SelfTransformMoved()) {
            OpenSelfTransform();
        }
        return selfTransformRow_;
    }
    // A context that runs calls on a worker thread hands out SelfTransform through `writes` (worker range `range`).
    void UseWorkerWrites(kb::scene::SceneTransformWorkerWrites* writes, std::size_t range) noexcept {
        workerWrites_ = writes;
        workerRange_ = range;
    }
    // Publishes the transform SelfTransform handed out; the host calls it when the script's call returns.
    void FinishSelfTransform() {
        if (selfTransformOpen_) {
            PublishSelfTransform();
        }
    }

    [[nodiscard]] kb::scene::Scene& GetScene() noexcept;
    [[nodiscard]] const kb::scene::Scene& GetScene() const noexcept;
    // Convenience access to the scene's runtime input subsystem for native scripts.
    [[nodiscard]] kb::input::InputSubsystem& Input() noexcept;
    [[nodiscard]] const kb::input::InputSubsystem& Input() const noexcept;
    [[nodiscard]] kb::scene::SceneEntity Self() const noexcept;
    [[nodiscard]] kb::assets::AssetId Asset() const noexcept;
    [[nodiscard]] kb::scene::BehaviourBackend Backend() const noexcept;
    [[nodiscard]] ScriptLifecycleEvent Lifecycle() const noexcept;
    [[nodiscard]] float DeltaSeconds() const noexcept;
    [[nodiscard]] const ScriptEvent* IncomingEvent() const noexcept;
    [[nodiscard]] std::span<const ScriptEventArgument> EventArguments() const noexcept;
    [[nodiscard]] ScriptSharedState* SharedState() noexcept;
    [[nodiscard]] const ScriptSharedState* SharedState() const noexcept;
    [[nodiscard]] bool SetSharedValue(std::string key, ScriptValue value);
    [[nodiscard]] bool HasSharedValue(std::string_view key) const;
    [[nodiscard]] std::optional<ScriptValue> GetSharedValue(std::string_view key) const;
    [[nodiscard]] bool RemoveSharedValue(std::string_view key);
    [[nodiscard]] ScriptFunctionRegistry* Functions() noexcept;
    [[nodiscard]] const ScriptFunctionRegistry* Functions() const noexcept;
    [[nodiscard]] ScriptFunctionCallResult CallFunction(std::string_view name, std::span<const ScriptFunctionArgument> arguments);
    // LIB-105: null only for a ScriptExecutionContext built without a
    // ScriptRuntime behind it (e.g. isolated unit tests exercising a single
    // IScriptBackend directly) — every context reached through the normal
    // ScriptRuntime dispatch path always has one, mirroring Functions().
    [[nodiscard]] ScriptEventBus* Events() noexcept;
    [[nodiscard]] const ScriptEventBus* Events() const noexcept;

    void Emit(std::string eventName);
    void Emit(std::string eventName, std::vector<ScriptEventArgument> arguments, bool observationAlreadyNotified = false);
    void EmitTo(kb::scene::SceneEntity target, std::string eventName);
    void EmitTo(kb::scene::SceneEntity target, std::string eventName, std::vector<ScriptEventArgument> arguments, bool observationAlreadyNotified = false);

private:
    kb::scene::Scene& scene_;
    kb::scene::SceneEntity self_{};
    kb::assets::AssetId assetId_{};
    kb::scene::BehaviourBackend backend_ = kb::scene::BehaviourBackend::Native;
    ScriptLifecycleEvent lifecycle_ = ScriptLifecycleEvent::Tick;
    float deltaSeconds_ = 0.0F;
    std::vector<ScriptEvent>* emittedEvents_ = nullptr;
    const ScriptEvent* incomingEvent_ = nullptr;
    ScriptSharedState* sharedState_ = nullptr;
    ScriptFunctionRegistry* functions_ = nullptr;
    ScriptEventBus* events_ = nullptr;
    void OpenSelfTransform();
    [[nodiscard]] bool SelfTransformMoved() const noexcept;
    void PublishSelfTransform();
    kb::scene::TransformComponent* selfTransformRow_ = nullptr;
    kb::scene::SceneTransformWorkerWrites* workerWrites_ = nullptr;
    std::size_t workerRange_ = 0U;
    std::uint64_t selfTransformStructure_ = 0U;
    bool selfTransformOpen_ = false;
    bool selfTransformComposes_ = false;
    bool selfTransformMirrored_ = false;
};

} // namespace kb::script
