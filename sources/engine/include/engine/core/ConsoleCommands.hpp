#pragma once

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

[[nodiscard]] std::string_view ToString(ConsoleArgumentType type) noexcept;
[[nodiscard]] bool IsValidConsoleArgument(ConsoleArgumentType type, std::string_view value) noexcept;
[[nodiscard]] bool CanExecute(const ConsoleCommand& command, ConsolePermission caller,
                              const std::vector<std::string_view>& values) noexcept;
[[nodiscard]] std::string HelpFromManifest(const ConsoleCommand& command);

} // namespace kb::core
