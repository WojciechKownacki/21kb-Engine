#include "TestSupport.hpp"

#include "engine/ecs/NativeArchetypeStorage.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"
#include "scene/transform/SceneTransformDirtyFrontier.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace {

using kb::scene::Scene;
using kb::scene::SceneEntity;
using kb::scene::SceneObject;
using kb::scene::SceneState;
using kb::scene::TransformComponent;

struct ScratchFootprint {
    std::size_t worldSize = 0U;
    std::size_t worldCapacity = 0U;
    std::size_t validSize = 0U;
    std::size_t validCapacity = 0U;
    std::size_t dirtySize = 0U;
    std::size_t dirtyCapacity = 0U;

    [[nodiscard]] std::size_t LiveBytes() const noexcept {
        return worldSize * sizeof(TransformComponent) + validSize * sizeof(std::uint32_t) + dirtySize * sizeof(std::uint8_t);
    }

    [[nodiscard]] std::size_t AllocatedPayloadBytes() const noexcept {
        return worldCapacity * sizeof(TransformComponent) + validCapacity * sizeof(std::uint32_t) + dirtyCapacity * sizeof(std::uint8_t);
    }
};

[[nodiscard]] ScratchFootprint Footprint(const Scene& scene) noexcept {
    const SceneState& state = kb::scene::SceneAccess::State(scene);
    return {
        state.denseTransformWorldScratch.size(), state.denseTransformWorldScratch.capacity(),
        state.denseTransformWorldScratchValid.size(), state.denseTransformWorldScratchValid.capacity(),
        state.denseTransformDirtyScratch.size(), state.denseTransformDirtyScratch.capacity(),
    };
}

void PrintFootprint(const char* phase, const Scene& scene) {
    const auto footprint = Footprint(scene);
    const auto& state = kb::scene::SceneAccess::State(scene);
    std::cout << "transform_scratch phase=" << phase
              << " live_entities=" << scene.Entities().Count()
              << " dense_hierarchy_slots=" << state.denseHierarchyParents.size()
              << " world_size=" << footprint.worldSize << " world_capacity=" << footprint.worldCapacity
              << " valid_size=" << footprint.validSize << " valid_capacity=" << footprint.validCapacity
              << " dirty_size=" << footprint.dirtySize << " dirty_capacity=" << footprint.dirtyCapacity
              << " live_bytes=" << footprint.LiveBytes()
              << " allocated_payload_bytes=" << footprint.AllocatedPayloadBytes()
              << " epoch=" << state.denseTransformWorldScratchEpoch
              << " sparse_world_size=" << state.transformWorldScratch.size()
              << " sparse_dirty_size=" << state.transformDirtyScratch.size() << std::endl;
}

[[nodiscard]] bool ExpectLazyScratch() {
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t length = 0U;
    if (_dupenv_s(&value, &length, "EXPECT_LAZY_SCRATCH") != 0) return false;
    const bool enabled = value != nullptr && std::string_view{value} != "" && std::string_view{value} != "0";
    std::free(value);
    return enabled;
#else
    const char* value = std::getenv("EXPECT_LAZY_SCRATCH");
    return value != nullptr && std::string_view{value} != "" && std::string_view{value} != "0";
#endif
}

void RequireUnusedRootScratch(const Scene& scene, bool expectLazy) {
    if (!expectLazy) return;
    const auto footprint = Footprint(scene);
    kb::tests::Require(footprint.LiveBytes() == 0U && footprint.AllocatedPayloadBytes() == 0U,
        "Fresh native roots must not allocate the unused dense hierarchy scratch");
}

void RequirePreparedDenseScratch(const Scene& scene) {
    const auto footprint = Footprint(scene);
    const auto slots = kb::scene::SceneAccess::State(scene).denseHierarchyParents.size();
    kb::tests::Require(footprint.worldSize == slots && footprint.validSize == slots && footprint.dirtySize == slots,
        "Generic hierarchy propagation must prepare all three scratch arrays before using them");
    kb::tests::Require(footprint.LiveBytes() == slots * (sizeof(TransformComponent) + sizeof(std::uint32_t) + sizeof(std::uint8_t)),
        "Scratch live bytes must be derived from actual array sizes");
}

