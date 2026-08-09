#pragma once

#include "engine/library/EngineLibraryParsing.hpp"

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kb::core {

enum class ConsolePermission : std::uint8_t { User, Developer, Admin };
enum class ConsoleArgumentType : std::uint8_t { Bool, Integer, Float, String };

struct ConsoleArgument {
    std::string name;
    ConsoleArgumentType type = ConsoleArgumentType::String;
};

struct ConsoleCommand {
    std::string name;
    std::string help;
    std::vector<ConsoleArgument> arguments;
    ConsolePermission permission = ConsolePermission::User;
};

[[nodiscard]] inline std::string_view ToString(ConsoleArgumentType type) noexcept {
    switch (type) { case ConsoleArgumentType::Bool: return "bool"; case ConsoleArgumentType::Integer: return "int"; case ConsoleArgumentType::Float: return "float"; case ConsoleArgumentType::String: return "string"; }
    return "unknown";
}

[[nodiscard]] inline bool IsValidConsoleArgument(ConsoleArgumentType type, std::string_view value) noexcept {
    if (value.empty()) return false;
    if (type == ConsoleArgumentType::String) return true;
    if (type == ConsoleArgumentType::Bool) return value == "true" || value == "false";
    if (type == ConsoleArgumentType::Integer) { std::int64_t parsed{}; const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed); return error == std::errc{} && end == value.data() + value.size(); }
    double parsed{};
    return kb::library::TryParseDouble(value, parsed);
}

[[nodiscard]] inline bool CanExecute(const ConsoleCommand& command, ConsolePermission caller, const std::vector<std::string_view>& values) noexcept {
    if (static_cast<unsigned>(caller) < static_cast<unsigned>(command.permission) || values.size() != command.arguments.size()) return false;
    for (std::size_t index = 0U; index < values.size(); ++index) if (!IsValidConsoleArgument(command.arguments[index].type, values[index])) return false;
    return true;
}

[[nodiscard]] inline std::string HelpFromManifest(const ConsoleCommand& command) {
    std::string result = command.name;
    for (const ConsoleArgument& argument : command.arguments) result += " <" + argument.name + ":" + std::string{ToString(argument.type)} + ">";
    return result + " — " + command.help;
}

} // namespace kb::core
