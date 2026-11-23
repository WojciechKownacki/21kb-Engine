#include "CliCaptureImage.hpp"
#include "CliCommands.hpp"
#include "CliProgram.hpp"

#include "engine/core/JsonValue.hpp"
#include "engine/project/AgentProcessMarker.hpp"

#if KB_CLI_RENDERER
#include <bimg/bimg.h>
#include <bimg/encode.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <bx/readerwriter.h>
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <stdlib.h>
#endif

// What an agent sees and undoes: a capture as an image and a summary, the undo point of its changes, Save.* in a headless run,
// platform builds that say what is missing, and C++ scripts that say they are for developers.
namespace {

namespace fs = std::filesystem;
using kb::core::JsonValue;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        std::fputs(message.c_str(), stderr);
        std::fputs("\n", stderr);
        std::exit(EXIT_FAILURE);
    }
}

[[nodiscard]] bool Contains(const std::string& text, std::string_view part) {
    return text.find(part) != std::string::npos;
}

struct Run {
    int exitCode = 0;
    std::string out;
    std::string err;
};

[[nodiscard]] Run Cli(const std::vector<std::string>& words) {
    std::istringstream in;
    std::ostringstream out;
    std::ostringstream err;
    const int exitCode = kb::cli::RunCli(words, in, kb::cli::CommandIo{ .out = out, .err = err });
    return Run{ exitCode, out.str(), err.str() };
}

[[nodiscard]] fs::path Root() {
    return fs::temp_directory_path() / "21kb_kb_cli_agent_tests";
}

[[nodiscard]] std::string Project(std::string_view name) {
    kb::cli::ResetCliConfinementForTests();
    std::error_code error;
    fs::remove_all(Root() / std::string{ name }, error);
    fs::create_directories(Root(), error);
    const Run made = Cli({ "new", "--parent", Root().string(), "--name", std::string{ name } });
    Require(made.exitCode == 0, "the project could not be made: " + made.out + made.err);
    return (Root() / std::string{ name }).string();
}

void Write(const fs::path& file, const std::string& text) {
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    std::ofstream output{ file, std::ios::binary | std::ios::trunc };
    output << text;
}

[[nodiscard]] std::string Read(const fs::path& file) {
    std::ifstream input{ file, std::ios::binary };
    return std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

// The MCP server's answer to one tools/call: the content blocks (type, text or image size) and whether it is an error.
struct McpAnswer {
    bool isError = false;
    std::string text;
    std::vector<std::string> types;
    std::size_t imageBytes = 0U;
};

[[nodiscard]] McpAnswer Mcp(const std::string& project, std::string_view call) {
    std::string input = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})";
    input += "\n{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":" + std::string{ call } + "}\n";
    std::istringstream in{ input };
    std::ostringstream out;
    std::ostringstream err;
    const std::vector<std::string> raw{ "--project", project };
    Require(kb::cli::RunMcpCommand(kb::cli::ArgumentList{ raw }, in, kb::cli::CommandIo{ .out = out, .err = err }) == 0, "the mcp server exited with an error");
    kb::cli::ResetCliConfinementForTests();
    std::istringstream lines{ out.str() };
    std::string line;
    std::string last;
    while (std::getline(lines, line)) last = line;
    JsonValue response;
    std::string parseError;
    Require(JsonValue::Parse(last, response, parseError), "the mcp answer must be JSON: " + last.substr(0U, 200U));
    const JsonValue* result = response.Find("result");
    Require(result != nullptr, "the mcp call must answer: " + last.substr(0U, 200U));
    McpAnswer answer;
    answer.isError = result->Find("isError")->AsBool();
    const JsonValue* content = result->Find("content");
    for (std::size_t index = 0U; index < content->Size(); ++index) {
        const JsonValue* block = content->At(index);
        const std::string type = block->Find("type")->AsString();
        answer.types.push_back(type);
        if (type == "text") answer.text += block->Find("text")->AsString();
        if (type == "image") {
            Require(block->Find("mimeType")->AsString() == "image/png", "an image block says it is a PNG");
            answer.imageBytes += block->Find("data")->AsString().size();
        }
    }
    return answer;
}

