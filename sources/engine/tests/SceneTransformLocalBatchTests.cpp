#include "TestSupport.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using kb::scene::RowLocalTRS;
using kb::scene::Scene;
using kb::scene::SceneObject;
using kb::scene::TransformComponent;

constexpr std::size_t kRoots = 1003U;

[[nodiscard]] std::vector<SceneObject> CreateRoots(Scene& scene) {
    std::vector<kb::scene::SceneObjectDesc> descriptions(kRoots);
    for (std::size_t index = 0U; index < kRoots; ++index) {
        descriptions[index].transform.localPosition = { static_cast<float>(index), 0.0F, 0.0F };
    }
    return scene.Entities().CreateObjects(descriptions);
}

// The key is the root's creation index, kept in localPosition.x until the first write.
// Rotation cases cover the packed path (ordinary, identity) and the scalar fallbacks (zero, tiny, huge components).
[[nodiscard]] RowLocalTRS ValueFor(std::size_t index, std::size_t pass) {
    const float i = static_cast<float>(index);
    const float p = static_cast<float>(pass);
    RowLocalTRS value{ { i, 1.0F + p, -i }, { 0.1F, 0.7F, 0.2F, 0.9F }, { 1.0F + static_cast<float>(index % 7U), 2.0F, 3.0F } };
    switch (index % 6U) {
    case 1U: value.rotation = { 0.0F, 0.0F, 0.0F, 1.0F }; break;
    case 2U: value.rotation = { 0.0F, 0.0F, 0.0F, 0.0F }; break;
    case 3U: value.rotation = { 1.0e-20F, 0.0F, 0.0F, 1.0e-20F }; break;
    case 4U: value.rotation = { 1.0e20F, 2.0e20F, 0.0F, 1.0F }; break;
    default: break;
    }
    return value;
}

template <typename T>
[[nodiscard]] bool SameBits(const T& left, const T& right) noexcept {
    return std::memcmp(&left, &right, sizeof(T)) == 0;
}

// Position.x of every written row is its creation index, so the same values land on the same root in both scenes.
void RunPass(Scene& scene, std::size_t grain, std::size_t pass, bool batched) {
    const auto stats = scene.Transforms().ParallelForEachRoot<>(grain, [pass, batched](kb::scene::TransformRowRange& range) {
        std::vector<RowLocalTRS> values(range.Count());
        for (std::size_t row = 0U; row < range.Count(); ++row) {
            values[row] = ValueFor(static_cast<std::size_t>(range.Get(row).localPosition.x), pass);
        }
        if (batched) {
            // An odd split: groups of four then start at every alignment, and the tail is scalar.
            const std::size_t head = range.Count() > 1U ? 1U + (range.Count() % 3U) : range.Count();
            range.SetLocalBatch(0U, std::span<const RowLocalTRS>{ values }.first(head));
            range.SetLocalBatch(head, std::span<const RowLocalTRS>{ values }.subspan(head));
        } else {
            for (std::size_t row = 0U; row < range.Count(); ++row) {
                range.SetLocal(row, values[row].position, values[row].rotation, values[row].scale);
            }
        }
    });
    kb::tests::Require(stats.rowsWritten == kRoots, "A local batch pass must report every row it wrote");
}

void RequireSameTransforms(Scene& actual, const std::vector<SceneObject>& actualRoots, Scene& reference,
    const std::vector<SceneObject>& referenceRoots, const char* message) {
    for (std::size_t index = 0U; index < actualRoots.size(); ++index) {
        const auto left = actual.Transforms().Get(actualRoots[index]);
        const auto right = reference.Transforms().Get(referenceRoots[index]);
        kb::tests::Require(SameBits(left.localPosition, right.localPosition) && SameBits(left.localRotation, right.localRotation) &&
                SameBits(left.localScale, right.localScale) && SameBits(left.worldPosition, right.worldPosition) &&
                SameBits(left.worldRotation, right.worldRotation) && SameBits(left.worldScale, right.worldScale) &&
                left.worldDirty == right.worldDirty && left.parentVersion == right.parentVersion &&
                left.localVersion == right.localVersion && left.worldVersion == right.worldVersion,
            message);
    }
}

