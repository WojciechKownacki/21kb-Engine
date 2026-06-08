#include "project/ProjectDescriptorBinaryIO.hpp"

#include <fstream>
#include <iterator>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::project::ProjectDescriptorBinaryIO {
namespace {

[[nodiscard]] bool PrepareOutputPath(const std::filesystem::path& path) {
    if (!path.has_parent_path()) {
        return true;
    }

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    return !error;
}

[[nodiscard]] std::filesystem::path TempPathFor(const std::filesystem::path& path) {
    std::filesystem::path tempPath = path;
    tempPath += ".tmp";
    return tempPath;
}

[[nodiscard]] bool ReplaceFileAtomically(const std::filesystem::path& source, const std::filesystem::path& destination) {
#if defined(_WIN32)
    return MoveFileExW(source.wstring().c_str(), destination.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    return !error;
#endif
}

} // namespace

std::vector<std::uint8_t> ReadAllBytes(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        return {};
    }
    return std::vector<std::uint8_t>{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

bool WriteBytesAtomically(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    if (!PrepareOutputPath(path) || bytes.empty()) {
        return false;
    }

    const std::filesystem::path tempPath = TempPathFor(path);
    {
        std::ofstream output{ tempPath, std::ios::binary | std::ios::trunc };
        if (!output.is_open()) {
            return false;
        }
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output) {
            std::error_code error;
            std::filesystem::remove(tempPath, error);
            return false;
        }
    }

    if (!ReplaceFileAtomically(tempPath, path)) {
        std::error_code error;
        std::filesystem::remove(tempPath, error);
        return false;
    }
    return true;
}

} // namespace kb::project::ProjectDescriptorBinaryIO