#if KB_CLI_RENDERER
// A PNG of one flat colour, or of a red square on grey.
void WritePng(const fs::path& file, unsigned width, unsigned height, bool flat) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4U);
    for (unsigned y = 0U; y < height; ++y) {
        for (unsigned x = 0U; x < width; ++x) {
            std::uint8_t* pixel = &pixels[(static_cast<std::size_t>(y) * width + x) * 4U];
            const bool square = !flat && x > width / 3U && x < width * 2U / 3U && y > height / 3U && y < height * 2U / 3U;
            pixel[0] = square ? 220 : 90;
            pixel[1] = square ? 30 : 100;
            pixel[2] = square ? 30 : 110;
            pixel[3] = 255;
        }
    }
    bx::DefaultAllocator allocator;
    bx::MemoryBlock block(&allocator);
    bx::MemoryWriter writer(&block);
    bx::Error error;
    bimg::imageWritePng(&writer, width, height, width * 4U, pixels.data(), bimg::TextureFormat::RGBA8, false, &error);
    const auto size = static_cast<std::size_t>(bx::seek(&writer, 0, bx::Whence::Current));
    Require(error.isOk() && size != 0U, "the test PNG could not be written");
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream output{ file, std::ios::binary | std::ios::trunc };
    output.write(static_cast<const char*>(block.more(0U)), static_cast<std::streamsize>(size));
}

// The picture a model gets: at most 640 wide, and a few lines that say whether the frame is blank and what colours it holds.
void RunCaptureDescriptionTests() {
    const fs::path blank = Root() / "pictures" / "blank.png";
    const fs::path scene = Root() / "pictures" / "scene.png";
    WritePng(blank, 1280U, 720U, true);
    WritePng(scene, 1280U, 720U, false);
    kb::cli::CaptureForModel shown;
    std::string error;
    Require(kb::cli::DescribeCapture(scene, 640U, shown, error), "a PNG must be described: " + error);
    Require(shown.width == 1280U && shown.height == 720U && shown.shownWidth == 640U && shown.shownHeight == 360U, "the picture is sent at 640 pixels wide");
    Require(!shown.blank && Contains(shown.summary, "capture 1280x720") && Contains(shown.summary, "mean #") && Contains(shown.summary, "dominant:") &&
            Contains(shown.summary, "red"),
        "the summary names the size, the mean colour and the dominant colours: " + shown.summary);
    Require(shown.png.size() > 8U && shown.png[1] == 'P' && shown.png[2] == 'N' && shown.png[3] == 'G', "the picture is a PNG");
    Require(shown.png.size() < 640U * 360U * 3U / 4U, "the picture is compressed: " + std::to_string(shown.png.size()) + " bytes");
    // What was sent can be read again, and says the same.
    const fs::path again = Root() / "pictures" / "again.png";
    {
        std::ofstream output{ again, std::ios::binary | std::ios::trunc };
        output.write(reinterpret_cast<const char*>(shown.png.data()), static_cast<std::streamsize>(shown.png.size()));
    }
    kb::cli::CaptureForModel reread;
    Require(kb::cli::DescribeCapture(again, 640U, reread, error) && reread.width == 640U && reread.height == 360U && Contains(reread.summary, "red"),
        "the compressed picture decodes to the same picture: " + error + reread.summary);
    Require(kb::cli::Base64Encode(std::vector<std::uint8_t>{ 'M', 'a', 'n' }) == "TWFu" && kb::cli::Base64Encode(std::vector<std::uint8_t>{ 'M', 'a' }) == "TWE=",
        "base64 must follow RFC 4648");
    kb::cli::CaptureForModel flat;
    Require(kb::cli::DescribeCapture(blank, 640U, flat, error) && flat.blank && Contains(flat.summary, "blank"), "a flat frame is called blank: " + flat.summary);
    Require(!kb::cli::DescribeCapture(Root() / "pictures" / "missing.png", 640U, flat, error) && !error.empty(), "a missing file is an error");
    Require(!kb::cli::CaptureSummary(scene).empty() && Contains(kb::cli::CaptureSummary(scene), "dominant"), "the command line prints the same summary");
}
#endif

