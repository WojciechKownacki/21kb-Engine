#include "RendererTestSupport.hpp"

#include "kb/render/ShaderManifest.hpp"

#include "engine/assets/bake/BakeTargetProfile.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::render::tests {
namespace {

void ShaderProfileDirectoryMapsProductionBackends() {
    Require(std::string_view{ShaderProfileDirectoryForRenderer(bgfx::RendererType::Direct3D11)} == "shaders/dxbc", "D3D11 shader profile should be dxbc");
    Require(std::string_view{ShaderProfileDirectoryForRenderer(bgfx::RendererType::Direct3D12)} == "shaders/dxil", "D3D12 shader profile should be dxil");
    Require(std::string_view{ShaderProfileDirectoryForRenderer(bgfx::RendererType::Vulkan)} == "shaders/spirv", "Vulkan shader profile should be spirv");
    Require(std::string_view{ShaderProfileDirectoryForRenderer(bgfx::RendererType::OpenGL)} == "shaders/glsl", "OpenGL shader profile should be glsl");
    Require(std::string_view{ShaderProfileDirectoryForRenderer(bgfx::RendererType::OpenGLES)} == "shaders/essl", "OpenGL ES shader profile should be essl");
    Require(std::string_view{ShaderProfileDirectoryForRenderer(bgfx::RendererType::Metal)} == "shaders/metal", "Metal shader profile should be metal");
}

// Red when: an unmapped renderer type inherits some other backend's directory
// instead of admitting it has none. Before this test the function ended in
// `default: return "shaders/dxbc"`, so WebGPU, AGC, GNM and NVN all answered
// "shaders/dxbc" and every assertion below failed.
//
// This is not a cosmetic mapping. A bgfx .bin header carries no backend
// identifier, so a backend handed another backend's blob does not reject it --
// it takes a hard BGFX_FATAL. Answering "shaders/dxbc" for WebGPU hands D3D
// bytecode to a WebGPU device; a null answer stops the load with the shader
// simply missing, which is the failure the caller already handles.
void UnmappedRenderersGetNoShaderDirectoryInsteadOfAWrongOne() {
    Require(ShaderProfileDirectoryForRenderer(bgfx::RendererType::WebGPU) != nullptr,
        "WebGPU must resolve to its own shader profile directory");
    Require(std::string_view{ShaderProfileDirectoryForRenderer(bgfx::RendererType::WebGPU)} == "shaders/wgsl",
        "WebGPU must load wgsl binaries, never another backend's bytecode");

    constexpr std::array<bgfx::RendererType::Enum, 4U> unshipped{
        bgfx::RendererType::Agc,
        bgfx::RendererType::Gnm,
        bgfx::RendererType::Nvn,
        bgfx::RendererType::Count,
    };
    for (const bgfx::RendererType::Enum renderer : unshipped) {
        Require(ShaderProfileDirectoryForRenderer(renderer) == nullptr,
            "A renderer we ship no bytecode for must report no shader directory, not fall back to one");
    }
}

void ShaderManifestDeclaresRuntimePrograms() {
    const std::span<const ShaderProgramManifestEntry> programs = RequiredShaderProgramManifest();
    Require(programs.size() >= 9U, "Shader program manifest is missing runtime programs");

    bool foundSceneMesh = false;
    bool foundPostBloom = false;
    bool foundEditorGrid = false;
    bool foundSkinned = false;
    bool foundSkinnedMotion = false;
    bool foundScreenUI = false;
    for (const ShaderProgramManifestEntry& program : programs) {
        Require(std::string_view{program.name}.size() > 0U, "Shader program manifest contains an empty program name");
        foundSceneMesh = foundSceneMesh || std::string_view{program.name} == "scene_mesh_instanced";
        foundPostBloom = foundPostBloom || std::string_view{program.name} == "post_bloom_combine";
        foundEditorGrid = foundEditorGrid || std::string_view{program.name} == "editor_grid";
        foundSkinned = foundSkinned || std::string_view{program.name} == "scene_mesh_skinned";
        foundSkinnedMotion = foundSkinnedMotion ||
            std::string_view{program.name} == "scene_mesh_skinned_motion_vectors";
        foundScreenUI = foundScreenUI || std::string_view{program.name} == "screen_ui";
        Require(std::string_view{program.vertexShader}.starts_with("vs_"), "Shader program manifest has an invalid vertex shader name");
        Require(std::string_view{program.fragmentShader}.starts_with("fs_"), "Shader program manifest has an invalid fragment shader name");
    }
    Require(foundSceneMesh, "Shader program manifest is missing scene mesh program");
    Require(foundPostBloom, "Shader program manifest is missing post-process bloom program");
    Require(foundEditorGrid, "Shader program manifest is missing editor grid program");
    Require(foundSkinned && foundSkinnedMotion,
        "Shader program manifest is missing required skinned runtime programs");
    Require(foundScreenUI, "Shader program manifest is missing screen UI program");
}

void PackagedShaderManifestRequiresEveryRequiredProgramStage() {
    const std::vector<std::string_view> required = RequiredPackagedShaderNames();
    const auto contains = [&required](std::string_view name) {
        return std::ranges::find(required, name) != required.end();
    };
    Require(contains("vs_mesh_skinned_instanced.sc") &&
            contains("vs_mesh_shadow_skinned_instanced.sc") &&
            contains("vs_mesh_skinned_motion_vectors_instanced.sc") &&
            contains("vs_mesh_motion_vectors_instanced.sc") &&
            contains("fs_mesh_motion_vectors.sc"),
        "Packaged shader closure omitted a stage referenced by a required skinned/motion program");
    Require(!contains("fs_editor_grid.sc"),
        "Packaged game shader closure included an unselected editor-only program");
    Require(contains("vs_screen_ui.sc") && contains("fs_screen_ui.sc"),
        "Packaged shader closure omitted the screen UI program");

    const auto manifestEntry = [](std::string_view name) -> const ShaderManifestEntry* {
        const auto found = std::ranges::find_if(
            RequiredShaderManifest(),
            [name](const ShaderManifestEntry& shader) {
                return std::string_view{ shader.name } == name;
            });
        return found == RequiredShaderManifest().end() ? nullptr : &*found;
    };
    // Skinned characters and the motion vectors TAA reprojects with ship on every backend, so
    // their stages are required in their own right, not only through the program closure.
    for (const std::string_view shader : { "vs_mesh_skinned_instanced.sc", "vs_mesh_shadow_skinned_instanced.sc",
             "vs_mesh_skinned_motion_vectors_instanced.sc", "vs_mesh_motion_vectors_instanced.sc",
             "fs_mesh_motion_vectors.sc" }) {
        const ShaderManifestEntry* entry = manifestEntry(shader);
        Require(entry != nullptr && entry->required && entry->requiredFeature == 0U,
            "A skinned or motion vector stage is optional in the shader manifest");
    }

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "21kb_packaged_shader_program_closure";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root, error);
    Require(!error, "Could not create packaged shader program closure fixture");
    for (const std::string_view shader : required) {
        std::ofstream output{ root / (std::string{ shader } + ".bin"), std::ios::binary };
        output.put('\x01');
        Require(output.good(), "Could not write packaged shader program closure fixture");
    }
    Require(ValidatePackagedShaderManifestProfile(root).Succeeded(),
        "Complete required program closure was rejected");

