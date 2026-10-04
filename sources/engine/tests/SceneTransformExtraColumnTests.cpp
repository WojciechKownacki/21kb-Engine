#include "TestSupport.hpp"

#include "engine/ecs/NativeArchetypeStorage.hpp"
#include "engine/ecs/Query.hpp"
#include "engine/ecs/QueryFilter.hpp"
#include "engine/ecs/World.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <array>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

struct ExtraState {
    std::uint32_t value = 0U;
};

struct ExtraReadState {
    std::uint32_t value = 91U;
};

struct PartitionState {
    std::uint32_t value = 0U;
};

struct ExtraFixture {
    kb::scene::Scene scene{ kb::ecs::WorldConfig{ .chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk4KB, .workerThreadLimit = 4U } };
    kb::ecs::World& world = scene.Runtime().EcsWorld();
    kb::ecs::NativeArchetypeStorage& storage = const_cast<kb::ecs::NativeArchetypeStorage&>(world.NativeStorage());
    std::vector<kb::scene::SceneObject> objects;
    std::vector<kb::ecs::QueryTableDispatchRecord> records;
    kb::ecs::ComponentId extraId = 0U;
    kb::ecs::ComponentId readId = 0U;

    void Spawn(std::size_t count) {
        std::vector<ExtraState> states(count);
        for (std::size_t row = 0U; row < count; ++row) states[row].value = static_cast<std::uint32_t>(objects.size() + row);
        const std::vector<ExtraReadState> reads(count);
        const std::array<kb::ecs::World::BulkComponentView, 2U> components{
            kb::ecs::World::MakeBulkComponentView(std::span<const ExtraState>{ states }),
            kb::ecs::World::MakeBulkComponentView(std::span<const ExtraReadState>{ reads })
        };
        const auto spawned = scene.Entities().CreateObjects(std::vector<kb::scene::SceneObjectDesc>(count), components);
        objects.insert(objects.end(), spawned.begin(), spawned.end());
        extraId = world.Component<ExtraState>();
        readId = world.Component<ExtraReadState>();
        RefreshRecords();
    }

    void RefreshRecords() {
        const std::array<kb::ecs::ComponentId, 2U> ids{ extraId, readId };
        storage.CollectQueryRecords(ids, {}, {}, records);
    }

    void ClearDirty() {
        for (const auto& record : records) {
            storage.ClearComponentDirtyRows(record.nativeArchetypeIndex, record.nativeChunkIndex, extraId);
            storage.ClearComponentDirtyRows(record.nativeArchetypeIndex, record.nativeChunkIndex, readId);
        }
    }

    void CreateThreeChunksAndTail() {
        Spawn(1U);
        const auto stats = storage.Stats();
        const auto found = std::find_if(stats.archetypeCounters.begin(), stats.archetypeCounters.end(), [&](const auto& table) {
            return table.archetypeIndex == records[0].nativeArchetypeIndex;
        });
        kb::tests::Require(found != stats.archetypeCounters.end() && found->chunkCounters[0].capacity > 4U,
            "Extra-column test could not resolve its chunk capacity");
        const std::size_t capacity = found->chunkCounters[0].capacity;
        Spawn(3U * capacity + 5U - 1U);
        kb::tests::Require(records.size() == 4U && records[0].entityCount == capacity && records[1].entityCount == capacity &&
                records[2].entityCount == capacity && records[3].entityCount == 5U,
            "Extra-column test requires three full chunks and a tail");
    }
};

void CountExtra(kb::ecs::Entity, const ExtraState&, void* context) {
    ++*static_cast<std::size_t*>(context);
}

void ObserveExtra(kb::ecs::Entity, kb::ecs::ComponentEventKind event, const ExtraState* state, void* context) {
    if (event == kb::ecs::ComponentEventKind::Modified && state != nullptr) {
        ++*static_cast<std::size_t*>(context);
    }
}

// Deliberately uses only the pre-existing API, so it also reproduces the missing publication against S0.
void RunLegacyMutableExtraPublicationTest() {
    kb::scene::Scene scene;
    kb::ecs::World& world = scene.Runtime().EcsWorld();
    const std::vector<ExtraState> states(7U);
    const std::array<kb::ecs::World::BulkComponentView, 1U> components{
        kb::ecs::World::MakeBulkComponentView(std::span<const ExtraState>{ states })
    };
    const std::vector<kb::scene::SceneObject> objects = scene.Entities().CreateObjects(
        std::vector<kb::scene::SceneObjectDesc>(states.size()), components);
    const auto extraId = world.Component<ExtraState>();
    auto& storage = const_cast<kb::ecs::NativeArchetypeStorage&>(world.NativeStorage());
    const std::array<kb::ecs::ComponentId, 1U> ids{ extraId };
    std::vector<kb::ecs::QueryTableDispatchRecord> records;
    storage.CollectQueryRecords(ids, {}, {}, records);
    kb::tests::Require(records.size() == 1U, "Extra publication reproducer must use one chunk");
    storage.ClearComponentDirtyRows(records[0].nativeArchetypeIndex, records[0].nativeChunkIndex, extraId);
    const std::uint64_t version = storage.ComponentVersion(objects[0].Entity(), extraId);
    kb::ecs::QueryFilter changedFilter;
    changedFilter.Changed(extraId);
    auto changed = world.CreateQuery<ExtraState>(changedFilter);
    std::size_t changedRows = 0U;
    changed.ForEach(&CountExtra, &changedRows);
    changedRows = 0U;
    changed.ForEach(&CountExtra, &changedRows);
    kb::tests::Require(changedRows == 0U, "Extra publication reproducer must drain initial Changed state");
    std::size_t notifications = 0U;
    const auto observer = world.ObserveComponent<ExtraState>(kb::ecs::ComponentEventKind::Modified, &ObserveExtra, &notifications);
    kb::tests::Require(observer != 0U, "Extra publication observer registration failed");

    const auto stats = scene.Transforms().ParallelForEachRoot<ExtraState>(64U, [](kb::scene::TransformRowRange& range) {
        ExtraState* extras = range.Column<ExtraState>();
        for (std::size_t row = 0U; row < range.Count(); ++row) extras[row].value = 42U;
    });
    changed.ForEach(&CountExtra, &changedRows);
    const std::uint64_t finalVersion = storage.ComponentVersion(objects[0].Entity(), extraId);
    const std::size_t dirtyRows = storage.ComponentDirtyCount(records[0].nativeArchetypeIndex, records[0].nativeChunkIndex, extraId);
    std::cout << "extra-only: rows=" << stats.rowsVisited << " SetLocal=" << stats.rowsWritten
              << " native=" << world.TryGet<ExtraState>(objects[0].Entity())->value
              << " version=" << version << "->" << finalVersion << " dirty=" << dirtyRows
              << " changed=" << changedRows << " OnSet=" << notifications << '\n';
    kb::tests::Require(stats.rowsWritten == 0U && world.TryGet<ExtraState>(objects[0].Entity())->value == 42U,
        "An extra-only transform pass must keep its native writes without SetLocal");
    kb::tests::Require(finalVersion > version && dirtyRows == states.size() && changedRows == states.size() && notifications == states.size(),
        "Mutable extra columns must publish component versions, dirty rows, Changed and OnSet without SetLocal");
}

