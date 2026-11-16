#include "CliCommands.hpp"
#include "CliLuaSourceIndex.hpp"
#include "CliProgram.hpp"
#include "CliProtocolStream.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/script/LuaDebugFileTransport.hpp"
#include "engine/script/ScriptValue.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

// The Debug Adapter Protocol for the live Lua debugger. The adapter translates an editor's debugger to the commands of
// the debugger endpoint of a game started with --lua-debug-dir (LuaDebugFileTransport.hpp); nothing else is added to it.
namespace kb::cli {
namespace {

using kb::core::JsonValue;

constexpr std::chrono::milliseconds kPollInterval{ 200 };
constexpr std::chrono::milliseconds kStatusTimeout{ 1000 };
constexpr std::chrono::milliseconds kCommandTimeout{ 5000 };
constexpr unsigned kMostTimeouts = 10U; // consecutive status requests the game may leave unanswered (a long frame)
constexpr int kThreadId = 1;            // the Lua scripts are one thread of the game for the editor
constexpr std::string_view kTimedOut = "debug request timed out";

[[nodiscard]] JsonValue Command(std::string_view name) {
    return Object({ { "schema", Str("21kb.lua-debug/v1") }, { "command", Str(std::string{ name }) } });
}

[[nodiscard]] bool Succeeded(const JsonValue& reply) {
    const JsonValue* succeeded = reply.Find("succeeded");
    return succeeded != nullptr && succeeded->AsBool();
}

[[nodiscard]] std::string ErrorOf(const JsonValue& reply) {
    const JsonValue* error = reply.Find("error");
    return IsString(error) ? error->AsString() : std::string{ "the debugger did not answer" };
}

struct Outcome {
    bool success = true;
    std::string message;
    JsonValue body; // null: the response has none
};

[[nodiscard]] Outcome Fail(std::string message) { return Outcome{ .success = false, .message = std::move(message) }; }
[[nodiscard]] Outcome Ok(JsonValue body = {}) { return Outcome{ .body = std::move(body) }; }

class DapAdapter final {
public:
    DapAdapter(std::filesystem::path project, std::ostream& out, std::ostream& err)
        : out_(out), err_(err) {
        if (!project.empty()) assets_ = (std::filesystem::absolute(project) / "Assets").lexically_normal();
    }

    ~DapAdapter() { Detach(); }
    DapAdapter(const DapAdapter&) = delete;
    DapAdapter& operator=(const DapAdapter&) = delete;

    // Serves until `disconnect` or the end of the input.
    int Run(std::istream& in) {
        UseBinaryStdio(in);
        std::string body;
        for (;;) {
            const FramedRead read = ReadFramedMessage(in, body);
            if (read == FramedRead::EndOfInput) break;
            if (read == FramedRead::Malformed) {
                err_ << "dap: ignored a message without a valid Content-Length header\n";
                continue;
            }
            JsonValue message;
            std::string error;
            if (!JsonValue::Parse(body, message, error) || message.GetKind() != JsonValue::Kind::Object) {
                err_ << "dap: ignored a message that is not a JSON object\n";
                continue;
            }
            if (Handle(message)) break;
        }
        Detach();
        return 0;
    }

private:
    // Returns true for `disconnect`.
    [[nodiscard]] bool Handle(const JsonValue& message) {
        const JsonValue* type = message.Find("type");
        const JsonValue* sequence = message.Find("seq");
        const JsonValue* command = message.Find("command");
        if (!IsString(type) || type->AsString() != "request") return false; // an event or a response of the editor: there are none to answer
        const double requestSequence = IsNumber(sequence) ? sequence->AsNumber() : 0.0;
        if (!IsString(command)) {
            Respond(requestSequence, {}, Fail("a request needs a command"));
            return false;
        }
        const std::string& name = command->AsString();
        const Outcome outcome = Dispatch(name, message.Find("arguments"));
        Respond(requestSequence, name, outcome);
        if (name == "initialize" && outcome.success) Emit(Object({ { "type", Str("event") }, { "event", Str("initialized") } }));
        return name == "disconnect";
    }

