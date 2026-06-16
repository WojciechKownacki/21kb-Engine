#include "EcsTestSuites.hpp"
#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/Kernel.hpp"
#include "engine/ecs/KernelVectorMath.hpp"
#include "engine/ecs/World.hpp"

#include <array>
#include <bit>
#include <cstdint>

namespace {

struct EcsMovementScaleAsset {
    float multiplier = 1.0F;
};

struct EcsMovementConstants {
    float deltaSeconds = 0.0F;
    float drag = 0.0F;
};

using EcsMovementKernelContract = kb::ecs::KernelContract<
    kb::ecs::KernelInputComponents<EcsVelocity>,
    kb::ecs::KernelOutputComponents<EcsPosition>,
    kb::ecs::KernelReadOnlyAssets<EcsMovementScaleAsset>,
    EcsMovementConstants>;

struct EcsMovementKernel {
    void operator()(kb::ecs::KernelBatch<EcsMovementKernelContract>& batch) const {
        const EcsVelocity* velocities = batch.Inputs<0>();
        EcsPosition* positions = batch.Outputs<0>();
        const EcsMovementScaleAsset& scale = batch.Asset<0>();
        const EcsMovementConstants& constants = batch.Constants();

        for (std::size_t index = 0; index < batch.Count(); ++index) {
            batch.Prefetch(index + 4);
            positions[index].x += velocities[index].x * constants.deltaSeconds * scale.multiplier;
            positions[index].y += velocities[index].y * constants.deltaSeconds * (scale.multiplier - constants.drag);
        }
    }
};

using EcsSimdKernelContract = kb::ecs::KernelContract<
    kb::ecs::KernelInputComponents<EcsVelocity>,
    kb::ecs::KernelOutputComponents<EcsPosition>>;

void AddProbeMovement(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch, float scale) {
    const EcsVelocity* velocities = batch.Inputs<0>();
    EcsPosition* positions = batch.Outputs<0>();
    for (std::size_t index = 0; index < batch.Count(); ++index) {
        positions[index].x += velocities[index].x * scale;
        positions[index].y += velocities[index].y * scale;
    }
}

struct EcsScalarOnlyProbeKernel {
    int* scalarBatches = nullptr;

    void operator()(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch) const {
        ++(*scalarBatches);
        AddProbeMovement(batch, 1.0F);
    }
};

struct EcsSimdProbeKernel {
    int* scalarBatches = nullptr;
    int* sse2Batches = nullptr;
    int* avx2Batches = nullptr;
    int* avx512Batches = nullptr;

    void operator()(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch) const {
        ++(*scalarBatches);
        AddProbeMovement(batch, 1.0F);
    }

    void operator()(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch, kb::ecs::KernelSse2Tag) const {
        ++(*sse2Batches);
        AddProbeMovement(batch, 2.0F);
    }

    void operator()(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch, kb::ecs::KernelAvx2Tag) const {
        ++(*avx2Batches);
        AddProbeMovement(batch, 4.0F);
    }

    void operator()(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch, kb::ecs::KernelAvx512Tag) const {
        ++(*avx512Batches);
        AddProbeMovement(batch, 8.0F);
    }
};

[[nodiscard]] float SumPositions(kb::ecs::KernelQuery<EcsSimdKernelContract>& query) {
    float sum = 0.0F;
    query.ForEachBatchKernel([&sum](const kb::ecs::QueryBatch<EcsVelocity, EcsPosition>& batch) {
        const EcsPosition* positions = batch.Components<1>();
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            sum += positions[index].x + positions[index].y;
        }
    });
    return sum;
}

void PopulateSimdProbeWorld(kb::ecs::World& world, int entityCount) {
    for (int index = 0; index < entityCount; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsVelocity{ .x = 1.0F, .y = 2.0F });
        world.Set(entity, EcsPosition{ .x = 0.0F, .y = 0.0F });
    }
}

[[nodiscard]] std::uint32_t FloatBits(float value) noexcept {
    return std::bit_cast<std::uint32_t>(value);
}

[[nodiscard]] float DeterministicMulAdd(float multiplicand, float multiplier, float addend) noexcept {
    const float product = multiplicand * multiplier;
    return product + addend;
}

