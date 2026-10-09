#include "engine/script/ScriptNavigationApi.hpp"

#include "engine/math/DVec3.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneNavigation.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

using kb::math::DVec3;

const ScriptValue* FindArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) {
    for (const ScriptFunctionArgument& argument : arguments) {
        if (argument.name == name) {
            return &argument.value;
        }
    }
    return nullptr;
}

[[nodiscard]] float FloatArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, float fallback) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? fallback : value->AsFloat(fallback);
}

[[nodiscard]] int IntArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, int fallback) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? fallback : value->AsInt(fallback);
}

[[nodiscard]] bool PointArg(std::span<const ScriptFunctionArgument> arguments, std::string_view prefix, DVec3& point) {
    const float x = FloatArg(arguments, std::string{ prefix } + "X", 0.0F);
    const float y = FloatArg(arguments, std::string{ prefix } + "Y", 0.0F);
    const float z = FloatArg(arguments, std::string{ prefix } + "Z", 0.0F);
    point = DVec3{ x, y, z };
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

[[nodiscard]] kb::scene::NavQueryOptions OptionsArg(std::span<const ScriptFunctionArgument> arguments) noexcept {
    kb::scene::NavQueryOptions options;
    options.profile = static_cast<std::uint32_t>(std::max(0, IntArg(arguments, "profile", 0)));
    options.areas = static_cast<kb::scene::NavAreaMask>(IntArg(arguments, "areaMask", -1));
    return options;
}

ScriptFunctionCallResult Error(std::string message) {
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { std::move(message) } };
}

[[nodiscard]] ScriptFunctionArgument Out(std::string name, ScriptValue value) {
    return ScriptFunctionArgument{ std::move(name), std::move(value) };
}

[[nodiscard]] float Narrow(double value) noexcept {
    return static_cast<float>(value);
}

ScriptFunctionCallResult FindPath(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("navigation api requires an active scene");
    DVec3 start{};
    DVec3 end{};
    if (!PointArg(arguments, "start", start) || !PointArg(arguments, "end", end)) return Error("path start or end is not finite");
    const kb::scene::NavPathResult path = kb::scene::SceneNavigation{ *context.scene }.FindPath(start, end, OptionsArg(arguments));
    const bool found = path.status == kb::scene::NavPathStatus::Complete || path.status == kb::scene::NavPathStatus::Partial;
    // The corners stay with the scene for Navigation.PathCorner.
    kb::scene::SceneAccess::State(*context.scene).navigation.scriptPath = path.corners;
    const DVec3 reached = path.corners.empty() ? DVec3{} : path.corners.back();
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            Out("found", ScriptValue{ found }),
            Out("complete", ScriptValue{ path.status == kb::scene::NavPathStatus::Complete }),
            Out("length", ScriptValue{ path.length }),
            Out("corners", ScriptValue{ static_cast<int>(path.corners.size()) }),
            Out("endX", ScriptValue{ Narrow(reached.x) }),
            Out("endY", ScriptValue{ Narrow(reached.y) }),
            Out("endZ", ScriptValue{ Narrow(reached.z) }),
        },
        .errors = {},
    };
}

ScriptFunctionCallResult PathCorner(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("navigation api requires an active scene");
    const std::vector<DVec3>& corners = kb::scene::SceneAccess::State(*context.scene).navigation.scriptPath;
    const int index = IntArg(arguments, "index", -1);
    const bool found = index >= 0 && static_cast<std::size_t>(index) < corners.size();
    const DVec3 corner = found ? corners[static_cast<std::size_t>(index)] : DVec3{};
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            Out("found", ScriptValue{ found }),
            Out("x", ScriptValue{ Narrow(corner.x) }),
            Out("y", ScriptValue{ Narrow(corner.y) }),
            Out("z", ScriptValue{ Narrow(corner.z) }),
        },
        .errors = {},
    };
}

ScriptFunctionCallResult Raycast(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("navigation api requires an active scene");
    DVec3 start{};
    DVec3 end{};
    if (!PointArg(arguments, "start", start) || !PointArg(arguments, "end", end)) return Error("raycast start or end is not finite");
    const kb::scene::NavRaycastResult ray = kb::scene::SceneNavigation{ *context.scene }.Raycast(start, end, OptionsArg(arguments));
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            Out("valid", ScriptValue{ ray.valid }),
            Out("hit", ScriptValue{ ray.hit }),
            Out("fraction", ScriptValue{ ray.fraction }),
            Out("x", ScriptValue{ Narrow(ray.position.x) }),
            Out("y", ScriptValue{ Narrow(ray.position.y) }),
            Out("z", ScriptValue{ Narrow(ray.position.z) }),
            Out("normalX", ScriptValue{ ray.normal.x }),
            Out("normalZ", ScriptValue{ ray.normal.z }),
        },
        .errors = {},
    };
}