// A capture named no file goes to Saved/Agent/Captures under a new name each time.
void RunCaptureNamingTests() {
    const std::string project = Project("CaptureNames");
    const fs::path first = kb::cli::NextCapturePath(project);
    Require(first.generic_string().ends_with("Saved/Agent/Captures/capture-1.png"), "the first capture is capture-1: " + first.generic_string());
    Write(first, "x");
    Require(kb::cli::NextCapturePath(project).filename() == "capture-2.png", "the next capture takes the next number");
    Require(kb::cli::NextCapturePath(project, { kb::cli::NextCapturePath(project) }).filename() == "capture-3.png", "a name taken in the same transaction is skipped");
    const Run help = Cli({ "help", "capture" });
    Require(Contains(help.out, "--camera") && Contains(help.out, "--view overview") && Contains(help.out, "--eye") && Contains(help.out, "Saved/Agent/Captures"),
        "capture help must name the camera, the free views and where a picture goes: " + help.out);
    const Run badView = Cli({ "capture", "--project", project, "--view", "sideways" });
    Require(badView.exitCode == 1, "an unknown view is an error");
}

void RunUndoCommandTests() {
    const std::string project = Project("UndoPoint");
    const fs::path root{ project };
    const Run none = Cli({ "undo", "--project", project, "--list" });
    Require(none.exitCode == 0 && Contains(none.out, "no undo point"), "without an undo point the list says so: " + none.out);
    Require(Cli({ "undo", "--project", project }).exitCode == 1 && Cli({ "undo", "--project", project, "--list", "--last" }).exitCode == 1, "exactly one of the actions is needed");
    const Run begun = Cli({ "undo", "--project", project, "--begin", "--label", "add a crate" });
    Require(begun.exitCode == 0 && Contains(begun.out, "undo point taken"), "an undo point is taken: " + begun.out + begun.err);
    Require(Contains(Cli({ "undo", "--project", project, "--list" }).out, "no changes since the undo point"), "nothing changed yet");

    const std::string sceneBefore = Read(root / "Assets" / "Scenes" / "Main.21kbscene");
    Require(Cli({ "entity", "add", "--project", project, "--create", "Cube", "--name", "Crate", "--color", "#E03C31" }).exitCode == 0, "the agent adds a crate");
    Write(root / "Assets" / "Scripts" / "Extra.lua", "function Tick(self, dt) end\n");
    const Run listed = Cli({ "undo", "--project", project, "--list", "--json" });
    Require(listed.exitCode == 0, "the list works: " + listed.err);
    const Run text = Cli({ "undo", "--project", project, "--list" });
    Require(Contains(text.out, "modified Assets/Scenes/Main.21kbscene") && Contains(text.out, "added Assets/Scripts/Extra.lua") &&
            Contains(text.out, "added Assets/Materials/Crate.kbmat") && Contains(text.out, "(add a crate)") && Contains(text.out, "next: undo --last"),
        "the list names every changed and added file and what to do next: " + text.out);
    Require(!Contains(text.out, project), "the list names no folder of this machine");

    // Undo of one file, then of the rest.
    Require(Cli({ "undo", "--project", project, "--last", "--file", "Assets/Scripts/Extra.lua" }).exitCode == 0 && !fs::exists(root / "Assets" / "Scripts" / "Extra.lua"),
        "one added file is taken away");
    const Run wrong = Cli({ "undo", "--project", project, "--last", "--file", "Assets/Scripts/Nope.lua" });
    Require(wrong.exitCode == 1 && Contains(wrong.err, "Assets/Scripts/Nope.lua"), "a file with no change is named in the error: " + wrong.err);
    const Run undone = Cli({ "undo", "--project", project, "--last" });
    Require(undone.exitCode == 0 && Contains(undone.out, "undone:"), "undo puts the rest back: " + undone.out + undone.err);
    Require(Read(root / "Assets" / "Scenes" / "Main.21kbscene") == sceneBefore, "the scene is as it was, byte for byte");
    Require(!fs::exists(root / "Assets" / "Materials" / "Crate.kbmat"), "the material the agent made is gone");
    Require(Contains(Cli({ "undo", "--project", project, "--last" }).out, "nothing to undo"), "a second undo has nothing to do");

    // A deleted file comes back, and keeping accepts what is there.
    Require(Cli({ "undo", "--project", project, "--begin" }).exitCode == 0, "a second undo point");
    Require(Cli({ "entity", "add", "--project", project, "--create", "Sphere", "--name", "Ball", "--color", "#2ECC40" }).exitCode == 0, "a second change");
    const std::string material = Read(root / "Assets" / "Materials" / "Ball.kbmat");
    Require(!material.empty(), "the second material exists");
    std::error_code error;
    fs::remove(root / "Assets" / "Materials" / "Ball.kbmat", error);
    Require(Contains(Cli({ "undo", "--project", project, "--list" }).out, "added Assets/Materials/Ball.kbmat") == false, "a file the agent added and deleted is no change");
    Require(Cli({ "undo", "--project", project, "--accept" }).exitCode == 0, "keeping closes the undo point");
    Require(Contains(Cli({ "undo", "--project", project, "--list" }).out, "no undo point with changes") || Contains(Cli({ "undo", "--project", project, "--list" }).out, "no changes"),
        "after keeping nothing is pending");

    // Through the agent's own tool: the same command.
    const McpAnswer viaMcp = Mcp(project, R"({"name":"kb_cli","arguments":{"args":["undo","--begin"]}})");
    Require(!viaMcp.isError && Contains(viaMcp.text, "undo point taken"), "undo works through mcp: " + viaMcp.text);
}

