#pragma once

#include "engine/assets/IAssetLoader.hpp"
#include "kb/render/resources/RenderResources.hpp"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <typeindex>
#include <vector>

namespace kb::render {

enum class RenderTextureAssetColorSpace : std::uint8_t {
    Unknown,
    Linear,
    Srgb,
};

enum class RenderTextureAssetSemantic : std::uint8_t {
    Unknown,
    BaseColor,
    Normal,
    MetallicRoughness,
    Occlusion,
    Emissive,
};

struct RenderTextureAssetData {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t depth = 1;
    std::uint16_t layers = 1;
    std::uint8_t mipCount = 1;
    RenderTextureDimension dimension = RenderTextureDimension::Texture2D;
    // Authoring/import metadata. Unknown is intentional for legacy assets and decoded image
    // formats whose import settings did not declare a policy; callers must not infer either
    // value from the asset name or path.
    RenderTextureAssetColorSpace colorSpace = RenderTextureAssetColorSpace::Unknown;
    RenderTextureAssetSemantic semantic = RenderTextureAssetSemantic::Unknown;
    std::vector<std::uint8_t> rgba8;

    [[nodiscard]] RenderTextureDesc MakeDesc(const bgfx::Memory* memory, RenderTextureColorSpace runtimeColorSpace = RenderTextureColorSpace::Linear) const noexcept;
};

class RenderTextureAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;

    // LoadTexture(path) decodes through a process-wide cache keyed by (path, last-write-time, size): a file is
    // decoded to RGBA8 at most once while unchanged on disk, no matter how many scenes reference it. The stream
    // overload is uncached (no path to key on). A changed file is a cache miss, so hot-reload still works.
    [[nodiscard]] static std::optional<RenderTextureAssetData> LoadTexture(const std::filesystem::path& path);
    [[nodiscard]] static std::optional<RenderTextureAssetData> LoadTexture(std::istream& input);

    // Count of real decodes performed (cache misses) for cacheable paths. Test/telemetry hook: lets a caller
    // prove that a repeated load of an unchanged file did not re-decode.
    [[nodiscard]] static std::uint64_t DebugDecodeCount() noexcept;

    // Non-blocking texture streaming. TryAcquireDecodedTexture returns the decoded pixels only if they are
    // already cached (never decodes on the calling thread); on a miss the caller queues RequestAsyncTextureDecode
    // and retries a later frame, so a large first decode streams in on a background worker instead of freezing
    // the render thread. Both are safe to call from the render thread.
    [[nodiscard]] static std::shared_ptr<const RenderTextureAssetData> TryAcquireDecodedTexture(const std::filesystem::path& path);
    static void RequestAsyncTextureDecode(const std::filesystem::path& path);

    // Off by default. The runtime texture ensurer streams textures (non-blocking) only when enabled, which the
    // editor app turns on at startup. Left off, textures decode synchronously in the submit (deterministic for
    // tests and one-shot render/thumbnail captures).
    static void SetAsyncTextureDecodeEnabled(bool enabled) noexcept;
    [[nodiscard]] static bool IsAsyncTextureDecodeEnabled() noexcept;
};

} // namespace kb::render
