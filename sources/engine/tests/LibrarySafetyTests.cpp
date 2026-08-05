#include "TestSupport.hpp"

#include "engine/core/ReadSnapshotQueue.hpp"
#include "engine/library/EngineLibraryEntityHandle.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <atomic>
#include <thread>
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

void RunReadSnapshotPublisherConcurrencyTest() {
    struct Snapshot final : kb::core::ReadSnapshot {
        std::uint64_t payload = 0U;
    };
    kb::core::ReadSnapshotPublisher<Snapshot> publisher;
    std::atomic<bool> writerFinished{false};
    std::atomic<bool> readerObservedTear{false};
    std::thread reader{[&] {
        while (!writerFinished.load(std::memory_order_acquire)) {
            const std::shared_ptr<const Snapshot> snapshot = publisher.Read();
            if (snapshot->payload != snapshot->revision) {
                readerObservedTear.store(true, std::memory_order_release);
                return;
            }
        }
    }};
    for (std::uint64_t revision = 1U; revision <= 4'096U; ++revision) {
        Snapshot snapshot{};
        snapshot.revision = revision;
        snapshot.payload = revision;
        publisher.Publish(std::move(snapshot));
    }
    writerFinished.store(true, std::memory_order_release);
    reader.join();
    const std::shared_ptr<const Snapshot> final = publisher.Read();
    kb::tests::Require(!readerObservedTear.load(std::memory_order_acquire) &&
            final->revision == 4'096U && final->payload == 4'096U,
        "Read snapshot publisher must expose one complete immutable value to concurrent readers");
}

void RunReadSnapshotPublisherMonotonicTest() {
    struct Snapshot final : kb::core::ReadSnapshot {
        std::uint64_t payload = 0U;
    };
    kb::core::ReadSnapshotPublisher<Snapshot> publisher;
    Snapshot initial{};
    initial.revision = 4U;
    initial.payload = 4U;
    publisher.Publish(std::move(initial));

    Snapshot stale{};
    stale.revision = 3U;
    stale.payload = 3U;
    const bool staleAccepted = publisher.TryPublishMonotonic(std::move(stale));
    Snapshot equal{};
    equal.revision = 4U;
    equal.payload = 99U;
    const bool equalAccepted = publisher.TryPublishMonotonic(std::move(equal));
    Snapshot newer{};
    newer.revision = 5U;
    newer.payload = 5U;
    const bool newerAccepted = publisher.TryPublishMonotonic(std::move(newer));

    const std::shared_ptr<const Snapshot> latest = publisher.Read();
    kb::tests::Require(!staleAccepted && !equalAccepted && newerAccepted &&
            latest->revision == 5U && latest->payload == 5U,
        "Monotonic snapshot publish must reject stale and equal revisions under the publisher lock");
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
    RunReadSnapshotPublisherConcurrencyTest();
    RunReadSnapshotPublisherMonotonicTest();
    RunEntityHandleLifetimeSafetyTest();
    return EXIT_SUCCESS;
}
