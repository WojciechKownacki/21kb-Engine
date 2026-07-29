#include "TestSupport.hpp"

#include "engine/core/ReadSnapshotQueue.hpp"
#include "engine/library/EngineLibraryEntityHandle.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

void RunCommandQueueOwnershipSafetyTest() {
    kb::core::CommandQueue queue;
    for (std::uint64_t index = 0U; index < 4'096U; ++index) {
        queue.Enqueue(kb::core::RuntimeCommand{ .target = index, .kind = static_cast<std::uint32_t>(index) });
    }

    std::vector<kb::core::RuntimeCommand> commands = queue.Drain();
    kb::tests::Require(commands.size() == 4'096U && queue.Drain().empty(),
        "Command queue must transfer ownership into an independent drained batch");
    for (std::size_t index = 0U; index < commands.size(); ++index) {
        kb::tests::Require(commands[index].target == index && commands[index].kind == index,
            "Command queue changed a command while transferring ownership");
        queue.Enqueue(commands[index]);
    }

    commands.clear();
    const std::vector<kb::core::RuntimeCommand> replayed = queue.Drain();
    kb::tests::Require(replayed.size() == 4'096U && replayed.front().target == 0U && replayed.back().target == 4'095U,
        "Command queue must remain valid after a drained batch is released");
}

void RunEntityHandleLifetimeSafetyTest() {
    kb::scene::Scene scene;
    std::vector<kb::library::EntityHandle> staleHandles;
    staleHandles.reserve(1'024U);

    for (std::size_t index = 0U; index < staleHandles.capacity(); ++index) {
        const kb::scene::SceneEntity entity = scene.Entities().CreateEntity();
        staleHandles.emplace_back(entity, scene.Id());
        kb::tests::Require(staleHandles.back().IsAlive(scene), "Fresh entity handle must be alive before destruction");
        scene.Entities().Destroy(entity);
        kb::tests::Require(!staleHandles.back().IsAlive(scene), "Destroyed entity handle must become stale without dereferencing storage");

        const kb::scene::SceneEntity replacement = scene.Entities().CreateEntity();
        kb::tests::Require(!staleHandles.back().IsAlive(scene) && scene.Entities().IsAlive(replacement),
            "A recycled entity slot must not revive a stale generation-checked handle");
        scene.Entities().Destroy(replacement);
    }
}

} // namespace

int main() {
    RunCommandQueueOwnershipSafetyTest();
    RunEntityHandleLifetimeSafetyTest();
    return EXIT_SUCCESS;
}
