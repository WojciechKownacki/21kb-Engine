#pragma once

#include <string_view>

namespace kb::platform {
[[nodiscard]] constexpr bool IsSandboxStorageKey(std::string_view key) noexcept { if(key.empty()||key.front()=='/'||key.front()=='\\'||key.find(':')!=std::string_view::npos)return false; std::size_t segmentStart=0U; for(std::size_t index=0;index<=key.size();++index){if(index==key.size()||key[index]=='/'||key[index]=='\\'){const std::string_view segment=key.substr(segmentStart,index-segmentStart);if(segment.empty()||segment=="."||segment=="..")return false;segmentStart=index+1U;}} return true; }
} // namespace kb::platform