    const auto requireRejectedWhenRemoved = [&](std::string_view shader) {
        const std::filesystem::path path = root / (std::string{ shader } + ".bin");
        error.clear();
        std::filesystem::remove(path, error);
        Require(!error, "Could not remove required program shader from negative fixture");
        const ShaderManifestValidationResult rejected =
            ValidatePackagedShaderManifestProfile(root);
        Require(!rejected.Succeeded() &&
                std::ranges::find(rejected.missingRequiredShaders, shader) !=
                    rejected.missingRequiredShaders.end(),
            "Packaged shader validation accepted a required program with a missing optional stage");
        std::ofstream restored{ path, std::ios::binary };
        restored.put('\x01');
        Require(restored.good(), "Could not restore required program shader fixture");
    };
    requireRejectedWhenRemoved("vs_mesh_skinned_instanced.sc");
    requireRejectedWhenRemoved("fs_mesh_motion_vectors.sc");

    std::filesystem::remove_all(root, error);
}

void PrebuiltShaderProfilesContainRequiredManifest() {
    const std::filesystem::path cwd = std::filesystem::current_path();
    const std::vector<std::filesystem::path> candidates{
        cwd / "sources" / "renderer" / "prebuilt_shaders",
        cwd / ".." / "sources" / "renderer" / "prebuilt_shaders",
        cwd / ".." / ".." / "sources" / "renderer" / "prebuilt_shaders",
        cwd / "shaders",
    };
    const auto rootIter = std::ranges::find_if(candidates, [](const std::filesystem::path& candidate) {
        return std::filesystem::exists(candidate / "dxbc" / "vs_mesh_instanced.sc.bin");
    });
    Require(rootIter != candidates.end(), "Prebuilt shader profile root was not found");
    const std::filesystem::path root = *rootIter;
    constexpr std::array<std::string_view, 6U> profiles{
        "dxbc",
        "dxil",
        "spirv",
        "glsl",
        "essl",
        "metal",
    };

    for (std::string_view profile : profiles) {
        const ShaderManifestValidationResult result = ValidateShaderManifestProfile(root / profile);
        const std::size_t requiredShaderCount = static_cast<std::size_t>(std::ranges::count_if(
            RequiredShaderManifest(), [](const ShaderManifestEntry& shader) {
                return shader.required && shader.requiredFeature == 0U;
            }));
        Require(result.checkedRequiredShaderCount == requiredShaderCount, "Shader manifest validation checked the wrong number of required shaders");
        Require(result.Succeeded(), "Prebuilt shader profile is missing a required runtime shader");
        Require(ValidatePackagedShaderManifestProfile(root / profile).Succeeded(),
            "Prebuilt shader profile is missing a required runtime program stage");
    }
}

