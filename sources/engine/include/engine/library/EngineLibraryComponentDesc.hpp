#pragma once

#include "engine/library/EngineLibraryFunctionDesc.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kb::library {

// A deterministic hash of a component's registered name (FNV-1a 64-bit,
// same algorithm as ComputeLibraryFunctionId, LIB-026) — stable across
// sessions, builds, and registration order, unlike kb::ecs::ComponentId
// (a flecs registration-order index, explicitly documented in
// EngineLibraryExecutionOrder.hpp as "not yet a content-stable id... an
// open, unassigned gap" — this closes that gap for the closed set of
// components LIB-076 actually catalogs, not for every possible kb::ecs
// component type).
using LibraryComponentId = std::uint64_t;

[[nodiscard]] LibraryComponentId ComputeLibraryComponentId(std::string_view name) noexcept;

// Whether scripts may mutate this component (Add<T>/Remove<T>/
// SetProperty<Type>/World.SetProperty<Type>), or only ever read it.
// Every component LIB-076 currently catalogs is ReadWrite — no ReadOnly
// component exists yet, so this enumerator is real forward-compatible
// surface area (LIB-076's own "capability" requirement), not a fabricated
// example.
enum class LibraryComponentCapability : std::uint8_t {
    ReadOnly,
    ReadWrite,
};

// LIB-076: no version concept existed anywhere for components before this
// (unlike LibraryApiVersion for the whole kb::library contract, LIB-004) —
// starts at 1.0 for all six components cataloged today; bump minor for an
// additive field, major for a breaking layout change to a component this
// registry names.
struct LibraryComponentVersion {
    std::uint16_t major = 1U;
    std::uint16_t minor = 0U;

    [[nodiscard]] constexpr bool operator==(const LibraryComponentVersion&) const noexcept = default;
};

// The kb::library-level contract for one component type exposed to
// scripts. Deliberately does NOT duplicate the field schema here: that
// already exists, real and queryable, via
// kb::script::ScriptSceneComponentApi::ComponentProperties(name) (name+
// ScriptValueType+writable per field) — LibraryComponentDesc::name is the
// same key that function takes, so "schema" for a cataloged component
// means calling that function, not a second, parallel field list that
// could drift out of sync with it. Serialization likewise is not modeled
// here as a function pointer or codec reference — `serializable` names
// whether kb::scene's asset I/O layer (SceneAssetComponentCodec and its
// per-component codecs, or SceneAssetPrimitiveCodec for Transform) has a
// real Read/Write path for this component today; the codec itself remains
// owned by kb::scene, not duplicated into kb::library.
struct LibraryComponentDesc {
    std::string name;
    LibraryComponentId id = 0U;
    LibraryComponentVersion version{};
    // Every component this catalog currently lists is main-thread only —
    // the exact same real-world constraint LibraryThreadAffinity's own
    // doc comment already states for functions ("no worker-safe or
    // render-thread-command dispatch path exists yet"). Reused, not
    // duplicated: a component-specific enum would just be
    // MainThread/WorkerSafe/RenderThreadCommand again under a new name.
    LibraryThreadAffinity threadPolicy = LibraryThreadAffinity::MainThread;
    LibraryComponentCapability capability = LibraryComponentCapability::ReadWrite;
    bool serializable = false;
};

// LIB-076: the registry — currently catalogs exactly the six component
// types ScriptSceneComponentApi.cpp's kComponentNames and LIB-075's
// ScriptComponentAccess<T> specializations already expose to scripts
// (Transform/Visibility/Camera/Light/MeshRenderer/Behaviour). Coverage is
// intentionally incremental, the same convention LibraryModuleDesc::
// functions already established (LIB-016): a component NOT listed here
// is simply not yet cataloged, not a claim that it doesn't exist or isn't
// script-visible — kComponentNames/ScriptComponentAccess<T> remain the
// authoritative "is this exposed to scripts at all" answer.
class EngineLibraryComponentRegistry final {
public:
    EngineLibraryComponentRegistry() = delete;

    [[nodiscard]] static const std::vector<LibraryComponentDesc>& Catalog();
    [[nodiscard]] static const LibraryComponentDesc* Find(std::string_view name) noexcept;
};

} // namespace kb::library
