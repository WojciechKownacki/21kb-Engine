#include "CliCommands.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/script/ScriptApiCatalog.hpp"
#include "engine/script/ScriptLanguageSubset.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The language server of `kb_cli lsp`, spoken to through streams in memory.
namespace {

using kb::core::JsonValue;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        std::fputs(message.c_str(), stderr);
        std::fputs("\n", stderr);
        std::exit(EXIT_FAILURE);
    }
}

[[nodiscard]] std::filesystem::path Root() { return std::filesystem::temp_directory_path() / "21kb_engine_kb_cli_lsp_tests"; }

void WriteFile(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output << text;
    Require(output.good(), "lsp test file could not be written");
}

[[nodiscard]] std::string UriOf(const std::filesystem::path& path) {
    const std::string text = path.generic_string();
    std::string uri = text.front() == '/' ? "file://" : "file:///";
    for (const char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0 || std::string_view{ "-._~/:" }.find(c) != std::string_view::npos) {
            uri += c;
        } else {
            char escaped[4];
            std::snprintf(escaped, sizeof escaped, "%%%02X", static_cast<unsigned>(static_cast<unsigned char>(c)));
            uri += escaped;
        }
    }
    return uri;
}

[[nodiscard]] JsonValue Str(std::string_view text) { return JsonValue::MakeString(std::string{ text }); }
[[nodiscard]] JsonValue Num(double value) { return JsonValue::MakeNumber(value); }

[[nodiscard]] JsonValue Document(std::string_view uri) {
    JsonValue document = JsonValue::MakeObject();
    document.Set("uri", Str(uri));
    return document;
}

[[nodiscard]] std::string Frame(std::string_view body) {
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + std::string{ body };
}

// A request (id >= 0) or a notification (id < 0).
[[nodiscard]] std::string Message(std::string_view method, double id, JsonValue params = JsonValue::MakeObject()) {
    JsonValue message = JsonValue::MakeObject();
    message.Set("jsonrpc", Str("2.0"));
    if (id >= 0.0) message.Set("id", Num(id));
    message.Set("method", Str(method));
    message.Set("params", std::move(params));
    return Frame(message.Dump());
}

[[nodiscard]] std::string Open(std::string_view uri, std::string_view text) {
    JsonValue document = Document(uri);
    document.Set("languageId", Str("lua"));
    document.Set("version", Num(1));
    document.Set("text", Str(text));
    JsonValue params = JsonValue::MakeObject();
    params.Set("textDocument", std::move(document));
    return Message("textDocument/didOpen", -1.0, std::move(params));
}

[[nodiscard]] std::string Change(std::string_view uri, std::string_view text, double version) {
    JsonValue document = Document(uri);
    document.Set("version", Num(version));
    JsonValue change = JsonValue::MakeObject();
    change.Set("text", Str(text));
    JsonValue changes = JsonValue::MakeArray();
    changes.Append(std::move(change));
    JsonValue params = JsonValue::MakeObject();
    params.Set("textDocument", std::move(document));
    params.Set("contentChanges", std::move(changes));
    return Message("textDocument/didChange", -1.0, std::move(params));
}

// A request about a place in a document.
[[nodiscard]] std::string At(std::string_view method, double id, std::string_view uri, double line, double character) {
    JsonValue position = JsonValue::MakeObject();
    position.Set("line", Num(line));
    position.Set("character", Num(character));
    JsonValue params = JsonValue::MakeObject();
    params.Set("textDocument", Document(uri));
    params.Set("position", std::move(position));
    return Message(method, id, std::move(params));
}

[[nodiscard]] std::string Initialize() { return Message("initialize", 1.0) + Message("initialized", -1.0); }
[[nodiscard]] std::string Shutdown() { return Message("shutdown", 99.0) + Message("exit", -1.0); }

struct Session {
    int exitCode = 0;
    std::string errors;
    std::vector<JsonValue> frames;

