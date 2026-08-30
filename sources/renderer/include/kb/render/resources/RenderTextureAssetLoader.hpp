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

// A texture that reached the runtime ALREADY in a GPU block format, which is what a bake
// produces. `blocks` is the complete mip chain in `format`, level 0 first and levels packed
// back to back, i.e. exactly the layout bgfx::createTexture2D expects for its memory.
//
// This is the one payload the CPU never touches: decoding it back to RGBA8 would give back
// every byte the bake saved and add a decoder on the load path, which is the whole reason a
// texture is baked at all.
struct RenderTextureGpuBlocks {
    bgfx::TextureFormat::Enum format = bgfx::TextureFormat::Count;
    std::vector<std::uint8_t> blocks;
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
    // Set only for a baked texture. `gpuBlocks` and `rgba8` are alternatives, never both: when
    // this holds a value `rgba8` is empty and `mipCount` counts the levels inside it, and when
    // it is empty the asset is exactly what it has always been - `rgba8` holding LOD0.
    std::optional<RenderTextureGpuBlocks> gpuBlocks;

    [[nodiscard]] RenderTextureDesc MakeDesc(const bgfx::Memory* memory, RenderTextureColorSpace runtimeColorSpace = RenderTextureColorSpace::Linear) const noexcept;
};

// How a loaded texture is allowed to reach the GPU.
enum class RenderTextureUploadPath : std::uint8_t {
    // Hand the bytes to bgfx unchanged. The only path that keeps a bake's savings.
    GpuBlocks,
    // Decode LOD0 back to RGBA8 first and take the path every unbaked texture takes.
    DecodedRgba8,
};

// The device, not the baker, has the last word. `deviceSupportsBakedFormat` is
// RenderDeviceSupportsTextureFormat's answer for this asset's baked format; a format the
// device cannot sample is a hard bgfx failure at createTexture, not a quality drop, so an
// unsupported bake falls back rather than being handed over and hoped for. An asset with no
// baked payload always answers DecodedRgba8 - there is nothing else it could mean.
[[nodiscard]] RenderTextureUploadPath SelectRenderTextureUploadPath(
    const RenderTextureAssetData& asset,
    bool deviceSupportsBakedFormat) noexcept;

// The rule behind RenderDeviceSupportsTextureFormat, applied to one entry of bgfx's
// per-format capability table instead of to the running device. Separated so the rule can be
// stated as a truth table: asked through the device it can only ever be compared against
// whatever device happens to be up, and the two answers a device-independent test can give
// ("nothing is claimed for a format the device says nothing about", "sRGB is never a wider
// claim than linear") are both satisfied by a rule that ignores the sRGB bit entirely.
//
// BGFX_CAPS_FORMAT_TEXTURE_2D means the device samples the format natively and is required in
// both colour spaces. BGFX_CAPS_FORMAT_TEXTURE_2D_SRGB is required ON TOP of it for an sRGB
// binding, because a device may take a block format and still refuse to decode it as sRGB, and
// sampling that texture as linear would wash out every surface it is on.
// BGFX_CAPS_FORMAT_TEXTURE_2D_EMULATED is deliberately not accepted: bgfx satisfies it by
// converting the surface on the CPU, which is the cost a baked block format exists to avoid.
[[nodiscard]] bool RenderTextureFormatCapabilitySatisfied(
    std::uint32_t formatCapabilities,
    RenderTextureColorSpace colorSpace) noexcept;

// Whether the running device can sample `format` as a 2D texture, read from
// bgfx::getCaps()->formats[] and answered by RenderTextureFormatCapabilitySatisfied. Answers
// false when bgfx is not initialised, so a caller with no device decodes rather than guesses.
[[nodiscard]] bool RenderDeviceSupportsTextureFormat(
    bgfx::TextureFormat::Enum format,
    RenderTextureColorSpace colorSpace) noexcept;

// LOD0 of a baked texture, decoded back to RGBA8 - the fallback for a device that cannot
// sample the baked format. Returns the asset unchanged when it carries no baked payload, and
// nullopt when the payload cannot be decoded, which is a bad bake rather than a texture to
// salvage. The result has no mip chain, so it re-enters the same runtime mip generation an
// unbaked texture goes through.
[[nodiscard]] std::optional<RenderTextureAssetData> DecodeRenderTextureToRgba8(const RenderTextureAssetData& asset);

class RenderTextureAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] std::vector<std::string> BakedAssetTypes() const override;
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