void RunReadPartialRepeatedAndFullTest(bool observed) {
    ExtraFixture fixture;
    fixture.CreateThreeChunksAndTail();
    fixture.ClearDirty();
    const auto sample = fixture.objects[0].Entity();
    const auto version = fixture.storage.ComponentVersion(sample, fixture.extraId);
    const auto readVersion = fixture.storage.ComponentVersion(sample, fixture.readId);
    std::size_t notifications = 0U;
    if (observed) {
        kb::tests::Require(fixture.world.ObserveComponent<ExtraState>(kb::ecs::ComponentEventKind::Modified, &ObserveExtra, &notifications) != 0U,
            "Extra-column test observer registration failed");
    }
    const auto readStats = fixture.scene.Transforms().ParallelForEachRoot<ExtraState, ExtraReadState>(1U, [](kb::scene::TransformRowRange& range) {
        const auto* states = range.Column<const ExtraState>();
        const auto* reads = range.ReadColumn<ExtraReadState>(1U);
        kb::tests::Require(states != nullptr && reads != nullptr && reads[0].value == 91U,
            "Read-only extra columns must expose the original values");
        kb::tests::Require(range.Column<ExtraState>(4U) == nullptr, "An absent extra column must return null");
    });
    kb::tests::Require(readStats.rowsWritten == 0U && fixture.storage.ComponentVersion(sample, fixture.extraId) == version && notifications == 0U,
        "Read-only extra-column access must not publish writes");
    for (const auto& record : fixture.records) {
        kb::tests::Require(fixture.storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, fixture.extraId) == 0U,
            "Read-only extra-column access dirtied a chunk");
    }

    kb::ecs::QueryFilter filter;
    filter.Changed(fixture.extraId);
    auto changed = fixture.world.CreateQuery<ExtraState>(filter);
    std::size_t changedRows = 0U;
    changed.ForEach(&CountExtra, &changedRows);
    changedRows = 0U;
    changed.ForEach(&CountExtra, &changedRows);
    kb::tests::Require(changedRows == 0U, "Initial Changed state did not drain for the extra-column test");
    const auto partial = fixture.scene.Transforms().ParallelForEachRoot<ExtraState, ExtraReadState>(1U, [](kb::scene::TransformRowRange& range) {
        auto* first = range.WriteColumn<ExtraState>(1U, 2U);
        first[0].value = 101U;
        first[1].value = 102U;
        auto* overlap = range.WriteColumn<ExtraState>(2U, 2U);
        overlap[0].value = 202U;
        overlap[1].value = 203U;
        range.WriteColumn<ExtraState>(1U, 1U)[0].value = 301U;
        static_cast<void>(range.WriteColumn<ExtraState>(range.Count(), 0U));
        kb::tests::Require(range.ReadColumn<ExtraReadState>(1U)[0].value == 91U, "Partial extra writes changed a read-only column");
    });
    kb::tests::Require(partial.rowsWritten == 0U && notifications == (observed ? fixture.records.size() * 3U : 0U),
        "Partial/repeated extra writes must publish each declared row once without SetLocal");
    kb::tests::Require(fixture.storage.ComponentVersion(sample, fixture.extraId) > version &&
            fixture.storage.ComponentVersion(sample, fixture.readId) == readVersion,
        "Partial extra writes must advance only their component's version");
    for (const auto& record : fixture.records) {
        kb::tests::Require(fixture.storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, fixture.extraId) == 3U &&
                fixture.storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, fixture.readId) == 0U,
            "Partial/repeated extra writes must dirty exactly the union of declared rows in every chunk");
        const auto* states = static_cast<const ExtraState*>(record.fieldComponents[0]);
        kb::tests::Require(states[1].value == 301U && states[2].value == 202U && states[3].value == 203U,
            "Partial/repeated extra writes lost their final values");
    }
    changed.ForEach(&CountExtra, &changedRows);
    kb::tests::Require(changedRows == fixture.objects.size(), "Changed must see the whole written archetype across all chunks and its tail");
    changedRows = 0U;
    changed.ForEach(&CountExtra, &changedRows);
    kb::tests::Require(changedRows == 0U, "Changed did not consume published extra writes");

    fixture.ClearDirty();
    notifications = 0U;
    const auto full = fixture.scene.Transforms().ParallelForEachRoot<ExtraState, ExtraReadState>(1U, [](kb::scene::TransformRowRange& range) {
        auto* states = range.Column<ExtraState>();
        static_cast<void>(range.Column<ExtraState>());
        static_cast<void>(range.WriteColumn<ExtraState>(0U, 1U));
        for (std::size_t row = 0U; row < range.Count(); ++row) {
            states[row].value = 501U;
            range.SetLocal(row, kb::scene::Vec3{ 5.0F, 0.0F, 0.0F }, range.Get(row).localRotation, range.Get(row).localScale);
        }
    });
    kb::tests::Require(full.rowsWritten == fixture.objects.size() && notifications == (observed ? fixture.objects.size() : 0U),
        "Whole mutable access must publish each extra row once alongside transform writes");
    for (const auto& record : fixture.records) {
        kb::tests::Require(fixture.storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, fixture.extraId) == record.entityCount,
            "Whole mutable access missed extra dirty rows in a chunk");
    }
    for (const auto& object : fixture.objects) {
        const auto transform = fixture.scene.Transforms().Get(object);
        kb::tests::Require(fixture.world.TryGet<ExtraState>(object.Entity())->value == 501U &&
                transform.localPosition.x == 5.0F && transform.worldPosition.x == 5.0F && !transform.worldDirty,
            "Combined transform/extra writes must preserve current local and world transforms");
    }
}

