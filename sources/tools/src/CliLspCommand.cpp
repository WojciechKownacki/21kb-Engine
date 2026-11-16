#include "CliCommands.hpp"
#include "CliLuaSourceIndex.hpp"
#include "CliProgram.hpp"
#include "CliProtocolStream.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/script/ScriptApiCatalog.hpp"
#include "engine/script/ScriptLanguageSubset.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The Language Server Protocol for the Lua scripts of a project. Everything the editor is told comes from the engine:
// the script API from ScriptApiCatalog, diagnostics from the validator of the language every platform runs.
namespace kb::cli {
namespace {

using kb::core::JsonValue;
using kb::script::ScriptApiCatalog;
using kb::script::ScriptApiCatalogFunction;
using kb::script::ScriptApiCatalogLuaBinding;

constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kServerNotInitialized = -32002;

// The numbers LSP gives the severities and kinds this server sends.
constexpr int kErrorSeverity = 1;
constexpr int kWarningSeverity = 2;
constexpr int kFunctionCompletion = 3;
constexpr int kModuleCompletion = 9;
constexpr int kFunctionSymbol = 12;
constexpr int kVariableSymbol = 13;

[[nodiscard]] JsonValue Str(std::string value) { return JsonValue::MakeString(std::move(value)); }
[[nodiscard]] JsonValue Num(double value) { return JsonValue::MakeNumber(value); }
[[nodiscard]] JsonValue Bool(bool value) { return JsonValue::MakeBool(value); }

[[nodiscard]] JsonValue Object(std::initializer_list<std::pair<const char*, JsonValue>> members) {
    JsonValue object = JsonValue::MakeObject();
    for (const auto& [name, value] : members) object.Set(name, value);
    return object;
}

[[nodiscard]] const JsonValue* Member(const JsonValue* value, std::string_view name) {
    return value != nullptr && value->GetKind() == JsonValue::Kind::Object ? value->Find(name) : nullptr;
}

[[nodiscard]] bool IsString(const JsonValue* value) { return value != nullptr && value->GetKind() == JsonValue::Kind::String; }
[[nodiscard]] bool IsNumber(const JsonValue* value) { return value != nullptr && value->GetKind() == JsonValue::Kind::Number; }
[[nodiscard]] bool IsNameChar(char c) noexcept { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }
[[nodiscard]] bool IsMemberOperator(char c) noexcept { return c == '.' || c == ':'; }

// The text of a document as lines, without their line ends; a text that ends with a line end has an empty last line.
[[nodiscard]] std::vector<std::string_view> SplitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    for (std::size_t begin = 0U;;) {
        const std::size_t end = text.find('\n', begin);
        std::string_view line = text.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1U);
        lines.push_back(line);
        if (end == std::string_view::npos) return lines;
        begin = end + 1U;
    }
}

// LSP counts columns in UTF-16 code units; the script tools count bytes.
[[nodiscard]] std::uint32_t Utf16Column(std::string_view line, std::size_t byteColumn) {
    std::uint32_t units = 0U;
    for (std::size_t at = 0U; at < byteColumn && at < line.size(); ++at) {
        const auto byte = static_cast<unsigned char>(line[at]);
        if ((byte & 0xC0U) != 0x80U) units += byte >= 0xF0U ? 2U : 1U;
    }
    return units;
}

[[nodiscard]] std::size_t ByteColumn(std::string_view line, std::uint32_t utf16Column) {
    std::uint32_t units = 0U;
    std::size_t at = 0U;
    while (at < line.size() && units < utf16Column) {
        units += static_cast<unsigned char>(line[at]) >= 0xF0U ? 2U : 1U;
        ++at;
        while (at < line.size() && (static_cast<unsigned char>(line[at]) & 0xC0U) == 0x80U) ++at;
    }
    return at;
}

