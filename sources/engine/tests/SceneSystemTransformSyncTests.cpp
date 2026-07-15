#include "SceneSystemTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/ecs/World.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/VisibilityComponent.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <array>
#include <span>
#include <memory>
#include <optional>
#include <vector>

namespace {

struct SceneSystemCounters {
    int created = 0;
    int updated = 0;
    int fixedUpdated = 0;
    int destroyed = 0;
};

struct MeshRendererProxyStats {
    std::size_t visited = 0U;
    bool sawVisibleMesh = false;
    bool sawHiddenMesh = false;
    float visibleTranslationX = 0.0F;
};

struct CameraProxyStats {
    std::size_t visited = 0U;
    bool sawPrimary = false;
    float translationX = 0.0F;
};

struct LightProxyStats {
    std::size_t visited = 0U;
    bool sawPoint = false;
    float translationX = 0.0F;
};

struct PhysicsBodyStats {
    std::size_t visited = 0U;
    bool sawDynamic = false;
    bool sawSphere = false;
    float localPositionX = 0.0F;
};

void AccumulateMeshRendererProxyVisit(
    kb::scene::SceneEntity entity,
    const kb::scene::WorldTransformAffine3x4& worldTransform,
    const kb::scene::MeshRendererComponent& renderer,
    void* context) {
    static_cast<void>(entity);
    auto& stats = *static_cast<MeshRendererProxyStats*>(context);
    ++stats.visited;
    if (renderer.meshAssetId == 42U) {
        stats.sawVisibleMesh = true;
        stats.visibleTranslationX = worldTransform.values[9];
    } else if (renderer.meshAssetId == 77U) {
        stats.sawHiddenMesh = true;
    }
}

void AccumulateCameraProxyVisit(
    kb::scene::SceneEntity entity,
    const kb::scene::WorldTransformAffine3x4& worldTransform,
    const kb::scene::CameraComponent& camera,
    void* context) {
    static_cast<void>(entity);
    auto& stats = *static_cast<CameraProxyStats*>(context);
    ++stats.visited;
    if (camera.primary) {
        stats.sawPrimary = true;
        stats.translationX = worldTransform.values[9];
    }
}

void AccumulateLightProxyVisit(
    kb::scene::SceneEntity entity,
    const kb::scene::WorldTransformAffine3x4& worldTransform,
    const kb::scene::LightComponent& light,
    void* context) {
    static_cast<void>(entity);
    auto& stats = *static_cast<LightProxyStats*>(context);
    ++stats.visited;
    if (light.kind == kb::scene::LightKind::Point) {
        stats.sawPoint = true;
        stats.translationX = worldTransform.values[9];
    }
}

void AccumulatePhysicsBodyVisit(
    kb::scene::SceneEntity entity,
    const kb::scene::TransformComponent& transform,
    const kb::scene::RigidbodyComponent& rigidbody,
    const kb::scene::ColliderComponent& collider,
    void* context) {
    static_cast<void>(entity);
    auto& stats = *static_cast<PhysicsBodyStats*>(context);
    ++stats.visited;
    stats.sawDynamic = stats.sawDynamic || rigidbody.bodyType == kb::scene::RigidbodyBodyType::Dynamic;
    stats.sawSphere = stats.sawSphere || collider.shape == kb::scene::ColliderShape::Sphere;
    stats.localPositionX = transform.localPosition.x;
}

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

    [[nodiscard]] bool RequiresFixedStep() const override { return true; }

    [[nodiscard]] float LastVariableDeltaSeconds() const noexcept { return lastVariableDeltaSeconds_; }
    [[nodiscard]] float LastFixedDeltaSeconds() const noexcept { return lastFixedDeltaSeconds_; }

private:
    SceneSystemCounters& counters_;
    float lastVariableDeltaSeconds_ = 0.0F;
    float lastFixedDeltaSeconds_ = 0.0F;
};

class FixedMoveSceneSystem final : public kb::scene::SceneSystem {
public:
    FixedMoveSceneSystem(kb::scene::SceneEntity entity, kb::scene::Vec3 targetPosition) noexcept
        : entity_(entity)
        , targetPosition_(targetPosition) {}

    void OnFixedUpdate(kb::scene::SceneSystemContext& context) override {
        kb::scene::TransformComponent transform = context.Transforms().Get(entity_);
        transform.localPosition = targetPosition_;
        context.Transforms().Set(entity_, transform);
    }

    [[nodiscard]] bool RequiresFixedStep() const override { return true; }

private:
    kb::scene::SceneEntity entity_{};
    kb::scene::Vec3 targetPosition_{};
};

// LIB-089: records `entity`'s world.x AT THE MOMENT OnUpdate() runs — used
// to prove/disprove whether a same-Update()-call, earlier scene system's
// transform change is visible mid-pass via world*, per the timing contract
// now documented on kb::scene::SceneRuntime::SynchronizeTransforms().
class ObserveWorldXSceneSystem final : public kb::scene::SceneSystem {
public:
    ObserveWorldXSceneSystem(kb::scene::SceneEntity entity, float& observedWorldX) noexcept
        : entity_(entity)
        , observedWorldX_(observedWorldX) {}

    void OnUpdate(kb::scene::SceneSystemContext& context) override {
        observedWorldX_ = context.Transforms().Get(entity_).worldPosition.x;
    }

private:
    kb::scene::SceneEntity entity_{};
    float& observedWorldX_;
};

// LIB-089: same as ObserveWorldXSceneSystem, but observing from
// OnFixedUpdate() (RequiresFixedStep()==true) instead of OnUpdate() — used
// to prove a fixed-step (physics-shaped) scene system DOES see fresh,
// synced world* data automatically, unlike a plain variable-update system.
class ObserveWorldXFixedSceneSystem final : public kb::scene::SceneSystem {
public:
    ObserveWorldXFixedSceneSystem(kb::scene::SceneEntity entity, float& observedWorldX) noexcept
        : entity_(entity)
        , observedWorldX_(observedWorldX) {}

    void OnFixedUpdate(kb::scene::SceneSystemContext& context) override {
        observedWorldX_ = context.Transforms().Get(entity_).worldPosition.x;
    }

