#include "engine/script/ScriptUIApi.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/scene/SceneUIComponentSet.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"
#include "engine/ui/UIComponentCatalog.hpp"
#include "engine/ui/UIComponentPropertyCatalog.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

[[nodiscard]] const ScriptValue* FindArg(std::span<const ScriptFunctionArgument> arguments,
                                         std::string_view name) noexcept {
    for (const ScriptFunctionArgument& argument : arguments) {
        if (argument.name == name) {
            return &argument.value;
        }
    }
    return nullptr;
}

[[nodiscard]] std::string StringArg(std::span<const ScriptFunctionArgument> arguments,
                                    std::string_view name, std::string fallback = {}) {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? std::move(fallback) : value->AsString();
}

[[nodiscard]] kb::scene::SceneEntity EntityArg(
    std::span<const ScriptFunctionArgument> arguments, std::string_view name) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? kb::scene::SceneEntity{}
                            : kb::scene::SceneEntity{value->AsUInt64()};
}

[[nodiscard]] float FloatArg(std::span<const ScriptFunctionArgument> arguments,
                             std::string_view name) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? 0.0F : value->AsFloat();
}

[[nodiscard]] ScriptFunctionCallResult Error(std::string message) {
    return ScriptFunctionCallResult{
        .executed = false,
        .outputs = {},
        .errors = {std::move(message)},
    };
}

[[nodiscard]] ScriptFunctionCallResult NoScene() {
    return Error("ui api requires an active scene");
}

[[nodiscard]] ScriptFunctionCallResult BoolResult(std::string_view pin, bool value) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {ScriptFunctionArgument{std::string{pin}, ScriptValue{value}}},
        .errors = {},
    };
}

[[nodiscard]] ScriptFunctionCallResult EntityResult(std::string_view pin,
                                                     kb::scene::SceneEntity entity) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {ScriptFunctionArgument{
            std::string{pin}, ScriptValue{entity.Id(), ScriptValueType::Entity}}},
        .errors = {},
    };
}

[[nodiscard]] bool Alive(const ScriptFunctionCallContext& context,
                         kb::scene::SceneEntity entity) noexcept {
    return context.scene != nullptr && entity.IsValid() && context.scene->Entities().IsAlive(entity);
}

ScriptFunctionCallResult Create(const ScriptFunctionCallContext& context,
                                std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const std::string presetName = StringArg(arguments, "preset");
    const kb::scene::UIComponentPresetDescriptor* preset =
        kb::scene::FindUIComponentPreset(presetName);
    if (preset == nullptr) {
        return Error("ui preset is not registered: " + presetName);
    }

    kb::scene::SceneObjectDesc desc;
    desc.name = StringArg(arguments, "name", std::string{preset->name});
    const kb::scene::SceneEntity parent = EntityArg(arguments, "parent");
    if (parent.IsValid()) {
        if (!context.scene->Entities().IsAlive(parent)) {
            return Error("ui parent entity is not live");
        }
        desc.parent = context.scene->Entities().Object(parent);
    }

    const kb::scene::SceneEntity entity = context.scene->Entities().CreateEntity(std::move(desc));
    kb::scene::ApplySceneUIComponents(
        context.scene->Components().UI(), entity, kb::scene::BuildUIComponentPreset(preset->preset));
    return EntityResult("entity", entity);
}

[[nodiscard]] const kb::scene::UIComponentDescriptor* ComponentArg(
    std::span<const ScriptFunctionArgument> arguments) noexcept {
    const ScriptValue* value = FindArg(arguments, "component");
    return value == nullptr ? nullptr
                            : kb::scene::FindUIComponentDescriptor(value->AsString());
}

ScriptFunctionCallResult AddComponent(const ScriptFunctionCallContext& context,
                                      std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return Error("ui component requires a live entity");
    }
    const kb::scene::UIComponentDescriptor* component = ComponentArg(arguments);
    if (component == nullptr) {
        return Error("ui component is not registered");
    }
    kb::scene::UIComponentSet components =
        kb::scene::CaptureSceneUIComponents(context.scene->Components().UI(), entity);
    if (kb::scene::HasUIComponent(components, component->type)) {
        return BoolResult("added", false);
    }
    if (!kb::scene::AddUIComponent(components, component->type)) {
        return Error("ui component conflicts with the entity component set");
    }
    kb::scene::SynchronizeSceneUIComponents(context.scene->Components().UI(), entity, components);
    return BoolResult("added", true);
}

ScriptFunctionCallResult RemoveComponent(const ScriptFunctionCallContext& context,
                                         std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return Error("ui component requires a live entity");
    }
    const kb::scene::UIComponentDescriptor* component = ComponentArg(arguments);
    if (component == nullptr) {
        return Error("ui component is not registered");
    }
    kb::scene::UIComponentSet components =
        kb::scene::CaptureSceneUIComponents(context.scene->Components().UI(), entity);
    if (!kb::scene::RemoveUIComponent(components, component->type)) {
        return BoolResult("removed", false);
    }
    kb::scene::SynchronizeSceneUIComponents(context.scene->Components().UI(), entity, components);
    return BoolResult("removed", true);
}