[[nodiscard]] bool SameVector(kb::scene::Vec3 lhs, kb::scene::Vec3 rhs) noexcept {
    return std::bit_cast<std::uint32_t>(lhs.x) == std::bit_cast<std::uint32_t>(rhs.x) &&
        std::bit_cast<std::uint32_t>(lhs.y) == std::bit_cast<std::uint32_t>(rhs.y) &&
        std::bit_cast<std::uint32_t>(lhs.z) == std::bit_cast<std::uint32_t>(rhs.z);
}

[[nodiscard]] bool SameRotation(kb::scene::Quat lhs, kb::scene::Quat rhs) noexcept {
    return std::bit_cast<std::uint32_t>(lhs.x) == std::bit_cast<std::uint32_t>(rhs.x) &&
        std::bit_cast<std::uint32_t>(lhs.y) == std::bit_cast<std::uint32_t>(rhs.y) &&
        std::bit_cast<std::uint32_t>(lhs.z) == std::bit_cast<std::uint32_t>(rhs.z) &&
        std::bit_cast<std::uint32_t>(lhs.w) == std::bit_cast<std::uint32_t>(rhs.w);
}

void RequireSameTransforms(const Scene& actual, std::span<const SceneObject> actualObjects,
    const Scene& reference, std::span<const SceneObject> referenceObjects) {
    kb::tests::Require(actualObjects.size() == referenceObjects.size(), "Scratch test fixture sizes differ");
    for (std::size_t index = 0U; index < actualObjects.size(); ++index) {
        const auto lhs = actual.Transforms().Get(actualObjects[index]);
        const auto rhs = reference.Transforms().Get(referenceObjects[index]);
        kb::tests::Require(SameVector(lhs.localPosition, rhs.localPosition) && SameRotation(lhs.localRotation, rhs.localRotation) &&
            SameVector(lhs.localScale, rhs.localScale) && SameVector(lhs.worldPosition, rhs.worldPosition) &&
            SameRotation(lhs.worldRotation, rhs.worldRotation) && SameVector(lhs.worldScale, rhs.worldScale) &&
            lhs.localVersion == rhs.localVersion && lhs.parentVersion == rhs.parentVersion && lhs.worldVersion == rhs.worldVersion &&
            lhs.worldDirty == rhs.worldDirty,
            "Lazy scratch changed local/world poses, versions or dirty state against bounded propagation");
    }
}

[[nodiscard]] kb::ecs::WorldConfig SmallChunkConfig() noexcept {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk4KB;
    config.workerThreadLimit = 2U;
    return config;
}

[[nodiscard]] std::vector<SceneObject> CreateRoots(Scene& scene, std::size_t count, std::size_t firstIndex = 0U) {
    std::vector<kb::scene::SceneObjectDesc> descriptions(count);
    for (std::size_t index = 0U; index < count; ++index) {
        auto& transform = descriptions[index].transform;
        transform.localPosition = {static_cast<float>((firstIndex + index) % 31U), 2.0F, -3.0F};
        if ((firstIndex + index) % 3U == 1U) {
            transform.localRotation = {0.0F, 0.38268343F, 0.0F, 0.92387953F};
            transform.localScale = {2.0F, 3.0F, 4.0F};
        }
    }
    return scene.Entities().CreateObjects(descriptions);
}

void MoveRootsThroughPass(Scene& scene) {
    const auto stats = scene.Transforms().ParallelForEachRoot<>(41U, [](kb::scene::TransformRowRange& range) {
        for (std::size_t row = 0U; row < range.Count(); ++row) {
            const auto transform = range.Get(row);
            auto position = transform.localPosition;
            position.x += 0.25F;
            range.SetLocal(row, position, transform.localRotation, transform.localScale);
        }
    });
    kb::tests::Require(stats.rowsVisited == scene.Entities().Count() && stats.rowsWritten == stats.rowsVisited,
        "Scratch root pass must write every live row, including tails");
}