    [[nodiscard]] bool RequiresFixedStep() const override { return true; }

private:
    kb::scene::SceneEntity entity_{};
    float& observedWorldX_;
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

// LIB-089: locks in the "scripts must self-sync across phases" half of the
// timing contract now documented on
// kb::scene::SceneRuntime::SynchronizeTransforms() — a transform change one
// scene system makes in OnUpdate() is NOT automatically visible via world*
// to a LATER scene system's OnUpdate() within the SAME Update() call
// (SceneSystemScheduler::Update only calls SynchronizeTransformHierarchy
// once, before the whole pass, and once more after the whole pass — never
// between two systems inside it), even though it genuinely IS visible once
// Update() itself returns (RunSceneSystemTransformSyncTest above already
// proves that half; this test contrasts the two).
void RunTransformSyncContractScriptsRequireExplicitSyncAcrossSystemsTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "ContractParent",
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F } },
    });
    const kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "ContractChild",
        .parent = parent,
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 2.0F, 0.0F, 0.0F } },
    });
    // Establish a baseline synced world position: parent(1) + child(2) = 3.
    static_cast<void>(scene.Runtime().Update(0.016F));
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(child).worldPosition.x, 3.0F), "Contract test fixture baseline world position must be 3 before the test proper");

    SceneSystemCounters counters;
    float observedChildWorldXDuringLaterSystem = -1.0F;
    // Registration order matters: MoveEntitySceneSystem must run BEFORE
    // ObserveWorldXSceneSystem within the same Update() call's scheduler
    // pass (SceneSystemScheduler::Update iterates systems_ in push_back
    // order).
    scene.Runtime().AddSceneSystem(std::make_unique<MoveEntitySceneSystem>(counters, parent.Entity(), kb::scene::Vec3{ 10.0F, 0.0F, 0.0F }));
    scene.Runtime().AddSceneSystem(std::make_unique<ObserveWorldXSceneSystem>(child.Entity(), observedChildWorldXDuringLaterSystem));

    static_cast<void>(scene.Runtime().Update(0.016F));

    kb::tests::Require(kb::tests::NearlyEqual(observedChildWorldXDuringLaterSystem, 3.0F),
        "LIB-089 contract: a same-pass, earlier scene system's transform change must NOT be visible via world* to a later scene system's OnUpdate() without an explicit SynchronizeTransforms() call");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(child).worldPosition.x, 12.0F),
        "LIB-089 contract: after Update() returns, world* must reflect every change made during that call, even ones a same-pass later system could not see mid-pass");
}

// LIB-089: locks in the "physics gets fresh data automatically" half of the
// timing contract — a fixed-step scene system (RequiresFixedStep()==true,
// the shape JoltPhysicsSceneSystem has) sees a transform change made by an
// EARLIER, plain variable-update scene system in the SAME Update() call,
// with no manual sync call of its own, because SceneRuntimeService::Update
// calls SynchronizeTransformHierarchy immediately before entering the
// fixed-step loop (in addition to before/after every individual step).
void RunTransformSyncContractFixedStepGetsFreshDataAutomaticallyTest() {
    kb::scene::Scene scene;
    scene.Runtime().SetFixedStepSettings(kb::scene::SceneRuntimeFixedStepSettings{
        .fixedDeltaSeconds = 0.02F,
        .maxFrameDeltaSeconds = 0.25F,
        .maxFixedStepsPerFrame = 4U,
    });
    const kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "FixedContractParent" });
    const kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "FixedContractChild",
        .parent = parent,
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 2.0F, 0.0F, 0.0F } },
    });

    SceneSystemCounters counters;
    float observedChildWorldXDuringFixedUpdate = -1.0F;
    scene.Runtime().AddSceneSystem(std::make_unique<MoveEntitySceneSystem>(counters, parent.Entity(), kb::scene::Vec3{ 10.0F, 0.0F, 0.0F }));
    scene.Runtime().AddSceneSystem(std::make_unique<ObserveWorldXFixedSceneSystem>(child.Entity(), observedChildWorldXDuringFixedUpdate));

    static_cast<void>(scene.Runtime().Update(0.03F)); // Exceeds fixedDeltaSeconds, so at least one fixed step runs.

    kb::tests::Require(kb::tests::NearlyEqual(observedChildWorldXDuringFixedUpdate, 12.0F),
        "LIB-089 contract: a fixed-step scene system must see fresh, synced world* data automatically, including a change from an earlier same-Update()-call variable-update system, with no manual SynchronizeTransforms() call of its own");
}

// LIB-128: a fake, physics-shaped stand-in (RequiresFixedStep()==true) for
// the real JoltPhysicsSceneSystem - a SceneSystemScheduler-level ordering
// fact is provable without a real Jolt plugin, the same way this file's
// other Observe*FixedSceneSystem fakes already do for LIB-089.
class RecordingFixedPhysicsStandIn final : public kb::scene::SceneSystem {
public:
    void OnFixedUpdate(kb::scene::SceneSystemContext& context) override {
        static_cast<void>(context);
        ++stepsRun;
        lastWrittenValue = stepsRun;
    }

    [[nodiscard]] bool RequiresFixedStep() const override { return true; }

    int stepsRun = 0;
    int lastWrittenValue = 0;
};