ScriptFunctionCallResult HasComponent(const ScriptFunctionCallContext& context,
                                      std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    const kb::scene::UIComponentDescriptor* component = ComponentArg(arguments);
    return BoolResult(
        "present",
        component != nullptr && Alive(context, entity) &&
            kb::scene::HasUIComponent(
                kb::scene::CaptureSceneUIComponents(context.scene->Components().UI(), entity),
                component->type));
}

ScriptFunctionCallResult Focus(const ScriptFunctionCallContext& context,
                               std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return BoolResult("focused", context.scene->UI().SetFocus(EntityArg(arguments, "entity")));
}

ScriptFunctionCallResult ClearFocus(const ScriptFunctionCallContext& context,
                                    std::span<const ScriptFunctionArgument>) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    context.scene->UI().ClearFocus();
    return BoolResult("cleared", true);
}

ScriptFunctionCallResult HitTest(const ScriptFunctionCallContext& context,
                                 std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return EntityResult(
        "entity", context.scene->UI().HitTest({FloatArg(arguments, "x"), FloatArg(arguments, "y")}));
}

ScriptFunctionCallResult Hovered(const ScriptFunctionCallContext& context,
                                 std::span<const ScriptFunctionArgument>) {
    return context.scene == nullptr ? NoScene()
                                    : EntityResult("entity", context.scene->UI().Hovered());
}

ScriptFunctionCallResult Pressed(const ScriptFunctionCallContext& context,
                                 std::span<const ScriptFunctionArgument>) {
    return context.scene == nullptr ? NoScene()
                                    : EntityResult("entity", context.scene->UI().Pressed());
}

ScriptFunctionCallResult Focused(const ScriptFunctionCallContext& context,
                                 std::span<const ScriptFunctionArgument>) {
    return context.scene == nullptr ? NoScene()
                                    : EntityResult("entity", context.scene->UI().Focused());
}

bool RegisterFunction(ScriptRuntimeHost& host, std::string name,
                      std::vector<ScriptFunctionPin> inputs,
                      std::vector<ScriptFunctionPin> outputs,
                      ScriptFunctionCallback callback) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = std::move(inputs);
    desc.signature.outputs = std::move(outputs);
    desc.callback = std::move(callback);
    return host.RegisterFunction(std::move(desc));
}

} // namespace

bool ScriptUIApi::Register(ScriptRuntimeHost& host) {
    bool ok = true;
    ok = RegisterFunction(
             host, "UI.Create",
             {
                 ScriptFunctionPin{"preset", ScriptValueType::String, true},
                 ScriptFunctionPin{"name", ScriptValueType::String, false},
                 ScriptFunctionPin{"parent", ScriptValueType::Entity, false},
             },
             {ScriptFunctionPin{"entity", ScriptValueType::Entity, true}}, &Create) &&
         ok;
    ok = RegisterFunction(
             host, "UI.AddComponent",
             {
                 ScriptFunctionPin{"entity", ScriptValueType::Entity, true},
                 ScriptFunctionPin{"component", ScriptValueType::String, true},
             },
             {ScriptFunctionPin{"added", ScriptValueType::Bool, true}}, &AddComponent) &&
         ok;
    ok = RegisterFunction(
             host, "UI.RemoveComponent",
             {
                 ScriptFunctionPin{"entity", ScriptValueType::Entity, true},
                 ScriptFunctionPin{"component", ScriptValueType::String, true},
             },
             {ScriptFunctionPin{"removed", ScriptValueType::Bool, true}}, &RemoveComponent) &&
         ok;
    ok = RegisterFunction(
             host, "UI.HasComponent",
             {
                 ScriptFunctionPin{"entity", ScriptValueType::Entity, true},
                 ScriptFunctionPin{"component", ScriptValueType::String, true},
             },
             {ScriptFunctionPin{"present", ScriptValueType::Bool, true}}, &HasComponent) &&
         ok;
    ok = RegisterFunction(
             host, "UI.Focus", {ScriptFunctionPin{"entity", ScriptValueType::Entity, true}},
             {ScriptFunctionPin{"focused", ScriptValueType::Bool, true}}, &Focus) &&
         ok;
    ok = RegisterFunction(host, "UI.ClearFocus", {},
                          {ScriptFunctionPin{"cleared", ScriptValueType::Bool, true}},
                          &ClearFocus) &&
         ok;
    ok = RegisterFunction(
             host, "UI.HitTest",
             {
                 ScriptFunctionPin{"x", ScriptValueType::Float, true},
                 ScriptFunctionPin{"y", ScriptValueType::Float, true},
             },
             {ScriptFunctionPin{"entity", ScriptValueType::Entity, true}}, &HitTest) &&
         ok;
    ok = RegisterFunction(host, "UI.Hovered", {},
                          {ScriptFunctionPin{"entity", ScriptValueType::Entity, true}},
                          &Hovered) &&
         ok;
    ok = RegisterFunction(host, "UI.Pressed", {},
                          {ScriptFunctionPin{"entity", ScriptValueType::Entity, true}},
                          &Pressed) &&
         ok;
    ok = RegisterFunction(host, "UI.Focused", {},
                          {ScriptFunctionPin{"entity", ScriptValueType::Entity, true}},
                          &Focused) &&
         ok;
    return ok;
}

} // namespace kb::script