    // The answer to request `id`.
    [[nodiscard]] const JsonValue& Answer(double id) const {
        for (const JsonValue& frame : frames) {
            const JsonValue* frameId = frame.Find("id");
            if (frameId != nullptr && frameId->GetKind() == JsonValue::Kind::Number && frameId->AsNumber() == id && frame.Find("method") == nullptr) return frame;
        }
        Require(false, "the server did not answer request " + std::to_string(id));
        return frames.front();
    }

    [[nodiscard]] const JsonValue& Result(double id) const {
        const JsonValue* result = Answer(id).Find("result");
        Require(result != nullptr, "request " + std::to_string(id) + " got an error: " + Answer(id).Dump());
        return *result;
    }

    [[nodiscard]] double ErrorCode(double id) const {
        const JsonValue* error = Answer(id).Find("error");
        Require(error != nullptr, "request " + std::to_string(id) + " was expected to fail: " + Answer(id).Dump());
        return error->Find("code")->AsNumber();
    }

    // The params of every publishDiagnostics notification, in order.
    [[nodiscard]] std::vector<const JsonValue*> Diagnostics() const {
        std::vector<const JsonValue*> published;
        for (const JsonValue& frame : frames) {
            const JsonValue* method = frame.Find("method");
            if (method != nullptr && method->AsString() == "textDocument/publishDiagnostics") published.push_back(frame.Find("params"));
        }
        return published;
    }
};

[[nodiscard]] Session Converse(const std::string& input, std::vector<std::string> arguments = {}) {
    std::istringstream in{ input };
    std::ostringstream out;
    std::ostringstream err;
    const kb::cli::ArgumentList parsed{ arguments };
    Session session;
    session.exitCode = kb::cli::RunLspCommand(parsed, in, kb::cli::CommandIo{ .out = out, .err = err });
    session.errors = err.str();
    const std::string output = out.str();
    for (std::size_t at = 0U; at < output.size();) {
        constexpr std::string_view kHeader = "Content-Length: ";
        const std::size_t headerEnd = output.find("\r\n\r\n", at);
        Require(headerEnd != std::string::npos && output.compare(at, kHeader.size(), kHeader) == 0, "the server wrote something that is not a frame");
        const std::size_t length = std::stoul(output.substr(at + kHeader.size(), headerEnd - at - kHeader.size()));
        Require(headerEnd + 4U + length <= output.size(), "a frame is shorter than its Content-Length");
        JsonValue frame;
        std::string error;
        Require(JsonValue::Parse(std::string_view{ output }.substr(headerEnd + 4U, length), frame, error), "a frame is not valid JSON: " + error);
        Require(frame.Find("jsonrpc") != nullptr && frame.Find("jsonrpc")->AsString() == "2.0", "a frame is not JSON-RPC 2.0");
        session.frames.push_back(std::move(frame));
        at = headerEnd + 4U + length;
    }
    return session;
}

[[nodiscard]] std::vector<std::string> LabelsOf(const JsonValue& completion) {
    std::vector<std::string> labels;
    const JsonValue* items = completion.Find("items");
    for (std::size_t index = 0U; index < items->Size(); ++index) labels.push_back(items->At(index)->Find("label")->AsString());
    std::ranges::sort(labels);
    return labels;
}

[[nodiscard]] double Number(const JsonValue* value) { return value->AsNumber(); }

