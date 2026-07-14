#pragma once

#include "engine/script/ScriptEventId.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kb::library {

// LIB-108: "wersjonowanie schema" (schema versioning) for event payloads.
// This catalogs ONLY the CLOSED set of events the ENGINE ITSELF emits
// (scene lifecycle/LIB-073, TimerFired/LIB-095, TaskCompleted+TaskFailed/
// LIB-097-098) — every ScriptEvent construction site outside tests was
// grepped before writing this to confirm the list below is exhaustive.
// Deliberately NOT a general-purpose registry for arbitrary game-defined
// events (Events.Emit("MyCustomEvent", ...) can name anything, with any
// arguments, and nothing anywhere declares what a given game's custom
// event even means) — versioning THAT would require a schema-authoring/
// registration system that does not exist anywhere in this engine and is
// not scoped by any other backlog item either; inventing one here would be
// exactly the kind of fabrication this session has repeatedly refused
// (LIB-097/098's yield-reason deferrals, LIB-106's "player" filter
// deferral). What DOES have a real, fixed, versionable shape today is the
// engine's own built-in event set, mirroring LIB-076's
// LibraryComponentDesc/LibraryComponentVersion precedent for the engine's
// closed component catalog.
struct LibraryEventVersion {
    std::uint16_t major = 1U;
    std::uint16_t minor = 0U;

    [[nodiscard]] constexpr bool operator==(const LibraryEventVersion&) const noexcept = default;
};

// One argument's name/type/required-ness. Reuses kb::script::
// ScriptFunctionPin (ScriptFunctionRegistry.hpp) as-is rather than
// inventing a parallel descriptor — the shape a function's input/output
// pin needs (name + ScriptValueType + required) is EXACTLY the shape an
// event argument needs; LibraryTypeDesc/DescribeType (EngineLibraryTypeDesc.
// hpp) already documents what each ScriptValueType means at every boundary
// this engine has.
using LibraryEventArgumentDesc = kb::script::ScriptFunctionPin;

// The kb::library-level contract for one built-in engine event.
// `id`/`ComputeLibraryEventId` deliberately do NOT reimplement a hash —
// they reuse kb::script::EventId/ComputeEventId (ScriptEventId.hpp, LIB-104)
// directly, since kb::library is already free to depend on kb::script (the
// reverse is what's forbidden) and ScriptEvent::Id() already IS the
// canonical dispatch key for this exact `name` — a second, independently
// computed id here would be a parallel source of truth for no benefit.
struct LibraryEventDesc {
    std::string name;
    kb::script::EventId id = 0U;
    LibraryEventVersion version{};
    std::vector<LibraryEventArgumentDesc> arguments;
};

// LIB-108: catalogs the engine's built-in events with their CURRENT payload
// shape and version. Bump `version.minor` for an additive argument, `major`
// for a breaking change to an argument this catalog already names —
// RunEngineLibraryEventSchemaRegistryTest locks in today's shape against
// the REAL dispatch sites (ScriptRuntimeSceneSystem.cpp), so an accidental
// breaking change to a built-in event's argument list fails that test
// instead of silently shipping.
class EngineLibraryEventRegistry final {
public:
    EngineLibraryEventRegistry() = delete;

    [[nodiscard]] static const std::vector<LibraryEventDesc>& Catalog();
    [[nodiscard]] static const LibraryEventDesc* Find(std::string_view name) noexcept;
};

} // namespace kb::library
