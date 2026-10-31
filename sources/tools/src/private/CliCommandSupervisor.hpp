#pragma once

#include "CliCommon.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace kb::cli {

// Executable-level limits are prefix options. Return no value when no limits
// were requested, allowing the normal in-process dispatcher to run unchanged.
[[nodiscard]] std::optional<int> SuperviseCliCommand(const std::filesystem::path& executable,
    std::span<const std::string> arguments, CommandIo io);

} // namespace kb::cli
