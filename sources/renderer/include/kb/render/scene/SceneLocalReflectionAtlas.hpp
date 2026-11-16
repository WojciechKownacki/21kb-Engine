#pragma once

#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

namespace kb::render {

// Where each local reflection probe sits in the shared atlas (local_reflection.sh reads the same numbers from
// uniforms). A probe owns one slot: its radiance as octahedral maps of kLevelCount roughness levels, each
// level half the size of the one before and framed by a one texel border that continues the map across its edge.
struct LocalReflectionAtlasLayout {
    static constexpr std::uint32_t kSlotCount = kMaxSceneReflectionProbes;
    static constexpr std::uint32_t kLevelCount = 5U;
    static constexpr std::uint32_t kBaseSize = 128U;
    static constexpr std::uint32_t kGutter = 1U;
    static constexpr std::uint32_t kColumns = 4U;

    [[nodiscard]] static constexpr std::uint32_t LevelSize(std::uint32_t level) noexcept { return kBaseSize >> level; }
    [[nodiscard]] static constexpr std::uint32_t TileSize(std::uint32_t level) noexcept { return LevelSize(level) + 2U * kGutter; }
    // Level 0 fills the left of a slot; the smaller levels are stacked on its right.
    [[nodiscard]] static constexpr std::uint32_t LevelX(std::uint32_t level) noexcept { return level == 0U ? 0U : TileSize(0U); }
    [[nodiscard]] static constexpr std::uint32_t LevelY(std::uint32_t level) noexcept {
        std::uint32_t y = 0U;
        for (std::uint32_t lower = 1U; lower < level; ++lower) y += TileSize(lower);
        return y;
    }
    [[nodiscard]] static constexpr std::uint32_t SlotWidth() noexcept { return TileSize(0U) + TileSize(1U); }
    [[nodiscard]] static constexpr std::uint32_t SlotHeight() noexcept { return TileSize(0U); }
    [[nodiscard]] static constexpr std::uint32_t SlotX(std::uint32_t slot) noexcept { return (slot % kColumns) * SlotWidth(); }
    [[nodiscard]] static constexpr std::uint32_t SlotY(std::uint32_t slot) noexcept { return (slot / kColumns) * SlotHeight(); }
    [[nodiscard]] static constexpr std::uint32_t Width() noexcept { return kColumns * SlotWidth(); }
    [[nodiscard]] static constexpr std::uint32_t Height() noexcept { return ((kSlotCount + kColumns - 1U) / kColumns) * SlotHeight(); }
};
// The stacked levels must fit beside the first one.
static_assert(LocalReflectionAtlasLayout::LevelY(LocalReflectionAtlasLayout::kLevelCount) <= LocalReflectionAtlasLayout::SlotHeight(),
    "Reflection atlas levels do not fit in a slot");

// One HDR (RGBA16F) texture holding every local reflection probe, so the lighting reads any of up to
// kSlotCount probes through a single sampler. A probe's cube map is copied into its slot once, when the
// probe is new, and is sampled from the next frame on (the copy's view runs after the lit views of its frame,
// like the sky capture). Applying the atlas to a lighting config tells each probe its slot and whether that
// copy is ready; probes that are not ready yet simply do not contribute.
class SceneLocalReflectionAtlas {
public:
    SceneLocalReflectionAtlas();
    ~SceneLocalReflectionAtlas();

    SceneLocalReflectionAtlas(const SceneLocalReflectionAtlas&) = delete;
    SceneLocalReflectionAtlas& operator=(const SceneLocalReflectionAtlas&) = delete;

    // False when the backend cannot render to a filterable half-float texture or lacks the copy shader.
    // Update calls it when the first probe appears.
    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

    // Queues the copy of every probe cube map that is not in the atlas yet, on ViewId::LocalReflectionAtlas.
    void Update(const SceneRenderIblConfig& ibl);
    // Sets the atlas texture and every probe's slot and readiness on the config.
    void Apply(SceneRenderIblConfig& ibl) const noexcept;
    // Copies queued this frame count as in the atlas after the frame that rendered them.
    void EndFrame() noexcept;

    [[nodiscard]] bgfx::TextureHandle Texture() const noexcept { return atlas_; }

private:
    struct Key {
        std::uint16_t texture = bgfx::kInvalidHandle;
        std::uint8_t maxMip = 0U;
        [[nodiscard]] bool operator==(const Key&) const noexcept = default;
        [[nodiscard]] bool Valid() const noexcept { return texture != bgfx::kInvalidHandle; }
    };

    [[nodiscard]] static Key KeyOf(const SceneRenderReflectionProbe& probe) noexcept;
    [[nodiscard]] int FindSlot(const Key& key) const noexcept;
    [[nodiscard]] bool SubmitSlot(std::uint32_t slot, bgfx::TextureHandle source, std::uint8_t maxMip) const;

    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sourceSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle buildUniform_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle atlas_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frameBuffer_ = BGFX_INVALID_HANDLE;
    std::array<Key, LocalReflectionAtlasLayout::kSlotCount> keys_{};
    std::array<bool, LocalReflectionAtlasLayout::kSlotCount> shown_{};
    std::array<bool, LocalReflectionAtlasLayout::kSlotCount> pending_{};
};

} // namespace kb::render