[[nodiscard]] std::filesystem::path PrebuiltShaderRoot() {
    const std::filesystem::path root =
        std::filesystem::path{ KB_RENDERER_TEST_SOURCE_ROOT } / "sources" / "renderer" / "prebuilt_shaders";
    Require(std::filesystem::is_directory(root), "Prebuilt shader root was not found");
    return root;
}

// The bgfx shader binary header: "VSH", "FSH" or "CSH" and a format version byte.
[[nodiscard]] bool HasShaderHeader(const std::filesystem::path& path, char stage) {
    std::ifstream input{ path, std::ios::binary };
    std::array<char, 4U> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    return input.gcount() == 4 && magic[0] == stage && magic[1] == 'S' && magic[2] == 'H' && magic[3] != '\0';
}

// Red when: a runtime program has a stage some backend the engine ships prebuilt shaders for
// has no binary of -- skinned meshes and TAA motion vectors once existed only for Direct3D, so
// Vulkan and OpenGL loaded the programs, got invalid handles, and drew nothing for them.
void EveryRuntimeProgramExistsForEveryClaimedBackend() {
    struct ClaimedBackend {
        bgfx::RendererType::Enum renderer;
        kb::assets::bake::ShaderBakeBackend bake;
    };
    // The backends README's Shaders row promises prebuilt shaders for.
    constexpr std::array<ClaimedBackend, 6U> claimed{
        ClaimedBackend{ bgfx::RendererType::Direct3D11, kb::assets::bake::ShaderBakeBackend::Dxbc },
        ClaimedBackend{ bgfx::RendererType::Direct3D12, kb::assets::bake::ShaderBakeBackend::Dxil },
        ClaimedBackend{ bgfx::RendererType::Vulkan, kb::assets::bake::ShaderBakeBackend::Spirv },
        ClaimedBackend{ bgfx::RendererType::OpenGL, kb::assets::bake::ShaderBakeBackend::Glsl },
        ClaimedBackend{ bgfx::RendererType::OpenGLES, kb::assets::bake::ShaderBakeBackend::Essl },
        ClaimedBackend{ bgfx::RendererType::Metal, kb::assets::bake::ShaderBakeBackend::Metal },
    };
    const std::filesystem::path root = PrebuiltShaderRoot();
    for (const ClaimedBackend& backend : claimed) {
        const char* const directory = ShaderProfileDirectoryForRenderer(backend.renderer);
        Require(directory != nullptr && std::string_view{ directory }.starts_with("shaders/"),
            "A claimed backend has no shader profile directory");
        const std::filesystem::path profile = root / std::string_view{ directory }.substr(8U);
        for (const ShaderProgramManifestEntry& program : RequiredShaderProgramManifest()) {
            const std::filesystem::path vertex = profile / (std::string{ program.vertexShader } + ".bin");
            const std::filesystem::path fragment = profile / (std::string{ program.fragmentShader } + ".bin");
            if (!HasShaderHeader(vertex, 'V') || !HasShaderHeader(fragment, 'F')) {
                std::cerr << "missing or invalid " << profile.filename().string() << " stage of program "
                          << program.name << '\n';
            }
            Require(HasShaderHeader(vertex, 'V') && HasShaderHeader(fragment, 'F'),
                "A runtime shader program is missing a stage on a backend the engine ships shaders for");
        }
        // Compute kernels a packaged game requires on this backend (none where it has no compute).
        const ShaderRuntimeFeatureMask features = PackagedGameShaderFeatures(backend.bake);
        for (const ShaderManifestEntry& shader : RequiredShaderManifest()) {
            if (shader.stage != ShaderStage::Compute || !shader.required ||
                (shader.requiredFeature != 0U && (features & shader.requiredFeature) == 0U)) {
                continue;
            }
            Require(HasShaderHeader(profile / (std::string{ shader.name } + ".bin"), 'C'),
                "A required compute shader is missing on a backend the engine ships shaders for");
        }
        Require(ValidatePackagedShaderManifestProfile(profile, features).Succeeded(),
            "A claimed backend's shader profile fails packaged manifest validation");
    }
}

