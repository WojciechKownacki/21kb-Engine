#include "engine/platform/UserStorage.hpp"

#include <fstream>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace kb::platform {

UserStorage::UserStorage(std::filesystem::path root, std::uintmax_t quotaBytes)
    : root_(std::move(root)), quotaBytes_(quotaBytes) {
    std::filesystem::create_directories(root_);
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
        !std::filesystem::create_directories(path.parent_path(), error) && error) return false;
    const std::filesystem::path temporary = path.string() + ".tmp";
    { std::ofstream output(temporary, std::ios::binary | std::ios::trunc); if (!output) return false; output.write(data.data(), static_cast<std::streamsize>(data.size())); if (!output) return false; }
#if defined(_WIN32)
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { std::filesystem::remove(temporary, error); return false; }
#else
    std::filesystem::rename(temporary, path, error);
    if (error) { std::filesystem::remove(temporary, error); return false; }
#endif
    return true;
}

std::optional<std::string> UserStorage::Read(std::string_view key) const {
    const std::filesystem::path path = PathFor(key);
    if (path.empty()) return std::nullopt;
    std::lock_guard lock{ mutex_ };
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    return std::string{ std::istreambuf_iterator<char>{ input },
        std::istreambuf_iterator<char>{} };
}
bool UserStorage::Delete(std::string_view key) { const std::filesystem::path path = PathFor(key); if (path.empty()) return false; std::lock_guard lock{ mutex_ }; std::error_code error; return std::filesystem::remove(path, error); }
std::vector<std::string> UserStorage::List() const { std::lock_guard lock{ mutex_ }; std::vector<std::string> result; std::error_code error; for (std::filesystem::recursive_directory_iterator iterator(root_, error), end; iterator != end && !error; iterator.increment(error)) if (iterator->is_regular_file(error)) result.push_back(std::filesystem::relative(iterator->path(), root_, error).generic_string()); return result; }
std::future<bool> UserStorage::WriteAsync(std::string key, std::string data) { return std::async(std::launch::async, [this, key = std::move(key), data = std::move(data)] { return Write(key, data); }); }

} // namespace kb::platform
