#include "scene/submit/SceneMeshPassResources.hpp"

#include "kb/render/ShaderLoader.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"
#include "renderer/RendererDebugLog.hpp"
#include "scene/submit/SceneMeshMaterialBindingResolver.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace kb::render {
namespace {

[[nodiscard]] bgfx::TextureHandle CreateFallbackTexture(
    std::uint32_t rgba,
    RenderTextureDimension dimension) {
    switch (dimension) {
    case RenderTextureDimension::Texture2D: {
        const bgfx::Memory* memory = bgfx::copy(&rgba, sizeof(rgba));
        return bgfx::createTexture2D(1U, 1U, false, 1U, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE, memory);
    }
    case RenderTextureDimension::TextureCube: {
        const std::array<std::uint32_t, 6U> texels{ rgba, rgba, rgba, rgba, rgba, rgba };
        const bgfx::Memory* memory = bgfx::copy(texels.data(), static_cast<std::uint32_t>(sizeof(texels)));
        return bgfx::createTextureCube(1U, false, 1U, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE, memory);
    }
    case RenderTextureDimension::Texture3D: {
        // bgfx classifies depth==1 as a 2D resource on multiple backends. Two slices keep this a true 3D fallback.
        const std::array<std::uint32_t, 2U> texels{ rgba, rgba };
        const bgfx::Memory* memory = bgfx::copy(texels.data(), static_cast<std::uint32_t>(sizeof(texels)));
        return bgfx::createTexture3D(1U, 1U, 2U, false, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE, memory);
    }
    case RenderTextureDimension::Texture2DArray: {
        // Two layers make the resource unambiguously a 2D array on every backend.
        const std::array<std::uint32_t, 2U> texels{ rgba, rgba };
        const bgfx::Memory* memory = bgfx::copy(texels.data(), static_cast<std::uint32_t>(sizeof(texels)));
        return bgfx::createTexture2D(1U, 1U, false, 2U, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE, memory);
    }
    }
    return BGFX_INVALID_HANDLE;
}

[[nodiscard]] bool IsSelectionPass(MeshPassType pass) noexcept {
    return pass == MeshPassType::SelectionId || pass == MeshPassType::EditorSelection;
}

constexpr std::uint64_t kBuiltinMeshMaterialTypeId = 0x6275696C74696E70ULL; // "builtinp"
constexpr std::uint32_t kBuiltinMeshMaterialTypeVersion = 1U;

[[nodiscard]] const char* GraphBackendDirectoryForKey(std::uint32_t backend) noexcept;

[[nodiscard]] MaterialProgramKey BuiltinMeshProgramKey(std::string pass) {
    return MaterialProgramKey{
        .materialTypeId = kBuiltinMeshMaterialTypeId,
        .materialTypeVersion = kBuiltinMeshMaterialTypeVersion,
        .graphSourceHash = 0U,
        .variantKey = 0U,
        .pass = std::move(pass),
        .backend = 0U,
        .pipelineStateKey = 0U,
        .requiresGeneratedVertexShader = false,
        .graphProgram = false,
    };
}

[[nodiscard]] std::uint64_t GraphBinaryRevision(
    std::string_view cacheRoot,
    std::uint64_t sourceHash,
    std::uint64_t variantKey,
    std::string_view pass,
    std::uint32_t backend) {
    if (cacheRoot.empty()) {
        return 0U;
    }
    const std::filesystem::path root = std::filesystem::path{ cacheRoot } /
        ("graph_" + std::to_string(sourceHash)) /
        ("variant_" + std::to_string(variantKey)) /
        pass /
        GraphBackendDirectoryForKey(backend);
    std::uint64_t hash = 1469598103934665603ULL;
    for (const char* name : { "fs.bin.hash", "vs.bin.hash" }) {
        std::ifstream input{ root / name, std::ios::binary };
        if (!input) {
            continue;
        }
        char ch = 0;
        while (input.get(ch)) {
            hash ^= static_cast<unsigned char>(ch);
            hash *= 1099511628211ULL;
        }
        hash ^= 0xffU;
        hash *= 1099511628211ULL;
    }
    return hash;
}

template <typename T>
void AppendUniqueValue(std::vector<T>& values, const T& value) {
    if (std::ranges::find(values, value) == values.end()) {
        values.push_back(value);
    }
}

[[nodiscard]] bgfx::ProgramHandle LoadBuiltinMeshProgram(const MaterialProgramKey& key) {
    if (key.pass == "BaseOpaque") {
        return ShaderLoader::LoadProgram("vs_mesh_instanced.sc", "fs_mesh_instanced.sc");
    }
    if (key.pass == "BaseTransparent") {
        // Transparent reuses the forward shader; the alpha blend is a render state (MAT-80), not a shader.
        return ShaderLoader::LoadProgram("vs_mesh_instanced.sc", "fs_mesh_instanced.sc");
    }
    if (key.pass == "GBuffer") {
        return ShaderLoader::LoadProgram("vs_mesh_instanced.sc", "fs_mesh_gbuffer_instanced.sc");
    }
    if (key.pass == "ShadowDepth") {
        return ShaderLoader::LoadProgram("vs_mesh_shadow_instanced.sc", "fs_mesh_shadow_instanced.sc");
    }
    if (key.pass == "SelectionId") {
        return ShaderLoader::LoadProgram("vs_mesh_instanced.sc", "fs_mesh_selection_instanced.sc");
    }
    return BGFX_INVALID_HANDLE;
}

[[nodiscard]] const char* GraphBackendDirectoryForRenderer(bgfx::RendererType::Enum renderer) noexcept {
    switch (renderer) {
    case bgfx::RendererType::Noop:
    case bgfx::RendererType::Direct3D11:
        return "dxbc";
    case bgfx::RendererType::Direct3D12:
        return "dxil";
    case bgfx::RendererType::Vulkan:
        return "spirv";
    case bgfx::RendererType::OpenGL:
        return "glsl";
    case bgfx::RendererType::OpenGLES:
        return "essl";
    case bgfx::RendererType::Metal:
        return "metal";
    default:
        return "dxbc";
    }
}

[[nodiscard]] const char* GraphBackendDirectoryForKey(std::uint32_t backend) noexcept {
    return GraphBackendDirectoryForRenderer(static_cast<bgfx::RendererType::Enum>(backend));
}

[[nodiscard]] std::string GraphMeshPassName(MeshPassType pass) {
    switch (pass) {
    case MeshPassType::GBuffer:
        return "GBuffer";
    case MeshPassType::BaseTransparent:
        return "BaseTransparent";
    case MeshPassType::ShadowDepth:
        return "ShadowDepth";
    default:
        return "BaseOpaque";
    }
}

[[nodiscard]] std::string_view GraphRuntimeColorSpaceName(RenderTextureColorSpace colorSpace) noexcept {
    switch (colorSpace) {
    case RenderTextureColorSpace::Srgb: return "Srgb";
    case RenderTextureColorSpace::Linear: return "Linear";
    }
    return "Linear";
}

[[nodiscard]] bool IsNormalGraphTextureRole(std::string_view role) noexcept {
    return role == "normal" || role == "normalMap";
}

[[nodiscard]] bool IsGraphCapablePass(MeshPassType pass) noexcept {
    return pass == MeshPassType::BaseOpaque || pass == MeshPassType::GBuffer || pass == MeshPassType::BaseTransparent || pass == MeshPassType::ShadowDepth;
}

[[nodiscard]] std::vector<std::uint8_t> ReadShaderBinaryFile(const std::filesystem::path& path) {
    std::ifstream file{ path, std::ios::binary | std::ios::ate };
    if (!file.is_open()) {
        return {};
    }
    const std::streamsize size = file.tellg();
    if (size <= 0) {
        return {};
    }
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        return {};
    }
    return bytes;
}

} // namespace

