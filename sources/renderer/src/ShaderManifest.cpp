#include "kb/render/ShaderManifest.hpp"

#include <array>

namespace kb::render {
namespace {

constexpr auto kRequiredShaders = std::to_array<ShaderManifestEntry>({
    ShaderManifestEntry{.name = "cs_hzb_downsample.sc", .stage = ShaderStage::Compute},
    ShaderManifestEntry{.name = "cs_hzb_seed.sc", .stage = ShaderStage::Compute},
    ShaderManifestEntry{.name = "cs_instance_cull.sc", .stage = ShaderStage::Compute},
    ShaderManifestEntry{.name = "cs_instance_cull_clear.sc", .stage = ShaderStage::Compute},
    ShaderManifestEntry{.name = "cs_instance_cull_finalize.sc", .stage = ShaderStage::Compute},
    ShaderManifestEntry{.name = "cs_levg_meshlet_clear.sc", .stage = ShaderStage::Compute},
    ShaderManifestEntry{.name = "cs_levg_meshlet_cull.sc", .stage = ShaderStage::Compute},
    ShaderManifestEntry{.name = "cs_levg_meshlet_finalize.sc", .stage = ShaderStage::Compute},
    ShaderManifestEntry{.name = "cs_massive_fill.sc", .stage = ShaderStage::Compute},
    ShaderManifestEntry{.name = "fs_editor_gizmo.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_editor_gizmo_resolve.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_editor_grid.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_editor_selection_outline.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_editor_viewport_grid.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_leui_rect.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_lit.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_mesh.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_mesh_instanced.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_mesh_selection_instanced.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_mesh_shadow_instanced.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_post_bloom_blur.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_post_bloom_combine.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_post_bloom_prefilter.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_post_exposure_luminance.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_post_fxaa.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_post_motion_vectors.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_post_taa_resolve.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_present_tex.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "vs_editor_gizmo.sc", .stage = ShaderStage::Vertex},
    ShaderManifestEntry{.name = "vs_editor_grid.sc", .stage = ShaderStage::Vertex},
    ShaderManifestEntry{.name = "vs_editor_selection_outline.sc", .stage = ShaderStage::Vertex},
    ShaderManifestEntry{.name = "vs_editor_viewport_grid.sc", .stage = ShaderStage::Vertex},
    ShaderManifestEntry{.name = "vs_leui_rect.sc", .stage = ShaderStage::Vertex},
    ShaderManifestEntry{.name = "vs_lit.sc", .stage = ShaderStage::Vertex},
    ShaderManifestEntry{.name = "vs_lit_trs.sc", .stage = ShaderStage::Vertex},
    ShaderManifestEntry{.name = "vs_lit_trs_gpu_cull.sc", .stage = ShaderStage::Vertex},
    ShaderManifestEntry{.name = "vs_mesh.sc", .stage = ShaderStage::Vertex},
    ShaderManifestEntry{.name = "vs_mesh_instanced.sc", .stage = ShaderStage::Vertex},
    ShaderManifestEntry{.name = "vs_mesh_shadow_instanced.sc", .stage = ShaderStage::Vertex},
    ShaderManifestEntry{.name = "vs_present.sc", .stage = ShaderStage::Vertex},
});

constexpr auto kRequiredPrograms = std::to_array<ShaderProgramManifestEntry>({
    ShaderProgramManifestEntry{.name = "scene_mesh_instanced", .vertexShader = "vs_mesh_instanced.sc", .fragmentShader = "fs_mesh_instanced.sc"},
    ShaderProgramManifestEntry{.name = "scene_mesh_shadow", .vertexShader = "vs_mesh_shadow_instanced.sc", .fragmentShader = "fs_mesh_shadow_instanced.sc"},
    ShaderProgramManifestEntry{.name = "scene_mesh_selection", .vertexShader = "vs_mesh_instanced.sc", .fragmentShader = "fs_mesh_selection_instanced.sc"},
    ShaderProgramManifestEntry{.name = "fullscreen_present", .vertexShader = "vs_present.sc", .fragmentShader = "fs_present_tex.sc"},
    ShaderProgramManifestEntry{.name = "post_bloom_prefilter", .vertexShader = "vs_present.sc", .fragmentShader = "fs_post_bloom_prefilter.sc"},
    ShaderProgramManifestEntry{.name = "post_bloom_blur", .vertexShader = "vs_present.sc", .fragmentShader = "fs_post_bloom_blur.sc"},
    ShaderProgramManifestEntry{.name = "post_bloom_combine", .vertexShader = "vs_present.sc", .fragmentShader = "fs_post_bloom_combine.sc"},
    ShaderProgramManifestEntry{.name = "post_exposure_luminance", .vertexShader = "vs_present.sc", .fragmentShader = "fs_post_exposure_luminance.sc"},
    ShaderProgramManifestEntry{.name = "post_fxaa", .vertexShader = "vs_present.sc", .fragmentShader = "fs_post_fxaa.sc"},
    ShaderProgramManifestEntry{.name = "post_motion_vectors", .vertexShader = "vs_present.sc", .fragmentShader = "fs_post_motion_vectors.sc"},
    ShaderProgramManifestEntry{.name = "post_taa_resolve", .vertexShader = "vs_present.sc", .fragmentShader = "fs_post_taa_resolve.sc"},
    ShaderProgramManifestEntry{.name = "editor_selection_outline", .vertexShader = "vs_present.sc", .fragmentShader = "fs_editor_selection_outline.sc"},
    ShaderProgramManifestEntry{.name = "editor_grid", .vertexShader = "vs_editor_grid.sc", .fragmentShader = "fs_editor_grid.sc"},
});

} // namespace

const char* ShaderStageName(ShaderStage stage) noexcept {
    switch (stage) {
    case ShaderStage::Vertex:
        return "vertex";
    case ShaderStage::Fragment:
        return "fragment";
    case ShaderStage::Compute:
        return "compute";
    }
    return "unknown";
}

const char* ShaderProfileDirectoryForRenderer(bgfx::RendererType::Enum renderer) noexcept {
    switch (renderer) {
    case bgfx::RendererType::Noop:
    case bgfx::RendererType::Direct3D11:
        return "shaders/dxbc";
    case bgfx::RendererType::Direct3D12:
        return "shaders/dxil";
    case bgfx::RendererType::Vulkan:
        return "shaders/spirv";
    case bgfx::RendererType::OpenGL:
        return "shaders/glsl";
    case bgfx::RendererType::OpenGLES:
        return "shaders/essl";
    case bgfx::RendererType::Metal:
        return "shaders/metal";
    default:
        return "shaders/dxbc";
    }
}

std::span<const ShaderManifestEntry> RequiredShaderManifest() noexcept {
    return kRequiredShaders;
}

std::span<const ShaderProgramManifestEntry> RequiredShaderProgramManifest() noexcept {
    return kRequiredPrograms;
}

ShaderManifestValidationResult ValidateShaderManifestProfile(const std::filesystem::path& profileRoot) {
    ShaderManifestValidationResult result{
        .profileRoot = profileRoot,
    };
    for (const ShaderManifestEntry& shader : kRequiredShaders) {
        if (!shader.required) {
            continue;
        }
        ++result.checkedRequiredShaderCount;
        if (!std::filesystem::exists(profileRoot / (std::string{shader.name} + ".bin"))) {
            result.missingRequiredShaders.emplace_back(shader.name);
        }
    }
    return result;
}

} // namespace kb::render
