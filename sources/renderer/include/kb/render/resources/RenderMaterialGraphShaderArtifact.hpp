#pragma once

#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::render {

inline constexpr std::uint64_t kRenderMaterialGraphShaderWrapperVersion = 11ULL;

// Four-target variant of graph deferred shading. Additional model color/iris data is omitted.
inline constexpr std::string_view kRenderMaterialGraphCompactGBufferPass = "GBufferCompact";

// The scene pass that writes motion vectors with the graph's own program.
inline constexpr std::string_view kRenderMaterialGraphMotionVectorsPass = "MotionVectors";
// A Post Process material's fullscreen pass (the post-process chain, before the tonemap).
inline constexpr std::string_view kRenderMaterialGraphPostProcessPass = "PostProcess";
// A User Interface material's pass (a widget's rectangle in the screen UI).
inline constexpr std::string_view kRenderMaterialGraphUserInterfacePass = "UserInterface";

enum class RenderMaterialGraphShaderBackend : std::uint8_t {
    Dxbc,
    Dxil,
    Spirv,
    Metal,
    Essl,
    Glsl,
    Wgsl,
};

[[nodiscard]] std::string_view RenderMaterialGraphShaderBackendName(RenderMaterialGraphShaderBackend backend) noexcept;
[[nodiscard]] std::string_view RenderMaterialGraphShaderBackendProfile(RenderMaterialGraphShaderBackend backend) noexcept;
[[nodiscard]] std::string_view RenderMaterialGraphShaderBackendDirectory(RenderMaterialGraphShaderBackend backend) noexcept;
[[nodiscard]] std::optional<RenderMaterialGraphShaderBackend> ParseRenderMaterialGraphShaderBackend(std::string_view text) noexcept;

struct RenderMaterialGraphShaderArtifactRequest {
    std::string shadercPath;
    std::string varyingDefPath;
    std::vector<std::string> includeDirs;
    std::vector<std::string> dependencyFiles;
    std::string cacheRoot;
    std::string pass = "BaseOpaque";
    // Required. shaderc platform defines cannot be derived from the binary
    // backend: SPIR-V targets three shipped platforms and ESSL targets two.
    std::optional<kb::assets::bake::ShaderBakePlatform> shaderPlatform;
    std::uint32_t materialTypeVersion = 1U;
    bool debug = false;
    // While this reads true, a shader compiler the cook has running is ended and the cook gives up: a newer
    // edit has made its result useless. Null means never. The flag must outlive the cook.
    const std::atomic<bool>* terminate = nullptr;
};

struct RenderMaterialGraphArtifactDependency {
    std::string name;
    std::uint64_t contentHash = 0U;
};

struct RenderMaterialGraphShaderBinary {
    RenderMaterialGraphShaderBackend backend = RenderMaterialGraphShaderBackend::Spirv;
    std::string binaryPath;
    std::uint64_t byteSize = 0U;
    bool cacheHit = false;
};

struct RenderMaterialGraphShaderArtifact {
    std::uint64_t graphSourceHash = 0U;
    std::uint64_t variantKey = 0U;
    std::uint64_t wrapperHash = 0U;
    std::uint64_t reflectionHash = 0U;
    std::uint64_t dependencyHash = 0U;
    std::uint64_t artifactHash = 0U;
    std::uint32_t materialTypeVersion = 1U;
    std::string pass;
    std::string entryPoint = "EvaluateMaterialGraph";
    std::string wrapperSource;
    kb::assets::bake::ShaderBakePlatform shaderPlatform =
        kb::assets::bake::ShaderBakePlatform::Windows;
    std::vector<RenderMaterialGraphShaderBinary> binaries;
    std::vector<RenderMaterialGraphArtifactDependency> dependencies;
    bool graphGenerated = true;
    // MAT-67/#54: when the graph drives vertex-domain outputs it also cooks a generated vertex shader
    // (mesh VS + WPO/displacement/CustomizedUV0) so the scene changes real geometry/UVs instead of using
    // the fixed VS. Empty when the graph has no vertex-domain output.
    bool hasVertexShader = false;
    std::string vertexWrapperSource;
    std::vector<RenderMaterialGraphShaderBinary> vertexBinaries;

    [[nodiscard]] const RenderMaterialGraphShaderBinary* FindBinary(RenderMaterialGraphShaderBackend backend) const noexcept;
    [[nodiscard]] const RenderMaterialGraphShaderBinary* FindVertexBinary(RenderMaterialGraphShaderBackend backend) const noexcept;
};

struct RenderMaterialGraphShaderArtifactResult {
    std::optional<RenderMaterialGraphShaderArtifact> artifact;
    std::vector<RenderMaterialGraphDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept;
};

struct RenderMaterialGraphShaderManifestEntry {
    std::uint64_t graphSourceHash = 0U;
    std::uint64_t variantKey = 0U;
    std::uint64_t wrapperHash = 0U;
    std::uint64_t reflectionHash = 0U;
    std::uint64_t dependencyHash = 0U;
    std::uint64_t artifactHash = 0U;
    std::uint32_t materialTypeVersion = 1U;
    std::string pass;
    RenderMaterialGraphShaderBackend backend = RenderMaterialGraphShaderBackend::Spirv;
    kb::assets::bake::ShaderBakePlatform shaderPlatform =
        kb::assets::bake::ShaderBakePlatform::Windows;
    std::string binaryPath;
    bool graphGenerated = true;
};

