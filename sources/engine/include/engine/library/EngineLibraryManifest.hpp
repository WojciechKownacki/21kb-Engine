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
    // The catalog snapshot from which this manifest hash was derived. API
    // reference generators consume it instead of querying a second source.
    kb::script::ScriptApiCatalog catalog;
};

struct ApiReferenceValidationResult {
    std::vector<std::string> errors;

    [[nodiscard]] bool Succeeded() const noexcept { return errors.empty(); }
};

[[nodiscard]] ApiManifest BuildApiManifest(const kb::script::ScriptApiCatalog& catalog);

// Renders the manifest as the small JSON object kb-cli writes to
// ".kb/api/manifest.json" (see ScriptAgentProjectFiles' build artifact
// directory): {"version":"major.minor.patch","hash":"..."}.
[[nodiscard]] std::string ToJson(const ApiManifest& manifest);

// Renders the human-readable API reference from the catalog snapshot carried
// by the manifest, keeping reference generation and manifest validation on a
// single source of truth.
[[nodiscard]] std::string ToReferenceMarkdown(const ApiManifest& manifest);
// Checks that a rendered reference exposes every manifest function with its
// exact name, pin signature, authored semantics, and documentation anchor.
[[nodiscard]] ApiReferenceValidationResult ValidateReferenceMarkdown(const ApiManifest& manifest, std::string_view markdown);

} // namespace kb::library
