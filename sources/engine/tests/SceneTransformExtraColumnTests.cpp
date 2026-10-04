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
    std::cout << "Scene transform extra column tests passed" << std::endl;
}
