#pragma once

#include <string>
#include <string_view>

namespace kb::assets {

// Why `name` cannot be the name of a file or folder of a project, or an empty string when it can.
// One rule for the editor (Project Files, New Scene) and kb_cli: no path separators, none of the
// characters Windows refuses (< > : " | ? * and control characters), not "." or "..", not a name
// Windows reserves for devices (CON, NUL, COM1...) even with an extension, and no space or dot at
// the end. Letters of any language are fine; `name` is UTF-8.
[[nodiscard]] std::string AssetFileNameError(std::string_view name);

} // namespace kb::assets