[[nodiscard]] std::string PercentDecode(std::string_view text) {
    const auto hex = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0 ? c - '0' : std::tolower(static_cast<unsigned char>(c)) - 'a' + 10; };
    std::string decoded;
    for (std::size_t at = 0U; at < text.size(); ++at) {
        if (text[at] == '%' && at + 2U < text.size() && std::isxdigit(static_cast<unsigned char>(text[at + 1U])) != 0 &&
            std::isxdigit(static_cast<unsigned char>(text[at + 2U])) != 0) {
            decoded += static_cast<char>(hex(text[at + 1U]) * 16 + hex(text[at + 2U]));
            at += 2U;
        } else {
            decoded += text[at];
        }
    }
    return decoded;
}

// The file a file: URI names, empty for any other kind of URI.
[[nodiscard]] std::filesystem::path PathFromUri(std::string_view uri) {
    if (!uri.starts_with("file://")) return {};
    std::string path = PercentDecode(uri.substr(7U));
    if (path.empty() || path.front() != '/') return {};
    if (path.size() >= 3U && std::isalpha(static_cast<unsigned char>(path[1])) != 0 && path[2] == ':') path.erase(0U, 1U); // /C:/dir
    return std::filesystem::path{ path };
}

[[nodiscard]] std::string UriFromPath(const std::filesystem::path& path) {
    const std::string text = path.generic_string();
    std::string uri = "file://";
    if (text.empty() || text.front() != '/') uri += '/';
    for (const char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0 || std::string_view{ "-._~/:" }.find(c) != std::string_view::npos) {
            uri += c;
        } else {
            constexpr std::string_view digits = "0123456789ABCDEF";
            uri += '%';
            uri += digits[static_cast<unsigned char>(c) >> 4U];
            uri += digits[static_cast<unsigned char>(c) & 15U];
        }
    }
    return uri;
}