void RunExceptionAndInvalidSpanTest() {
    ExtraFixture fixture;
    fixture.Spawn(7U);
    fixture.ClearDirty();
    std::size_t notifications = 0U;
    kb::tests::Require(fixture.world.ObserveComponent<ExtraState>(kb::ecs::ComponentEventKind::Modified, &ObserveExtra, &notifications) != 0U,
        "Exception extra-column test observer registration failed");
    bool rethrown = false;
    try {
        static_cast<void>(fixture.scene.Transforms().ParallelForEachRoot<ExtraState>(1U, [](kb::scene::TransformRowRange& range) {
            auto* states = range.WriteColumn<ExtraState>(1U, 2U);
            states[0].value = 701U;
            states[1].value = 702U;
            range.SetLocal(1U, kb::scene::Vec3{ 7.0F, 0.0F, 0.0F }, range.Get(1U).localRotation, range.Get(1U).localScale);
            throw std::runtime_error("extra body failed");
        }));
    } catch (const std::runtime_error&) {
        rethrown = true;
    }
    const auto& record = fixture.records[0];
    kb::tests::Require(rethrown && notifications == 2U &&
            fixture.storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, fixture.extraId) == 2U &&
            fixture.world.TryGet<ExtraState>(fixture.objects[1].Entity())->value == 701U &&
            fixture.scene.Transforms().Get(fixture.objects[1]).worldPosition.x == 7.0F,
        "Extra/transform writes must be published before rethrowing the body's exception");

    fixture.ClearDirty();
    notifications = 0U;
    const auto version = fixture.storage.ComponentVersion(fixture.objects[0].Entity(), fixture.extraId);
    for (std::size_t kind = 0U; kind < 3U; ++kind) {
        bool invalid = false;
        try {
            static_cast<void>(fixture.scene.Transforms().ParallelForEachRoot<ExtraState>(1U, [kind](kb::scene::TransformRowRange& range) {
                if (kind == 0U) static_cast<void>(range.WriteColumn<ExtraState>(range.Count() + 1U, 0U));
                if (kind == 1U) static_cast<void>(range.WriteColumn<ExtraState>(1U, std::numeric_limits<std::size_t>::max()));
                if (kind == 2U) static_cast<void>(range.WriteColumn<ExtraState>(0U, 1U, 1U));
            }));
        } catch (const std::out_of_range&) {
            invalid = true;
        }
        kb::tests::Require(invalid, "An invalid extra-column write span must throw");
    }
    kb::tests::Require(notifications == 0U && fixture.storage.ComponentVersion(fixture.objects[0].Entity(), fixture.extraId) == version,
        "Invalid extra-column write spans must not publish writes");
}

void RunUntouchedTransformArchetypeTest() {
    ExtraFixture fixture;
    fixture.Spawn(7U);
    fixture.world.Set(fixture.objects[0].Entity(), PartitionState{});
    fixture.RefreshRecords();
    const auto transformId = fixture.world.Component<kb::scene::TransformComponent>();
    const auto untouched = fixture.objects[1].Entity();
    const auto untouchedVersion = fixture.storage.ComponentVersion(untouched, transformId);
    const auto moved = fixture.objects[0].Entity();
    static_cast<void>(fixture.scene.Transforms().ParallelForEachRoot<ExtraState>(1U, [moved](kb::scene::TransformRowRange& range) {
        for (std::size_t row = 0U; row < range.Count(); ++row) {
            if (range.Entity(row) == moved) {
                range.SetLocal(row, kb::scene::Vec3{ 11.0F, 0.0F, 0.0F }, range.Get(row).localRotation, range.Get(row).localScale);
            }
        }
    }));
    kb::tests::Require(fixture.storage.ComponentVersion(untouched, transformId) == untouchedVersion &&
            fixture.scene.Transforms().Get(moved).worldPosition.x == 11.0F,
        "A transform write must not publish an untouched archetype's transform version");
}

void RunParallelExceptionPublicationTest() {
    ExtraFixture fixture;
    fixture.CreateThreeChunksAndTail();
    fixture.ClearDirty();
    std::size_t notifications = 0U;
    kb::tests::Require(fixture.world.ObserveComponent<ExtraState>(kb::ecs::ComponentEventKind::Modified, &ObserveExtra, &notifications) != 0U,
        "Parallel exception extra-column observer registration failed");
    std::atomic_size_t writes{ 0U };
    bool rethrown = false;
    try {
        static_cast<void>(fixture.scene.Transforms().ParallelForEachRoot<ExtraState>(1U, [&writes](kb::scene::TransformRowRange& range) {
            range.WriteColumn<ExtraState>(1U, 1U)[0].value = 901U;
            range.SetLocal(1U, kb::scene::Vec3{ 9.0F, 0.0F, 0.0F }, range.Get(1U).localRotation, range.Get(1U).localScale);
            writes.fetch_add(1U, std::memory_order_relaxed);
            throw std::runtime_error("parallel extra body failed");
        }));
    } catch (const std::runtime_error&) {
        rethrown = true;
    }
    std::size_t dirtyRows = 0U;
    std::size_t valueWrites = 0U;
    for (const auto& record : fixture.records) {
        const std::size_t dirty = fixture.storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, fixture.extraId);
        const auto* states = static_cast<const ExtraState*>(record.fieldComponents[0]);
        kb::tests::Require(dirty <= 1U && (dirty == 0U || states[1].value == 901U),
            "A parallel body exception must retain exactly its declared extra row");
        dirtyRows += dirty;
        if (states[1].value == 901U) {
            ++valueWrites;
            kb::tests::Require(fixture.scene.Transforms().Get(kb::scene::SceneEntity{ record.entityIds[1] }).worldPosition.x == 9.0F,
                "A parallel body exception lost its completed world transform write");
        }
    }
    const std::size_t completedWrites = writes.load(std::memory_order_relaxed);
    kb::tests::Require(rethrown && completedWrites > 0U && dirtyRows == completedWrites && valueWrites == completedWrites && notifications == completedWrites,
        "A parallel body exception must publish all writes already completed by any worker");
}

