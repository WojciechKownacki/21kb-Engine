#pragma once

#include <cstdint>
#include <filesystem>

namespace kb::assets {

class AssetFileSystem {
public:
    AssetFileSystem() = delete;

    [[nodiscard]] static std::filesystem::path UniqueFilePathInFolder(
        const std::filesystem::path& folder,
        const std::filesystem::path& filename);
    [[nodiscard]] static std::filesystem::path UniqueFolderPathInFolder(
        const std::filesystem::path& folder,
        const std::filesystem::path& folderName);
    [[nodiscard]] static bool MoveFileReplacingNothing(
        const std::filesystem::path& source,
        const std::filesystem::path& destination);
    [[nodiscard]] static bool MoveFolderReplacingNothing(
        const std::filesystem::path& source,
        const std::filesystem::path& destination);
    [[nodiscard]] static std::uint64_t HashFile(const std::filesystem::path& path) noexcept;
};

} // namespace kb::assets