// LIB-128: the OPPOSITE direction from
// RunTransformSyncContractFixedStepGetsFreshDataAutomaticallyTest above - a
// real kb::script::ScriptRuntimeSceneSystem's FixedTick dispatch, within
// ONE Update() call, does NOT see that SAME call's physics fixed-step
// result, because SceneRuntimeService::Update (SceneRuntime.cpp) calls
// sceneSystemScheduler.Update() (where ScriptRuntimeSceneSystem::OnUpdate,
// including its own internal FixedTick dispatch loop, lives) BEFORE the
// scene's fixed-step loop (where a RequiresFixedStep()==true system's
// OnFixedUpdate - JoltPhysicsSceneSystem in a real project - executes).
// This asymmetry is exactly why LIB-014's Projectile template retries
// Physics.SetVelocity every Tick instead of calling it once in Ready: a
// freshly-spawned entity's live Jolt body is not created until its first
// fixed step, which cannot have run yet by the time the SAME call's
// Ready/FixedTick/Tick already did.
void RunFixedTickSeesPreviousFramePhysicsResultTest() {
    kb::scene::Scene scene;
    scene.Runtime().SetFixedStepSettings(kb::scene::SceneRuntimeFixedStepSettings{
        .fixedDeltaSeconds = 0.02F,
        .maxFrameDeltaSeconds = 0.25F,
        .maxFixedStepsPerFrame = 4U,
    });

    auto physicsStandIn = std::make_unique<RecordingFixedPhysicsStandIn>();
    RecordingFixedPhysicsStandIn* physicsView = physicsStandIn.get();
    scene.Runtime().AddSceneSystem(std::move(physicsStandIn));

    kb::script::ScriptRuntimeHost host{
        scene,
        kb::script::ScriptRuntimeHostOptions{
            .frameSettings = kb::script::ScriptRuntimeFrameSettings{ .fixedDeltaSeconds = 0.02F, .maxFixedStepsPerFrame = 4U },
        },
    };
    kb::tests::Require(host.Succeeded(), "LIB-128 timing contract test host did not initialize");

    constexpr kb::assets::AssetId kAsset{ 9700U };
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "FixedTickObserver" });
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
    });

    std::vector<int> observedDuringFixedTick;
    kb::tests::Require(host.NativeBackend().RegisterLifecycle(kAsset, kb::script::ScriptLifecycleEvent::FixedTick, [&observedDuringFixedTick, physicsView](kb::script::ScriptExecutionContext&) {
                           observedDuringFixedTick.push_back(physicsView->lastWrittenValue);
                       }),
        "LIB-128 timing contract test FixedTick registration failed");

    kb::tests::Require(host.InstallSceneSystem(), "LIB-128 timing contract test scene system install failed");

    // dt == fixedDeltaSeconds exactly, so every Update() call consumes
    // exactly one fixed step with zero accumulator remainder - no
    // carry-over arithmetic to reason about.
    static_cast<void>(scene.Runtime().Update(0.02F));
    kb::tests::Require(physicsView->stepsRun == 1, "LIB-128 timing contract test fixture: physics stand-in must have run exactly once");
    kb::tests::Require(observedDuringFixedTick.size() == 1U, "LIB-128 timing contract test fixture: FixedTick must have dispatched exactly once");
    kb::tests::Require(observedDuringFixedTick[0] == 0,
        "LIB-128 contract: FixedTick dispatched during an Update() call must NOT see that SAME call's own physics fixed-step result - physics has not simulated yet when FixedTick fires");

    static_cast<void>(scene.Runtime().Update(0.02F));
    kb::tests::Require(observedDuringFixedTick.size() == 2U, "LIB-128 timing contract test fixture: FixedTick must have dispatched twice by now");
    kb::tests::Require(observedDuringFixedTick[1] == 1,
        "LIB-128 contract: FixedTick sees the PREVIOUS Update() call's physics result, one call late - never the current call's");
}

// LIB-128: kb::script::ScriptRuntimeFrameSettings::fixedDeltaSeconds
// (drives ScriptRuntimeSceneSystem's own FixedTick accumulator) and
// kb::scene::SceneRuntimeFixedStepSettings::fixedDeltaSeconds (drives the
// scene's own accumulator, which steps a RequiresFixedStep()==true system
// like the real JoltPhysicsSceneSystem) are two GENUINELY INDEPENDENT
// accumulators with no code keeping them synchronized - configuring them
// to different rates here and watching the step counts diverge over
// several Update() calls proves that fact empirically, rather than just
// asserting it.
void RunFixedTickAndPhysicsAccumulatorsAreIndependentTest() {
    kb::scene::Scene scene;
    scene.Runtime().SetFixedStepSettings(kb::scene::SceneRuntimeFixedStepSettings{
        .fixedDeltaSeconds = 0.02F,
        .maxFrameDeltaSeconds = 0.25F,
        .maxFixedStepsPerFrame = 8U,
    });

    auto physicsStandIn = std::make_unique<RecordingFixedPhysicsStandIn>();
    RecordingFixedPhysicsStandIn* physicsView = physicsStandIn.get();
    scene.Runtime().AddSceneSystem(std::move(physicsStandIn));

    // Script's own FixedTick accumulator runs at HALF physics's rate
    // (0.01s vs 0.02s) - deliberately different, to make any divergence
    // impossible to miss.
    kb::script::ScriptRuntimeHost host{
        scene,
        kb::script::ScriptRuntimeHostOptions{
            .frameSettings = kb::script::ScriptRuntimeFrameSettings{ .fixedDeltaSeconds = 0.01F, .maxFixedStepsPerFrame = 8U },
        },
    };
    kb::tests::Require(host.Succeeded(), "LIB-128 independence test host did not initialize");

    constexpr kb::assets::AssetId kAsset{ 9701U };
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "FixedTickCounter" });
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
    });

    int fixedTickCount = 0;
    kb::tests::Require(host.NativeBackend().RegisterLifecycle(kAsset, kb::script::ScriptLifecycleEvent::FixedTick, [&fixedTickCount](kb::script::ScriptExecutionContext&) {
                           ++fixedTickCount;
                       }),
        "LIB-128 independence test FixedTick registration failed");
    kb::tests::Require(host.InstallSceneSystem(), "LIB-128 independence test scene system install failed");

    static_cast<void>(scene.Runtime().Update(0.02F));
    kb::tests::Require(physicsView->stepsRun == 1, "LIB-128 independence test: physics (0.02s rate) must consume exactly 1 step from a 0.02s update");
    kb::tests::Require(fixedTickCount == 2, "LIB-128 independence test: script FixedTick (0.01s rate) must consume exactly 2 steps from the SAME 0.02s update - twice physics's count, proving the two accumulators are genuinely independent, not silently unified");
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