// The answers of one MCP server session to several tool calls, in order (text and whether each was an error).
[[nodiscard]] std::vector<std::pair<bool, std::string>> McpSession(const std::string& project, const std::vector<std::string>& calls) {
    std::string input = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})";
    input += "\n";
    for (std::size_t index = 0U; index < calls.size(); ++index) {
        input += "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(index + 2U) + ",\"method\":\"tools/call\",\"params\":" + calls[index] + "}\n";
    }
    std::istringstream in{ input };
    std::ostringstream out;
    std::ostringstream err;
    const std::vector<std::string> raw{ "--project", project };
    Require(kb::cli::RunMcpCommand(kb::cli::ArgumentList{ raw }, in, kb::cli::CommandIo{ .out = out, .err = err }) == 0, "the mcp server exited with an error");
    kb::cli::ResetCliConfinementForTests();
    std::vector<std::pair<bool, std::string>> answers;
    std::istringstream lines{ out.str() };
    std::string line;
    std::getline(lines, line); // the initialize answer
    while (std::getline(lines, line)) {
        JsonValue response;
        std::string parseError;
        Require(JsonValue::Parse(line, response, parseError) && response.Find("result") != nullptr, "the mcp answer must be JSON: " + line.substr(0U, 200U));
        const JsonValue* result = response.Find("result");
        std::string text;
        const JsonValue* content = result->Find("content");
        for (std::size_t block = 0U; block < content->Size(); ++block) text += content->At(block)->Find("text") != nullptr ? content->At(block)->Find("text")->AsString() : std::string{};
        answers.emplace_back(result->Find("isError")->AsBool(), text);
    }
    return answers;
}

