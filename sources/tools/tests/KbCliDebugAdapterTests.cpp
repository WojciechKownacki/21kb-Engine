#include "CliCommands.hpp"
#include "CliProtocolStream.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/script/LuaDebugFileTransport.hpp"
#include "engine/script/LuaScriptBackend.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"
#include "engine/script/ScriptLifecycle.hpp"
#include "engine/script/ScriptRuntime.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// The debug adapter of `kb_cli dap`, spoken to through blocking streams in memory, against a script runtime and its
// debugger endpoint on a thread of their own, as a game has them.
namespace {

using kb::cli::Num;
using kb::cli::Object;
using kb::cli::Str;
using kb::core::JsonValue;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        std::fputs(message.c_str(), stderr);
        std::fputs("\n", stderr);
        std::exit(EXIT_FAILURE);
    }
}

[[nodiscard]] std::filesystem::path Root() { return std::filesystem::temp_directory_path() / "21kb_engine_kb_cli_dap_tests"; }

[[nodiscard]] std::string Frame(std::string_view body) {
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + std::string{ body };
}

// What the editor writes: the adapter reads it, and waits while there is nothing yet.
class EditorToAdapter final : public std::streambuf {
public:
    void Write(std::string_view bytes) {
        const std::lock_guard lock{ mutex_ };
        pending_ += bytes;
        changed_.notify_all();
    }

    void Close() {
        const std::lock_guard lock{ mutex_ };
        closed_ = true;
        changed_.notify_all();
    }

protected:
    int_type underflow() override {
        std::unique_lock lock{ mutex_ };
        changed_.wait(lock, [this] { return !pending_.empty() || closed_; });
        if (pending_.empty()) return traits_type::eof();
        taken_ = std::move(pending_);
        pending_.clear();
        setg(taken_.data(), taken_.data(), taken_.data() + taken_.size());
        return traits_type::to_int_type(taken_.front());
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::string pending_;
    std::string taken_;
    bool closed_ = false;
};

// What the adapter writes: the editor takes the frames from it as they arrive.
class AdapterToEditor final : public std::streambuf {
public:
    // The first message after the last one found for which `matches` is true; none when `timeout` passes first.
    [[nodiscard]] std::optional<JsonValue> Await(const std::function<bool(const JsonValue&)>& matches, std::chrono::milliseconds timeout = std::chrono::seconds{ 10 }) {
        std::unique_lock lock{ mutex_ };
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            ParseFrames();
            for (; cursor_ < frames_.size(); ++cursor_) {
                if (matches(frames_[cursor_])) return frames_[cursor_++];
            }
            if (changed_.wait_until(lock, deadline) == std::cv_status::timeout) return std::nullopt;
        }
    }

protected:
    int_type overflow(int_type c) override {
        if (c != traits_type::eof()) Append(std::string(1U, static_cast<char>(c)));
        return traits_type::not_eof(c);
    }

    std::streamsize xsputn(const char* data, std::streamsize count) override {
        Append(std::string{ data, static_cast<std::size_t>(count) });
        return count;
    }

private:
    void Append(const std::string& bytes) {
        const std::lock_guard lock{ mutex_ };
        bytes_ += bytes;
        changed_.notify_all();
    }

    void ParseFrames() {
        constexpr std::string_view kHeader = "Content-Length: ";
        for (;;) {
            const std::size_t headerEnd = bytes_.find("\r\n\r\n", parsed_);
            if (headerEnd == std::string::npos) return;
            Require(bytes_.compare(parsed_, kHeader.size(), kHeader) == 0, "the adapter wrote something that is not a frame");
            const std::size_t length = std::stoul(bytes_.substr(parsed_ + kHeader.size(), headerEnd - parsed_ - kHeader.size()));
            if (bytes_.size() < headerEnd + 4U + length) return;
            JsonValue frame;
            std::string error;
            Require(JsonValue::Parse(std::string_view{ bytes_ }.substr(headerEnd + 4U, length), frame, error), "a frame is not valid JSON: " + error);
            frames_.push_back(std::move(frame));
            parsed_ = headerEnd + 4U + length;
        }
    }

    std::mutex mutex_;
    std::condition_variable changed_;
    std::string bytes_;
    std::size_t parsed_ = 0U;
    std::vector<JsonValue> frames_;
    std::size_t cursor_ = 0U;
};

// The game: a script runtime that runs a behaviour every tick on its own thread, with the debugger endpoint. It stops
// ticking, and removes the endpoint, when the script has run to its end.
class Game final {
public:
    Game(const std::filesystem::path& endpoint, const std::string& script) : thread_{ [this, endpoint, script] { Run(endpoint, script); } } {
        while (!ready_) std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
    }

    ~Game() {
        stop_ = true;
        thread_.join();
    }

