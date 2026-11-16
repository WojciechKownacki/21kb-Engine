#include "CliCommon.hpp"
#include "CliProgram.hpp"

#include "engine/core/JsonValue.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <mutex>
#include <thread>
#endif

namespace kb::cli {
namespace {

namespace fs = std::filesystem;

// Whether `path` is `root` or lies below it; `relative` is then its path from the root.
bool Under(const fs::path& path, const fs::path& root, fs::path& relative) {
    relative = path.lexically_relative(root);
    return !relative.empty() && *relative.begin() != "..";
}

// The folders to watch for the roots: an existing folder is watched whole; a root that is a file or does not exist
// yet is seen through its parent folder (one level, which reports its creation), or through the nearest existing
// ancestor when more than one level is missing. A folder below a whole watch is already covered.
std::vector<std::pair<fs::path, bool>> WatchPlan(const std::vector<fs::path>& roots) {
    std::error_code ec;
    std::vector<std::pair<fs::path, bool>> plan;
    for (const fs::path& root : roots) {
        if (fs::is_directory(root, ec)) {
            plan.emplace_back(root, true);
            continue;
        }
        fs::path folder = root.parent_path();
        if (fs::is_directory(folder, ec)) {
            plan.emplace_back(folder, false);
            continue;
        }
        while (!folder.empty() && !fs::is_directory(folder, ec) && folder.has_parent_path() && folder != folder.parent_path()) folder = folder.parent_path();
        if (fs::is_directory(folder, ec)) plan.emplace_back(folder, true);
    }
    std::vector<std::pair<fs::path, bool>> kept;
    for (std::size_t index = 0U; index < plan.size(); ++index) {
        const auto& [folder, whole] = plan[index];
        fs::path relative;
        const bool covered = std::ranges::any_of(plan, [&](const std::pair<fs::path, bool>& other) {
            if (&other == &plan[index]) return false;
            if (other.first == folder) return (other.second && !whole) || (other.second == whole && &other < &plan[index]);
            return other.second && Under(folder, other.first, relative);
        });
        if (!covered) kept.push_back(plan[index]);
    }
    return kept;
}

} // namespace

#ifdef _WIN32

struct ChangeWatch::State {
    struct Folder {
        fs::path path;
        bool whole = false;
        HANDLE handle = INVALID_HANDLE_VALUE;
        HANDLE done = nullptr;
        OVERLAPPED overlapped{};
        std::vector<char> buffer;
        std::thread pump;
    };
    struct Event {
        fs::path path;
        DWORD action = 0;
    };

    static constexpr DWORD kFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE |
                                     FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;
    // A large buffer is only touched as far as the events fill it; the pump empties it while the command runs.
    static constexpr std::size_t kBufferBytes = 1U << 20U;

    std::vector<std::unique_ptr<Folder>> folders;
    HANDLE stop = nullptr;
    std::mutex mutex;
    std::vector<Event> events;
    std::string error;
    bool stopped = false;