template <std::size_t Count>
[[nodiscard]] std::array<std::uint32_t, Count * 2U> ReferenceMovementBits(
    std::array<EcsPosition, Count> positions,
    const std::array<EcsVelocity, Count>& velocities) noexcept {
    constexpr float deltaSeconds = 0.0166666675F;
    constexpr float maxSpeedSquared = 2.5F;
    constexpr float highBias = 0.125F;
    constexpr float lowBias = -0.25F;

    std::array<std::uint32_t, Count * 2U> bits{};
    for (std::size_t index = 0; index < Count; ++index) {
        const float speedSquared = velocities[index].x * velocities[index].x + velocities[index].y * velocities[index].y;
        const float bias = speedSquared > maxSpeedSquared ? highBias : lowBias;
        positions[index].x = DeterministicMulAdd(velocities[index].x, deltaSeconds, positions[index].x);
        positions[index].y = DeterministicMulAdd(velocities[index].y, deltaSeconds, positions[index].y + bias);
        bits[index * 2U] = FloatBits(positions[index].x);
        bits[index * 2U + 1U] = FloatBits(positions[index].y);
    }
    return bits;
}

template <typename BackendTag, std::size_t Count>
[[nodiscard]] std::array<std::uint32_t, Count * 2U> VectorMovementBits(
    std::array<EcsPosition, Count> positions,
    const std::array<EcsVelocity, Count>& velocities) noexcept {
    using FloatLanes = kb::ecs::KernelFloatLanes<BackendTag>;
    using Vec2Lanes = kb::ecs::KernelFloat2Lanes<BackendTag>;

    constexpr float deltaSeconds = 0.0166666675F;
    constexpr float maxSpeedSquared = 2.5F;
    constexpr float highBias = 0.125F;
    constexpr float lowBias = -0.25F;

    for (std::size_t begin = 0; begin < Count; begin += FloatLanes::LaneCount) {
        const std::size_t remaining = Count - begin;
        const std::size_t batchCount = remaining < FloatLanes::LaneCount ? remaining : FloatLanes::LaneCount;

        Vec2Lanes position = Vec2Lanes::LoadMembersPartial(positions.data() + begin, batchCount, 0.0F, &EcsPosition::x, &EcsPosition::y);
        const Vec2Lanes velocity = Vec2Lanes::LoadMembersPartial(velocities.data() + begin, batchCount, 0.0F, &EcsVelocity::x, &EcsVelocity::y);
        const FloatLanes speedSquared = kb::ecs::KernelDot(velocity, velocity);
        const FloatLanes bias = kb::ecs::KernelSelect(
            kb::ecs::KernelGreaterThan(speedSquared, FloatLanes::Splat(maxSpeedSquared)),
            FloatLanes::Splat(highBias),
            FloatLanes::Splat(lowBias));

        position.template Component<1>() = position.template Component<1>() + bias;
        position = kb::ecs::KernelDeterministicMulAdd(velocity, FloatLanes::Splat(deltaSeconds), position);
        position.StoreMembersPartial(positions.data() + begin, batchCount, &EcsPosition::x, &EcsPosition::y);
    }

    std::array<std::uint32_t, Count * 2U> bits{};
    for (std::size_t index = 0; index < Count; ++index) {
        bits[index * 2U] = FloatBits(positions[index].x);
        bits[index * 2U + 1U] = FloatBits(positions[index].y);
    }
    return bits;
}

void RunEcsKernelContractScalarExecutionTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 8,
    });

    for (int index = 0; index < 17; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsVelocity{ .x = 2.0F, .y = 4.0F });
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 1.0F });
    }

    kb::ecs::SystemAccess access = kb::ecs::DeclareKernelAccess<EcsMovementKernelContract>(world);
    kb::tests::Require(access.ReadComponents().size() == 1U, "ECS kernel access contract did not declare input component reads");
    kb::tests::Require(access.WriteComponents().size() == 1U, "ECS kernel access contract did not declare output component writes");
    kb::tests::Require(access.ReadComponents().front() != access.WriteComponents().front(), "ECS kernel access contract overlapped input and output components");

    kb::ecs::KernelQuery<EcsMovementKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
    const EcsMovementScaleAsset asset{ .multiplier = 3.0F };
    const EcsMovementConstants constants{
        .deltaSeconds = 0.5F,
        .drag = 1.0F,
    };

    kb::ecs::ExecuteKernelScalar<EcsMovementKernelContract>(
        query,
        kb::ecs::QueryExecutionSettings{
            .maxBatchSize = 8,
            .prefetchDistance = 4,
        },
        EcsMovementKernel{},
        kb::ecs::BindKernelAssets(asset),
        constants);

    int visited = 0;
    float sumX = 0.0F;
    float sumY = 0.0F;
    query.ForEachBatchKernel([&visited, &sumX, &sumY](const kb::ecs::QueryBatch<EcsVelocity, EcsPosition>& batch) {
        const EcsPosition* positions = batch.Components<1>();
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            ++visited;
            sumX += positions[index].x;
            sumY += positions[index].y;
        }
    });

    kb::tests::Require(visited == 17, "ECS scalar kernel did not visit all matching entities");
    kb::tests::Require(kb::tests::NearlyEqual(sumX, 187.0F), "ECS scalar kernel did not apply input components, output components, assets and constants to X");
    kb::tests::Require(kb::tests::NearlyEqual(sumY, 85.0F), "ECS scalar kernel did not apply input components, output components, assets and constants to Y");
}

void RunEcsKernelRequestedSimdFallsBackToScalarTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 5,
    });
    PopulateSimdProbeWorld(world, 13);

    int scalarBatches = 0;
    kb::ecs::KernelQuery<EcsSimdKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
    const kb::ecs::KernelNoConstants constants{};
    kb::ecs::ExecuteKernelAvx2<EcsSimdKernelContract>(
        query,
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 5 },
        EcsScalarOnlyProbeKernel{ .scalarBatches = &scalarBatches },
        kb::ecs::BindKernelAssets(),
        constants);

    kb::tests::Require(scalarBatches == 3, "ECS kernel did not use scalar fallback for a requested SIMD backend without an overload");
    kb::tests::Require(kb::tests::NearlyEqual(SumPositions(query), 39.0F), "ECS scalar fallback did not process all kernel rows");
}