    void Start() { running_ = true; }

private:
    void Run(const std::filesystem::path& endpoint, const std::string& script) {
        constexpr kb::assets::AssetId kProbe{ 4101U };
        kb::script::PucLuaScriptRuntime luaRuntime;
        kb::scene::Scene scene;
        kb::script::ScriptRuntime dispatcher;
        Require(dispatcher.RegisterBackend(std::make_unique<kb::script::LuaScriptBackend>(luaRuntime)), "the Lua backend did not register");
        Require(luaRuntime.LoadScript(kProbe, script, "Probe.lua").succeeded, "the debugged script did not load");
        const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Debugged" });
        scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
            .behaviourAssetId = kProbe.value, .backend = kb::scene::BehaviourBackend::Lua, .enabled = true });
        kb::script::LuaDebugFileServer server{ luaRuntime };
        std::string error;
        Require(server.Start(endpoint, error, std::chrono::seconds{ 30 }), "the debugger endpoint did not start: " + error);
        ready_ = true;
        while (!stop_) {
            server.Poll();
            if (running_) {
                Require(dispatcher.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.016F).Succeeded(), "the script failed");
                if (dispatcher.SharedState().Get("probe.done").has_value()) {
                    server.Stop(); // the game ends
                    return;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{ 2 });
        }
    }

    std::atomic<bool> ready_{ false };
    std::atomic<bool> running_{ false };
    std::atomic<bool> stop_{ false };
    std::thread thread_;
};

// The editor's side of the conversation.
struct Editor {
    EditorToAdapter toAdapter;
    AdapterToEditor fromAdapter;
    int sequence = 0;

    // Sends a request and returns the adapter's response to it.
    [[nodiscard]] JsonValue Call(std::string_view command, JsonValue arguments = JsonValue::MakeObject()) {
        const int number = ++sequence;
        JsonValue request = Object({ { "seq", Num(number) }, { "type", Str("request") }, { "command", Str(std::string{ command }) } });
        request.Set("arguments", std::move(arguments));
        toAdapter.Write(Frame(request.Dump()));
        const std::optional<JsonValue> response = fromAdapter.Await([&](const JsonValue& message) {
            return message.Find("type")->AsString() == "response" && message.Find("request_seq")->AsNumber() == number;
        });
        Require(response.has_value(), "the adapter did not answer " + std::string{ command });
        Require(response->Find("command")->AsString() == command, "a response names the command it answers");
        return *response;
    }

    // The next event with that name.
    [[nodiscard]] JsonValue Event(std::string_view name) {
        const std::optional<JsonValue> event = fromAdapter.Await([&](const JsonValue& message) {
            return message.Find("type")->AsString() == "event" && message.Find("event")->AsString() == name;
        });
        Require(event.has_value(), "the adapter did not send " + std::string{ name });
        return *event;
    }
};

[[nodiscard]] bool Succeeded(const JsonValue& response) { return response.Find("success")->AsBool(); }

[[nodiscard]] JsonValue Source(const std::filesystem::path& file) {
    return Object({ { "path", Str(file.string()) } });
}

[[nodiscard]] JsonValue Frames(Editor& editor) {
    const JsonValue trace = editor.Call("stackTrace", Object({ { "threadId", Num(1) } }));
    Require(Succeeded(trace), "stackTrace failed: " + trace.Dump());
    return *trace.Find("body")->Find("stackFrames");
}

} // namespace

