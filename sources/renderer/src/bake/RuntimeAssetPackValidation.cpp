#include "kb/render/bake/RuntimeAssetPackValidation.hpp"

#include "assets/MiniaudioClipResolver.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/RuntimeAssetShaderProvider.hpp"
#include "kb/render/ShaderManifest.hpp"
#include "kb/render/bake/MeshBaker.hpp"
#include "kb/render/bake/TextureBaker.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderPrewarmer.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "kb/render/runtime/RuntimeRenderAssetDiscovery.hpp"
#include "runtime/RuntimeMaterialGraphDependencyLoader.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <exception>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::render {
namespace {

constexpr std::array<std::string_view, 4U> kMaterialPasses{
    "BaseOpaque", "GBuffer", "ShadowDepth", "BaseTransparent"
};

struct MaterialShaderQualifier final {
    std::uint64_t graphSourceHash = 0U;
    std::uint64_t variantKey = 0U;
    std::string_view pass;
    std::string_view backend;
    std::string_view platform;
    std::string_view stage;
};

[[nodiscard]] RuntimeAssetPackValidationResult Failure(std::string error) {
    return RuntimeAssetPackValidationResult{ .error = std::move(error) };
}

[[nodiscard]] bgfx::RendererType::Enum RendererForShaderBackend(
    kb::assets::bake::ShaderBakeBackend backend) noexcept {
    using kb::assets::bake::ShaderBakeBackend;
    switch (backend) {
    case ShaderBakeBackend::Dxbc: return bgfx::RendererType::Direct3D11;
    case ShaderBakeBackend::Dxil: return bgfx::RendererType::Direct3D12;
    case ShaderBakeBackend::Spirv: return bgfx::RendererType::Vulkan;
    case ShaderBakeBackend::Glsl: return bgfx::RendererType::OpenGL;
    case ShaderBakeBackend::Essl: return bgfx::RendererType::OpenGLES;
    case ShaderBakeBackend::Metal: return bgfx::RendererType::Metal;
    case ShaderBakeBackend::Wgsl: return bgfx::RendererType::WebGPU;
    }
    return bgfx::RendererType::Count;
}

[[nodiscard]] bool ParseNonZeroU64(std::string_view text, std::uint64_t& value) noexcept {
    value = 0U;
    const char* const end = text.data() + text.size();
    const auto parsed = std::from_chars(text.data(), end, value);
    return !text.empty() && parsed.ec == std::errc{} && parsed.ptr == end && value != 0U;
}

[[nodiscard]] std::optional<MaterialShaderQualifier> ParseMaterialShaderQualifier(
    std::string_view qualifier) noexcept {
    std::array<std::string_view, 6U> fields{};
    std::size_t begin = 0U;
    for (std::size_t index = 0U; index < fields.size(); ++index) {
        const std::size_t separator = qualifier.find(':', begin);
        const std::size_t end = separator == std::string_view::npos ? qualifier.size() : separator;
        fields[index] = qualifier.substr(begin, end - begin);
        if (index + 1U == fields.size()) {
            if (separator != std::string_view::npos) {
                return std::nullopt;
            }
        } else if (separator == std::string_view::npos) {
            return std::nullopt;
        }
        begin = end + 1U;
    }

    MaterialShaderQualifier parsed{};
    if (!ParseNonZeroU64(fields[0], parsed.graphSourceHash) ||
        !ParseNonZeroU64(fields[1], parsed.variantKey)) {
        return std::nullopt;
    }
    parsed.pass = fields[2];
    parsed.backend = fields[3];
    parsed.platform = fields[4];
    parsed.stage = fields[5];
    return parsed;
}

[[nodiscard]] bool HasSourceBytes(
    const kb::assets::bake::RuntimeAssetManifestEntry& asset) noexcept {
    return std::ranges::any_of(asset.artifacts, [](const auto& artifact) {
        return artifact.encoding == kb::assets::bake::RuntimeArtifactEncoding::SourceBytes;
    });
}

[[nodiscard]] std::string DescribeAsset(
    const kb::assets::bake::RuntimeAssetManifestEntry& asset) {
    return asset.virtualPath + " [" + asset.type + ", id=" + std::to_string(asset.id.value) + "]";
}

[[nodiscard]] bool IsGraphBacked(const RenderMaterialGraphDocument& graph) noexcept {
    return !(graph.nodes.size() == 1U && graph.links.empty() &&
        graph.nodes.front().kind == RenderMaterialGraphNodeKind::MaterialOutput);
}

[[nodiscard]] bool IsValidShaderBlob(
    std::span<const std::uint8_t> bytes,
    PrewarmShaderStage expectedStage) noexcept {
    const std::optional<PrewarmShaderBytecode> parsed = ExtractBgfxShaderBytecode(bytes);
    return parsed.has_value() && parsed->stage == expectedStage && parsed->size != 0U;
}

[[nodiscard]] std::optional<PrewarmShaderStage> ExpectedFixedShaderStage(
    std::string_view name) noexcept {
    const auto shader = std::ranges::find(
        RequiredShaderManifest(), name, &ShaderManifestEntry::name);
    if (shader == RequiredShaderManifest().end()) {
        return std::nullopt;
    }
    switch (shader->stage) {
    case ShaderStage::Vertex: return PrewarmShaderStage::Vertex;
    case ShaderStage::Fragment: return PrewarmShaderStage::Fragment;
    case ShaderStage::Compute: return PrewarmShaderStage::Compute;
    }
    return std::nullopt;
}

[[nodiscard]] RuntimeAssetPackValidationResult ValidateMaterialShaderCompleteness(
    const kb::assets::bake::RuntimeAssetManifestEntry& asset,
    const kb::assets::bake::BakeTargetProfile& profile,
    kb::assets::AssetManager& assets,
    const RuntimeAssetShaderProvider& shaderProvider) {
    std::vector<MaterialShaderQualifier> shaders;
    shaders.reserve(asset.artifacts.size());
    for (const kb::assets::bake::RuntimeArtifactReference& artifact : asset.artifacts) {
        if (artifact.encoding != kb::assets::bake::RuntimeArtifactEncoding::MaterialShader) {
            continue;
        }
        const std::optional<MaterialShaderQualifier> parsed =
            ParseMaterialShaderQualifier(artifact.qualifier);
        if (!parsed.has_value()) {
            return Failure("material shader qualifier is malformed for " + DescribeAsset(asset));
        }
        shaders.push_back(*parsed);
    }

    if (asset.type != "RenderMaterial") {
        return shaders.empty()
            ? RuntimeAssetPackValidationResult{}
            : Failure("material shader artifacts are attached to a non-material asset: " +
                DescribeAsset(asset));
    }
    if (!asset.runtimeLoadable) {
        return shaders.empty()
            ? RuntimeAssetPackValidationResult{}
            : Failure("material shader artifacts are attached to a non-runtime material: " +
                DescribeAsset(asset));
    }
    if (!HasSourceBytes(asset)) {
        return Failure("runtime material has no source payload: " + DescribeAsset(asset));
    }

    const kb::assets::AssetHandle<RenderMaterialAssetData> loaded =
        assets.Load<RenderMaterialAssetData>(asset.id);
    const kb::assets::AssetMetadata* const metadata = assets.Registry().Find(asset.id);
    if (!loaded.IsLoaded() || metadata == nullptr) {
        return Failure("runtime material source could not be inspected for shader completeness: " +
            DescribeAsset(asset));
    }
    const RenderMaterialSourceGraphResolveResult resolved =
        ResolveRenderMaterialSourceGraph(assets, *metadata, *loaded);
    if (!resolved.graph.has_value()) {
        return Failure("authoritative runtime material graph could not be resolved for " +
            DescribeAsset(asset));
    }

    const bool graphBacked = IsGraphBacked(*resolved.graph);
    if (!graphBacked) {
        return shaders.empty()
            ? RuntimeAssetPackValidationResult{}
            : Failure("non-graph material contains graph shader artifacts: " + DescribeAsset(asset));
    }
    if (shaders.empty()) {
        return Failure("graph material has no cooked shader artifacts: " + DescribeAsset(asset));
    }

    const RuntimeMaterialFunctionLibraryBuildResult functions =
        BuildRuntimeMaterialFunctionLibrary(assets, *resolved.graph);
    if (std::ranges::any_of(functions.diagnostics, [](const RenderMaterialGraphDiagnostic& diagnostic) {
            return diagnostic.severity == RenderMaterialGraphDiagnosticSeverity::Error;
        })) {
        return Failure("runtime material function graph could not be resolved for shader validation: " +
            DescribeAsset(asset));
    }
    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(
        *resolved.graph,
        RenderMaterialGraphBuildContext{
            .assetId = asset.id.value,
            .sourcePath = asset.virtualPath,
            .functionLibrary = &functions.library,
        });
    if (!compiled.Succeeded()) {
        return Failure("runtime material graph could not be recompiled for shader validation: " +
            DescribeAsset(asset));
    }

    const std::uint64_t graphSourceHash = compiled.shader.sourceHash;
    const std::uint64_t variantKey = ComputeRenderMaterialGraphVariantKey(compiled.shader);
    const std::string_view expectedPlatform =
        kb::assets::bake::ShaderBakePlatformName(profile.shaderPlatform);
    const bool requiresVertex = compiled.shader.reflection.hasWorldPositionOffset ||
        compiled.shader.reflection.hasCustomizedUv0 ||
        compiled.shader.reflection.hasDisplacement;
    for (const MaterialShaderQualifier& shader : shaders) {
        if (shader.graphSourceHash != graphSourceHash || shader.variantKey != variantKey) {
            return Failure("one material maps to multiple graph shader identities: " +
                DescribeAsset(asset));
        }
        if (shader.platform != expectedPlatform) {
            return Failure("material shader targets the wrong platform for " + DescribeAsset(asset));
        }
        bool backendEnabled = false;
        for (std::uint32_t backendIndex = 0U;
             backendIndex < kb::assets::bake::kShaderBakeBackendCount;
             ++backendIndex) {
            const auto backend =
                static_cast<kb::assets::bake::ShaderBakeBackend>(backendIndex);
            if (kb::assets::bake::HasShaderBakeBackend(profile.shaderBackends, backend) &&
                shader.backend == kb::assets::bake::ShaderBakeBackendName(backend)) {
                backendEnabled = true;
                break;
            }
        }
        if (!backendEnabled) {
            return Failure("material shader contains a backend outside the target profile for " +
                DescribeAsset(asset));
        }
        if (std::ranges::find(kMaterialPasses, shader.pass) == kMaterialPasses.end() ||
            (shader.stage != "fragment" && shader.stage != "vertex")) {
            return Failure("material shader contains an unsupported pass or stage for " +
                DescribeAsset(asset));
        }
        if (!requiresVertex && shader.stage == "vertex") {
            return Failure("material without vertex-domain outputs contains generated vertex shaders: " +
                DescribeAsset(asset));
        }
    }

    std::vector<std::uint8_t> bytes;
    for (std::uint32_t backendIndex = 0U;
         backendIndex < kb::assets::bake::kShaderBakeBackendCount;
         ++backendIndex) {
        const auto backend = static_cast<kb::assets::bake::ShaderBakeBackend>(backendIndex);
        if (!kb::assets::bake::HasShaderBakeBackend(profile.shaderBackends, backend)) {
            continue;
        }
        const bgfx::RendererType::Enum renderer = RendererForShaderBackend(backend);
        for (const std::string_view pass : kMaterialPasses) {
            const auto validateStage = [&](std::string_view stage) {
                bytes.clear();
                std::uint64_t revision = 0U;
                const bool read = shaderProvider.ReadMaterialShader(
                           graphSourceHash,
                           variantKey,
                           pass,
                           renderer,
                           stage,
                           bytes,
                           revision);
                const PrewarmShaderStage expectedStage = stage == "vertex"
                    ? PrewarmShaderStage::Vertex
                    : PrewarmShaderStage::Fragment;
                return read && revision != 0U && IsValidShaderBlob(bytes, expectedStage);
            };
            if (!validateStage("fragment")) {
                return Failure("graph material is missing a required fragment shader (" +
                    std::string{ pass } + "/" +
                    std::string{ kb::assets::bake::ShaderBakeBackendName(backend) } + "): " +
                    DescribeAsset(asset));
            }
            if (requiresVertex && !validateStage("vertex")) {
                return Failure("vertex-domain graph material is missing a required vertex shader (" +
                    std::string{ pass } + "/" +
                    std::string{ kb::assets::bake::ShaderBakeBackendName(backend) } + "): " +
                    DescribeAsset(asset));
            }
        }
    }
    return {};
}

[[nodiscard]] RuntimeAssetPackValidationResult ValidateMountedRuntimeAssetPack(
    const std::shared_ptr<kb::assets::bake::RuntimeAssetPack>& pack,
    const kb::assets::bake::BakeTargetProfile& profile) {
    if (pack == nullptr || !pack->IsMounted()) {
        return Failure("runtime asset pack is not mounted");
    }
    if (pack->Manifest().targetProfileId != profile.identifier ||
        pack->Manifest().targetProfileHash !=
            kb::assets::bake::BakeTargetProfileFingerprint(profile)) {
        return Failure("runtime asset pack target profile does not match the validation profile");
    }

    kb::scene::Scene sceneValidation;
    kb::audio_miniaudio::MiniaudioClipResolver audioClipResolver;
    RuntimeRenderAssetDiscovery renderAssetDiscovery;
    renderAssetDiscovery.Ensure(sceneValidation, 0U);
    kb::assets::AssetManager& validationAssets = sceneValidation.Assets().Manager();
    if (!validationAssets.MountRuntimePack(pack)) {
        return Failure("runtime asset catalogue could not be mounted: " +
            validationAssets.LastError());
    }

    const kb::assets::bake::RuntimeAssetManifestEntry* const defaultMap =
        pack->FindAsset(pack->Manifest().settings.defaultMap);
    if (defaultMap == nullptr || defaultMap->type != "Scene" || !defaultMap->runtimeLoadable ||
        !HasSourceBytes(*defaultMap)) {
        return Failure("runtime default map is missing or is not a loadable Scene source");
    }

    for (const kb::assets::bake::RuntimeAssetManifestEntry& asset : pack->Manifest().assets) {
        if (!asset.runtimeLoadable || !HasSourceBytes(asset)) {
            continue;
        }
        if (!validationAssets.LoadOpaque(asset.id)) {
            std::string error = "runtime source payload is not semantically loadable: " +
                DescribeAsset(asset);
            if (!validationAssets.LastError().empty()) {
                error += ": " + validationAssets.LastError();
            }
            return Failure(std::move(error));
        }
        if (asset.type == "AudioClip" ||
            (asset.type == "ImportedAsset" && asset.importCategory == "Audio")) {
            const kb::audio_miniaudio::MiniaudioClipResolver::Resolution resolved =
                audioClipResolver.Resolve(sceneValidation, asset.id.value);
            if (!resolved.Succeeded()) {
                return Failure("runtime audio source payload is not decode-ready: " +
                    DescribeAsset(asset));
            }
        }
    }

    std::string shaderProviderError;
    const std::shared_ptr<RuntimeAssetShaderProvider> shaderProvider =
        RuntimeAssetShaderProvider::Create(pack, shaderProviderError);
    if (shaderProvider == nullptr) {
        return Failure("runtime shader index validation failed: " + shaderProviderError);
    }
    for (const kb::assets::bake::RuntimeAssetManifestEntry& asset : pack->Manifest().assets) {
        const RuntimeAssetPackValidationResult completeness =
            ValidateMaterialShaderCompleteness(asset, profile, validationAssets, *shaderProvider);
        if (!completeness.Succeeded()) {
            return completeness;
        }
    }

    std::vector<std::uint8_t> bytes;
    for (std::uint32_t backendIndex = 0U;
         backendIndex < kb::assets::bake::kShaderBakeBackendCount;
         ++backendIndex) {
        const auto backend = static_cast<kb::assets::bake::ShaderBakeBackend>(backendIndex);
        if (!kb::assets::bake::HasShaderBakeBackend(profile.shaderBackends, backend)) {
            continue;
        }
        const ShaderRuntimeFeatureMask features = PackagedGameShaderFeatures(backend);
        const bgfx::RendererType::Enum renderer = RendererForShaderBackend(backend);
        for (const std::string_view shader : RequiredPackagedShaderNames(features)) {
            bytes.clear();
            std::uint64_t shaderRevision = 0U;
            const std::optional<PrewarmShaderStage> expectedStage =
                ExpectedFixedShaderStage(shader);
            if (!expectedStage.has_value() || !shaderProvider->ReadFixedShader(
                    renderer, shader, bytes, shaderRevision) ||
                shaderRevision == 0U || !IsValidShaderBlob(bytes, *expectedStage)) {
                return Failure("required runtime shader is missing or corrupt: " +
                    std::string{ kb::assets::bake::ShaderBakeBackendName(backend) } + "/" +
                    std::string{ shader });
            }
        }
    }

    for (const kb::assets::bake::AssetPackArtifactEntry& artifact : pack->Artifacts()) {
        for (const kb::assets::bake::AssetPackBlockEntry& block : artifact.blocks) {
            const kb::assets::bake::AssetPackReadStatus status =
                pack->ReadArtifactBlock(artifact.key, block.name, bytes);
            if (status != kb::assets::bake::AssetPackReadStatus::Success) {
                return Failure("runtime asset payload validation failed: " +
                    std::string{ kb::assets::bake::ToString(status) });
            }
        }
    }

    for (const kb::assets::bake::RuntimeAssetManifestEntry& asset : pack->Manifest().assets) {
        for (const kb::assets::bake::RuntimeArtifactReference& artifact : asset.artifacts) {
            kb::assets::bake::RuntimeAssetPayload payload{};
            const kb::assets::bake::RuntimeAssetPackStatus readStatus =
                pack->ReadAssetPayload(asset.id, artifact.encoding, artifact.qualifier, payload);
            if (readStatus != kb::assets::bake::RuntimeAssetPackStatus::Success) {
                return Failure("runtime asset payload is corrupt for " + DescribeAsset(asset) +
                    ": " + std::string{ kb::assets::bake::ToString(readStatus) });
            }
            if (artifact.encoding == kb::assets::bake::RuntimeArtifactEncoding::BakedTexture) {
                if (payload.blocks.size() != 1U ||
                    payload.blocks.front().name !=
                        kb::assets::bake::kBakedAssetPrimaryBlockName) {
                    return Failure("baked texture block layout is corrupt for " +
                        DescribeAsset(asset));
                }
                RenderTextureAssetData texture{};
                if (!kb::render::bake::ReadBakedTexture(
                        payload.blocks.front().bytes, texture) ||
                    !texture.gpuBlocks.has_value() ||
                    !kb::render::bake::BakedTextureFormatMatchesFamily(
                        texture.gpuBlocks->format, artifact.qualifier)) {
                    return Failure("baked texture payload is corrupt for " +
                        DescribeAsset(asset));
                }
            } else if (artifact.encoding ==
                       kb::assets::bake::RuntimeArtifactEncoding::BakedMesh) {
                if (payload.blocks.empty() ||
                    payload.blocks.front().name !=
                        kb::assets::bake::kBakedAssetPrimaryBlockName) {
                    return Failure("baked mesh block layout is corrupt for " +
                        DescribeAsset(asset));
                }
                std::vector<std::vector<std::uint8_t>> chunks;
                chunks.reserve(payload.blocks.size() - 1U);
                for (std::size_t chunkIndex = 1U;
                     chunkIndex < payload.blocks.size();
                     ++chunkIndex) {
                    if (payload.blocks[chunkIndex].name !=
                        kb::render::bake::BakedMeshChunkBlockName(
                            static_cast<std::uint32_t>(chunkIndex - 1U))) {
                        return Failure("baked mesh chunk layout is corrupt for " +
                            DescribeAsset(asset));
                    }
                    chunks.push_back(payload.blocks[chunkIndex].bytes);
                }
                RenderMeshAssetData mesh{};
                if (!kb::render::bake::ReadBakedMesh(
                        payload.blocks.front().bytes, chunks, mesh) ||
                    !kb::render::bake::BakedMeshMatchesTargetProfile(
                        payload.blocks.front().bytes, chunks, profile)) {
                    return Failure("baked mesh payload is corrupt for " +
                        DescribeAsset(asset));
                }
            } else if (artifact.encoding ==
                       kb::assets::bake::RuntimeArtifactEncoding::MaterialShader) {
                if (payload.blocks.size() != 1U ||
                    payload.blocks.front().name !=
                        kb::assets::bake::kBakedAssetPrimaryBlockName ||
                    payload.blocks.front().bytes.empty()) {
                    return Failure("material shader payload is corrupt for " +
                        DescribeAsset(asset));
                }
            }
        }
    }

    for (const kb::assets::bake::RuntimeAuxiliaryFileEntry& file :
         pack->Manifest().auxiliaryFiles) {
        if (pack->ReadAuxiliaryFile(file.virtualPath, bytes) !=
            kb::assets::bake::RuntimeAssetPackStatus::Success) {
            return Failure("runtime auxiliary file is corrupt: " + file.virtualPath);
        }
    }
    return {};
}

} // namespace

RuntimeAssetPackValidationResult ValidateRuntimeAssetPack(
    const std::shared_ptr<kb::assets::bake::RuntimeAssetPack>& pack,
    const kb::assets::bake::BakeTargetProfile& expectedProfile) {
    try {
        return ValidateMountedRuntimeAssetPack(pack, expectedProfile);
    } catch (const std::exception& error) {
        return Failure("runtime asset pack validation raised an exception: " +
            std::string{ error.what() });
    } catch (...) {
        return Failure("runtime asset pack validation raised an unknown exception");
    }
}

} // namespace kb::render