struct RenderMaterialGraphShaderManifest {
    std::vector<RenderMaterialGraphShaderManifestEntry> entries;
    std::uint64_t manifestHash = 0U;

    [[nodiscard]] const RenderMaterialGraphShaderManifestEntry* Find(
        std::uint64_t graphSourceHash,
        std::uint64_t variantKey,
        std::string_view pass,
        RenderMaterialGraphShaderBackend backend) const noexcept;
    // Compatibility lookup for old single-variant manifests. Returns null when the identity is ambiguous.
    [[nodiscard]] const RenderMaterialGraphShaderManifestEntry* Find(
        std::uint64_t graphSourceHash,
        std::string_view pass,
        RenderMaterialGraphShaderBackend backend) const noexcept;
};

[[nodiscard]] std::string BuildGraphFragmentWrapperSource(
    const RenderMaterialGraphShaderSource& shader,
    std::string_view pass);
// The generated vertex stage of `pass`: the instanced mesh transform with the graph's vertex-domain outputs.
[[nodiscard]] std::string BuildGraphVertexWrapperSource(
    const RenderMaterialGraphShaderSource& shader,
    std::string_view pass);

// Whether the motion vector pass must run the graph's own program: the graph moves its surface beyond the
// rigid mesh (world position offset, displacement), moves its depth (pixel depth offset) or clips it
// (Masked). Any other opaque graph moves exactly like its mesh and draws with the builtin program; a
// translucent graph writes no motion vectors. The pass uses the material's shaders in the
// same cases.
[[nodiscard]] bool RenderMaterialGraphDrawsOwnMotionVectors(const RenderMaterialGraphReflection& reflection) noexcept;
// Whether a scene cook produces `pass` (one of kRenderMaterialGraphScenePasses) for a graph.
// Four-target profiles require the compact variant for graphs with extra model data.
[[nodiscard]] bool RenderMaterialGraphCooksScenePass(const RenderMaterialGraphReflection& reflection,
    std::string_view pass, bool fourTargetGBuffer = false) noexcept;
// Whether the graph's scene passes draw with its generated vertex shader instead of the builtin mesh one:
// vertex-domain outputs move or re-map the surface, and a Deferred Decal carries its box's inverse transform.
[[nodiscard]] bool RenderMaterialGraphRequiresGeneratedVertexShader(const RenderMaterialGraphReflection& reflection) noexcept;

[[nodiscard]] std::uint64_t ComputeRenderMaterialGraphReflectionHash(const RenderMaterialGraphReflection& reflection) noexcept;
[[nodiscard]] std::uint64_t ComputeRenderMaterialGraphVariantKey(const RenderMaterialGraphShaderSource& shader) noexcept;

[[nodiscard]] RenderMaterialGraphShaderArtifactResult CookRenderMaterialGraphShaderArtifact(
    const RenderMaterialGraphShaderSource& shader,
    std::span<const RenderMaterialGraphShaderBackend> backends,
    const RenderMaterialGraphShaderArtifactRequest& request);

// How much compiled graph shader output a cache root may keep. Entries are grouped per graph (one
// `graph_<hash>` folder per shader source); the least recently used graphs go first.
struct RenderMaterialGraphShaderCachePolicy {
    std::uint64_t maxBytes = 0U;
    // The size a prune shrinks to once the limit is passed, so the next cook does not prune again.
    std::uint64_t targetBytes = 0U;
    // A graph that was used within this window is never removed: a compile may be writing into it.
    std::chrono::seconds inUseWindow{ 600 };
};

struct RenderMaterialGraphShaderCachePruneResult {
    std::uint32_t removedGraphs = 0U;
    std::uint64_t removedBytes = 0U;
    std::uint64_t remainingBytes = 0U;
    // Graphs over the limit that were kept because they are in use or could not be removed.
    std::uint32_t keptGraphs = 0U;
};

// Removes least recently used graphs from `cacheRoot` until it fits `policy`. Safe next to running cooks:
// every cook stamps the entry it works on, and a file a cook holds open cannot be deleted.
[[nodiscard]] RenderMaterialGraphShaderCachePruneResult PruneRenderMaterialGraphShaderCache(
    std::string_view cacheRoot,
    const RenderMaterialGraphShaderCachePolicy& policy);

[[nodiscard]] RenderMaterialGraphShaderManifest BuildRenderMaterialGraphShaderManifest(
    std::span<const RenderMaterialGraphShaderArtifact> artifacts);

[[nodiscard]] std::vector<RenderMaterialGraphDiagnostic> ValidateRenderMaterialGraphShaderManifest(
    const RenderMaterialGraphShaderManifest& manifest);

void WriteRenderMaterialGraphShaderManifest(std::ostream& output, const RenderMaterialGraphShaderManifest& manifest);
[[nodiscard]] RenderMaterialGraphShaderManifest ParseRenderMaterialGraphShaderManifest(std::istream& input);

} // namespace kb::render
