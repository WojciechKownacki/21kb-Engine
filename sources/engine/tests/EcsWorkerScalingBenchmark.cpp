#include "EcsBenchmarkMeasurements.hpp"
#include "engine/ecs/WorkerPool.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <vector>

namespace {
std::uint64_t Work(std::size_t index, std::size_t iterations) {
    std::uint64_t value = index + 1U;
    for (std::size_t i = 0; i < iterations; ++i) {
        value ^= value << 13U; value ^= value >> 7U; value ^= value << 17U;
    }
    return value;
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::invalid_argument("Expected worker count and uniform|uneven workload");
        const auto workers = static_cast<std::size_t>(std::stoul(argv[1]));
        const std::string mode{argv[2]};
        if (workers < 1U || workers > 64U || (mode != "uniform" && mode != "uneven"))
            throw std::invalid_argument("Invalid worker workload arguments");
        kb::ecs::WorkerPool pool{{.workerCount = workers, .collectDispatchTelemetry = true}};
        std::array<std::uint64_t, 96> results{}, expected{};
        std::array<unsigned, 96> visits{};
        const auto iterations = [&](std::size_t index) { return mode == "uniform" ? 6000U : index % 8U == 0U ? 48000U : 0U; };
        for (std::size_t i = 0; i < expected.size(); ++i) expected[i] = Work(i, iterations(i));
        std::vector<double> samples;
        samples.reserve(200U);
        const auto dispatch = [&] {
            visits.fill(0U);
            pool.ParallelForChunks(results.size(), 1U, [&](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
                for (std::size_t i = chunk.begin; i < chunk.begin + chunk.count; ++i) {
                    results[i] = Work(i, iterations(i));
                    ++visits[i];
                }
            });
            if (results != expected || !std::ranges::all_of(visits, [](unsigned count) { return count == 1U; }))
                throw std::runtime_error("Worker dispatch lost, repeated or corrupted work");
        };
        for (int i = 0; i < 20; ++i) dispatch();
        ecs_benchmark::PrintMemory("warmed");
        const ecs_benchmark::Sample allocations;
        for (int frame = 0; frame < 200; ++frame) {
            const auto start = std::chrono::steady_clock::now();
            dispatch();
            samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }
        ecs_benchmark::PrintAllocations("dispatch", allocations);
        ecs_benchmark::PrintMemory("settled");
        std::ranges::sort(samples);
        std::uint64_t checksum = 0U;
        for (auto value : results) checksum ^= value;
        std::cout << "worker_scaling,workers=" << pool.WorkerCount() << ",mode=" << mode
                  << ",jobs=" << results.size() * 220U << ",median_ms=" << samples[100]
                  << ",p95_ms=" << samples[189] << ",p99_ms=" << samples[197]
                  << ",checksum=" << checksum << ",correct=1\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