void MoveRootsThroughSet(Scene& scene, std::span<const SceneObject> roots) {
    for (const auto object : roots) {
        auto transform = scene.Transforms().Get(object);
        transform.localPosition.x += 0.25F;
        scene.Transforms().Set(object, transform);
    }
}

void RunNativeRootsAndHierarchyTransitionTest(bool expectLazy) {
    Scene actual{SmallChunkConfig()}, reference{SmallChunkConfig()};
    // The nonzero budget forces the bounded algorithm without changing the
    // transform mathematics, and finishes this small fixture in one sync.
    reference.Runtime().SetTransformPropagationBudget({.maxInspectedEntitiesPerSync = 100000U});
    auto actualRoots = CreateRoots(actual, 1021U);
    auto referenceRoots = CreateRoots(reference, 1021U);
    actual.Runtime().SynchronizeTransforms();
    reference.Runtime().SynchronizeTransforms();
    RequireSameTransforms(actual, actualRoots, reference, referenceRoots);
    PrintFootprint("fresh_native_roots", actual);
    RequireUnusedRootScratch(actual, expectLazy);

    MoveRootsThroughPass(actual);
    MoveRootsThroughSet(reference, referenceRoots);
    static_cast<void>(actual.Runtime().Update(0.016F));
    static_cast<void>(reference.Runtime().Update(0.016F));
    RequireSameTransforms(actual, actualRoots, reference, referenceRoots);
    RequireUnusedRootScratch(actual, expectLazy);

    auto actualAppended = CreateRoots(actual, 333U, actualRoots.size());
    auto referenceAppended = CreateRoots(reference, 333U, referenceRoots.size());
    actualRoots.insert(actualRoots.end(), actualAppended.begin(), actualAppended.end());
    referenceRoots.insert(referenceRoots.end(), referenceAppended.begin(), referenceAppended.end());
    actual.Runtime().SynchronizeTransforms();
    reference.Runtime().SynchronizeTransforms();
    RequireSameTransforms(actual, actualRoots, reference, referenceRoots);
    PrintFootprint("append333_native_roots", actual);
    RequireUnusedRootScratch(actual, expectLazy);

    // First generic use starts with no dense scratch in the candidate. The
    // second link adds another depth level and tests parent scratch reads.
    for (auto* scene : {&actual, &reference}) {
        const auto& roots = scene == &actual ? actualRoots : referenceRoots;
        kb::tests::Require(scene->Hierarchy().SetParent(roots[1], roots[0]) && scene->Hierarchy().SetParent(roots[2], roots[1]),
            "Scratch fixture could not form its first hierarchy");
        auto parent = scene->Transforms().Get(roots[0]);
        parent.localPosition = {10.0F, -2.0F, 4.0F};
        parent.localScale = {1.5F, 2.0F, 0.75F};
        scene->Transforms().Set(roots[0], parent);
    }
    actual.Runtime().SynchronizeTransforms();
    reference.Runtime().SynchronizeTransforms();
    RequirePreparedDenseScratch(actual);
    RequireSameTransforms(actual, actualRoots, reference, referenceRoots);
    PrintFootprint("first_hierarchy", actual);
    const auto allocated = Footprint(actual);
    const auto child = actual.Transforms().Get(actualRoots[1]);
    const auto grandchild = actual.Transforms().Get(actualRoots[2]);
    kb::tests::Require(!child.worldDirty && !grandchild.worldDirty &&
        child.parentVersion == actual.Transforms().Get(actualRoots[0]).worldVersion && grandchild.parentVersion == child.worldVersion,
        "First hierarchy use must publish clean children with their current parent versions");

    for (auto* scene : {&actual, &reference}) {
        const auto& roots = scene == &actual ? actualRoots : referenceRoots;
        kb::tests::Require(scene->Hierarchy().SetParent(roots[2], SceneObject{}) && scene->Hierarchy().SetParent(roots[1], SceneObject{}),
            "Scratch fixture could not return to roots");
    }
    actual.Runtime().SynchronizeTransforms();
    reference.Runtime().SynchronizeTransforms();
    RequireSameTransforms(actual, actualRoots, reference, referenceRoots);
    const auto retained = Footprint(actual);
    kb::tests::Require(retained.worldCapacity >= allocated.worldCapacity && retained.validCapacity >= allocated.validCapacity &&
        retained.dirtyCapacity >= allocated.dirtyCapacity, "Returning to native roots must retain previously allocated scratch capacity");
    PrintFootprint("roots_after_hierarchy_retained", actual);
}

