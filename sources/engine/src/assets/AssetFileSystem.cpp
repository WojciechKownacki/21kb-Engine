#include "assets/AssetFileSystem.hpp"

#include <fstream>
#include <string>
#include <system_error>

namespace kb::assets {
namespace {

constexpr std::uint64_t FnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t FnvPrime = 1099511628211ULL;

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
    char value = 0;
    while (input.get(value)) {
        hash ^= static_cast<unsigned char>(value);
        hash *= FnvPrime;
    }
    return hash == 0 ? FnvPrime : hash;
}

} // namespace kb::assets
