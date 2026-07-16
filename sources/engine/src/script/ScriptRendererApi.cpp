#include "engine/script/ScriptRendererApi.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <cstdint>
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

[[nodiscard]] float FloatArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, float fallback = 0.0F) {
    const ScriptValue* argument = FindArg(arguments, name);
    return argument == nullptr ? fallback : argument->AsFloat();
}

ScriptFunctionCallResult RendererWorldToScreen(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("renderer api requires an active scene");
    }
    const kb::math::Vec3 world{ FloatArg(arguments, "x"), FloatArg(arguments, "y"), FloatArg(arguments, "z") };
    const kb::scene::SceneRenderScreenPoint point = kb::scene::SceneRenderFeedback::WorldToScreen(*context.scene, world);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "valid", ScriptValue{ point.valid } },
            ScriptFunctionArgument{ "onScreen", ScriptValue{ point.onScreen } },
            ScriptFunctionArgument{ "screenX", ScriptValue{ point.screenX } },
            ScriptFunctionArgument{ "screenY", ScriptValue{ point.screenY } },
            ScriptFunctionArgument{ "depth", ScriptValue{ point.viewDepth } },
        },
        .errors = {},
    };
}

ScriptFunctionCallResult RendererScreenPointToRay(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("renderer api requires an active scene");
    }
    const kb::scene::SceneRenderCameraRay cameraRay = kb::scene::SceneRenderFeedback::ScreenPointToRay(
        *context.scene,
        FloatArg(arguments, "screenX"),
        FloatArg(arguments, "screenY"));
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "valid", ScriptValue{ cameraRay.valid } },
            ScriptFunctionArgument{ "originX", ScriptValue{ cameraRay.ray.origin.x } },
            ScriptFunctionArgument{ "originY", ScriptValue{ cameraRay.ray.origin.y } },
            ScriptFunctionArgument{ "originZ", ScriptValue{ cameraRay.ray.origin.z } },
            ScriptFunctionArgument{ "directionX", ScriptValue{ cameraRay.ray.direction.x } },
            ScriptFunctionArgument{ "directionY", ScriptValue{ cameraRay.ray.direction.y } },
            ScriptFunctionArgument{ "directionZ", ScriptValue{ cameraRay.ray.direction.z } },
        },
        .errors = {},
    };
}

ScriptFunctionCallResult RendererScreenToWorld(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("renderer api requires an active scene");
    }
    kb::math::Vec3 world{};
    const bool valid = kb::scene::SceneRenderFeedback::ScreenToWorld(
        *context.scene,
        FloatArg(arguments, "screenX"),
        FloatArg(arguments, "screenY"),
        FloatArg(arguments, "distance"),
        world);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "valid", ScriptValue{ valid } },
            ScriptFunctionArgument{ "x", ScriptValue{ world.x } },
            ScriptFunctionArgument{ "y", ScriptValue{ world.y } },
            ScriptFunctionArgument{ "z", ScriptValue{ world.z } },
        },
        .errors = {},
    };
}

ScriptFunctionCallResult RendererCaptureScreen(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("renderer api requires an active scene");
    }
    const ScriptValue* pathArgument = FindArg(arguments, "path");
    const std::string path = pathArgument == nullptr ? std::string{} : pathArgument->AsString();
    if (path.empty()) {
        return Error("screen capture requires a non-empty output path");
    }
    const std::uint64_t capture = kb::scene::SceneRenderFeedback::RequestScreenCapture(*context.scene, path);
    if (capture == 0U) {
        return Error("screen capture could not be requested (another capture is still pending)");
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "capture", ScriptValue{ capture, ScriptValueType::Hash } } },
        .errors = {},
    };
}

[[nodiscard]] const char* CaptureStatusName(kb::scene::SceneScreenCaptureStatus status) noexcept {
    switch (status) {
    case kb::scene::SceneScreenCaptureStatus::Pending:
        return "pending";
    case kb::scene::SceneScreenCaptureStatus::Completed:
        return "completed";
    case kb::scene::SceneScreenCaptureStatus::Failed:
        return "failed";
    case kb::scene::SceneScreenCaptureStatus::Unknown:
        break;
    }
    return "unknown";
}