// LIB-065: FrameIndex/FixedStepIndex are monotonic across the scene's
// whole lifetime (never reset per frame, unlike LastFixedStepCount) —
// reuses the exact fixture from RunSceneRuntimeFixedStepTest so the
// expected cumulative fixed-step counts are directly comparable.
void RunSceneRuntimeFrameAndPlayStateTest() {
    kb::scene::Scene scene;
    kb::tests::Require(scene.Runtime().IsPlaying(), "A scene must report IsPlaying() == true by default");
    scene.Runtime().SetPlaying(false);
    kb::tests::Require(!scene.Runtime().IsPlaying(), "SetPlaying(false) must be reflected by IsPlaying()");
    scene.Runtime().SetPlaying(true);
    kb::tests::Require(scene.Runtime().IsPlaying(), "SetPlaying(true) must be reflected by IsPlaying()");

    kb::tests::Require(scene.Runtime().FrameIndex() == 0U, "A freshly constructed scene must report FrameIndex() == 0 before any Update()");
    kb::tests::Require(scene.Runtime().FixedStepIndex() == 0U, "A freshly constructed scene must report FixedStepIndex() == 0 before any Update()");

    scene.Runtime().SetFixedStepSettings(kb::scene::SceneRuntimeFixedStepSettings{
        .fixedDeltaSeconds = 0.02F,
        .maxFrameDeltaSeconds = 0.25F,
        .maxFixedStepsPerFrame = 4U,
    });
    SceneSystemCounters counters;
    scene.Runtime().AddSceneSystem(std::make_unique<CountingFixedSceneSystem>(counters));

    static_cast<void>(scene.Runtime().Update(0.01F));
    kb::tests::Require(scene.Runtime().FrameIndex() == 1U, "FrameIndex must increment by exactly one per Update() call");
    kb::tests::Require(scene.Runtime().FixedStepIndex() == 0U, "FixedStepIndex must not advance before the accumulator reaches one fixed step");

    static_cast<void>(scene.Runtime().Update(0.03F));
    kb::tests::Require(scene.Runtime().FrameIndex() == 2U, "FrameIndex must keep incrementing across successive Update() calls");
    kb::tests::Require(scene.Runtime().FixedStepIndex() == 2U, "FixedStepIndex must accumulate the 2 fixed steps this frame consumed, on top of the previous 0");

    static_cast<void>(scene.Runtime().Update(1.0F));
    kb::tests::Require(scene.Runtime().FrameIndex() == 3U, "FrameIndex must reach 3 after a third Update() call");
    kb::tests::Require(scene.Runtime().FixedStepIndex() == 6U, "FixedStepIndex must accumulate the 4 capped fixed steps on top of the previous 2, never resetting like LastFixedStepCount does");
}

void RunSceneRuntimeFixedInterpolationTest() {
    kb::scene::Scene scene;
    scene.Runtime().SetFixedStepSettings(kb::scene::SceneRuntimeFixedStepSettings{
        .fixedDeltaSeconds = 0.02F,
        .maxFrameDeltaSeconds = 0.25F,
        .maxFixedStepsPerFrame = 4U,
    });

    kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Interpolated",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, 0.0F, 0.0F },
        },
    });
    scene.Runtime().AddSceneSystem(std::make_unique<FixedMoveSceneSystem>(object.Entity(), kb::scene::Vec3{ 10.0F, 0.0F, 0.0F }));

    static_cast<void>(scene.Runtime().Update(0.02F));
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(object).localPosition.x, 10.0F), "Fixed system should write the current transform");
    std::optional<kb::scene::TransformComponent> interpolated = scene.Runtime().InterpolatedTransform(object.Entity());
    kb::tests::Require(interpolated.has_value(), "Runtime should expose an interpolated transform sample");
    kb::tests::Require(kb::tests::NearlyEqual(interpolated->localPosition.x, 0.0F), "Alpha 0 should expose the previous fixed transform sample");

    static_cast<void>(scene.Runtime().Update(0.01F));
    interpolated = scene.Runtime().InterpolatedTransform(object.Entity());
    kb::tests::Require(interpolated.has_value(), "Runtime should keep interpolation samples across frames without a fixed step");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Runtime().FixedInterpolationAlpha(), 0.5F), "Runtime should expose half-step interpolation alpha");
    kb::tests::Require(kb::tests::NearlyEqual(interpolated->localPosition.x, 5.0F), "Interpolated transform should blend previous and current fixed samples");
}

