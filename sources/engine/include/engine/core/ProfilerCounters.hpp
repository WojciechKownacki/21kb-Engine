#pragma once
#include <cstdint>
#include <string_view>
namespace kb::core { struct ProfilerCounters { std::uint64_t scopes=0U;std::uint64_t timelineEvents=0U;std::uint64_t allocations=0U;void Scope(std::string_view)noexcept{++scopes;}void Timeline(std::string_view)noexcept{++timelineEvents;}void Allocation()noexcept{++allocations;} }; }
