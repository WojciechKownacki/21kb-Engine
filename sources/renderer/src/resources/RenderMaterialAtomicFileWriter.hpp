#pragma once

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace kb::render::detail {

inline std::atomic_uint64_t gMaterialAtomicTempSerial{ 0U };

[[nodiscard]] inline std::filesystem::path MaterialAtomicTempPath(const std::filesystem::path& path) {
    std::filesystem::path temp = path;
#if defined(_WIN32)
    temp += ".tmp." + std::to_string(GetCurrentProcessId()) + "." + std::to_string(++gMaterialAtomicTempSerial);
#else
    temp += ".tmp." + std::to_string(++gMaterialAtomicTempSerial);
#endif
    return temp;
}

[[nodiscard]] inline bool ReplaceMaterialFileAtomically(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
#if defined(_WIN32)
    const std::wstring sourceText = source.wstring();
    const std::wstring destinationText = destination.wstring();
    return MoveFileExW(
               sourceText.c_str(),
               destinationText.c_str(),
               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    return !error;
#endif
}

inline void RemoveMaterialTempFile(const std::filesystem::path& path) noexcept {
    std::error_code error;
    static_cast<void>(std::filesystem::remove(path, error));
}

template <typename WriteDocument>
[[nodiscard]] bool WriteMaterialFileAtomically(
    const std::filesystem::path& path,
    WriteDocument&& writeDocument) {
    std::error_code error;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return false;
        }
    }

    std::filesystem::path tempPath;
    try {
        tempPath = MaterialAtomicTempPath(path);
        std::ofstream output{ tempPath, std::ios::trunc | std::ios::binary };
        if (!output.is_open()) {
            RemoveMaterialTempFile(tempPath);
            return false;
        }
        writeDocument(output);
        output.flush();
        output.close();
        if (!output) {
            RemoveMaterialTempFile(tempPath);
            return false;
        }
        if (!ReplaceMaterialFileAtomically(tempPath, path)) {
            RemoveMaterialTempFile(tempPath);
            return false;
        }
        return true;
    } catch (...) {
        if (!tempPath.empty()) {
            RemoveMaterialTempFile(tempPath);
        }
        return false;
    }
}

} // namespace kb::render::detail