void RunAliasedExtraColumnTest() {
    ExtraFixture fixture;
    fixture.Spawn(7U);
    std::cout << "aliases: fixture ready" << std::endl;
    fixture.ClearDirty();
    std::size_t notifications = 0U;
    kb::tests::Require(fixture.world.ObserveComponent<ExtraState>(kb::ecs::ComponentEventKind::Modified, &ObserveExtra, &notifications) != 0U,
        "Aliased extra-column observer registration failed");
    std::cout << "aliases: observer ready" << std::endl;
    static_cast<void>(fixture.scene.Transforms().ParallelForEachRoot<ExtraState, ExtraState, ExtraReadState>(1U, [](kb::scene::TransformRowRange& range) {
        std::cout << "aliases: partial body entered" << std::endl;
        kb::tests::Require(range.ReadColumn<ExtraState>(0U) == range.ReadColumn<ExtraState>(1U),
            "Duplicate extra-component columns must expose the same native data");
        kb::tests::Require(range.ReadColumn<ExtraReadState>(2U)[0].value == 91U,
            "Expanding duplicate aliases must preserve a later distinct component column");
        range.WriteColumn<ExtraState>(1U, 2U, 0U)[0].value = 1001U;
        auto* overlap = range.WriteColumn<ExtraState>(2U, 2U, 1U);
        overlap[0].value = 1002U;
        overlap[1].value = 1003U;
        std::cout << "aliases: partial body complete" << std::endl;
    }));
    std::cout << "aliases: partial published" << std::endl;
    const auto& record = fixture.records[0];
    kb::tests::Require(notifications == 3U &&
            fixture.storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, fixture.extraId) == 3U,
        "Aliased extra columns must publish each component row once across overlapping aliases");
    fixture.ClearDirty();
    notifications = 0U;
    std::cout << "aliases: full pass starts" << std::endl;
    static_cast<void>(fixture.scene.Transforms().ParallelForEachRoot<ExtraState, ExtraState>(1U, [](kb::scene::TransformRowRange& range) {
        auto* first = range.Column<ExtraState>(0U);
        auto* second = range.Column<ExtraState>(1U);
        for (std::size_t row = 0U; row < range.Count(); ++row) {
            first[row].value = 1101U;
            second[row].value = 1102U;
        }
    }));
    std::cout << "aliases: full published" << std::endl;
    kb::tests::Require(notifications == fixture.objects.size() && fixture.world.TryGet<ExtraState>(fixture.objects[0].Entity())->value == 1102U,
        "Whole mutable aliases must publish each row once with its final value");
    fixture.Spawn(33U);
    notifications = 0U;
    const auto appended = fixture.scene.Transforms().ParallelForEachRoot<ExtraState, ExtraState>(1U, [](kb::scene::TransformRowRange& range) {
        auto* states = range.Column<ExtraState>(1U);
        for (std::size_t row = 0U; row < range.Count(); ++row) states[row].value = 1201U;
    });
    kb::tests::Require(appended.rowsVisited == fixture.objects.size() && notifications == fixture.objects.size() &&
            fixture.world.TryGet<ExtraState>(fixture.objects.back().Entity())->value == 1201U,
        "Aliased extra-column records must refresh correctly after appended rows and chunks");
}

void RunRawEntryPointContractTest() {
    ExtraFixture fixture;
    fixture.Spawn(7U);
    fixture.ClearDirty();
    std::size_t notifications = 0U;
    kb::tests::Require(fixture.world.ObserveComponent<ExtraState>(kb::ecs::ComponentEventKind::Modified, &ObserveExtra, &notifications) != 0U,
        "Raw extra-column contract observer registration failed");
    const std::array<kb::ecs::ComponentId, 1U> ids{ fixture.extraId };
    const auto readOnly = [](kb::scene::TransformRowRange& range, void*) {
        kb::tests::Require(range.Column<const ExtraState>() != nullptr, "Raw callback must expose its read-only extra column");
    };
    const auto version = fixture.storage.ComponentVersion(fixture.objects[0].Entity(), fixture.extraId);
    static_cast<void>(fixture.scene.Transforms().ParallelForEachRootDeclared(1U, ids, readOnly, nullptr));
    kb::tests::Require(notifications == 0U && fixture.storage.ComponentVersion(fixture.objects[0].Entity(), fixture.extraId) == version,
        "Access-aware raw callbacks must respect read-only declarations");
    static_cast<void>(fixture.scene.Transforms().ParallelForEachRoot(1U, ids, readOnly, nullptr));
    kb::tests::Require(notifications == fixture.objects.size() && fixture.storage.ComponentVersion(fixture.objects[0].Entity(), fixture.extraId) > version,
        "The original raw callback entry point must conservatively publish Extra intent for prebuilt callbacks");
}

void CountTransformEvent(kb::ecs::Entity, kb::ecs::ComponentEventKind event, const kb::scene::TransformComponent* transform, void* context) {
    if (event == kb::ecs::ComponentEventKind::Modified && transform != nullptr) ++*static_cast<std::size_t*>(context);
}

void RunTransformAliasTest() {
    for (bool observed : { false, true }) {
        ExtraFixture fixture;
        fixture.Spawn(7U);
        kb::tests::Require(fixture.scene.Hierarchy().SetParent(fixture.objects[2].Entity(), fixture.objects[1].Entity()),
            "Transform alias test could not parent a row");
        static_cast<void>(fixture.scene.Runtime().Update(1.0F / 60.0F));
        const auto transformId = fixture.world.Component<kb::scene::TransformComponent>();
        const auto root = fixture.objects[0].Entity();
        const auto parent = fixture.objects[1].Entity();
        const auto child = fixture.objects[2].Entity();
        const auto rootVersion = fixture.scene.Transforms().Get(root).localVersion;
        const auto tableVersion = fixture.storage.ComponentVersion(root, transformId);
        std::size_t notifications = 0U;
        if (observed) {
            kb::tests::Require(fixture.world.ObserveComponent<kb::scene::TransformComponent>(kb::ecs::ComponentEventKind::Modified, &CountTransformEvent, &notifications) != 0U,
                "Transform alias observer registration failed");
        }
        const auto read = fixture.scene.Transforms().ParallelForEachRoot<kb::scene::TransformComponent>(1U, [](kb::scene::TransformRowRange& range) {
            const auto* transforms = range.Column<const kb::scene::TransformComponent>();
            kb::tests::Require(transforms == &range.Get(0U), "A transform extra must alias the range's transform column");
        });
        kb::tests::Require(read.rowsWritten == 0U && notifications == 0U && fixture.storage.ComponentVersion(root, transformId) == tableVersion,
            "A read-only transform alias must not publish a mutation");

        const auto stats = fixture.scene.Transforms().ParallelForEachRoot<kb::scene::TransformComponent, kb::scene::TransformComponent>(1U, [](kb::scene::TransformRowRange& range) {
            range.WriteColumn<kb::scene::TransformComponent>(0U, 1U, 0U)[0].localPosition.x = 12.0F;
            range.WriteColumn<kb::scene::TransformComponent>(0U, 3U, 1U)[0].localPosition.x = 13.0F;
            auto* transforms = range.WriteColumn<kb::scene::TransformComponent>(1U, 2U, 0U);
            transforms[0].localPosition.x = 20.0F;
            transforms[1].localPosition.x = 3.0F;
        });
        kb::tests::Require(stats.rowsWritten == 0U && stats.rowsDeferred == 0U &&
                fixture.scene.Transforms().Get(root).localVersion == rootVersion + 1U && notifications == (observed ? 3U : 0U),
            "Mutable transform aliases must declare one normal mutation per row while keeping explicit SetLocal stats");
        if (!observed) {
            kb::tests::Require(fixture.scene.Transforms().Get(root).worldPosition.x == 13.0F && !fixture.scene.Transforms().Get(root).worldDirty,
                "A mutable root transform alias must compose its current local TRS before returning");
        }
        static_cast<void>(fixture.scene.Runtime().Update(1.0F / 60.0F));
        kb::tests::Require(fixture.scene.Transforms().Get(root).worldPosition.x == 13.0F &&
                fixture.scene.Transforms().Get(parent).worldPosition.x == 20.0F && fixture.scene.Transforms().Get(child).worldPosition.x == 23.0F,
            "Mutable transform aliases must preserve root, parent and child world transforms through sync");

        notifications = 0U;
        const auto before = fixture.scene.Transforms().Get(root).localVersion;
        const auto combined = fixture.scene.Transforms().ParallelForEachRoot<kb::scene::TransformComponent>(1U, [](kb::scene::TransformRowRange& range) {
            range.SetLocal(0U, kb::scene::Vec3{ 21.0F, 0.0F, 0.0F }, range.Get(0U).localRotation, range.Get(0U).localScale);
            range.WriteColumn<kb::scene::TransformComponent>(0U, 1U)[0].localPosition.x = 22.0F;
        });
        kb::tests::Require(combined.rowsWritten == 1U && fixture.scene.Transforms().Get(root).localVersion == before + 2U &&
                notifications == (observed ? 2U : 0U),
            "A mutable transform alias after SetLocal is a further mutation intent without changing explicit write stats");
        static_cast<void>(fixture.scene.Runtime().Update(1.0F / 60.0F));
        kb::tests::Require(fixture.scene.Transforms().Get(root).worldPosition.x == 22.0F,
            "A transform alias must reconcile local changes made after SetLocal");

        notifications = 0U;
        bool rethrown = false;
        try {
            static_cast<void>(fixture.scene.Transforms().ParallelForEachRoot<kb::scene::TransformComponent>(1U, [](kb::scene::TransformRowRange& range) {
                range.WriteColumn<kb::scene::TransformComponent>(0U, 1U)[0].localPosition.x = 31.0F;
                throw std::runtime_error("transform alias body failed");
            }));
        } catch (const std::runtime_error&) {
            rethrown = true;
        }
        kb::tests::Require(rethrown && notifications == (observed ? 1U : 0U),
            "A transform alias must publish its mutation before rethrowing the body's exception");
        static_cast<void>(fixture.scene.Runtime().Update(1.0F / 60.0F));
        kb::tests::Require(fixture.scene.Transforms().Get(root).worldPosition.x == 31.0F,
            "A transform alias body exception must retain its final local/world result");
    }
}