void ShaderManifestFeatureRequirementsAreTargetSelectable() {
    using kb::assets::bake::ShaderBakeBackend;
    Require(PackagedGameShaderFeatures(ShaderBakeBackend::Spirv) != 0U &&
            PackagedGameShaderFeatures(ShaderBakeBackend::Dxbc) != 0U &&
            PackagedGameShaderFeatures(ShaderBakeBackend::Wgsl) != 0U &&
            PackagedGameShaderFeatures(ShaderBakeBackend::Essl) == 0U &&
            PackagedGameShaderFeatures(ShaderBakeBackend::Glsl) == 0U,
        "Packaged game shader features do not match backend compute support");
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "21kb_shader_manifest_features";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root, error);
    Require(!error, "Could not create the shader feature manifest fixture");

    const ShaderManifestValidationResult core = ValidateShaderManifestProfile(root);
    const auto missing = [](const ShaderManifestValidationResult& result, std::string_view name) {
        return std::ranges::find(result.missingRequiredShaders, name) != result.missingRequiredShaders.end();
    };
    Require(!missing(core, "cs_instance_cull.sc") &&
            !missing(core, "cs_instance_cull_clear.sc") &&
            !missing(core, "cs_instance_cull_finalize.sc") &&
            !missing(core, "cs_particle_visual_integrate.sc") &&
            !missing(core, "cs_particle_gpu_emit.sc") &&
            !missing(core, "fs_editor_gizmo.sc") &&
            !missing(core, "vs_editor_grid.sc"),
        "A game-runtime shader manifest incorrectly requires compute or editor shaders");

    const ShaderRuntimeFeatureMask computeFeatures =
        ShaderRuntimeFeatureBit(ShaderRuntimeFeature::GpuDrivenCulling) |
        ShaderRuntimeFeatureBit(ShaderRuntimeFeature::ParticleGpuVisual);
    const ShaderManifestValidationResult compute = ValidateShaderManifestProfile(root, computeFeatures);
    Require(missing(compute, "cs_instance_cull.sc") &&
            missing(compute, "cs_instance_cull_clear.sc") &&
            missing(compute, "cs_instance_cull_finalize.sc") &&
            missing(compute, "cs_particle_visual_integrate.sc"),
        "A compute-capable backend did not require its four consumed compute shaders");
    Require(!missing(compute, "cs_hzb_seed.sc") && !missing(compute, "cs_hzb_downsample.sc") &&
            !missing(compute, "cs_levg_meshlet_cull.sc") && !missing(compute, "cs_massive_fill.sc"),
        "Unused compute experiments must not block a production package");

    const ShaderManifestValidationResult editor = ValidateShaderManifestProfile(
        root, ShaderRuntimeFeatureBit(ShaderRuntimeFeature::Editor));
    Require(missing(editor, "fs_editor_gizmo.sc") && missing(editor, "vs_editor_grid.sc") &&
            !missing(editor, "cs_instance_cull.sc"),
        "The editor feature must select only editor shaders");

    std::filesystem::remove_all(root, error);
}

