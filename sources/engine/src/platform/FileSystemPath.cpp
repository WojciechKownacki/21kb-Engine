#include "engine/platform/FileSystemPath.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

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
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
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

std::filesystem::path RunningExecutablePath() {
#if defined(_WIN32)
    // A path longer than the buffer comes back cut and with the length of the buffer: grow and ask again.
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0U) {
            return {};
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return std::filesystem::path{ buffer };
        }
        if (buffer.size() >= 32768U) {
            return {};
        }
        buffer.resize(buffer.size() * 2U);
    }
#elif defined(__APPLE__)
    std::uint32_t size = 1024U;
    std::vector<char> buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        buffer.assign(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
            return {};
        }
    }
    return std::filesystem::path{ buffer.data() };
#else
    std::vector<char> buffer(4096U, '\0');
    const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1U);
    return length <= 0 ? std::filesystem::path{} : std::filesystem::path{ std::string(buffer.data(), static_cast<std::size_t>(length)) };
#endif
}

} // namespace kb::platform