struct StructuralObserverContext {
    kb::ecs::World* world = nullptr;
    kb::ecs::Entity migrated;
    kb::ecs::Entity destroyed;
    std::size_t notifications = 0U;
    bool changedStructure = false;
};

void ObserveAndChangeStructure(kb::ecs::Entity, kb::ecs::ComponentEventKind event, const ExtraState* state, void* context) {
    auto& observed = *static_cast<StructuralObserverContext*>(context);
    if (event != kb::ecs::ComponentEventKind::Modified || state == nullptr) return;
    kb::tests::Require(state->value == 801U, "An extra observer must read the final native value after migration");
    ++observed.notifications;
    if (!observed.changedStructure) {
        observed.changedStructure = true;
        observed.world->Set(observed.migrated, PartitionState{});
        observed.world->DestroyEntity(observed.destroyed);
    }
}

void RunObserverStructureChangeTest() {
    ExtraFixture fixture;
    fixture.CreateThreeChunksAndTail();
    StructuralObserverContext observed{ .world = &fixture.world,
        .migrated = fixture.objects[1].Entity(), .destroyed = fixture.objects[2].Entity() };
    kb::tests::Require(fixture.world.ObserveComponent<ExtraState>(kb::ecs::ComponentEventKind::Modified, &ObserveAndChangeStructure, &observed) != 0U,
        "Structural extra-column observer registration failed");
    static_cast<void>(fixture.scene.Transforms().ParallelForEachRoot<ExtraState>(1U, [](kb::scene::TransformRowRange& range) {
        auto* states = range.Column<ExtraState>();
        for (std::size_t row = 0U; row < range.Count(); ++row) states[row].value = 801U;
    }));
    kb::tests::Require(observed.changedStructure && observed.notifications == fixture.objects.size() - 1U &&
            !fixture.world.IsAlive(observed.destroyed) && fixture.world.TryGet<ExtraState>(observed.migrated)->value == 801U,
        "Extra publication must retain entity identities after an observer migrates and destroys later rows");
}

template<std::size_t Bit>
struct ArenaPartitionMarker {
    std::uint32_t value = static_cast<std::uint32_t>(Bit);
};

struct ArenaLifetimeContext {
    ExtraFixture* fixture = nullptr;
    kb::ecs::ComponentId transformId = 0U;
    kb::ecs::ComponentId partitionId = 0U;
    std::size_t outerRecordCount = 0U;
    std::size_t nestedCount = 0U;
    std::size_t nestedBodies = 0U;
    bool entered = false;
    bool nestedRethrown = false;
    std::vector<kb::scene::TransformComponent> initial;
    std::vector<std::array<std::uint64_t, 4U>> initialNative;
    std::vector<std::array<std::uint64_t, 4U>> migratedNative;
    std::vector<std::array<std::size_t, 3U>> events;
    std::vector<std::array<std::uint32_t, 2U>> lastExtra;
    std::vector<float> lastTransformX;
    std::vector<std::uint32_t> lastTransformLocalVersion;
    std::vector<std::uint8_t> nestedRows;
    std::vector<std::uint8_t> selectedTable;
    std::vector<std::size_t> tableRows;

    std::size_t Index(kb::ecs::Entity entity) const {
        const auto& objects = fixture->objects;
        const auto found = std::find_if(objects.begin(), objects.end(), [entity](const auto& object) {
            return object.Entity() == entity;
        });
        kb::tests::Require(found != objects.end(), "Arena lifetime publication delivered an unknown entity");
        return static_cast<std::size_t>(found - objects.begin());
    }

    std::array<std::uint64_t, 4U> NativeVersions(std::size_t index) const {
        const auto entity = fixture->objects[index].Entity();
        return { fixture->storage.ComponentVersion(entity, fixture->extraId),
            fixture->storage.ComponentVersion(entity, fixture->readId),
            fixture->storage.ComponentVersion(entity, transformId),
            fixture->world.Has<PartitionState>(entity) ? fixture->storage.ComponentVersion(entity, partitionId) : 0U };
    }
};

