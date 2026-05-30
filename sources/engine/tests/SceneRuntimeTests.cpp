#include "engine/ecs/System.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

struct SystemCounters {
    int created = 0;
    int updated = 0;
    int destroyed = 0;
    float lastDelta = 0.0F;
};

struct SceneSystemCounters {
    int created = 0;
    int updated = 0;
    int destroyed = 0;
};

class CountingSystem final : public kb::ecs::System {
public:
    explicit CountingSystem(SystemCounters& counters) noexcept
        : counters_(counters) {}

    void OnCreate(kb::ecs::World& world) override {
        static_cast<void>(world);
        ++counters_.created;
    }

    void OnUpdate(kb::ecs::World& world, float deltaSeconds) override {
        static_cast<void>(world);
        ++counters_.updated;
        counters_.lastDelta = deltaSeconds;
    }

    void OnDestroy(kb::ecs::World& world) override {
        static_cast<void>(world);
        ++counters_.destroyed;
    }

private:
    SystemCounters& counters_;
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

[[nodiscard]] bool NearlyEqual(float lhs, float rhs) noexcept {
    return std::fabs(lhs - rhs) <= 0.0001F;
}

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void RunSystemLifecycleTest() {
    SystemCounters counters;

    {
        kb::scene::Scene scene;
        scene.Runtime().AddSystem(std::make_unique<CountingSystem>(counters));
        Require(counters.created == 1, "System OnCreate was not called");

        [[maybe_unused]] const bool progressed = scene.Runtime().Update(0.25F);
        Require(counters.updated == 1, "System OnUpdate was not called");
        Require(NearlyEqual(counters.lastDelta, 0.25F), "System received invalid delta time");
    }

    Require(counters.destroyed == 1, "System OnDestroy was not called");
}

void RunTransformHierarchyTest() {
    kb::scene::Scene scene;

    kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Parent",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 10.0F, 1.0F, 0.0F },
        },
    });

    kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Child",
        .parent = parent,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 2.0F, 3.0F, 4.0F },
        },
    });

    [[maybe_unused]] const bool firstProgress = scene.Runtime().Update(0.016F);
    kb::scene::TransformComponent childTransform = scene.Transforms().Get(child);
    Require(NearlyEqual(childTransform.worldPosition.x, 12.0F), "Child world X was not composed with parent");
    Require(NearlyEqual(childTransform.worldPosition.y, 4.0F), "Child world Y was not composed with parent");
    Require(NearlyEqual(childTransform.worldPosition.z, 4.0F), "Child world Z was not composed with parent");
    Require(!childTransform.worldDirty, "Child transform stayed dirty after update");

    kb::scene::TransformComponent parentTransform = scene.Transforms().Get(parent);
    parentTransform.localPosition = kb::scene::Vec3{ 20.0F, 0.0F, 1.0F };
    scene.Transforms().Set(parent, parentTransform);

    [[maybe_unused]] const bool secondProgress = scene.Runtime().Update(0.016F);
    childTransform = scene.Transforms().Get(child);
    Require(NearlyEqual(childTransform.worldPosition.x, 22.0F), "Child world X did not react to parent change");
    Require(NearlyEqual(childTransform.worldPosition.y, 3.0F), "Child world Y did not react to parent change");
    Require(NearlyEqual(childTransform.worldPosition.z, 5.0F), "Child world Z did not react to parent change");

    Require(scene.Hierarchy().SetParent(child, {}), "Child could not detach from parent");
    [[maybe_unused]] const bool thirdProgress = scene.Runtime().Update(0.016F);
    childTransform = scene.Transforms().Get(child);
    Require(NearlyEqual(childTransform.worldPosition.x, 2.0F), "Detached child world X should match local X");
    Require(NearlyEqual(childTransform.worldPosition.y, 3.0F), "Detached child world Y should match local Y");
    Require(NearlyEqual(childTransform.worldPosition.z, 4.0F), "Detached child world Z should match local Z");
}

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
        Require(counters.created == 1, "Scene system OnCreate was not called");

        [[maybe_unused]] const bool progressed = scene.Runtime().Update(0.016F);
        Require(counters.updated == 1, "Scene system OnUpdate was not called");

        const kb::scene::TransformComponent childTransform = scene.Transforms().Get(child);
        Require(NearlyEqual(childTransform.worldPosition.x, 12.0F), "Scene system transform changes were not synchronized in the same update");
    }

    Require(counters.destroyed == 1, "Scene system OnDestroy was not called");
}

} // namespace

int main() {
    RunSystemLifecycleTest();
    RunTransformHierarchyTest();
    RunSceneSystemTransformSyncTest();
    return EXIT_SUCCESS;
}
