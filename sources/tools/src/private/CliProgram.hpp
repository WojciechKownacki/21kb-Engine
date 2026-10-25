#pragma once

#include "CliCommon.hpp"

#include <istream>
#include <span>
#include <string>

namespace kb::cli {

// The kb_cli program: `args` are the command line after the program name. Exits 0 on success and
// 1 on any failure. With --json, prints one result object instead of the command's text.
[[nodiscard]] int RunCli(std::span<const std::string> args, std::istream& in, CommandIo io);

} // namespace kb::cli
