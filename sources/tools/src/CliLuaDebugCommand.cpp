#include "CliCommands.hpp"
#include "engine/script/LuaDebugFileTransport.hpp"
#include <array>
#include <charconv>
#include <fstream>

namespace kb::cli {
int RunLuaDebugCommand(const ArgumentList& arguments, CommandIo io) {
    constexpr std::array<std::string_view, 6> allowed{"--endpoint", "--command", "--expression", "--frame", "--breakpoints", "--timeout-ms"};
    if (!arguments.Errors().empty() || !arguments.UnknownOptions(allowed).empty() || !arguments.Positionals().empty()) {
        io.err << "error: debug expects --endpoint <dir> --command <command> [--expression <expression>] [--frame <index>] [--breakpoints <json-file>] [--timeout-ms <milliseconds>]\n";
        return 1;
    }
    for (auto name : allowed) if (arguments.Options(name).size() > 1U) { io.err << "error: duplicate debug option\n"; return 1; }
    const auto endpoint = arguments.Option("--endpoint");
    const auto command = arguments.Option("--command");
    if (!endpoint || !command) { io.err << "error: debug requires endpoint and command\n"; return 1; }
    auto request = kb::core::JsonValue::MakeObject();
    request.Set("schema", kb::core::JsonValue::MakeString("21kb.lua-debug/v1"));
    request.Set("command", kb::core::JsonValue::MakeString(std::string{*command}));
    if (const auto expression = arguments.Option("--expression")) request.Set("expression", kb::core::JsonValue::MakeString(std::string{*expression}));
    int frame = 0, timeout = 5000;
    for (auto [name, destination] : {std::pair{"--frame", &frame}, std::pair{"--timeout-ms", &timeout}}) {
        if (const auto value = arguments.Option(name)) {
            const auto parsed = std::from_chars(value->data(), value->data() + value->size(), *destination);
            if (parsed.ec != std::errc{} || parsed.ptr != value->data() + value->size()) { io.err << "error: invalid debug integer\n"; return 1; }
        }
    }
    if (frame < 0 || frame > 127 || timeout < 1 || timeout > 60000) { io.err << "error: debug integer outside limits\n"; return 1; }
    request.Set("frame", kb::core::JsonValue::MakeNumber(frame));
    if (const auto path = arguments.Option("--breakpoints")) {
        std::error_code error;
        const std::filesystem::path file{std::string{*path}};
        const auto size = std::filesystem::file_size(file, error);
        if (error || size > 60000U) { io.err << "error: cannot read bounded breakpoint file\n"; return 1; }
        std::string text(static_cast<std::size_t>(size), '\0');
        std::ifstream input(file, std::ios::binary);
        input.read(text.data(), static_cast<std::streamsize>(text.size()));
        kb::core::JsonValue points;
        std::string parseError;
        if (!input.good() || !kb::core::JsonValue::Parse(text, points, parseError)) { io.err << "error: invalid breakpoint file\n"; return 1; }
        request.Set("breakpoints", std::move(points));
    }
    const auto response = kb::script::SendLuaDebugFileRequest(std::filesystem::path{std::string{*endpoint}}, request.Dump(), std::chrono::milliseconds{timeout});
    io.out << response.Dump() << '\n';
    const auto* succeeded = response.Find("succeeded");
    return succeeded && succeeded->AsBool() && io.out.good() ? 0 : 1;
}
} // namespace kb::cli
