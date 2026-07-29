#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace kb::core { enum class ConsolePermission:std::uint8_t{User,Developer,Admin}; struct ConsoleCommand {std::string name;std::string help;std::vector<std::string> argumentTypes;ConsolePermission permission=ConsolePermission::User;}; [[nodiscard]] inline bool CanExecute(const ConsoleCommand& command,ConsolePermission caller,std::size_t arguments)noexcept{return static_cast<unsigned>(caller)>=static_cast<unsigned>(command.permission)&&arguments==command.argumentTypes.size();} }
