#include "engine/script/ScriptRendererApi.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace kb::script {
namespace {

const ScriptValue* FindArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) {
    for (const ScriptFunctionArgument& argument : arguments) {
        if (argument.name == name) {
            return &argument.value;
        }
    }
    return nullptr;
}

ScriptFunctionCallResult Error(std::string message) {
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { std::move(message) } };
}

[[nodiscard]] kb::scene::SceneEntity TargetEntity(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) noexcept {
    const ScriptValue* explicitEntity = FindArg(arguments, "entity");
    if (explicitEntity != nullptr) {
        return kb::scene::SceneEntity{ explicitEntity->AsUInt64() };
    }
    return context.caller;
}

ScriptFunctionCallResult RendererIsVisible(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("renderer api requires an active scene");
    }
    const kb::scene::SceneEntity entity = TargetEntity(context, arguments);
    if (!entity.IsValid() || !context.scene->Entities().IsAlive(entity)) {
        return Error("renderer visibility entity is not alive");
    }

    const bool visible = kb::scene::SceneRenderFeedback::IsVisible(*context.scene, entity);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "visible", ScriptValue{ visible } } },
        .errors = {},
    };
}

ScriptFunctionCallResult RendererGetBounds(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("renderer api requires an active scene");
    }
    const kb::scene::SceneEntity entity = TargetEntity(context, arguments);
    if (!entity.IsValid() || !context.scene->Entities().IsAlive(entity)) {
        return Error("renderer bounds entity is not alive");
    }

    const kb::scene::SceneRenderBounds bounds = kb::scene::SceneRenderFeedback::WorldBounds(*context.scene, entity);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "found", ScriptValue{ bounds.IsValid() } },
            ScriptFunctionArgument{ "centerX", ScriptValue{ bounds.center.x } },
            ScriptFunctionArgument{ "centerY", ScriptValue{ bounds.center.y } },
            ScriptFunctionArgument{ "centerZ", ScriptValue{ bounds.center.z } },
            ScriptFunctionArgument{ "radius", ScriptValue{ bounds.radius } },
        },
        .errors = {},
    };
}

ScriptFunctionCallResult RendererTestFrustum(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("renderer api requires an active scene");
    }
    const ScriptValue* centerXArgument = FindArg(arguments, "centerX");
    const ScriptValue* centerYArgument = FindArg(arguments, "centerY");
    const ScriptValue* centerZArgument = FindArg(arguments, "centerZ");
    const ScriptValue* radiusArgument = FindArg(arguments, "radius");
    const kb::math::Vec3 center{
        centerXArgument == nullptr ? 0.0F : centerXArgument->AsFloat(),
        centerYArgument == nullptr ? 0.0F : centerYArgument->AsFloat(),
        centerZArgument == nullptr ? 0.0F : centerZArgument->AsFloat(),
    };
    const float radius = radiusArgument == nullptr ? 0.0F : radiusArgument->AsFloat();

    const bool inside = kb::scene::SceneRenderFeedback::TestFrustum(*context.scene, center, radius);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "inside", ScriptValue{ inside } } },
        .errors = {},
    };
}

ScriptFunctionCallResult RendererHasFrame(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    static_cast<void>(arguments);
    if (context.scene == nullptr) {
        return Error("renderer api requires an active scene");
    }
    const bool published = kb::scene::SceneRenderFeedback::HasFrame(*context.scene);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "published", ScriptValue{ published } } },
        .errors = {},
    };
}

} // namespace

bool ScriptRendererApi::Register(ScriptRuntimeHost& host) {
    ScriptFunctionDesc isVisible;
    isVisible.signature.name = "Renderer.IsVisible";
    isVisible.signature.inputs = {
        ScriptFunctionPin{ "entity", ScriptValueType::Entity, false },
    };
    isVisible.signature.outputs = {
        ScriptFunctionPin{ "visible", ScriptValueType::Bool, true },
    };
    isVisible.callback = &RendererIsVisible;
    if (!host.RegisterFunction(std::move(isVisible))) {
        return false;
    }

    ScriptFunctionDesc getBounds;
    getBounds.signature.name = "Renderer.GetBounds";
    getBounds.signature.inputs = {
        ScriptFunctionPin{ "entity", ScriptValueType::Entity, false },
    };
    getBounds.signature.outputs = {
        ScriptFunctionPin{ "found", ScriptValueType::Bool, true },
        ScriptFunctionPin{ "centerX", ScriptValueType::Float, true },
        ScriptFunctionPin{ "centerY", ScriptValueType::Float, true },
        ScriptFunctionPin{ "centerZ", ScriptValueType::Float, true },
        ScriptFunctionPin{ "radius", ScriptValueType::Float, true },
    };
    getBounds.callback = &RendererGetBounds;
    if (!host.RegisterFunction(std::move(getBounds))) {
        return false;
    }

    ScriptFunctionDesc testFrustum;
    testFrustum.signature.name = "Renderer.TestFrustum";
    testFrustum.signature.inputs = {
        ScriptFunctionPin{ "centerX", ScriptValueType::Float, true },
        ScriptFunctionPin{ "centerY", ScriptValueType::Float, true },
        ScriptFunctionPin{ "centerZ", ScriptValueType::Float, true },
        ScriptFunctionPin{ "radius", ScriptValueType::Float, false },
    };
    testFrustum.signature.outputs = {
        ScriptFunctionPin{ "inside", ScriptValueType::Bool, true },
    };
    testFrustum.callback = &RendererTestFrustum;
    if (!host.RegisterFunction(std::move(testFrustum))) {
        return false;
    }

    ScriptFunctionDesc hasFrame;
    hasFrame.signature.name = "Renderer.HasFrame";
    hasFrame.signature.outputs = {
        ScriptFunctionPin{ "published", ScriptValueType::Bool, true },
    };
    hasFrame.callback = &RendererHasFrame;
    return host.RegisterFunction(std::move(hasFrame));
}

} // namespace kb::script
