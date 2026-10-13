#pragma once

#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::render {

inline constexpr std::uint64_t kRenderMaterialGraphShaderWrapperVersion = 9ULL;

// The scene pass that writes motion vectors with the graph's own program (Unreal's velocity pass).
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
// translucent graph writes no motion vectors. Unreal's velocity pass uses the material's shaders in the
// same cases.
[[nodiscard]] bool RenderMaterialGraphDrawsOwnMotionVectors(const RenderMaterialGraphReflection& reflection) noexcept;
// Whether a scene cook produces `pass` (one of kRenderMaterialGraphScenePasses) for a graph.
[[nodiscard]] bool RenderMaterialGraphCooksScenePass(const RenderMaterialGraphReflection& reflection, std::string_view pass) noexcept;
// Whether the graph's scene passes draw with its generated vertex shader instead of the builtin mesh one:
// vertex-domain outputs move or re-map the surface, and a Deferred Decal carries its box's inverse transform.
[[nodiscard]] bool RenderMaterialGraphRequiresGeneratedVertexShader(const RenderMaterialGraphReflection& reflection) noexcept;

[[nodiscard]] std::uint64_t ComputeRenderMaterialGraphReflectionHash(const RenderMaterialGraphReflection& reflection) noexcept;
[[nodiscard]] std::uint64_t ComputeRenderMaterialGraphVariantKey(const RenderMaterialGraphShaderSource& shader) noexcept;

[[nodiscard]] RenderMaterialGraphShaderArtifactResult CookRenderMaterialGraphShaderArtifact(
    const RenderMaterialGraphShaderSource& shader,
    std::span<const RenderMaterialGraphShaderBackend> backends,
    const RenderMaterialGraphShaderArtifactRequest& request);

[[nodiscard]] RenderMaterialGraphShaderManifest BuildRenderMaterialGraphShaderManifest(
    std::span<const RenderMaterialGraphShaderArtifact> artifacts);

[[nodiscard]] std::vector<RenderMaterialGraphDiagnostic> ValidateRenderMaterialGraphShaderManifest(
    const RenderMaterialGraphShaderManifest& manifest);

void WriteRenderMaterialGraphShaderManifest(std::ostream& output, const RenderMaterialGraphShaderManifest& manifest);
[[nodiscard]] RenderMaterialGraphShaderManifest ParseRenderMaterialGraphShaderManifest(std::istream& input);

} // namespace kb::render
