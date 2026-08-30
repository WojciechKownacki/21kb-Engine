#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "PackagedRuntimeModuleContract.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneDocument.hpp"
#include "kb/render/bake/MeshBaker.hpp"
#include "kb/render/bake/TextureBaker.hpp"
#include "kb/render/RuntimeAssetShaderProvider.hpp"
#include "kb/render/ShaderManifest.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

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

int ValidateRuntimePack(std::string_view expectedProfileId, const std::filesystem::path& packPath) {
    kb::assets::bake::BakeTargetProfile profile{};
    if (!kb::assets::bake::TryFindBakeTargetProfile(expectedProfileId, profile)) {
        std::cerr << "unknown target profile\n";
        return 2;
    }
    auto pack = std::make_shared<kb::assets::bake::RuntimeAssetPack>();
    const kb::assets::bake::RuntimeAssetPackStatus mount = pack->Mount(packPath, profile);
    if (mount != kb::assets::bake::RuntimeAssetPackStatus::Success) {
        std::cerr << "runtime asset pack validation failed: "
                  << kb::assets::bake::ToString(mount) << '\n';
        return 1;
    }
    if (const std::optional<std::string_view> unsupported =
            kb::game::FirstUnsupportedPackagedRuntimeModule(
                profile.identifier, pack->Manifest().descriptor);
        unsupported.has_value()) {
        std::cerr << "target runtime does not provide configured module: "
                  << *unsupported << '\n';
        return 1;
    }
    kb::scene::Scene sceneValidation;
    kb::assets::AssetManager& validationAssets = sceneValidation.Assets().Manager();
    if (!validationAssets.MountRuntimePack(pack)) {
        std::cerr << "runtime default Scene catalogue validation failed\n";
        return 1;
    }
    const kb::assets::bake::RuntimeAssetManifestEntry* defaultMap =
        pack->FindAsset(pack->Manifest().settings.defaultMap);
    if (defaultMap == nullptr || defaultMap->type != "Scene" || !defaultMap->runtimeLoadable) {
        std::cerr << "runtime default map is missing or is not a loadable Scene\n";
        return 1;
    }
    const kb::assets::AssetHandle<kb::scene::SceneDocument> loadedDefaultMap =
        validationAssets.Load<kb::scene::SceneDocument>(defaultMap->id);
    if (!loadedDefaultMap.IsLoaded()) {
        std::cerr << "runtime default Scene payload is not loadable";
        if (!validationAssets.LastError().empty()) {
            std::cerr << ": " << validationAssets.LastError();
        }
        std::cerr << '\n';
        return 1;
    }
    std::vector<std::uint8_t> bytes;
    std::string shaderProviderError;
    const std::shared_ptr<kb::render::RuntimeAssetShaderProvider> shaderProvider =
        kb::render::RuntimeAssetShaderProvider::Create(pack, shaderProviderError);
    if (shaderProvider == nullptr) {
        std::cerr << "runtime shader index validation failed: " << shaderProviderError << '\n';
        return 1;
    }
    std::uint64_t shaderRevision = 0U;
    for (std::uint32_t backendIndex = 0U;
         backendIndex < kb::assets::bake::kShaderBakeBackendCount;
         ++backendIndex) {
        const auto backend = static_cast<kb::assets::bake::ShaderBakeBackend>(backendIndex);
        if (!kb::assets::bake::HasShaderBakeBackend(profile.shaderBackends, backend)) {
            continue;
        }
        const kb::render::ShaderRuntimeFeatureMask features =
            kb::render::PackagedGameShaderFeatures(backend);
        const bgfx::RendererType::Enum renderer = RendererForShaderBackend(backend);
        for (const std::string_view shader :
             kb::render::RequiredPackagedShaderNames(features)) {
            bytes.clear();
            shaderRevision = 0U;
            if (!shaderProvider->ReadFixedShader(renderer, shader, bytes, shaderRevision) ||
                bytes.empty() || shaderRevision == 0U) {
                std::cerr << "required runtime shader is missing or corrupt: "
                          << kb::assets::bake::ShaderBakeBackendName(backend) << '/'
                          << shader << '\n';
                return 1;
            }
        }
    }
    for (const kb::assets::bake::AssetPackArtifactEntry& artifact : pack->Artifacts()) {
        for (const kb::assets::bake::AssetPackBlockEntry& block : artifact.blocks) {
            const kb::assets::bake::AssetPackReadStatus status =
                pack->ReadArtifactBlock(artifact.key, block.name, bytes);
            if (status != kb::assets::bake::AssetPackReadStatus::Success) {
                std::cerr << "runtime asset payload validation failed: "
                          << kb::assets::bake::ToString(status) << '\n';
                return 1;
            }
        }
    }
    for (const kb::assets::bake::RuntimeAssetManifestEntry& asset : pack->Manifest().assets) {
        for (const kb::assets::bake::RuntimeArtifactReference& artifact : asset.artifacts) {
            kb::assets::bake::RuntimeAssetPayload payload{};
            const kb::assets::bake::RuntimeAssetPackStatus readStatus =
                pack->ReadAssetPayload(asset.id, artifact.encoding, artifact.qualifier, payload);
            if (readStatus != kb::assets::bake::RuntimeAssetPackStatus::Success) {
                std::cerr << "runtime asset payload is corrupt: "
                          << kb::assets::bake::ToString(readStatus) << '\n';
                return 1;
            }
            if (artifact.encoding == kb::assets::bake::RuntimeArtifactEncoding::BakedTexture) {
                if (payload.blocks.size() != 1U ||
                    payload.blocks.front().name != kb::assets::bake::kBakedAssetPrimaryBlockName) {
                    std::cerr << "baked texture block layout is corrupt\n";
                    return 1;
                }
                kb::render::RenderTextureAssetData texture{};
                if (!kb::render::bake::ReadBakedTexture(payload.blocks.front().bytes, texture) ||
                    !texture.gpuBlocks.has_value() ||
                    !kb::render::bake::BakedTextureFormatMatchesFamily(
                        texture.gpuBlocks->format, artifact.qualifier)) {
                    std::cerr << "baked texture payload is corrupt\n";
                    return 1;
                }
            } else if (artifact.encoding == kb::assets::bake::RuntimeArtifactEncoding::BakedMesh) {
                if (payload.blocks.empty() ||
                    payload.blocks.front().name != kb::assets::bake::kBakedAssetPrimaryBlockName) {
                    std::cerr << "baked mesh block layout is corrupt\n";
                    return 1;
                }
                std::vector<std::vector<std::uint8_t>> chunks;
                chunks.reserve(payload.blocks.size() - 1U);
                for (std::size_t chunkIndex = 1U; chunkIndex < payload.blocks.size(); ++chunkIndex) {
                    if (payload.blocks[chunkIndex].name !=
                        kb::render::bake::BakedMeshChunkBlockName(
                            static_cast<std::uint32_t>(chunkIndex - 1U))) {
                        std::cerr << "baked mesh chunk layout is corrupt\n";
                        return 1;
                    }
                    chunks.push_back(payload.blocks[chunkIndex].bytes);
                }
                kb::render::RenderMeshAssetData mesh{};
                if (!kb::render::bake::ReadBakedMesh(payload.blocks.front().bytes, chunks, mesh)) {
                    std::cerr << "baked mesh payload is corrupt\n";
                    return 1;
                }
            } else if (artifact.encoding ==
                           kb::assets::bake::RuntimeArtifactEncoding::MaterialShader) {
                if (payload.blocks.size() != 1U ||
                    payload.blocks.front().name != kb::assets::bake::kBakedAssetPrimaryBlockName ||
                    payload.blocks.front().bytes.empty()) {
                    std::cerr << "material shader payload is corrupt\n";
                    return 1;
                }
            }
        }
    }
    for (const kb::assets::bake::RuntimeAuxiliaryFileEntry& file : pack->Manifest().auxiliaryFiles) {
        if (pack->ReadAuxiliaryFile(file.virtualPath, bytes) !=
            kb::assets::bake::RuntimeAssetPackStatus::Success) {
            std::cerr << "runtime auxiliary file is corrupt\n";
            return 1;
        }
    }
    return 0;
}

#if defined(_WIN32)
[[nodiscard]] std::optional<std::string> NarrowAscii(std::wstring_view text) {
    std::string result;
    result.reserve(text.size());
    for (const wchar_t value : text) {
        if (value > 0x7F) {
            return std::nullopt;
        }
        result.push_back(static_cast<char>(value));
    }
    return result;
}
#endif

} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::cerr << "usage: kb_runtime_asset_pack_validator <target-profile> <input.kbpack>\n";
        return 2;
    }
    const std::optional<std::string> profile = NarrowAscii(argv[1]);
    if (!profile.has_value()) {
        std::cerr << "target profile must be ASCII\n";
        return 2;
    }
    return ValidateRuntimePack(*profile, std::filesystem::path{ argv[2] });
}
#else
int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: kb_runtime_asset_pack_validator <target-profile> <input.kbpack>\n";
        return 2;
    }
    return ValidateRuntimePack(argv[1], std::filesystem::path{ argv[2] });
}
#endif