void TestDiagnostics(const std::vector<std::string>& project) {
    const std::string uri = UriOf(Root() / "Assets" / "Scripts" / "Diag.lua");
    const std::string withOperator = "local a, b = 7, 2\nlocal c = a // b\n";
    const std::vector<kb::script::ScriptSubsetFinding> expected = kb::script::FindOutOfSubsetConstructs(withOperator);
    Require(expected.size() == 1U, "the validator must find the one construct outside the shared language");
    const Session session = Converse(Initialize() + Open(uri, withOperator) + Change(uri, "local a, b = 7, 2\nlocal c = math.floor(a / b)\n", 2.0) + Shutdown(), project);
    Require(session.exitCode == 0, "the server must exit with 0 after shutdown and exit");
    Require(session.Result(1.0).Find("capabilities")->Find("completionProvider") != nullptr, "initialize must offer completion");
    const std::vector<const JsonValue*> published = session.Diagnostics();
    Require(published.size() == 2U, "one publishDiagnostics for the open and one for the change");
    Require(published[0]->Find("uri")->AsString() == uri, "diagnostics go to the document's URI");
    const JsonValue& first = *published[0]->Find("diagnostics");
    Require(first.Size() == 1U, "the construct outside the shared language is one diagnostic: " + published[0]->Dump());
    const JsonValue& warning = *first.At(0U);
    Require(Number(warning.Find("severity")) == 2.0, "a construct outside the shared language is a warning");
    const JsonValue& range = *warning.Find("range");
    Require(Number(range.Find("start")->Find("line")) == expected[0].line - 1U && Number(range.Find("end")->Find("line")) == expected[0].line - 1U,
        "the warning is on the line of the validator's finding");
    Require(Number(range.Find("start")->Find("character")) == expected[0].column - 1U &&
            Number(range.Find("end")->Find("character")) == expected[0].column - 1U + expected[0].length,
        "the warning covers the validator's column and length");
    Require(warning.Find("message")->AsString() == expected[0].message, "the warning carries the validator's message");
    Require(published[1]->Find("diagnostics")->Size() == 0U, "the corrected text has no diagnostics");

    // Columns in UTF-16 units: the letter before the construct is two bytes and one unit.
    const std::string accent = "local s = \"\xC5\xBC\" local c = 7 // 2\n";
    const std::vector<kb::script::ScriptSubsetFinding> accentFindings = kb::script::FindOutOfSubsetConstructs(accent);
    const Session unicode = Converse(Initialize() + Open(uri, accent) + Shutdown(), project);
    const JsonValue& accentRange = *unicode.Diagnostics()[0]->Find("diagnostics")->At(0U)->Find("range");
    Require(Number(accentRange.Find("start")->Find("character")) == accentFindings[0].column - 2U, "columns are UTF-16 code units");

    const Session broken = Converse(Initialize() + Open(uri, "local ok = 1\nlocal x = = 2\n") + Shutdown(), project);
    const JsonValue& errors = *broken.Diagnostics()[0]->Find("diagnostics");
    Require(errors.Size() == 1U, "a syntax error is one diagnostic: " + broken.Diagnostics()[0]->Dump());
    Require(Number(errors.At(0U)->Find("severity")) == 1.0 && Number(errors.At(0U)->Find("range")->Find("start")->Find("line")) == 1.0,
        "a syntax error is an error on its line");
}

