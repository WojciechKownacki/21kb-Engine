#include "assets/AssetFileSystem.hpp"

#include <array>
#include <chrono>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_map>

namespace kb::assets {
namespace {

constexpr std::uint64_t FnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t FnvPrime = 1099511628211ULL;

// What HashFileUnlessUnchanged remembers of a file, keyed by its path.
struct HashedFile {
    std::uintmax_t size = 0U;
    std::filesystem::file_time_type written{};
    std::uint64_t hash = 0U;
};

constexpr std::chrono::seconds kSettledAfter{ 2 };
constexpr std::size_t kMaxRememberedFiles = 1U << 20U;

std::mutex& HashedFilesMutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::filesystem::path::string_type, HashedFile>& HashedFiles() {
    static std::unordered_map<std::filesystem::path::string_type, HashedFile> files;
    return files;
}

} // namespace

std::filesystem::path AssetFileSystem::UniqueFilePathInFolder(
    const std::filesystem::path& folder,
    const std::filesystem::path& filename) {
    std::filesystem::path candidate = (folder / filename).lexically_normal();
    std::error_code error;
    if (!std::filesystem::exists(candidate, error)) {
        return candidate;
    }

    const std::string stem = filename.stem().string();
    const std::string extension = filename.extension().string();
    for (int index = 1; index < 10000; ++index) {
        candidate = (folder / (stem + "_" + std::to_string(index) + extension)).lexically_normal();
        error.clear();
        if (!std::filesystem::exists(candidate, error)) {
            return candidate;
        }
    }
    return {};
}

std::filesystem::path AssetFileSystem::UniqueFolderPathInFolder(
    const std::filesystem::path& folder,
    const std::filesystem::path& folderName) {
    const std::string name = folderName.string();
    std::filesystem::path candidate = (folder / name).lexically_normal();
    std::error_code error;
    if (!std::filesystem::exists(candidate, error)) {
        return candidate;
    }

    for (int index = 1; index < 10000; ++index) {
        candidate = (folder / (name + "_" + std::to_string(index))).lexically_normal();
        error.clear();
        if (!std::filesystem::exists(candidate, error)) {
            return candidate;
        }
    }
    return {};
}

bool AssetFileSystem::MoveFileReplacingNothing(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    if (!error) {
        return true;
    }

    error.clear();
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, error);
    if (error) {
        return false;
    }

    error.clear();
    std::filesystem::remove(source, error);
    return !error;
}

bool AssetFileSystem::MoveFolderReplacingNothing(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    if (!error) {
        return true;
    }

    error.clear();
    std::filesystem::copy(source, destination, std::filesystem::copy_options::recursive, error);
    if (error) {
        std::error_code cleanupError;
        std::filesystem::remove_all(destination, cleanupError);
        return false;
    }

    error.clear();
    std::filesystem::remove_all(source, error);
    return !error;
}

std::uint64_t AssetFileSystem::HashFile(const std::filesystem::path& path) noexcept {
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        return 0;
    }

    std::uint64_t hash = FnvOffset;
    std::array<char, 64U * 1024U> buffer;
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        for (std::streamsize index = 0; index < count; ++index) {
            hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)]);
            hash *= FnvPrime;
        }
    }
    if (input.bad() || !input.eof()) {
        return 0;
    }
    return hash == 0 ? FnvPrime : hash;
}

std::uint64_t AssetFileSystem::HashFileUnlessUnchanged(const std::filesystem::path& path) noexcept {
    try {
        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(path, error);
        if (error) {
            return HashFile(path);
        }
        const std::filesystem::file_time_type written = std::filesystem::last_write_time(path, error);
        if (error) {
            return HashFile(path);
        }
        {
            std::scoped_lock lock{ HashedFilesMutex() };
            const auto found = HashedFiles().find(path.native());
            if (found != HashedFiles().end() && found->second.size == size && found->second.written == written) {
                return found->second.hash;
            }
        }
        // The stamp was read before the content, so a write that lands in between leaves a stamp that no longer
        // matches the file and the next call reads it again.
        const std::uint64_t hash = HashFile(path);
        if (hash != 0U && std::filesystem::file_time_type::clock::now() - written > kSettledAfter) {
            std::scoped_lock lock{ HashedFilesMutex() };
            if (HashedFiles().size() >= kMaxRememberedFiles) {
                HashedFiles().clear();
            }
            HashedFiles()[path.native()] = HashedFile{ .size = size, .written = written, .hash = hash };
        }
        return hash;
    } catch (...) {
        return HashFile(path);
    }
}

} // namespace kb::assets