void RunSceneRuntimeTransformHotPathReportTest() {
    kb::scene::Scene scene;

    kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Hot Path Parent",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 3.0F, 0.0F, 0.0F },
        },
    });
    kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Hot Path Child",
        .parent = parent,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 4.0F, 0.0F, 0.0F },
        },
    });
    kb::scene::SceneObject hiddenChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Hidden Hot Path Child",
        .parent = parent,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 6.0F, 0.0F, 0.0F },
        },
    });
    kb::scene::SceneObject cameraChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Camera Hot Path Child",
        .parent = parent,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 8.0F, 0.0F, 0.0F },
        },
    });
    kb::scene::SceneObject lightChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Light Hot Path Child",
        .parent = parent,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 10.0F, 0.0F, 0.0F },
        },
    });
    scene.Components().MeshRenderers().Set(child.Entity(), kb::scene::MeshRendererComponent{ .meshAssetId = 42U, .materialAssetId = 5U });
    scene.Components().MeshRenderers().Set(hiddenChild.Entity(), kb::scene::MeshRendererComponent{ .meshAssetId = 77U, .materialAssetId = 9U });
    scene.Components().Visibility().Set(hiddenChild.Entity(), kb::scene::VisibilityComponent{ .visible = false });
    scene.Components().Cameras().Set(cameraChild.Entity(), kb::scene::CameraComponent{ .primary = true });
    scene.Components().Lights().Set(lightChild.Entity(), kb::scene::LightComponent{ .kind = kb::scene::LightKind::Point, .intensity = 3.0F });

    static_cast<void>(scene.Runtime().Update(0.016F));

    const kb::scene::TransformComponent childTransform = scene.Transforms().Get(child);
    kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.x, 7.0F), "Scene transform hot path did not update hierarchy without a virtual scene system");

    const kb::scene::SceneRuntimeHotPathReport report = scene.Runtime().HotPathReport();
    kb::tests::Require(report.transformHierarchyUsesBatchPath, "Scene transform hot path should report the batch path");
    kb::tests::Require(report.transformHierarchyUsesKernelContract, "Scene transform hot path should report the kernel contract path");
    kb::tests::Require(!report.transformHierarchyUsesVirtualSceneSystem, "Scene transform hot path should not report virtual SceneSystem execution");
    kb::tests::Require(report.transformTopologicalBatchCount >= 2U, "Scene transform hot path should expose topological batches");
    kb::tests::Require(report.transformRenderProxyUpdateCount >= 2U, "Scene transform hot path should expose render-proxy update batching");
    kb::tests::Require(report.transformRenderProxyMeshRendererCount == 2U, "Scene transform hot path did not compact mesh render proxy indices");
    kb::tests::Require(report.transformRenderProxyVisibleMeshRendererCount == 1U, "Scene transform hot path did not compact visible mesh render proxy indices");
    kb::tests::Require(report.transformRenderProxyCameraCount == 1U, "Scene transform hot path did not compact camera render proxy indices");
    kb::tests::Require(report.transformRenderProxyLightCount == 1U, "Scene transform hot path did not compact light render proxy indices");
    kb::tests::Require(report.transformRenderProxyIdentityAffineFastPathCount == report.transformRenderProxyUpdateCount, "Scene transform render proxy did not use the identity affine fast path for identity transforms");

    const std::span<const kb::scene::SceneEntity> renderProxyEntities = scene.Runtime().TransformRenderProxyUpdateEntities();
    const std::span<const kb::scene::WorldTransformAffine3x4> renderProxyWorlds = scene.Runtime().TransformRenderProxyWorldAffine3x4();
    kb::tests::Require(renderProxyWorlds.size() == renderProxyEntities.size(), "Scene transform compact render proxy payload count diverged from entity count");
    bool foundChildProxy = false;
    for (std::size_t index = 0; index < renderProxyEntities.size(); ++index) {
        if (renderProxyEntities[index] == child.Entity()) {
            foundChildProxy = true;
            kb::tests::Require(kb::tests::NearlyEqual(renderProxyWorlds[index].values[9], 7.0F), "Scene transform compact render proxy wrote invalid child translation X");
            kb::tests::Require(kb::tests::NearlyEqual(renderProxyWorlds[index].values[10], 0.0F), "Scene transform compact render proxy wrote invalid child translation Y");
            kb::tests::Require(kb::tests::NearlyEqual(renderProxyWorlds[index].values[11], 0.0F), "Scene transform compact render proxy wrote invalid child translation Z");
        }
    }
    kb::tests::Require(foundChildProxy, "Scene transform compact render proxy did not include the updated child");

    MeshRendererProxyStats allMeshProxyStats;
    scene.Components().Visitors().ForEachUpdatedMeshRendererRenderProxy(&AccumulateMeshRendererProxyVisit, &allMeshProxyStats);
    kb::tests::Require(allMeshProxyStats.sawVisibleMesh, "Scene compact mesh renderer proxy did not visit the visible mesh");
    kb::tests::Require(allMeshProxyStats.sawHiddenMesh, "Scene compact mesh renderer proxy did not visit the hidden mesh in the unfiltered pass");
    kb::tests::Require(kb::tests::NearlyEqual(allMeshProxyStats.visibleTranslationX, 7.0F), "Scene compact mesh renderer proxy provided an invalid visible mesh transform");

    MeshRendererProxyStats visibleMeshProxyStats;
    scene.Components().Visitors().ForEachVisibleUpdatedMeshRendererRenderProxy(&AccumulateMeshRendererProxyVisit, &visibleMeshProxyStats);
    kb::tests::Require(visibleMeshProxyStats.sawVisibleMesh, "Scene visible compact mesh renderer proxy did not visit the visible mesh");
    kb::tests::Require(!visibleMeshProxyStats.sawHiddenMesh, "Scene visible compact mesh renderer proxy visited a hidden mesh");

    CameraProxyStats cameraProxyStats;
    scene.Components().Visitors().ForEachUpdatedCameraRenderProxy(&AccumulateCameraProxyVisit, &cameraProxyStats);
    kb::tests::Require(cameraProxyStats.visited == 1U, "Scene compact camera proxy visited an invalid number of cameras");
    kb::tests::Require(cameraProxyStats.sawPrimary, "Scene compact camera proxy did not visit the primary camera");
    kb::tests::Require(kb::tests::NearlyEqual(cameraProxyStats.translationX, 11.0F), "Scene compact camera proxy provided an invalid camera transform");

    LightProxyStats lightProxyStats;
    scene.Components().Visitors().ForEachUpdatedLightRenderProxy(&AccumulateLightProxyVisit, &lightProxyStats);
    kb::tests::Require(lightProxyStats.visited == 1U, "Scene compact light proxy visited an invalid number of lights");
    kb::tests::Require(lightProxyStats.sawPoint, "Scene compact light proxy did not visit the point light");
    kb::tests::Require(kb::tests::NearlyEqual(lightProxyStats.translationX, 13.0F), "Scene compact light proxy provided an invalid light transform");

    scene.Components().MeshRenderers().Remove(child.Entity());
    kb::scene::TransformComponent movedChild = scene.Transforms().Get(child);
    movedChild.localPosition.x = 5.0F;
    scene.Transforms().Set(child, movedChild);
    static_cast<void>(scene.Runtime().Update(0.016F));

    const kb::scene::SceneRuntimeHotPathReport removedRendererReport = scene.Runtime().HotPathReport();
    kb::tests::Require(removedRendererReport.transformRenderProxyUpdateCount >= 1U, "Scene render proxy mask regression did not update the moved child");
    kb::tests::Require(removedRendererReport.transformRenderProxyMeshRendererCount == 0U, "Scene render proxy mask kept a removed mesh renderer in the compact proxy list");
    kb::tests::Require(removedRendererReport.transformRenderProxyVisibleMeshRendererCount == 0U, "Scene render proxy mask kept a removed visible mesh renderer in the compact proxy list");
}

void RunSceneRuntimeRootTransformFastPathCorrectnessTest() {
    kb::scene::Scene scene;

    const kb::scene::SceneObject identityRoot = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Identity Root",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 2.0F, -3.0F, 5.0F },
            .localScale = kb::scene::Vec3{ 2.0F, 4.0F, 8.0F },
        },
    });
    const kb::scene::SceneObject rotatedRoot = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Rotated Root",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ -1.0F, 7.0F, 0.5F },
            .localRotation = kb::scene::Quat{ .z = 0.5F, .w = 0.5F },
            .localScale = kb::scene::Vec3{ 3.0F, 3.0F, 3.0F },
        },
    });

    static_cast<void>(scene.Runtime().Update(0.016F));

    const kb::scene::TransformComponent identity = scene.Transforms().Get(identityRoot);
    kb::tests::Require(kb::tests::NearlyEqual(identity.worldPosition.x, identity.localPosition.x), "Root transform fast path did not copy local X to world X");
    kb::tests::Require(kb::tests::NearlyEqual(identity.worldPosition.y, identity.localPosition.y), "Root transform fast path did not copy local Y to world Y");
    kb::tests::Require(kb::tests::NearlyEqual(identity.worldPosition.z, identity.localPosition.z), "Root transform fast path did not copy local Z to world Z");
    kb::tests::Require(kb::tests::NearlyEqual(identity.worldScale.x, identity.localScale.x), "Root transform fast path did not copy local scale X");
    kb::tests::Require(kb::tests::NearlyEqual(identity.worldScale.y, identity.localScale.y), "Root transform fast path did not copy local scale Y");
    kb::tests::Require(kb::tests::NearlyEqual(identity.worldScale.z, identity.localScale.z), "Root transform fast path did not copy local scale Z");
    kb::tests::Require(identity.worldRotation.x == 0.0F && identity.worldRotation.y == 0.0F && identity.worldRotation.z == 0.0F && identity.worldRotation.w == 1.0F, "Root transform fast path did not preserve identity rotation");
    kb::tests::Require(identity.parentVersion == 0U && !identity.worldDirty, "Root transform fast path did not publish clean root metadata");

    const kb::scene::TransformComponent rotated = scene.Transforms().Get(rotatedRoot);
    kb::tests::Require(kb::tests::NearlyEqual(rotated.worldRotation.z, 0.70710677F), "Root transform fallback did not normalize non-identity rotation Z");
    kb::tests::Require(kb::tests::NearlyEqual(rotated.worldRotation.w, 0.70710677F), "Root transform fallback did not normalize non-identity rotation W");
    kb::tests::Require(!rotated.worldDirty, "Root transform fallback did not publish clean metadata");
}