// A command that fails the same way again is answered with a short hint to change course, in the same server session, whatever ran
// in between; a different command is a first failure, and one that works is a plain success.
void RunRepeatedFailureTests() {
    const std::string project = Project("RepeatedFailure");
    const std::string failing = R"({"name":"kb_cli","arguments":{"args":["material-show","--material","/Game/Materials/Nope.kbmat"]}})";
    const std::string other = R"({"name":"kb_cli","arguments":{"args":["material-show","--material","/Game/Materials/Other.kbmat"]}})";
    const std::string make = R"({"name":"kb_cli","arguments":{"args":["material-new","--name","Made"]}})";
    const std::string showMade = R"({"name":"kb_cli","arguments":{"args":["material-show","--material","/Game/Materials/Made.kbmat"]}})";
    const auto answers = McpSession(project, { failing, failing, other, failing, make, showMade });
    Require(answers.size() == 6U, "every call is answered");
    Require(answers[0].first && !Contains(answers[0].second, "This failed"), "the first failure is told in full: " + answers[0].second);
    Require(answers[1].first && Contains(answers[1].second, "This failed 2 times") && Contains(answers[1].second, "Change the arguments") &&
            Contains(answers[1].second, "stop and report") && answers[1].second.size() < answers[0].second.size() + 220U,
        "the second identical failure is answered with the hint: " + answers[1].second);
    Require(answers[2].first && !Contains(answers[2].second, "This failed"), "a different command is a first failure: " + answers[2].second);
    Require(answers[3].first && Contains(answers[3].second, "This failed 3 times"), "the hint counts the failures, whatever ran between them: " + answers[3].second);
    Require(!answers[4].first && !answers[5].first && !Contains(answers[5].second, "This failed"), "what works is a plain success: " + answers[5].second);
}

// A transaction whose process was cut off is put back by the next command in the project; a damaged marker is named, not thrown;
// a second transaction while the first runs is refused.
void RunInterruptedTransactionTests() {
    const std::string project = Project("CutOff");
    const fs::path root{ project };
    const fs::path marker = root / "Saved" / "Agent" / "transaction.json";
    Write(root / "Assets" / "Notes" / "keep.txt", "kept");
    const std::string sceneBefore = Read(root / "Assets" / "Scenes" / "Main.21kbscene");
    // The backup a transaction takes before it changes anything, and the marker of its process.
    const fs::path backup = Root() / "CutOffBackup";
    std::error_code error;
    fs::remove_all(backup, error);
    fs::copy(root, backup, fs::copy_options::recursive, error);
    Require(!error, "the backup is made: " + error.message());
    // The transaction changed files and was cut off.
    fs::remove(root / "Assets" / "Notes" / "keep.txt", error);
    Write(root / "Assets" / "Scenes" / "Main.21kbscene", "half written");
    Write(root / "Assets" / "Scripts" / "Extra.lua", "function Tick(self, dt) end\n");
    Write(marker, "{\"pid\":999999,\"started\":1,\"detail\":\"" + backup.generic_string() + "\"}");

    const Run next = Cli({ "scene-list", "--project", project });
    Require(Contains(next.err, "restored the project after a transaction of process 999999 was cut off"), "the next command says it put the project back: " + next.err);
    Require(Read(root / "Assets" / "Notes" / "keep.txt") == "kept" && Read(root / "Assets" / "Scenes" / "Main.21kbscene") == sceneBefore &&
            !fs::exists(root / "Assets" / "Scripts" / "Extra.lua"),
        "the deleted file is back, the changed one as it was, the added one gone");
    Require(!fs::exists(marker) && !fs::exists(backup), "the marker and the backup are gone");
    Require(!Contains(Cli({ "scene-list", "--project", project }).err, "restored"), "it is said once");

    // A marker nobody can read: one named line, the command still runs.
    Write(marker, "not json");
    const Run damaged = Cli({ "scene-list", "--project", project });
    Require(damaged.exitCode == 0 && Contains(damaged.err, "transaction marker Saved/Agent/transaction.json was damaged") && !fs::exists(marker),
        "a damaged marker is named and dropped: " + damaged.err);

    // A transaction of a process that runs (this one): a command goes on, a second transaction is refused.
    std::string markerError;
    Require(kb::project::WriteAgentProcessMarker(marker, "somewhere", markerError), "a live marker: " + markerError);
    Require(Cli({ "scene-list", "--project", project }).exitCode == 0 && fs::exists(marker), "an ordinary command leaves a live transaction alone");
    const McpAnswer refused = Mcp(project, R"({"name":"kb_cli","arguments":{"commands":[["entity","add","--create","Cube","--name","Crate"]]}})");
    Require(refused.isError && Contains(refused.text, "another transaction is running"), "a second transaction is refused: " + refused.text);
    Require(fs::exists(marker), "the first one's marker stays");
    fs::remove(marker, error);

    // A transaction that ends leaves no marker.
    const McpAnswer done = Mcp(project, R"({"name":"kb_cli","arguments":{"commands":[["entity","add","--create","Cube","--name","Crate"]]}})");
    Require(!done.isError && !fs::exists(marker), "a finished transaction leaves no marker: " + done.text);
    fs::remove_all(backup, error);
}

