#pragma once

#include "HubState.hpp"

#include <filesystem>
#include <optional>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::hub {

struct HubCreateProjectResult {
    bool succeeded = false;
    std::filesystem::path projectFile;
    std::wstring error;
};

class HubProjectActions {
public:
    HubProjectActions() = delete;

    [[nodiscard]] static std::filesystem::path DefaultProjectLocation();
    [[nodiscard]] static std::optional<std::filesystem::path> BrowseProjectFile(HWND owner);
    [[nodiscard]] static std::optional<std::filesystem::path> BrowseFolder(HWND owner, const std::filesystem::path& initialFolder);
    [[nodiscard]] static std::optional<std::filesystem::path> BrowseProjectFolder(HWND owner);
    [[nodiscard]] static HubCreateProjectResult CreateProjectInFolder(const std::filesystem::path& projectRoot);
    [[nodiscard]] static bool LaunchEditor(HWND owner, const std::filesystem::path& projectFile, std::wstring& error);
};

} // namespace kb::hub