void RunNestedArenaPasses(ArenaLifetimeContext& observed) {
    ExtraFixture& fixture = *observed.fixture;
    const std::size_t count = fixture.objects.size();
    // Every outer native component's metadata must be complete before the first observer runs.
    for (std::size_t index = 0U; index < count; ++index) {
        const auto entity = fixture.objects[index].Entity();
        const auto transform = fixture.scene.Transforms().Get(entity);
        const auto versions = observed.NativeVersions(index);
        kb::tests::Require(fixture.world.TryGet<ExtraState>(entity)->value == 10000U + index &&
                fixture.world.TryGet<ExtraReadState>(entity)->value == 20000U + index &&
                transform.localPosition.x == 13.0F && transform.localVersion == observed.initial[index].localVersion + 1U &&
                transform.worldVersion == observed.initial[index].worldVersion && transform.worldDirty &&
                versions[0] > observed.initialNative[index][0] && versions[1] > observed.initialNative[index][1] &&
                versions[2] > observed.initialNative[index][2],
            "Arena lifetime outer publication began before all native values and metadata were ready");
    }

    // Split four outer chunks into eight schemas. Native migration happens immediately, even when
    // backend observer delivery is deferred, and invalidates the outer cached query's row pointers.
    for (std::size_t index = 0U; index < count; ++index) {
        const auto entity = fixture.objects[index].Entity();
        fixture.world.Set(entity, PartitionState{ static_cast<std::uint32_t>(index) });
        if ((index & 1U) != 0U) fixture.world.Set(entity, ArenaPartitionMarker<0U>{});
        if ((index & 2U) != 0U) fixture.world.Set(entity, ArenaPartitionMarker<1U>{});
        if ((index & 4U) != 0U) fixture.world.Set(entity, ArenaPartitionMarker<2U>{});
    }
    observed.partitionId = fixture.world.Component<PartitionState>();
    const std::array<kb::ecs::ComponentId, 3U> ids{ fixture.readId, fixture.extraId, observed.partitionId };
    std::vector<kb::ecs::QueryTableDispatchRecord> nestedRecords;
    fixture.storage.CollectQueryRecords(ids, {}, {}, nestedRecords);
    kb::tests::Require(nestedRecords.size() >= 8U && nestedRecords.size() > observed.outerRecordCount,
        "Arena lifetime migration must grow the nested query's record and task counts");
    observed.nestedCount = nestedRecords[0].entityCount;
    for (std::size_t row = 0U; row < observed.nestedCount; ++row) {
        observed.nestedRows[observed.Index(kb::ecs::Entity{ nestedRecords[0].entityIds[row] })] = 1U;
    }
    for (const auto& record : nestedRecords) {
        std::size_t tableRows = 0U;
        for (const auto& sameTable : nestedRecords) {
            if (sameTable.nativeArchetypeIndex == record.nativeArchetypeIndex) tableRows += sameTable.entityCount;
        }
        for (std::size_t row = 0U; row < record.entityCount; ++row) {
            const auto index = observed.Index(kb::ecs::Entity{ record.entityIds[row] });
            observed.tableRows[index] = tableRows;
            observed.selectedTable[index] = static_cast<std::uint8_t>(record.nativeArchetypeIndex == nestedRecords[0].nativeArchetypeIndex);
        }
    }
    for (std::size_t index = 0U; index < count; ++index) observed.migratedNative[index] = observed.NativeVersions(index);

    // A single task makes the exception's exact first-record coverage independent of scheduling.
    try {
        static_cast<void>(fixture.scene.Transforms().ParallelForEachRoot<ExtraReadState, ExtraState, PartitionState>(
            count * nestedRecords.size(), [&observed](kb::scene::TransformRowRange& range) {
                ++observed.nestedBodies;
                kb::tests::Require(observed.nestedBodies == 1U && range.Count() == observed.nestedCount,
                    "Arena lifetime exception must visit exactly its first nested record");
                auto* reads = range.Column<ExtraReadState>(0U);
                auto* extras = range.Column<ExtraState>(1U);
                const auto* partitions = range.ReadColumn<PartitionState>(2U);
                for (std::size_t row = 0U; row < range.Count(); ++row) {
                    const auto index = observed.Index(range.Entity(row));
                    kb::tests::Require(observed.nestedRows[index] != 0U && partitions[row].value == index,
                        "Arena lifetime nested range lost its exact entity or partition value");
                    extras[row].value = 33300U + static_cast<std::uint32_t>(index);
                    reads[row].value = 44400U + static_cast<std::uint32_t>(index);
                    range.SetLocal(row, kb::scene::Vec3{ 29.0F, 0.0F, 0.0F }, range.Get(row).localRotation, range.Get(row).localScale);
                }
                throw std::runtime_error("arena nested sentinel");
            }));
    } catch (const std::runtime_error& exception) {
        if (std::string_view{ exception.what() } != "arena nested sentinel") throw;
        observed.nestedRethrown = true;
    }
    kb::tests::Require(observed.nestedRethrown && observed.nestedBodies == 1U,
        "Arena lifetime nested exception was not published and rethrown");
    for (std::size_t index = 0U; index < count; ++index) {
        const auto versions = observed.NativeVersions(index);
        const std::uint64_t delta = observed.selectedTable[index] != 0U ? 1U + observed.nestedCount : 0U;
        for (std::size_t component = 0U; component < 3U; ++component) {
            kb::tests::Require(versions[component] == observed.migratedNative[index][component] + delta,
                "Arena lifetime nested exception published an incorrect component version increment");
        }
        kb::tests::Require(versions[3] == observed.migratedNative[index][3],
            "Arena lifetime nested exception wrote its read-only partition component");
    }

    // Change both the plan key and task count while the outer output/deferred lists are still live.
    // This successful nested read must clear its own declarations, without clearing the outer arena.
    const auto read = fixture.scene.Transforms().ParallelForEachRoot<PartitionState>(1U, [&observed](kb::scene::TransformRowRange& range) {
        const auto* partitions = range.Column<const PartitionState>();
        for (std::size_t row = 0U; row < range.Count(); ++row) {
            kb::tests::Require(partitions[row].value == observed.Index(range.Entity(row)),
                "Arena lifetime nested recovery read used stale query records");
        }
    });
    kb::tests::Require(read.rowsVisited == count && read.rowsWritten == 0U && read.rowsDeferred == 0U,
        "Arena lifetime nested recovery read retained an exception's transform writes");
    for (std::size_t index = 0U; index < count; ++index) {
        const auto versions = observed.NativeVersions(index);
        const std::uint64_t delta = observed.selectedTable[index] != 0U ? 1U + observed.nestedCount : 0U;
        for (std::size_t component = 0U; component < 4U; ++component) {
            kb::tests::Require(versions[component] == observed.migratedNative[index][component] + (component < 3U ? delta : 0U),
                "Arena lifetime nested recovery read published stale component declarations");
        }
    }
}

void ObserveArenaExtra(kb::ecs::Entity entity, kb::ecs::ComponentEventKind event, const ExtraState* state, void* context) {
    if (event != kb::ecs::ComponentEventKind::Modified || state == nullptr) return;
    auto& observed = *static_cast<ArenaLifetimeContext*>(context);
    const std::uint32_t value = state->value; // Never retain a backend pointer across the nested migration.
    const auto index = observed.Index(entity);
    kb::tests::Require(value == observed.fixture->world.TryGet<ExtraState>(entity)->value,
        "Arena lifetime extra observer received a stale native value");
    ++observed.events[index][0];
    observed.lastExtra[index][0] = value;
    if (!observed.entered) {
        observed.entered = true;
        RunNestedArenaPasses(observed);
    }
}

