#include "kb/render/ShaderManifest.hpp"

#include "engine/assets/bake/BakeTargetProfile.hpp"

#include <algorithm>
#include <array>

namespace kb::render {
namespace {

constexpr auto kRequiredShaders = std::to_array<ShaderManifestEntry>({
    ShaderManifestEntry{.name = "cs_hzb_downsample.sc", .stage = ShaderStage::Compute, .required = false},
    ShaderManifestEntry{.name = "cs_hzb_seed.sc", .stage = ShaderStage::Compute, .required = false},
    ShaderManifestEntry{.name = "cs_instance_cull.sc", .stage = ShaderStage::Compute,
        .requiredFeature = ShaderRuntimeFeatureBit(ShaderRuntimeFeature::GpuDrivenCulling)},
    ShaderManifestEntry{.name = "cs_instance_cull_clear.sc", .stage = ShaderStage::Compute,
        .requiredFeature = ShaderRuntimeFeatureBit(ShaderRuntimeFeature::GpuDrivenCulling)},
    ShaderManifestEntry{.name = "cs_instance_cull_finalize.sc", .stage = ShaderStage::Compute,
        .requiredFeature = ShaderRuntimeFeatureBit(ShaderRuntimeFeature::GpuDrivenCulling)},
    ShaderManifestEntry{.name = "cs_levg_meshlet_clear.sc", .stage = ShaderStage::Compute, .required = false},
    ShaderManifestEntry{.name = "cs_levg_meshlet_cull.sc", .stage = ShaderStage::Compute, .required = false},
    ShaderManifestEntry{.name = "cs_levg_meshlet_finalize.sc", .stage = ShaderStage::Compute, .required = false},
    ShaderManifestEntry{.name = "cs_massive_fill.sc", .stage = ShaderStage::Compute, .required = false},
    ShaderManifestEntry{.name = "cs_particle_visual_integrate.sc", .stage = ShaderStage::Compute,
        .requiredFeature = ShaderRuntimeFeatureBit(ShaderRuntimeFeature::ParticleGpuVisual)},
    ShaderManifestEntry{.name = "fs_editor_gizmo.sc", .stage = ShaderStage::Fragment,
        .requiredFeature = ShaderRuntimeFeatureBit(ShaderRuntimeFeature::Editor)},
    ShaderManifestEntry{.name = "fs_editor_gizmo_resolve.sc", .stage = ShaderStage::Fragment,
        .requiredFeature = ShaderRuntimeFeatureBit(ShaderRuntimeFeature::Editor)},
    ShaderManifestEntry{.name = "fs_editor_grid.sc", .stage = ShaderStage::Fragment,
        .requiredFeature = ShaderRuntimeFeatureBit(ShaderRuntimeFeature::Editor)},
    ShaderManifestEntry{.name = "fs_editor_selection_outline.sc", .stage = ShaderStage::Fragment,
        .requiredFeature = ShaderRuntimeFeatureBit(ShaderRuntimeFeature::Editor)},
    ShaderManifestEntry{.name = "fs_editor_viewport_grid.sc", .stage = ShaderStage::Fragment,
        .requiredFeature = ShaderRuntimeFeatureBit(ShaderRuntimeFeature::Editor)},
    // Legacy LEUI/lit binaries remain in the prebuilt developer bundle, but no
    // runtime program references them and their sources are no longer shipped.
    ShaderManifestEntry{.name = "fs_leui_rect.sc", .stage = ShaderStage::Fragment, .required = false},
    ShaderManifestEntry{.name = "fs_lit.sc", .stage = ShaderStage::Fragment, .required = false},
    ShaderManifestEntry{.name = "fs_deferred_lighting.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_mesh.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_mesh_gbuffer_instanced.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_mesh_instanced.sc", .stage = ShaderStage::Fragment},
    // Legacy developer prebuilt bundles contain these permutations only in DXBC, so they stay
    // individually optional for ValidateShaderManifestProfile. Production packages compile
    // them per target and RequiredPackagedShaderNames promotes every stage referenced by a
    // required runtime program; the cooker/validator therefore cannot omit them.
    ShaderManifestEntry{.name = "fs_mesh_motion_vectors.sc", .stage = ShaderStage::Fragment, .required = false},
    ShaderManifestEntry{.name = "fs_mesh_selection_instanced.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_mesh_shadow_instanced.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_post_bloom_blur.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_post_bloom_combine.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_post_bloom_prefilter.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_post_exposure_luminance.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_post_fxaa.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_post_motion_vectors.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_post_taa_resolve.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_particle_instanced.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "fs_present_tex.sc", .stage = ShaderStage::Fragment},
    ShaderManifestEntry{.name = "vs_editor_gizmo.sc", .stage = ShaderStage::Vertex,
        .requiredFeature = ShaderRuntimeFeatureBit(ShaderRuntimeFeature::Editor)},
    ShaderManifestEntry{.name = "vs_editor_grid.sc", .stage = ShaderStage::Vertex,
        .requiredFeature = ShaderRuntimeFeatureBit(ShaderRuntimeFeature::Editor)},
    ShaderManifestEntry{.name = "vs_editor_selection_outline.sc", .stage = ShaderStage::Vertex,
        .requiredFeature = ShaderRuntimeFeatureBit(ShaderRuntimeFeature::Editor)},
    ShaderManifestEntry{.name = "vs_editor_viewport_grid.sc", .stage = ShaderStage::Vertex,
        .requiredFeature = ShaderRuntimeFeatureBit(ShaderRuntimeFeature::Editor)},
    ShaderManifestEntry{.name = "vs_leui_rect.sc", .stage = ShaderStage::Vertex, .required = false},
    ShaderManifestEntry{.name = "vs_lit.sc", .stage = ShaderStage::Vertex, .required = false},
    ShaderManifestEntry{.name = "vs_lit_trs.sc", .stage = ShaderStage::Vertex, .required = false},
    ShaderManifestEntry{.name = "vs_lit_trs_gpu_cull.sc", .stage = ShaderStage::Vertex, .required = false},
    ShaderManifestEntry{.name = "vs_mesh.sc", .stage = ShaderStage::Vertex},
    ShaderManifestEntry{.name = "vs_mesh_instanced.sc", .stage = ShaderStage::Vertex},
    ShaderManifestEntry{.name = "vs_mesh_motion_vectors_instanced.sc", .stage = ShaderStage::Vertex, .required = false},
    ShaderManifestEntry{.name = "vs_mesh_shadow_instanced.sc", .stage = ShaderStage::Vertex},
    ShaderManifestEntry{.name = "vs_mesh_shadow_skinned_instanced.sc", .stage = ShaderStage::Vertex, .required = false},
    ShaderManifestEntry{.name = "vs_mesh_skinned_instanced.sc", .stage = ShaderStage::Vertex, .required = false},
    ShaderManifestEntry{.name = "vs_mesh_skinned_motion_vectors_instanced.sc", .stage = ShaderStage::Vertex, .required = false},
    ShaderManifestEntry{.name = "vs_present.sc", .stage = ShaderStage::Vertex},
    ShaderManifestEntry{.name = "vs_particle_instanced.sc", .stage = ShaderStage::Vertex},
});

constexpr auto kRequiredPrograms = std::to_array<ShaderProgramManifestEntry>({
    ShaderProgramManifestEntry{.name = "scene_mesh_instanced", .vertexShader = "vs_mesh_instanced.sc", .fragmentShader = "fs_mesh_instanced.sc"},
    ShaderProgramManifestEntry{.name = "scene_mesh_skinned", .vertexShader = "vs_mesh_skinned_instanced.sc", .fragmentShader = "fs_mesh_instanced.sc"},
    ShaderProgramManifestEntry{.name = "scene_mesh_gbuffer", .vertexShader = "vs_mesh_instanced.sc", .fragmentShader = "fs_mesh_gbuffer_instanced.sc"},
    ShaderProgramManifestEntry{.name = "scene_mesh_shadow", .vertexShader = "vs_mesh_shadow_instanced.sc", .fragmentShader = "fs_mesh_shadow_instanced.sc"},
    ShaderProgramManifestEntry{.name = "scene_mesh_shadow_skinned", .vertexShader = "vs_mesh_shadow_skinned_instanced.sc", .fragmentShader = "fs_mesh_shadow_instanced.sc"},
    ShaderProgramManifestEntry{.name = "scene_mesh_selection", .vertexShader = "vs_mesh_instanced.sc", .fragmentShader = "fs_mesh_selection_instanced.sc"},
    ShaderProgramManifestEntry{.name = "deferred_lighting", .vertexShader = "vs_present.sc", .fragmentShader = "fs_deferred_lighting.sc"},
    ShaderProgramManifestEntry{.name = "fullscreen_present", .vertexShader = "vs_present.sc", .fragmentShader = "fs_present_tex.sc"},
    ShaderProgramManifestEntry{.name = "particle_instanced", .vertexShader = "vs_particle_instanced.sc", .fragmentShader = "fs_particle_instanced.sc"},
    ShaderProgramManifestEntry{.name = "post_bloom_prefilter", .vertexShader = "vs_present.sc", .fragmentShader = "fs_post_bloom_prefilter.sc"},
    ShaderProgramManifestEntry{.name = "post_bloom_blur", .vertexShader = "vs_present.sc", .fragmentShader = "fs_post_bloom_blur.sc"},
    ShaderProgramManifestEntry{.name = "post_bloom_combine", .vertexShader = "vs_present.sc", .fragmentShader = "fs_post_bloom_combine.sc"},
    ShaderProgramManifestEntry{.name = "post_exposure_luminance", .vertexShader = "vs_present.sc", .fragmentShader = "fs_post_exposure_luminance.sc"},
    ShaderProgramManifestEntry{.name = "post_fxaa", .vertexShader = "vs_present.sc", .fragmentShader = "fs_post_fxaa.sc"},
    ShaderProgramManifestEntry{.name = "post_motion_vectors", .vertexShader = "vs_present.sc", .fragmentShader = "fs_post_motion_vectors.sc"},
    ShaderProgramManifestEntry{.name = "scene_mesh_motion_vectors", .vertexShader = "vs_mesh_motion_vectors_instanced.sc", .fragmentShader = "fs_mesh_motion_vectors.sc"},
    ShaderProgramManifestEntry{.name = "scene_mesh_skinned_motion_vectors", .vertexShader = "vs_mesh_skinned_motion_vectors_instanced.sc", .fragmentShader = "fs_mesh_motion_vectors.sc"},
    ShaderProgramManifestEntry{.name = "post_taa_resolve", .vertexShader = "vs_present.sc", .fragmentShader = "fs_post_taa_resolve.sc"},
    ShaderProgramManifestEntry{.name = "editor_selection_outline", .vertexShader = "vs_present.sc", .fragmentShader = "fs_editor_selection_outline.sc",
        .requiredFeature = ShaderRuntimeFeatureBit(ShaderRuntimeFeature::Editor)},
    ShaderProgramManifestEntry{.name = "editor_grid", .vertexShader = "vs_editor_grid.sc", .fragmentShader = "fs_editor_grid.sc",
        .requiredFeature = ShaderRuntimeFeatureBit(ShaderRuntimeFeature::Editor)},
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
    case bgfx::RendererType::WebGPU:
        return "shaders/wgsl";
    // Console backends. We ship no bytecode for them, and there is deliberately
    // no `default:` label above: a renderer type this function has not been
    // taught about must reach the nullptr below, never inherit a neighbour's
    // directory, and adding one to bgfx must show up here as an unhandled case.
    case bgfx::RendererType::Agc:
    case bgfx::RendererType::Gnm:
    case bgfx::RendererType::Nvn:
    case bgfx::RendererType::Count:
        break;
    }
    return nullptr;
}

std::span<const ShaderManifestEntry> RequiredShaderManifest() noexcept {
    return kRequiredShaders;
}

std::span<const ShaderProgramManifestEntry> RequiredShaderProgramManifest() noexcept {
    return kRequiredPrograms;
}

std::vector<std::string_view> RequiredPackagedShaderNames(
    ShaderRuntimeFeatureMask requiredFeatures) {
    std::vector<std::string_view> names;
    names.reserve(kRequiredShaders.size());
    const auto appendUnique = [&names](std::string_view name) {
        if (!name.empty() && std::ranges::find(names, name) == names.end()) {
            names.push_back(name);
        }
    };
    for (const ShaderManifestEntry& shader : kRequiredShaders) {
        if (shader.required &&
            (shader.requiredFeature == 0U ||
                (requiredFeatures & shader.requiredFeature) != 0U)) {
            appendUnique(shader.name);
        }
    }
    for (const ShaderProgramManifestEntry& program : kRequiredPrograms) {
        if (!program.required ||
            (program.requiredFeature != 0U &&
                (requiredFeatures & program.requiredFeature) == 0U)) {
            continue;
        }
        appendUnique(program.vertexShader);
        appendUnique(program.fragmentShader);
    }
    return names;
}

ShaderRuntimeFeatureMask PackagedGameShaderFeatures(
    kb::assets::bake::ShaderBakeBackend backend) noexcept {
    using kb::assets::bake::ShaderBakeBackend;
    if (backend == ShaderBakeBackend::Dxbc || backend == ShaderBakeBackend::Dxil ||
        backend == ShaderBakeBackend::Spirv || backend == ShaderBakeBackend::Wgsl) {
        return ShaderRuntimeFeatureBit(ShaderRuntimeFeature::GpuDrivenCulling) |
            ShaderRuntimeFeatureBit(ShaderRuntimeFeature::ParticleGpuVisual);
    }
    return 0U;
}

ShaderManifestValidationResult ValidateShaderManifestProfile(
    const std::filesystem::path& profileRoot,
    ShaderRuntimeFeatureMask requiredFeatures) {
    ShaderManifestValidationResult result{
        .profileRoot = profileRoot,
    };
    for (const ShaderManifestEntry& shader : kRequiredShaders) {
        if (!shader.required ||
            (shader.requiredFeature != 0U && (requiredFeatures & shader.requiredFeature) == 0U)) {
            continue;
        }
        ++result.checkedRequiredShaderCount;
        if (!std::filesystem::exists(profileRoot / (std::string{shader.name} + ".bin"))) {
            result.missingRequiredShaders.emplace_back(shader.name);
        }
    }
    return result;
}

ShaderManifestValidationResult ValidatePackagedShaderManifestProfile(
    const std::filesystem::path& profileRoot,
    ShaderRuntimeFeatureMask requiredFeatures) {
    ShaderManifestValidationResult result{
        .profileRoot = profileRoot,
    };
    const std::vector<std::string_view> requiredShaders =
        RequiredPackagedShaderNames(requiredFeatures);
    result.checkedRequiredShaderCount = static_cast<std::uint32_t>(requiredShaders.size());
    for (const std::string_view shader : requiredShaders) {
        if (!std::filesystem::exists(profileRoot / (std::string{ shader } + ".bin"))) {
            result.missingRequiredShaders.emplace_back(shader);
        }
    }
    return result;
}

} // namespace kb::render