ScriptFunctionCallResult NearestPoint(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("navigation api requires an active scene");
    const float x = FloatArg(arguments, "x", 0.0F);
    const float y = FloatArg(arguments, "y", 0.0F);
    const float z = FloatArg(arguments, "z", 0.0F);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return Error("position is not finite");
    const DVec3 position{ x, y, z };
    const std::optional<DVec3> nearest = kb::scene::SceneNavigation{ *context.scene }.NearestPoint(position, OptionsArg(arguments));
    const DVec3 point = nearest.value_or(DVec3{});
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            Out("found", ScriptValue{ nearest.has_value() }),
            Out("x", ScriptValue{ Narrow(point.x) }),
            Out("y", ScriptValue{ Narrow(point.y) }),
            Out("z", ScriptValue{ Narrow(point.z) }),
        },
        .errors = {},
    };
}

ScriptFunctionCallResult SetAreaCost(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("navigation api requires an active scene");
    const int area = IntArg(arguments, "area", -1);
    const float cost = FloatArg(arguments, "cost", 1.0F);
    const bool applied = area >= 0 && area < static_cast<int>(kb::scene::kNavAreaCount) && std::isfinite(cost) && cost > 0.0F;
    if (applied) kb::scene::SceneNavigation{ *context.scene }.SetAreaCost(static_cast<kb::scene::NavAreaId>(area), cost);
    return ScriptFunctionCallResult{ .executed = true, .outputs = { Out("applied", ScriptValue{ applied }) }, .errors = {} };
}

[[nodiscard]] std::vector<ScriptFunctionPin> PointPins(std::string_view prefix) {
    return {
        ScriptFunctionPin{ std::string{ prefix } + "X", ScriptValueType::Float, true },
        ScriptFunctionPin{ std::string{ prefix } + "Y", ScriptValueType::Float, true },
        ScriptFunctionPin{ std::string{ prefix } + "Z", ScriptValueType::Float, true },
    };
}

[[nodiscard]] std::vector<ScriptFunctionPin> OptionPins() {
    return {
        ScriptFunctionPin{ "profile", ScriptValueType::Int, false },
        ScriptFunctionPin{ "areaMask", ScriptValueType::Int, false },
    };
}

[[nodiscard]] bool RegisterFn(ScriptRuntimeHost& host, std::string name, std::vector<ScriptFunctionPin> inputs, std::vector<ScriptFunctionPin> outputs,
    ScriptFunctionCallback callback) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = std::move(inputs);
    desc.signature.outputs = std::move(outputs);
    desc.callback = callback;
    return host.RegisterFunction(std::move(desc));
}

template <typename... Lists>
[[nodiscard]] std::vector<ScriptFunctionPin> Join(Lists... lists) {
    std::vector<ScriptFunctionPin> joined;
    (joined.insert(joined.end(), lists.begin(), lists.end()), ...);
    return joined;
}

} // namespace

bool ScriptNavigationApi::Register(ScriptRuntimeHost& host) {
    const auto floatOut = [](std::string name) { return ScriptFunctionPin{ std::move(name), ScriptValueType::Float, true }; };
    const auto boolOut = [](std::string name) { return ScriptFunctionPin{ std::move(name), ScriptValueType::Bool, true }; };
    bool ok = RegisterFn(host, "Navigation.FindPath", Join(PointPins("start"), PointPins("end"), OptionPins()),
        { boolOut("found"), boolOut("complete"), floatOut("length"), ScriptFunctionPin{ "corners", ScriptValueType::Int, true }, floatOut("endX"),
            floatOut("endY"), floatOut("endZ") },
        &FindPath);
    ok = RegisterFn(host, "Navigation.PathCorner", { ScriptFunctionPin{ "index", ScriptValueType::Int, true } },
        { boolOut("found"), floatOut("x"), floatOut("y"), floatOut("z") }, &PathCorner) && ok;
    ok = RegisterFn(host, "Navigation.Raycast", Join(PointPins("start"), PointPins("end"), OptionPins()),
        { boolOut("valid"), boolOut("hit"), floatOut("fraction"), floatOut("x"), floatOut("y"), floatOut("z"), floatOut("normalX"), floatOut("normalZ") },
        &Raycast) && ok;
    const std::vector<ScriptFunctionPin> position{ ScriptFunctionPin{ "x", ScriptValueType::Float, true },
        ScriptFunctionPin{ "y", ScriptValueType::Float, true }, ScriptFunctionPin{ "z", ScriptValueType::Float, true } };
    ok = RegisterFn(host, "Navigation.NearestPoint", Join(position, OptionPins()), { boolOut("found"), floatOut("x"), floatOut("y"), floatOut("z") },
        &NearestPoint) && ok;
    ok = RegisterFn(host, "Navigation.SetAreaCost",
        { ScriptFunctionPin{ "area", ScriptValueType::Int, true }, ScriptFunctionPin{ "cost", ScriptValueType::Float, true } }, { boolOut("applied") },
        &SetAreaCost) && ok;
    return ok;
}

} // namespace kb::script