    [[nodiscard]] Outcome Dispatch(const std::string& name, const JsonValue* arguments) {
        if (name == "initialize") {
            return Ok(Object({ { "supportsConfigurationDoneRequest", Bool(true) }, { "supportsEvaluateForHovers", Bool(true) } }));
        }
        if (name == "launch") {
            return Fail("launch is not supported: start the game with --lua-debug-dir=<new folder> and attach to that folder");
        }
        if (name == "attach") return Attach(arguments);
        if (name == "disconnect" || name == "configurationDone" || name == "setExceptionBreakpoints") return Ok();
        if (!attached_) return Fail("the debugger is not attached: send attach with the folder of --lua-debug-dir first");
        if (name == "threads") return Ok(Object({ { "threads", Threads() } }));
        if (name == "setBreakpoints") return SetBreakpoints(arguments);
        if (name == "stackTrace") return StackTrace();
        if (name == "scopes") return Scopes(arguments);
        if (name == "variables") return Variables(arguments);
        if (name == "evaluate") return Evaluate(arguments);
        if (name == "continue") return Resume("continue", "breakpoint", Object({ { "allThreadsContinued", Bool(true) } }));
        if (name == "next") return Resume("step_over", "step", {});
        if (name == "stepIn") return Resume("step_into", "step", {});
        if (name == "stepOut") return Resume("step_out", "step", {});
        if (name == "pause") return Resume("pause", "pause", {});
        return Fail("the request is not supported: " + name);
    }

    [[nodiscard]] static JsonValue Threads() {
        JsonValue threads = JsonValue::MakeArray();
        threads.Append(Object({ { "id", Num(kThreadId) }, { "name", Str("Lua") } }));
        return threads;
    }

    // ---- sending -------------------------------------------------------------------------------------------------

    void Emit(JsonValue message) {
        const std::lock_guard lock{ outputMutex_ };
        message.Set("seq", Num(++sequence_));
        WriteFramedMessage(out_, message);
    }

    void Respond(double requestSequence, std::string_view command, const Outcome& outcome) {
        JsonValue response = Object({ { "type", Str("response") }, { "request_seq", Num(requestSequence) }, { "success", Bool(outcome.success) },
            { "command", Str(std::string{ command }) } });
        if (!outcome.success) response.Set("message", Str(outcome.message));
        else if (!outcome.body.IsNull()) response.Set("body", outcome.body);
        Emit(std::move(response));
    }

    void EmitEvent(std::string name, JsonValue body) {
        Emit(Object({ { "type", Str("event") }, { "event", Str(std::move(name)) }, { "body", std::move(body) } }));
    }

    // One request to the game's debugger. The game answers one at a time, and the stopped state the poll and the
    // commands keep is changed by both, so they take the same lock for the whole request.
    [[nodiscard]] JsonValue Send(const JsonValue& request, std::chrono::milliseconds timeout) const {
        return kb::script::SendLuaDebugFileRequest(endpoint_, request.Dump(), timeout);
    }

    [[nodiscard]] JsonValue Ask(const JsonValue& request) {
        const std::lock_guard lock{ debuggeeMutex_ };
        return Send(request, kCommandTimeout);
    }

    // ---- requests ------------------------------------------------------------------------------------------------

    [[nodiscard]] Outcome Attach(const JsonValue* arguments) {
        const JsonValue* endpoint = Member(arguments, "endpoint");
        if (!IsString(endpoint)) return Fail("attach needs arguments.endpoint: the folder the game was started with in --lua-debug-dir");
        if (attached_) return Fail("the debugger is attached already");
        endpoint_ = std::filesystem::path{ endpoint->AsString() };
        const JsonValue reply = Send(Command("connect"), kCommandTimeout);
        if (!Succeeded(reply)) return Fail("cannot attach: " + ErrorOf(reply));
        attached_ = true;
        poller_ = std::thread{ [this] { PollLoop(); } };
        return Ok();
    }