void RunSceneRuntimeRootOnlyNativeDirtyRangePathTest() {
    kb::scene::Scene scene;

    kb::scene::SceneObject first = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Root Native Dirty A",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 1.0F, 2.0F, 3.0F },
        },
    });
    kb::scene::SceneObject second = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Root Native Dirty B",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 4.0F, 5.0F, 6.0F },
        },
    });
    kb::scene::SceneObject rotated = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Root Native Dirty Rotated",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 7.0F, 8.0F, 9.0F },
            .localRotation = kb::scene::Quat{ .z = 0.5F, .w = 0.5F },
        },
    });

    static_cast<void>(scene.Runtime().SynchronizeTransforms());
    const kb::scene::SceneRuntimeHotPathReport dirtyReport = scene.Runtime().HotPathReport();
    kb::tests::Require(dirtyReport.transformHierarchyInspectedCount == 3U, "Scene root-only native dirty path did not inspect dirty roots");
    kb::tests::Require(dirtyReport.transformHierarchyUpdatedCount == 3U, "Scene root-only native dirty path did not update dirty roots");
    kb::tests::Require(dirtyReport.transformHierarchyRootFastPathCount == 2U, "Scene root-only native dirty path did not isolate identity root fast path updates");
    kb::tests::Require(dirtyReport.transformHierarchyCacheBuildNanoseconds == 0U, "Scene root-only native dirty path built the transform cache");
    kb::tests::Require(dirtyReport.transformHierarchyEntryBuildNanoseconds == 0U, "Scene root-only native dirty path built hierarchy entries");
    kb::tests::Require(dirtyReport.transformHierarchyBatchFlushCount == 0U, "Scene root-only native dirty path used a batch flush");
    kb::tests::Require(dirtyReport.transformHierarchyFlushedEntityCount == 3U, "Scene root-only native dirty path did not report direct flushed roots");
    kb::tests::Require(dirtyReport.transformRenderProxyUpdateCount == 3U, "Scene root-only native dirty path did not cache render proxy updates");
    kb::tests::Require(dirtyReport.transformRenderProxyIdentityAffineFastPathCount == 2U, "Scene root-only native dirty path did not isolate identity affine proxy writes");

    const kb::scene::TransformComponent firstTransform = scene.Transforms().Get(first);
    const kb::scene::TransformComponent secondTransform = scene.Transforms().Get(second);
    const kb::scene::TransformComponent rotatedTransform = scene.Transforms().Get(rotated);
    kb::tests::Require(!firstTransform.worldDirty && !secondTransform.worldDirty && !rotatedTransform.worldDirty, "Scene root-only native dirty path left roots dirty");
    kb::tests::Require(kb::tests::NearlyEqual(firstTransform.worldPosition.x, 1.0F), "Scene root-only native dirty path wrote invalid first root X");
    kb::tests::Require(kb::tests::NearlyEqual(secondTransform.worldPosition.y, 5.0F), "Scene root-only native dirty path wrote invalid second root Y");
    kb::tests::Require(kb::tests::NearlyEqual(rotatedTransform.worldRotation.z, 0.70710677F), "Scene root-only native dirty path did not normalize rotated root Z");

    scene.Runtime().SynchronizeTransforms();
    const kb::scene::SceneRuntimeHotPathReport cleanReport = scene.Runtime().HotPathReport();
    kb::tests::Require(cleanReport.transformHierarchyInspectedCount == 0U, "Scene root-only native clean path inspected clean roots");
    kb::tests::Require(cleanReport.transformHierarchyUpdatedCount == 0U, "Scene root-only native clean path updated clean roots");
    kb::tests::Require(cleanReport.transformHierarchyCacheBuildNanoseconds == 0U, "Scene root-only native clean path built the transform cache");
}

void RunSceneRuntimeRootOnlyNativeDirtyRangeParallelPathTest() {
    kb::scene::Scene scene;
    constexpr std::size_t kRootCount = 1024U;
    std::vector<kb::scene::SceneObject> roots;
    roots.reserve(kRootCount);
    for (std::size_t index = 0U; index < kRootCount; ++index) {
        roots.push_back(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Parallel Root Native Dirty",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ static_cast<float>(index), 2.0F, 3.0F },
            },
        }));
    }

    scene.Runtime().SynchronizeTransforms();
    const kb::scene::SceneRuntimeHotPathReport report = scene.Runtime().HotPathReport();
    kb::tests::Require(report.transformHierarchyInspectedCount == kRootCount, "Scene root-only parallel native dirty path did not inspect all roots");
    kb::tests::Require(report.transformHierarchyUpdatedCount == kRootCount, "Scene root-only parallel native dirty path did not update all roots");
    kb::tests::Require(report.transformHierarchyParallelBatchCount == 1U, "Scene root-only parallel native dirty path did not report a parallel batch");
    kb::tests::Require(report.transformHierarchyParallelChunkCount >= 8U, "Scene root-only parallel native dirty path did not split work into chunks");
    kb::tests::Require(report.transformHierarchyParallelEntityCount == kRootCount, "Scene root-only parallel native dirty path reported an invalid entity count");
    kb::tests::Require(report.transformHierarchyCacheBuildNanoseconds == 0U, "Scene root-only parallel native dirty path built the transform cache");
    kb::tests::Require(report.transformHierarchyEntryBuildNanoseconds == 0U, "Scene root-only parallel native dirty path built hierarchy entries");
    kb::tests::Require(report.transformRenderProxyUpdateCount == kRootCount, "Scene root-only parallel native dirty path missed render-proxy updates");
    kb::tests::Require(report.transformRenderProxyIdentityAffineFastPathCount == kRootCount, "Scene root-only parallel native dirty path did not batch identity affine proxy writes");

    const kb::scene::TransformComponent sampled = scene.Transforms().Get(roots[777U]);
    kb::tests::Require(kb::tests::NearlyEqual(sampled.worldPosition.x, 777.0F), "Scene root-only parallel native dirty path wrote invalid world X");
    kb::tests::Require(!sampled.worldDirty, "Scene root-only parallel native dirty path left a root dirty");

    scene.Runtime().SynchronizeTransforms();
    const kb::scene::SceneRuntimeHotPathReport cleanReport = scene.Runtime().HotPathReport();
    kb::tests::Require(cleanReport.transformHierarchyInspectedCount == 0U, "Scene root-only parallel native clean path inspected clean roots");
    kb::tests::Require(cleanReport.transformHierarchyUpdatedCount == 0U, "Scene root-only parallel native clean path updated clean roots");
    kb::tests::Require(cleanReport.transformHierarchyCacheBuildNanoseconds == 0U, "Scene root-only parallel native clean path built the transform cache");
}