void RunFirstBudgetAndResumeTest(bool expectLazy) {
    Scene actual{SmallChunkConfig()}, reference{SmallChunkConfig()};
    reference.Runtime().SetTransformPropagationBudget({.maxInspectedEntitiesPerSync = 100000U});
    auto actualRoots = CreateRoots(actual, 7U);
    auto referenceRoots = CreateRoots(reference, 7U);
    actual.Runtime().SynchronizeTransforms();
    reference.Runtime().SynchronizeTransforms();
    RequireUnusedRootScratch(actual, expectLazy);
    for (auto* scene : {&actual, &reference}) {
        const auto& roots = scene == &actual ? actualRoots : referenceRoots;
        for (const auto object : roots) {
            auto* transform = scene->Transforms().TryGet(object.Entity());
            transform->localPosition.x += 5.0F;
            scene->Transforms().MarkModified(object.Entity());
        }
        scene->Runtime().SetTransformPropagationBudget({.maxInspectedEntitiesPerSync = 2U});
    }
    actual.Runtime().SynchronizeTransforms();
    reference.Runtime().SynchronizeTransforms();
    RequirePreparedDenseScratch(actual);
    kb::tests::Require(actual.Runtime().HotPathReport().transformHierarchyBudgetExhausted &&
        actual.Runtime().HotPathReport().transformHierarchyInspectedCount <= 2U,
        "First budgeted root sync must stop at its inspection budget");
    RequireSameTransforms(actual, actualRoots, reference, referenceRoots);
    PrintFootprint("first_budget_slice", actual);

    // Reset the topology during a suspended slice; subsequent slices must
    // initialize scratch and prewarm completed parent levels correctly.
    kb::tests::Require(actual.Hierarchy().SetParent(actualRoots[6], actualRoots[0]) &&
        reference.Hierarchy().SetParent(referenceRoots[6], referenceRoots[0]), "Budget scratch fixture could not reparent");
    reference.Runtime().SetTransformPropagationBudget({.maxInspectedEntitiesPerSync = 100000U});
    reference.Runtime().SynchronizeTransforms();
    bool completed = false;
    for (std::size_t slice = 0U; slice < actualRoots.size() + 2U; ++slice) {
        actual.Runtime().SynchronizeTransforms();
        kb::tests::Require(actual.Runtime().HotPathReport().transformHierarchyInspectedCount <= 2U,
            "Resumed scratch propagation exceeded its inspection budget");
        if (!actual.Runtime().HotPathReport().transformHierarchyBudgetExhausted) {
            completed = true;
            break;
        }
    }
    kb::tests::Require(completed, "Budget scratch propagation did not finish after topology reset");
    RequireSameTransforms(actual, actualRoots, reference, referenceRoots);
    PrintFootprint("budget_resumed_hierarchy", actual);
}

