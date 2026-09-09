#pragma once

#include <optional>
#include <string_view>
#include <vector>

namespace kb::editor::package_process {

#if defined(_WIN32)
[[nodiscard]] bool IsBlockedEnvironmentVariable(std::wstring_view name) noexcept;
[[nodiscard]] std::optional<std::vector<wchar_t>> BuildSanitizedEnvironment();
#endif

} // namespace kb::editor::package_process
