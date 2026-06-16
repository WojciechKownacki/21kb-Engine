#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/CommandBuffer.hpp"
#include "engine/ecs/ComponentReflection.hpp"
#include "engine/ecs/World.hpp"
#include "engine/ecs/WorldConfigPresets.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace {

constexpr std::size_t kEntityCount = 1'000'000;
constexpr std::size_t kBatchSize = 100'000;

#if defined(NDEBUG)
constexpr double kCreateSecondsLimit = 30.0;
constexpr double kSaveSecondsLimit = 20.0;
constexpr double kLoadSecondsLimit = 30.0;
constexpr double kVerifySecondsLimit = 10.0;
constexpr std::uint64_t kPeakResidentBytesLimit = 1024ULL * 1024ULL * 1024ULL;
#else
constexpr double kCreateSecondsLimit = 180.0;
constexpr double kSaveSecondsLimit = 120.0;
constexpr double kLoadSecondsLimit = 180.0;
constexpr double kVerifySecondsLimit = 60.0;
constexpr std::uint64_t kPeakResidentBytesLimit = 2ULL * 1024ULL * 1024ULL * 1024ULL;
#endif

using Clock = std::chrono::steady_clock;

struct TimedStep {
    Clock::time_point start = Clock::now();

    [[nodiscard]] double ElapsedSeconds() const noexcept {
        return std::chrono::duration<double>(Clock::now() - start).count();
    }
};

struct SnapshotScaleCounters {
    std::size_t count = 0;
    double positionX = 0.0;
    double positionY = 0.0;
    double velocityX = 0.0;
    double velocityY = 0.0;
};

void AccumulateSnapshotScaleComponents(kb::ecs::Entity, const EcsPosition& position, const EcsVelocity& velocity, void* context) {
    auto* counters = static_cast<SnapshotScaleCounters*>(context);
    ++counters->count;
    counters->positionX += static_cast<double>(position.x);
    counters->positionY += static_cast<double>(position.y);
    counters->velocityX += static_cast<double>(velocity.x);
    counters->velocityY += static_cast<double>(velocity.y);
}

[[nodiscard]] std::uint64_t PeakResidentBytes() noexcept {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
#elif defined(__APPLE__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#elif defined(__unix__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ULL;
#else
    return 0;
#endif
}

[[nodiscard]] std::filesystem::path ScaleSnapshotPath() {
    return std::filesystem::temp_directory_path() / "21kb_ecs_snapshot_scale_1m.kbecssnp";
}

void RegisterScaleReflection(kb::ecs::World& world) {
    const kb::ecs::ComponentReflection* positionReflection = world.RegisterComponentReflection<EcsPosition>(
        "test.EcsPosition",
        {
            { "x", kb::ecs::ComponentFieldType::Float32, offsetof(EcsPosition, x), sizeof(float) },
            { "y", kb::ecs::ComponentFieldType::Float32, offsetof(EcsPosition, y), sizeof(float) },
        });
    const kb::ecs::ComponentReflection* velocityReflection = world.RegisterComponentReflection<EcsVelocity>(
        "test.EcsVelocity",
        {
            { "x", kb::ecs::ComponentFieldType::Float32, offsetof(EcsVelocity, x), sizeof(float) },
            { "y", kb::ecs::ComponentFieldType::Float32, offsetof(EcsVelocity, y), sizeof(float) },
        });
    kb::tests::Require(positionReflection != nullptr && velocityReflection != nullptr, "ECS scale snapshot reflection registration failed");
}

void FillBatch(std::size_t firstIndex, std::span<EcsPosition> positions, std::span<EcsVelocity> velocities) {
    for (std::size_t offset = 0; offset < positions.size(); ++offset) {
        const std::size_t index = firstIndex + offset;
        positions[offset] = EcsPosition{
            .x = static_cast<float>(index),
            .y = static_cast<float>(index + 1U),
        };
        velocities[offset] = EcsVelocity{
            .x = static_cast<float>(index * 2U),
            .y = -static_cast<float>(index),
        };
    }
}

void BuildMillionEntityWorld(kb::ecs::World& world) {
    std::vector<EcsPosition> positions(kBatchSize);
    std::vector<EcsVelocity> velocities(kBatchSize);

    for (std::size_t first = 0; first < kEntityCount; first += kBatchSize) {
        const std::size_t count = std::min(kBatchSize, kEntityCount - first);
        FillBatch(
            first,
            std::span<EcsPosition>{ positions.data(), count },
            std::span<EcsVelocity>{ velocities.data(), count });

        kb::ecs::CommandBuffer buffer{ 1 };
        static_cast<void>(buffer.Worker(0).CreateEntities(
            std::span<const EcsPosition>{ positions.data(), count },
            std::span<const EcsVelocity>{ velocities.data(), count }));
        const kb::ecs::CommandBufferPlaybackResult result = buffer.Playback(world);
        kb::tests::Require(result.CreatedCount() == count, "ECS scale snapshot bulk create returned an invalid count");
    }

    kb::tests::Require(world.NativeStorageStats().liveEntities == kEntityCount, "ECS scale snapshot source world has invalid entity count");
}