// The key a document is stored under: its normalised path (case-folded on Windows), or its URI when it has no file.
[[nodiscard]] std::string DocumentKey(const std::filesystem::path& path, std::string_view uri) {
    if (path.empty()) return std::string{ uri };
    std::string key = path.lexically_normal().generic_string();
#if defined(_WIN32)
    std::ranges::transform(key, key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return key;
}

struct Reply {
    JsonValue result;
    int code = 0; // an error code, or zero
    std::string message;
};

[[nodiscard]] Reply Ok(JsonValue result) { return Reply{ .result = std::move(result) }; }
[[nodiscard]] Reply Fail(int code, std::string message) { return Reply{ .code = code, .message = std::move(message) }; }

// The pins as "`entity`: Entity, `x`: Float?" ("?" marks an optional input).
[[nodiscard]] std::string PinSummary(const std::vector<kb::script::ScriptApiPin>& pins) {
    std::string text;
    for (const kb::script::ScriptApiPin& pin : pins) {
        text += (text.empty() ? "`" : ", `") + pin.name + "`: " + kb::script::ToString(pin.type) + (pin.required ? "" : "?");
    }
    return text;
}

// "a, b" for the parameters of a function.
[[nodiscard]] std::string ParameterList(const LuaDeclaration& declaration) {
    std::string text;
    for (const std::string& parameter : declaration.parameters) text += (text.empty() ? "" : ", ") + parameter;
    return text;
}

// The last name of the qualifier of a declaration: "Module" for `function Outer.Module.Name`.
[[nodiscard]] std::string_view LastQualifier(const LuaDeclaration& declaration) {
    return std::string_view{ declaration.qualifier }.substr(declaration.qualifier.rfind('.') + 1U);
}

[[nodiscard]] JsonValue RangeJson(const std::vector<std::string_view>& lines, LuaRange range) {
    const auto position = [&](LuaSpot spot) {
        const std::string_view line = spot.line < lines.size() ? lines[spot.line] : std::string_view{};
        return Object({ { "line", Num(spot.line) }, { "character", Num(Utf16Column(line, spot.column)) } });
    };
    return Object({ { "start", position(range.begin) }, { "end", position(range.end) } });
}

[[nodiscard]] JsonValue LocationJson(const std::filesystem::path& file, const std::vector<std::string_view>& lines, LuaRange range) {
    return Object({ { "uri", Str(UriFromPath(file)) }, { "range", RangeJson(lines, range) } });
}

// The name at a column of a line, and the name before the "." or ":" it follows.
struct Word {
    std::size_t begin = 0U;
    std::size_t end = 0U;
    std::string_view text;
    std::string_view qualifier;
    bool member = false; // follows "." or ":"
};

[[nodiscard]] Word WordAt(std::string_view line, std::size_t column) {
    Word word;
    word.begin = word.end = std::min(column, line.size());
    while (word.begin > 0U && IsNameChar(line[word.begin - 1U])) --word.begin;
    while (word.end < line.size() && IsNameChar(line[word.end])) ++word.end;
    word.text = line.substr(word.begin, word.end - word.begin);
    word.member = word.begin > 0U && IsMemberOperator(line[word.begin - 1U]);
    if (word.member) {
        std::size_t begin = word.begin - 1U;
        while (begin > 0U && IsNameChar(line[begin - 1U])) --begin;
        // `a.b.c` has no single qualifier a table could answer to.
        if (begin == 0U || !IsMemberOperator(line[begin - 1U])) word.qualifier = line.substr(begin, word.begin - 1U - begin);
    }
    return word;
}

class LspServer final {
public:
    LspServer(const ScriptApiCatalog& catalog, std::filesystem::path project, std::ostream& out)
        : catalog_(catalog), project_(std::move(project)), out_(out) {
        for (const ScriptApiCatalogLuaBinding& binding : catalog.luaBindings) tables_[binding.tableName].push_back(&binding);
    }

    // Serves until `exit` or the end of the input; the exit code the protocol asks for.
    int Run(std::istream& in) {
        UseBinaryStdio(in);
        std::string body;
        for (;;) {
            const FramedRead read = ReadFramedMessage(in, body);
            if (read == FramedRead::EndOfInput) return shutdown_ ? 0 : 1;
            if (read == FramedRead::Malformed) {
                SendError(JsonValue{}, kParseError, "a message needs a valid Content-Length header");
                continue;
            }
            JsonValue message;
            std::string error;
            if (!JsonValue::Parse(body, message, error)) {
                SendError(JsonValue{}, kParseError, "the message is not valid JSON: " + error);
                continue;
            }
            if (message.GetKind() != JsonValue::Kind::Object) {
                SendError(JsonValue{}, kInvalidRequest, "a message must be a JSON object");
                continue;
            }
            if (Handle(message)) return shutdown_ ? 0 : 1;
        }
    }

private:
    struct Document {
        std::string uri;
        std::string text;
        std::optional<double> version;
    };

    // The document and the position a request is about.
    struct Target {
        std::string uri;
        std::filesystem::path path;
        std::string text;
        std::vector<std::string_view> lines; // of `text`
        LuaSpot spot;                        // line and byte column

        [[nodiscard]] std::string_view Line() const { return spot.line < lines.size() ? lines[spot.line] : std::string_view{}; }
    };

    void Send(JsonValue message) {
        message.Set("jsonrpc", Str("2.0"));
        WriteFramedMessage(out_, message);
    }

    void SendError(const JsonValue& id, int code, std::string message) {
        Send(Object({ { "id", id }, { "error", Object({ { "code", Num(code) }, { "message", Str(std::move(message)) } }) } }));
    }

    void SendNotification(std::string method, JsonValue params) {
        Send(Object({ { "method", Str(std::move(method)) }, { "params", std::move(params) } }));
    }

    // Returns true for `exit`.
    [[nodiscard]] bool Handle(const JsonValue& message) {
        const JsonValue* method = message.Find("method");
        const JsonValue* id = message.Find("id");
        if (id != nullptr && id->GetKind() != JsonValue::Kind::Number && id->GetKind() != JsonValue::Kind::String) {
            SendError(JsonValue{}, kInvalidRequest, "the id of a request is a number or a string");
            return false;
        }
        if (!IsString(method)) {
            // A message with a result or an error answers a request of ours, and there are none.
            if (message.Find("result") == nullptr && message.Find("error") == nullptr) {
                SendError(id != nullptr ? *id : JsonValue{}, kInvalidRequest, "a request needs a method");
            }
            return false;
        }
        const JsonValue* params = message.Find("params");
        if (id == nullptr) return Notify(method->AsString(), params);
        Reply reply = Request(method->AsString(), params);
        if (reply.code != 0) {
            SendError(*id, reply.code, std::move(reply.message));
        } else {
            Send(Object({ { "id", *id }, { "result", std::move(reply.result) } }));
        }
        return false;
    }

    [[nodiscard]] Reply Request(const std::string& method, const JsonValue* params) {
        if (method == "initialize") return Initialize();
        if (!initialized_) return Fail(kServerNotInitialized, "the server has not been initialized");
        if (shutdown_) return Fail(kInvalidRequest, "the server has been shut down");
        if (method == "shutdown") {
            shutdown_ = true;
            return Ok(JsonValue{});
        }
        if (method == "textDocument/completion") return Completion(params);
        if (method == "textDocument/hover") return Hover(params);
        if (method == "textDocument/definition") return Definition(params);
        if (method == "textDocument/documentSymbol") return DocumentSymbols(params);
        return Fail(kMethodNotFound, "the method is not supported: " + method);
    }

    // Returns true for `exit`. Every other notification a client may send is ignored.
    [[nodiscard]] bool Notify(const std::string& method, const JsonValue* params) {
        if (method == "exit") return true;
        if (!initialized_ || shutdown_) return false;
        if (method == "textDocument/didOpen") DidOpen(params);
        else if (method == "textDocument/didChange") DidChange(params);
        else if (method == "textDocument/didClose") DidClose(params);
        return false;
    }

    [[nodiscard]] Reply Initialize() {
        if (initialized_) return Fail(kInvalidRequest, "the server has been initialized already");
        initialized_ = true;
        JsonValue triggers = JsonValue::MakeArray();
        triggers.Append(Str("."));
        triggers.Append(Str(":"));
        JsonValue capabilities = Object({
            { "textDocumentSync", Object({ { "openClose", Bool(true) }, { "change", Num(1) } }) }, // 1: the whole text on every change
            { "completionProvider", Object({ { "triggerCharacters", std::move(triggers) } }) },
            { "hoverProvider", Bool(true) },
            { "definitionProvider", Bool(true) },
            { "documentSymbolProvider", Bool(true) },
        });
        return Ok(Object({ { "capabilities", std::move(capabilities) }, { "serverInfo", Object({ { "name", Str("kb_cli lsp") } }) } }));
    }

    // ---- documents and diagnostics -------------------------------------------------------------------------------

    void DidOpen(const JsonValue* params) {
        const JsonValue* document = Member(params, "textDocument");
        const JsonValue* uri = Member(document, "uri");
        const JsonValue* text = Member(document, "text");
        if (!IsString(uri) || !IsString(text)) return;
        const JsonValue* version = Member(document, "version");
        Document& stored = documents_[DocumentKey(PathFromUri(uri->AsString()), uri->AsString())];
        stored = Document{ uri->AsString(), text->AsString(), IsNumber(version) ? std::optional<double>{ version->AsNumber() } : std::nullopt };
        PublishDiagnostics(stored);
    }

    void DidChange(const JsonValue* params) {
        const JsonValue* document = Member(params, "textDocument");
        const JsonValue* uri = Member(document, "uri");
        const JsonValue* changes = Member(params, "contentChanges");
        if (!IsString(uri) || changes == nullptr || changes->GetKind() != JsonValue::Kind::Array) return;
        const auto found = documents_.find(DocumentKey(PathFromUri(uri->AsString()), uri->AsString()));
        if (found == documents_.end()) return;
        // The server asks for the whole text; a change with a range would be an increment of it.
        for (std::size_t index = changes->Size(); index > 0U; --index) {
            const JsonValue* change = changes->At(index - 1U);
            if (IsString(Member(change, "text")) && Member(change, "range") == nullptr) {
                found->second.text = Member(change, "text")->AsString();
                break;
            }
        }
        if (const JsonValue* version = Member(document, "version"); IsNumber(version)) found->second.version = version->AsNumber();
        PublishDiagnostics(found->second);
    }

    void DidClose(const JsonValue* params) {
        const JsonValue* uri = Member(Member(params, "textDocument"), "uri");
        if (!IsString(uri)) return;
        documents_.erase(DocumentKey(PathFromUri(uri->AsString()), uri->AsString()));
        SendNotification("textDocument/publishDiagnostics", Object({ { "uri", *uri }, { "diagnostics", JsonValue::MakeArray() } }));
    }

    [[nodiscard]] static JsonValue Diagnostic(JsonValue range, int severity, std::string message) {
        return Object({ { "range", std::move(range) }, { "severity", Num(severity) }, { "source", Str("21kb") }, { "message", Str(std::move(message)) } });
    }

    void PublishDiagnostics(const Document& document) {
        const std::vector<std::string_view> lines = SplitLines(document.text);
        const std::vector<kb::script::ScriptSubsetFinding> findings = kb::script::FindOutOfSubsetConstructs(document.text);
        JsonValue diagnostics = JsonValue::MakeArray();
        // A line the compiler of this build refuses because of a construct outside the shared language is explained by
        // that construct's warning, so it gets no second diagnostic.
        if (const std::optional<kb::script::ScriptSyntaxError> error = kb::script::FindSyntaxError(document.text);
            error.has_value() && std::ranges::none_of(findings, [&](const auto& finding) { return finding.line == error->line; })) {
            const auto line = static_cast<std::uint32_t>(std::min<std::size_t>(std::max<std::size_t>(error->line, 1U), lines.size()) - 1U);
            const std::string_view text = lines[line];
            const auto first = static_cast<std::uint32_t>(std::min(text.find_first_not_of(" \t"), text.size()));
            diagnostics.Append(Diagnostic(RangeJson(lines, { { line, first }, { line, static_cast<std::uint32_t>(text.size()) } }), kErrorSeverity, error->message));
        }
        for (const kb::script::ScriptSubsetFinding& finding : findings) {
            const std::uint32_t line = finding.line - 1U;
            diagnostics.Append(Diagnostic(RangeJson(lines, { { line, finding.column - 1U }, { line, finding.column - 1U + finding.length } }), kWarningSeverity, finding.message));
        }
        JsonValue params = Object({ { "uri", Str(document.uri) }, { "diagnostics", std::move(diagnostics) } });
        if (document.version.has_value()) params.Set("version", Num(*document.version));
        SendNotification("textDocument/publishDiagnostics", std::move(params));
    }

    // ---- requests ------------------------------------------------------------------------------------------------

    // The text of a file: the open document when the editor has it open, else the file on disk.
    [[nodiscard]] std::optional<std::string> TextOf(const std::filesystem::path& path) const {
        if (const auto open = documents_.find(DocumentKey(path, {})); open != documents_.end()) return open->second.text;
        std::ifstream input{ path, std::ios::binary };
        if (!input.is_open()) return std::nullopt;
        return std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    }

    [[nodiscard]] bool Resolve(const JsonValue* params, Target& target, Reply& failure, bool needsPosition = true) const {
        const JsonValue* uri = Member(Member(params, "textDocument"), "uri");
        if (!IsString(uri)) {
            failure = Fail(kInvalidParams, "textDocument.uri is required");
            return false;
        }
        target.uri = uri->AsString();
        target.path = PathFromUri(target.uri);
        if (const auto open = documents_.find(DocumentKey(target.path, target.uri)); open != documents_.end()) {
            target.text = open->second.text;
        } else if (std::optional<std::string> text = target.path.empty() ? std::nullopt : TextOf(target.path)) {
            target.text = std::move(*text);
        } else {
            failure = Fail(kInvalidParams, "the document is not open and is not a file: " + target.uri);
            return false;
        }
        target.lines = SplitLines(target.text);
        if (!needsPosition) return true;
        const JsonValue* position = Member(params, "position");
        const JsonValue* line = Member(position, "line");
        const JsonValue* character = Member(position, "character");
        if (!IsNumber(line) || !IsNumber(character) || line->AsNumber() < 0.0 || character->AsNumber() < 0.0) {
            failure = Fail(kInvalidParams, "position.line and position.character are required");
            return false;
        }
        target.spot.line = static_cast<std::uint32_t>(line->AsNumber());
        target.spot.column = static_cast<std::uint32_t>(ByteColumn(target.Line(), static_cast<std::uint32_t>(character->AsNumber())));
        return true;
    }

    [[nodiscard]] const std::vector<const ScriptApiCatalogLuaBinding*>* Table(std::string_view name) const {
        const auto found = tables_.find(name);
        return found == tables_.end() ? nullptr : &found->second;
    }

    [[nodiscard]] const ScriptApiCatalogLuaBinding* FindBinding(std::string_view table, std::string_view name) const {
        const std::vector<const ScriptApiCatalogLuaBinding*>* members = Table(table);
        if (members == nullptr) return nullptr;
        const auto found = std::ranges::find_if(*members, [&](const ScriptApiCatalogLuaBinding* binding) { return binding->luaName == name; });
        return found == members->end() ? nullptr : *found;
    }

    // How a script calls the function a binding reaches: "Transform.SetPosition(entity, x, y, z)".
    [[nodiscard]] std::string Signature(const ScriptApiCatalogLuaBinding& binding) const {
        std::string text = (binding.tableName.empty() ? std::string{} : binding.tableName + ".") + binding.luaName + "(";
        const ScriptApiCatalogFunction* function = catalog_.FindFunction(binding.functionName);
        if (function == nullptr) return text + "...)";
        for (std::size_t index = 0U; index < function->inputs.size(); ++index) text += (index == 0U ? "" : ", ") + function->inputs[index].name;
        return text + ")";
    }

    [[nodiscard]] std::string Description(const ScriptApiCatalogLuaBinding& binding) const {
        std::string text = "```lua\n" + Signature(binding) + "\n```\n";
        if (const ScriptApiCatalogFunction* function = catalog_.FindFunction(binding.functionName)) {
            if (!function->description.empty()) text += "\n" + function->description + "\n";
            if (!function->inputs.empty()) text += "\nInputs: " + PinSummary(function->inputs) + "\n";
            if (!function->outputs.empty()) text += "\nReturns: " + PinSummary(function->outputs) + "\n";
        }
        return text;
    }

    [[nodiscard]] JsonValue FunctionItem(const ScriptApiCatalogLuaBinding& binding) const {
        JsonValue item = Object({ { "label", Str(binding.luaName) }, { "kind", Num(kFunctionCompletion) }, { "detail", Str(Signature(binding)) } });
        if (const ScriptApiCatalogFunction* function = catalog_.FindFunction(binding.functionName); function != nullptr && !function->description.empty()) {
            item.Set("documentation", Object({ { "kind", Str("markdown") }, { "value", Str(function->description) } }));
        }
        return item;
    }

    [[nodiscard]] Reply Completion(const JsonValue* params) const {
        Target target;
        Reply failure;
        if (!Resolve(params, target, failure)) return failure;
        // The text before the cursor decides: after "Table." its functions, elsewhere the globals.
        const std::string_view before = target.Line().substr(0U, target.spot.column);
        const Word word = WordAt(before, before.size());
        JsonValue items = JsonValue::MakeArray();
        if (word.member) {
            if (const auto* members = Table(word.qualifier)) {
                for (const ScriptApiCatalogLuaBinding* binding : *members) items.Append(FunctionItem(*binding));
            }
        } else {
            for (const auto& [name, members] : tables_) {
                if (name.empty()) {
                    for (const ScriptApiCatalogLuaBinding* binding : members) items.Append(FunctionItem(*binding));
                } else {
                    items.Append(Object({ { "label", Str(name) }, { "kind", Num(kModuleCompletion) }, { "detail", Str(std::to_string(members.size()) + " functions") } }));
                }
            }
        }
        return Ok(Object({ { "isIncomplete", Bool(false) }, { "items", std::move(items) } }));
    }

    [[nodiscard]] Reply Hover(const JsonValue* params) const {
        Target target;
        Reply failure;
        if (!Resolve(params, target, failure)) return failure;
        const Word word = WordAt(target.Line(), target.spot.column);
        if (word.text.empty()) return Ok(JsonValue{});
        std::string markdown;
        if (const ScriptApiCatalogLuaBinding* binding = FindBinding(word.member ? word.qualifier : std::string_view{}, word.text)) {
            markdown = Description(*binding);
        } else if (const auto* members = word.member ? nullptr : Table(word.text)) {
            markdown = "**" + std::string{ word.text } + "**: " + std::to_string(members->size()) + " functions, such as ";
            for (std::size_t index = 0U; index < members->size() && index < 8U; ++index) markdown += (index == 0U ? "`" : ", `") + (*members)[index]->luaName + "`";
        } else {
            for (const LuaDeclaration& declaration : IndexLuaSource(target.text).declarations) {
                if (!declaration.isFunction || declaration.name != word.text || (word.member && word.qualifier != LastQualifier(declaration))) continue;
                markdown = "```lua\nfunction " + (declaration.qualifier.empty() ? std::string{} : declaration.qualifier + ".") + declaration.name + "(" +
                    ParameterList(declaration) + ")\n```";
                break;
            }
        }
        if (markdown.empty()) return Ok(JsonValue{});
        const LuaRange range{ { target.spot.line, static_cast<std::uint32_t>(word.begin) }, { target.spot.line, static_cast<std::uint32_t>(word.end) } };
        return Ok(Object({ { "contents", Object({ { "kind", Str("markdown") }, { "value", Str(std::move(markdown)) } }) }, { "range", RangeJson(target.lines, range) } }));
    }

    // The file `Import("name")` or `require("name")` in `owner` reaches: the engine resolves a name next to the importing
    // script, with dots as folders, or from /Game (the project's Assets folder); require also looks in Assets.
    [[nodiscard]] std::optional<std::filesystem::path> ResolveModule(std::string_view name, const std::filesystem::path& owner) const {
        std::string relative{ name };
        std::ranges::replace(relative, '.', '/');
        std::vector<std::filesystem::path> candidates;
        if (relative.starts_with("/Game/")) {
            if (!project_.empty()) candidates.push_back(project_ / "Assets" / (relative.substr(6U) + ".lua"));
        } else {
            if (!owner.empty()) candidates.push_back(owner.parent_path() / (relative + ".lua"));
            if (!project_.empty()) candidates.push_back(project_ / "Assets" / (relative + ".lua"));
        }
        for (const std::filesystem::path& candidate : candidates) {
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error)) return candidate;
        }
        return std::nullopt;
    }

    // Where the name under the cursor is defined: in this file, or in the module its qualifier names; on the
    // name of a module in Import or require, the module's file.
    [[nodiscard]] Reply Definition(const JsonValue* params) const {
        Target target;
        Reply failure;
        if (!Resolve(params, target, failure)) return failure;
        const LuaSourceIndex index = IndexLuaSource(target.text);
        JsonValue locations = JsonValue::MakeArray();
        for (const LuaImport& import : index.imports) {
            if (!import.moduleRange.Contains(target.spot)) continue;
            if (const auto file = ResolveModule(import.module, target.path)) locations.Append(LocationJson(*file, {}, {}));
            return Ok(locations.Size() == 0U ? JsonValue{} : std::move(locations));
        }
        const Word word = WordAt(target.Line(), target.spot.column);
        if (word.text.empty()) return Ok(JsonValue{});
        // The declarations in the module an Import or require names that have the name asked for.
        const auto addFromModule = [&](const LuaImport& import, bool plainFunctionsOnly) {
            const auto file = ResolveModule(import.module, target.path);
            const std::optional<std::string> text = file ? TextOf(*file) : std::nullopt;
            if (!text) return;
            const std::vector<std::string_view> lines = SplitLines(*text);
            for (const LuaDeclaration& declaration : IndexLuaSource(*text).declarations) {
                if (declaration.name == word.text && (!plainFunctionsOnly || (declaration.isFunction && declaration.qualifier.empty()))) {
                    locations.Append(LocationJson(*file, lines, declaration.nameRange));
                }
            }
        };
        if (word.member) {
            for (const LuaImport& import : index.imports) {
                if (import.alias == word.qualifier) addFromModule(import, false);
            }
            for (const LuaDeclaration& declaration : index.declarations) {
                if (declaration.name == word.text && LastQualifier(declaration) == word.qualifier) locations.Append(LocationJson(target.path, target.lines, declaration.nameRange));
            }
        } else {
            for (const LuaDeclaration& declaration : index.declarations) {
                if (declaration.name == word.text && declaration.qualifier.empty()) locations.Append(LocationJson(target.path, target.lines, declaration.nameRange));
            }
            if (locations.Size() == 0U) {
                for (const LuaImport& import : index.imports) addFromModule(import, true);
            }
        }
        return Ok(locations.Size() == 0U ? JsonValue{} : std::move(locations));
    }

    // The functions and variables a script defines outside any block.
    [[nodiscard]] Reply DocumentSymbols(const JsonValue* params) const {
        Target target;
        Reply failure;
        if (!Resolve(params, target, failure, false)) return failure;
        JsonValue symbols = JsonValue::MakeArray();
        for (const LuaDeclaration& declaration : IndexLuaSource(target.text).declarations) {
            if (!declaration.atTopLevel) continue;
            JsonValue symbol = Object({
                { "name", Str(declaration.qualifier.empty() ? declaration.name : declaration.qualifier + "." + declaration.name) },
                { "kind", Num(declaration.isFunction ? kFunctionSymbol : kVariableSymbol) },
                { "range", RangeJson(target.lines, declaration.range) },
                { "selectionRange", RangeJson(target.lines, declaration.nameRange) },
            });
            if (declaration.isFunction) symbol.Set("detail", Str("(" + ParameterList(declaration) + ")"));
            symbols.Append(std::move(symbol));
        }
        return Ok(std::move(symbols));
    }

    const ScriptApiCatalog& catalog_;
    std::filesystem::path project_;
    std::ostream& out_;
    std::map<std::string, std::vector<const ScriptApiCatalogLuaBinding*>, std::less<>> tables_; // by Lua table; "" holds the globals
    std::map<std::string, Document> documents_;
    bool initialized_ = false;
    bool shutdown_ = false;
};

} // namespace

int RunLspCommand(const ArgumentList& arguments, std::istream& in, CommandIo io) {
    constexpr std::array<std::string_view, 1> allowed{ "--project" };
    if (!arguments.Errors().empty() || !arguments.UnknownOptions(allowed).empty() || !arguments.Positionals().empty() || arguments.Options("--project").size() > 1U) {
        io.err << "error: lsp expects [--project <dir>]\n";
        return 1;
    }
    const std::optional<std::string> project = arguments.Option("--project");
    ScriptApiCatalog catalog;
    std::string error;
    if (!BuildScriptApiCatalog(project, catalog, error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    LspServer server{ catalog, project ? std::filesystem::absolute(std::filesystem::path{ *project }) : std::filesystem::path{}, io.out };
    return server.Run(in);
}

} // namespace kb::cli
