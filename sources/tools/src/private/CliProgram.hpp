#pragma once

#include "CliCommon.hpp"

#include <functional>
#include <istream>
#include <span>
#include <string>
#include <string_view>

namespace kb::cli {

enum class CliCommandKind { Normal, Particle, Batch, Mcp, Help, Catalog };

struct CliCommandDescriptor {
    std::string_view name;
    int (*run)(const ArgumentList&, CommandIo) = nullptr;
    CliCommandKind kind = CliCommandKind::Normal;
    bool available = true;
    std::string_view mcpTool;
};

[[nodiscard]] std::span<const CliCommandDescriptor> CliCommandCatalog() noexcept;
[[nodiscard]] const CliCommandDescriptor* FindCliCommand(std::string_view name) noexcept;

// The kb_cli program: `args` are the command line after the program name. Exits 0 on success and
// 1 on any failure. With --json, prints one result object instead of the command's text.
[[nodiscard]] int RunCli(std::span<const std::string> args, std::istream& in, CommandIo io);

// Runs one kb_cli command line (the words after the program name).
using CommandRunner = std::function<int(std::span<const std::string>, CommandIo)>;

// batch: runs the operations of a file as one transaction on a project; see the usage text.
[[nodiscard]] int RunBatchCommand(const ArgumentList& arguments, CommandIo io, const CommandRunner& run);

// Text as JSON can carry it: bytes that are not valid UTF-8 become U+FFFD.
[[nodiscard]] std::string ToValidUtf8(std::string_view text);

} // namespace kb::cli