void WriteBinaryFile(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    kb::tests::Require(output.is_open(), "ECS scale snapshot output file could not be opened");
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    kb::tests::Require(output.good(), "ECS scale snapshot output file could not be written");
}

[[nodiscard]] std::vector<std::byte> ReadBinaryFile(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary | std::ios::ate };
    kb::tests::Require(input.is_open(), "ECS scale snapshot input file could not be opened");
    const std::ifstream::pos_type end = input.tellg();
    kb::tests::Require(end >= 0, "ECS scale snapshot input file size could not be read");

    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    kb::tests::Require(input.good() || input.eof(), "ECS scale snapshot input file could not be read");
    return bytes;
}

void VerifyRestoredWorld(kb::ecs::World& world) {
    SnapshotScaleCounters counters;
    world.ForEach<EcsPosition, EcsVelocity>(&AccumulateSnapshotScaleComponents, &counters);

    const double n = static_cast<double>(kEntityCount);
    const double sum = n * (n - 1.0) * 0.5;
    kb::tests::Require(counters.count == kEntityCount, "ECS scale snapshot restored invalid query entity count");
    kb::tests::Require(std::fabs(counters.positionX - sum) <= 0.5, "ECS scale snapshot restored invalid position x checksum");
    kb::tests::Require(std::fabs(counters.positionY - (sum + n)) <= 0.5, "ECS scale snapshot restored invalid position y checksum");
    kb::tests::Require(std::fabs(counters.velocityX - (sum * 2.0)) <= 1.0, "ECS scale snapshot restored invalid velocity x checksum");
    kb::tests::Require(std::fabs(counters.velocityY + sum) <= 0.5, "ECS scale snapshot restored invalid velocity y checksum");
}

void RunSaveLoadMillionEntitiesTest() {
    const std::uint64_t baselinePeakBytes = PeakResidentBytes();
    const std::filesystem::path snapshotPath = ScaleSnapshotPath();
    std::error_code removeError;
    std::filesystem::remove(snapshotPath, removeError);

    {
        kb::ecs::WorldConfig config = kb::ecs::WorldConfigPresets::BenchmarkDefault();
        config.reserveEntities = kEntityCount;
        kb::ecs::World source{ config };
        RegisterScaleReflection(source);

        TimedStep createTimer;
        BuildMillionEntityWorld(source);
        const double createSeconds = createTimer.ElapsedSeconds();
        kb::tests::Require(createSeconds <= kCreateSecondsLimit, "ECS scale snapshot source creation exceeded time limit");

        TimedStep saveTimer;
        std::vector<std::byte> bytes;
        kb::tests::Require(source.SerializeChunkedSnapshotBinary(bytes), "ECS scale snapshot binary save failed");
        kb::tests::Require(!bytes.empty(), "ECS scale snapshot binary save produced no data");
        WriteBinaryFile(snapshotPath, bytes);
        const double saveSeconds = saveTimer.ElapsedSeconds();
        kb::tests::Require(saveSeconds <= kSaveSecondsLimit, "ECS scale snapshot save exceeded time limit");
    }

    kb::ecs::WorldConfig restoreConfig = kb::ecs::WorldConfigPresets::BenchmarkDefault();
    restoreConfig.reserveEntities = kEntityCount;
    kb::ecs::World restored{ restoreConfig };
    RegisterScaleReflection(restored);

    TimedStep loadTimer;
    std::vector<std::byte> loadedBytes = ReadBinaryFile(snapshotPath);
    kb::tests::Require(restored.RestoreChunkedSnapshotBinary(loadedBytes), "ECS scale snapshot binary load failed");
    const double loadSeconds = loadTimer.ElapsedSeconds();
    kb::tests::Require(loadSeconds <= kLoadSecondsLimit, "ECS scale snapshot load exceeded time limit");
    kb::tests::Require(restored.NativeStorageStats().liveEntities == kEntityCount, "ECS scale snapshot restored invalid live entity count");

    TimedStep verifyTimer;
    VerifyRestoredWorld(restored);
    const double verifySeconds = verifyTimer.ElapsedSeconds();
    kb::tests::Require(verifySeconds <= kVerifySecondsLimit, "ECS scale snapshot verification exceeded time limit");

    const std::uint64_t peakBytes = PeakResidentBytes();
    if (peakBytes != 0U && peakBytes > baselinePeakBytes) {
        kb::tests::Require(peakBytes - baselinePeakBytes <= kPeakResidentBytesLimit, "ECS scale snapshot exceeded peak resident memory limit");
    }

    std::filesystem::remove(snapshotPath, removeError);
}

} // namespace

int main() {
    RunSaveLoadMillionEntitiesTest();
    return EXIT_SUCCESS;
}
