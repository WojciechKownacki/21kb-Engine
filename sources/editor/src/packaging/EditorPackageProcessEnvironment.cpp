#include "packaging/EditorPackageProcessEnvironment.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <algorithm>
#include <array>
#include <ranges>
#include <string>
#include <string_view>

namespace kb::editor::package_process {

#if defined(_WIN32)
bool IsBlockedEnvironmentVariable(std::wstring_view name) noexcept {
    constexpr std::array<std::wstring_view, 6> blocked{
        L"ANDROID_KEYSTORE_PASSWORD",
        L"ANDROID_KEY_PASSWORD",
        L"KB_ANDROID_SIGNING_STORE_PASSWORD",
        L"KB_ANDROID_SIGNING_KEY_PASSWORD",
        L"ORG_GRADLE_PROJECT_kbSigningStorePassword",
        L"ORG_GRADLE_PROJECT_kbSigningKeyPassword",
    };
    return std::ranges::any_of(blocked, [&](std::wstring_view candidate) {
        return name.size() == candidate.size() && CompareStringOrdinal(
            name.data(), static_cast<int>(name.size()),
            candidate.data(), static_cast<int>(candidate.size()), TRUE) == CSTR_EQUAL;
    });
}

std::optional<std::vector<wchar_t>> BuildSanitizedEnvironment() {
    LPWCH inherited = GetEnvironmentStringsW();
    if (inherited == nullptr) return std::nullopt;
    std::vector<std::wstring> entries;
    for (const wchar_t* cursor = inherited; *cursor != L'\0';) {
        const std::wstring_view entry{ cursor };
        cursor += entry.size() + 1U;
        const std::size_t separator = entry.find(L'=');
        const std::wstring_view name = entry.substr(0U, separator);
        if (!IsBlockedEnvironmentVariable(name)) entries.emplace_back(entry);
    }
    FreeEnvironmentStringsW(inherited);
    std::ranges::sort(entries, [](const std::wstring& left, const std::wstring& right) {
        return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    std::vector<wchar_t> block;
    for (const std::wstring& entry : entries) {
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}
#endif

} // namespace kb::editor::package_process