void RunBatchMatchesSetLocalTest() {
    Scene actual;
    Scene reference;
    const auto actualRoots = CreateRoots(actual);
    const auto referenceRoots = CreateRoots(reference);
    actual.Runtime().SynchronizeTransforms();
    reference.Runtime().SynchronizeTransforms();

    std::vector<std::uint32_t> previousLocal(kRoots);
    std::vector<std::uint32_t> previousWorld(kRoots);
    for (std::size_t pass = 0U; pass < 3U; ++pass) {
        for (std::size_t index = 0U; index < kRoots; ++index) {
            const auto row = actual.Transforms().Get(actualRoots[index]);
            previousLocal[index] = row.localVersion;
            previousWorld[index] = row.worldVersion;
        }
        // Grains of 37 and 4 give chunk ranges of many lengths and group alignments.
        const std::size_t grain = pass == 1U ? 4U : 37U;
        RunPass(actual, grain, pass, true);
        RunPass(reference, grain, pass, false);
        static_cast<void>(actual.Runtime().Update(0.016F));
        static_cast<void>(reference.Runtime().Update(0.016F));
        RequireSameTransforms(actual, actualRoots, reference, referenceRoots, "A local batch must give SetLocal's local and world rows");
        for (std::size_t index = 0U; index < kRoots; ++index) {
            const auto row = actual.Transforms().Get(actualRoots[index]);
            kb::tests::Require(row.localVersion != previousLocal[index] && row.worldVersion != previousWorld[index],
                "A batch write must change both versions of every row it writes");
        }
    }
}

void RunBatchWithHierarchyTest() {
    Scene actual;
    Scene reference;
    const auto actualRoots = CreateRoots(actual);
    const auto referenceRoots = CreateRoots(reference);
    for (std::size_t child = 1U; child < 40U; child += 3U) {
        kb::tests::Require(actual.Hierarchy().SetParent(actualRoots[child], actualRoots[child - 1U]) &&
                reference.Hierarchy().SetParent(referenceRoots[child], referenceRoots[child - 1U]),
            "Batch hierarchy fixture could not parent");
    }
    actual.Runtime().SynchronizeTransforms();
    reference.Runtime().SynchronizeTransforms();
    RunPass(actual, 29U, 0U, true);
    RunPass(reference, 29U, 0U, false);
    static_cast<void>(actual.Runtime().Update(0.016F));
    static_cast<void>(reference.Runtime().Update(0.016F));
    RequireSameTransforms(actual, actualRoots, reference, referenceRoots, "Linked rows written by a batch must settle like SetLocal's");
}

// Many short passes on the worker pool: streaming stores of one pass must be visible to the readers after it
// (no tool for data races is available on this toolchain, so this is a repetition check, not a proof).
void RunBatchRepeatedPassesTest() {
    Scene actual;
    Scene reference;
    const auto actualRoots = CreateRoots(actual);
    const auto referenceRoots = CreateRoots(reference);
    actual.Runtime().SynchronizeTransforms();
    reference.Runtime().SynchronizeTransforms();
    for (std::size_t pass = 0U; pass < 150U; ++pass) {
        const std::size_t grain = 1U + (pass * 7U) % 61U;
        RunPass(actual, grain, pass, true);
        RunPass(reference, grain, pass, false);
        if (pass % 10U == 9U) {
            static_cast<void>(actual.Runtime().Update(0.016F));
            static_cast<void>(reference.Runtime().Update(0.016F));
        }
        RequireSameTransforms(actual, actualRoots, reference, referenceRoots, "Repeated batch passes must keep matching SetLocal");
    }
}

