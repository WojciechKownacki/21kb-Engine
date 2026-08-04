#include "RendererTestSupport.hpp"

#include "kb/render/ShaderManifest.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
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
    Require(std::string_view{ShaderProfileDirectoryForRenderer(bgfx::RendererType::Metal)} == "shaders/metal", "Metal shader profile should be metal");
}

void ShaderManifestDeclaresRuntimePrograms() {
    const std::span<const ShaderProgramManifestEntry> programs = RequiredShaderProgramManifest();
    Require(programs.size() >= 9U, "Shader program manifest is missing runtime programs");

    bool foundSceneMesh = false;
    bool foundPostBloom = false;
    bool foundEditorGrid = false;
    for (const ShaderProgramManifestEntry& program : programs) {
        Require(std::string_view{program.name}.size() > 0U, "Shader program manifest contains an empty program name");
        foundSceneMesh = foundSceneMesh || std::string_view{program.name} == "scene_mesh_instanced";
        foundPostBloom = foundPostBloom || std::string_view{program.name} == "post_bloom_combine";
        foundEditorGrid = foundEditorGrid || std::string_view{program.name} == "editor_grid";
        Require(std::string_view{program.vertexShader}.starts_with("vs_"), "Shader program manifest has an invalid vertex shader name");
        Require(std::string_view{program.fragmentShader}.starts_with("fs_"), "Shader program manifest has an invalid fragment shader name");
    }
    Require(foundSceneMesh, "Shader program manifest is missing scene mesh program");
    Require(foundPostBloom, "Shader program manifest is missing post-process bloom program");
    Require(foundEditorGrid, "Shader program manifest is missing editor grid program");
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
            RequiredShaderManifest(), [](const ShaderManifestEntry& shader) { return shader.required; }));
        Require(result.checkedRequiredShaderCount == requiredShaderCount, "Shader manifest validation checked the wrong number of required shaders");
        Require(result.Succeeded(), "Prebuilt shader profile is missing a required runtime shader");
    }
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
    ShaderManifestDeclaresRuntimePrograms();
    PrebuiltShaderProfilesContainRequiredManifest();
    ShaderManifestCoversEveryPrebuiltShaderVariant();
    MotionVectorShaderUsesTopLeftUvToNdcMapping();
}

} // namespace kb::render::tests