void RunDebugAdapterTests() {
    std::error_code ignored;
    std::filesystem::remove_all(Root(), ignored);
    const std::filesystem::path script = Root() / "Assets" / "Probe.lua";
    std::filesystem::create_directories(script.parent_path());
    // Line 5 is empty: no breakpoint can stop there.
    const std::string text = "function Tick(self, dt)\n    local counter = 40\n    local total = counter + 2\n    SetShared('probe.total', total)\n\n    SetShared('probe.done', 1)\nend\n";
    {
        std::ofstream output{ script, std::ios::binary };
        output << text;
        Require(output.good(), "the debugged script could not be written");
    }

    Game game{ Root() / "endpoint", text };
    Editor editor;
    std::istream input{ &editor.toAdapter };
    std::ostream output{ &editor.fromAdapter };
    std::ostringstream errors;
    int exitCode = -1;
    const std::vector<std::string> arguments{ "--project", Root().string() };
    std::thread adapter{ [&] {
        exitCode = kb::cli::RunDapCommand(kb::cli::ArgumentList{ arguments }, input, kb::cli::CommandIo{ .out = output, .err = errors });
    } };

    const JsonValue initialized = editor.Call("initialize", Object({ { "adapterID", Str("kb_cli") } }));
    Require(Succeeded(initialized) && initialized.Find("body")->Find("supportsConfigurationDoneRequest")->AsBool(), "initialize must offer configurationDone");
    static_cast<void>(editor.Event("initialized"));

    // Requests that cannot be served are answered, and the adapter goes on.
    Require(!Succeeded(editor.Call("setBreakpoints", Object({ { "source", Source(script) } }))), "a request before attach must fail");
    editor.toAdapter.Write("Content-Type: application/json\r\n\r\n" + Frame("{broken") + Frame("[1]"));
    editor.toAdapter.Write(Frame("{\"seq\":90,\"type\":\"request\"}"));
    Require(editor.fromAdapter.Await([](const JsonValue& message) { return message.Find("request_seq") != nullptr && message.Find("request_seq")->AsNumber() == 90.0 && !message.Find("success")->AsBool(); }).has_value(),
        "a request without a command is answered with a failure");
    Require(!Succeeded(editor.Call("frobnicate")), "an unknown request must fail");
    const JsonValue launch = editor.Call("launch");
    Require(!Succeeded(launch) && launch.Find("message")->AsString().find("attach") != std::string::npos, "launch names attach as the way in");
    Require(!Succeeded(editor.Call("attach", Object({ { "endpoint", Str((Root() / "nobody-listens").string()) } }))), "attach to a folder without a game must fail");
    Require(!Succeeded(editor.Call("attach")), "attach needs its endpoint");

    Require(Succeeded(editor.Call("attach", Object({ { "endpoint", Str((Root() / "endpoint").string()) } }))), "attach to the game's endpoint failed");
    JsonValue points = JsonValue::MakeArray();
    points.Append(Object({ { "line", Num(3) } }));
    points.Append(Object({ { "line", Num(5) } }));
    const JsonValue set = editor.Call("setBreakpoints", Object({ { "source", Source(script) }, { "breakpoints", std::move(points) } }));
    Require(Succeeded(set), "setBreakpoints failed: " + set.Dump());
    const JsonValue& answers = *set.Find("body")->Find("breakpoints");
    Require(answers.Size() == 2U && answers.At(0U)->Find("verified")->AsBool() && answers.At(0U)->Find("line")->AsNumber() == 3.0 && !answers.At(1U)->Find("verified")->AsBool(),
        "the breakpoint on code is verified and the one on the empty line is not: " + answers.Dump());
    Require(Succeeded(editor.Call("configurationDone")), "configurationDone failed");

    game.Start();
    const JsonValue stopped = editor.Event("stopped");
    Require(stopped.Find("body")->Find("reason")->AsString() == "breakpoint" && stopped.Find("body")->Find("threadId")->AsNumber() == 1.0, "the script stops at the breakpoint");

    const JsonValue frames = Frames(editor);
    Require(frames.Size() >= 1U && frames.At(0U)->Find("line")->AsNumber() == 3.0, "the top frame is on the breakpoint's line: " + frames.Dump());
    Require(std::filesystem::path{ frames.At(0U)->Find("source")->Find("path")->AsString() }.lexically_normal() == script.lexically_normal(), "the frame is in the script's file");

    const JsonValue scopes = editor.Call("scopes", Object({ { "frameId", Num(0) } }));
    Require(Succeeded(scopes), "scopes failed");
    const JsonValue variables = editor.Call("variables", Object({ { "variablesReference", *scopes.Find("body")->Find("scopes")->At(0U)->Find("variablesReference") } }));
    Require(Succeeded(variables), "variables failed: " + variables.Dump());
    std::string counter;
    for (std::size_t index = 0U; index < variables.Find("body")->Find("variables")->Size(); ++index) {
        const JsonValue& variable = *variables.Find("body")->Find("variables")->At(index);
        if (variable.Find("name")->AsString() == "counter") counter = variable.Find("value")->AsString();
    }
    Require(counter == "40", "the local variable has its value: " + variables.Dump());

    const JsonValue evaluated = editor.Call("evaluate", Object({ { "expression", Str("counter + 2") }, { "frameId", Num(0) } }));
    Require(Succeeded(evaluated) && evaluated.Find("body")->Find("result")->AsString() == "42", "evaluate gives the value of the expression: " + evaluated.Dump());
    Require(!Succeeded(editor.Call("evaluate", Object({ { "expression", Str("counter +") } }))), "an expression with an error fails and does not end the session");

    Require(Succeeded(editor.Call("next")), "next failed");
    Require(editor.Event("stopped").Find("body")->Find("reason")->AsString() == "step", "a step stops with the reason step");
    Require(Frames(editor).At(0U)->Find("line")->AsNumber() == 4.0, "next stops on the next line");

    Require(Succeeded(editor.Call("continue")), "continue failed");
    static_cast<void>(editor.Event("terminated"));
    Require(!Succeeded(editor.Call("stackTrace", Object({ { "threadId", Num(1) } }))), "the ended game has no stack");

    Require(Succeeded(editor.Call("disconnect")), "disconnect failed");
    editor.toAdapter.Close();
    adapter.join();
    Require(exitCode == 0, "the adapter must end with 0 after disconnect");

    std::istringstream nothing;
    std::ostringstream out;
    std::ostringstream err;
    const std::vector<std::string> unknown{ "--nonsense" };
    Require(kb::cli::RunDapCommand(kb::cli::ArgumentList{ unknown }, nothing, kb::cli::CommandIo{ .out = out, .err = err }) == 1 && out.str().empty(), "dap refuses an option it does not have");
    std::filesystem::remove_all(Root(), ignored);
}
