#include "engine/ecs/CommandBuffer.hpp"
#include "engine/ecs/Query.hpp"
#include "engine/ecs/World.hpp"
#include "engine/ecs/WorldConfigPresets.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

struct Position {
    float x = 0.0F;
    float y = 0.0F;
};

struct Velocity {
    float x = 0.0F;
    float y = 0.0F;
};

struct Active {
    unsigned value = 0;
};

using Clock = std::chrono::steady_clock;

[[nodiscard]] double Milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

[[nodiscard]] std::size_t ParsePositive(std::string_view text) {
    if (text.empty()) {
        throw std::invalid_argument("empty numeric argument");
    }
    std::size_t result = 0;
    for (const char digit : text) {
        if (digit < '0' || digit > '9') {
            throw std::invalid_argument("numeric arguments must contain decimal digits only");
        }
        const std::size_t value = static_cast<std::size_t>(digit - '0');
        if (result > (std::numeric_limits<std::size_t>::max() - value) / 10U) {
            throw std::out_of_range("numeric argument is too large");
        }
        result = result * 10U + value;
    }
    if (result == 0) {
        throw std::invalid_argument("numeric arguments must be positive");
    }
    return result;
}

void Integrate(kb::ecs::MutableQueryBatch<Position, Velocity>& batch) {
    Position* positions = batch.Components<0>();
    const Velocity* velocities = batch.Components<1>();
    constexpr float step = 1.0F / 60.0F;
    for (std::size_t index = 0; index < batch.Count(); ++index) {
        positions[index].x += velocities[index].x * step;
        positions[index].y += velocities[index].y * step;
    }
}

void PrintSamples(std::string_view phase, std::vector<double> samples, std::size_t entityCount) {
    std::sort(samples.begin(), samples.end());
    const double median = samples[samples.size() / 2U];
    const std::size_t p95Index = (samples.size() * 95U + 99U) / 100U - 1U;
    std::cout << phase << ",median_ms=" << median << ",p95_ms=" << samples[p95Index]
              << ",entities_per_second=" << static_cast<double>(entityCount) * 1000.0 / median << '\n';
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc > 4 || (argc == 4 && std::string_view{ argv[3] } != "native-only")) {
            std::cerr << "usage: kb_ecs_workload_benchmark [entity_count] [measured_frames] [native-only]\n";
            return EXIT_FAILURE;
        }
        const std::size_t entityCount = argc > 1 ? ParsePositive(argv[1]) : 1'000'000U;
        const std::size_t measuredFrames = argc > 2 ? ParsePositive(argv[2]) : 120U;
        const bool nativeOnly = argc == 4;
        constexpr std::size_t warmupFrames = 10U;
        if (measuredFrames > std::numeric_limits<std::size_t>::max() - warmupFrames) {
            throw std::out_of_range("frame count is too large");
        }

        kb::ecs::WorldConfig config = kb::ecs::WorldConfigPresets::DesktopDefault();
        if (nativeOnly) {
            config.mirrorEntitiesToBackend = false;
            config.mirrorNativeComponentChangesToBackend = false;
            config.trackEntityCatalog = false;
        }
        kb::ecs::World world{ config };
        std::vector<Position> positions(entityCount);
        std::vector<Velocity> velocities(entityCount);
        for (std::size_t index = 0; index < entityCount; ++index) {
            positions[index] = Position{ static_cast<float>(index % 1024U) * 0.01F, 0.0F };
            velocities[index] = Velocity{ 1.0F + static_cast<float>(index % 7U) * 0.1F, -0.5F };
        }

        const std::array views{
            kb::ecs::World::MakeBulkComponentView<Position>(std::span<const Position>{ positions }),
            kb::ecs::World::MakeBulkComponentView<Velocity>(std::span<const Velocity>{ velocities }),
        };
        const auto createStart = Clock::now();
        std::vector<kb::ecs::Entity> entities = nativeOnly
            ? world.CreateEntitiesNativeOnly(entityCount, views)
            : world.CreateEntities(entityCount, views);
        const double createMs = Milliseconds(createStart);
        if (entities.size() != entityCount) {
            throw std::runtime_error("bulk creation returned the wrong entity count");
        }
        positions.clear();
        velocities.clear();
        positions.shrink_to_fit();
        velocities.shrink_to_fit();

        kb::ecs::Query<Position, Velocity> query = world.CreateQuery<Position, Velocity>();
        std::vector<double> frameSamples;
        frameSamples.reserve(measuredFrames);
        for (std::size_t frame = 0; frame < warmupFrames + measuredFrames; ++frame) {
            const auto start = Clock::now();
            query.ForEachMutableBatchKernel(&Integrate);
            if (frame >= warmupFrames) {
                frameSamples.push_back(Milliseconds(start));
            }
        }

        const std::array<std::size_t, 3> checkedIndices{ 0U, entityCount / 2U, entityCount - 1U };
        for (const std::size_t index : checkedIndices) {
            const Position* position = world.TryGet<Position>(entities[index]);
            const float expectedX = static_cast<float>(index % 1024U) * 0.01F
                + (1.0F + static_cast<float>(index % 7U) * 0.1F)
                    * static_cast<float>(warmupFrames + measuredFrames) / 60.0F;
            if (position == nullptr || !std::isfinite(position->x)
                || std::fabs(position->x - expectedX) > 0.02F) {
                throw std::runtime_error("position verification failed");
            }
        }

        std::vector<Active> active(entityCount, Active{ 1U });
        kb::ecs::CommandBuffer buffer{ 1 };
        const auto addStart = Clock::now();
        buffer.Worker(0).AddMissingBorrowed(std::span<const kb::ecs::Entity>{ entities }, std::span<const Active>{ active });
        const auto addResult = buffer.Playback(world);
        const double addMs = Milliseconds(addStart);
        std::size_t activeCount = 0;
        world.CreateQuery<Active>().ForEachBatchKernel([&activeCount](const kb::ecs::QueryBatch<Active>& batch) {
            activeCount += batch.Count();
        });
        if (addResult.CreatedCount() != 0U || activeCount != entityCount) {
            throw std::runtime_error("component addition verification failed");
        }

        std::cout << std::fixed << std::setprecision(3);
        std::cout << "mode=" << (nativeOnly ? "native-only" : "full")
                  << ",entities=" << entityCount << ",warmup_frames=" << warmupFrames
                  << ",measured_frames=" << measuredFrames << ",step_seconds=0.016666667\n";
        std::cout << "bulk_create,ms=" << createMs << ",entities_per_second="
                  << static_cast<double>(entityCount) * 1000.0 / createMs << '\n';
        PrintSamples("position_update", std::move(frameSamples), entityCount);
        std::cout << "bulk_add_component,ms=" << addMs << ",entities_per_second="
                  << static_cast<double>(entityCount) * 1000.0 / addMs << '\n';
        std::cout << "sample_last_position_x=" << world.TryGet<Position>(entities.back())->x << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "ECS workload benchmark failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
