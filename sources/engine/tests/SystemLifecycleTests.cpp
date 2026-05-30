#include "SceneSystemTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/System.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRuntime.hpp"

#include <memory>

namespace {

struct SystemCounters {
    int created = 0;
    int updated = 0;
    int destroyed = 0;
    float lastDelta = 0.0F;
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

void RunSystemLifecycleTest() {
    SystemCounters counters;

    {
        kb::scene::Scene scene;
        scene.Runtime().AddSystem(std::make_unique<CountingSystem>(counters));
        kb::tests::Require(counters.created == 1, "System OnCreate was not called");

        [[maybe_unused]] const bool progressed = scene.Runtime().Update(0.25F);
        kb::tests::Require(counters.updated == 1, "System OnUpdate was not called");
        kb::tests::Require(kb::tests::NearlyEqual(counters.lastDelta, 0.25F), "System received invalid delta time");
    }

    kb::tests::Require(counters.destroyed == 1, "System OnDestroy was not called");
}

} // namespace

namespace kb::tests {

void RunSystemLifecycleTests() {
    RunSystemLifecycleTest();
}

} // namespace kb::tests