void TestApiFeatures(const std::vector<std::string>& project) {
    kb::script::ScriptApiCatalog catalog;
    std::string error;
    Require(kb::cli::BuildScriptApiCatalog(std::nullopt, catalog, error), "the script API catalog could not be built: " + error);
    std::vector<std::string> expected;
    for (const auto& binding : catalog.luaBindings) {
        if (binding.tableName == "Transform") expected.push_back(binding.luaName);
    }
    std::ranges::sort(expected);
    Require(expected.size() > 3U, "the catalog has the Transform table");

    const std::string uri = UriOf(Root() / "Assets" / "Scripts" / "Api.lua");
    const std::string text = "function Tick(self, dt)\n  Transform.\n  Transform.SetPosition(self.entity, 0, 1, 2)\nend\n";
    const Session session = Converse(
        Initialize() + Open(uri, text) + At("textDocument/completion", 2.0, uri, 1.0, 12.0) + At("textDocument/completion", 3.0, uri, 1.0, 2.0) +
            At("textDocument/hover", 4.0, uri, 2.0, 16.0) + At("textDocument/hover", 5.0, uri, 0.0, 0.0) + Shutdown(),
        project);
    Require(LabelsOf(session.Result(2.0)) == expected, "completion after Transform. lists exactly the functions of that table");
    const JsonValue* items = session.Result(2.0).Find("items");
    const auto* setPosition = catalog.FindFunction("Transform.SetPosition");
    Require(setPosition != nullptr, "the catalog has Transform.SetPosition");
    bool described = false;
    for (std::size_t index = 0U; index < items->Size(); ++index) {
        const JsonValue& item = *items->At(index);
        if (item.Find("label")->AsString() != "SetPosition") continue;
        described = item.Find("detail")->AsString() == "Transform.SetPosition(entity, x, y, z)" &&
            item.Find("documentation")->Find("value")->AsString() == setPosition->description;
    }
    Require(described, "a completion item carries the signature and the description of the catalog");
    const std::vector<std::string> globals = LabelsOf(session.Result(3.0));
    Require(std::ranges::find(globals, "Transform") != globals.end() && std::ranges::find(globals, "Log") != globals.end(), "completion without a table lists the tables and the global functions");

    const std::string hover = session.Result(4.0).Find("contents")->Find("value")->AsString();
    Require(hover.find("Transform.SetPosition(entity, x, y, z)") != std::string::npos && hover.find(setPosition->description) != std::string::npos,
        "hover on an API function shows its signature and description: " + hover);
    Require(session.Answer(5.0).Find("result") != nullptr && session.Answer(5.0).Find("result")->IsNull(), "hover on a keyword is empty");
}

void TestDefinition(const std::vector<std::string>& project) {
    const std::filesystem::path scripts = Root() / "Assets" / "Scripts";
    WriteFile(scripts / "Util.lua", "local M = {}\nfunction M.scale(v)\n  return v * 2\nend\nreturn M\n");
    const std::string main = UriOf(scripts / "Main.lua");
    const std::string require = UriOf(scripts / "Require.lua");
    const std::string text = "local Util = Import(\"Util\")\nlocal function helper(a)\n  return a\nend\nfunction Tick(self, dt)\n  helper(1)\n  Util.scale(2)\nend\n";
    const std::string requireText = "local U = require(\"Util\")\nU.scale(1)\n";
    const Session session = Converse(
        Initialize() + Open(main, text) + Open(require, requireText) + At("textDocument/definition", 2.0, main, 5.0, 3.0) +
            At("textDocument/definition", 3.0, main, 6.0, 9.0) + At("textDocument/definition", 4.0, main, 0.0, 22.0) + At("textDocument/definition", 5.0, require, 1.0, 4.0) +
            At("textDocument/definition", 6.0, main, 4.0, 10.0) + At("textDocument/documentSymbol", 7.0, main, 0.0, 0.0) + Shutdown(),
        project);
    const auto location = [&](double id) -> const JsonValue& {
        const JsonValue& result = session.Result(id);
        Require(result.GetKind() == JsonValue::Kind::Array && result.Size() == 1U, "definition must find one place: " + result.Dump());
        return *result.At(0U);
    };
    Require(location(2.0).Find("uri")->AsString() == main && Number(location(2.0).Find("range")->Find("start")->Find("line")) == 1.0,
        "the definition of a local function is its declaration");
    Require(location(3.0).Find("uri")->AsString() == UriOf(scripts / "Util.lua") && Number(location(3.0).Find("range")->Find("start")->Find("line")) == 1.0,
        "the definition of Module.function is in the imported file, at the function");
    Require(location(4.0).Find("uri")->AsString() == UriOf(scripts / "Util.lua") && Number(location(4.0).Find("range")->Find("start")->Find("line")) == 0.0,
        "the definition of the module name in Import is the module's file");
    Require(location(5.0).Find("uri")->AsString() == UriOf(scripts / "Util.lua") && Number(location(5.0).Find("range")->Find("start")->Find("line")) == 1.0,
        "require resolves the same way");
    Require(session.Result(6.0).GetKind() == JsonValue::Kind::Array, "the definition of a function is itself");

    const JsonValue& symbols = session.Result(7.0);
    std::vector<std::string> names;
    for (std::size_t index = 0U; index < symbols.Size(); ++index) names.push_back(symbols.At(index)->Find("name")->AsString());
    Require(names == std::vector<std::string>{ "Util", "helper", "Tick" }, "the outline lists what the script defines at its top level");
    const JsonValue& tick = *symbols.At(2U);
    Require(Number(tick.Find("kind")) == 12.0 && tick.Find("detail")->AsString() == "(self, dt)" && Number(tick.Find("range")->Find("start")->Find("line")) == 4.0 &&
            Number(tick.Find("range")->Find("end")->Find("line")) == 7.0,
        "a function in the outline spans its declaration up to its end");
}

