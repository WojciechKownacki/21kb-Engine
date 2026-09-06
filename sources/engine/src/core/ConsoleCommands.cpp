#include "engine/core/ConsoleCommands.hpp"

#include "engine/library/EngineLibraryParsing.hpp"

#include <charconv>
#include <cstddef>
#include <system_error>

namespace kb::core {

std::string_view ToString(ConsoleArgumentType type) noexcept {
    switch (type) {
    case ConsoleArgumentType::Bool:
        return "bool";
    case ConsoleArgumentType::Integer:
        return "int";
    case ConsoleArgumentType::Float:
        return "float";
    case ConsoleArgumentType::String:
        return "string";
    }
    return "unknown";
}

bool IsValidConsoleArgument(ConsoleArgumentType type, std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    if (type == ConsoleArgumentType::String) {
        return true;
    }
    if (type == ConsoleArgumentType::Bool) {
        return value == "true" || value == "false";
    }
    if (type == ConsoleArgumentType::Integer) {
        std::int64_t parsed = 0;
        const std::from_chars_result result = std::from_chars(value.data(), value.data() + value.size(), parsed);
        return result.ec == std::errc{} && result.ptr == value.data() + value.size();
    }
    double parsed = 0.0;
    return kb::library::TryParseDouble(value, parsed);
}

bool CanExecute(const ConsoleCommand& command, ConsolePermission caller,
                const std::vector<std::string_view>& values) noexcept {
    if (static_cast<unsigned>(caller) < static_cast<unsigned>(command.permission) ||
        values.size() != command.arguments.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < values.size(); ++index) {
        if (!IsValidConsoleArgument(command.arguments[index].type, values[index])) {
            return false;
        }
    }
    return true;
}

std::string HelpFromManifest(const ConsoleCommand& command) {
    std::string result = command.name;
    for (const ConsoleArgument& argument : command.arguments) {
        result += " <" + argument.name + ":" + std::string{ToString(argument.type)} + ">";
    }
    return result + " — " + command.help;
}

} // namespace kb::core