    State(const std::vector<fs::path>& roots) {
        stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        for (const auto& [path, whole] : WatchPlan(roots)) {
            auto folder = std::make_unique<Folder>();
            folder->path = path;
            folder->whole = whole;
            folder->buffer.resize(kBufferBytes);
            folder->handle = CreateFileW(path.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
            folder->done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (folder->handle == INVALID_HANDLE_VALUE || !Arm(*folder)) {
                error = "cannot watch " + path.generic_string() + " for changes (error " + std::to_string(GetLastError()) + ")";
                Close(*folder);
                continue;
            }
            Folder& watched = *folder;
            folders.push_back(std::move(folder));
            watched.pump = std::thread{ [this, &watched] { Pump(watched); } };
        }
    }

    ~State() { Stop(); }

    static void Close(Folder& folder) {
        if (folder.handle != INVALID_HANDLE_VALUE) CloseHandle(folder.handle);
        if (folder.done != nullptr) CloseHandle(folder.done);
        folder.handle = INVALID_HANDLE_VALUE;
        folder.done = nullptr;
    }

    static bool Arm(Folder& folder) {
        folder.overlapped = OVERLAPPED{};
        folder.overlapped.hEvent = folder.done;
        ResetEvent(folder.done);
        return ReadDirectoryChangesW(folder.handle, folder.buffer.data(), static_cast<DWORD>(folder.buffer.size()), folder.whole ? TRUE : FALSE,
                   kFilter, nullptr, &folder.overlapped, nullptr) != FALSE;
    }

    void Fail(std::string message) {
        const std::lock_guard lock{ mutex };
        if (error.empty()) error = std::move(message);
    }

    void Take(const Folder& folder, DWORD bytes) {
        std::vector<Event> taken;
        for (std::size_t offset = 0U; offset < bytes;) {
            const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(folder.buffer.data() + offset);
            taken.push_back(Event{ folder.path / std::wstring{ info->FileName, info->FileNameLength / sizeof(wchar_t) }, info->Action });
            if (info->NextEntryOffset == 0U) break;
            offset += info->NextEntryOffset;
        }
        const std::lock_guard lock{ mutex };
        events.insert(events.end(), std::make_move_iterator(taken.begin()), std::make_move_iterator(taken.end()));
    }

    // Takes what the system reports for one folder until told to stop, then once more for what is still queued.
    void Pump(Folder& folder) {
        const HANDLE waits[2]{ folder.done, stop };
        for (;;) {
            const bool stopping = WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0 + 1;
            DWORD bytes = 0;
            if (GetOverlappedResult(folder.handle, &folder.overlapped, &bytes, FALSE)) {
                if (bytes == 0U) Fail("the file system's change notifications overflowed, so the list of changes may be incomplete");
                else Take(folder, bytes);
            } else if (const DWORD code = GetLastError(); code == ERROR_NOTIFY_ENUM_DIR) {
                Fail("the file system's change notifications overflowed, so the list of changes may be incomplete");
            } else if (code == ERROR_IO_INCOMPLETE) {
                if (stopping) break;
                continue;
            } else {
                Fail("the file system's change notifications failed (error " + std::to_string(code) + ")");
                return;
            }
            if (!Arm(folder)) {
                Fail("cannot watch " + folder.path.generic_string() + " for changes");
                return;
            }
        }
        CancelIoEx(folder.handle, &folder.overlapped);
        DWORD ignored = 0;
        GetOverlappedResult(folder.handle, &folder.overlapped, &ignored, TRUE);
    }

    void Stop() {
        if (stopped) return;
        stopped = true;
        if (stop != nullptr) SetEvent(stop);
        for (auto& folder : folders) {
            if (folder->pump.joinable()) folder->pump.join();
            Close(*folder);
        }
        if (stop != nullptr) CloseHandle(stop);
    }

    // The net kind of every file the events name: what the first event says about its state before, and the file now.
    bool Collect(const std::vector<fs::path>& roots, std::map<fs::path, std::string_view>& kinds, std::string& message) {
        Stop();
        message = error;
        // Whether each path existed before the command: removal, modification and rename-away come first for a file
        // that was there; creation and rename-in for one that was not (a replace by rename reports the removal first).
        std::map<fs::path, bool> existed;
        for (const Event& event : events) {
            if (existed.contains(event.path)) continue;
            bool relevant = false;
            for (const fs::path& root : roots) {
                fs::path relative;
                if (Under(event.path, root, relative) && !IsExcludedFolder(relative)) relevant = true;
            }
            if (relevant) existed.emplace(event.path, event.action != FILE_ACTION_ADDED && event.action != FILE_ACTION_RENAMED_NEW_NAME);
        }
        std::error_code ec;
        for (const auto& [path, before] : existed) {
            if (fs::is_directory(path, ec)) {
                // A folder that is new may hold files that came with it (renamed into place): they are all new.
                if (before) continue;
                Listing listing;
                if (!ScanProject(path, listing, message, false)) return false;
                for (const auto& [relative, size] : listing.files) kinds[(path / relative).lexically_normal()] = "created";
                continue;
            }
            const bool exists = fs::is_regular_file(path, ec);
            if (exists) kinds.emplace(path, before ? "modified" : "created");
            else if (before) {
                // The removal of a folder names the folder too; its files are in the list on their own.
                const auto next = existed.upper_bound(path);
                fs::path relative;
                if (next == existed.end() || !Under(next->first, path, relative)) kinds.emplace(path, "removed");
            }
        }
        return true;
    }
};

#else

// Without a recursive directory watcher the folders are listed before and after the run.
struct ChangeWatch::State {
    using Stamp = std::pair<std::uintmax_t, fs::file_time_type>;

    std::map<fs::path, Stamp> before;
    std::string error;

    explicit State(const std::vector<fs::path>& roots) {
        if (!Capture(roots, before, error)) before.clear();
    }

