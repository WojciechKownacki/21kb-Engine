#include "EcsTestTypes.hpp"
#include "EcsTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/World.hpp"

#include <algorithm>

namespace {

void CountMovingBatches(const kb::ecs::QueryBatch<EcsPosition, EcsVelocity>& batch, void* context) {
    auto* counters = static_cast<EcsBatchCounters*>(context);
    ++counters->batches;
    counters->maxBatch = std::max(counters->maxBatch, batch.Count());

    const EcsPosition* positions = batch.Components<0>();
    const EcsVelocity* velocities = batch.Components<1>();
    for (std::size_t index = 0; index < batch.Count(); ++index) {
        kb::tests::Require(batch.EntityAt(index).IsValid(), "Typed ECS batch query returned invalid entity");
        ++counters->visited;
        counters->sumX += positions[index].x + velocities[index].x;
    }
}

void RunTypedEcsQueryBatchTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 128,
    });
    for (int index = 0; index < 600; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = 1.0F, .y = 0.0F });
        world.Set(entity, EcsVelocity{ .x = 2.0F, .y = 0.0F });
    }

    kb::ecs::Query<EcsPosition, EcsVelocity> query = world.CreateQuery<EcsPosition, EcsVelocity>();
    EcsBatchCounters counters;
    query.ForEachBatch(&CountMovingBatches, &counters);
    kb::tests::Require(counters.visited == 600, "Typed ECS batch query did not visit all entities");
    kb::tests::Require(counters.batches >= 5, "Typed ECS batch query did not split work into execution grains");
    kb::tests::Require(counters.maxBatch <= 128, "Typed ECS batch query exceeded configured execution grain size");
    kb::tests::Require(kb::tests::NearlyEqual(counters.sumX, 1800.0F), "Typed ECS batch query saw invalid component data");
}

} // namespace

namespace kb::tests {

void RunEcsQueryTests() {
    RunTypedEcsQueryBatchTest();
}

} // namespace kb::tests
