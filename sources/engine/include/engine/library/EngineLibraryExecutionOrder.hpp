#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/Entity.hpp"
#include "engine/scene/BehaviourExecutionOrder.hpp"

namespace kb::library {

// kb::library reuses kb::scene::BehaviourTickGroup as-is (no parallel enum):
// it is the coarse group every behaviour dispatch (lifecycle events and
// per-frame ticks) sorts by first.
using TickGroup = kb::scene::BehaviourTickGroup;

// The one guaranteed behaviour execution order: TickGroup ascending, then
// BehaviourComponent::executionOrder ascending, then entity id ascending as
// a deterministic tie-breaker. This is the same comparator
// ScriptRuntime::ExecuteLifecycleAndDispatchEvents (per-tick dispatch) and
// ScriptRuntimeSceneSystem (Created/Activated/Ready/Deactivated/Destroyed
// transitions) both sort with — kb::library does not define a second order,
// it names this one.
using kb::scene::BehaviourExecutionOrderLess;

// The stable identifier the execution-order tie-break (and
// kb::library::EntityHandle, LIB-008) is built on. kb::ecs::Entity::Id()
// packs a flecs index and generation into one 64-bit value, so an id from a
// destroyed entity — even if its index gets recycled by a new entity —
// never compares equal to that new entity's id. Stable for the lifetime of
// the world session; not guaranteed identical across separate runs or
// builds.
using EntityId = kb::ecs::Entity::IdType;

// The identifier a registered component type resolves to today. It is
// stable only within one running process: kb::ecs::ComponentRegistry
// assigns it sequentially the first time each C++ type is registered with
// flecs, so changing registration order changes every ComponentId that
// follows. It is not yet a content-stable id (e.g. a hash of the
// component's canonical name), so it must never be persisted across
// sessions or builds. Unlike kb::assets::AssetId (LIB-009: a deterministic
// hash of the asset's logical path, stable across sessions and builds),
// nothing in the current LIB-xxx plan owns closing this gap for
// ComponentId yet — treat it as an open, unassigned gap, not a promise any
// shipped task has fulfilled. Real consumer: kb::library::Query's
// QueryFilterOptions (EngineLibraryQuery.hpp) resolves this exact type per
// filtered component type, entirely within one process/frame, so the
// process-only stability caveat above never applies to that use.
//
// Not to be confused with kb::library::LibraryComponentId (LIB-076,
// EngineLibraryComponentDesc.hpp) — a DIFFERENT, deliberately narrower type:
// a content-stable FNV-1a hash of a component's name, defined only for the
// closed set of components EngineLibraryComponentRegistry::Catalog() lists.
// This ComponentId is the general, always-available-but-process-only
// identifier every registered component gets; LibraryComponentId is the
// cross-session-stable identifier only the cataloged subset gets.
using ComponentId = kb::ecs::ComponentId;

} // namespace kb::library