void RunSceneTransformIterationUsesUnsafeHotQueryTest() {
    kb::scene::Scene scene;
    constexpr std::size_t kRootCount = 32U;
    for (std::size_t index = 0U; index < kRootCount; ++index) {
        static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Hot Query Iteration Root",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ static_cast<float>(index), 0.0F, 0.0F },
            },
        }));
    }

    scene.Runtime().SynchronizeTransforms();
    const kb::ecs::WorldTelemetrySnapshot beforeRead = scene.Runtime().EcsWorld().TelemetrySnapshot();
    std::size_t readCount = 0U;
    scene.Transforms().ForEach([](kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, void* context) {
        static_cast<void>(entity);
        static_cast<void>(transform);
        ++(*static_cast<std::size_t*>(context));
    }, &readCount);
    const kb::ecs::WorldTelemetrySnapshot afterRead = scene.Runtime().EcsWorld().TelemetrySnapshot();
    kb::tests::Require(readCount == kRootCount, "Scene transform hot iteration did not visit all transforms");
    kb::tests::Require(afterRead.queryExecutions == beforeRead.queryExecutions, "Scene transform read iteration used the safe query executor");

    const kb::ecs::WorldTelemetrySnapshot beforeWrite = scene.Runtime().EcsWorld().TelemetrySnapshot();
    std::size_t writeCount = 0U;
    scene.Transforms().ForEachMutable([](kb::scene::SceneEntity entity, kb::scene::TransformComponent& transform, void* context) {
        static_cast<void>(entity);
        transform.localPosition.y += 1.0F;
        transform.worldDirty = true;
        ++(*static_cast<std::size_t*>(context));
    }, &writeCount);
    const kb::ecs::WorldTelemetrySnapshot afterWrite = scene.Runtime().EcsWorld().TelemetrySnapshot();
    kb::tests::Require(writeCount == kRootCount, "Scene transform mutable hot iteration did not visit all transforms");
    kb::tests::Require(afterWrite.queryExecutions == beforeWrite.queryExecutions, "Scene transform mutable iteration used the safe query executor");

    scene.Runtime().SynchronizeTransforms();
    const kb::scene::SceneRuntimeHotPathReport report = scene.Runtime().HotPathReport();
    kb::tests::Require(report.transformHierarchyUpdatedCount == kRootCount, "Scene transform mutable hot iteration did not mark transforms dirty");
}

void RunSceneRuntimeTransformHierarchyUsesUnsafeQueryPlanTest() {
    SceneSystemCounters counters;
    kb::scene::Scene scene;
    kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Parent",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 10.0F, 1.0F, 2.0F },
        },
    });
    std::vector<kb::scene::SceneObject> children;
    children.reserve(4096);
    for (int index = 0; index < 4096; ++index) {
        children.push_back(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Child",
            .parent = parent,
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ static_cast<float>(index), 1.0F, 2.0F },
            },
        }));
    }

    static_cast<void>(scene.Runtime().Update(0.016F));
    scene.Runtime().AddSceneSystem(std::make_unique<MoveEntitySceneSystem>(
        counters,
        parent.Entity(),
        kb::scene::Vec3{ 10.0F, 6.0F, 2.0F }));
    scene.Runtime().SetTransformPropagationBudget(kb::scene::SceneTransformPropagationBudget{
        .maxInspectedEntitiesPerSync = children.size() + 2U,
    });

    const kb::ecs::WorldTelemetrySnapshot before = scene.Runtime().EcsWorld().TelemetrySnapshot();
    static_cast<void>(scene.Runtime().Update(0.016F));
    const kb::ecs::WorldTelemetrySnapshot after = scene.Runtime().EcsWorld().TelemetrySnapshot();
    kb::tests::Require(counters.updated == 1, "Scene transform hierarchy unsafe query test did not run the scene system update");

    const kb::scene::SceneRuntimeHotPathReport report = scene.Runtime().HotPathReport();
    kb::tests::Require(report.transformTopologicalBatchCount >= 2U, "Scene transform hierarchy unsafe query test did not build topological batches");
    kb::tests::Require(report.transformRenderProxyUpdateCount >= children.size() + 1U, "Scene transform hierarchy unsafe query path missed render-proxy updates");
    kb::tests::Require(after.queryExecutions == before.queryExecutions, "Scene transform hierarchy hot path used the safe query executor");

    const kb::scene::TransformComponent transform = scene.Transforms().Get(children[123]);
    kb::tests::Require(kb::tests::NearlyEqual(transform.worldPosition.x, 133.0F), "Scene transform hierarchy unsafe hot query path wrote an invalid child world X");
    kb::tests::Require(kb::tests::NearlyEqual(transform.worldPosition.y, 7.0F), "Scene transform hierarchy unsafe hot query path did not publish modified parent transform");
    kb::tests::Require(!transform.worldDirty, "Scene transform hierarchy unsafe hot query path left a root transform dirty");
}