void RunScratchEpochWrapTest(bool expectLazy) {
    Scene actual{SmallChunkConfig()}, reference{SmallChunkConfig()};
    reference.Runtime().SetTransformPropagationBudget({.maxInspectedEntitiesPerSync = 100000U});
    auto actualRoots = CreateRoots(actual, 4U);
    auto referenceRoots = CreateRoots(reference, 4U);
    actual.Runtime().SynchronizeTransforms();
    reference.Runtime().SynchronizeTransforms();
    RequireUnusedRootScratch(actual, expectLazy);
    // Keep a dense hole which no live row can overwrite during propagation.
    // A missing wrap clear must remain observable in that untouched slot.
    const auto hole = kb::ecs::GeneratedEntityIndex(actualRoots.back().Entity());
    actual.Entities().Destroy(actualRoots.back());
    reference.Entities().Destroy(referenceRoots.back());
    actualRoots.pop_back();
    referenceRoots.pop_back();
    actual.Runtime().SynchronizeTransforms();
    reference.Runtime().SynchronizeTransforms();
    auto& state = kb::scene::SceneAccess::State(actual);
    const auto slots = state.denseHierarchyParents.size();
    kb::tests::Require(hole != kb::ecs::kInvalidGeneratedEntityIndex && hole < slots,
        "Epoch wrap fixture must retain a destroyed root's dense hole");
    TransformComponent stale;
    stale.worldPosition = {100000.0F, -100000.0F, 100000.0F};
    stale.worldVersion = 100000U;
    state.denseTransformWorldScratch.assign(slots, stale);
    state.denseTransformWorldScratchValid.assign(slots, 1U);
    state.denseTransformDirtyScratch.assign(slots, 1U);
    state.denseTransformWorldScratchEpoch = std::numeric_limits<std::uint32_t>::max();
    // A native-only sync may leave epoch untouched. It must not use these
    // stale entries, and the first generic sync must invalidate epoch 1.
    actual.Runtime().SynchronizeTransforms();
    reference.Runtime().SynchronizeTransforms();
    RequireSameTransforms(actual, actualRoots, reference, referenceRoots);
    const auto epochBeforeGeneric = state.denseTransformWorldScratchEpoch;
    kb::tests::Require(actual.Hierarchy().SetParent(actualRoots[1], actualRoots[0]) &&
        reference.Hierarchy().SetParent(referenceRoots[1], referenceRoots[0]), "Epoch scratch fixture could not form a hierarchy");
    actual.Runtime().SynchronizeTransforms();
    reference.Runtime().SynchronizeTransforms();
    RequireSameTransforms(actual, actualRoots, reference, referenceRoots);
    kb::tests::Require(state.denseTransformWorldScratchEpoch ==
        (epochBeforeGeneric == std::numeric_limits<std::uint32_t>::max() ? 1U : epochBeforeGeneric + 1U),
        "First generic sync must advance or wrap the scratch epoch");
    kb::tests::Require(state.denseTransformWorldScratchValid[hole] == 0U,
        "Scratch epoch wrap must clear the poisoned epoch-1 marker in an untouched dense hole");
    PrintFootprint("epoch_wrap_then_hierarchy", actual);
}

[[nodiscard]] SceneObject AdoptNativeExternalObject(Scene& scene, SceneEntity entity, SceneEntity parent, float x) {
    auto& state = kb::scene::SceneAccess::State(scene);
    const auto denseIndex = kb::ecs::GeneratedEntityIndex(entity);
    kb::tests::Require(denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex >= 64U && denseIndex < 256U,
        "Adopted fixture IDs must be valid native handles with bounded scene dense indices");
    // World adoption is private; this fixture intentionally exercises the
    // existing native-storage adoption API with backend mirroring disabled.
    // Native adoption requires packed index >= the generated-ID base. The
    // external handle uses native sparse lookup, but scene caches still use
    // its bounded GeneratedEntityIndex; scene-sparse IDs cannot be adopted.
    auto& storage = const_cast<kb::ecs::NativeArchetypeStorage&>(state.world.NativeStorage());
    storage.AdoptEntity(entity);
    kb::tests::Require(storage.IsAlive(entity) && storage.ResolveAliveEntity(entity.Id() & 0xFFFFFFFFULL) == entity &&
        denseIndex >= storage.Stats().liveEntities,
        "Native storage must resolve the sparse external handle and its generation without a dense record at its packed index");
    state.componentStorage.SetDefaults(entity, {.localPosition = {x, 0.0F, 0.0F}}, {});
    kb::scene::SceneHierarchyCache::AssignOrder(state, entity);
    kb::scene::SceneHierarchyCache::Add(state, entity, parent);
    return kb::scene::SceneAccess::MakeObject(scene, entity);
}

