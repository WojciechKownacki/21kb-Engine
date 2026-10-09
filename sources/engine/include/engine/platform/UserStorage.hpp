#pragma once

#include <string_view>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <mutex>
#include <vector>

namespace kb::assets::streaming {
class BackgroundLane;
}

namespace kb::platform {
[[nodiscard]] constexpr bool IsSandboxStorageKey(std::string_view key) noexcept { if(key.empty()||key.front()=='/'||key.front()=='\\'||key.find(':')!=std::string_view::npos)return false; std::size_t segmentStart=0U; for(std::size_t index=0;index<=key.size();++index){if(index==key.size()||key[index]=='/'||key[index]=='\\'){const std::string_view segment=key.substr(segmentStart,index-segmentStart);if(segment.empty()||segment=="."||segment=="..")return false;segmentStart=index+1U;}} return true; }
// A single user-chosen storage name (a save slot, a settings profile): 1..64
// ASCII letters, digits, '_' or '-', and never a Windows device name. It can
// therefore never name a directory, a drive, a parent or a stream; callers
// append their own extension and pass the result to UserStorage as a key.
inline constexpr std::size_t kMaxUserStorageSlotNameBytes = 64U;
[[nodiscard]] constexpr bool IsUserStorageSlotName(std::string_view name) noexcept {
    if (name.empty() || name.size() > kMaxUserStorageSlotNameBytes) return false;
    for (const char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    const auto startsWith = [name](std::string_view device) noexcept {
        if (name.size() < device.size()) return false;
        for (std::size_t index = 0U; index < device.size(); ++index) {
            const char c = name[index];
            if ((c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c) != device[index]) return false;
        }
        return true;
    };
    if (name.size() == 3U && (startsWith("CON") || startsWith("PRN") || startsWith("AUX") || startsWith("NUL"))) return false;
    return !(name.size() == 4U && (startsWith("COM") || startsWith("LPT")) && name[3] >= '0' && name[3] <= '9');
}
// WriteAsync runs the write as a long job of the engine's background service; writes land in
// the order they were asked for, and destroying the storage waits for every write it queued.
class UserStorage final { public: UserStorage(std::filesystem::path root, std::uintmax_t quotaBytes); ~UserStorage(); UserStorage(const UserStorage&) = delete; UserStorage& operator=(const UserStorage&) = delete; [[nodiscard]] bool Write(std::string_view key, std::string_view data); [[nodiscard]] std::optional<std::string> Read(std::string_view key) const; [[nodiscard]] bool Delete(std::string_view key); [[nodiscard]] std::vector<std::string> List() const; [[nodiscard]] std::future<bool> WriteAsync(std::string key, std::string data); private: [[nodiscard]] std::filesystem::path PathFor(std::string_view key) const; [[nodiscard]] std::uintmax_t Usage() const; std::filesystem::path root_; std::uintmax_t quotaBytes_; mutable std::mutex mutex_; std::mutex writesMutex_; std::unique_ptr<kb::assets::streaming::BackgroundLane> writes_; };
} // namespace kb::platform