// editor play, stop, capture and frames: with no editor they say so and what to do instead; with one they wait for its answer.
void RunEditorControlTests() {
    const std::string project = Project("EditorControl");
    const fs::path root{ project };
    const Run capture = Cli({ "editor", "capture", "--project", project });
    Require(capture.exitCode == 1 && Contains(capture.err, "no editor took the capture request") && Contains(capture.err, "kb_cli capture --project"),
        "capture without an editor names the capture command: " + capture.err);
    const Run play = Cli({ "editor", "play", "--project", project });
    Require(play.exitCode == 1 && Contains(play.err, "no editor took the play request") && Contains(play.err, "kb_cli run --project"),
        "play without an editor names the headless run: " + play.err);
    Require(Cli({ "editor", "frames", "--project", project, "--seconds", "99" }).exitCode == 1, "the measurement has a limit");
    const fs::path requests = root / "Saved" / "Agent" / "EditorRequests";
    Require(!fs::exists(requests) || fs::is_empty(requests), "the withdrawn requests are gone");

    // An editor that answers: takes the request file and writes the reply the command waits for.
    std::thread editor([&root, &requests] {
        for (int waited = 0; waited < 100; ++waited) {
            std::error_code error;
            for (const fs::directory_entry& entry : fs::directory_iterator(requests, error)) {
                if (entry.path().extension() != ".json") continue;
                const std::string stamp = entry.path().stem().string();
                fs::remove(entry.path(), error);
                Write(root / "Saved" / "Agent" / "EditorReply" / (stamp + ".txt"), "ok\n120 frames in 3 s: 40.0 fps\n");
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{ 50 });
        }
    });
    const Run frames = Cli({ "editor", "frames", "--project", project, "--seconds", "3" });
    editor.join();
    Require(frames.exitCode == 0 && Contains(frames.out, "120 frames in 3 s: 40.0 fps"), "the editor's answer is the command's output: " + frames.out + frames.err);
}

