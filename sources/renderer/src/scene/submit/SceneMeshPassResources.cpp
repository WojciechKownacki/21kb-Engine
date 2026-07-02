#include "scene/submit/SceneMeshPassResources.hpp"

#include "kb/render/ShaderLoader.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"
#include "scene/submit/SceneMeshMaterialBindingResolver.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace kb::render {
namespace {

[[nodiscard]] bgfx::TextureHandle CreateFallbackWhiteTexture() {
    const std::uint32_t white = 0xFFFF'FFFFU;
    const bgfx::Memory* memory = bgfx::copy(&white, sizeof(white));
    return bgfx::createTexture2D(1U, 1U, false, 1U, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE, memory);
}

[[nodiscard]] bgfx::TextureHandle CreateFallbackTexture(std::uint32_t rgba) {
    const bgfx::Memory* memory = bgfx::copy(&rgba, sizeof(rgba));
    return bgfx::createTexture2D(1U, 1U, false, 1U, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE, memory);
}

[[nodiscard]] bool IsSelectionPass(MeshPassType pass) noexcept {
    return pass == MeshPassType::SelectionId || pass == MeshPassType::EditorSelection;
}

constexpr std::uint64_t kBuiltinMeshMaterialTypeId = 0x6275696C74696E70ULL; // "builtinp"
constexpr std::uint32_t kBuiltinMeshMaterialTypeVersion = 1U;

[[nodiscard]] MaterialProgramKey BuiltinMeshProgramKey(std::string pass) {
    return MaterialProgramKey{
        .materialTypeId = kBuiltinMeshMaterialTypeId,
        .materialTypeVersion = kBuiltinMeshMaterialTypeVersion,
        .graphSourceHash = 0U,
        .variantKey = 0U,
        .pass = std::move(pass),
        .backend = 0U,
        .pipelineStateKey = 0U,
        .graphProgram = false,
    };
}

[[nodiscard]] bgfx::ProgramHandle LoadBuiltinMeshProgram(const MaterialProgramKey& key) {
    if (key.pass == "BaseOpaque") {
        return ShaderLoader::LoadProgram("vs_mesh_instanced.sc", "fs_mesh_instanced.sc");
    }
    if (key.pass == "BaseTransparent") {
        // Transparent reuses the forward shader; the alpha blend is a render state (MAT-80), not a shader.
        return ShaderLoader::LoadProgram("vs_mesh_instanced.sc", "fs_mesh_instanced.sc");
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
    case MeshPassType::BaseTransparent:
        return "BaseTransparent";
    case MeshPassType::ShadowDepth:
        return "ShadowDepth";
    default:
        return "BaseOpaque";
    }
}

[[nodiscard]] bool IsGraphCapablePass(MeshPassType pass) noexcept {
    return pass == MeshPassType::BaseOpaque || pass == MeshPassType::BaseTransparent || pass == MeshPassType::ShadowDepth;
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
    materialParamsUniform_ = bgfx::createUniform("u_materialParams", bgfx::UniformType::Vec4);
    materialEmissiveUniform_ = bgfx::createUniform("u_materialEmissive", bgfx::UniformType::Vec4);
    materialFlagsUniform_ = bgfx::createUniform("u_materialFlags", bgfx::UniformType::Vec4);
    materialUvTransformUniform_ = bgfx::createUniform("u_materialUvTransform", bgfx::UniformType::Vec4);
    cameraPositionUniform_ = bgfx::createUniform("u_cameraPosition", bgfx::UniformType::Vec4);
    timeUniform_ = bgfx::createUniform("u_time", bgfx::UniformType::Vec4);
    dynamicParameterUniform_ = bgfx::createUniform("u_dynamicParameter", bgfx::UniformType::Vec4);
    lightDirKindUniform_ = bgfx::createUniform("u_lightDirKind", bgfx::UniformType::Vec4, kMaxSceneForwardLights);
    lightPositionRangeUniform_ = bgfx::createUniform("u_lightPositionRange", bgfx::UniformType::Vec4, kMaxSceneForwardLights);
    lightColorIntensityUniform_ = bgfx::createUniform("u_lightColorIntensity", bgfx::UniformType::Vec4, kMaxSceneForwardLights);
    lightSpotUniform_ = bgfx::createUniform("u_lightSpot", bgfx::UniformType::Vec4, kMaxSceneForwardLights);
    lightParamsUniform_ = bgfx::createUniform("u_lightParams", bgfx::UniformType::Vec4);
    ambientColorUniform_ = bgfx::createUniform("u_ambientColor", bgfx::UniformType::Vec4);
    environmentZenithUniform_ = bgfx::createUniform("u_environmentZenith", bgfx::UniformType::Vec4);
    environmentGroundUniform_ = bgfx::createUniform("u_environmentGround", bgfx::UniformType::Vec4);
    environmentParamsUniform_ = bgfx::createUniform("u_environmentParams", bgfx::UniformType::Vec4);
    shadowViewProjUniform_ = bgfx::createUniform("u_shadowViewProj", bgfx::UniformType::Mat4);
    shadowParamsUniform_ = bgfx::createUniform("u_shadowParams", bgfx::UniformType::Vec4);
    fallbackWhiteTexture_ = CreateFallbackWhiteTexture();
    fallbackNormalTexture_ = CreateFallbackTexture(0xFFFF'8080U);
    if (!IsInitialized()) {
        Shutdown();
        return false;
    }

    return true;
}

void SceneMeshPassResources::Shutdown() {
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
    for (auto& [name, uniform] : graphUniforms_) {
        static_cast<void>(name);
        if (bgfx::isValid(uniform)) {
            bgfx::destroy(uniform);
        }
    }
    graphUniforms_.clear();
    programRegistry_.Shutdown();
    meshProgram_ = BGFX_INVALID_HANDLE;
    selectionProgram_ = BGFX_INVALID_HANDLE;
    shadowProgram_ = BGFX_INVALID_HANDLE;
}

bool SceneMeshPassResources::IsInitialized() const noexcept {
    return bgfx::isValid(meshProgram_) &&
        bgfx::isValid(shadowProgram_) &&
        bgfx::isValid(selectionProgram_) &&
        bgfx::isValid(albedoSampler_) &&
        bgfx::isValid(shadowSampler_) &&
        bgfx::isValid(normalSampler_) &&
        bgfx::isValid(metallicRoughnessSampler_) &&
        bgfx::isValid(occlusionSampler_) &&
        bgfx::isValid(emissiveSampler_) &&
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
        bgfx::isValid(lightSpotUniform_) &&
        bgfx::isValid(lightParamsUniform_) &&
        bgfx::isValid(ambientColorUniform_) &&
        bgfx::isValid(environmentZenithUniform_) &&
        bgfx::isValid(environmentGroundUniform_) &&
        bgfx::isValid(environmentParamsUniform_) &&
        bgfx::isValid(shadowViewProjUniform_) &&
        bgfx::isValid(shadowParamsUniform_) &&
        bgfx::isValid(fallbackWhiteTexture_) &&
        bgfx::isValid(fallbackNormalTexture_);
}

bgfx::ProgramHandle SceneMeshPassResources::LoadProgramForKey(const MaterialProgramKey& key) const {
    if (!key.graphProgram) {
        return LoadBuiltinMeshProgram(key);
    }
    if (graphShaderCacheRoot_.empty()) {
        return BGFX_INVALID_HANDLE;
    }
    const std::filesystem::path fragmentPath = std::filesystem::path{ graphShaderCacheRoot_ } /
        ("graph_" + std::to_string(key.graphSourceHash)) / key.pass /
        GraphBackendDirectoryForKey(key.backend) / "fs.bin";
    const std::vector<std::uint8_t> fragmentBytes = ReadShaderBinaryFile(fragmentPath);
    if (fragmentBytes.empty()) {
        return BGFX_INVALID_HANDLE;
    }
    // MAT-81/#19: a world-position-offset graph cooks its own vertex shader (vs.bin) next to the fragment
    // shader. When present, pair it with the graph fragment shader so the scene moves real geometry;
    // otherwise use the fixed instanced mesh vertex shader.
    const std::filesystem::path vertexPath = std::filesystem::path{ graphShaderCacheRoot_ } /
        ("graph_" + std::to_string(key.graphSourceHash)) / key.pass /
        GraphBackendDirectoryForKey(key.backend) / "vs.bin";
    const std::vector<std::uint8_t> vertexBytes = ReadShaderBinaryFile(vertexPath);
    bgfx::ShaderHandle vertex = BGFX_INVALID_HANDLE;
    if (!vertexBytes.empty()) {
        const bgfx::Memory* vertexMemory = bgfx::copy(vertexBytes.data(), static_cast<std::uint32_t>(vertexBytes.size()));
        vertex = bgfx::createShader(vertexMemory);
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

SceneMeshPassProgramResolution SceneMeshPassResources::ResolveMeshPassProgram(const RenderMaterialResource* material, MeshPassType pass) const noexcept {
    SceneMeshPassProgramResolution resolution{};
    if (IsSelectionPass(pass)) {
        resolution.program = selectionProgram_;
        resolution.key = BuiltinMeshProgramKey("SelectionId");
        resolution.materialProgramIdentity = MaterialProgramKeyIdentityHash(resolution.key);
        resolution.status = SceneRenderMaterialProgramStatus::Builtin;
    } else {
        resolution.program = pass == MeshPassType::ShadowDepth ? shadowProgram_ : meshProgram_;
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
                .pipelineStateKey = material->graphProgram.pipelineStateKey,
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
                resolution.program = graphHandle;
                resolution.graphProgram = true;
                resolution.status = SceneRenderMaterialProgramStatus::GraphReady;
            } else {
                resolution.fellBackToBuiltin = true;
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
    if (!bgfx::isValid(lastBoundProgram_) || lastBoundProgram_.idx != resolution.program.idx) {
        ++programBindStats_.programSwitchCount;
        lastBoundProgram_ = resolution.program;
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
    bgfx::setUniform(lightDirKindUniform_, desc.lighting.dirKind.data(), kMaxSceneForwardLights);
    bgfx::setUniform(lightPositionRangeUniform_, desc.lighting.positionRange.data(), kMaxSceneForwardLights);
    bgfx::setUniform(lightColorIntensityUniform_, desc.lighting.colorIntensity.data(), kMaxSceneForwardLights);
    bgfx::setUniform(lightSpotUniform_, desc.lighting.spot.data(), kMaxSceneForwardLights);
    bgfx::setUniform(lightParamsUniform_, desc.lighting.params.data());
    bgfx::setUniform(ambientColorUniform_, desc.lighting.ambient.data());
    bgfx::setUniform(environmentZenithUniform_, desc.lighting.environmentZenith.data());
    bgfx::setUniform(environmentGroundUniform_, desc.lighting.environmentGround.data());
    bgfx::setUniform(environmentParamsUniform_, desc.lighting.environmentParams.data());
    bgfx::setUniform(shadowViewProjUniform_, desc.shadowMap != nullptr && desc.shadowMap->IsValid() ? desc.shadowMap->lightViewProjection.data() : disabledShadowParams.data());
    bgfx::setUniform(shadowParamsUniform_, desc.shadowMap != nullptr && desc.shadowMap->IsValid() ? desc.shadowMap->params.data() : disabledShadowParams.data());

    // MAT-78/#16: bind the graph material's own textures at their graph slots (>= 6). The builtin slots
    // (0-5) above are for the flattened PBR path; a real graph shader samples its own SAMPLER2D(name, slot),
    // so without this the graph texture is never bound and the surface renders black.
    if (resolution.graphProgram && material != nullptr) {
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

            bgfx::UniformHandle& uniform = graphUniforms_[graphUniform.name];
            if (!bgfx::isValid(uniform)) {
                uniform = bgfx::createUniform(graphUniform.name.c_str(), bgfx::UniformType::Vec4);
            }
            bgfx::setUniform(uniform, value.data());
        }

        for (const RenderMaterialGraphTextureBinding& graphTexture : material->graphProgram.textures) {
            const RenderTextureHandle resolved = desc.resourceMap.ResolveTexture(graphTexture.textureAssetId, graphTexture.colorSpace);
            const RenderTextureResource* textureResource = desc.resources.FindTexture(resolved);
            const bgfx::TextureHandle handle = (textureResource != nullptr && bgfx::isValid(textureResource->texture))
                ? textureResource->texture
                : fallbackWhiteTexture_;

            bgfx::UniformHandle& sampler = graphSamplerUniforms_[graphTexture.samplerName];
            if (!bgfx::isValid(sampler)) {
                sampler = bgfx::createUniform(graphTexture.samplerName.c_str(), bgfx::UniformType::Sampler);
            }
            bgfx::setTexture(static_cast<std::uint8_t>(graphTexture.slot), sampler, handle, graphTexture.samplerFlags);
        }

        if (material->graphProgram.usesSceneColor && bgfx::isValid(desc.sceneColorTexture)) {
            bgfx::UniformHandle& colorSampler = graphSamplerUniforms_["s_kbSceneColor"];
            if (!bgfx::isValid(colorSampler)) {
                colorSampler = bgfx::createUniform("s_kbSceneColor", bgfx::UniformType::Sampler);
            }
            bgfx::setTexture(4U, colorSampler, desc.sceneColorTexture);
        }

        // MAT-80/#18b: bind the opaque scene depth at the reserved slot 5 for graphs that sample it, so
        // SceneDepth / DepthFade read real geometry depth in the transparent pass.
        if (material->graphProgram.usesSceneDepth && bgfx::isValid(desc.sceneDepthTexture)) {
            bgfx::UniformHandle& depthSampler = graphSamplerUniforms_["s_kbSceneDepth"];
            if (!bgfx::isValid(depthSampler)) {
                depthSampler = bgfx::createUniform("s_kbSceneDepth", bgfx::UniformType::Sampler);
            }
            bgfx::setTexture(5U, depthSampler, desc.sceneDepthTexture);
        }
    }
    return resolution.program;
}

} // namespace kb::render
