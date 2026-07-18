#pragma once

#include "engine/ecs/Query.hpp"
#include "engine/ecs/QueryExecutionSettings.hpp"
#include "engine/ecs/QueryFilter.hpp"
#include "engine/ecs/StructuralChangeValidator.hpp"
#include "engine/ecs/World.hpp"
#include "engine/library/EngineLibraryEntityHandle.hpp"
#include "engine/library/EngineLibraryExecutionOrder.hpp"
#include "engine/library/EngineLibraryLifecycle.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace kb::library {

// LIB-079: fluent filter/order options for Query<Component>::ForEach.
// With/Without/ChangedSince resolve straight onto kb::ecs::QueryFilter's
// existing Require/Exclude/Changed (LIB-079 does not reinvent filtering —
// QueryFilter already had exactly this shape); the component id for an
// arbitrary type is resolved lazily, once World& is available inside
// ForEach, via World::Component<T>() — never re-registering, just
// looking up the id SceneComponentRegistry already assigned at Scene
// construction (or 0 for a genuinely unregistered type, which
// QueryFilter::Require/Exclude then treat as an always-false/always-true
// degenerate filter rather than crashing).
//
// Any<Components...>() has no kb::ecs-level equivalent (QueryFilter's
// Optional means "may or may not be present," not "at least one of a
// set") — implemented as a real per-entity OR predicate
// (World::Has<T>(entity) || ...) over the given type pack, stored as a
// plain (non-capturing-lambda-derived) function pointer, not a heavier
// std::function. Calling Any<...>() more than once ANDs the resulting
// OR-groups together (each call is its own "must have at least one of
// these" requirement).
//
// Enabled() has no kb::ecs-level equivalent either — kb::ecs has no
// notion of an entity being active/inactive at all. It bridges to
// kb::scene::SceneEntities::IsActive(entity), the flat-set LIB-068 added
// (SceneState::inactiveEntities) — the only "active" concept that exists
// anywhere in this engine today.
//
// StableOrder() maps directly onto the already-fully-implemented
// kb::ecs::QueryIterationOrder::Deterministic (LIB-074 confirmed this
// forces serial, byte-for-byte-repeatable execution) — no new ordering
// logic. Both the stable and default (StorageOrder) paths always force
// QueryExecutionPolicy::SingleThread regardless: this is a script-facing
// wrapper, and every other native script dispatch path in this engine
// runs on the main thread only (LibraryThreadAffinity's own doc comment:
// "no worker-safe... dispatch path exists yet") — StorageOrder is about
// whether swap-remove churn may reorder survivors between calls, NOT an
// invitation to run the visitor from multiple threads at once.
class QueryFilterOptions final {
public:
    template <typename Component>
    QueryFilterOptions& With() {
        with_.push_back(&ResolveComponentId<Component>);
        return *this;
    }

    template <typename Component>
    QueryFilterOptions& Without() {
        without_.push_back(&ResolveComponentId<Component>);
        return *this;
    }

    template <typename Component>
    QueryFilterOptions& ChangedSince() {
        changed_.push_back(&ResolveComponentId<Component>);
        return *this;
    }

    template <typename... AnyComponents>
    QueryFilterOptions& Any() {
        anyPredicates_.push_back([](const kb::ecs::World& world, kb::ecs::Entity entity) noexcept {
            return (world.Has<AnyComponents>(entity) || ...);
        });
        return *this;
    }

    QueryFilterOptions& Enabled() noexcept {
        enabledOnly_ = true;
        return *this;
    }

    QueryFilterOptions& StableOrder() noexcept {
        stableOrder_ = true;
        return *this;
    }

    // ComponentId (LIB-005) — the same kb::ecs::ComponentId, named through
    // kb::library's own alias rather than the raw engine type, so this
    // resolver is a real consumer of the identifier LIB-005 defines instead
    // of leaving it declared-only.
    using ComponentIdResolver = ComponentId (*)(const kb::ecs::World&);
    using AnyPredicate = bool (*)(const kb::ecs::World&, kb::ecs::Entity);

    [[nodiscard]] std::span<const ComponentIdResolver> WithResolvers() const noexcept { return with_; }
    [[nodiscard]] std::span<const ComponentIdResolver> WithoutResolvers() const noexcept { return without_; }
    [[nodiscard]] std::span<const ComponentIdResolver> ChangedResolvers() const noexcept { return changed_; }
    [[nodiscard]] std::span<const AnyPredicate> AnyPredicates() const noexcept { return anyPredicates_; }
    [[nodiscard]] bool EnabledOnlyRequested() const noexcept { return enabledOnly_; }
    [[nodiscard]] bool StableOrderRequested() const noexcept { return stableOrder_; }

private:
    template <typename Component>
    [[nodiscard]] static ComponentId ResolveComponentId(const kb::ecs::World& world) noexcept {
        return world.Component<Component>();
    }

    std::vector<ComponentIdResolver> with_;
    std::vector<ComponentIdResolver> without_;
    std::vector<ComponentIdResolver> changed_;
    std::vector<AnyPredicate> anyPredicates_;
    bool enabledOnly_ = false;
    bool stableOrder_ = false;
};

