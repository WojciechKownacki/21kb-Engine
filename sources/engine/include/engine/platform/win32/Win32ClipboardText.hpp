#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace kb::platform {
// UTF-8 at the engine boundary; CF_UNICODETEXT at the native boundary.
// Transfers are bounded to one MiB. Invalid encodings are refused.
[[nodiscard]] bool WriteWin32ClipboardText(void* owner, std::string_view text) noexcept;
[[nodiscard]] std::optional<std::string> ReadWin32ClipboardText(
    void* owner, std::size_t maximumBytes = 1024U * 1024U);
} // namespace kb::platform
