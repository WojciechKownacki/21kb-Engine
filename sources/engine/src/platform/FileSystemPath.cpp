#include "engine/platform/FileSystemPath.hpp"

#include <atomic>
#include <cstdint>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace kb::platform {

std::filesystem::path ToolScratchPath(std::string_view prefix, std::error_code& error) {
    static std::atomic<std::uint64_t> serial{ 0U };
    const std::filesystem::path root = std::filesystem::temp_directory_path(error) / "21kb-tool-scratch";
    if (error) {
        return {};
    }
    std::filesystem::create_directories(root, error);
    if (error) {
        return {};
    }
    // The process id keeps two cooks running side by side out of each other's directory; the serial
    // keeps two of them inside one process apart.
#if defined(_WIN32)
    const auto processId = static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    const auto processId = static_cast<std::uint64_t>(getpid());
#endif
    return root / (".kb-" + std::string{ prefix } + "." + std::to_string(processId) + "." +
        std::to_string(serial.fetch_add(1U, std::memory_order_relaxed)));
}

} // namespace kb::platform
