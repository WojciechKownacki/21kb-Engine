#pragma once

#include "engine/library/EngineLibraryManifest.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace kb::library {

// Editor-ready metadata for one callable exposed by the Lua sandbox. The
// insertion text intentionally excludes snippets/placeholder syntax so it is
// valid in both the built-in editor and external Lua Language Server clients.
struct LibraryLuaCompletion {
    std::string label;
    std::string insertionText;
    std::string description;
    std::string category;
    std::string example;
    std::string documentationAnchor;
    LibraryApiVersion version{};
};

// One searchable Visual Graph function node. These items are derived from the
// manifest source map, so a node suggestion always identifies a registered
// runtime binding and generated reference section.
struct LibraryVisualGraphNodeSearchHint {
    std::string nodeId;
    std::string displayName;
    std::string description;
    std::string category;
    std::string example;
    std::string documentationAnchor;
    LibraryApiVersion version{};
};

// Case-insensitive, token-based lookup. An empty query returns every item in
// deterministic label order; non-empty query tokens must all match an item's
// label, category, or authored description.
[[nodiscard]] std::vector<LibraryLuaCompletion> BuildLuaAutocomplete(const ApiManifest& manifest, std::string_view query = {});
[[nodiscard]] std::vector<LibraryVisualGraphNodeSearchHint> BuildVisualGraphNodeSearchHints(const ApiManifest& manifest, std::string_view query = {});

// Stable JSON artifact for authoring clients that cannot link to kb_engine.
[[nodiscard]] std::string ToAuthoringHintsJson(const ApiManifest& manifest);

} // namespace kb::library