void RunSceneSystemPhysicsBodyQueryAccessTest() {
    kb::scene::Scene scene;
    kb::scene::SceneObject physicsBody = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "PhysicsBody",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 7.0F, 1.0F, 2.0F },
        },
    });
    kb::scene::SceneObject transformOnly = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "TransformOnly",
    });
    kb::scene::SceneObject missingCollider = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "MissingCollider",
    });

    scene.Components().Rigidbodies().Set(physicsBody.Entity(), kb::scene::RigidbodyComponent{
        .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
        .mass = 3.0F,
    });
    scene.Components().Colliders().Set(physicsBody.Entity(), kb::scene::ColliderComponent{
        .shape = kb::scene::ColliderShape::Sphere,
        .radius = 2.0F,
    });
    scene.Components().Rigidbodies().Set(missingCollider.Entity(), kb::scene::RigidbodyComponent{
        .bodyType = kb::scene::RigidbodyBodyType::Kinematic,
        .mass = 1.0F,
    });

    static_cast<void>(transformOnly);
    kb::scene::SceneSystemContext context(scene, 0.016F);
    PhysicsBodyStats firstPass;
    context.Queries().ForEachPhysicsBody(&AccumulatePhysicsBodyVisit, &firstPass);
    kb::tests::Require(firstPass.visited == 1U, "Scene physics body query did not filter to complete physics bodies");
    kb::tests::Require(firstPass.sawDynamic, "Scene physics body query missed rigidbody data");
    kb::tests::Require(firstPass.sawSphere, "Scene physics body query missed collider data");
    kb::tests::Require(kb::tests::NearlyEqual(firstPass.localPositionX, 7.0F), "Scene physics body query missed transform data");

    PhysicsBodyStats secondPass;
    context.Queries().ForEachPhysicsBody(&AccumulatePhysicsBodyVisit, &secondPass);
    kb::tests::Require(secondPass.visited == 1U, "Scene physics body cached query returned inconsistent results");
}

} // namespace

namespace kb::tests {

void RunSceneBulkMarkModifiedTest() {
    kb::scene::Scene scene;
    const std::array<kb::scene::SceneObject, 3U> objects{
        scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "A", .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F } } }),
        scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "B", .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 0.0F, 2.0F, 0.0F } } }),
        scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "C", .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 0.0F, 0.0F, 3.0F } } }),
    };
    static_cast<void>(scene.Runtime().Update(0.016F)); // compute world, clear dirty

    // Mutate local transforms directly, then signal them all with one bulk call.
    std::array<kb::scene::SceneEntity, 3U> entities{};
    for (std::size_t index = 0; index < objects.size(); ++index) {
        entities[index] = objects[index].Entity();
        kb::scene::TransformComponent* transform = scene.Transforms().TryGet(entities[index]);
        kb::tests::Require(transform != nullptr, "Bulk mark test could not fetch a transform");
        transform->localPosition = kb::scene::Vec3{ static_cast<float>(index) + 10.0F, 0.0F, 0.0F };
    }
    scene.Transforms().MarkModified(std::span<const kb::scene::SceneEntity>{ entities });

    static_cast<void>(scene.Runtime().Update(0.016F));
    for (std::size_t index = 0; index < objects.size(); ++index) {
        const kb::scene::TransformComponent transform = scene.Transforms().Get(objects[index]);
        kb::tests::Require(kb::tests::NearlyEqual(transform.worldPosition.x, static_cast<float>(index) + 10.0F), "Bulk mark did not propagate the mutated local position to world");
        kb::tests::Require(!transform.worldDirty, "Bulk mark left a transform dirty after update");
    }
    kb::tests::Require(scene.Runtime().TransformRenderProxyUpdateEntities().size() >= objects.size(), "Bulk mark did not emit render proxy updates for the marked entities");
}

void RunSceneBulkCreateObjectsTest() {
    // H8: bulk scene spawn creates one object per descriptor in one call; the
    // structural changes batch through the world's lazy query-plan invalidation.
    kb::scene::Scene scene;
    std::array<kb::scene::SceneObjectDesc, 4U> descs{
        kb::scene::SceneObjectDesc{ .name = "Bulk0", .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 10.0F, 0.0F, 0.0F } } },
        kb::scene::SceneObjectDesc{ .name = "Bulk1", .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 20.0F, 0.0F, 0.0F } } },
        kb::scene::SceneObjectDesc{ .name = "Bulk2", .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 30.0F, 0.0F, 0.0F } } },
        kb::scene::SceneObjectDesc{ .name = "Bulk3", .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 40.0F, 0.0F, 0.0F } } },
    };

    const std::vector<kb::scene::SceneObject> created = scene.Entities().CreateObjects(std::span<const kb::scene::SceneObjectDesc>{ descs });
    kb::tests::Require(created.size() == descs.size(), "Bulk create did not create one object per descriptor");
    for (const kb::scene::SceneObject object : created) {
        kb::tests::Require(scene.Entities().IsAlive(object), "Bulk-created object is not alive");
    }

    static_cast<void>(scene.Runtime().Update(0.016F));
    for (std::size_t index = 0; index < created.size(); ++index) {
        const kb::scene::TransformComponent transform = scene.Transforms().Get(created[index]);
        kb::tests::Require(kb::tests::NearlyEqual(transform.worldPosition.x, 10.0F * static_cast<float>(index + 1U)), "Bulk-created object world transform was not computed");
    }

    scene.Entities().Destroy(std::span<const kb::scene::SceneObject>{ created });
    for (const kb::scene::SceneObject object : created) {
        kb::tests::Require(!scene.Entities().IsAlive(object), "Bulk-destroyed object is still alive");
    }
}

void RunSceneSystemTransformSyncTests() {
    RunSceneSystemTransformSyncTest();
    RunTransformSyncContractScriptsRequireExplicitSyncAcrossSystemsTest();
    RunTransformSyncContractFixedStepGetsFreshDataAutomaticallyTest();
    RunFixedTickSeesPreviousFramePhysicsResultTest();
    RunFixedTickAndPhysicsAccumulatorsAreIndependentTest();
    RunSceneRuntimeFixedStepTest();
    RunSceneRuntimeFrameAndPlayStateTest();
    RunSceneRuntimeFixedInterpolationTest();
    RunSceneRuntimeTransformHotPathReportTest();
    RunSceneRuntimeRootTransformFastPathCorrectnessTest();
    RunSceneRuntimeRootOnlyNativeDirtyRangePathTest();
    RunSceneRuntimeRootOnlyNativeDirtyRangeParallelPathTest();
    RunSceneTransformIterationUsesUnsafeHotQueryTest();
    RunSceneRuntimeTransformHierarchyUsesUnsafeQueryPlanTest();
    RunSceneSystemPhysicsBodyQueryAccessTest();
    RunSceneBulkMarkModifiedTest();
    RunSceneBulkCreateObjectsTest();
}

} // namespace kb::tests