    // The name the debugger knows a script file by: its path in the project's Assets folder (a suffix of the
    // /Game/... path the engine gives the script), else the file name.
    [[nodiscard]] std::string ChunkOf(const std::filesystem::path& source) const {
        if (!assets_.empty()) {
            const std::filesystem::path relative = source.lexically_normal().lexically_relative(assets_);
            if (!relative.empty() && *relative.begin() != "..") return relative.generic_string();
        }
        return source.filename().generic_string();
    }

    // The file of a script the debugger names: in the project's Assets folder when it is there.
    [[nodiscard]] std::filesystem::path PathOfChunk(std::string_view chunk) const {
        std::string name{ chunk };
        if (name.starts_with("/Game/")) name.erase(0U, 6U);
        std::error_code error;
        if (!assets_.empty() && std::filesystem::is_regular_file(assets_ / name, error)) return assets_ / name;
        return std::filesystem::path{ name };
    }

    // Sets the breakpoints of one file; the debugger takes them all at once, so every file's are sent again.
    [[nodiscard]] Outcome SetBreakpoints(const JsonValue* arguments) {
        const JsonValue* path = Member(Member(arguments, "source"), "path");
        const JsonValue* requested = Member(arguments, "breakpoints");
        if (!IsString(path) || requested == nullptr || requested->GetKind() != JsonValue::Kind::Array) {
            return Fail("setBreakpoints needs source.path and breakpoints");
        }
        // A breakpoint is verified when the script has code on its line, which is where the line hook can stop.
        std::vector<std::uint32_t> codeLines;
        std::ifstream input{ std::filesystem::path{ path->AsString() }, std::ios::binary };
        const bool readable = input.is_open();
        if (readable) codeLines = IndexLuaSource(std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} }).codeLines;
        JsonValue answers = JsonValue::MakeArray();
        std::vector<int> lines;
        for (std::size_t index = 0U; index < requested->Size(); ++index) {
            const JsonValue* line = Member(requested->At(index), "line");
            if (!IsNumber(line) || line->AsNumber() < 1.0 || line->AsNumber() > 2147483647.0) return Fail("a breakpoint needs a line of 1 or more");
            const int number = static_cast<int>(line->AsNumber());
            const bool verified = readable && std::ranges::binary_search(codeLines, static_cast<std::uint32_t>(number - 1));
            JsonValue answer = Object({ { "verified", Bool(verified) }, { "line", Num(number) } });
            if (!verified) answer.Set("message", Str(readable ? "there is no code on this line" : "the source file could not be read"));
            answers.Append(std::move(answer));
            if (verified) lines.push_back(number);
        }
        breakpoints_[ChunkOf(std::filesystem::path{ path->AsString() })] = std::move(lines);
        JsonValue all = JsonValue::MakeArray();
        for (const auto& [chunk, chunkLines] : breakpoints_) {
            for (const int line : chunkLines) all.Append(Object({ { "chunk", Str(chunk) }, { "line", Num(line) } }));
        }
        JsonValue request = Command("breakpoints");
        request.Set("breakpoints", std::move(all));
        const JsonValue reply = Ask(request);
        if (!Succeeded(reply)) return Fail(ErrorOf(reply));
        return Ok(Object({ { "breakpoints", std::move(answers) } }));
    }

    // The frames of the stopped script, innermost first; fails while the script runs.
    [[nodiscard]] Outcome Frames(JsonValue& stack) {
        const JsonValue reply = Ask(Command("stack"));
        if (!Succeeded(reply)) return Fail(ErrorOf(reply));
        const JsonValue* paused = reply.Find("paused");
        const JsonValue* frames = reply.Find("stack");
        if (paused == nullptr || !paused->AsBool() || frames == nullptr) return Fail("the script is not stopped");
        stack = *frames;
        return Ok();
    }

