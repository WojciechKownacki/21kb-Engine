#pragma once

#include "kb/render/ShaderBinaryProvider.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/bake/AssetBakeKey.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace kb::assets::bake {
class RuntimeAssetPack;
}

namespace kb::render {

// Direct package-backed shader provider. It indexes immutable manifest
// identities once and reads verified artifact blocks on demand; no extraction,
// pseudo-path or writable cache participates in packaged runtime loading.
class RuntimeAssetShaderProvider final : public ShaderBinaryProvider {
public:
    [[nodiscard]] static std::shared_ptr<RuntimeAssetShaderProvider> Create(
        std::shared_ptr<kb::assets::bake::RuntimeAssetPack> pack,
        std::string& error);

    [[nodiscard]] bool ReadFixedShader(
        bgfx::RendererType::Enum renderer,
        std::string_view name,
        std::vector<std::uint8_t>& bytes,
        std::uint64_t& revision) const override;
    [[nodiscard]] bool ReadMaterialShader(
        std::uint64_t graphSourceHash,
        std::uint64_t variantKey,
        std::string_view pass,
        bgfx::RendererType::Enum renderer,
        std::string_view stage,
        std::vector<std::uint8_t>& bytes,
        std::uint64_t& revision) const override;
    [[nodiscard]] std::uint64_t MaterialShaderRevision(
        std::uint64_t graphSourceHash,
        std::uint64_t variantKey,
        std::string_view pass,
        bgfx::RendererType::Enum renderer) const noexcept override;

private:
    struct IndexedShader {
        kb::assets::AssetId asset{};
        kb::assets::bake::AssetBakeDigest digest{};
    };

    explicit RuntimeAssetShaderProvider(
        std::shared_ptr<kb::assets::bake::RuntimeAssetPack> pack,
        std::string platform);

    std::shared_ptr<kb::assets::bake::RuntimeAssetPack> pack_;
    std::string platform_;
    std::unordered_map<std::string, IndexedShader> materialShaders_;
};

} // namespace kb::render
