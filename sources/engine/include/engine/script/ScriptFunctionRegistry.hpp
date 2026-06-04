#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/script/ScriptLifecycle.hpp"
#include "engine/script/ScriptValue.hpp"

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
    std::vector<ScriptFunctionPin> inputs;
    std::vector<ScriptFunctionPin> outputs;
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
    [[nodiscard]] bool Register(ScriptFunctionDesc function);
    [[nodiscard]] const ScriptFunctionSignature* FindSignature(std::string_view name) const noexcept;
    [[nodiscard]] const std::vector<ScriptFunctionDesc>& Functions() const noexcept;
    [[nodiscard]] ScriptFunctionCallResult Call(
        std::string_view name,
        std::span<const ScriptFunctionArgument> arguments,
        const ScriptFunctionCallContext& context) const;

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
};

} // namespace kb::script
