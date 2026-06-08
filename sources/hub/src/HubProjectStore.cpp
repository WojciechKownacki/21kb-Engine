#include "HubProjectStore.hpp"

#include "HubText.hpp"
#include "engine/project/ProjectManager.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <fstream>
#include <system_error>

namespace kb::hub {
namespace {

[[nodiscard]] std::filesystem::path AppDataRoot() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path{ buffer } / "21kb" / "Hub";
}

[[nodiscard]] bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::error_code error;
    return std::filesystem::weakly_canonical(left, error) == std::filesystem::weakly_canonical(right, error);
}

[[nodiscard]] std::wstring FormatByteSize(std::uintmax_t bytes) {
    constexpr std::uintmax_t kib = 1024U;
    constexpr std::uintmax_t mib = kib * 1024U;
    constexpr std::uintmax_t gib = mib * 1024U;

    wchar_t buffer[64]{};
    if (bytes >= gib) {
        swprintf_s(buffer, L"%.2f GB", static_cast<double>(bytes) / static_cast<double>(gib));
    } else if (bytes >= mib) {
        swprintf_s(buffer, L"%.1f MB", static_cast<double>(bytes) / static_cast<double>(mib));
    } else if (bytes >= kib) {
        swprintf_s(buffer, L"%.1f KB", static_cast<double>(bytes) / static_cast<double>(kib));
    } else {
        swprintf_s(buffer, L"%llu B", static_cast<unsigned long long>(bytes));
    }
    return buffer;
}

[[nodiscard]] std::uintmax_t DirectorySize(const std::filesystem::path& root) {
    std::error_code error;
    if (!std::filesystem::is_directory(root, error) || error) {
        return 0U;
    }

    std::uintmax_t total = 0U;
    std::filesystem::recursive_directory_iterator iterator(root, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        if (iterator->is_regular_file(error) && !error) {
            const std::uintmax_t size = iterator->file_size(error);
            if (!error) {
                total += size;
            }
        }
        iterator.increment(error);
    }
    return total;
}

[[nodiscard]] std::wstring LastModifiedLabel(const std::filesystem::path& path) {
    std::error_code error;
    const auto fileTime = std::filesystem::last_write_time(path, error);
    if (error) {
        return L"Unknown";
    }

    const auto nowFileTime = std::filesystem::file_time_type::clock::now();
    const auto age = nowFileTime > fileTime ? nowFileTime - fileTime : fileTime - nowFileTime;
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(age).count();
    const auto hours = std::chrono::duration_cast<std::chrono::hours>(age).count();
    const auto days = std::chrono::duration_cast<std::chrono::hours>(age).count() / 24;
    if (minutes < 2) {
        return L"Just now";
    }
    if (minutes < 60) {
        return std::to_wstring(minutes) + L" min ago";
    }
    if (hours < 24) {
        return std::to_wstring(hours) + L" h ago";
    }
    if (days < 30) {
        return std::to_wstring(days) + L" d ago";
    }
    return L"30+ d ago";
}

[[nodiscard]] std::filesystem::path TempPathFor(const std::filesystem::path& path) {
    std::filesystem::path tempPath = path;
    tempPath += L".tmp";
    return tempPath;
}

[[nodiscard]] bool ReplaceFileAtomically(const std::filesystem::path& source, const std::filesystem::path& destination) {
#if defined(_WIN32)
    return MoveFileExW(source.wstring().c_str(), destination.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    return !error;
#endif
}

} // namespace

std::vector<HubProjectItem> HubProjectStore::Load() {
    std::vector<HubProjectItem> projects;
    const std::filesystem::path store = StoreFile();

    std::wifstream input(store);
    std::wstring line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        AddOrPromote(projects, std::filesystem::path{ line });
    }

    return projects;
}

void HubProjectStore::Save(const std::vector<HubProjectItem>& projects) {
    const std::filesystem::path store = StoreFile();
    std::error_code error;
    std::filesystem::create_directories(store.parent_path(), error);
    if (error) {
        return;
    }

    const std::filesystem::path tempPath = TempPathFor(store);
    {
        std::wofstream output(tempPath, std::ios::trunc);
        if (!output.is_open()) {
            return;
        }
        for (const HubProjectItem& project : projects) {
            if (!project.projectFile.empty()) {
                output << project.projectFile.wstring() << L'\n';
            }
        }
        output.close();
        if (!output) {
            std::filesystem::remove(tempPath, error);
            return;
        }
    }

    if (!ReplaceFileAtomically(tempPath, store)) {
        std::filesystem::remove(tempPath, error);
    }
}

void HubProjectStore::AddOrPromote(std::vector<HubProjectItem>& projects, const std::filesystem::path& projectFile) {
    if (projectFile.empty()) {
        return;
    }

    const auto existing = std::ranges::find_if(projects, [&projectFile](const HubProjectItem& item) {
        return SamePath(item.projectFile, projectFile);
    });
    if (existing != projects.end()) {
        HubProjectItem item = *existing;
        projects.erase(existing);
        projects.insert(projects.begin(), std::move(item));
        return;
    }

    projects.insert(projects.begin(), BuildProjectItem(projectFile));
    constexpr std::size_t maxRecentProjects = 24;
    if (projects.size() > maxRecentProjects) {
        projects.resize(maxRecentProjects);
    }
}

std::filesystem::path HubProjectStore::StoreFile() {
    return AppDataRoot() / "recent-projects.txt";
}

HubProjectItem HubProjectStore::BuildProjectItem(const std::filesystem::path& projectFile) {
    HubProjectItem item;
    item.projectFile = projectFile;
    item.projectRoot = projectFile.parent_path();
    item.name = projectFile.stem().wstring();
    item.modified = L"Unknown";
    item.size = L"-";
    item.validationMessage = L"Project descriptor is missing.";

    std::error_code error;
    item.valid = std::filesystem::is_regular_file(projectFile, error) && !error;
    if (!item.valid) {
        return item;
    }

    item.modified = LastModifiedLabel(projectFile);
    item.size = FormatByteSize(DirectorySize(item.projectRoot));
    if (item.valid) {
        const kb::project::ProjectDescriptorReadResult loaded = kb::project::ProjectManager::LoadProject(projectFile);
        item.valid = loaded.succeeded;
        if (loaded.succeeded) {
            item.name = HubText::Utf8ToWide(loaded.descriptor.name);
            item.description = HubText::Utf8ToWide(loaded.descriptor.description);
            item.category = HubText::Utf8ToWide(loaded.descriptor.category);
            item.defaultScene = HubText::Utf8ToWide(loaded.descriptor.defaultScene);
            item.validationMessage = L"Ready";
        } else {
            item.validationMessage = HubText::Utf8ToWide(loaded.error);
            if (item.validationMessage.empty()) {
                item.validationMessage = L"Project descriptor is invalid.";
            }
        }
    }

    return item;
}

} // namespace kb::hub
