#pragma once

#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::hub {

struct HubProjectItem {
    std::wstring name;
    std::filesystem::path projectFile;
    std::filesystem::path projectRoot;
    std::wstring description;
    std::wstring category;
    std::wstring defaultScene;
    std::wstring modified;
    std::wstring size;
    std::wstring validationMessage;
    std::wstring editorVersion = L"21kb";
    bool valid = false;
};

struct HubLayout {
    RECT searchField{};
    RECT newProjectButton{};
    RECT openProjectButton{};
    RECT launchProjectButton{};
    RECT removeProjectButton{};
    RECT listRect{};
    RECT detailsRect{};
};

struct HubState {
    std::vector<HubProjectItem> projects;
    HubLayout layout;
    std::wstring status;
    std::wstring searchQuery;
    int selectedProject = -1;
    int scrollY = 0;
    int width = 1250;
    int height = 760;
    bool searchFocused = false;
};

} // namespace kb::hub
