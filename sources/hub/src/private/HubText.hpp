#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace kb::hub {

class HubText {
public:
    HubText() = delete;

    [[nodiscard]] static std::string WideToUtf8(std::wstring_view text);
    [[nodiscard]] static std::wstring Utf8ToWide(std::string_view text);
    [[nodiscard]] static std::wstring PathLabel(const std::filesystem::path& path);
    [[nodiscard]] static std::wstring SanitizeProjectName(std::wstring name);
};

} // namespace kb::hub
