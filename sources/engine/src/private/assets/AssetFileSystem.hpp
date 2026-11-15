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
    // HashFile that skips reading a file it has already hashed: the remembered hash is returned while the file's size
    // and last write time are unchanged. Only a file written more than a couple of seconds ago is remembered, because
    // a file written again within the same time stamp would keep its stamp with different content. Safe from any thread.
    [[nodiscard]] static std::uint64_t HashFileUnlessUnchanged(const std::filesystem::path& path) noexcept;
};

} // namespace kb::assets