ScriptFunctionCallResult RendererCaptureStatus(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("renderer api requires an active scene");
    }
    const ScriptValue* captureArgument = FindArg(arguments, "capture");
    const std::uint64_t capture = captureArgument == nullptr ? 0U : captureArgument->AsUInt64();
    const kb::scene::SceneScreenCaptureStatus status = kb::scene::SceneRenderFeedback::ScreenCaptureStatus(*context.scene, capture);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "status", ScriptValue{ std::string{ CaptureStatusName(status) } } } },
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
    if (!host.RegisterFunction(std::move(hasFrame))) {
        return false;
    }

    ScriptFunctionDesc worldToScreen;
    worldToScreen.signature.name = "Renderer.WorldToScreen";
    worldToScreen.signature.inputs = {
        ScriptFunctionPin{ "x", ScriptValueType::Float, true },
        ScriptFunctionPin{ "y", ScriptValueType::Float, true },
        ScriptFunctionPin{ "z", ScriptValueType::Float, true },
    };
    worldToScreen.signature.outputs = {
        ScriptFunctionPin{ "valid", ScriptValueType::Bool, true },
        ScriptFunctionPin{ "onScreen", ScriptValueType::Bool, true },
        ScriptFunctionPin{ "screenX", ScriptValueType::Float, true },
        ScriptFunctionPin{ "screenY", ScriptValueType::Float, true },
        ScriptFunctionPin{ "depth", ScriptValueType::Float, true },
    };
    worldToScreen.callback = &RendererWorldToScreen;
    if (!host.RegisterFunction(std::move(worldToScreen))) {
        return false;
    }

    ScriptFunctionDesc screenPointToRay;
    screenPointToRay.signature.name = "Renderer.ScreenPointToRay";
    screenPointToRay.signature.inputs = {
        ScriptFunctionPin{ "screenX", ScriptValueType::Float, true },
        ScriptFunctionPin{ "screenY", ScriptValueType::Float, true },
    };
    screenPointToRay.signature.outputs = {
        ScriptFunctionPin{ "valid", ScriptValueType::Bool, true },
        ScriptFunctionPin{ "originX", ScriptValueType::Float, true },
        ScriptFunctionPin{ "originY", ScriptValueType::Float, true },
        ScriptFunctionPin{ "originZ", ScriptValueType::Float, true },
        ScriptFunctionPin{ "directionX", ScriptValueType::Float, true },
        ScriptFunctionPin{ "directionY", ScriptValueType::Float, true },
        ScriptFunctionPin{ "directionZ", ScriptValueType::Float, true },
    };
    screenPointToRay.callback = &RendererScreenPointToRay;
    if (!host.RegisterFunction(std::move(screenPointToRay))) {
        return false;
    }

    ScriptFunctionDesc screenToWorld;
    screenToWorld.signature.name = "Renderer.ScreenToWorld";
    screenToWorld.signature.inputs = {
        ScriptFunctionPin{ "screenX", ScriptValueType::Float, true },
        ScriptFunctionPin{ "screenY", ScriptValueType::Float, true },
        ScriptFunctionPin{ "distance", ScriptValueType::Float, true },
    };
    screenToWorld.signature.outputs = {
        ScriptFunctionPin{ "valid", ScriptValueType::Bool, true },
        ScriptFunctionPin{ "x", ScriptValueType::Float, true },
        ScriptFunctionPin{ "y", ScriptValueType::Float, true },
        ScriptFunctionPin{ "z", ScriptValueType::Float, true },
    };
    screenToWorld.callback = &RendererScreenToWorld;
    if (!host.RegisterFunction(std::move(screenToWorld))) {
        return false;
    }

    ScriptFunctionDesc captureScreen;
    captureScreen.signature.name = "Renderer.CaptureScreen";
    captureScreen.signature.inputs = {
        ScriptFunctionPin{ "path", ScriptValueType::String, true },
    };
    captureScreen.signature.outputs = {
        ScriptFunctionPin{ "capture", ScriptValueType::Hash, true },
    };
    captureScreen.callback = &RendererCaptureScreen;
    if (!host.RegisterFunction(std::move(captureScreen))) {
        return false;
    }

    ScriptFunctionDesc captureStatus;
    captureStatus.signature.name = "Renderer.CaptureStatus";
    captureStatus.signature.inputs = {
        ScriptFunctionPin{ "capture", ScriptValueType::Hash, true },
    };
    captureStatus.signature.outputs = {
        ScriptFunctionPin{ "status", ScriptValueType::String, true },
    };
    captureStatus.callback = &RendererCaptureStatus;
    return host.RegisterFunction(std::move(captureStatus));
}

} // namespace kb::script