// Save.* in a headless run: a second run reads what the first wrote.
void RunSaveRunTests() {
    const std::string project = Project("SaveRuns");
    Require(Cli({ "script-new", "--project", project, "--name", "RunCounter", "--language", "lua", "--code",
                  "function Created(self)\n    Save.Read(\"game\")\n    local runs = Save.GetInt(\"runs\").value + 1\n    Save.SetInt(\"runs\", runs)\n"
                  "    Save.Write(\"game\")\n    Log(\"runs \" .. runs)\nend\n" }).exitCode == 0, "the counter script");
    Require(Cli({ "scene-attach", "--project", project, "--node", "Plane", "--script", "/Game/Scripts/RunCounter.lua" }).exitCode == 0, "the attach");
    const Run first = Cli({ "run", "--project", project, "--frames", "3" });
    Require(first.exitCode == 0 && Contains(first.out, "runs 1"), "the first run counts one: " + first.out + first.err);
    const Run second = Cli({ "run", "--project", project, "--frames", "3" });
    Require(second.exitCode == 0 && Contains(second.out, "runs 2"), "the second run reads the slot of the first: " + second.out + second.err);
    Require(fs::exists(fs::path{ project } / "Saved" / "Saves"), "the slots are in the project's Saved folder");
    const Run fresh = Cli({ "run", "--project", project, "--frames", "3", "--fresh-saves" });
    Require(fresh.exitCode == 0 && Contains(fresh.out, "runs 1"), "--fresh-saves starts with no slot: " + fresh.out + fresh.err);
    const Run again = Cli({ "run", "--project", project, "--frames", "3" });
    Require(Contains(again.out, "runs 2"), "and the run after it keeps counting: " + again.out);
    // A deterministic run does not depend on an earlier one.
    const Run deterministic = Cli({ "run", "--project", project, "--frames", "3", "--deterministic" });
    Require(deterministic.exitCode == 0 && Contains(deterministic.out, "runs 1"), "a deterministic run starts with no saves: " + deterministic.out + deterministic.err);
}

// A platform build says what the machine lacks before it starts anything, and a request for the editor is withdrawn when none answers.
void RunPlatformBuildTests() {
    const std::string project = Project("PlatformBuild");
    const Run unknown = Cli({ "build", "--project", project, "--platform", "playstation", "--out", (Root() / "Packages").string() });
    Require(unknown.exitCode == 1 && Contains(unknown.err, "--platform takes windows, android, web or linux"), "an unknown platform is named: " + unknown.err);
    const Run web = Cli({ "build", "--project", project, "--platform", "web", "--out", (Root() / "Packages").string(), "--emsdk", (Root() / "NoEmsdk").string(),
        "--python", "NoSuchPython" });
    Require(web.exitCode == 1 && Contains(web.err, "cannot build for web on this machine") && Contains(web.err, "Emscripten SDK was not found") &&
            Contains(web.err, "Python was not found"),
        "a web build lists what to install: " + web.err);
    const Run linux = Cli({ "build", "--project", project, "--platform", "linux", "--out", (Root() / "Packages").string() });
    Require(linux.exitCode == 1 && Contains(linux.err, "built on a Linux machine over SSH") && Contains(linux.err, "--linux-host"), "a Linux build says what it needs: " + linux.err);
#ifdef _WIN32
    // Where this machine keeps its SDK and JDK is hidden, so the answer does not depend on what is installed.
    const char* const names[]{ "ANDROID_SDK_ROOT", "ANDROID_HOME", "JAVA_HOME", "USERPROFILE", "ProgramFiles", "LOCALAPPDATA", "PATH" };
    // The process's own variables, which the build reads (and sets), not the C runtime's copy.
    const auto environment = [](const char* name) {
        std::string text(32768U, 'x');
        const DWORD length = GetEnvironmentVariableA(name, text.data(), static_cast<DWORD>(text.size()));
        text.resize(length < text.size() ? length : 0U);
        return text;
    };
    std::vector<std::string> saved;
    for (const char* name : names) saved.push_back(environment(name));
    const std::string hidden = (Root() / "Hidden").string();
    std::error_code ignored;
    fs::remove_all(Root() / "Hidden", ignored);
    fs::create_directories(Root() / "Hidden");
    for (const char* name : names) _putenv_s(name, hidden.c_str());
    _putenv_s("PATH", hidden.c_str());
    const Run android = Cli({ "build", "--project", project, "--platform", "android", "--out", (Root() / "Packages").string() });
    Require(android.exitCode == 1 && Contains(android.err, "Android SDK was not found") && Contains(android.err, "no JDK 17 or newer was found"),
        "an Android build names the SDK and the JDK: " + android.err);
    // A JDK in a place JDK installers use is found although JAVA_HOME names nothing; the one that is too old is not taken.
    const fs::path jdks = Root() / "Hidden" / ".jdks";
    for (const auto& [folder, version] : { std::pair{ "temurin-21", "21.0.1" }, std::pair{ "old-8", "1.8.0_452" } }) {
        Write(jdks / folder / "bin" / "java.exe", "x");
        Write(jdks / folder / "release", std::string{ "JAVA_VERSION=\"" } + version + "\"\n");
    }
    const Run withJdk = Cli({ "build", "--project", project, "--platform", "android", "--out", (Root() / "Packages").string() });
    Require(withJdk.exitCode == 1 && !Contains(withJdk.err, "no JDK") && Contains(withJdk.err, "Android SDK was not found"), "a JDK in the usual place is found: " + withJdk.err);
    Require(fs::path{ environment("JAVA_HOME") }.lexically_normal() == (jdks / "temurin-21").lexically_normal(),
        "JAVA_HOME is set to the JDK found, not the old one: " + environment("JAVA_HOME"));
    for (std::size_t index = 0U; index < std::size(names); ++index) {
        _putenv_s(names[index], saved[index].c_str());
        SetEnvironmentVariableA(names[index], saved[index].empty() ? nullptr : saved[index].c_str());
    }
#endif
    const Run help = Cli({ "help", "build" });
    Require(Contains(help.out, "--platform android|web|linux") && Contains(help.out, "without the editor"), "build help names the platforms: " + help.out);

    // The editor takes a request in a second; with none running, the request is withdrawn and the command fails.
    const Run noEditor = Cli({ "editor", "build", "--project", project, "--target", "android" });
    Require(noEditor.exitCode == 1 && Contains(noEditor.err, "no editor took the build request") && Contains(noEditor.err, "--platform android"), "editor build says when no editor runs: " + noEditor.err + noEditor.out);
    Require(!fs::exists(fs::path{ project } / "Saved" / "Agent" / "EditorRequests") || fs::is_empty(fs::path{ project } / "Saved" / "Agent" / "EditorRequests"), "the withdrawn request is gone");
}

