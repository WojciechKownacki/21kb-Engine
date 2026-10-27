#include "engine/platform/UserStorage.hpp"

#include "engine/assets/streaming/BackgroundLoadService.hpp"
#include "save/SaveGameBinaryIO.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <limits>
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>

// clang-format off
EM_JS(int, BrowserUserStorageOperation, (const char* root, int operation), {
    const storage = Module['kbUserStorage'];
    if (!storage || UTF8ToString(root) !== storage.root + '/Saves') return operation === 2 ? 4 : 0;
    if (operation === 1) return storage.flush() ? 1 : 0;
    return storage.status;
});
EM_JS(void, BrowserUserStorageChanged, (const char* root, const char* path), {
    const storage = Module['kbUserStorage'];
    if (storage && UTF8ToString(root) === storage.root + '/Saves')
        storage.changed(path ? UTF8ToString(path) : null);
});
EM_JS(char*, BrowserUserStorageDiagnostic, (const char* root), {
    const storage = Module['kbUserStorage'];
    const message = storage && UTF8ToString(root) === storage.root + '/Saves'
        ? storage.diagnostic : 'Persistent browser storage is not configured';
    return stringToNewUTF8(message);
});
// clang-format on
#endif

namespace kb::platform {

UserStorage::UserStorage(std::filesystem::path root, std::uintmax_t quotaBytes)
    : root_(std::move(root)), quotaBytes_(quotaBytes) {
    // The root is created by the first Write: a host that never persists
    // anything leaves no directory behind, and an unwritable root fails that
    // Write instead of throwing out of the host's start-up.
}

UserStorage::~UserStorage() {
    std::lock_guard lock{ writesMutex_ };
    if (writes_ != nullptr) {
        writes_->Drain();
    }
}

std::filesystem::path UserStorage::PathFor(std::string_view key) const {
    return IsSandboxStorageKey(key) ? root_ / std::filesystem::path{ key }
                                     : std::filesystem::path{};
}

std::uintmax_t UserStorage::Usage() const {
    std::uintmax_t total = 0U;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator(root_, error), end;
         iterator != end && !error; iterator.increment(error)) {
        if (iterator->is_regular_file(error)) total += iterator->file_size(error);
    }
    return total;
}

bool UserStorage::Write(std::string_view key, std::string_view data) {
    const std::filesystem::path path = PathFor(key);
    if (path.empty() || data.size() > quotaBytes_) return false;
    std::lock_guard lock{ mutex_ };
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) return false;
    const std::uintmax_t oldBytes = exists ? std::filesystem::file_size(path, error) : 0U;
    const std::uintmax_t usedBytes = Usage();
    if (error || usedBytes < oldBytes || usedBytes - oldBytes > quotaBytes_ || data.size() > quotaBytes_ - (usedBytes - oldBytes) ||
        (!std::filesystem::create_directories(path.parent_path(), error) && error)) return false;
    if (!kb::save::SaveGameBinaryIO::WriteBytesAtomically(path,
            { reinterpret_cast<const std::uint8_t*>(data.data()), data.size() })) return false;
#if defined(__EMSCRIPTEN__)
    BrowserUserStorageChanged(root_.generic_string().c_str(), path.generic_string().c_str());
#endif
    return true;
}

std::optional<std::string> UserStorage::Read(std::string_view key) const {
    return ReadBounded(key, static_cast<std::size_t>(std::min<std::uintmax_t>(quotaBytes_, std::numeric_limits<std::size_t>::max())));
}

std::optional<std::string> UserStorage::ReadBounded(std::string_view key, std::size_t maxBytes) const {
    const std::filesystem::path path = PathFor(key);
    if (path.empty()) return std::nullopt;
    std::lock_guard lock{ mutex_ };
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return std::nullopt;
    const auto size = input.tellg();
    if (size < 0 || static_cast<std::uintmax_t>(size) > maxBytes || static_cast<std::uintmax_t>(size) > quotaBytes_) return std::nullopt;
    std::string bytes(static_cast<std::size_t>(size), '\0');
    input.seekg(0);
    if (!bytes.empty()) input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!input || input.peek() != std::char_traits<char>::eof()) return std::nullopt;
    return bytes;
}
bool UserStorage::Delete(std::string_view key) {
    const std::filesystem::path path = PathFor(key);
    if (path.empty()) return false;
    std::lock_guard lock{ mutex_ };
    std::error_code error;
    const bool removed = std::filesystem::remove(path, error);
#if defined(__EMSCRIPTEN__)
    if (removed) BrowserUserStorageChanged(root_.generic_string().c_str(), nullptr);
#endif
    return removed;
}

bool UserStorage::Flush() {
#if defined(__EMSCRIPTEN__)
    return BrowserUserStorageOperation(root_.generic_string().c_str(), 1) != 0;
#else
    return true;
#endif
}

UserStorageSyncStatus UserStorage::SyncStatus() const {
#if defined(__EMSCRIPTEN__)
    switch (BrowserUserStorageOperation(root_.generic_string().c_str(), 2)) {
    case 0: return UserStorageSyncStatus::Clean;
    case 1: return UserStorageSyncStatus::Dirty;
    case 2: return UserStorageSyncStatus::Pending;
    case 3: return UserStorageSyncStatus::Failed;
    default: return UserStorageSyncStatus::Unavailable;
    }
#else
    return UserStorageSyncStatus::Clean;
#endif
}

std::string UserStorage::SyncDiagnostic() const {
#if defined(__EMSCRIPTEN__)
    char* message = BrowserUserStorageDiagnostic(root_.generic_string().c_str());
    const std::string result{ message };
    std::free(message);
    return result;
#else
    return {};
#endif
}
std::vector<std::string> UserStorage::List() const { std::lock_guard lock{ mutex_ }; std::vector<std::string> result; std::error_code error; for (std::filesystem::recursive_directory_iterator iterator(root_, error), end; iterator != end && !error; iterator.increment(error)) if (iterator->is_regular_file(error)) result.push_back(std::filesystem::relative(iterator->path(), root_, error).generic_string()); std::ranges::sort(result); return result; }
std::future<bool> UserStorage::WriteAsync(std::string key, std::string data) {
    auto write = std::make_shared<std::packaged_task<bool()>>([this, key = std::move(key), data = std::move(data)] { return Write(key, data); });
    std::future<bool> result = write->get_future();
    std::lock_guard lock{ writesMutex_ };
    if (writes_ == nullptr) {
        writes_ = std::make_unique<kb::assets::streaming::BackgroundLane>(
            kb::assets::streaming::BackgroundLoadService::Shared(), kb::assets::streaming::BackgroundJobClass::Long);
    }
    static_cast<void>(writes_->Run([write](std::string&) {
        (*write)();
        return true;
    }));
    return result;
}

} // namespace kb::platform
