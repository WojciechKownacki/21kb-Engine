#include "kb/render/RuntimeAssetShaderProvider.hpp"

#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/RuntimeAssetManifest.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <optional>
#include <string>
#include <utility>

namespace kb::render {
namespace {

[[nodiscard]] std::string_view BackendName(bgfx::RendererType::Enum renderer) noexcept {
    switch (renderer) {
    case bgfx::RendererType::Noop:
    case bgfx::RendererType::Direct3D11: return "dxbc";
    case bgfx::RendererType::Direct3D12: return "dxil";
    case bgfx::RendererType::Vulkan: return "spirv";
    case bgfx::RendererType::OpenGL: return "glsl";
    case bgfx::RendererType::OpenGLES: return "essl";
    case bgfx::RendererType::Metal: return "metal";
    case bgfx::RendererType::WebGPU: return "wgsl";
    case bgfx::RendererType::Agc:
    case bgfx::RendererType::Gnm:
    case bgfx::RendererType::Nvn:
    case bgfx::RendererType::Count:
        break;
    }
    return {};
}

[[nodiscard]] bool ValidShaderName(std::string_view name) noexcept {
    if (name.empty() || name.size() > 128U) {
        return false;
    }
    for (const char character : name) {
        const bool valid = (character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') || character == '_' || character == '.' || character == '-';
        if (!valid) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string MaterialQualifier(
    std::uint64_t graphSourceHash,
    std::uint64_t variantKey,
    std::string_view pass,
    std::string_view backend,
    std::string_view platform,
    std::string_view stage) {
    return std::to_string(graphSourceHash) + ":" + std::to_string(variantKey) + ":" +
        std::string{ pass } + ":" + std::string{ backend } + ":" + std::string{ platform } + ":" +
        std::string{ stage };
}

[[nodiscard]] bool ParseNonZeroU64(std::string_view value) noexcept {
    std::uint64_t parsed = 0U;
    const char* const end = value.data() + value.size();
    const auto result = std::from_chars(value.data(), end, parsed);
    return !value.empty() && result.ec == std::errc{} && result.ptr == end && parsed != 0U;
}

[[nodiscard]] bool IsCanonicalMaterialQualifier(
    std::string_view qualifier,
    std::string_view expectedPlatform) noexcept {
    std::array<std::string_view, 6U> fields{};
    if (static_cast<std::size_t>(std::ranges::count(qualifier, ':')) != fields.size() - 1U) {
        return false;
    }
    std::size_t field = 0U;
    std::size_t begin = 0U;
    while (field < fields.size()) {
        const std::size_t separator = qualifier.find(':', begin);
        const std::size_t end = separator == std::string_view::npos ? qualifier.size() : separator;
        fields[field++] = qualifier.substr(begin, end - begin);
        if (separator == std::string_view::npos) {
            break;
        }
        begin = separator + 1U;
    }
    if (field != fields.size() || !ParseNonZeroU64(fields[0]) ||
        !ParseNonZeroU64(fields[1]) || fields[4] != expectedPlatform) {
        return false;
    }
    const bool validPass = fields[2] == "BaseOpaque" || fields[2] == "GBuffer" ||
        fields[2] == "ShadowDepth" || fields[2] == "BaseTransparent";
    kb::assets::bake::ShaderBakePlatform platform{};
    if (!kb::assets::bake::TryParseShaderBakePlatform(fields[4], platform)) {
        return false;
    }
    std::optional<kb::assets::bake::ShaderBakeBackend> backend;
    for (std::uint32_t index = 0U; index < kb::assets::bake::kShaderBakeBackendCount; ++index) {
        const auto candidate = static_cast<kb::assets::bake::ShaderBakeBackend>(index);
        if (kb::assets::bake::ShaderBakeBackendName(candidate) == fields[3]) {
            backend = candidate;
            break;
        }
    }
    return validPass && backend.has_value() &&
        kb::assets::bake::ShaderBakePlatformSupportsBackend(platform, *backend) &&
        (fields[5] == "fragment" || fields[5] == "vertex");
}

[[nodiscard]] std::uint64_t DigestRevision(const kb::assets::bake::AssetBakeDigest& digest) noexcept {
    std::uint64_t value = digest.high ^ (digest.low + 0x9e3779b97f4a7c15ULL +
        (digest.high << 6U) + (digest.high >> 2U));
    return value == 0U ? 1U : value;
}

} // namespace

RuntimeAssetShaderProvider::RuntimeAssetShaderProvider(
    std::shared_ptr<kb::assets::bake::RuntimeAssetPack> pack,
    std::string platform)
    : pack_{ std::move(pack) }
    , platform_{ std::move(platform) } {}

std::shared_ptr<RuntimeAssetShaderProvider> RuntimeAssetShaderProvider::Create(
    std::shared_ptr<kb::assets::bake::RuntimeAssetPack> pack,
    std::string& error) {
    error.clear();
    if (pack == nullptr || !pack->IsMounted()) {
        error = "runtime shader provider requires a mounted package";
        return {};
    }
    kb::assets::bake::BakeTargetProfile profile{};
    if (!kb::assets::bake::TryFindBakeTargetProfile(pack->Manifest().targetProfileId, profile) ||
        pack->Manifest().targetProfileHash != kb::assets::bake::BakeTargetProfileFingerprint(profile)) {
        error = "runtime shader provider rejected the package target profile";
        return {};
    }
    auto provider = std::shared_ptr<RuntimeAssetShaderProvider>{
        new RuntimeAssetShaderProvider{ pack, std::string{ kb::assets::bake::ShaderBakePlatformName(profile.shaderPlatform) } }
    };
    for (const kb::assets::bake::RuntimeAssetManifestEntry& asset : pack->Manifest().assets) {
        for (const kb::assets::bake::RuntimeArtifactReference& artifact : asset.artifacts) {
            if (artifact.encoding != kb::assets::bake::RuntimeArtifactEncoding::MaterialShader) {
                continue;
            }
            if (!IsCanonicalMaterialQualifier(artifact.qualifier, provider->platform_)) {
                error = "runtime package contains a malformed material shader qualifier: " + artifact.qualifier;
                return {};
            }
            const auto [iterator, inserted] = provider->materialShaders_.emplace(
                artifact.qualifier, IndexedShader{ .asset = asset.id, .digest = artifact.digest });
            if (!inserted && iterator->second.digest != artifact.digest) {
                error = "runtime package maps one material shader identity to different artifacts";
                return {};
            }
        }
    }
    return provider;
}

bool RuntimeAssetShaderProvider::ReadFixedShader(
    bgfx::RendererType::Enum renderer,
    std::string_view name,
    std::vector<std::uint8_t>& bytes,
    std::uint64_t& revision) const {
    bytes.clear();
    revision = 0U;
    const std::string_view backend = BackendName(renderer);
    if (backend.empty() || !ValidShaderName(name)) {
        return false;
    }
    const std::string path = "/Engine/Shaders/" + platform_ + "/" + std::string{ backend } + "/" +
        std::string{ name } + ".bin";
    if (pack_->ReadAuxiliaryFile(path, bytes) != kb::assets::bake::RuntimeAssetPackStatus::Success ||
        bytes.empty()) {
        bytes.clear();
        return false;
    }
    const auto iterator = std::ranges::find(
        pack_->Manifest().auxiliaryFiles, path, &kb::assets::bake::RuntimeAuxiliaryFileEntry::virtualPath);
    if (iterator == pack_->Manifest().auxiliaryFiles.end()) {
        bytes.clear();
        return false;
    }
    revision = DigestRevision(iterator->artifactDigest);
    return true;
}

bool RuntimeAssetShaderProvider::ReadMaterialShader(
    std::uint64_t graphSourceHash,
    std::uint64_t variantKey,
    std::string_view pass,
    bgfx::RendererType::Enum renderer,
    std::string_view stage,
    std::vector<std::uint8_t>& bytes,
    std::uint64_t& revision) const {
    bytes.clear();
    revision = 0U;
    const std::string_view backend = BackendName(renderer);
    if (backend.empty() || (stage != "fragment" && stage != "vertex")) {
        return false;
    }
    const std::string qualifier = MaterialQualifier(
        graphSourceHash, variantKey, pass, backend, platform_, stage);
    const auto indexed = materialShaders_.find(qualifier);
    if (indexed == materialShaders_.end()) {
        return false;
    }
    kb::assets::bake::RuntimeAssetPayload payload{};
    if (pack_->ReadAssetPayload(
            indexed->second.asset,
            kb::assets::bake::RuntimeArtifactEncoding::MaterialShader,
            qualifier,
            payload) != kb::assets::bake::RuntimeAssetPackStatus::Success ||
        payload.blocks.size() != 1U || payload.blocks.front().name != "primary" ||
        payload.blocks.front().bytes.empty()) {
        return false;
    }
    bytes = std::move(payload.blocks.front().bytes);
    revision = DigestRevision(indexed->second.digest);
    return true;
}

std::uint64_t RuntimeAssetShaderProvider::MaterialShaderRevision(
    std::uint64_t graphSourceHash,
    std::uint64_t variantKey,
    std::string_view pass,
    bgfx::RendererType::Enum renderer) const noexcept {
    const std::string_view backend = BackendName(renderer);
    if (backend.empty()) {
        return 0U;
    }
    try {
        const auto findRevision = [&](std::string_view stage) {
            const std::string qualifier = MaterialQualifier(
                graphSourceHash, variantKey, pass, backend, platform_, stage);
            const auto iterator = materialShaders_.find(qualifier);
            return iterator == materialShaders_.end() ? 0U : DigestRevision(iterator->second.digest);
        };
        const std::uint64_t fragment = findRevision("fragment");
        const std::uint64_t vertex = findRevision("vertex");
        return fragment == 0U ? 0U : fragment ^ (vertex + 0x9e3779b97f4a7c15ULL +
            (fragment << 6U) + (fragment >> 2U));
    } catch (...) {
        return 0U;
    }
}

} // namespace kb::render
