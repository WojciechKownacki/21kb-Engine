#pragma once

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
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
    if (path.empty()) {
        return path;
    }
    // A path that has been through generic_string() carries its prefix as "//?/", which is still an
    // extended-length path and must not be prefixed a second time. Separators are settled first so
    // the check sees one spelling.
    std::wstring separated = path.native();
    std::ranges::replace(separated, L'/', L'\\');
    if (separated.starts_with(LR"(\\?\)") || separated.starts_with(LR"(\\.\)")) {
        return std::filesystem::path{ separated };
    }
    std::error_code error;
    const std::filesystem::path absolutePath = std::filesystem::absolute(std::filesystem::path{ separated }, error);
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

// A working directory for an external tool, unique to this call. bx, and so the shaderc built on
// it, opens files through the narrow CRT, which stops at MAX_PATH whatever prefix the path carries:
// a tool cannot read or write under a project the user put deep in their own folders. Its inputs,
// outputs and logs live here instead, and only the finished artifact is published where the project
// wants it. The caller owns the directory and deletes it; `error` says why nothing came back.
[[nodiscard]] std::filesystem::path ToolScratchPath(std::string_view prefix, std::error_code& error);

// The portable form of the same path: what a manifest stores, what a comparison uses and what a
// message shows. ExtendedLengthPath produces a path for handing to the operating system, not a
// path to keep, so anything that outlives the call comes back through here.
[[nodiscard]] inline std::filesystem::path PortablePath(std::filesystem::path path) {
#if defined(_WIN32)
    std::wstring native = path.native();
    std::ranges::replace(native, L'/', L'\\');
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
