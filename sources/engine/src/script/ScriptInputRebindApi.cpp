#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/input/InputRebindSession.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/assets/AssetManager.hpp"

#include <array>
#include <charconv>
#include <memory>
#include <unordered_map>

namespace kb::script {
namespace {
const ScriptValue* Arg(std::span<const ScriptFunctionArgument> args, std::string_view name) {
    for (const auto& arg : args) if (arg.name == name) return &arg.value;
    return nullptr;
}
std::string Text(std::span<const ScriptFunctionArgument> args, std::string_view name) {
    const auto* value = Arg(args, name);
    return value ? value->AsString() : std::string{};
}
std::uint64_t Id(std::string_view text) {
    std::uint64_t id = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() ? id : 0;
}
kb::input::LocalUserId Player(std::span<const ScriptFunctionArgument> args) {
    const auto* player = Arg(args, "player");
    return {player && player->AsInt() > 0 ? static_cast<std::uint32_t>(player->AsInt()) : 0U};
}
ScriptFunctionCallResult Result(std::string_view pin, bool value, std::string error = {}) {
    return {.executed = true, .outputs = {{std::string{pin}, ScriptValue{value}}, {"error", ScriptValue{std::move(error)}}}};
}
} // namespace

bool RegisterInputRebindSessionApi(ScriptRuntimeHost& host) {
    using Session = kb::input::InputRebindSession;
    auto sessions = std::make_shared<std::unordered_map<std::uint32_t, Session>>();
    const ScriptFunctionPin player{"player", ScriptValueType::Int, false};
    const auto add = [&](std::string name, std::vector<ScriptFunctionPin> inputs,
        std::vector<ScriptFunctionPin> outputs, ScriptFunctionCallback callback) {
        ScriptFunctionDesc desc;
        desc.signature.name = std::move(name);
        desc.signature.inputs = std::move(inputs);
        desc.signature.outputs = std::move(outputs);
        desc.callback = std::move(callback);
        return host.RegisterFunction(std::move(desc));
    };
    const auto boolean = [](std::string name) { return std::vector<ScriptFunctionPin>{
        {std::move(name), ScriptValueType::Bool, true}, {"error", ScriptValueType::String, true}}; };
    bool ok = add("Input.OpenRebindProfile", {{"context", ScriptValueType::String, true},
        {"path", ScriptValueType::String, true}, player}, boolean("opened"),
        [sessions](const ScriptFunctionCallContext& ctx, std::span<const ScriptFunctionArgument> args) {
            if (!ctx.scene) return Result("opened", false, "an active scene is required");
            auto& assets = ctx.scene->Assets().Manager();
            const auto context = Text(args, "context");
            auto id = Id(context);
            if (!id) {
                const auto* metadata = assets.Registry().FindByPath(context);
                if (metadata) id = metadata->id.value;
            }
            const auto defaults = assets.Load<kb::input::InputMappingContextAsset>(kb::assets::AssetId{id});
            if (!defaults.IsLoaded()) return Result("opened", false, "mapping context could not be loaded");
            auto& session = (*sessions)[Player(args).value];
            const bool opened = session.Initialize(ctx.scene->Input(Player(args)), id, *defaults.Shared(), Text(args, "path"));
            return Result("opened", opened, session.Error());
        });
    ok = add("Input.BeginRebind", {{"binding", ScriptValueType::String, true}, player}, boolean("started"),
        [sessions](const ScriptFunctionCallContext& ctx, std::span<const ScriptFunctionArgument> args) {
            if (!ctx.scene) return Result("started", false, "an active scene is required");
            auto& session = (*sessions)[Player(args).value];
            const bool started = session.Begin(Id(Text(args, "binding")), ctx.scene->Input().DeviceState());
            return Result("started", started, started ? "" : "open a profile and select a valid binding first");
        }) && ok;
    ok = add("Input.PollRebind", {{"swapConflict", ScriptValueType::Bool, false}, player},
        {{"state", ScriptValueType::String, true}, {"conflict", ScriptValueType::String, true}, {"error", ScriptValueType::String, true}},
        [sessions](const ScriptFunctionCallContext& ctx, std::span<const ScriptFunctionArgument> args) {
            if (!ctx.scene) return ScriptFunctionCallResult{.errors = {"an active scene is required"}};
            auto& session = (*sessions)[Player(args).value];
            const auto* swap = Arg(args, "swapConflict");
            const auto state = session.Poll(ctx.scene->Input(Player(args)), ctx.scene->Input().DeviceState(), swap && swap->AsBool());
            constexpr std::array names{"idle", "listening", "conflict", "applied", "canceled", "failed"};
            return ScriptFunctionCallResult{.executed = true, .outputs = {
                {"state", ScriptValue{std::string{names[static_cast<std::size_t>(state)]}}},
                {"conflict", ScriptValue{session.ConflictBinding() ? std::to_string(session.ConflictBinding()) : std::string{}}},
                {"error", ScriptValue{session.Error()}}}};
        }) && ok;
    ok = add("Input.CancelRebind", {player}, boolean("canceled"),
        [sessions](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
            (*sessions)[Player(args).value].Cancel();
            return Result("canceled", true);
        }) && ok;
    ok = add("Input.ResetRebind", {player}, boolean("reset"),
        [sessions](const ScriptFunctionCallContext& ctx, std::span<const ScriptFunctionArgument> args) {
            if (!ctx.scene) return Result("reset", false, "an active scene is required");
            auto& session = (*sessions)[Player(args).value];
            const bool reset = session.Reset(ctx.scene->Input(Player(args)));
            return Result("reset", reset, session.Error());
        }) && ok;
    return ok;
}
} // namespace kb::script