    [[nodiscard]] Outcome StackTrace() {
        JsonValue stack;
        if (Outcome failed = Frames(stack); !failed.success) return failed;
        JsonValue frames = JsonValue::MakeArray();
        for (std::size_t index = 0U; index < stack.Size(); ++index) {
            const JsonValue& frame = *stack.At(index);
            const std::filesystem::path file = PathOfChunk(Member(&frame, "chunk") != nullptr ? Member(&frame, "chunk")->AsString() : std::string{});
            frames.Append(Object({
                { "id", Num(static_cast<double>(index)) },
                { "name", Str(IsString(Member(&frame, "name")) ? Member(&frame, "name")->AsString() : std::string{ "?" }) },
                { "source", Object({ { "name", Str(file.filename().string()) }, { "path", Str(file.string()) } }) },
                { "line", Num(IsNumber(Member(&frame, "line")) ? Member(&frame, "line")->AsNumber() : 0.0) },
                { "column", Num(1) },
            }));
        }
        return Ok(Object({ { "stackFrames", std::move(frames) }, { "totalFrames", Num(static_cast<double>(stack.Size())) } }));
    }

    // Each frame has one scope, its locals; the scope's reference is the frame's number plus one (zero means none).
    [[nodiscard]] static Outcome Scopes(const JsonValue* arguments) {
        const JsonValue* frame = Member(arguments, "frameId");
        if (!IsNumber(frame) || frame->AsNumber() < 0.0) return Fail("scopes needs a frameId");
        JsonValue scopes = JsonValue::MakeArray();
        scopes.Append(Object({ { "name", Str("Locals") }, { "variablesReference", Num(frame->AsNumber() + 1.0) }, { "expensive", Bool(false) } }));
        return Ok(Object({ { "scopes", std::move(scopes) } }));
    }

    [[nodiscard]] Outcome Variables(const JsonValue* arguments) {
        const JsonValue* reference = Member(arguments, "variablesReference");
        if (!IsNumber(reference) || reference->AsNumber() < 1.0) return Fail("variables needs a variablesReference from scopes");
        JsonValue stack;
        if (Outcome failed = Frames(stack); !failed.success) return failed;
        const auto frame = static_cast<std::size_t>(reference->AsNumber()) - 1U;
        if (frame >= stack.Size()) return Fail("the frame does not exist");
        const JsonValue* locals = Member(stack.At(frame), "locals");
        JsonValue variables = JsonValue::MakeArray();
        for (std::size_t index = 0U; locals != nullptr && index < locals->Size(); ++index) {
            variables.Append(Variable(*locals->At(index)));
        }
        return Ok(Object({ { "variables", std::move(variables) } }));
    }

    // A variable or result of the debugger as DAP shows it: tables and functions are named by their type.
    [[nodiscard]] static JsonValue Variable(const JsonValue& value) {
        const JsonValue* name = Member(&value, "name");
        const JsonValue* text = Member(&value, "value");
        const JsonValue* type = Member(&value, "type");
        JsonValue variable = Object({ { "name", Str(IsString(name) ? name->AsString() : std::string{}) }, { "value", Str(IsString(text) ? text->AsString() : std::string{}) },
            { "variablesReference", Num(0) } });
        if (IsNumber(type)) variable.Set("type", Str(kb::script::ToString(static_cast<kb::script::ScriptValueType>(static_cast<int>(type->AsNumber())))));
        return variable;
    }

    [[nodiscard]] Outcome Evaluate(const JsonValue* arguments) {
        const JsonValue* expression = Member(arguments, "expression");
        const JsonValue* frame = Member(arguments, "frameId");
        if (!IsString(expression)) return Fail("evaluate needs an expression");
        JsonValue request = Command("evaluate");
        request.Set("expression", Str(expression->AsString()));
        request.Set("frame", Num(IsNumber(frame) ? frame->AsNumber() : 0.0));
        const JsonValue reply = Ask(request);
        if (!Succeeded(reply)) return Fail(ErrorOf(reply));
        const JsonValue* result = reply.Find("result");
        if (result == nullptr) return Fail("the debugger gave no result");
        JsonValue value = Variable(*result);
        const JsonValue* text = value.Find("value");
        JsonValue body = Object({ { "result", *text }, { "variablesReference", Num(0) } });
        if (const JsonValue* type = value.Find("type")) body.Set("type", *type);
        return Ok(std::move(body));
    }

