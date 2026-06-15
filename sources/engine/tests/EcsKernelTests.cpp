#include "EcsTestSuites.hpp"
#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/Kernel.hpp"
#include "engine/ecs/World.hpp"

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

} // namespace

namespace kb::tests {

void RunEcsKernelTests() {
    RunEcsKernelContractScalarExecutionTest();
    RunEcsKernelRequestedSimdFallsBackToScalarTest();
    RunEcsKernelSse2AndAvx2DispatchTest();
    RunEcsKernelAvx512DispatchTest();
    RunEcsCompiledKernelQueryExecutionTest();
}

} // namespace kb::tests
