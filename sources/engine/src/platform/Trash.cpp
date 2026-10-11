#include "engine/platform/Trash.hpp"

#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <unistd.h>
#endif

namespace kb::platform {

#if defined(_WIN32)

bool MoveToTrash(const std::filesystem::path& path, std::string& error) {
    std::error_code pathError;
    const std::filesystem::path absolute = std::filesystem::absolute(path, pathError).lexically_normal();
    if (pathError || !std::filesystem::exists(absolute, pathError)) {
        error = "it no longer exists";
        return false;
    }
    std::wstring from = absolute.native();
    if (from.size() >= MAX_PATH) {
        error = "its path is too long for the Recycle Bin (" + std::to_string(from.size()) + " characters)";
        return false;
    }
    from.push_back(L'\0'); // the list of paths ends with a second terminator
    SHFILEOPSTRUCTW operation{};
    operation.wFunc = FO_DELETE;
    operation.pFrom = from.c_str();
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    const int result = SHFileOperationW(&operation);
    if (result != 0 || operation.fAnyOperationsAborted != FALSE) {
        error = "the Recycle Bin refused it (shell error " + std::to_string(result) + ")";
        return false;
    }
    return true;
}

#else

namespace {

// The freedesktop.org home trash: $XDG_DATA_HOME/Trash, else ~/.local/share/Trash.
[[nodiscard]] std::filesystem::path HomeTrash() {
    if (const char* dataHome = std::getenv("XDG_DATA_HOME"); dataHome != nullptr && dataHome[0] != '\0') {
        return std::filesystem::path{ dataHome } / "Trash";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::filesystem::path{ home } / ".local" / "share" / "Trash";
    }
    return {};
}

// Path= in a .trashinfo file is a URL-escaped absolute path.
[[nodiscard]] std::string EscapePath(const std::string& path) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string escaped;
    for (const char character : path) {
        const auto byte = static_cast<unsigned char>(character);
        const bool plain = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
            byte == '/' || byte == '-' || byte == '_' || byte == '.' || byte == '~';
        if (plain) {
            escaped.push_back(character);
        } else {
            escaped.push_back('%');
            escaped.push_back(kHex[byte >> 4U]);
            escaped.push_back(kHex[byte & 0x0FU]);
        }
    }
    return escaped;
}

} // namespace

bool MoveToTrash(const std::filesystem::path& path, std::string& error) {
    std::error_code pathError;
    const std::filesystem::path absolute = std::filesystem::absolute(path, pathError).lexically_normal();
    if (pathError || !std::filesystem::exists(absolute, pathError)) {
        error = "it no longer exists";
        return false;
    }
    const std::filesystem::path trash = HomeTrash();
    if (trash.empty()) {
        error = "there is no home folder for the Trash (HOME is not set)";
        return false;
    }
    std::filesystem::create_directories(trash / "files", pathError);
    std::filesystem::create_directories(trash / "info", pathError);
    if (pathError) {
        error = "the Trash folder could not be created: " + pathError.message();
        return false;
    }

    // A name not yet in the Trash: "Name", "Name.2", "Name.3"...
    const std::string base = absolute.filename().string();
    std::string name = base;
    for (int attempt = 2; std::filesystem::exists(trash / "files" / name, pathError) ||
                          std::filesystem::exists(trash / "info" / (name + ".trashinfo"), pathError);
         ++attempt) {
        name = base + "." + std::to_string(attempt);
    }

    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char date[32]{};
    std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &local);
    const std::filesystem::path info = trash / "info" / (name + ".trashinfo");
    {
        std::ofstream output{ info, std::ios::binary | std::ios::trunc };
        output << "[Trash Info]\nPath=" << EscapePath(absolute.string()) << "\nDeletionDate=" << date << "\n";
        if (!output.good()) {
            error = "the Trash entry could not be written";
            return false;
        }
    }

    std::filesystem::rename(absolute, trash / "files" / name, pathError);
    if (pathError) {
        // Another file system than the home Trash: copy, then remove the original.
        pathError.clear();
        std::filesystem::copy(absolute, trash / "files" / name, std::filesystem::copy_options::recursive, pathError);
        if (!pathError) {
            std::filesystem::remove_all(absolute, pathError);
        }
        if (pathError) {
            std::filesystem::remove_all(trash / "files" / name, pathError);
            std::filesystem::remove(info, pathError);
            error = "it could not be moved to the Trash";
            return false;
        }
    }
    return true;
}

#endif

} // namespace kb::platform