    // continue, the steps and pause. The next stop is reported with the reason that asked for it.
    [[nodiscard]] Outcome Resume(std::string_view command, std::string reason, JsonValue body) {
        const std::lock_guard lock{ debuggeeMutex_ };
        const JsonValue reply = Send(Command(command), kCommandTimeout);
        if (!Succeeded(reply)) return Fail(ErrorOf(reply));
        if (command != "pause") stopped_ = false;
        stopReason_ = std::move(reason);
        return Ok(std::move(body));
    }

    // ---- the game's state ----------------------------------------------------------------------------------------

    // Asks the game whether it is stopped, to tell the editor when it stops and when it ends. The requests also keep
    // the session, which the game drops after thirty seconds of silence.
    void PollLoop() {
        unsigned timeouts = 0U;
        for (;;) {
            {
                std::unique_lock lock{ wakeMutex_ };
                if (wake_.wait_for(lock, kPollInterval, [this] { return stopping_; })) return;
            }
            const std::lock_guard lock{ debuggeeMutex_ };
            const JsonValue reply = Send(Command("status"), kStatusTimeout);
            if (!Succeeded(reply)) {
                if (ErrorOf(reply) == kTimedOut && ++timeouts < kMostTimeouts) continue;
                EmitEvent("terminated", JsonValue::MakeObject());
                return;
            }
            timeouts = 0U;
            const JsonValue* paused = reply.Find("paused");
            const bool nowStopped = paused != nullptr && paused->AsBool();
            if (nowStopped && !stopped_) {
                EmitEvent("stopped", Object({ { "reason", Str(stopReason_) }, { "threadId", Num(kThreadId) }, { "allThreadsStopped", Bool(true) } }));
                stopReason_ = "breakpoint";
            }
            stopped_ = nowStopped;
        }
    }

    // Ends the polling and lets the game go: no breakpoints, and the script it holds runs on.
    void Detach() {
        {
            const std::lock_guard lock{ wakeMutex_ };
            stopping_ = true;
        }
        wake_.notify_all();
        if (poller_.joinable()) poller_.join();
        if (attached_) {
            static_cast<void>(Ask(Command("disconnect")));
            attached_ = false;
        }
    }

    std::ostream& out_;
    std::ostream& err_;
    std::filesystem::path assets_; // the project's Assets folder, or empty
    std::filesystem::path endpoint_;
    std::map<std::string, std::vector<int>> breakpoints_; // by script name
    bool attached_ = false;

    std::mutex outputMutex_;
    double sequence_ = 0.0;

    std::mutex debuggeeMutex_; // one request to the game at a time; guards stopped_ and stopReason_
    bool stopped_ = false;     // the editor was told the script stopped, and no continue or step has gone since
    std::string stopReason_ = "breakpoint";

    std::mutex wakeMutex_;
    std::condition_variable wake_;
    bool stopping_ = false;
    std::thread poller_;
};

} // namespace

int RunDapCommand(const ArgumentList& arguments, std::istream& in, CommandIo io) {
    constexpr std::array<std::string_view, 1> allowed{ "--project" };
    if (!arguments.Errors().empty() || !arguments.UnknownOptions(allowed).empty() || !arguments.Positionals().empty() || arguments.Options("--project").size() > 1U) {
        io.err << "error: dap expects [--project <dir>]\n";
        return 1;
    }
    const std::optional<std::string> project = arguments.Option("--project");
    DapAdapter adapter{ project ? std::filesystem::path{ *project } : std::filesystem::path{}, io.out, io.err };
    return adapter.Run(in);
}

} // namespace kb::cli