void ShaderManifestCoversEveryPrebuiltShaderVariant() {
    const std::filesystem::path cwd = std::filesystem::current_path();
    const std::vector<std::filesystem::path> candidates{
        cwd / "sources" / "renderer" / "prebuilt_shaders",
        cwd / ".." / "sources" / "renderer" / "prebuilt_shaders",
        cwd / ".." / ".." / "sources" / "renderer" / "prebuilt_shaders",
        cwd / "shaders",
    };
    const auto rootIter = std::ranges::find_if(candidates, [](const std::filesystem::path& candidate) {
        return std::filesystem::exists(candidate / "dxbc" / "vs_mesh_instanced.sc.bin");
    });
    Require(rootIter != candidates.end(), "Prebuilt shader profile root was not found");

    std::vector<std::string> declaredShaders;
    for (const ShaderManifestEntry& shader : RequiredShaderManifest()) {
        Require(std::string_view{shader.name}.size() > 0U, "Shader manifest contains an empty shader name");
        declaredShaders.emplace_back(std::string{shader.name} + ".bin");
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{*rootIter / "dxbc"}) {
        if (!entry.is_regular_file() || entry.path().extension() != ".bin") {
            continue;
        }
        const std::string filename = entry.path().filename().string();
        const auto shaderIter = std::ranges::find(declaredShaders, filename);
        Require(shaderIter != declaredShaders.end(), "Prebuilt shader profile contains a shader missing from the production manifest");
    }
}

void MotionVectorShaderUsesTopLeftUvToNdcMapping() {
    const std::filesystem::path cwd = std::filesystem::current_path();
    const std::vector<std::filesystem::path> candidates{
        cwd / "sources" / "renderer" / "shaders" / "fs_post_motion_vectors.sc",
        cwd / ".." / "sources" / "renderer" / "shaders" / "fs_post_motion_vectors.sc",
        cwd / ".." / ".." / "sources" / "renderer" / "shaders" / "fs_post_motion_vectors.sc",
    };
    const auto sourceIter = std::ranges::find_if(candidates, [](const std::filesystem::path& candidate) {
        return std::filesystem::exists(candidate);
    });
    Require(sourceIter != candidates.end(), "Motion vector shader source was not found");

    std::ifstream file{*sourceIter};
    Require(file.good(), "Motion vector shader source could not be opened");
    const std::string source{
        std::istreambuf_iterator<char>{file},
        std::istreambuf_iterator<char>{},
    };
    Require(
        source.find("1.0 - v_texcoord0.y * 2.0") != std::string::npos,
        "Motion vector shader must map top-left UVs to positive NDC Y");
    Require(
        source.find("0.5 - previousNdc.y * 0.5") != std::string::npos,
        "Motion vector shader must map previous NDC Y back to top-left UVs");
}

} // namespace

void RunShaderManifestTests() {
    ShaderProfileDirectoryMapsProductionBackends();
    UnmappedRenderersGetNoShaderDirectoryInsteadOfAWrongOne();
    ShaderManifestDeclaresRuntimePrograms();
    PackagedShaderManifestRequiresEveryRequiredProgramStage();
    PrebuiltShaderProfilesContainRequiredManifest();
    EveryRuntimeProgramExistsForEveryClaimedBackend();
    ShaderManifestFeatureRequirementsAreTargetSelectable();
    ShaderManifestCoversEveryPrebuiltShaderVariant();
    MotionVectorShaderUsesTopLeftUvToNdcMapping();
}

} // namespace kb::render::tests
