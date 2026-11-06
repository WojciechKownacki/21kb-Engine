#include "EcsBenchmarkMeasurements.hpp"
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
    const auto percentileIndex = [count = samples.size()](std::size_t percentile) {
        return (count / 100U) * percentile + ((count % 100U) * percentile + 99U) / 100U - 1U;
    };
    const std::size_t p95Index = percentileIndex(95U);
    const std::size_t p99Index = percentileIndex(99U);
    std::cout << phase << ",median_ms=" << median << ",p95_ms=" << samples[p95Index]
              << ",p99_ms=" << samples[p99Index] << ",max_ms=" << samples.back()
              << ",entities_per_second=" << static_cast<double>(entityCount) * 1000.0 / median << '\n';
}


void RunFlecsReference(std::size_t entityCount, std::size_t measuredFrames) {
    if (entityCount > static_cast<std::size_t>(std::numeric_limits<int32_t>::max()))
        throw std::out_of_range("Flecs bulk count exceeds int32");
    constexpr std::size_t warmupFrames = 10U;
    ecs_benchmark::PrintMemory("baseline");
    ecs_world_t* world = ecs_init();
    const auto component = [world](std::size_t size, std::size_t alignment) {
        ecs_component_desc_t desc{};
        desc.type.size = static_cast<ecs_size_t>(size);
        desc.type.alignment = static_cast<ecs_size_t>(alignment);
        return ecs_component_init(world, &desc);
    };
    const ecs_entity_t positionId = component(sizeof(Position), alignof(Position));
    const ecs_entity_t velocityId = component(sizeof(Velocity), alignof(Velocity));
    const ecs_entity_t activeId = component(sizeof(Active), alignof(Active));
    std::vector<Position> positions(entityCount);
    std::vector<Velocity> velocities(entityCount);
    for (std::size_t i = 0; i < entityCount; ++i) {
        positions[i] = Position{static_cast<float>(i % 1024U) * 0.01F, 0.0F};
        velocities[i] = Velocity{1.0F + static_cast<float>(i % 7U) * 0.1F, -0.5F};
    }
    void* columns[]{positions.data(), velocities.data()};
    ecs_bulk_desc_t desc{};
    desc.count = static_cast<int32_t>(entityCount);
    desc.ids[0] = positionId;
    desc.ids[1] = velocityId;
    desc.data = columns;
    const ecs_benchmark::Sample createAllocations;
    auto start = Clock::now();
    const auto* created = ecs_bulk_init(world, &desc);
    if (!created) throw std::runtime_error("Flecs reference bulk creation failed");
    std::vector<ecs_entity_t> entities(created, created + entityCount);
    const double createMs = Milliseconds(start);
    ecs_benchmark::PrintAllocations("bulk_create", createAllocations);
    positions.clear(); positions.shrink_to_fit();
    velocities.clear(); velocities.shrink_to_fit();
    ecs_query_desc_t queryDesc{};
    queryDesc.terms[0].id = positionId;
    queryDesc.terms[1].id = velocityId;
    ecs_query_t* query = ecs_query_init(world, &queryDesc);
    if (!query) throw std::runtime_error("Flecs reference query failed");
    std::vector<double> samples;
    samples.reserve(measuredFrames);
    const ecs_benchmark::Sample updateAllocations;
    for (std::size_t frame = 0; frame < warmupFrames + measuredFrames; ++frame) {
        start = Clock::now();
        auto it = ecs_query_iter(world, query);
        while (ecs_query_next(&it)) {
            auto* p = static_cast<Position*>(ecs_field_w_size(&it, sizeof(Position), 0));
            const auto* v = static_cast<const Velocity*>(ecs_field_w_size(&it, sizeof(Velocity), 1));
            for (int32_t i = 0; i < it.count; ++i) {
                p[i].x += v[i].x * (1.0F / 60.0F);
                p[i].y += v[i].y * (1.0F / 60.0F);
            }
        }
        if (frame >= warmupFrames) samples.push_back(Milliseconds(start));
    }
    ecs_benchmark::PrintAllocations("position_update", updateAllocations);
    const ecs_benchmark::Sample addAllocations;
    start = Clock::now();
    const Active value{1U};
    for (auto entity : entities) ecs_set_id(world, entity, activeId, sizeof(value), &value);
    const double addMs = Milliseconds(start);
    ecs_benchmark::PrintAllocations("bulk_add_component", addAllocations);
    double checksumX = 0.0, checksumY = 0.0;
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const auto* p = static_cast<const Position*>(ecs_get_id(world, entities[i], positionId));
        const auto* a = static_cast<const Active*>(ecs_get_id(world, entities[i], activeId));
        const float expected = static_cast<float>(i % 1024U) * 0.01F +
            (1.0F + static_cast<float>(i % 7U) * 0.1F) * static_cast<float>(warmupFrames + measuredFrames) / 60.0F;
        if (!p || !a || a->value != 1U || std::abs(p->x - expected) > 0.02F)
            throw std::runtime_error("Flecs reference result mismatch");
        checksumX += p->x; checksumY += p->y;
    }
    ecs_benchmark::PrintMemory("resident");
    std::cout << std::fixed << std::setprecision(6)
              << "mode=flecs-reference,entities=" << entities.size() << ",warmup_frames=" << warmupFrames
              << ",measured_frames=" << measuredFrames << ",add_mode=ecs_set_id\n"
              << "bulk_create,ms=" << createMs << '\n'
              << "bulk_add_component,ms=" << addMs << '\n'
              << "result,entities=" << entities.size() << ",active=" << entityCount
              << ",checksum_x=" << checksumX << ",checksum_y=" << checksumY << '\n';
    PrintSamples("position_update", std::move(samples), entityCount);
    ecs_query_fini(query);
    ecs_fini(world);
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc > 5) {
            std::cerr << "usage: kb_ecs_workload_benchmark [entity_count] [measured_frames] [native-only|flecs-reference] [direct-add]\n";
            return EXIT_FAILURE;
        }
        const std::size_t entityCount = argc > 1 ? ParsePositive(argv[1]) : 1'000'000U;
        const std::size_t measuredFrames = argc > 2 ? ParsePositive(argv[2]) : 120U;
        bool nativeOnly = false;
        bool directAdd = false;
        bool reference = false;
        for (int index = 3; index < argc; ++index) {
            const std::string_view option{ argv[index] };
            if (option == "flecs-reference" && !reference) {
                reference = true;
            } else if (option == "native-only" && !nativeOnly) {
                nativeOnly = true;
            } else if (option == "direct-add" && !directAdd) {
                directAdd = true;
            } else {
                throw std::invalid_argument("unknown or duplicate workload benchmark option");
            }
        }
        constexpr std::size_t warmupFrames = 10U;
        if (measuredFrames > std::numeric_limits<std::size_t>::max() - warmupFrames) {
            throw std::out_of_range("frame count is too large");
        }

        if (reference) {
            if (nativeOnly || directAdd) throw std::invalid_argument("Reference mode does not accept engine options");
            RunFlecsReference(entityCount, measuredFrames);
            return EXIT_SUCCESS;
        }
        ecs_benchmark::PrintMemory("baseline");
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
        const ecs_benchmark::Sample createAllocations;
        const auto createStart = Clock::now();
        std::vector<kb::ecs::Entity> entities = nativeOnly
            ? world.CreateEntitiesNativeOnly(entityCount, views)
            : world.CreateEntities(entityCount, views);
        const double createMs = Milliseconds(createStart);
        ecs_benchmark::PrintAllocations("bulk_create", createAllocations);
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
        const ecs_benchmark::Sample updateAllocations;
        for (std::size_t frame = 0; frame < warmupFrames + measuredFrames; ++frame) {
            const auto start = Clock::now();
            query.ForEachMutableBatchKernel(&Integrate);
            if (frame >= warmupFrames) {
                frameSamples.push_back(Milliseconds(start));
            }
        }

        ecs_benchmark::PrintAllocations("position_update", updateAllocations);
        double checksumX = 0.0, checksumY = 0.0;
        for (std::size_t index = 0; index < entityCount; ++index) {
            const Position* position = world.TryGet<Position>(entities[index]);
            const float expectedX = static_cast<float>(index % 1024U) * 0.01F
                + (1.0F + static_cast<float>(index % 7U) * 0.1F)
                    * static_cast<float>(warmupFrames + measuredFrames) / 60.0F;
            if (position == nullptr || !std::isfinite(position->x)
                || std::fabs(position->x - expectedX) > 0.02F) {
                throw std::runtime_error("position verification failed");
            }
            checksumX += position->x; checksumY += position->y;
        }

        std::vector<Active> active(entityCount, Active{ 1U });
        kb::ecs::CommandBuffer buffer{ 1 };
        const ecs_benchmark::Sample addAllocations;
        const auto addStart = Clock::now();
        std::size_t createdDuringAdd = 0U;
        if (directAdd) {
            world.SetMany<Active>(entities, active);
        } else {
            buffer.Worker(0).AddMissingBorrowed(std::span<const kb::ecs::Entity>{ entities }, std::span<const Active>{ active });
            createdDuringAdd = buffer.Playback(world).CreatedCount();
        }
        const double addMs = Milliseconds(addStart);
        ecs_benchmark::PrintAllocations("bulk_add_component", addAllocations);
        std::size_t activeCount = 0;
        world.CreateQuery<Active>().ForEachBatchKernel([&activeCount](const kb::ecs::QueryBatch<Active>& batch) {
            activeCount += batch.Count();
        });
        if (createdDuringAdd != 0U || activeCount != entityCount) {
            throw std::runtime_error("component addition verification failed");
        }
        active.clear();
        active.shrink_to_fit();

        ecs_benchmark::PrintMemory("resident");
        const auto memory = world.TelemetrySnapshot();
        std::cout << "native_storage,tracked_bytes=" << memory.trackedBytes << ",system_allocations=" << memory.storageSystemAllocationCount << ",basis=accounted_storage_capacity\n";
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "result,entities=" << entities.size() << ",active=" << activeCount << ",checksum_x=" << checksumX << ",checksum_y=" << checksumY << '\n';
        std::cout << "mode=" << (nativeOnly ? "native-only" : "full")
                  << ",entities=" << entityCount << ",warmup_frames=" << warmupFrames
                  << ",measured_frames=" << measuredFrames << ",step_seconds=0.016666667"
                  << ",add_mode=" << (directAdd ? "direct-set-many" : "command-buffer-transactional") << '\n';
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