// A plain Set between two batch passes: the second pass must still give the row a world version no reader has seen,
// or the renderer, which pulls a row only when its world version changed, keeps showing the Set's position.
void RunBatchAfterSetTest() {
    Scene actual;
    Scene reference;
    const auto actualRoots = CreateRoots(actual);
    const auto referenceRoots = CreateRoots(reference);
    actual.Runtime().SynchronizeTransforms();
    reference.Runtime().SynchronizeTransforms();
    for (std::size_t pass = 0U; pass < 4U; ++pass) {
        RunPass(actual, 64U, pass, true);
        RunPass(reference, 64U, pass, false);
        std::vector<std::uint32_t> seen(kRoots);
        for (std::size_t index = 0U; index < kRoots; index += 2U) {
            auto transform = actual.Transforms().Get(actualRoots[index]);
            transform.localScale.y += 1.0F;
            actual.Transforms().Set(actualRoots[index], transform);
            reference.Transforms().Set(referenceRoots[index], transform);
        }
        for (std::size_t index = 0U; index < kRoots; ++index) seen[index] = actual.Transforms().Get(actualRoots[index]).worldVersion;
        RunPass(actual, 64U, pass + 1U, true);
        RunPass(reference, 64U, pass + 1U, false);
        RequireSameTransforms(actual, actualRoots, reference, referenceRoots, "A batch after Set must give SetLocal's rows and versions");
        for (std::size_t index = 0U; index < kRoots; ++index) {
            kb::tests::Require(actual.Transforms().Get(actualRoots[index]).worldVersion != seen[index],
                "A batch after Set must not repeat a world version a reader has already seen");
        }
    }
}

void RunBatchBoundsTest() {
    Scene scene;
    const auto roots = CreateRoots(scene);
    scene.Runtime().SynchronizeTransforms();
    std::vector<TransformComponent> before;
    for (const auto& root : roots) before.push_back(scene.Transforms().Get(root));

    bool threw = false;
    try {
        static_cast<void>(scene.Transforms().ParallelForEachRoot<>(64U, [](kb::scene::TransformRowRange& range) {
            const RowLocalTRS value{ { 9.0F, 9.0F, 9.0F }, { 0.0F, 0.0F, 0.0F, 1.0F }, { 1.0F, 1.0F, 1.0F } };
            range.SetLocalBatch(range.Count(), std::span<const RowLocalTRS>{});
            range.SetLocalBatch(0U, std::span<const RowLocalTRS>{});
            // One value past the end: rejected before any row of this call is written.
            const std::vector<RowLocalTRS> tooMany(range.Count() + 1U, value);
            range.SetLocalBatch(0U, tooMany);
        }));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    kb::tests::Require(threw, "A batch past the end of its range must throw out_of_range");
    for (std::size_t index = 0U; index < roots.size(); ++index) {
        const auto row = scene.Transforms().Get(roots[index]);
        kb::tests::Require(SameBits(row.localPosition, before[index].localPosition) && row.localVersion == before[index].localVersion,
            "A rejected batch must not write any row");
    }
}

} // namespace

namespace kb::tests {

void RunSceneTransformLocalBatchTests() {
    std::cout << "START local batch matches SetLocal" << std::endl;
    RunBatchMatchesSetLocalTest();
    std::cout << "START local batch with hierarchy" << std::endl;
    RunBatchWithHierarchyTest();
    std::cout << "START local batch repeated passes" << std::endl;
    RunBatchRepeatedPassesTest();
    std::cout << "START local batch after Set" << std::endl;
    RunBatchAfterSetTest();
    std::cout << "START local batch bounds" << std::endl;
    RunBatchBoundsTest();
    std::cout << "PASS local batch" << std::endl;
}

} // namespace kb::tests

#if defined(KB_SCENE_TRANSFORM_LOCAL_BATCH_STANDALONE)
int main() {
    std::cout << std::unitbuf;
    try {
        kb::tests::RunSceneTransformLocalBatchTests();
        std::cout << "Scene transform local batch tests passed" << std::endl;
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL scene transform local batch: " << exception.what() << std::endl;
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "FAIL scene transform local batch: unknown exception" << std::endl;
        return EXIT_FAILURE;
    }
}
#endif
