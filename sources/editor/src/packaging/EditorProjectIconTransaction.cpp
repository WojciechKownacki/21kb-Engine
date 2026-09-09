#include "packaging/EditorProjectIconTransaction.hpp"

#include <array>
#include <fstream>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace kb::editor {
namespace {

[[nodiscard]] std::filesystem::path NormalizedAbsolute(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return error ? path.lexically_normal() : absolute.lexically_normal();
}

[[nodiscard]] std::string ProcessSuffix() {
#if defined(_WIN32)
    return std::to_string(GetCurrentProcessId());
#else
    return std::to_string(static_cast<unsigned long long>(getpid()));
#endif
}

[[nodiscard]] bool ExistingPathIsSafe(
    const std::filesystem::path& path,
    bool mustExist) noexcept {
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::symlink_status(path, error);
    if (error) {
        return !mustExist && error == std::errc::no_such_file_or_directory;
    }
    if (status.type() == std::filesystem::file_type::not_found) return !mustExist;
    if (std::filesystem::is_symlink(status)) return false;
#if defined(_WIN32)
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) return false;
#endif
    return true;
}

[[nodiscard]] bool ValidateTransactionPaths(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& destination,
    const std::filesystem::path& temporary,
    const std::filesystem::path& backup,
    bool parentMustExist,
    std::string& error) {
    const std::filesystem::path parent = projectRoot / "Branding";
    if (destination != parent / "ApplicationIcon.png" ||
        (!temporary.empty() && temporary.parent_path() != parent) ||
        (!backup.empty() && backup.parent_path() != parent) ||
        !ExistingPathIsSafe(projectRoot, true) || !std::filesystem::is_directory(projectRoot) ||
        !ExistingPathIsSafe(parent, parentMustExist) ||
        !ExistingPathIsSafe(destination, false) ||
        (!temporary.empty() && !ExistingPathIsSafe(temporary, false)) ||
        (!backup.empty() && !ExistingPathIsSafe(backup, false))) {
        error = "The project Branding path is not a safe local directory.";
        return false;
    }
    std::error_code filesystemError;
    if (std::filesystem::exists(parent, filesystemError)) {
        const std::filesystem::path canonicalParent = std::filesystem::canonical(parent, filesystemError);
        if (filesystemError || canonicalParent != parent) {
            error = "The project Branding path is not a safe local directory.";
            return false;
        }
    } else if (filesystemError || parentMustExist) {
        error = "The project Branding path is not a safe local directory.";
        return false;
    }
    return true;
}

} // namespace

EditorProjectIconTransaction::~EditorProjectIconTransaction() {
    try {
        std::string ignored;
        static_cast<void>(Rollback(ignored));
    } catch (...) {
    }
}

