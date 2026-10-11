#pragma once

#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace kb::render {

// The shaderc toolchain a graph shader cook needs: the bgfx shader compiler, the engine's varying
// definitions and shader include directories. One resolution for every cook on this machine -
// the editor's cook service and the renderer's on-demand cook (an unpackaged project run).
struct RenderMaterialGraphShaderToolchain {
    std::string shadercPath;
    std::string varyingDefPath;
    std::vector<std::string> includeDirs;
    kb::assets::bake::ShaderBakePlatform shaderPlatform = kb::assets::bake::ShaderBakePlatform::Windows;

    [[nodiscard]] bool Available() const noexcept { return !shadercPath.empty() && !varyingDefPath.empty(); }
};

// The passes a scene uses a graph material in; a scene cook produces each of them.
inline constexpr std::array<std::string_view, 4U> kRenderMaterialGraphScenePasses{
    "BaseOpaque", "GBuffer", "ShadowDepth", "BaseTransparent",
};

// Resolves the toolchain: the KB_GRAPH_SHADERC environment variable, then `builtIn` (paths a build
// baked in), then a shaderc and an engine source tree found by walking up from the running
// executable. Fields that stay empty mean the toolchain is unavailable on this machine.
[[nodiscard]] RenderMaterialGraphShaderToolchain ResolveRenderMaterialGraphShaderToolchain(
    const RenderMaterialGraphShaderToolchain& builtIn);

// The graph shader backend whose binaries the active bgfx renderer loads.
[[nodiscard]] RenderMaterialGraphShaderBackend RenderMaterialGraphShaderBackendForActiveRenderer() noexcept;

} // namespace kb::render
