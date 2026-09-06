#pragma once

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>

namespace kb::platform {

// Win32 resolves an ordinary path against MAX_PATH (260 characters); only the extended-length
// prefix lifts that ceiling to 32767. std::filesystem applies that prefix itself, so a deep path
// that reaches the operating system only through it works, while the same path handed straight to
// CreateFileW fails in the worst possible way: the directory creates, and the file inside it cannot
// be opened. Any raw Win32 call on a path built under a root the user chooses - a project, a cache,
// a bake store - goes through here first, so the two halves agree on what a path means. Off Windows
// the path is returned unchanged.
[[nodiscard]] inline std::filesystem::path ExtendedLengthPath(std::filesystem::path path) {
#if defined(_WIN32)
    if (path.empty() || path.native().starts_with(LR"(\\?\)") || path.native().starts_with(LR"(\\.\)")) {
        return path;
    }
    std::error_code error;
    const std::filesystem::path absolutePath = std::filesystem::absolute(path, error);
    if (error) {
        return path;
    }
    // An extended-length path is passed to the object manager verbatim: no '/' separators, no '.'
    // or '..' components and no trailing separator, or the final name resolves to something other
    // than the entry meant.
    const std::filesystem::path normalized = absolutePath.lexically_normal();
    std::wstring native = normalized.native();
    std::ranges::replace(native, L'/', L'\\');
    const std::size_t rootLength = normalized.root_path().native().size();
    while (native.size() > rootLength && native.back() == L'\\') {
        native.pop_back();
    }
    if (native.starts_with(LR"(\\)")) {
        return std::filesystem::path{ std::wstring{ LR"(\\?\UNC)" } + native.substr(1U) };
    }
    return std::filesystem::path{ std::wstring{ LR"(\\?\)" } + native };
#else
    return path;
#endif
}

// The portable form of the same path: what a manifest stores, what a comparison uses and what a
// message shows. ExtendedLengthPath produces a path for handing to the operating system, not a
// path to keep, so anything that outlives the call comes back through here.
[[nodiscard]] inline std::filesystem::path PortablePath(std::filesystem::path path) {
#if defined(_WIN32)
    const std::wstring& native = path.native();
    if (native.starts_with(LR"(\\?\UNC\)")) {
        return std::filesystem::path{ LR"(\\)" + native.substr(8U) };
    }
    if (native.starts_with(LR"(\\?\)")) {
        return std::filesystem::path{ native.substr(4U) };
    }
    return path;
#else
    return path;
#endif
}

} // namespace kb::platform
