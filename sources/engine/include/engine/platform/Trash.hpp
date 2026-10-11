#pragma once

#include <filesystem>
#include <string>

namespace kb::platform {

// Moves a file or a folder to the system's bin, where the user can restore it - the Recycle Bin on
// Windows, the desktop Trash on Linux (freedesktop.org Trash specification). Project Files deletes
// through it, like Unity and Unreal. Returns false with `error` naming the reason; the item is then
// left where it was.
[[nodiscard]] bool MoveToTrash(const std::filesystem::path& path, std::string& error);

} // namespace kb::platform