// C++ scripts say plainly that they are for developers, and fail at once without the SDK.
void RunNativeScriptNoticeTests() {
    const std::string project = Project("NativeNotice");
    const Run help = Cli({ "help", "script-new" });
    Require(Contains(help.out, "C++ is for developers") && Contains(help.out, "SDK") && Contains(help.out, "Lua"), "script-new help says who C++ is for: " + help.out);
    Require(Contains(Cli({ "help", "script-build" }).out, "for developers"), "script-build help too");
#ifdef _WIN32
    // A C++ script made where an SDK exists (this machine's own build tree), then the SDK taken away.
    const Run made = Cli({ "script-new", "--project", project, "--name", "Native", "--language", "cpp" });
    _putenv_s("KB_ENGINE_SDK_ROOT", (Root() / "NoSdkAnywhere").string().c_str());
    const Run created = Cli({ "script-new", "--project", project, "--name", "Other", "--language", "cpp" });
    Require(created.exitCode == 1 && Contains(created.err, "SDK was not found") && Contains(created.err, "for developers") && Contains(created.err, "Lua"),
        "script-new cpp without an SDK points to Lua: " + created.err);
    if (made.exitCode == 0) {
        const Run built = Cli({ "script-build", "--project", project });
        Require(built.exitCode == 1 && Contains(built.err, "SDK was not found") && Contains(built.err, "Lua"), "script-build without an SDK fails at once: " + built.err + built.out);
    }
    _putenv_s("KB_ENGINE_SDK_ROOT", "");
#endif
}

} // namespace

void RunAgentPackTests() {
#if KB_CLI_RENDERER
    RunCaptureDescriptionTests();
#endif
    RunCaptureNamingTests();
    RunUndoCommandTests();
    RunRepeatedFailureTests();
    RunInterruptedTransactionTests();
    RunEditorControlTests();
    RunSaveRunTests();
    RunPlatformBuildTests();
    RunNativeScriptNoticeTests();
}
