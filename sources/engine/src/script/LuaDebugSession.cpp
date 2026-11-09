#include "engine/script/LuaDebugSession.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"

#include <cmath>
#include <limits>

namespace kb::script {
namespace {
using kb::core::JsonValue;
JsonValue Reply(bool succeeded, std::string error = {}) {
    auto value = JsonValue::MakeObject();
    value.Set("schema", JsonValue::MakeString("21kb.lua-debug/v1"));
    value.Set("succeeded", JsonValue::MakeBool(succeeded));
    if (!error.empty()) value.Set("error", JsonValue::MakeString(std::move(error)));
    return value;
}
JsonValue Variable(const PucLuaDebugVariableSnapshot& variable) {
    auto value = JsonValue::MakeObject();
    value.Set("name", JsonValue::MakeString(variable.name));
    value.Set("value", JsonValue::MakeString(variable.value));
    value.Set("type", JsonValue::MakeNumber(static_cast<unsigned>(variable.type)));
    return value;
}
}
LuaDebugSession::~LuaDebugSession() { Disconnect(); }
void LuaDebugSession::Disconnect() noexcept {
    if (!connected_) return;
    runtime_.SetDebugSettings({.enableBreakpoints = false});
    runtime_.ResumeDebugExecution();
    runtime_.ClearDebugPause();
    connected_ = false;
}
JsonValue LuaDebugSession::Execute(std::string_view request) {
    if (request.size() > 65536U) return Reply(false, "debug request exceeds limit");
    JsonValue message;
    std::string error;
    if (!JsonValue::Parse(request, message, error) || message.GetKind() != JsonValue::Kind::Object)
        return Reply(false, "invalid debug JSON request");
    const auto* schema = message.Find("schema");
    const auto* commandValue = message.Find("command");
    if (!schema || schema->AsString() != "21kb.lua-debug/v1" || !commandValue || commandValue->GetKind() != JsonValue::Kind::String)
        return Reply(false, "unsupported debug schema or missing command");
    const auto& command = commandValue->AsString();
    if (command == "connect") {
        if (!connected_) runtime_.SetDebugSettings({});
        connected_ = true;
        return Reply(true);
    }
    if (!connected_) return Reply(false, "debug session is disconnected");
    if (command == "disconnect") { Disconnect(); return Reply(true); }
    if (command == "heartbeat") return Reply(true);
    if (command == "breakpoints") {
        const auto* entries = message.Find("breakpoints");
        if (!entries || entries->GetKind() != JsonValue::Kind::Array || entries->Size() > 1024U)
            return Reply(false, "breakpoints must be a bounded array");
        PucLuaDebugSettings settings;
        for (std::size_t index = 0; index < entries->Size(); ++index) {
            const auto* entry = entries->At(index);
            const auto* chunk = entry->Find("chunk");
            const auto* line = entry->Find("line");
            if (!chunk || chunk->GetKind() != JsonValue::Kind::String || chunk->AsString().size() > 1024U ||
                !line || line->GetKind() != JsonValue::Kind::Number || line->AsNumber() < 1.0 ||
                line->AsNumber() > std::numeric_limits<int>::max() || std::floor(line->AsNumber()) != line->AsNumber())
                return Reply(false, "invalid breakpoint location");
            settings.breakpoints.push_back({.chunkName = chunk->AsString(), .line = static_cast<int>(line->AsNumber())});
        }
        runtime_.SetDebugSettings(std::move(settings));
        return Reply(true);
    }
    if (command == "pause") { runtime_.RequestBreakOnNextLine(); return Reply(true); }
    if (command == "continue" || command == "step_into" || command == "step_over" || command == "step_out") {
        if (!runtime_.IsDebugPaused()) return Reply(false, "debugger is not paused");
        if (command == "continue") runtime_.ResumeDebugExecution();
        else if (command == "step_into") runtime_.RequestStepInto();
        else if (command == "step_over") runtime_.RequestStepOver();
        else runtime_.RequestStepOut();
        return Reply(true);
    }
    if (command == "evaluate") {
        const auto* expression = message.Find("expression");
        const auto* frame = message.Find("frame");
        if (!expression || expression->GetKind() != JsonValue::Kind::String ||
            (frame && (frame->GetKind() != JsonValue::Kind::Number || frame->AsNumber() < 0.0 ||
             frame->AsNumber() > 127.0 || std::floor(frame->AsNumber()) != frame->AsNumber())))
            return Reply(false, "invalid expression or frame");
        const auto evaluated = runtime_.EvaluateDebugExpression(expression->AsString(), frame ? static_cast<int>(frame->AsNumber()) : 0);
        auto reply = Reply(evaluated.succeeded, evaluated.error);
        if (evaluated.succeeded) reply.Set("result", Variable(evaluated.value));
        return reply;
    }
    if (command == "status" || command == "stack" || command == "locals") {
        auto reply = Reply(true);
        reply.Set("paused", JsonValue::MakeBool(runtime_.IsDebugPaused()));
        const auto pause = runtime_.InspectDebugPause();
        reply.Set("chunk", JsonValue::MakeString(pause.chunkName));
        reply.Set("line", JsonValue::MakeNumber(pause.line));
        auto stack = JsonValue::MakeArray();
        for (const auto& frame : pause.callStack) {
            auto item = JsonValue::MakeObject();
            item.Set("name", JsonValue::MakeString(frame.name));
            item.Set("chunk", JsonValue::MakeString(frame.chunkName));
            item.Set("line", JsonValue::MakeNumber(frame.line));
            auto locals = JsonValue::MakeArray();
            for (const auto& local : frame.locals) locals.Append(Variable(local));
            item.Set("locals", std::move(locals));
            stack.Append(std::move(item));
        }
        reply.Set("stack", std::move(stack));
        return reply;
    }
    return Reply(false, "unknown debug command");
}
} // namespace kb::script