void TestProtocolErrors(const std::vector<std::string>& project) {
    const std::string uri = UriOf(Root() / "Assets" / "Scripts" / "Api.lua");
    const std::string input = Initialize() + "Content-Type: application/json\r\n\r\n" + Frame("{not json") + Frame("[1,2]") + Frame("{\"jsonrpc\":\"2.0\",\"id\":8}") +
        Message("textDocument/nothing", 9.0) + Message("$/unknownNotification", -1.0) + Message("textDocument/hover", 10.0) + Open(uri, "x = 1\n") + At("textDocument/hover", 11.0, uri, 0.0, 0.0) + Shutdown();
    const Session session = Converse(input, project);
    Require(session.exitCode == 0, "the server must survive malformed input and exit with 0");
    const auto code = [&](const JsonValue& frame) { return frame.Find("error") != nullptr ? Number(frame.Find("error")->Find("code")) : 0.0; };
    std::vector<double> parseErrors;
    for (const JsonValue& frame : session.frames) {
        if (frame.Find("error") != nullptr && frame.Find("id")->IsNull()) parseErrors.push_back(code(frame));
    }
    Require(parseErrors == std::vector<double>{ -32700, -32700, -32600 }, "a missing length, bad JSON and a message that is not an object each get an error without an id");
    Require(session.ErrorCode(8.0) == -32600, "a message without a method is an invalid request");
    Require(session.ErrorCode(9.0) == -32601, "an unknown method is MethodNotFound");
    Require(session.ErrorCode(10.0) == -32602, "a request without its parameters is InvalidParams");
    Require(session.Answer(11.0).Find("result") != nullptr, "the server goes on after the errors");

    const Session early = Converse(Message("textDocument/hover", 2.0) + Message("initialize", 3.0) + Message("exit", -1.0), project);
    Require(early.ErrorCode(2.0) == -32002, "a request before initialize is ServerNotInitialized");
    Require(early.exitCode == 1, "exit without shutdown ends with 1");

    const Session late = Converse(Initialize() + Message("shutdown", 2.0) + Message("textDocument/hover", 3.0) + Message("exit", -1.0), project);
    Require(late.ErrorCode(3.0) == -32600 && late.exitCode == 0, "a request after shutdown is refused");
}

} // namespace

void RunLanguageServerTests() {
    std::error_code ignored;
    std::filesystem::remove_all(Root(), ignored);
    std::filesystem::create_directories(Root() / "Assets" / "Scripts");
    const std::vector<std::string> project{ "--project", Root().string() };
    TestDiagnostics(project);
    TestApiFeatures(project);
    TestDefinition(project);
    TestProtocolErrors(project);
    const std::vector<std::string> unknown{ "--nonsense" };
    std::istringstream nothing;
    std::ostringstream out;
    std::ostringstream err;
    Require(kb::cli::RunLspCommand(kb::cli::ArgumentList{ unknown }, nothing, kb::cli::CommandIo{ .out = out, .err = err }) == 1 && out.str().empty(),
        "lsp refuses an option it does not have");
    std::filesystem::remove_all(Root(), ignored);
}