bool EditorProjectIconTransaction::Publish(
    const std::filesystem::path& source,
    const std::filesystem::path& projectRoot,
    std::string& error) {
    error.clear();
    if (active_) {
        error = "An application icon import is already active.";
        return false;
    }

    std::error_code filesystemError;
    const std::filesystem::path absoluteRoot = NormalizedAbsolute(projectRoot);
    if (!ExistingPathIsSafe(absoluteRoot, true) || !std::filesystem::is_directory(absoluteRoot, filesystemError) ||
        filesystemError) {
        error = "The project root is not a safe local directory.";
        return false;
    }
    const std::filesystem::path canonicalRoot = std::filesystem::canonical(absoluteRoot, filesystemError);
    if (filesystemError || canonicalRoot.empty()) {
        error = "The project root is not a safe local directory.";
        return false;
    }
    const std::filesystem::path destination = canonicalRoot / "Branding" / "ApplicationIcon.png";

    if (!std::filesystem::is_regular_file(source, filesystemError) || filesystemError) {
        error = "Application icon must be a readable PNG file.";
        return false;
    }
    constexpr std::array<unsigned char, 8> kPngSignature{
        0x89U, 0x50U, 0x4eU, 0x47U, 0x0dU, 0x0aU, 0x1aU, 0x0aU };
    std::array<unsigned char, kPngSignature.size()> signature{};
    std::ifstream input{ source, std::ios::binary };
    input.read(reinterpret_cast<char*>(signature.data()), static_cast<std::streamsize>(signature.size()));
    if (!input || signature != kPngSignature) {
        error = "Application icon must be a readable PNG file.";
        return false;
    }
    if (NormalizedAbsolute(source) == destination) return true;

    const std::string suffix = ProcessSuffix();
    const std::filesystem::path temporary = destination.parent_path() /
        ("ApplicationIcon.importing." + suffix + ".png");
    const std::filesystem::path backup = destination.parent_path() /
        ("ApplicationIcon.rollback." + suffix + ".png");
    if (!ValidateTransactionPaths(canonicalRoot, destination, temporary, backup, false, error)) return false;

    std::filesystem::create_directories(destination.parent_path(), filesystemError);
    if (filesystemError) {
        error = "The Branding directory could not be created.";
        return false;
    }
    if (!ValidateTransactionPaths(canonicalRoot, destination, temporary, backup, true, error)) return false;
    std::filesystem::remove(temporary, filesystemError);
    if (filesystemError) {
        error = "A previous application icon import could not be cleared.";
        return false;
    }
    std::filesystem::remove(backup, filesystemError);
    if (filesystemError) {
        error = "A previous application icon rollback file could not be cleared.";
        return false;
    }
    if (!ValidateTransactionPaths(canonicalRoot, destination, temporary, backup, true, error)) return false;
    std::filesystem::copy_file(source, temporary, std::filesystem::copy_options::overwrite_existing, filesystemError);
    if (filesystemError) {
        error = "Application icon could not be copied into the project.";
        return false;
    }

    hadPrevious_ = std::filesystem::exists(destination, filesystemError);
    if (filesystemError || (hadPrevious_ && !std::filesystem::is_regular_file(destination, filesystemError))) {
        if (ValidateTransactionPaths(canonicalRoot, destination, temporary, backup, true, error)) {
            std::filesystem::remove(temporary, filesystemError);
        }
        error = "The existing application icon is not a regular file.";
        hadPrevious_ = false;
        return false;
    }
    if (!ValidateTransactionPaths(canonicalRoot, destination, temporary, backup, true, error)) {
        hadPrevious_ = false;
        return false;
    }

#if defined(_WIN32)
    const bool published = hadPrevious_
        ? ReplaceFileW(destination.c_str(), temporary.c_str(), backup.c_str(), REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != FALSE
        : MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    bool published = true;
    if (hadPrevious_) {
        std::filesystem::rename(destination, backup, filesystemError);
        published = !filesystemError;
    }
    if (published) {
        std::filesystem::rename(temporary, destination, filesystemError);
        published = !filesystemError;
        if (!published && hadPrevious_) {
            std::error_code restoreError;
            std::filesystem::rename(backup, destination, restoreError);
        }
    }
#endif
    if (!published) {
        if (ValidateTransactionPaths(canonicalRoot, destination, temporary, backup, true, error)) {
            std::filesystem::remove(temporary, filesystemError);
        }
        error = "Application icon could not be published in the project.";
        hadPrevious_ = false;
        return false;
    }

    projectRoot_ = canonicalRoot;
    destination_ = destination;
    backup_ = backup;
    active_ = true;
    return true;
}

bool EditorProjectIconTransaction::Rollback(std::string& error) {
    error.clear();
    if (!active_) return true;

    std::error_code filesystemError;
    if (!ValidateTransactionPaths(projectRoot_, destination_, {}, backup_, true, error) ||
        (hadPrevious_ && !std::filesystem::is_regular_file(backup_, filesystemError))) {
        if (error.empty()) error = "The application icon rollback path is no longer safe.";
        return false;
    }
#if defined(_WIN32)
    bool restored = false;
    if (hadPrevious_) {
        const bool destinationExists = std::filesystem::exists(destination_, filesystemError);
        restored = !filesystemError && (destinationExists
            ? ReplaceFileW(destination_.c_str(), backup_.c_str(), nullptr,
                REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != FALSE
            : MoveFileExW(backup_.c_str(), destination_.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE);
    } else {
        const bool destinationExists = std::filesystem::exists(destination_, filesystemError);
        restored = !filesystemError && (!destinationExists || std::filesystem::remove(destination_, filesystemError));
    }
#else
    std::filesystem::remove(destination_, filesystemError);
    bool restored = !filesystemError;
    if (restored && hadPrevious_) {
        std::filesystem::rename(backup_, destination_, filesystemError);
        restored = !filesystemError;
    }
#endif
    if (!restored) {
        error = "The previous application icon could not be restored; its rollback copy was preserved.";
        return false;
    }

    active_ = false;
    projectRoot_.clear();
    destination_.clear();
    backup_.clear();
    hadPrevious_ = false;
    return true;
}

void EditorProjectIconTransaction::Commit() noexcept {
    if (!active_) return;
    std::string ignoredError;
    if (hadPrevious_ && ValidateTransactionPaths(
            projectRoot_, destination_, {}, backup_, true, ignoredError)) {
        std::error_code ignored;
        std::filesystem::remove(backup_, ignored);
    }
    active_ = false;
    projectRoot_.clear();
    destination_.clear();
    backup_.clear();
    hadPrevious_ = false;
}

} // namespace kb::editor
