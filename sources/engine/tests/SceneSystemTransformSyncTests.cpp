#include "SceneSystemTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <memory>

namespace {

struct SceneSystemCounters {
    int created = 0;
    int updated = 0;
    int fixedUpdated = 0;
    int destroyed = 0;
};

class MoveEntitySceneSystem final : public kb::scene::SceneSystem {
public:
    MoveEntitySceneSystem(SceneSystemCounters& counters, kb::scene::SceneEntity entity, kb::scene::Vec3 targetPosition) noexcept
        : counters_(counters)
        , entity_(entity)
        , targetPosition_(targetPosition) {}

    void OnCreate(kb::scene::SceneSystemContext& context) override {
        static_cast<void>(context);
        ++counters_.created;
    }

    void OnUpdate(kb::scene::SceneSystemContext& context) override {
        ++counters_.updated;
        kb::scene::TransformComponent transform = context.Transforms().Get(entity_);
        transform.localPosition = targetPosition_;
        context.Transforms().Set(entity_, transform);
    }

    void OnDestroy(kb::scene::SceneSystemContext& context) override {
        static_cast<void>(context);
        ++counters_.destroyed;
    }

private:
    SceneSystemCounters& counters_;
    kb::scene::SceneEntity entity_{};
    kb::scene::Vec3 targetPosition_{};
};

class CountingFixedSceneSystem final : public kb::scene::SceneSystem {
public:
    explicit CountingFixedSceneSystem(SceneSystemCounters& counters) noexcept
        : counters_(counters) {}

    void OnUpdate(kb::scene::SceneSystemContext& context) override {
        ++counters_.updated;
        lastVariableDeltaSeconds_ = context.DeltaSeconds();
    }

    void OnFixedUpdate(kb::scene::SceneSystemContext& context) override {
        ++counters_.fixedUpdated;
        lastFixedDeltaSeconds_ = context.DeltaSeconds();
    }

    [[nodiscard]] float LastVariableDeltaSeconds() const noexcept { return lastVariableDeltaSeconds_; }
    [[nodiscard]] float LastFixedDeltaSeconds() const noexcept { return lastFixedDeltaSeconds_; }

private:
    SceneSystemCounters& counters_;
    float lastVariableDeltaSeconds_ = 0.0F;
    float lastFixedDeltaSeconds_ = 0.0F;
};

void RunSceneSystemTransformSyncTest() {
    SceneSystemCounters counters;

    {
        kb::scene::Scene scene;

        kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Parent",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F },
            },
        });

        kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Child",
            .parent = parent,
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ 2.0F, 0.0F, 0.0F },
            },
        });

        scene.Runtime().AddSceneSystem(std::make_unique<MoveEntitySceneSystem>(counters, parent.Entity(), kb::scene::Vec3{ 10.0F, 0.0F, 0.0F }));
        kb::tests::Require(counters.created == 1, "Scene system OnCreate was not called");

        [[maybe_unused]] const bool progressed = scene.Runtime().Update(0.016F);
        kb::tests::Require(counters.updated == 1, "Scene system OnUpdate was not called");

        const kb::scene::TransformComponent childTransform = scene.Transforms().Get(child);
        kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.x, 12.0F), "Scene system transform changes were not synchronized in the same update");
    }

    kb::tests::Require(counters.destroyed == 1, "Scene system OnDestroy was not called");
}

void RunSceneRuntimeFixedStepTest() {
    SceneSystemCounters counters;
    kb::scene::Scene scene;
    scene.Runtime().SetFixedStepSettings(kb::scene::SceneRuntimeFixedStepSettings{
        .fixedDeltaSeconds = 0.02F,
        .maxFrameDeltaSeconds = 0.25F,
        .maxFixedStepsPerFrame = 4U,
    });

    auto system = std::make_unique<CountingFixedSceneSystem>(counters);
    CountingFixedSceneSystem* systemView = system.get();
    scene.Runtime().AddSceneSystem(std::move(system));

    static_cast<void>(scene.Runtime().Update(0.01F));
    kb::tests::Require(counters.updated == 1, "Variable scene update should run once even before a fixed step is due");
    kb::tests::Require(counters.fixedUpdated == 0, "Fixed scene update should wait until the accumulator reaches fixed dt");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Runtime().FixedInterpolationAlpha(), 0.5F), "Fixed interpolation alpha should expose the partial accumulator");

    static_cast<void>(scene.Runtime().Update(0.03F));
    kb::tests::Require(counters.updated == 2, "Variable scene update should run once per frame");
    kb::tests::Require(counters.fixedUpdated == 2, "Fixed scene update should consume accumulated fixed steps");
    kb::tests::Require(scene.Runtime().LastFixedStepCount() == 2U, "Runtime should report fixed steps from the last frame");
    kb::tests::Require(kb::tests::NearlyEqual(systemView->LastVariableDeltaSeconds(), 0.03F), "Variable scene update should receive frame dt");
    kb::tests::Require(kb::tests::NearlyEqual(systemView->LastFixedDeltaSeconds(), 0.02F), "Fixed scene update should receive fixed dt");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Runtime().FixedInterpolationAlpha(), 0.0F), "Fixed interpolation alpha should reset after exact fixed consumption");

    static_cast<void>(scene.Runtime().Update(1.0F));
    kb::tests::Require(scene.Runtime().LastFixedStepCount() == 4U, "Runtime should cap fixed steps per frame");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Runtime().FixedInterpolationAlpha(), 0.0F), "Runtime should drop excess fixed time after max-step safety triggers");
}

} // namespace

namespace kb::tests {

void RunSceneSystemTransformSyncTests() {
    RunSceneSystemTransformSyncTest();
    RunSceneRuntimeFixedStepTest();
}

} // namespace kb::tests
