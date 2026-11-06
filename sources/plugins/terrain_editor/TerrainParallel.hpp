#pragma once

#include <algorithm>
#include <cstdint>
#include <exception>
#include <thread>
#include <vector>

namespace kb::terrain_editor {

inline constexpr std::uint32_t kTerrainMaximumWorkers = 16U;

// Splits [begin, end) into contiguous parts and runs function(first, last, part) for each, the
// first on the calling thread. Ranges too small for `minimumPerPart` items run in one part on the
// caller, and `maxParts` (when not 0) caps the parts. The parts do not overlap, so a function that
// only writes its own items gives the same result for any number of parts.
template <typename Function>
void ParallelForRange(std::uint32_t begin, std::uint32_t end, std::uint32_t minimumPerPart, Function&& function, std::uint32_t maxParts = 0U) {
    const std::uint32_t count = end > begin ? end - begin : 0U;
    const std::uint32_t hardware = std::max(1U, std::thread::hardware_concurrency());
    const std::uint32_t limit = std::min({ hardware, kTerrainMaximumWorkers, maxParts == 0U ? kTerrainMaximumWorkers : maxParts });
    const std::uint32_t parts = std::clamp(count / std::max(1U, minimumPerPart), 1U, limit);
    if (parts <= 1U) {
        function(begin, end, 0U);
        return;
    }
    const auto edge = [&](std::uint32_t part) {
        return begin + static_cast<std::uint32_t>(static_cast<std::uint64_t>(count) * part / parts);
    };
    std::vector<std::thread> workers;
    workers.reserve(parts - 1U);
    for (std::uint32_t part = 1U; part < parts; ++part) {
        const std::uint32_t first = edge(part);
        const std::uint32_t last = edge(part + 1U);
        try {
            workers.emplace_back([&function, first, last, part] { function(first, last, part); });
        } catch (const std::exception&) {
            function(first, last, part); // no thread to be had: do this part here
        }
    }
    function(edge(0U), edge(1U), 0U);
    for (std::thread& worker : workers) worker.join();
}

} // namespace kb::terrain_editor