void RunMixedNativeExternalScratchTest(bool expectLazy) {
    auto config = SmallChunkConfig();
    config.mirrorEntitiesToBackend = false;
    config.mirrorNativeComponentChangesToBackend = false;
    Scene actual{config}, reference{config};
    reference.Runtime().SetTransformPropagationBudget({.maxInspectedEntitiesPerSync = 100000U});
    std::vector<SceneObject> actualObjects{actual.Entities().CreateObject({.transform = {.localPosition = {10.0F, 0.0F, 0.0F}}})};
    std::vector<SceneObject> referenceObjects{reference.Entities().CreateObject({.transform = {.localPosition = {10.0F, 0.0F, 0.0F}}})};
    actual.Runtime().SynchronizeTransforms();
    reference.Runtime().SynchronizeTransforms();
    RequireUnusedRootScratch(actual, expectLazy);
    for (auto* scene : {&actual, &reference}) {
        auto& objects = scene == &actual ? actualObjects : referenceObjects;
        objects.push_back(AdoptNativeExternalObject(*scene,
            SceneEntity{(7ULL << 32U) | (kb::ecs::kGeneratedEntityIndexBase + 64U)}, objects[0].Entity(), 2.0F));
        objects.push_back(AdoptNativeExternalObject(*scene,
            SceneEntity{(11ULL << 32U) | (kb::ecs::kGeneratedEntityIndexBase + 127U)}, objects[1].Entity(), 3.0F));
        objects.push_back(scene->Entities().CreateObject({.parent = objects[2], .transform = {.localPosition = {4.0F, 0.0F, 0.0F}}}));
    }
    actual.Runtime().SynchronizeTransforms();
    reference.Runtime().SynchronizeTransforms();
    RequireSameTransforms(actual, actualObjects, reference, referenceObjects);
    RequirePreparedDenseScratch(actual);
    const auto& state = kb::scene::SceneAccess::State(actual);
    kb::tests::Require(state.denseHierarchyParents.size() == 128U &&
        state.denseTransformWorldScratchValid[64U] == state.denseTransformWorldScratchEpoch &&
        state.denseTransformWorldScratchValid[127U] == state.denseTransformWorldScratchEpoch,
        "Generic propagation must prepare bounded scratch slots for externally adopted parents");
    kb::tests::Require(actual.Transforms().Get(actualObjects.back()).worldPosition.x == 19.0F,
        "Generated child did not compose through its externally adopted ancestors");
    for (auto* scene : {&actual, &reference}) {
        const auto& objects = scene == &actual ? actualObjects : referenceObjects;
        auto transform = scene->Transforms().Get(objects[0]);
        transform.localPosition.x = 20.0F;
        scene->Transforms().Set(objects[0], transform);
    }
    actual.Runtime().SynchronizeTransforms();
    reference.Runtime().SynchronizeTransforms();
    RequireSameTransforms(actual, actualObjects, reference, referenceObjects);
    kb::tests::Require(actual.Transforms().Get(actualObjects.back()).worldPosition.x == 29.0F,
        "Scratch did not refresh an externally adopted chain after its generated ancestor moved");
    PrintFootprint("mixed_native_external_adopted", actual);
}

