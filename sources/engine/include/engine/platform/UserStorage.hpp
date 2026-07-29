#pragma once

#include <string_view>
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <vector>

namespace kb::platform {
[[nodiscard]] constexpr bool IsSandboxStorageKey(std::string_view key) noexcept { if(key.empty()||key.front()=='/'||key.front()=='\\'||key.find(':')!=std::string_view::npos)return false; std::size_t segmentStart=0U; for(std::size_t index=0;index<=key.size();++index){if(index==key.size()||key[index]=='/'||key[index]=='\\'){const std::string_view segment=key.substr(segmentStart,index-segmentStart);if(segment.empty()||segment=="."||segment=="..")return false;segmentStart=index+1U;}} return true; }
class UserStorage final { public: UserStorage(std::filesystem::path root, std::uintmax_t quotaBytes); [[nodiscard]] bool Write(std::string_view key, std::string_view data); [[nodiscard]] std::optional<std::string> Read(std::string_view key) const; [[nodiscard]] bool Delete(std::string_view key); [[nodiscard]] std::vector<std::string> List() const; [[nodiscard]] std::future<bool> WriteAsync(std::string key, std::string data); private: [[nodiscard]] std::filesystem::path PathFor(std::string_view key) const; [[nodiscard]] std::uintmax_t Usage() const; std::filesystem::path root_; std::uintmax_t quotaBytes_; };
} // namespace kb::platform
