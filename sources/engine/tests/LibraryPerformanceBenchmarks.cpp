#include "TestSupport.hpp"

#include "engine/library/EngineLibraryCollections.hpp"
#include "engine/library/EngineLibrarySignal.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct BenchmarkResult {
    std::size_t entityCount = 0U;
    double spawnMilliseconds = 0.0;
    double queryMilliseconds = 0.0;
    double transformReadMilliseconds = 0.0;
    double eventEmitMilliseconds = 0.0;
    double physicsCallMilliseconds = 0.0;
};

[[nodiscard]] double ElapsedMilliseconds(Clock::time_point startedAt) noexcept {
    return std::chrono::duration<double, std::milli>(Clock::now() - startedAt).count();
}

void CountTransforms(kb::scene::SceneEntity, const kb::scene::TransformComponent&, void* context) {
    auto* count = static_cast<std::size_t*>(context);
    ++*count;
}

[[nodiscard]] BenchmarkResult RunBenchmark(std::size_t entityCount) {
    BenchmarkResult result{ .entityCount = entityCount };
    kb::scene::Scene scene;
    std::vector<kb::scene::SceneEntity> entities;
    entities.reserve(entityCount);

    const Clock::time_point spawnStartedAt = Clock::now();
    for (std::size_t index = 0U; index < entityCount; ++index) {
        entities.push_back(scene.Entities().CreateEntity());
    }
    result.spawnMilliseconds = ElapsedMilliseconds(spawnStartedAt);
    kb::tests::Require(entities.size() == entityCount, "Library benchmark spawn did not create the requested entity count");

    std::size_t visitedTransforms = 0U;
    const Clock::time_point queryStartedAt = Clock::now();
    scene.Transforms().ForEach(&CountTransforms, &visitedTransforms);
    result.queryMilliseconds = ElapsedMilliseconds(queryStartedAt);
    kb::tests::Require(visitedTransforms == entityCount, "Library benchmark query did not visit every spawned entity");

    std::vector<kb::scene::TransformComponent> transforms(entityCount);
    const Clock::time_point transformReadStartedAt = Clock::now();
    const bool allTransformsRead = scene.Transforms().ReadNonAlloc(entities, transforms);
    result.transformReadMilliseconds = ElapsedMilliseconds(transformReadStartedAt);
    kb::tests::Require(allTransformsRead, "Library benchmark transform read did not resolve every spawned entity");

    kb::library::Signal<> signal;
    std::size_t emittedCount = 0U;
    kb::tests::Require(signal.Connect([&emittedCount]() { ++emittedCount; }) != kb::library::Signal<>::kInvalidSlotId,
        "Library benchmark could not connect its event listener");
    std::array<kb::library::Signal<>::SlotId, 1U> eventScratch{};
    const Clock::time_point eventEmitStartedAt = Clock::now();
    for (std::size_t index = 0U; index < entityCount; ++index) {
        kb::tests::Require(signal.EmitNonAlloc(eventScratch), "Library benchmark event emit rejected valid scratch storage");
    }
    result.eventEmitMilliseconds = ElapsedMilliseconds(eventEmitStartedAt);
    kb::tests::Require(emittedCount == entityCount, "Library benchmark event emit did not dispatch every event");

    for (const kb::scene::SceneEntity entity : entities) {
        scene.Components().Colliders().Set(entity, kb::scene::ColliderComponent{});
    }
    std::array<kb::scene::PhysicsCastResult, 1U> raycastStorage{};
    kb::library::ArrayNonAlloc<kb::scene::PhysicsCastResult> raycastResults{ raycastStorage };
    const Clock::time_point physicsCallStartedAt = Clock::now();
    kb::scene::RaycastAllNonAlloc(
        scene,
        kb::scene::Vec3{ -2.0F, 0.0F, 0.0F },
        kb::scene::Vec3{ 1.0F, 0.0F, 0.0F },
        10.0F,
        kb::scene::kPhysicsAllLayers,
        raycastResults);
    result.physicsCallMilliseconds = ElapsedMilliseconds(physicsCallStartedAt);
    kb::tests::Require(raycastResults.Count() == 1U && raycastResults.GetAt(0U)->hit,
        "Library benchmark physics call did not execute collider geometry against the spawned entities");

    return result;
}

void PrintResult(const BenchmarkResult& result) {
    std::cout << "LIB-239 " << result.entityCount << " entities:"
              << " spawn=" << result.spawnMilliseconds << "ms"
              << " query=" << result.queryMilliseconds << "ms"
              << " transform-read=" << result.transformReadMilliseconds << "ms"
              << " event-emit=" << result.eventEmitMilliseconds << "ms"
              << " physics-call=" << result.physicsCallMilliseconds << "ms\n";
}

} // namespace

int main() {
    for (const std::size_t entityCount : std::array<std::size_t, 3U>{ 1'000U, 10'000U, 100'000U }) {
        PrintResult(RunBenchmark(entityCount));
    }
    return 0;
}