void RunTransformQueueUnequalSizesTest(bool epochsLong, bool wrapEpoch) {
    Scene actual{SmallChunkConfig()};
    Scene reference{SmallChunkConfig()};
    std::vector<SceneObject> actualObjects;
    std::vector<SceneObject> referenceObjects;
    SceneEntity previousChild;
    for (auto* scene : {&actual, &reference}) {
        auto& objects = scene == &actual ? actualObjects : referenceObjects;
        objects.push_back(scene->Entities().CreateObject({
            .transform = TransformComponent{.localPosition = {10.0F, 0.0F, 0.0F}},
        }));
        objects.push_back(scene->Entities().CreateObject({
            .parent = objects.front(),
            .transform = TransformComponent{.localPosition = {2.0F, 3.0F, 4.0F}},
        }));
        scene->Runtime().SynchronizeTransforms();
        const auto oldChild = objects.back().Entity();
        scene->Entities().Destroy(objects.back());
        objects.back() = scene->Entities().CreateObject({
            .parent = objects.front(),
            .transform = TransformComponent{.localPosition = {2.0F, 3.0F, 4.0F}},
        });
        kb::tests::Require(kb::ecs::GeneratedEntityIndex(oldChild) ==
            kb::ecs::GeneratedEntityIndex(objects.back().Entity()) && oldChild != objects.back().Entity(),
            "Queue test must retain the recycled slot with a different full entity ID");
        if (scene == &actual) previousChild = oldChild;
        scene->Runtime().SynchronizeTransforms();
        scene->Transforms().Set(objects.front(), scene->Transforms().Get(objects.front()));
        scene->Runtime().SynchronizeTransforms();
    }

    auto& state = kb::scene::SceneAccess::State(actual);
    const auto child = actualObjects.back().Entity();
    const auto childIndex = kb::ecs::GeneratedEntityIndex(child);
    const std::size_t requiredSize = static_cast<std::size_t>(childIndex) + 1U;
    const std::size_t retainedSize = requiredSize + 3U;
    // Both are valid vector states. Growing either vector must preserve the
    // retained prefix of the other, including the reverse length relationship.
    kb::scene::ClearSceneTransformDirtyFrontier(state);
    state.transformDirtyFrontierDenseMarkEpochs.assign(epochsLong ? retainedSize : 0U, 0U);
    state.transformDirtyFrontierDenseMarkedEntities.assign(epochsLong ? 0U : retainedSize, SceneEntity{});
    state.transformValueCacheLoadDenseMarkEpochs.assign(epochsLong ? retainedSize : 0U, 0U);
    state.transformValueCacheLoadDenseMarkedEntities.assign(epochsLong ? 0U : retainedSize, SceneEntity{});
    constexpr std::uint32_t retainedEpoch = 0x13579U;
    if (epochsLong) {
        state.transformDirtyFrontierDenseMarkEpochs.back() = retainedEpoch;
        state.transformValueCacheLoadDenseMarkEpochs.back() = retainedEpoch;
    } else {
        state.transformDirtyFrontierDenseMarkedEntities.back() = previousChild;
        state.transformValueCacheLoadDenseMarkedEntities.back() = previousChild;
    }
    if (wrapEpoch) {
        state.transformDirtyFrontierMarkEpoch = std::numeric_limits<std::uint32_t>::max();
        state.transformValueCacheLoadMarkEpoch = std::numeric_limits<std::uint32_t>::max();
    }

    for (auto* scene : {&actual, &reference}) {
        const auto& objects = scene == &actual ? actualObjects : referenceObjects;
        auto transform = scene->Transforms().Get(objects.back());
        transform.localPosition = {7.0F, 8.0F, 9.0F};
        scene->Transforms().Set(objects.back(), transform);
        scene->Transforms().Set(objects.back(), transform);
    }
    kb::tests::Require(state.transformDirtyFrontierDenseMarkEpochs.size() >= requiredSize &&
        state.transformDirtyFrontierDenseMarkedEntities.size() >= requiredSize &&
        state.transformDirtyFrontierDenseMarkedEntities[childIndex] == child &&
        state.transformDirtyFrontierDenseMarkEpochs[childIndex] == state.transformDirtyFrontierMarkEpoch &&
        std::ranges::count(state.transformDirtyFrontierEntities, child) == 1,
        "Dirty queue must grow each mark vector and deduplicate the full entity ID");
    kb::tests::Require(epochsLong ?
        state.transformDirtyFrontierDenseMarkEpochs.size() == retainedSize &&
            state.transformDirtyFrontierDenseMarkEpochs.back() == retainedEpoch :
        state.transformDirtyFrontierDenseMarkedEntities.size() == retainedSize &&
            state.transformDirtyFrontierDenseMarkedEntities.back() == previousChild,
        "Growing dirty queue marks must preserve the other vector's retained prefix");

    actual.Runtime().SynchronizeTransforms();
    reference.Runtime().SynchronizeTransforms();
    RequireSameTransforms(actual, actualObjects, reference, referenceObjects);
    kb::tests::Require(actual.Transforms().Get(actualObjects.back()).worldPosition.x == 17.0F,
        "Queue growth must preserve the child's normal parent composition");
    kb::tests::Require(state.transformValueCacheLoadDenseMarkEpochs.size() >= requiredSize &&
        state.transformValueCacheLoadDenseMarkedEntities.size() >= requiredSize &&
        state.transformValueCacheLoadDenseMarkedEntities[childIndex] == child &&
        state.transformValueCacheLoadDenseMarkEpochs[childIndex] == state.transformValueCacheLoadMarkEpoch &&
        std::ranges::count(state.transformValueCacheLoadEntitiesScratch, child) == 1,
        "Cache load queue must grow each mark vector and retain the current full ID");
    kb::tests::Require(epochsLong ?
        state.transformValueCacheLoadDenseMarkEpochs.size() == retainedSize &&
            state.transformValueCacheLoadDenseMarkEpochs.back() == (wrapEpoch ? 0U : retainedEpoch) :
        state.transformValueCacheLoadDenseMarkedEntities.size() == retainedSize &&
            state.transformValueCacheLoadDenseMarkedEntities.back() == previousChild,
        "Growing cache queue marks must preserve its retained prefix except epoch reset");
    if (wrapEpoch) {
        kb::tests::Require(state.transformDirtyFrontierMarkEpoch == 1U &&
            state.transformValueCacheLoadMarkEpoch == 1U,
            "Both transform queue epochs must retain their rollover behavior");
    }
    const auto actualComponent = actual.Runtime().EcsWorld().Component<TransformComponent>();
    const auto referenceComponent = reference.Runtime().EcsWorld().Component<TransformComponent>();
    kb::tests::Require(actual.Runtime().EcsWorld().NativeStorage().ComponentVersion(child, actualComponent) ==
        reference.Runtime().EcsWorld().NativeStorage().ComponentVersion(referenceObjects.back().Entity(), referenceComponent),
        "Queue growth must preserve the normal native component version");
}

} // namespace