// LIB-078/079: a script-facing, phase-gated, filterable read-only query
// over one component type registered for scripts. Native C++ script code
// only — Lua/Visual Graph cannot express a C++ template, so they never
// reach this type.
//
// Wraps kb::ecs::World::CreateQuery<Component>(filter) directly (LIB-079
// switched this from LIB-078's original kb::scene-per-type-visitor
// dispatch): every kb::scene named component — INCLUDING
// VisibilityComponent, which LIB-078 had to exclude for lack of a
// kb::scene-level bulk iteration primitive — is confirmed to be a real,
// registered kb::ecs component (SceneComponentRegistry.cpp registers all
// of them via World::RegisterComponent<T>), so this now covers all six
// uniformly. It also eliminates the flecs-C-iteration exception-safety
// hazard LIB-078 had to work around for Camera/Light/MeshRenderer
// (SceneComponentVisitors::ForEachCamera/Light/MeshRenderer cache a raw
// flecs ecs_query_t* under the hood): kb::ecs::Query<T...>'s own
// iteration is pure native-storage C++, never crosses into a C library's
// callback frame, so a visitor's exception propagates through
// ForEachBatchKernel exactly like it would through any other C++ call —
// confirmed by RunLibraryQueryFilterAndOrderTest, no catch/rethrow
// trampoline needed here.
//
// "Only for phases that allow iteration" still reuses LIB-007's
// ClassifyLifecycleContext (Behaviour phases refused, Fixed/Frame/Render
// allowed) — unchanged from LIB-078. "Ban on structural change in the
// loop" still enters kb::ecs::World::EnterIteration() explicitly before
// building the query — ForEachBatchKernel's own QueryState.cpp also
// enters the same guard internally, so this is redundant-but-harmless
// (the guard is an atomic counter, not an exclusive lock — nested entry
// is fine), kept for defense-in-depth and because it must still cover the
// filter-resolution step that runs before the query itself exists.
// LIB-078: the component list is a TYPE PACK, so a script may iterate every
// entity that has ALL of several components in one query and receive each
// component's data in the visitor: Query<TransformComponent,
// LightComponent>::ForEach(scene, event, [](EntityHandle e, const
// TransformComponent& t, const LightComponent& l){ ... }). A single-type
// Query<Transform> is just the one-element pack, so every existing caller
// keeps working unchanged. The multi-component match itself is not
// re-implemented here — kb::ecs::World::CreateQuery<Components...>(filter)
// already builds a real archetype-intersecting query (the same primitive
// kb::ecs::QuerySystem uses), and QueryBatch<Components...> already exposes
// each component column via Components<N>(); this wrapper only adds the
// script-facing phase gate, filter options, and per-entity Any/Enabled
// predicates on top of it.
template <typename... Components>
class Query final {
public:
    static_assert(sizeof...(Components) >= 1, "kb::library::Query needs at least one component type to iterate");

    Query() = delete;

    template <typename Visitor>
    [[nodiscard]] static bool ForEach(kb::scene::Scene& scene, LifecycleEvent event, Visitor&& visitor) {
        return ForEach(scene, event, QueryFilterOptions{}, std::forward<Visitor>(visitor));
    }

    template <typename Visitor>
    [[nodiscard]] static bool ForEach(kb::scene::Scene& scene, LifecycleEvent event, const QueryFilterOptions& options, Visitor&& visitor) {
        if (ClassifyLifecycleContext(event) == LibraryLifecycleContextKind::Behaviour) {
            return false;
        }

        kb::ecs::World& world = scene.Runtime().EcsWorld();
        const kb::ecs::StructuralChangeValidator::Guard iterationGuard = world.EnterIteration();

        kb::ecs::QueryFilter filter;
        for (const QueryFilterOptions::ComponentIdResolver resolve : options.WithResolvers()) {
            filter.Require(resolve(world));
        }
        for (const QueryFilterOptions::ComponentIdResolver resolve : options.WithoutResolvers()) {
            filter.Exclude(resolve(world));
        }
        for (const QueryFilterOptions::ComponentIdResolver resolve : options.ChangedResolvers()) {
            filter.Changed(resolve(world));
        }

        const kb::ecs::Query<Components...> query = world.CreateQuery<Components...>(filter);
        const kb::ecs::QueryExecutionSettings settings{
            .iterationOrder = options.StableOrderRequested() ? kb::ecs::QueryIterationOrder::Deterministic : kb::ecs::QueryIterationOrder::StorageOrder,
            .policy = kb::ecs::QueryExecutionPolicy::SingleThread,
        };

        query.ForEachBatchKernel(settings, [&](const typename kb::ecs::Query<Components...>::Batch& batch) {
            VisitBatch(batch, scene, world, options, visitor, std::index_sequence_for<Components...>{});
        });
        return true;
    }

private:
    // Expands the component-column pack: for each surviving entity in the
    // batch it passes one const reference per queried component to the
    // visitor, in the same order as the type pack. Components<Is>() is the
    // Is-th component's contiguous column for this batch (kb::ecs guarantees
    // one column per queried type), so Components<Is>()[index] is that
    // entity's data for the Is-th component.
    template <typename Visitor, std::size_t... Is>
    static void VisitBatch(
        const typename kb::ecs::Query<Components...>::Batch& batch,
        kb::scene::Scene& scene,
        const kb::ecs::World& world,
        const QueryFilterOptions& options,
        Visitor& visitor,
        std::index_sequence<Is...>) {
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            const kb::ecs::Entity entity = batch.EntityAt(index);
            if (options.EnabledOnlyRequested() && !scene.Entities().IsActive(entity)) {
                continue;
            }
            bool matchesEveryAnyGroup = true;
            for (const QueryFilterOptions::AnyPredicate predicate : options.AnyPredicates()) {
                if (!predicate(world, entity)) {
                    matchesEveryAnyGroup = false;
                    break;
                }
            }
            if (!matchesEveryAnyGroup) {
                continue;
            }
            visitor(EntityHandle{ entity, scene.Id() }, batch.template Components<Is>()[index]...);
        }
    }
};

} // namespace kb::library
