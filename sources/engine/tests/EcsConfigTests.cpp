#include "EcsTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/ChunkSizeAutoTuner.hpp"
#include "engine/ecs/QueryExecutionScratch.hpp"
#include "engine/ecs/QueryExecutionSettings.hpp"
#include "engine/ecs/QueryExecutionTuning.hpp"
#include "engine/ecs/World.hpp"
#include "engine/ecs/WorldConfigPresets.hpp"
#include "engine/ecs/WorldProfile.hpp"

#include <array>
#include <cstdint>

namespace {

struct TunePosition {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;
};

struct TuneVelocity {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;
};

struct ReductionProbe {
    std::uint64_t visited = 0;
    double checksum = 0.0;
};

template <typename T>
[[nodiscard]] constexpr kb::ecs::NativeComponentType TuneComponent(kb::ecs::ComponentId id) noexcept {
    return kb::ecs::NativeComponentType{ .id = id, .size = sizeof(T), .alignment = alignof(T) };
}

void RunWorldConfigTest() {
    static_assert(kb::ecs::ChunkPayloadBytes(kb::ecs::ChunkSizeProfile::Chunk4KB) == 4 * 1024);
    static_assert(kb::ecs::ChunkPayloadBytes(kb::ecs::ChunkSizeProfile::Chunk8KB) == 8 * 1024);
    static_assert(kb::ecs::ChunkPayloadBytes(kb::ecs::ChunkSizeProfile::Chunk16KB) == 16 * 1024);
    static_assert(kb::ecs::ChunkPayloadBytes(kb::ecs::ChunkSizeProfile::Chunk32KB) == 32 * 1024);
    static_assert(kb::ecs::ChunkPayloadBytes(kb::ecs::ChunkSizeProfile::Chunk64KB) == 64 * 1024);
    static_assert(kb::ecs::ChunkPayloadBytes(kb::ecs::ChunkSizeProfile::Chunk128KB) == 128 * 1024);
    static_assert(kb::ecs::ChunkPayloadBytes(kb::ecs::ChunkSizeProfile::Chunk256KB) == 256 * 1024);
    static_assert(kb::ecs::ChunkPayloadBytes(kb::ecs::ChunkSizeProfile::Chunk512KB) == 512 * 1024);
    static_assert(kb::ecs::kMinChunkPayloadBytes == 4 * 1024);
    static_assert(kb::ecs::kMaxChunkPayloadBytes == 512 * 1024);
    static_assert(kb::ecs::IsPowerOfTwoChunkPayloadBytes(4 * 1024));
    static_assert(!kb::ecs::IsPowerOfTwoChunkPayloadBytes(12 * 1024));
    static_assert(kb::ecs::IsValidCustomChunkPayloadBytes(4 * 1024));
    static_assert(kb::ecs::IsValidCustomChunkPayloadBytes(512 * 1024));
    static_assert(!kb::ecs::IsValidCustomChunkPayloadBytes(2 * 1024));
    static_assert(!kb::ecs::IsValidCustomChunkPayloadBytes(12 * 1024));
    static_assert(!kb::ecs::IsValidCustomChunkPayloadBytes(1024 * 1024));
    static_assert(kb::ecs::ChunkSizeProfileName(kb::ecs::ChunkSizeProfile::Chunk4KB) == "4KB");
    static_assert(kb::ecs::ChunkSizeProfileName(kb::ecs::ChunkSizeProfile::Chunk512KB) == "512KB");
    static_assert(kb::ecs::ChunkSizeProfileFromPayloadBytes(4 * 1024) == kb::ecs::ChunkSizeProfile::Chunk4KB);
    static_assert(kb::ecs::ChunkSizeProfileFromPayloadBytes(512 * 1024) == kb::ecs::ChunkSizeProfile::Chunk512KB);
    static_assert(!kb::ecs::ChunkSizeProfileFromPayloadBytes(12 * 1024).has_value());
    static_assert(kb::ecs::ParseChunkSizeProfile("4kb") == kb::ecs::ChunkSizeProfile::Chunk4KB);
    static_assert(kb::ecs::ParseChunkSizeProfile("8KB") == kb::ecs::ChunkSizeProfile::Chunk8KB);
    static_assert(kb::ecs::ParseChunkSizeProfile("512Kb") == kb::ecs::ChunkSizeProfile::Chunk512KB);
    static_assert(!kb::ecs::ParseChunkSizeProfile("2KB").has_value());
    static_assert(!kb::ecs::ParseChunkSizeProfile("1024KB").has_value());
    static_assert(kb::ecs::ParseChunkSizeProfileWithDiagnostics("4kb").HasValue());
    static_assert(kb::ecs::ParseChunkSizeProfileWithDiagnostics("4kb").profile == kb::ecs::ChunkSizeProfile::Chunk4KB);
    static_assert(!kb::ecs::ParseChunkSizeProfileWithDiagnostics("").HasValue());
    static_assert(kb::ecs::ParseChunkSizeProfileWithDiagnostics("").error == kb::ecs::ChunkSizeProfileParseError::Empty);
    static_assert(!kb::ecs::ParseChunkSizeProfileWithDiagnostics("12KB").HasValue());
    static_assert(kb::ecs::ParseChunkSizeProfileWithDiagnostics("12KB").error == kb::ecs::ChunkSizeProfileParseError::UnsupportedValue);
    static_assert(kb::ecs::QueryExecutionPolicyName(kb::ecs::QueryExecutionPolicy::SingleThread) == "single_thread");
    static_assert(kb::ecs::QueryExecutionPolicyName(kb::ecs::QueryExecutionPolicy::ParallelChunks) == "parallel_chunks");
    static_assert(kb::ecs::QueryExecutionPolicyName(kb::ecs::QueryExecutionPolicy::ParallelRanges) == "parallel_ranges");
    static_assert(kb::ecs::QueryExecutionPolicyName(kb::ecs::QueryExecutionPolicy::SIMDPreferred) == "simd_preferred");
    static_assert(kb::ecs::QueryExecutionPolicyName(kb::ecs::QueryExecutionPolicy::Deterministic) == "deterministic");
    static_assert(kb::ecs::ParseQueryExecutionPolicy("parallel_chunks") == kb::ecs::QueryExecutionPolicy::ParallelChunks);
    static_assert(kb::ecs::ParseQueryExecutionPolicy("parallel-ranges") == kb::ecs::QueryExecutionPolicy::ParallelRanges);
    static_assert(kb::ecs::ParseQueryExecutionPolicy("simd") == kb::ecs::QueryExecutionPolicy::SIMDPreferred);
    static_assert(kb::ecs::ParseQueryExecutionPolicyWithDiagnostics("deterministic").HasValue());
    static_assert(kb::ecs::ParseQueryExecutionPolicyWithDiagnostics("deterministic").policy == kb::ecs::QueryExecutionPolicy::Deterministic);
    static_assert(!kb::ecs::ParseQueryExecutionPolicyWithDiagnostics("").HasValue());
    static_assert(kb::ecs::ParseQueryExecutionPolicyWithDiagnostics("").error == kb::ecs::QueryExecutionPolicyParseError::Empty);
    static_assert(!kb::ecs::ParseQueryExecutionPolicyWithDiagnostics("workers").HasValue());
    static_assert(kb::ecs::ParseQueryExecutionPolicyWithDiagnostics("workers").error == kb::ecs::QueryExecutionPolicyParseError::UnsupportedValue);
    static_assert(kb::ecs::WorldProfileName(kb::ecs::WorldProfile::Mobile4K) == "mobile4k");
    static_assert(kb::ecs::WorldProfileName(kb::ecs::WorldProfile::Mobile8K) == "mobile8k");
    static_assert(kb::ecs::WorldProfileName(kb::ecs::WorldProfile::Balanced32K) == "balanced32k");
    static_assert(kb::ecs::WorldProfileName(kb::ecs::WorldProfile::Desktop64K) == "desktop64k");
    static_assert(kb::ecs::WorldProfileName(kb::ecs::WorldProfile::Streaming128KPlus) == "streaming128kplus");
    static_assert(kb::ecs::WorldProfileName(kb::ecs::WorldProfile::BenchmarkAuto) == "benchmarkauto");
    static_assert(kb::ecs::ParseWorldProfileWithDiagnostics("mobile-4k").HasValue());
    static_assert(kb::ecs::ParseWorldProfileWithDiagnostics("mobile-4k").profile == kb::ecs::WorldProfile::Mobile4K);
    static_assert(kb::ecs::ParseWorldProfileWithDiagnostics("balanced_32k").profile == kb::ecs::WorldProfile::Balanced32K);
    static_assert(kb::ecs::ParseWorldProfileWithDiagnostics("desktop64k").profile == kb::ecs::WorldProfile::Desktop64K);
    static_assert(kb::ecs::ParseWorldProfileWithDiagnostics("streaming-128k-plus").profile == kb::ecs::WorldProfile::Streaming128KPlus);
    static_assert(kb::ecs::ParseWorldProfileWithDiagnostics("benchmark_auto").profile == kb::ecs::WorldProfile::BenchmarkAuto);
    static_assert(!kb::ecs::ParseWorldProfileWithDiagnostics("").HasValue());
    static_assert(kb::ecs::ParseWorldProfileWithDiagnostics("").error == kb::ecs::WorldProfileParseError::Empty);
    static_assert(!kb::ecs::ParseWorldProfileWithDiagnostics("desktop128k").HasValue());
    static_assert(kb::ecs::ParseWorldProfileWithDiagnostics("desktop128k").error == kb::ecs::WorldProfileParseError::UnsupportedValue);
    static_assert(kb::ecs::WorldProfileConfig(kb::ecs::WorldProfile::Mobile4K).chunkSizeProfile == kb::ecs::ChunkSizeProfile::Chunk4KB);
    static_assert(kb::ecs::WorldProfileConfig(kb::ecs::WorldProfile::Mobile8K).chunkSizeProfile == kb::ecs::ChunkSizeProfile::Chunk8KB);
    static_assert(kb::ecs::WorldProfileConfig(kb::ecs::WorldProfile::Balanced32K).chunkSizeProfile == kb::ecs::ChunkSizeProfile::Chunk32KB);
    static_assert(kb::ecs::WorldProfileConfig(kb::ecs::WorldProfile::Desktop64K).chunkSizeProfile == kb::ecs::ChunkSizeProfile::Chunk64KB);
    static_assert(kb::ecs::WorldProfileConfig(kb::ecs::WorldProfile::Streaming128KPlus).chunkSizeProfile == kb::ecs::ChunkSizeProfile::Chunk128KB);

    kb::ecs::World defaultWorld;
    kb::tests::Require(defaultWorld.Config().chunkSizeProfile == kb::ecs::ChunkSizeProfile::Chunk32KB, "ECS default config uses invalid storage chunk profile");
    kb::tests::Require(kb::ecs::ChunkPayloadBytes(defaultWorld.Config().chunkSizeProfile) == 32 * 1024, "ECS default config exposes invalid storage chunk payload");
    kb::tests::Require(defaultWorld.Config().trackEntityCatalog, "ECS default config must preserve editor entity catalog tracking");

    kb::ecs::World world(kb::ecs::WorldConfigPresets::BenchmarkDefault());
    kb::tests::Require(world.Config().chunkSizeProfile == kb::ecs::ChunkSizeProfile::Chunk512KB, "ECS benchmark config uses invalid storage chunk profile");
    kb::tests::Require(kb::ecs::ChunkPayloadBytes(world.Config().chunkSizeProfile) == 512 * 1024, "ECS benchmark config exposes invalid storage chunk payload");
    kb::tests::Require(world.Config().executionGrainSize == 16 * 1024, "ECS benchmark config exposes invalid execution grain size");
    kb::ecs::World nativeBenchmarkWorld(kb::ecs::WorldConfigPresets::BenchmarkNativeOnly());
    kb::tests::Require(!nativeBenchmarkWorld.Config().mirrorEntitiesToBackend, "ECS native benchmark config must keep entity creation on native storage");
    kb::tests::Require(
        !nativeBenchmarkWorld.Config().mirrorNativeComponentChangesToBackend,
        "ECS native benchmark config must keep component writes on native storage");
    kb::tests::Require(!nativeBenchmarkWorld.Config().trackEntityCatalog, "ECS native benchmark config must skip editor entity catalog tracking");
    kb::tests::Require(
        nativeBenchmarkWorld.Config().chunkSizeProfile == world.Config().chunkSizeProfile,
        "ECS native benchmark config must share the benchmark chunk profile");
    const kb::ecs::Entity benchmarkEntity = nativeBenchmarkWorld.CreateEntity();
    nativeBenchmarkWorld.Set(benchmarkEntity, TunePosition{ .x = 1.0F });
    kb::tests::Require(nativeBenchmarkWorld.IsAlive(benchmarkEntity), "ECS native benchmark config failed to create a native entity");
    kb::tests::Require(
        !nativeBenchmarkWorld.BackendEntityAlive(benchmarkEntity),
        "ECS native benchmark config mirrored an entity into the compatibility backend");
    kb::tests::Require(nativeBenchmarkWorld.Has<TunePosition>(benchmarkEntity), "ECS native benchmark config failed to write a native component");
    kb::ecs::WorkerPool benchmarkPool{ kb::ecs::WorkerPoolConfig{ .workerCount = 2, .singleThreaded = true } };
    const kb::ecs::QueryExecutionSettings benchmarkSettings =
        world.DefaultQueryExecutionSettings(&benchmarkPool, kb::ecs::QueryExecutionPolicy::SIMDPreferred);
    kb::tests::Require(benchmarkSettings.maxBatchSize == world.Config().executionGrainSize, "ECS world did not export default query grain size");
    kb::tests::Require(benchmarkSettings.prefetchDistance == world.Config().queryPrefetchDistance, "ECS world did not export default query prefetch distance");
    kb::tests::Require(benchmarkSettings.workerCountOverride == world.Config().workerThreadLimit, "ECS world did not export default query worker cap");
    kb::tests::Require(benchmarkSettings.workerPool == &benchmarkPool, "ECS world did not attach the requested query worker pool");
    kb::tests::Require(benchmarkSettings.policy == kb::ecs::QueryExecutionPolicy::SIMDPreferred, "ECS world did not honor requested query policy");
    kb::tests::Require(benchmarkSettings.adaptiveGrain, "ECS world did not enable adaptive default query grain");

    const kb::ecs::WorldConfig mobile4K = kb::ecs::WorldConfigPresets::Mobile4K();
    kb::tests::Require(mobile4K.chunkSizeProfile == kb::ecs::ChunkSizeProfile::Chunk4KB, "ECS Mobile4K config uses invalid storage chunk profile");
    kb::tests::Require(mobile4K.workerThreadLimit == 2, "ECS Mobile4K config does not cap workers");
    kb::tests::Require(mobile4K.executionGrainSize == 512, "ECS Mobile4K config exposes invalid execution grain size");
    kb::tests::Require(mobile4K.queryPrefetchDistance == 8, "ECS Mobile4K config exposes invalid query prefetch distance");
    kb::tests::Require(mobile4K.maxNativeStorageCommittedPayloadBytes == kb::ecs::WorldConfigPresets::MiB(256), "ECS Mobile4K config exposes invalid memory guard");
    const kb::ecs::QueryExecutionSettings mobileSettings =
        kb::ecs::World{ mobile4K }.DefaultQueryExecutionSettings(nullptr, kb::ecs::QueryExecutionPolicy::ParallelRanges);
    kb::tests::Require(mobileSettings.maxBatchSize == 512, "ECS Mobile4K default query settings lost grain size");
    kb::tests::Require(mobileSettings.prefetchDistance == 8, "ECS Mobile4K default query settings lost prefetch distance");
    kb::tests::Require(mobileSettings.workerCountOverride == 2, "ECS Mobile4K default query settings lost worker cap");
    kb::tests::Require(mobileSettings.policy == kb::ecs::QueryExecutionPolicy::ParallelRanges, "ECS Mobile4K default query settings lost policy override");
    kb::tests::Require(mobileSettings.adaptiveGrain, "ECS Mobile4K default query settings lost adaptive grain");

    const kb::ecs::WorldConfig mobile8K = kb::ecs::WorldConfigPresets::Mobile8K();
    kb::tests::Require(mobile8K.chunkSizeProfile == kb::ecs::ChunkSizeProfile::Chunk8KB, "ECS Mobile8K config uses invalid storage chunk profile");
    kb::tests::Require(mobile8K.workerThreadLimit == 4, "ECS Mobile8K config does not cap workers");
    kb::tests::Require(mobile8K.queryPrefetchDistance == 12, "ECS Mobile8K config exposes invalid query prefetch distance");
    kb::tests::Require(mobile8K.maxNativeStorageCommittedPayloadBytes == kb::ecs::WorldConfigPresets::MiB(512), "ECS Mobile8K config exposes invalid memory guard");

    const kb::ecs::WorldConfig mobile16K = kb::ecs::WorldConfigPresets::Mobile16K();
    kb::tests::Require(mobile16K.chunkSizeProfile == kb::ecs::ChunkSizeProfile::Chunk16KB, "ECS Mobile16K config uses invalid storage chunk profile");
    kb::tests::Require(mobile16K.workerThreadLimit == 4, "ECS Mobile16K config does not cap workers");
    kb::tests::Require(mobile16K.executionGrainSize == 2048, "ECS Mobile16K config exposes invalid execution grain size");
    kb::tests::Require(mobile16K.queryPrefetchDistance == 16, "ECS Mobile16K config exposes invalid query prefetch distance");
    kb::tests::Require(mobile16K.maxNativeStorageCommittedPayloadBytes == kb::ecs::WorldConfigPresets::GiB(1), "ECS Mobile16K config exposes invalid memory guard");

    const kb::ecs::WorldConfig desktop64K = kb::ecs::WorldConfigPresets::Desktop64K();
    kb::tests::Require(desktop64K.chunkSizeProfile == kb::ecs::ChunkSizeProfile::Chunk64KB, "ECS Desktop64K config uses invalid storage chunk profile");
    kb::tests::Require(desktop64K.workerThreadLimit == 0, "ECS Desktop64K config unexpectedly caps workers");
    kb::tests::Require(desktop64K.queryPrefetchDistance == 32, "ECS Desktop64K config exposes invalid query prefetch distance");
    kb::tests::Require(desktop64K.maxNativeStorageCommittedPayloadBytes == kb::ecs::WorldConfigPresets::GiB(4), "ECS Desktop64K config exposes invalid memory guard");

    const kb::ecs::WorldConfig streaming128K = kb::ecs::WorldConfigPresets::Streaming128KPlus();
    kb::tests::Require(streaming128K.chunkSizeProfile == kb::ecs::ChunkSizeProfile::Chunk128KB, "ECS Streaming128KPlus config uses invalid storage chunk profile");
    kb::tests::Require(streaming128K.executionGrainSize == 8192, "ECS Streaming128KPlus config exposes invalid execution grain size");
    kb::tests::Require(streaming128K.queryPrefetchDistance == 48, "ECS Streaming128KPlus config exposes invalid query prefetch distance");
    kb::tests::Require(streaming128K.maxNativeStorageCommittedPayloadBytes == kb::ecs::WorldConfigPresets::GiB(8), "ECS Streaming128KPlus config exposes invalid memory guard");

    const kb::ecs::WorldConfig benchmarkAuto = kb::ecs::WorldProfileConfig(kb::ecs::WorldProfile::BenchmarkAuto);
    kb::tests::Require(benchmarkAuto.chunkSizeProfile == kb::ecs::ChunkSizeProfile::Chunk32KB, "ECS BenchmarkAuto config uses invalid fallback chunk profile");
    kb::tests::Require(benchmarkAuto.executionGrainSize == 4096, "ECS BenchmarkAuto config exposes invalid execution grain size");
    kb::tests::Require(benchmarkAuto.maxNativeStorageCommittedPayloadBytes == kb::ecs::WorldConfigPresets::GiB(2), "ECS BenchmarkAuto config exposes invalid memory guard");

    const kb::ecs::WorldConfig benchmarkDefault = kb::ecs::WorldConfigPresets::BenchmarkDefault();
    kb::tests::Require(benchmarkDefault.maxNativeStorageCommittedPayloadBytes == kb::ecs::WorldConfigPresets::GiB(16), "ECS BenchmarkDefault config exposes invalid memory guard");
}

void RunChunkSizeAutoTunerTest() {
    const std::array components{
        TuneComponent<TunePosition>(301),
        TuneComponent<TuneVelocity>(302),
    };

    const kb::ecs::ChunkSizeTuningInput desktopInput{
        .components = components,
        .entityCount = 100'000,
        .workload = kb::ecs::ChunkSizeTuningWorkload::SequentialHotLoop,
        .targetOccupancyPercent = 90.0,
    };
    const kb::ecs::ChunkSizeTuningResult desktop = kb::ecs::TuneChunkSizeProfile(desktopInput);
    kb::tests::Require(desktop.candidateCount == static_cast<std::size_t>(kb::ecs::ChunkSizeProfile::Count), "ECS chunk tuner did not evaluate every chunk profile");
    kb::tests::Require(desktop.recommendation.valid, "ECS chunk tuner did not produce a valid recommendation");
    kb::tests::Require(desktop.recommendation.capacity > 0, "ECS chunk tuner produced an invalid archetype capacity");
    kb::tests::Require(desktop.recommendation.estimatedChunks > 0, "ECS chunk tuner produced an invalid chunk estimate");
    kb::tests::Require(desktop.recommendation.occupancyPercent >= 90.0, "ECS chunk tuner ignored the occupancy target");

    const std::size_t capacity32K = kb::ecs::EstimateChunkCapacity(kb::ecs::ChunkSizeProfile::Chunk32KB, components);
    kb::tests::Require(capacity32K > 0, "ECS chunk tuner could not estimate a normal archetype capacity");
    kb::tests::Require(
        kb::ecs::ChunkSizeTuningPayloadForCapacity(components, capacity32K) <= kb::ecs::ChunkPayloadBytes(kb::ecs::ChunkSizeProfile::Chunk32KB),
        "ECS chunk tuner estimated a capacity that exceeds payload bytes");

    kb::ecs::ChunkSizeTuningInput mobileInput = desktopInput;
    mobileInput.workload = kb::ecs::ChunkSizeTuningWorkload::MobileThermal;
    mobileInput.maxChunkPayloadBytes = 16U * 1024U;
    const kb::ecs::ChunkSizeTuningResult mobile = kb::ecs::TuneChunkSizeProfile(mobileInput);
    kb::tests::Require(mobile.recommendation.valid, "ECS chunk tuner did not produce a mobile recommendation");
    kb::tests::Require(mobile.recommendation.payloadBytes <= 16U * 1024U, "ECS chunk tuner ignored max chunk payload bytes");

    const kb::ecs::ChunkSizeAutoTunedWorldConfig tunedConfig =
        kb::ecs::AutoTuneWorldConfig(kb::ecs::WorldConfigPresets::BenchmarkAuto(), mobileInput);
    kb::tests::Require(tunedConfig.applied, "ECS chunk tuner did not apply a valid recommendation to world config");
    kb::tests::Require(
        tunedConfig.config.chunkSizeProfile == tunedConfig.tuning.recommendation.profile,
        "ECS chunk tuner applied a config that diverged from its recommendation");
    kb::tests::Require(
        kb::ecs::ChunkPayloadBytes(tunedConfig.config.chunkSizeProfile) <= 16U * 1024U,
        "ECS chunk tuner applied a config outside the requested mobile payload budget");
}

void RunChunkProfileCapacityMatrixTest() {
    const std::array components{
        TuneComponent<TunePosition>(401),
        TuneComponent<TuneVelocity>(402),
    };

    const std::size_t bytesPerEntity = sizeof(TunePosition) + sizeof(TuneVelocity);
    for (kb::ecs::ChunkSizeProfile profile : kb::ecs::AllChunkSizeProfiles()) {
        const std::size_t payloadBytes = kb::ecs::ChunkPayloadBytes(profile);
        const std::size_t expectedCapacity = payloadBytes / bytesPerEntity;
        const std::size_t capacity = kb::ecs::EstimateChunkCapacity(profile, components);
        kb::tests::Require(capacity == expectedCapacity, "ECS chunk capacity matrix produced an unexpected capacity");
        kb::tests::Require(kb::ecs::ChunkSizeTuningPayloadForCapacity(components, capacity) <= payloadBytes, "ECS chunk capacity matrix overfilled a chunk");
        kb::tests::Require(
            kb::ecs::ChunkSizeTuningPayloadForCapacity(components, capacity + 1U) > payloadBytes,
            "ECS chunk capacity matrix left room for another entity");

        const kb::ecs::ChunkSizeTuningInput input{
            .components = components,
            .entityCount = capacity + 1U,
            .workload = kb::ecs::ChunkSizeTuningWorkload::SequentialHotLoop,
            .targetOccupancyPercent = 1.0,
        };
        const kb::ecs::ChunkSizeTuningCandidate candidate = kb::ecs::EvaluateChunkSizeProfile(input, profile);
        kb::tests::Require(candidate.valid, "ECS chunk capacity matrix produced an invalid candidate");
        kb::tests::Require(candidate.payloadBytes == payloadBytes, "ECS chunk capacity matrix reported invalid payload bytes");
        kb::tests::Require(candidate.bytesPerEntity == bytesPerEntity, "ECS chunk capacity matrix reported invalid bytes per entity");
        kb::tests::Require(candidate.hotBytesPerEntity == bytesPerEntity, "ECS chunk capacity matrix misclassified default hot bytes");
        kb::tests::Require(candidate.nonHotBytesPerEntity == 0U, "ECS chunk capacity matrix reported non-hot bytes for default hot components");
        kb::tests::Require(candidate.capacity == capacity, "ECS chunk capacity matrix reported inconsistent capacity");
        kb::tests::Require(candidate.hotOnlyCapacity == capacity, "ECS chunk capacity matrix reported hot-only loss for default hot components");
        kb::tests::Require(candidate.capacityLostToNonHotStorage == 0U, "ECS chunk capacity matrix reported non-hot capacity loss for default hot components");
        kb::tests::Require(candidate.estimatedChunks == 2U, "ECS chunk capacity matrix reported invalid chunk count");
        kb::tests::Require(candidate.estimatedAllocatedBytes == payloadBytes * 2U, "ECS chunk capacity matrix reported invalid allocated bytes");
        kb::tests::Require(candidate.estimatedUsedBytes == (capacity + 1U) * bytesPerEntity, "ECS chunk capacity matrix reported invalid used bytes");
        kb::tests::Require(
            candidate.estimatedWastedBytes == candidate.estimatedAllocatedBytes - candidate.estimatedUsedBytes,
            "ECS chunk capacity matrix reported invalid wasted bytes");
        kb::tests::Require(candidate.estimatedSparseChunks == 1U, "ECS chunk capacity matrix did not report the sparse tail chunk");
        kb::tests::Require(candidate.occupancyPercent > 0.0 && candidate.occupancyPercent <= 100.0, "ECS chunk capacity matrix reported invalid occupancy");
    }

    const std::array mixedComponents{
        TuneComponent<TunePosition>(501),
        kb::ecs::NativeComponentType{
            .id = 502,
            .size = sizeof(TuneVelocity),
            .alignment = alignof(TuneVelocity),
            .storageClass = kb::ecs::ComponentStorageClass::ColdTable,
        },
    };
    const kb::ecs::ChunkSizeTuningInput mixedInput{
        .components = mixedComponents,
        .entityCount = 10'000U,
        .workload = kb::ecs::ChunkSizeTuningWorkload::SequentialHotLoop,
        .targetOccupancyPercent = 1.0,
    };
    const kb::ecs::ChunkSizeTuningCandidate mixed = kb::ecs::EvaluateChunkSizeProfile(
        mixedInput,
        kb::ecs::ChunkSizeProfile::Chunk4KB);
    kb::tests::Require(mixed.valid, "ECS chunk capacity matrix rejected a mixed hot/cold archetype");
    kb::tests::Require(mixed.hotBytesPerEntity == sizeof(TunePosition), "ECS chunk capacity matrix missed mixed hot bytes");
    kb::tests::Require(mixed.nonHotBytesPerEntity == sizeof(TuneVelocity), "ECS chunk capacity matrix missed mixed non-hot bytes");
    kb::tests::Require(mixed.hotOnlyCapacity == mixed.capacity, "ECS chunk capacity matrix did not recover mixed hot-only capacity");
    kb::tests::Require(
        mixed.capacityLostToNonHotStorage > 0U,
        "ECS chunk capacity matrix did not expose avoided mixed non-hot capacity loss");
}

void RunQueryReductionSlotLayoutTest() {
    static_assert(alignof(kb::ecs::QueryReductionSlot<ReductionProbe>) >= kb::ecs::kQueryReductionSlotCacheLineBytes);
    static_assert((sizeof(kb::ecs::QueryReductionSlot<ReductionProbe>) % kb::ecs::kQueryReductionSlotCacheLineBytes) == 0U);

    kb::ecs::QueryReductionScratch<ReductionProbe> scratch;
    scratch.Reset(4);

    const auto first = reinterpret_cast<std::uintptr_t>(&scratch.Slot(0));
    const auto second = reinterpret_cast<std::uintptr_t>(&scratch.Slot(1));
    kb::tests::Require((first % kb::ecs::kQueryReductionSlotCacheLineBytes) == 0U, "ECS query reduction slot is not cache-line aligned");
    kb::tests::Require(
        second - first >= kb::ecs::kQueryReductionSlotCacheLineBytes,
        "ECS query reduction slots can share a cache line");
}

void RunQueryExecutionTuningTest() {
    static_assert(kb::ecs::QueryExecutionWorkloadGrainLimits(kb::ecs::QueryExecutionWorkloadClass::ReadOnlyMemory).minimum == 8U * 1024U);
    static_assert(kb::ecs::QueryExecutionWorkloadGrainLimits(kb::ecs::QueryExecutionWorkloadClass::LinearWrite).maximum == 32U * 1024U);
    static_assert(kb::ecs::QueryExecutionWorkloadGrainLimits(kb::ecs::QueryExecutionWorkloadClass::DenseMatrixWrite).preferred == 16U * 1024U);
    static_assert(kb::ecs::QueryExecutionWorkloadGrainLimits(kb::ecs::QueryExecutionWorkloadClass::HeavyTransform).preferred == 2U * 1024U);
    static_assert(kb::ecs::ResolveQueryExecutionGrainSize(kb::ecs::QueryExecutionTuningInput{
                      .workload = kb::ecs::QueryExecutionWorkloadClass::ReadOnlyMemory,
                      .entityCount = 10'000,
                      .workerCount = 4,
                  }) == 8192U);
    static_assert(kb::ecs::ResolveQueryExecutionGrainSize(kb::ecs::QueryExecutionTuningInput{
                      .workload = kb::ecs::QueryExecutionWorkloadClass::LinearWrite,
                      .entityCount = 1'000'000,
                      .workerCount = 4,
                  }) == 32768U);
    static_assert(kb::ecs::ResolveQueryExecutionGrainSize(kb::ecs::QueryExecutionTuningInput{
                      .workload = kb::ecs::QueryExecutionWorkloadClass::DenseMatrixWrite,
                      .entityCount = 1'000'000,
                      .workerCount = 4,
                  }) == 62500U);
    static_assert(kb::ecs::ResolveQueryExecutionGrainSize(kb::ecs::QueryExecutionTuningInput{
                      .workload = kb::ecs::QueryExecutionWorkloadClass::HeavyTransform,
                      .entityCount = 1'000'000,
                      .workerCount = 4,
                  }) == 4096U);
    static_assert(kb::ecs::ResolveQueryExecutionPolicy(
                      kb::ecs::QueryExecutionSettings{ .policy = kb::ecs::QueryExecutionPolicy::SIMDPreferred },
                      kb::ecs::QueryExecutionTuningInput{
                          .workload = kb::ecs::QueryExecutionWorkloadClass::DenseMatrixWrite,
                          .entityCount = 64'000,
                          .workerCount = 4,
                      })
        == kb::ecs::QueryExecutionPolicy::SingleThread);
    static_assert(kb::ecs::ResolveQueryExecutionPolicy(
                      kb::ecs::QueryExecutionSettings{ .policy = kb::ecs::QueryExecutionPolicy::SIMDPreferred },
                      kb::ecs::QueryExecutionTuningInput{
                          .workload = kb::ecs::QueryExecutionWorkloadClass::DenseMatrixWrite,
                          .entityCount = 1'000'000,
                          .workerCount = 4,
                      })
        == kb::ecs::QueryExecutionPolicy::SIMDPreferred);
    static_assert(kb::ecs::ResolveQueryExecutionPolicy(
                      kb::ecs::QueryExecutionSettings{ .policy = kb::ecs::QueryExecutionPolicy::SIMDPreferred },
                      kb::ecs::QueryExecutionTuningInput{
                          .workload = kb::ecs::QueryExecutionWorkloadClass::LinearWrite,
                          .entityCount = 2'000'000,
                          .workerCount = 4,
                      })
        == kb::ecs::QueryExecutionPolicy::SingleThread);
    static_assert(kb::ecs::ResolveQueryExecutionPolicy(
                      kb::ecs::QueryExecutionSettings{ .policy = kb::ecs::QueryExecutionPolicy::ParallelRanges },
                      kb::ecs::QueryExecutionTuningInput{
                          .workload = kb::ecs::QueryExecutionWorkloadClass::LinearWrite,
                          .entityCount = 1'000'000,
                          .workerCount = 4,
                      })
        == kb::ecs::QueryExecutionPolicy::SingleThread);
    static_assert(kb::ecs::ResolveQueryExecutionPolicy(
                      kb::ecs::QueryExecutionSettings{ .policy = kb::ecs::QueryExecutionPolicy::ParallelChunks },
                      kb::ecs::QueryExecutionTuningInput{
                          .workload = kb::ecs::QueryExecutionWorkloadClass::LinearWrite,
                          .entityCount = 1'000'000,
                          .workerCount = 4,
                      })
        == kb::ecs::QueryExecutionPolicy::SingleThread);
    static_assert(kb::ecs::ResolveQueryExecutionPolicy(
                      kb::ecs::QueryExecutionSettings{ .policy = kb::ecs::QueryExecutionPolicy::SIMDPreferred },
                      kb::ecs::QueryExecutionTuningInput{
                          .workload = kb::ecs::QueryExecutionWorkloadClass::LinearWrite,
                          .entityCount = 10'000'000,
                          .workerCount = 4,
                      })
        == kb::ecs::QueryExecutionPolicy::SIMDPreferred);

    kb::ecs::QueryExecutionSettings settings{
        .maxBatchSize = 4096,
        .policy = kb::ecs::QueryExecutionPolicy::SIMDPreferred,
        .reductionMode = kb::ecs::QueryReductionMode::PerWorker,
        .prefetchDistance = 32,
        .workerCountOverride = 4,
    };
    settings = kb::ecs::TuneQueryExecutionSettings(settings, kb::ecs::QueryExecutionTuningInput{
        .workload = kb::ecs::QueryExecutionWorkloadClass::ReadOnlyMemory,
        .entityCount = 1'000'000,
        .workerCount = 4,
    });
    kb::tests::Require(settings.maxBatchSize == 62500U, "ECS query tuning did not expand read-only grain for large workloads");
    kb::tests::Require(settings.policy == kb::ecs::QueryExecutionPolicy::SIMDPreferred, "ECS query tuning changed query policy");
    kb::tests::Require(settings.reductionMode == kb::ecs::QueryReductionMode::PerWorker, "ECS query tuning changed reduction mode");
    kb::tests::Require(settings.prefetchDistance == 32U, "ECS query tuning changed prefetch distance");
    kb::tests::Require(settings.workerCountOverride == 4U, "ECS query tuning changed worker cap");

    kb::ecs::QueryExecutionSettings automaticLinearWrite =
        kb::ecs::TuneQueryExecutionSettings(kb::ecs::QueryExecutionSettings{ .policy = kb::ecs::QueryExecutionPolicy::SIMDPreferred }, kb::ecs::QueryExecutionTuningInput{
            .workload = kb::ecs::QueryExecutionWorkloadClass::LinearWrite,
            .entityCount = 1'000'000,
            .workerCount = 4,
    });
    kb::tests::Require(automaticLinearWrite.policy == kb::ecs::QueryExecutionPolicy::SingleThread, "ECS query tuning did not avoid scheduler overhead for medium linear writes");
    kb::tests::Require(automaticLinearWrite.maxBatchSize == 1'000'000U, "ECS query tuning did not coalesce single-thread linear writes");
}

} // namespace

namespace kb::tests {

void RunEcsConfigTests() {
    RunWorldConfigTest();
    RunChunkSizeAutoTunerTest();
    RunChunkProfileCapacityMatrixTest();
    RunQueryReductionSlotLayoutTest();
    RunQueryExecutionTuningTest();
}

} // namespace kb::tests
