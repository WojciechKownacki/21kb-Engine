#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace kb::core {
struct CrashReport { std::uint32_t apiVersion=0U; std::vector<std::uint64_t> assetIds; std::string error; std::vector<std::string> recentEvents; };
[[nodiscard]] inline CrashReport MakeCrashReport(std::uint32_t apiVersion, std::vector<std::uint64_t> assetIds, std::string error, std::vector<std::string> events, std::size_t maxEvents=32U) { if(events.size()>maxEvents)events.erase(events.begin(),events.end()-static_cast<std::ptrdiff_t>(maxEvents)); return {apiVersion,std::move(assetIds),std::move(error),std::move(events)}; }
} // namespace kb::core