void RunEcsKernelSse2AndAvx2DispatchTest() {
    const kb::ecs::KernelNoConstants constants{};

    {
        kb::ecs::World world(kb::ecs::WorldConfig{
            .executionGrainSize = 4,
        });
        PopulateSimdProbeWorld(world, 8);

        int scalarBatches = 0;
        int sse2Batches = 0;
        int avx2Batches = 0;
        int avx512Batches = 0;
        kb::ecs::KernelQuery<EcsSimdKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
        auto compiled = kb::ecs::CompileKernelQuery<EcsSimdKernelContract>(
            std::move(query),
            kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 },
            kb::ecs::KernelBackendPreference::Sse2,
            EcsSimdProbeKernel{
                .scalarBatches = &scalarBatches,
                .sse2Batches = &sse2Batches,
                .avx2Batches = &avx2Batches,
                .avx512Batches = &avx512Batches,
            },
            kb::ecs::BindKernelAssets(),
            constants);

        compiled.Execute();
        kb::ecs::KernelQuery<EcsSimdKernelContract> verifyQuery = world.CreateQuery<EcsVelocity, EcsPosition>();
        if (kb::ecs::IsKernelBackendSupported(kb::ecs::KernelBackend::Sse2)) {
            kb::tests::Require(compiled.ResolvedBackend() == kb::ecs::KernelBackend::Sse2, "ECS kernel did not resolve the requested SSE2 backend");
            kb::tests::Require(sse2Batches == 2 && scalarBatches == 0 && avx2Batches == 0 && avx512Batches == 0, "ECS kernel did not dispatch through the SSE2 overload");
            kb::tests::Require(kb::tests::NearlyEqual(SumPositions(verifyQuery), 48.0F), "ECS SSE2 kernel path did not process all rows");
        } else {
            kb::tests::Require(compiled.ResolvedBackend() == kb::ecs::KernelBackend::Scalar, "ECS kernel did not resolve SSE2 to scalar on unsupported hardware");
            kb::tests::Require(scalarBatches == 2 && sse2Batches == 0 && avx2Batches == 0 && avx512Batches == 0, "ECS kernel did not fall back to scalar when SSE2 was unavailable");
            kb::tests::Require(kb::tests::NearlyEqual(SumPositions(verifyQuery), 24.0F), "ECS scalar fallback for SSE2 did not process all rows");
        }
    }

    {
        kb::ecs::World world(kb::ecs::WorldConfig{
            .executionGrainSize = 4,
        });
        PopulateSimdProbeWorld(world, 8);

        int scalarBatches = 0;
        int sse2Batches = 0;
        int avx2Batches = 0;
        int avx512Batches = 0;
        kb::ecs::KernelQuery<EcsSimdKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
        kb::ecs::ExecuteKernelAvx2<EcsSimdKernelContract>(
            query,
            kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 },
            EcsSimdProbeKernel{
                .scalarBatches = &scalarBatches,
                .sse2Batches = &sse2Batches,
                .avx2Batches = &avx2Batches,
                .avx512Batches = &avx512Batches,
            },
            kb::ecs::BindKernelAssets(),
            constants);

        if (kb::ecs::IsKernelBackendSupported(kb::ecs::KernelBackend::Avx2)) {
            kb::tests::Require(avx2Batches == 2 && scalarBatches == 0 && sse2Batches == 0 && avx512Batches == 0, "ECS kernel did not dispatch through the AVX2 overload");
            kb::tests::Require(kb::tests::NearlyEqual(SumPositions(query), 96.0F), "ECS AVX2 kernel path did not process all rows");
        } else {
            kb::tests::Require(scalarBatches == 2 && sse2Batches == 0 && avx2Batches == 0 && avx512Batches == 0, "ECS kernel did not fall back to scalar when AVX2 was unavailable");
            kb::tests::Require(kb::tests::NearlyEqual(SumPositions(query), 24.0F), "ECS scalar fallback for AVX2 did not process all rows");
        }
    }
}

void RunEcsKernelAvx512DispatchTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 4,
    });
    PopulateSimdProbeWorld(world, 8);

    int scalarBatches = 0;
    int sse2Batches = 0;
    int avx2Batches = 0;
    int avx512Batches = 0;
    const kb::ecs::KernelNoConstants constants{};
    kb::ecs::KernelQuery<EcsSimdKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
    auto compiled = kb::ecs::CompileKernelQuery<EcsSimdKernelContract>(
        std::move(query),
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 },
        kb::ecs::KernelBackendPreference::Avx512,
        EcsSimdProbeKernel{
            .scalarBatches = &scalarBatches,
            .sse2Batches = &sse2Batches,
            .avx2Batches = &avx2Batches,
            .avx512Batches = &avx512Batches,
        },
        kb::ecs::BindKernelAssets(),
        constants);

    compiled.Execute();
    kb::ecs::KernelQuery<EcsSimdKernelContract> verifyQuery = world.CreateQuery<EcsVelocity, EcsPosition>();
    if (kb::ecs::IsKernelBackendSupported(kb::ecs::KernelBackend::Avx512)) {
        kb::tests::Require(compiled.ResolvedBackend() == kb::ecs::KernelBackend::Avx512, "ECS kernel did not resolve the requested AVX-512 backend");
        kb::tests::Require(avx512Batches == 2 && scalarBatches == 0 && sse2Batches == 0 && avx2Batches == 0, "ECS kernel did not dispatch through the AVX-512 overload");
        kb::tests::Require(kb::tests::NearlyEqual(SumPositions(verifyQuery), 192.0F), "ECS AVX-512 kernel path did not process all rows");
    } else {
        kb::tests::Require(compiled.ResolvedBackend() == kb::ecs::KernelBackend::Scalar, "ECS kernel did not resolve AVX-512 to scalar on unsupported hardware");
        kb::tests::Require(scalarBatches == 2 && sse2Batches == 0 && avx2Batches == 0 && avx512Batches == 0, "ECS kernel did not fall back to scalar when AVX-512 was unavailable");
        kb::tests::Require(kb::tests::NearlyEqual(SumPositions(verifyQuery), 24.0F), "ECS scalar fallback for AVX-512 did not process all rows");
    }
}

void RunEcsCompiledKernelQueryExecutionTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 4,
    });

    for (int index = 0; index < 10; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsVelocity{ .x = 2.0F, .y = 4.0F });
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 1.0F });
    }

    const EcsMovementScaleAsset asset{ .multiplier = 4.0F };
    const EcsMovementConstants constants{
        .deltaSeconds = 0.25F,
        .drag = 0.5F,
    };

    auto kernelQuery = kb::ecs::CompileKernelQuery<EcsMovementKernelContract>(
        world,
        kb::ecs::QueryExecutionSettings{
            .maxBatchSize = 4,
            .prefetchDistance = 2,
        },
        EcsMovementKernel{},
        kb::ecs::BindKernelAssets(asset),
        constants);

    kb::tests::Require(kernelQuery.IsValid(), "Compiled ECS kernel query did not create a valid typed query");
    kb::tests::Require(kernelQuery.Settings().maxBatchSize == 4U, "Compiled ECS kernel query did not retain execution settings");

    kernelQuery.Execute();
    kernelQuery.Execute();

    kb::ecs::KernelQuery<EcsMovementKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
    int visited = 0;
    float sumX = 0.0F;
    float sumY = 0.0F;
    query.ForEachBatchKernel([&visited, &sumX, &sumY](const kb::ecs::QueryBatch<EcsVelocity, EcsPosition>& batch) {
        const EcsPosition* positions = batch.Components<1>();
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            ++visited;
            sumX += positions[index].x;
            sumY += positions[index].y;
        }
    });

    kb::tests::Require(visited == 10, "Compiled ECS kernel query did not visit all matching entities");
    kb::tests::Require(kb::tests::NearlyEqual(sumX, 85.0F), "Compiled ECS kernel query did not persist output component writes to X");
    kb::tests::Require(kb::tests::NearlyEqual(sumY, 80.0F), "Compiled ECS kernel query did not persist output component writes to Y");
}

void RunEcsKernelVectorMathBitDeterminismTest() {
    constexpr std::size_t kCount = 23U;
    std::array<EcsPosition, kCount> positions{};
    std::array<EcsVelocity, kCount> velocities{};

    for (std::size_t index = 0; index < kCount; ++index) {
        const float sample = static_cast<float>(index);
        positions[index] = EcsPosition{
            .x = sample * 0.125F - 1.5F,
            .y = 3.0F - sample * 0.0625F,
        };
        velocities[index] = EcsVelocity{
            .x = sample * 0.03125F - 0.5F,
            .y = 1.25F - sample * 0.046875F,
        };
    }

    const std::array<std::uint32_t, kCount * 2U> reference = ReferenceMovementBits(positions, velocities);
    kb::tests::Require(VectorMovementBits<kb::ecs::KernelScalarTag>(positions, velocities) == reference, "ECS scalar vector math was not bit-for-bit with reference movement");
    kb::tests::Require(VectorMovementBits<kb::ecs::KernelSse2Tag>(positions, velocities) == reference, "ECS SSE2-width vector math was not bit-for-bit deterministic");
    kb::tests::Require(VectorMovementBits<kb::ecs::KernelAvx2Tag>(positions, velocities) == reference, "ECS AVX2-width vector math was not bit-for-bit deterministic");
    kb::tests::Require(VectorMovementBits<kb::ecs::KernelAvx512Tag>(positions, velocities) == reference, "ECS AVX-512-width vector math was not bit-for-bit deterministic");
}

} // namespace

namespace kb::tests {

void RunEcsKernelTests() {
    RunEcsKernelContractScalarExecutionTest();
    RunEcsKernelRequestedSimdFallsBackToScalarTest();
    RunEcsKernelSse2AndAvx2DispatchTest();
    RunEcsKernelAvx512DispatchTest();
    RunEcsCompiledKernelQueryExecutionTest();
    RunEcsKernelVectorMathBitDeterminismTest();
}

} // namespace kb::tests
