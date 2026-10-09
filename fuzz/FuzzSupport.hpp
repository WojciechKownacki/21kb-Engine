#pragma once

// Shared plumbing for the libFuzzer harnesses in fuzz/targets. Each harness feeds
// one untrusted input format to the same reader the engine uses at run time and
// lets AddressSanitizer judge the result; parse failures are expected and ignored.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace kb::fuzz {

[[nodiscard]] inline std::span<const std::uint8_t> Bytes(const std::uint8_t* data, std::size_t size) noexcept {
    return { data, size };
}

[[nodiscard]] inline std::string_view Text(const std::uint8_t* data, std::size_t size) noexcept {
    return { reinterpret_cast<const char*>(data), size };
}

// Readers that only accept a path get the input through a file private to this
// process, under the system temporary directory.
[[nodiscard]] inline std::filesystem::path WriteScratchFile(
    const std::uint8_t* data, std::size_t size, std::string_view fileName) {
    static const std::filesystem::path directory = [] {
#if defined(_WIN32)
        const auto process = static_cast<long long>(_getpid());
#else
        const auto process = static_cast<long long>(getpid());
#endif
        std::filesystem::path path = std::filesystem::temp_directory_path() / ("kb-fuzz-" + std::to_string(process));
        std::filesystem::create_directories(path);
        return path;
    }();
    const std::filesystem::path path = directory / fileName;
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return path;
}

} // namespace kb::fuzz
