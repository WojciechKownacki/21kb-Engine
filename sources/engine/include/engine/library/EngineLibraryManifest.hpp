#pragma once

#include "engine/library/EngineLibrary.hpp"
#include "engine/library/EngineLibraryApiSurface.hpp"
#include "engine/script/ScriptApiCatalog.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace kb::library {

// A deterministic FNV-1a 64-bit hash (16 lowercase hex digits) of arbitrary
// manifest content. Used to detect when the registered API surface changes
// between builds without diffing full JSON/Markdown text by hand — LIB-024
// compares this across builds in CI.
[[nodiscard]] std::string ComputeApiManifestHash(std::string_view content) noexcept;

// The kb::library API manifest: the current LibraryApiVersion (LIB-001)
// paired with a hash of the registered function/component catalog
// (kb::script::ScriptApiCatalog, hashed from its canonical JSON export —
// the same text kb-cli api/init-agent already write as a build artifact
// under a project's .kb/api/ directory). Two builds with an identical
// manifestHash expose the identical API surface; a different hash means a
// function, component property, or lifecycle event changed.
struct ApiManifest {
    LibraryApiVersion version{};
    std::string manifestHash;
    std::vector<LibraryApiSurfaceManifestEntry> specialApis;
};

[[nodiscard]] ApiManifest BuildApiManifest(const kb::script::ScriptApiCatalog& catalog);

// Renders the manifest as the small JSON object kb-cli writes to
// ".kb/api/manifest.json" (see ScriptAgentProjectFiles' build artifact
// directory): {"version":"major.minor.patch","hash":"..."}.
[[nodiscard]] std::string ToJson(const ApiManifest& manifest);

} // namespace kb::library