    static bool Capture(const std::vector<fs::path>& roots, std::map<fs::path, Stamp>& files, std::string& message) {
        std::error_code ec;
        for (const fs::path& root : roots) {
            if (fs::is_directory(root, ec)) {
                Listing listing;
                if (!ScanProject(root, listing, message, false)) return false;
                for (const auto& [relative, size] : listing.files) files[(root / relative).lexically_normal()] = { size, listing.times[relative] };
            } else if (fs::is_regular_file(root, ec)) {
                files[root] = { fs::file_size(root, ec), fs::last_write_time(root, ec) };
            }
        }
        return true;
    }

    bool Collect(const std::vector<fs::path>& roots, std::map<fs::path, std::string_view>& kinds, std::string& message) {
        message = error;
        std::map<fs::path, Stamp> after;
        if (message.empty() && !Capture(roots, after, message)) return false;
        for (const auto& [path, stamp] : after) {
            const auto old = before.find(path);
            if (old == before.end()) kinds.emplace(path, "created");
            else if (old->second != stamp) kinds.emplace(path, "modified");
        }
        for (const auto& [path, stamp] : before) {
            if (!after.contains(path)) kinds.emplace(path, "removed");
        }
        return true;
    }
};

#endif

ChangeWatch::ChangeWatch(std::string_view command, const ArgumentList& arguments) : command_{ command } {
    std::error_code ec;
    const auto absolute = [&ec](const fs::path& path) { return fs::absolute(path, ec).lexically_normal(); };
    if (const auto project = arguments.Option("--project"); project.has_value()) {
        project_ = absolute(fs::path{ *project });
        roots_.push_back(project_);
    }
    // A relative output path is the project's or the working folder's, as the command resolves it.
    const auto watch = [&](const std::string& value) {
        const fs::path path{ value };
        if (path.is_relative() && !project_.empty()) roots_.push_back((project_ / path).lexically_normal());
        roots_.push_back(absolute(path));
    };
    for (const char* option : { "--out", "--output", "--index", "--mods", "--dir", "--cache", "--baseline", "--registry" }) {
        // A registry may be an address, which is not a path.
        if (const auto value = arguments.Option(option); value.has_value() && value->find("://") == std::string::npos) watch(*value);
    }
    if (command_ == "new") {
        const auto parent = arguments.Option("--parent");
        const auto name = arguments.Option("--name");
        // The new project is the project of the change list.
        if (parent.has_value() && name.has_value()) {
            project_ = absolute(fs::path{ *parent } / *name);
            roots_.push_back(project_);
        }
    }
    if (command_ == "pack" || command_ == "mod" || command_ == "release" || command_ == "keys") {
        for (const std::string& positional : arguments.Positionals()) watch(positional);
    }
    std::ranges::sort(roots_);
    roots_.erase(std::ranges::unique(roots_).begin(), roots_.end());
    state_ = std::make_unique<State>(roots_);
}

ChangeWatch::~ChangeWatch() = default;

bool ChangeWatch::Publish(kb::core::JsonValue& data, bool onlyIfChanged) const {
    using kb::core::JsonValue;
    std::map<fs::path, std::string_view> kinds;
    std::string error;
    if (!state_->Collect(roots_, kinds, error)) kinds.clear();

    const auto reported = [this](const fs::path& path) {
        const fs::path relative = project_.empty() ? fs::path{} : path.lexically_relative(project_);
        return (relative.empty() || *relative.begin() == "..") ? path.generic_string() : relative.generic_string();
    };
    JsonValue changes = JsonValue::MakeArray();
    for (const auto& [path, kind] : kinds) {
        const std::string text = reported(path);
        JsonValue row = JsonValue::MakeObject();
        row.Set("path", JsonValue::MakeString(ToValidUtf8(text)));
        row.Set("kind", JsonValue::MakeString(std::string{ kind }));
        // The asset the file is, by the path a command takes it by; a .meta file belongs to its asset.
        if (text.starts_with("Assets/") && path.extension() != ".meta") row.Set("asset", JsonValue::MakeString(ToValidUtf8("/Game/" + text.substr(7U))));
        changes.Append(std::move(row));
    }
    if (changes.Size() == 0U && error.empty() && (onlyIfChanged || data.GetKind() == JsonValue::Kind::Object)) return false;
    if (data.GetKind() != JsonValue::Kind::Object) data = DataObject("21kb.cli." + command_ + "-write/v1");
    data.Set("changes", std::move(changes));
    if (!error.empty()) data.Set("changesError", JsonValue::MakeString(ToValidUtf8(error)));
    return true;
}

} // namespace kb::cli
