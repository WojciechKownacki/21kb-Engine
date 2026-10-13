#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace kb::assets {

// Why `name` cannot be the name of a file or folder of a project, or an empty string when it can.
// One rule for the editor (Project Files, New Scene) and kb_cli: no path separators, none of the
// characters Windows refuses (< > : " | ? * and control characters), not "." or "..", not a name
// Windows reserves for devices (CON, NUL, COM1...) even with an extension, and no space or dot at
// the end. Letters of any language are fine; `name` is UTF-8.
[[nodiscard]] std::string AssetFileNameError(std::string_view name);

// A folder of the project's Assets named the way kb_cli and the editor's dialogs take it: relative to
// the project root ("Assets/Scenes") or virtual ("/Game/Scenes"), with either slash. `folder` is where
// it is on disk under `projectRoot` (it may not exist yet), `virtualPath` its /Game path; `error` says
// why `text` names no folder of Assets (outside Assets, or a part AssetFileNameError refuses).
struct ProjectAssetFolder {
    std::filesystem::path folder;
    std::string virtualPath;
    std::string error;
};
[[nodiscard]] ProjectAssetFolder ResolveProjectAssetFolder(const std::filesystem::path& projectRoot, std::string_view text);

// `text` (UTF-8) as a path, whatever the process code page is.
[[nodiscard]] std::filesystem::path Utf8Path(std::string_view text);

} // namespace kb::assets
