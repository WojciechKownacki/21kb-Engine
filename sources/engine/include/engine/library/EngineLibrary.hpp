#pragma once

#include <compare>
#include <cstdint>
#include <string>

// Engine21kbLibrary is the public scripting surface shared by C++ (native
// behaviour), Lua and Visual Graph. `kb::library` is its single namespace:
// every type, function and constant a game, Lua script or Visual Graph asset
// consumes through any of the three frontends is declared under this
// namespace (directly here or in a header included from here), and nowhere
// else. `kb::library` never introduces a second ECS, scheduler or scene; it
// only wraps state already owned by kb::script::ScriptRuntimeHost and the
// runtime systems it registers against (kb::scene, kb::ecs, kb::render,
// kb::audio, kb::input, kb::assets, ...). A symbol belongs in kb::library
// only once it has a real runtime backend plus Native, Lua and Visual Graph
// bindings; see others/Engine21kbLibrary.md for the module-by-module plan
// that grows this namespace (World, Transform, Time, Input, Physics, Audio,
// ...) and engine/script/ScriptRuntimeHost.hpp for the registration point
// each module attaches to.
namespace kb::library {

// Semantic version of the kb::library public contract: the surface a game,
// Lua script or Visual Graph asset is built against. Independent from
// kb::modules::EngineModuleMetadata::version (module load ordering only) and
// from the engine product version.
//
// - major: bumped when a published function/type/property is removed,
//   renamed, or its behaviour changes incompatibly, so Lua scripts or Visual
//   Graph assets built against the previous major may fail to load or run.
// - minor: bumped when the contract only grows (new module, new function,
//   new optional property) without breaking anything that compiled or
//   loaded against an earlier minor of the same major.
// - patch: bumped for a fix that does not change the public contract
//   (corrected diagnostic text, bug fix inside an existing function).
//
// LIB-023/LIB-024 attach a manifest hash derived from the registered
// ScriptApiCatalog to detect accidental breaking changes between builds;
// this struct is the version half of that future manifest.
struct LibraryApiVersion {
    std::uint16_t major = 0U;
    std::uint16_t minor = 0U;
    std::uint16_t patch = 0U;

    [[nodiscard]] constexpr bool operator==(const LibraryApiVersion&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const LibraryApiVersion&) const noexcept = default;

    // True when content built against `required` can run against this
    // version: same major, and this version is at least as new
    // (minor, then patch).
    [[nodiscard]] constexpr bool CanRun(const LibraryApiVersion& required) const noexcept {
        if (major != required.major) {
            return false;
        }
        if (minor != required.minor) {
            return minor > required.minor;
        }
        return patch >= required.patch;
    }
};

// Current kb::library API version. M0 (see "Kolejność realizacji" in
// others/Engine21kbLibrary.md) is still building the vertical slice the plan
// requires before the contract is declared stable. Bump to {1, 0, 0} only
// once M0 is complete and the catalog manifest (LIB-022) can pin
// compatibility to it.
inline constexpr LibraryApiVersion kEngineLibraryApiVersion{ 0U, 1U, 0U };

// Formats as "major.minor.patch", e.g. "0.1.0".
[[nodiscard]] std::string ToString(const LibraryApiVersion& version);

} // namespace kb::library
