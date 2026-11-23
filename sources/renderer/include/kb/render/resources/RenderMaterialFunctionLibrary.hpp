#pragma once

#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace kb::assets {
class AssetManager;
class AssetRegistry;
struct AssetMetadata;
}

namespace kb::render {

// The material functions a material graph calls (and the ones those call), gathered from a project, the one way every
// path that compiles a graph gets them: the editor's Material Editor, preview and scene cooks, kb_cli, the cooker and
// the runtime. A function the project lacks, or cannot read, is left out and named in `diagnostics`, so the compile
// says what is missing instead of failing without a reason.
struct RenderMaterialFunctionLibraryBuildResult {
    RenderMaterialGraphFunctionLibrary library;
    std::vector<RenderMaterialGraphDiagnostic> diagnostics;
};

// Reads one function's graph; false with `error` when it cannot.
using RenderMaterialFunctionGraphReader = std::function<std::optional<RenderMaterialGraphDocument>(
    const kb::assets::AssetMetadata& metadata, std::string& error)>;

[[nodiscard]] RenderMaterialFunctionLibraryBuildResult BuildRenderMaterialFunctionLibrary(
    const kb::assets::AssetRegistry& registry,
    const RenderMaterialGraphDocument& graph,
    const RenderMaterialFunctionGraphReader& reader);

// Reads the functions through the asset manager: loose project files and runtime packs alike.
[[nodiscard]] RenderMaterialFunctionLibraryBuildResult BuildRenderMaterialFunctionLibrary(
    kb::assets::AssetManager& manager,
    const RenderMaterialGraphDocument& graph);

// The same from the project's files, for a caller that only reads (a preview, a report): each function is read from its file
// in the project's mount, the way the editor and kb_cli read assets that are not packed.
[[nodiscard]] RenderMaterialFunctionLibraryBuildResult BuildRenderMaterialFunctionLibraryFromFiles(
    const kb::assets::AssetManager& manager,
    const RenderMaterialGraphDocument& graph);

// The same for a caller with only the registry (kb_cli): each function is read from the file its registry entry names.
[[nodiscard]] RenderMaterialFunctionLibraryBuildResult BuildRenderMaterialFunctionLibraryFromFiles(
    const kb::assets::AssetRegistry& registry,
    const RenderMaterialGraphDocument& graph);

// A value that changes when a function the library holds changes (its asset, its content), for the cache key of anything
// compiled from a graph that calls functions: changing a function recompiles the materials that call it.
[[nodiscard]] std::uint64_t HashRenderMaterialFunctionLibrary(const RenderMaterialGraphFunctionLibrary& library) noexcept;

} // namespace kb::render