bool SceneMeshPassResources::Initialize() {
    if (IsInitialized()) {
        return true;
    }

    programRegistry_.Configure(
        [this](const MaterialProgramKey& key) { return LoadProgramForKey(key); },
        [](bgfx::ProgramHandle handle) { bgfx::destroy(handle); });
    ResetProgramBindStats();

    meshProgram_ = programRegistry_.Acquire(BuiltinMeshProgramKey("BaseOpaque"));
    if (!bgfx::isValid(meshProgram_)) {
        return false;
    }
    shadowProgram_ = programRegistry_.Acquire(BuiltinMeshProgramKey("ShadowDepth"));
    if (!bgfx::isValid(shadowProgram_)) {
        Shutdown();
        return false;
    }
    gbufferProgram_ = programRegistry_.Acquire(BuiltinMeshProgramKey("GBuffer"));
    if (!bgfx::isValid(gbufferProgram_)) {
        Shutdown();
        return false;
    }
    selectionProgram_ = programRegistry_.Acquire(BuiltinMeshProgramKey("SelectionId"));
    if (!bgfx::isValid(selectionProgram_)) {
        Shutdown();
        return false;
    }

    albedoSampler_ = bgfx::createUniform("s_albedo", bgfx::UniformType::Sampler);
    shadowSampler_ = bgfx::createUniform("s_shadowMap", bgfx::UniformType::Sampler);
    normalSampler_ = bgfx::createUniform("s_normal", bgfx::UniformType::Sampler);
    metallicRoughnessSampler_ = bgfx::createUniform("s_metallicRoughness", bgfx::UniformType::Sampler);
    occlusionSampler_ = bgfx::createUniform("s_occlusion", bgfx::UniformType::Sampler);
    emissiveSampler_ = bgfx::createUniform("s_emissive", bgfx::UniformType::Sampler);
    terrainLayerWeightSampler_ = bgfx::createUniform("s_terrainLayerWeights", bgfx::UniformType::Sampler);
    terrainLayerParamsUniform_ = bgfx::createUniform("u_terrainLayerParams", bgfx::UniformType::Vec4);
    materialParamsUniform_ = bgfx::createUniform("u_materialParams", bgfx::UniformType::Vec4);
    materialEmissiveUniform_ = bgfx::createUniform("u_materialEmissive", bgfx::UniformType::Vec4);
    materialFlagsUniform_ = bgfx::createUniform("u_materialFlags", bgfx::UniformType::Vec4);
    materialUvTransformUniform_ = bgfx::createUniform("u_materialUvTransform", bgfx::UniformType::Vec4);
    cameraPositionUniform_ = bgfx::createUniform("u_cameraPosition", bgfx::UniformType::Vec4);
    timeUniform_ = bgfx::createUniform("u_time", bgfx::UniformType::Vec4);
    dynamicParameterUniform_ = bgfx::createUniform("u_dynamicParameter", bgfx::UniformType::Vec4);
    lightDirKindUniform_ = bgfx::createUniform("u_lightDirKind", bgfx::UniformType::Vec4, kMaxSceneForwardPlusLights);
    lightPositionRangeUniform_ = bgfx::createUniform("u_lightPositionRange", bgfx::UniformType::Vec4, kMaxSceneForwardPlusLights);
    lightColorIntensityUniform_ = bgfx::createUniform("u_lightColorIntensity", bgfx::UniformType::Vec4, kMaxSceneForwardPlusLights);
    lightSpotUniform_ = bgfx::createUniform("u_lightSpot", bgfx::UniformType::Vec4, kMaxSceneForwardPlusLights);
    lightAreaRightUniform_ = bgfx::createUniform("u_lightAreaRight", bgfx::UniformType::Vec4, kMaxSceneForwardPlusLights);
    lightParamsUniform_ = bgfx::createUniform("u_lightParams", bgfx::UniformType::Vec4);
    ambientColorUniform_ = bgfx::createUniform("u_ambientColor", bgfx::UniformType::Vec4);
    environmentZenithUniform_ = bgfx::createUniform("u_environmentZenith", bgfx::UniformType::Vec4);
    environmentGroundUniform_ = bgfx::createUniform("u_environmentGround", bgfx::UniformType::Vec4);
    environmentParamsUniform_ = bgfx::createUniform("u_environmentParams", bgfx::UniformType::Vec4);
    shadowViewProjUniform_ = bgfx::createUniform("u_shadowViewProj", bgfx::UniformType::Mat4);
    shadowParamsUniform_ = bgfx::createUniform("u_shadowParams", bgfx::UniformType::Vec4);
    fallbackWhiteTexture_ = CreateFallbackTexture(0xFFFF'FFFFU, RenderTextureDimension::Texture2D);
    fallbackNormalTexture_ = CreateFallbackTexture(0xFFFF'8080U, RenderTextureDimension::Texture2D);
    fallbackWhiteCubeTexture_ = CreateFallbackTexture(0xFFFF'FFFFU, RenderTextureDimension::TextureCube);
    fallbackNormalCubeTexture_ = CreateFallbackTexture(0xFFFF'8080U, RenderTextureDimension::TextureCube);
    fallbackWhite3DTexture_ = CreateFallbackTexture(0xFFFF'FFFFU, RenderTextureDimension::Texture3D);
    fallbackNormal3DTexture_ = CreateFallbackTexture(0xFFFF'8080U, RenderTextureDimension::Texture3D);
    fallbackWhite2DArrayTexture_ = CreateFallbackTexture(0xFFFF'FFFFU, RenderTextureDimension::Texture2DArray);
    fallbackNormal2DArrayTexture_ = CreateFallbackTexture(0xFFFF'8080U, RenderTextureDimension::Texture2DArray);
    if (!IsInitialized()) {
        Shutdown();
        return false;
    }

    return true;
}

void SceneMeshPassResources::Shutdown() {
    if (bgfx::isValid(terrainLayerParamsUniform_)) {
        bgfx::destroy(terrainLayerParamsUniform_);
        terrainLayerParamsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(terrainLayerWeightSampler_)) {
        bgfx::destroy(terrainLayerWeightSampler_);
        terrainLayerWeightSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(fallbackNormal2DArrayTexture_)) {
        bgfx::destroy(fallbackNormal2DArrayTexture_);
        fallbackNormal2DArrayTexture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(fallbackWhite2DArrayTexture_)) {
        bgfx::destroy(fallbackWhite2DArrayTexture_);
        fallbackWhite2DArrayTexture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(fallbackNormal3DTexture_)) {
        bgfx::destroy(fallbackNormal3DTexture_);
        fallbackNormal3DTexture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(fallbackWhite3DTexture_)) {
        bgfx::destroy(fallbackWhite3DTexture_);
        fallbackWhite3DTexture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(fallbackNormalCubeTexture_)) {
        bgfx::destroy(fallbackNormalCubeTexture_);
        fallbackNormalCubeTexture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(fallbackWhiteCubeTexture_)) {
        bgfx::destroy(fallbackWhiteCubeTexture_);
        fallbackWhiteCubeTexture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(fallbackNormalTexture_)) {
        bgfx::destroy(fallbackNormalTexture_);
        fallbackNormalTexture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(fallbackWhiteTexture_)) {
        bgfx::destroy(fallbackWhiteTexture_);
        fallbackWhiteTexture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(shadowParamsUniform_)) {
        bgfx::destroy(shadowParamsUniform_);
        shadowParamsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(shadowViewProjUniform_)) {
        bgfx::destroy(shadowViewProjUniform_);
        shadowViewProjUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(environmentParamsUniform_)) {
        bgfx::destroy(environmentParamsUniform_);
        environmentParamsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(environmentGroundUniform_)) {
        bgfx::destroy(environmentGroundUniform_);
        environmentGroundUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(environmentZenithUniform_)) {
        bgfx::destroy(environmentZenithUniform_);
        environmentZenithUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(ambientColorUniform_)) {
        bgfx::destroy(ambientColorUniform_);
        ambientColorUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightParamsUniform_)) {
        bgfx::destroy(lightParamsUniform_);
        lightParamsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightSpotUniform_)) {
        bgfx::destroy(lightSpotUniform_);
        lightSpotUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightAreaRightUniform_)) {
        bgfx::destroy(lightAreaRightUniform_);
        lightAreaRightUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightColorIntensityUniform_)) {
        bgfx::destroy(lightColorIntensityUniform_);
        lightColorIntensityUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightPositionRangeUniform_)) {
        bgfx::destroy(lightPositionRangeUniform_);
        lightPositionRangeUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightDirKindUniform_)) {
        bgfx::destroy(lightDirKindUniform_);
        lightDirKindUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(albedoSampler_)) {
        bgfx::destroy(albedoSampler_);
        albedoSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(shadowSampler_)) {
        bgfx::destroy(shadowSampler_);
        shadowSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(cameraPositionUniform_)) {
        bgfx::destroy(cameraPositionUniform_);
        cameraPositionUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(timeUniform_)) {
        bgfx::destroy(timeUniform_);
        timeUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(dynamicParameterUniform_)) {
        bgfx::destroy(dynamicParameterUniform_);
        dynamicParameterUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(materialFlagsUniform_)) {
        bgfx::destroy(materialFlagsUniform_);
        materialFlagsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(materialUvTransformUniform_)) {
        bgfx::destroy(materialUvTransformUniform_);
        materialUvTransformUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(materialEmissiveUniform_)) {
        bgfx::destroy(materialEmissiveUniform_);
        materialEmissiveUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(materialParamsUniform_)) {
        bgfx::destroy(materialParamsUniform_);
        materialParamsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(emissiveSampler_)) {
        bgfx::destroy(emissiveSampler_);
        emissiveSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(occlusionSampler_)) {
        bgfx::destroy(occlusionSampler_);
        occlusionSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(metallicRoughnessSampler_)) {
        bgfx::destroy(metallicRoughnessSampler_);
        metallicRoughnessSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(normalSampler_)) {
        bgfx::destroy(normalSampler_);
        normalSampler_ = BGFX_INVALID_HANDLE;
    }
    for (auto& [name, sampler] : graphSamplerUniforms_) {
        static_cast<void>(name);
        if (bgfx::isValid(sampler)) {
            bgfx::destroy(sampler);
        }
    }
    graphSamplerUniforms_.clear();
    for (auto& [name, retired] : retiredGraphSamplerUniforms_) {
        static_cast<void>(name);
        if (bgfx::isValid(retired.handle)) {
            bgfx::destroy(retired.handle);
        }
    }
    retiredGraphSamplerUniforms_.clear();
    for (auto& [name, uniform] : graphUniforms_) {
        static_cast<void>(name);
        if (bgfx::isValid(uniform)) {
            bgfx::destroy(uniform);
        }
    }
    graphUniforms_.clear();
    for (auto& [name, retired] : retiredGraphUniforms_) {
        static_cast<void>(name);
        if (bgfx::isValid(retired.handle)) {
            bgfx::destroy(retired.handle);
        }
    }
    retiredGraphUniforms_.clear();
    residentProgramKeys_.clear();
    usedProgramKeys_.clear();
    usedGraphSamplerUniforms_.clear();
    usedGraphUniforms_.clear();
    programRegistry_.Shutdown();
    meshProgram_ = BGFX_INVALID_HANDLE;
    gbufferProgram_ = BGFX_INVALID_HANDLE;
    selectionProgram_ = BGFX_INVALID_HANDLE;
    shadowProgram_ = BGFX_INVALID_HANDLE;
}

bool SceneMeshPassResources::IsInitialized() const noexcept {
    return bgfx::isValid(meshProgram_) &&
        bgfx::isValid(gbufferProgram_) &&
        bgfx::isValid(shadowProgram_) &&
        bgfx::isValid(selectionProgram_) &&
        bgfx::isValid(albedoSampler_) &&
        bgfx::isValid(shadowSampler_) &&
        bgfx::isValid(normalSampler_) &&
        bgfx::isValid(metallicRoughnessSampler_) &&
        bgfx::isValid(occlusionSampler_) &&
        bgfx::isValid(emissiveSampler_) &&
        bgfx::isValid(terrainLayerWeightSampler_) &&
        bgfx::isValid(terrainLayerParamsUniform_) &&
        bgfx::isValid(materialParamsUniform_) &&
        bgfx::isValid(materialEmissiveUniform_) &&
        bgfx::isValid(materialFlagsUniform_) &&
        bgfx::isValid(materialUvTransformUniform_) &&
        bgfx::isValid(cameraPositionUniform_) &&
        bgfx::isValid(timeUniform_) &&
        bgfx::isValid(dynamicParameterUniform_) &&
        bgfx::isValid(lightDirKindUniform_) &&
        bgfx::isValid(lightPositionRangeUniform_) &&
        bgfx::isValid(lightColorIntensityUniform_) &&
        bgfx::isValid(lightSpotUniform_) && bgfx::isValid(lightAreaRightUniform_) &&
        bgfx::isValid(lightParamsUniform_) &&
        bgfx::isValid(ambientColorUniform_) &&
        bgfx::isValid(environmentZenithUniform_) &&
        bgfx::isValid(environmentGroundUniform_) &&
        bgfx::isValid(environmentParamsUniform_) &&
        bgfx::isValid(shadowViewProjUniform_) &&
        bgfx::isValid(shadowParamsUniform_) &&
        bgfx::isValid(fallbackWhiteTexture_) &&
        bgfx::isValid(fallbackNormalTexture_) &&
        bgfx::isValid(fallbackWhiteCubeTexture_) &&
        bgfx::isValid(fallbackNormalCubeTexture_) &&
        bgfx::isValid(fallbackWhite3DTexture_) &&
        bgfx::isValid(fallbackNormal3DTexture_) &&
        bgfx::isValid(fallbackWhite2DArrayTexture_) &&
        bgfx::isValid(fallbackNormal2DArrayTexture_);
}

bgfx::TextureHandle SceneMeshPassResources::GraphFallbackTexture(
    RenderTextureDimension dimension,
    bool normal) const noexcept {
    switch (dimension) {
    case RenderTextureDimension::Texture2D:
        return normal ? fallbackNormalTexture_ : fallbackWhiteTexture_;
    case RenderTextureDimension::TextureCube:
        return normal ? fallbackNormalCubeTexture_ : fallbackWhiteCubeTexture_;
    case RenderTextureDimension::Texture3D:
        return normal ? fallbackNormal3DTexture_ : fallbackWhite3DTexture_;
    case RenderTextureDimension::Texture2DArray:
        return normal ? fallbackNormal2DArrayTexture_ : fallbackWhite2DArrayTexture_;
    }
    return BGFX_INVALID_HANDLE;
}

bgfx::ProgramHandle SceneMeshPassResources::LoadProgramForKey(const MaterialProgramKey& key) const {
    if (!key.graphProgram) {
        return LoadBuiltinMeshProgram(key);
    }
    if (graphShaderCacheRoot_.empty()) {
        return BGFX_INVALID_HANDLE;
    }
    const std::filesystem::path fragmentPath = std::filesystem::path{ graphShaderCacheRoot_ } /
        ("graph_" + std::to_string(key.graphSourceHash)) /
        ("variant_" + std::to_string(key.variantKey)) / key.pass /
        GraphBackendDirectoryForKey(key.backend) / "fs.bin";
    const std::vector<std::uint8_t> fragmentBytes = ReadShaderBinaryFile(fragmentPath);
    if (fragmentBytes.empty()) {
        return BGFX_INVALID_HANDLE;
    }
    // MAT-81/#19: a world-position-offset graph cooks its own vertex shader (vs.bin) next to the fragment
    // shader. When present, pair it with the graph fragment shader so the scene moves real geometry;
    // otherwise use the fixed instanced mesh vertex shader.
    const std::filesystem::path vertexPath = std::filesystem::path{ graphShaderCacheRoot_ } /
        ("graph_" + std::to_string(key.graphSourceHash)) /
        ("variant_" + std::to_string(key.variantKey)) / key.pass /
        GraphBackendDirectoryForKey(key.backend) / "vs.bin";
    const std::vector<std::uint8_t> vertexBytes = ReadShaderBinaryFile(vertexPath);
    bgfx::ShaderHandle vertex = BGFX_INVALID_HANDLE;
    if (!vertexBytes.empty()) {
        const bgfx::Memory* vertexMemory = bgfx::copy(vertexBytes.data(), static_cast<std::uint32_t>(vertexBytes.size()));
        vertex = bgfx::createShader(vertexMemory);
    } else if (key.requiresGeneratedVertexShader) {
        return BGFX_INVALID_HANDLE;
    } else {
        vertex = ShaderLoader::Load("vs_mesh_instanced.sc");
    }
    if (!bgfx::isValid(vertex)) {
        return BGFX_INVALID_HANDLE;
    }
    const bgfx::Memory* memory = bgfx::copy(fragmentBytes.data(), static_cast<std::uint32_t>(fragmentBytes.size()));
    const bgfx::ShaderHandle fragment = bgfx::createShader(memory);
    if (!bgfx::isValid(fragment)) {
        bgfx::destroy(vertex);
        return BGFX_INVALID_HANDLE;
    }
    return bgfx::createProgram(vertex, fragment, true);
}

void SceneMeshPassResources::ResetProgramBindStats() const noexcept {
    programBindStats_ = SceneMeshProgramBindStats{};
    lastBoundProgram_ = BGFX_INVALID_HANDLE;
    lastProgramResolution_ = SceneMeshPassProgramResolution{};
}

void SceneMeshPassResources::ResetGraphMaterialDrawStats() const noexcept {
    graphGpuMaterials_.clear();
    graphFellBackMaterials_.clear();
}

SceneMeshGraphMaterialDrawStats SceneMeshPassResources::GraphMaterialDrawStats() const noexcept {
    SceneMeshGraphMaterialDrawStats stats{};
    // A material renders its authored graph as long as it bound its GPU program in at least one drawn
    // pass. A per-pass fall-back to the builtin path is NOT necessarily a failure: a simple opaque graph
    // legitimately casts shadows with the builtin depth shader (its graph does not affect the silhouette),
    // so that pass reports fellBackToBuiltin without the material rendering flat. A material only counts as
    // a CPU fallback when it NEVER bound its GPU program (e.g. the cooked binary was missing everywhere).
    stats.gpuCount = static_cast<std::uint32_t>(graphGpuMaterials_.size());
    for (const void* material : graphFellBackMaterials_) {
        if (!graphGpuMaterials_.contains(material)) {
            ++stats.cpuFallbackCount;
        }
    }
    return stats;
}

bgfx::UniformHandle& SceneMeshPassResources::AcquireGraphUniform(
    std::string_view name,
    bool sampler) const {
    auto& active = sampler ? graphSamplerUniforms_ : graphUniforms_;
    auto& retired = sampler ? retiredGraphSamplerUniforms_ : retiredGraphUniforms_;
    const std::string key{ name };
    const bgfx::UniformHandle invalid = BGFX_INVALID_HANDLE;
    auto [activeIt, inserted] = active.try_emplace(key, invalid);
    if (inserted) {
        const auto retiredIt = retired.find(key);
        if (retiredIt != retired.end()) {
            activeIt->second = retiredIt->second.handle;
            retired.erase(retiredIt);
        }
    }
    if (!bgfx::isValid(activeIt->second)) {
        activeIt->second = bgfx::createUniform(
            key.c_str(),
            sampler ? bgfx::UniformType::Sampler : bgfx::UniformType::Vec4);
    }
    return activeIt->second;
}

void SceneMeshPassResources::EndFrame(std::uint64_t frameIndex) const {
    for (const MaterialProgramKey& resident : residentProgramKeys_) {
        if (std::ranges::find(usedProgramKeys_, resident) == usedProgramKeys_.end()) {
            programRegistry_.Release(resident);
        }
    }
    residentProgramKeys_ = usedProgramKeys_;
    usedProgramKeys_.clear();
    programRegistry_.BeginFrame(frameIndex);

    // bgfx's render thread can consume submitted uniform commands a couple of frames after the
    // API thread built them. Destroying a graph-only uniform in the first frame where it is unused
    // clears the renderer-side slot before those queued commands are read (for example when leaving
    // Material Editor for Scene View). Match the program registry's two-frame retirement grace.
    constexpr std::uint64_t uniformRetirementGraceFrames = 2U;
    const auto destroyRetired = [frameIndex](auto& retired) {
        for (auto it = retired.begin(); it != retired.end();) {
            if (frameIndex >= it->second.destroyFrame) {
                if (bgfx::isValid(it->second.handle)) {
                    bgfx::destroy(it->second.handle);
                }
                it = retired.erase(it);
            } else {
                ++it;
            }
        }
    };
    destroyRetired(retiredGraphUniforms_);
    destroyRetired(retiredGraphSamplerUniforms_);

    for (auto it = graphUniforms_.begin(); it != graphUniforms_.end();) {
        if (std::ranges::find(usedGraphUniforms_, it->first) == usedGraphUniforms_.end()) {
            retiredGraphUniforms_.insert_or_assign(it->first, RetiredGraphUniform{
                .handle = it->second,
                .destroyFrame = frameIndex + uniformRetirementGraceFrames,
            });
            it = graphUniforms_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = graphSamplerUniforms_.begin(); it != graphSamplerUniforms_.end();) {
        if (std::ranges::find(usedGraphSamplerUniforms_, it->first) == usedGraphSamplerUniforms_.end()) {
            retiredGraphSamplerUniforms_.insert_or_assign(it->first, RetiredGraphUniform{
                .handle = it->second,
                .destroyFrame = frameIndex + uniformRetirementGraceFrames,
            });
            it = graphSamplerUniforms_.erase(it);
        } else {
            ++it;
        }
    }
    usedGraphUniforms_.clear();
    usedGraphSamplerUniforms_.clear();
}

SceneMeshPassProgramResolution SceneMeshPassResources::ResolveMeshPassProgram(const RenderMaterialResource* material, MeshPassType pass) const noexcept {
    SceneMeshPassProgramResolution resolution{};
    if (IsSelectionPass(pass)) {
        resolution.program = selectionProgram_;
        resolution.key = BuiltinMeshProgramKey("SelectionId");
        resolution.materialProgramIdentity = MaterialProgramKeyIdentityHash(resolution.key);
        resolution.status = SceneRenderMaterialProgramStatus::Builtin;
    } else {
        resolution.program = pass == MeshPassType::ShadowDepth ? shadowProgram_ : (pass == MeshPassType::GBuffer ? gbufferProgram_ : meshProgram_);
        resolution.key = BuiltinMeshProgramKey(pass == MeshPassType::ShadowDepth ? "ShadowDepth" : GraphMeshPassName(pass));
        resolution.materialProgramIdentity = MaterialProgramKeyIdentityHash(resolution.key);
        resolution.status = SceneRenderMaterialProgramStatus::Builtin;
        if (material != nullptr && material->graphProgram.active && IsGraphCapablePass(pass)) {
            const MaterialProgramKey key{
                .materialTypeId = material->graphProgram.materialTypeId,
                .materialTypeVersion = material->graphProgram.materialTypeVersion,
                .graphSourceHash = material->graphProgram.graphSourceHash,
                .variantKey = material->graphProgram.variantKey,
                .pass = GraphMeshPassName(pass),
                .backend = static_cast<std::uint32_t>(bgfx::getRendererType()),
                .binaryRevision = GraphBinaryRevision(
                    graphShaderCacheRoot_,
                    material->graphProgram.graphSourceHash,
                    material->graphProgram.variantKey,
                    GraphMeshPassName(pass),
                    static_cast<std::uint32_t>(bgfx::getRendererType())),
                .pipelineStateKey = material->graphProgram.pipelineStateKey,
                .requiresGeneratedVertexShader = material->graphProgram.requiresGeneratedVertexShader,
                .graphProgram = true,
            };
            resolution.key = key;
            resolution.materialProgramIdentity = MaterialProgramKeyIdentityHash(key);
            resolution.status = SceneRenderMaterialProgramStatus::GraphFallback;
            bgfx::ProgramHandle graphHandle = programRegistry_.Find(key);
            if (!bgfx::isValid(graphHandle)) {
                graphHandle = programRegistry_.Acquire(key);
            }
            if (bgfx::isValid(graphHandle)) {
                AppendUniqueValue(usedProgramKeys_, key);
                AppendUniqueValue(residentProgramKeys_, key);
                resolution.program = graphHandle;
                resolution.graphProgram = true;
                resolution.status = SceneRenderMaterialProgramStatus::GraphReady;
            } else {
                const bool shadowRequiresGraphProgram = pass == MeshPassType::ShadowDepth &&
                    (material->graphProgram.requiresGeneratedVertexShader ||
                     material->graphProgram.alphaMode == RenderMaterialAlphaMode::Mask);
                if (shadowRequiresGraphProgram) {
                    // A fixed/position-only shadow shader would cast the wrong silhouette or ignore graph alpha.
                    // Fail closed so submit diagnostics expose the missing artifact instead of rendering a false shadow.
                    resolution.program = BGFX_INVALID_HANDLE;
                } else {
                    resolution.fellBackToBuiltin = true;
                }
            }
        }
    }

    ++programBindStats_.totalBindCount;
    if (resolution.graphProgram) {
        ++programBindStats_.graphProgramBindCount;
    } else {
        ++programBindStats_.builtinProgramBindCount;
    }
    if (resolution.fellBackToBuiltin) {
        ++programBindStats_.builtinFallbackBindCount;
    }
    // Record the ACTUAL per-material draw outcome (deduped by material resource) so the reported
    // graphMaterialGpuCount/CpuFallback reflect what rendered, not the resolve-time renderMode intent.
    // A graph-capable material that bound its GPU program lands in the GPU set; any pass that did NOT
    // (missing cooked binary, shadow fail-closed) lands it in the fell-back set. GraphMaterialDrawStats()
    // then treats "fell back in any pass" as a CPU fallback.
    if (material != nullptr && material->graphProgram.active && IsGraphCapablePass(pass)) {
        const void* materialId = static_cast<const void*>(material);
        if (resolution.graphProgram) {
            graphGpuMaterials_.insert(materialId);
        } else {
            graphFellBackMaterials_.insert(materialId);
        }
    }
    if (!bgfx::isValid(lastBoundProgram_) || lastBoundProgram_.idx != resolution.program.idx) {
        ++programBindStats_.programSwitchCount;
        lastBoundProgram_ = resolution.program;
    }
    if (material != nullptr && material->graphProgram.active && IsGraphCapablePass(pass)) {
        std::ostringstream row;
        row << "resolve-pass pass=" << GraphMeshPassName(pass)
            << " status=" << static_cast<int>(resolution.status)
            << " graphProgram=" << (resolution.graphProgram ? "true" : "false")
            << " fellBackToBuiltin=" << (resolution.fellBackToBuiltin ? "true" : "false")
            << " sourceHash=" << material->graphProgram.graphSourceHash
            << " programHandle=" << (bgfx::isValid(resolution.program) ? std::to_string(resolution.program.idx) : std::string{ "invalid" })
            << " textures=" << material->graphProgram.textures.size()
            << " uniforms=" << material->graphProgram.uniforms.size();
        WriteRendererMaterialGraphDebugLog("gpu", row.str());
    }
    return resolution;
}

bgfx::ProgramHandle SceneMeshPassResources::Bind(const SceneMeshPassBindDesc& desc) const noexcept {
    const RenderMaterialResource* material = desc.command.materialResource;
    const SceneMeshPassProgramResolution resolution = ResolveMeshPassProgram(material, desc.pass);
    lastProgramResolution_ = resolution;
    if (IsSelectionPass(desc.pass)) {
        const std::array<float, 16> disabledShadowViewProj{};
        bgfx::setUniform(shadowViewProjUniform_, disabledShadowViewProj.data());
        return resolution.program;
    }

    const SceneMeshMaterialBindingFallbacks fallbacks{
        .whiteTexture = fallbackWhiteTexture_,
        .normalTexture = fallbackNormalTexture_,
    };
    if (desc.pass != MeshPassType::ShadowDepth) {
        const RenderMeshResource* mesh = desc.command.meshResource;
        const bool terrainLayerEnabled =
            desc.command.terrainLayerIndex != UINT8_MAX &&
            mesh != nullptr &&
            bgfx::isValid(mesh->terrainLayerWeightTexture);
        const std::array<float, 4U> terrainLayerParams{
            terrainLayerEnabled ? static_cast<float>(desc.command.terrainLayerIndex) : 0.0F,
            terrainLayerEnabled ? 1.0F : 0.0F,
            0.0F,
            0.0F,
        };
        bgfx::setTexture(
            15U,
            terrainLayerWeightSampler_,
            terrainLayerEnabled ? mesh->terrainLayerWeightTexture : fallbackWhiteTexture_);
        bgfx::setUniform(terrainLayerParamsUniform_, terrainLayerParams.data());
    }
    if (resolution.graphProgram && material != nullptr) {
        WriteRendererMaterialGraphDebugLog(
            "gpu",
            "bind-graph-pass pass=" + GraphMeshPassName(desc.pass) +
                " graphSourceHash=" + std::to_string(material->graphProgram.graphSourceHash) +
                " normalScale=" + std::to_string(material->normalScale));
        const std::array<float, 4U> graphMaterialParams{
            std::clamp(material->metallicFactor, 0.0F, 1.0F),
            std::clamp(material->roughnessFactor, 0.04F, 1.0F),
            std::clamp(material->normalScale, 0.0F, 8.0F),
            material->alphaCutoff,
        };
        bgfx::setUniform(materialParamsUniform_, graphMaterialParams.data());
        bgfx::setUniform(cameraPositionUniform_, desc.cameraPosition.data());
        bgfx::setUniform(timeUniform_, desc.frameTime.data());
        bgfx::setUniform(dynamicParameterUniform_, desc.dynamicParameter.data());
        bgfx::setUniform(lightDirKindUniform_, desc.lighting.dirKind.data(), kMaxSceneForwardPlusLights);
        bgfx::setUniform(lightPositionRangeUniform_, desc.lighting.positionRange.data(), kMaxSceneForwardPlusLights);
        bgfx::setUniform(lightColorIntensityUniform_, desc.lighting.colorIntensity.data(), kMaxSceneForwardPlusLights);
        bgfx::setUniform(lightSpotUniform_, desc.lighting.spot.data(), kMaxSceneForwardPlusLights);
        bgfx::setUniform(lightAreaRightUniform_, desc.lighting.areaRight.data(), kMaxSceneForwardPlusLights);
        bgfx::setUniform(lightParamsUniform_, desc.lighting.params.data());
        bgfx::setUniform(ambientColorUniform_, desc.lighting.ambient.data());
        bgfx::setUniform(environmentZenithUniform_, desc.lighting.environmentZenith.data());
        bgfx::setUniform(environmentGroundUniform_, desc.lighting.environmentGround.data());
        bgfx::setUniform(environmentParamsUniform_, desc.lighting.environmentParams.data());

        for (const RenderMaterialGraphUniformBinding& graphUniform : material->graphProgram.uniforms) {
            std::array<float, 4U> value{
                graphUniform.value[0],
                graphUniform.value[1],
                graphUniform.value[2],
                graphUniform.value[3],
            };
            if (graphUniform.source == RenderMaterialGraphUniformBindingSource::ParameterCollection) {
                if (const std::optional<RenderMaterialParameterCollectionRuntimeValue> runtimeValue =
                        GlobalRenderMaterialParameterCollectionStore().Resolve(graphUniform.collectionAssetId, graphUniform.collectionParameterStableId)) {
                    value = runtimeValue->value;
                }
            }

            bgfx::UniformHandle& uniform = AcquireGraphUniform(graphUniform.name, false);
            AppendUniqueValue(usedGraphUniforms_, graphUniform.name);
            bgfx::setUniform(uniform, value.data());
        }

        for (const RenderMaterialGraphTextureBinding& graphTexture : material->graphProgram.textures) {
            const RenderTextureHandle resolved = graphTexture.texture.IsValid()
                ? graphTexture.texture
                : desc.resourceMap.ResolveTexture(graphTexture.textureAssetId, graphTexture.colorSpace);
            const RenderTextureResource* textureResource = desc.resources.FindTexture(resolved);
            const bool resourceValid = textureResource != nullptr && bgfx::isValid(textureResource->texture);
            const bool dimensionMatches = resourceValid && textureResource->dimension == graphTexture.dimension;
            const bool textureValid = resourceValid && dimensionMatches;
            const bool normalFallback = IsNormalGraphTextureRole(graphTexture.role);
            const bgfx::TextureHandle fallbackTexture = GraphFallbackTexture(graphTexture.dimension, normalFallback);
            const bgfx::TextureHandle handle = textureValid ? textureResource->texture : fallbackTexture;
            {
                std::ostringstream row;
                row << "setTexture sampler=" << graphTexture.samplerName
                    << " stableId=" << graphTexture.stableId
                    << " slot=" << graphTexture.slot
                    << " role=" << (graphTexture.role.empty() ? "<empty>" : graphTexture.role)
                    << " assetId=" << graphTexture.textureAssetId
                    << " colorSpace=" << GraphRuntimeColorSpaceName(graphTexture.colorSpace)
                    << " resolvedHandle=" << resolved.value
                    << " textureResource=" << (textureResource != nullptr ? "true" : "false")
                    << " expectedDimension=" << RenderTextureDimensionName(graphTexture.dimension)
                    << " dimensionMatch=" << (dimensionMatches ? "true" : "false")
                    << " bgfxHandle=" << (bgfx::isValid(handle) ? std::to_string(handle.idx) : std::string{ "invalid" })
                    << " fallback=" << (!textureValid ? (normalFallback ? "normal" : "white") : "none")
                    << " fallbackDimension=" << RenderTextureDimensionName(graphTexture.dimension);
                if (textureResource != nullptr) {
                    row << " resourceSize=" << textureResource->width << 'x' << textureResource->height
                        << 'x' << textureResource->depth
                        << " resourceLayers=" << textureResource->layers
                        << " actualDimension=" << RenderTextureDimensionName(textureResource->dimension)
                        << " resourceFormat=" << static_cast<int>(textureResource->format)
                        << " resourceColorSpace=" << GraphRuntimeColorSpaceName(textureResource->colorSpace)
                        << " resourceVersion=" << textureResource->version;
                }
                row << " samplerFlags=" << graphTexture.samplerFlags;
                WriteRendererMaterialGraphDebugLog("gpu", row.str());
            }

            bgfx::UniformHandle& sampler = AcquireGraphUniform(graphTexture.samplerName, true);
            AppendUniqueValue(usedGraphSamplerUniforms_, graphTexture.samplerName);
            bgfx::setTexture(static_cast<std::uint8_t>(graphTexture.slot), sampler, handle, graphTexture.samplerFlags);
        }

        if (material->graphProgram.usesSceneColor && bgfx::isValid(desc.sceneColorTexture)) {
            bgfx::UniformHandle& colorSampler = AcquireGraphUniform("s_kbSceneColor", true);
            AppendUniqueValue(usedGraphSamplerUniforms_, std::string{ "s_kbSceneColor" });
            bgfx::setTexture(4U, colorSampler, desc.sceneColorTexture);
        }

        // MAT-80/#18b: bind the opaque scene depth at the reserved slot 5 for graphs that sample it, so
        // SceneDepth / DepthFade read real geometry depth in the transparent pass.
        if (material->graphProgram.usesSceneDepth && bgfx::isValid(desc.sceneDepthTexture)) {
            bgfx::UniformHandle& depthSampler = AcquireGraphUniform("s_kbSceneDepth", true);
            AppendUniqueValue(usedGraphSamplerUniforms_, std::string{ "s_kbSceneDepth" });
            bgfx::setTexture(5U, depthSampler, desc.sceneDepthTexture);
        }

        return resolution.program;
    }

    if (desc.pass == MeshPassType::ShadowDepth) {
        const SceneMeshShadowMaterialBinding materialBinding = SceneMeshMaterialBindingResolver::ResolveShadow(
            material,
            desc.resources,
            desc.resourceMap,
            fallbacks);
        bgfx::setTexture(0U, albedoSampler_, materialBinding.albedoTexture);
        bgfx::setUniform(materialParamsUniform_, materialBinding.params.data());
        bgfx::setUniform(materialFlagsUniform_, materialBinding.flags.data());
        bgfx::setUniform(materialUvTransformUniform_, materialBinding.uvTransform.data());
        bgfx::setUniform(timeUniform_, desc.frameTime.data());
        bgfx::setUniform(dynamicParameterUniform_, desc.dynamicParameter.data());
        return resolution.program;
    }

    const SceneMeshMaterialBinding materialBinding = SceneMeshMaterialBindingResolver::Resolve(
        material,
        desc.resources,
        desc.resourceMap,
        fallbacks);
    if (material != nullptr) {
        std::ostringstream row;
        row << "fallback-bind pass=" << GraphMeshPassName(desc.pass)
            << " materialGraphActive=" << (material->graphProgram.active ? "true" : "false")
            << " albedoAssetId=" << material->albedoTextureAssetId
            << " normalAssetId=" << material->normalTextureAssetId
            << " normalScale=" << material->normalScale
            << " albedoTex=" << (bgfx::isValid(materialBinding.albedoTexture) ? std::to_string(materialBinding.albedoTexture.idx) : std::string{ "invalid" })
            << " normalTex=" << (bgfx::isValid(materialBinding.normalTexture) ? std::to_string(materialBinding.normalTexture.idx) : std::string{ "invalid" })
            << " params=(" << materialBinding.params[0] << ',' << materialBinding.params[1] << ',' << materialBinding.params[2] << ',' << materialBinding.params[3] << ')';
        WriteRendererMaterialGraphDebugLog("gpu", row.str());
    }
    bgfx::setTexture(0U, albedoSampler_, materialBinding.albedoTexture);
    bgfx::setUniform(materialParamsUniform_, materialBinding.params.data());
    bgfx::setUniform(materialFlagsUniform_, materialBinding.flags.data());
    bgfx::setUniform(materialUvTransformUniform_, materialBinding.uvTransform.data());

    const std::array<float, 4> disabledShadowParams{};
    bgfx::setTexture(1U, normalSampler_, materialBinding.normalTexture);
    bgfx::setTexture(2U, metallicRoughnessSampler_, materialBinding.metallicRoughnessTexture);
    bgfx::setTexture(3U, occlusionSampler_, materialBinding.occlusionTexture);
    bgfx::setTexture(4U, emissiveSampler_, materialBinding.emissiveTexture);
    bgfx::setTexture(5U, shadowSampler_, desc.shadowMap != nullptr && desc.shadowMap->IsValid() ? desc.shadowMap->depthTexture : fallbackWhiteTexture_);
    bgfx::setUniform(materialEmissiveUniform_, materialBinding.emissive.data());
    bgfx::setUniform(cameraPositionUniform_, desc.cameraPosition.data());
    bgfx::setUniform(timeUniform_, desc.frameTime.data());
    bgfx::setUniform(dynamicParameterUniform_, desc.dynamicParameter.data());
    bgfx::setUniform(lightDirKindUniform_, desc.lighting.dirKind.data(), kMaxSceneForwardPlusLights);
    bgfx::setUniform(lightPositionRangeUniform_, desc.lighting.positionRange.data(), kMaxSceneForwardPlusLights);
    bgfx::setUniform(lightColorIntensityUniform_, desc.lighting.colorIntensity.data(), kMaxSceneForwardPlusLights);
    bgfx::setUniform(lightSpotUniform_, desc.lighting.spot.data(), kMaxSceneForwardPlusLights);
    bgfx::setUniform(lightAreaRightUniform_, desc.lighting.areaRight.data(), kMaxSceneForwardPlusLights);
    bgfx::setUniform(lightParamsUniform_, desc.lighting.params.data());
    bgfx::setUniform(ambientColorUniform_, desc.lighting.ambient.data());
    bgfx::setUniform(environmentZenithUniform_, desc.lighting.environmentZenith.data());
    bgfx::setUniform(environmentGroundUniform_, desc.lighting.environmentGround.data());
    bgfx::setUniform(environmentParamsUniform_, desc.lighting.environmentParams.data());
    bgfx::setUniform(shadowViewProjUniform_, desc.shadowMap != nullptr && desc.shadowMap->IsValid() ? desc.shadowMap->lightViewProjection.data() : disabledShadowParams.data());
    bgfx::setUniform(shadowParamsUniform_, desc.shadowMap != nullptr && desc.shadowMap->IsValid() ? desc.shadowMap->params.data() : disabledShadowParams.data());

    return resolution.program;
}

} // namespace kb::render