void ObserveArenaReadExtra(kb::ecs::Entity entity, kb::ecs::ComponentEventKind event, const ExtraReadState* state, void* context) {
    if (event != kb::ecs::ComponentEventKind::Modified || state == nullptr) return;
    auto& observed = *static_cast<ArenaLifetimeContext*>(context);
    const auto index = observed.Index(entity);
    const std::uint32_t value = state->value;
    kb::tests::Require(value == observed.fixture->world.TryGet<ExtraReadState>(entity)->value,
        "Arena lifetime second extra observer received a stale native value");
    ++observed.events[index][1];
    observed.lastExtra[index][1] = value;
}

void ObserveArenaTransform(kb::ecs::Entity entity, kb::ecs::ComponentEventKind event, const kb::scene::TransformComponent* state, void* context) {
    if (event != kb::ecs::ComponentEventKind::Modified || state == nullptr) return;
    auto& observed = *static_cast<ArenaLifetimeContext*>(context);
    const auto index = observed.Index(entity);
    const auto current = observed.fixture->scene.Transforms().Get(entity);
    kb::tests::Require(state->localPosition.x == current.localPosition.x && state->localVersion == current.localVersion,
        "Arena lifetime transform observer received stale local metadata");
    ++observed.events[index][2];
    observed.lastTransformX[index] = state->localPosition.x;
    observed.lastTransformLocalVersion[index] = state->localVersion;
}

void RunObserverNestedArenaLifetimeTest() {
    ExtraFixture fixture;
    fixture.CreateThreeChunksAndTail();
    fixture.scene.Runtime().SynchronizeTransforms();
    const std::size_t count = fixture.objects.size();
    ArenaLifetimeContext observed{ .fixture = &fixture,
        .transformId = fixture.world.Component<kb::scene::TransformComponent>(),
        .partitionId = fixture.world.Component<PartitionState>(), .outerRecordCount = fixture.records.size() };
    observed.initial.resize(count);
    observed.initialNative.resize(count);
    observed.migratedNative.resize(count);
    observed.events.resize(count);
    observed.lastExtra.resize(count);
    observed.lastTransformX.resize(count);
    observed.lastTransformLocalVersion.resize(count);
    observed.nestedRows.resize(count, 0U);
    observed.selectedTable.resize(count, 0U);
    observed.tableRows.resize(count, 0U);
    const auto extraObserver = fixture.world.ObserveComponent<ExtraState>(kb::ecs::ComponentEventKind::Modified, &ObserveArenaExtra, &observed);
    const auto readObserver = fixture.world.ObserveComponent<ExtraReadState>(kb::ecs::ComponentEventKind::Modified, &ObserveArenaReadExtra, &observed);
    const auto transformObserver = fixture.world.ObserveComponent<kb::scene::TransformComponent>(kb::ecs::ComponentEventKind::Modified, &ObserveArenaTransform, &observed);
    kb::tests::Require(extraObserver != 0U && readObserver != 0U && transformObserver != 0U,
        "Arena lifetime observer registration failed");
    for (std::size_t index = 0U; index < count; ++index) {
        observed.initial[index] = fixture.scene.Transforms().Get(fixture.objects[index]);
        observed.initialNative[index] = observed.NativeVersions(index);
    }
    const auto outer = fixture.scene.Transforms().ParallelForEachRoot<ExtraState, ExtraReadState>(1U, [&observed](kb::scene::TransformRowRange& range) {
        auto* extras = range.Column<ExtraState>(0U);
        auto* reads = range.Column<ExtraReadState>(1U);
        for (std::size_t row = 0U; row < range.Count(); ++row) {
            const auto index = observed.Index(range.Entity(row));
            extras[row].value = 10000U + static_cast<std::uint32_t>(index);
            reads[row].value = 20000U + static_cast<std::uint32_t>(index);
            range.SetLocal(row, kb::scene::Vec3{ 13.0F, 0.0F, 0.0F }, range.Get(row).localRotation, range.Get(row).localScale);
        }
    });
    kb::tests::Require(observed.entered && observed.nestedRethrown && outer.rowsWritten == count && outer.rowsDeferred == count,
        "Arena lifetime outer pass lost its deferred output or did not enter the nested pass");
    // Flecs may defer nested OnSet delivery until the first outer callback returns. Count complete
    // per-entity coverage here, without depending on callback ordering or the delivery phase.
    for (std::size_t index = 0U; index < count; ++index) {
        const auto entity = fixture.objects[index].Entity();
        const bool nested = observed.nestedRows[index] != 0U;
        const std::uint32_t expectedExtra = (nested ? 33300U : 10000U) + static_cast<std::uint32_t>(index);
        const std::uint32_t expectedRead = (nested ? 44400U : 20000U) + static_cast<std::uint32_t>(index);
        const std::size_t expectedEvents = 1U + static_cast<std::size_t>(nested);
        const auto transform = fixture.scene.Transforms().Get(entity);
        const auto versions = observed.NativeVersions(index);
        kb::tests::Require(observed.events[index] == std::array<std::size_t, 3U>{ expectedEvents, expectedEvents, expectedEvents } &&
                fixture.world.TryGet<ExtraState>(entity)->value == expectedExtra && fixture.world.TryGet<ExtraReadState>(entity)->value == expectedRead &&
                observed.lastExtra[index] == std::array<std::uint32_t, 2U>{ expectedExtra, expectedRead } &&
                transform.localPosition.x == (nested ? 29.0F : 13.0F) && transform.localVersion == observed.initial[index].localVersion + expectedEvents &&
                transform.worldVersion == observed.initial[index].worldVersion && transform.worldDirty && transform.parentVersion == 0U &&
                observed.lastTransformX[index] == transform.localPosition.x && observed.lastTransformLocalVersion[index] == transform.localVersion,
            "Arena lifetime outer/nested publication lost an entity event, value or logical version");
        const std::uint64_t delta = observed.tableRows[index] + (observed.selectedTable[index] != 0U ? 1U + observed.nestedCount : 0U);
        for (std::size_t component = 0U; component < 4U; ++component) {
            kb::tests::Require(versions[component] == observed.migratedNative[index][component] + (component < 3U ? delta : 0U),
                "Arena lifetime outer/nested publication lost or repeated a component version increment");
        }
    }

    fixture.world.DestroyObserver(transformObserver);
    fixture.scene.Runtime().SynchronizeTransforms();
    fixture.RefreshRecords();
    fixture.ClearDirty();
    const auto eventBaseline = observed.events;
    std::vector<std::array<std::uint64_t, 4U>> versionBaseline(count);
    std::vector<kb::scene::TransformComponent> transformBaseline(count);
    for (std::size_t index = 0U; index < count; ++index) {
        versionBaseline[index] = observed.NativeVersions(index);
        transformBaseline[index] = fixture.scene.Transforms().Get(fixture.objects[index]);
        kb::tests::Require(!transformBaseline[index].worldDirty &&
                transformBaseline[index].worldPosition.x == transformBaseline[index].localPosition.x &&
                transformBaseline[index].worldVersion == observed.initial[index].worldVersion + 1U,
            "Arena lifetime deferred roots did not synchronize once with their final local values");
    }
    const auto read = fixture.scene.Transforms().ParallelForEachRoot<ExtraState, ExtraReadState>(1U, [&observed](kb::scene::TransformRowRange& range) {
        const auto* extras = range.Column<const ExtraState>(0U);
        const auto* reads = range.Column<const ExtraReadState>(1U);
        for (std::size_t row = 0U; row < range.Count(); ++row) {
            const auto index = observed.Index(range.Entity(row));
            kb::tests::Require(extras[row].value == observed.lastExtra[index][0] && reads[row].value == observed.lastExtra[index][1],
                "Arena lifetime later read used stale query rows");
        }
    });
    kb::tests::Require(read.rowsVisited == count && read.rowsWritten == 0U && read.rowsDeferred == 0U && observed.events == eventBaseline,
        "Arena lifetime retained primary outputs were not reset before a later read-only pass");
    for (std::size_t index = 0U; index < count; ++index) {
        kb::tests::Require(observed.NativeVersions(index) == versionBaseline[index],
            "Arena lifetime later read replayed stale component publication");
    }
    for (const auto& record : fixture.records) {
        kb::tests::Require(fixture.storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, fixture.extraId) == 0U &&
                fixture.storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, fixture.readId) == 0U,
            "Arena lifetime later read replayed old extra dirty rows");
    }

    const auto untouched = std::find(observed.nestedRows.begin(), observed.nestedRows.end(), std::uint8_t{ 0U });
    kb::tests::Require(untouched != observed.nestedRows.end(), "Arena lifetime recovery needs a row outside the exception's coverage");
    const std::size_t target = static_cast<std::size_t>(untouched - observed.nestedRows.begin());
    std::size_t targetArchetype = std::numeric_limits<std::size_t>::max();
    for (const auto& record : fixture.records) {
        for (std::size_t row = 0U; row < record.entityCount; ++row) {
            if (kb::ecs::Entity{ record.entityIds[row] } == fixture.objects[target].Entity()) targetArchetype = record.nativeArchetypeIndex;
        }
    }
    const auto recovered = fixture.scene.Transforms().ParallelForEachRoot<ExtraState, ExtraReadState>(1U, [&observed, target](kb::scene::TransformRowRange& range) {
        static_cast<void>(range.ReadColumn<ExtraReadState>(1U));
        for (std::size_t row = 0U; row < range.Count(); ++row) {
            if (range.Entity(row) != observed.fixture->objects[target].Entity()) continue;
            range.WriteColumn<ExtraState>(row, 1U)[0].value = 60600U + static_cast<std::uint32_t>(target);
            range.SetLocal(row, kb::scene::Vec3{ 47.0F, 0.0F, 0.0F }, range.Get(row).localRotation, range.Get(row).localScale);
        }
    });
    kb::tests::Require(recovered.rowsWritten == 1U && recovered.rowsDeferred == 0U,
        "Arena lifetime successful recovery pass retained stale transform outputs");
    for (const auto& record : fixture.records) {
        bool containsTarget = false;
        for (std::size_t row = 0U; row < record.entityCount; ++row) {
            const auto index = observed.Index(kb::ecs::Entity{ record.entityIds[row] });
            const bool isTarget = index == target;
            containsTarget = containsTarget || isTarget;
            const bool sameTable = record.nativeArchetypeIndex == targetArchetype;
            const auto versions = observed.NativeVersions(index);
            const auto transform = fixture.scene.Transforms().Get(fixture.objects[index]);
            auto expectedEvents = eventBaseline[index];
            if (isTarget) ++expectedEvents[0];
            kb::tests::Require(observed.events[index] == expectedEvents &&
                    fixture.world.TryGet<ExtraState>(fixture.objects[index].Entity())->value == (isTarget ? 60600U + target : observed.lastExtra[index][0]) &&
                    fixture.world.TryGet<ExtraReadState>(fixture.objects[index].Entity())->value == observed.lastExtra[index][1] &&
                    transform.localVersion == transformBaseline[index].localVersion + static_cast<std::size_t>(isTarget) &&
                    transform.worldVersion == transformBaseline[index].worldVersion + static_cast<std::size_t>(isTarget) &&
                    transform.worldPosition.x == (isTarget ? 47.0F : transformBaseline[index].worldPosition.x) && !transform.worldDirty &&
                    versions[0] == versionBaseline[index][0] + (sameTable ? 2U : 0U) && versions[1] == versionBaseline[index][1] &&
                    versions[2] == versionBaseline[index][2] + (sameTable ? 1U : 0U) && versions[3] == versionBaseline[index][3],
                "Arena lifetime successful recovery published anything beyond its one declared row");
        }
        kb::tests::Require(fixture.storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, fixture.extraId) == (containsTarget ? 1U : 0U) &&
                fixture.storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, fixture.readId) == 0U,
            "Arena lifetime successful recovery leaked old full or partial extra declarations");
    }
    fixture.world.DestroyObserver(extraObserver);
    fixture.world.DestroyObserver(readObserver);
}

} // namespace

