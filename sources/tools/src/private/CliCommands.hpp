#pragma once

#include "CliCommon.hpp"

#include <istream>

namespace kb::cli {

[[nodiscard]] int RunApiCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunApiCheckCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunInitAgentCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunValidateCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunSceneListCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunSceneAttachCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunRunCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunMcpCommand(const ArgumentList& arguments, std::istream& in, CommandIo io);

} // namespace kb::cli