namespace kb::tests {

void RunSceneTransformScratchTests() {
    const bool expectLazy = ExpectLazyScratch();
    std::cout << "transform_scratch expect_lazy=" << expectLazy << std::endl;
    std::cout << "START scratch native roots and hierarchy transition" << std::endl;
    RunNativeRootsAndHierarchyTransitionTest(expectLazy);
    std::cout << "START scratch first budget and resume" << std::endl;
    RunFirstBudgetAndResumeTest(expectLazy);
    std::cout << "START scratch epoch wrap" << std::endl;
    RunScratchEpochWrapTest(expectLazy);
    std::cout << "START scratch mixed native external adoption" << std::endl;
    RunMixedNativeExternalScratchTest(expectLazy);
    std::cout << "START scratch independent transform queue sizes" << std::endl;
    for (const bool epochsLong : {false, true}) {
        for (const bool wrapEpoch : {false, true}) RunTransformQueueUnequalSizesTest(epochsLong, wrapEpoch);
    }
    std::cout << "PASS scratch independent transform queue sizes cases=4" << std::endl;
}

} // namespace kb::tests

#if defined(KB_SCENE_TRANSFORM_SCRATCH_STANDALONE)
int main() {
    std::cout << std::unitbuf;
    try {
        kb::tests::RunSceneTransformScratchTests();
        std::cout << "Scene transform scratch tests passed" << std::endl;
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL scene transform scratch: " << exception.what() << std::endl;
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "FAIL scene transform scratch: unknown exception" << std::endl;
        return EXIT_FAILURE;
    }
}
#endif