int main(int argc, char** argv) {
    const auto run = [argc, argv](std::string_view name, auto&& test) {
        if (argc > 1 && std::string_view{ argv[1] } != name) return;
        std::cout << "START " << name << std::endl;
        try {
            test();
        } catch (const std::exception& exception) {
            std::cerr << "EXCEPTION " << name << ": " << exception.what() << std::endl;
            std::exit(EXIT_FAILURE);
        } catch (...) {
            std::cerr << "EXCEPTION " << name << ": unknown" << std::endl;
            std::exit(EXIT_FAILURE);
        }
        std::cout << "PASS " << name << std::endl;
    };
    run("legacy", &RunLegacyMutableExtraPublicationTest);
    run("unobserved", [] { RunReadPartialRepeatedAndFullTest(false); });
    run("observed", [] { RunReadPartialRepeatedAndFullTest(true); });
    run("exception", &RunExceptionAndInvalidSpanTest);
    run("untouched", &RunUntouchedTransformArchetypeTest);
    run("parallel-exception", &RunParallelExceptionPublicationTest);
    run("aliases", &RunAliasedExtraColumnTest);
    run("transform-alias", &RunTransformAliasTest);
    run("raw-contract", &RunRawEntryPointContractTest);
    run("structure", &RunObserverStructureChangeTest);
    run("arena-lifetime", &RunObserverNestedArenaLifetimeTest);
    std::cout << "Scene transform extra column tests passed" << std::endl;
}
