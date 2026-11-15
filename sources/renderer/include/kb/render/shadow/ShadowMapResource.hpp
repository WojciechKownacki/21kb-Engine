#pragma once

#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

namespace kb::render {

class ShadowMapResource {
public:
    ~ShadowMapResource();

    ShadowMapResource() = default;
    ShadowMapResource(const ShadowMapResource&) = delete;
    ShadowMapResource& operator=(const ShadowMapResource&) = delete;

    // `compactDepth` stores 16-bit depth where the device allows it (half the memory and bandwidth).
    [[nodiscard]] bool Ensure(std::uint32_t size, bool compactDepth = false);
    // Rectangular map, e.g. the point-light face atlas.
    [[nodiscard]] bool Ensure(std::uint32_t width, std::uint32_t height, bool compactDepth = false);
    void Shutdown() noexcept;

    [[nodiscard]] bgfx::TextureHandle DepthTexture() const noexcept;
    [[nodiscard]] bgfx::FrameBufferHandle FrameBuffer() const noexcept;
    [[nodiscard]] std::uint32_t Size() const noexcept;
    [[nodiscard]] std::uint32_t Height() const noexcept;
    [[nodiscard]] bool IsAllocated() const noexcept;
    [[nodiscard]] std::uint64_t AllocationBytes() const noexcept;

    [[nodiscard]] static std::uint64_t AllocationBytesFor(std::uint32_t shadowMapSize, std::uint32_t bytesPerTexel = 4U) noexcept;
    [[nodiscard]] static std::uint64_t AllocationBytesFor(std::uint32_t width, std::uint32_t height, std::uint32_t bytesPerTexel) noexcept;

    // The rows of a point-light atlas: the entity of the light each row belongs to (0 = free), kept while the
    // light stays selected so its tiles survive a camera that reorders the lights.
    static constexpr std::uint32_t kPointRows = 32U;
    [[nodiscard]] std::array<std::uint64_t, kPointRows>& RowOwners() noexcept { return rowOwners_; }
    // What the tiles of row `row` were last rendered for: unchanged, they are kept instead of rendered again.
    // 0 = nothing cached.
    [[nodiscard]] std::uint64_t CachedTiles(std::uint32_t row) const noexcept {
        return row < cachedTiles_.size() ? cachedTiles_[row] : 0U;
    }
    void SetCachedTiles(std::uint32_t row, std::uint64_t key) noexcept {
        if (row < cachedTiles_.size()) cachedTiles_[row] = key;
    }
    // The tiles of a row no longer match the scene: the next frame renders them again.
    void DiscardCachedTiles(std::uint32_t row) noexcept { SetCachedTiles(row, 0U); }
    void DiscardAllCachedTiles() noexcept { cachedTiles_.fill(0U); }
    // The cascades of a directional light's map: what each was last rendered for, kept instead of rendered again
    // while it still matches. `key` 0 = nothing cached; the window (`view`, `halfExtent`) is where the cascade
    // sees casters, so a caster change reaches it without the camera that drew it.
    struct CachedCascade {
        std::uint64_t key = 0U;
        std::uint32_t casterCount = 0U;
        float halfExtent = 0.0F;
        std::array<float, 16> view{};
    };
    static constexpr std::uint32_t kCascades = SceneRenderShadowMapBinding::kMaxCascades;
    [[nodiscard]] const CachedCascade& CachedCascadeAt(std::uint32_t cascade) const noexcept { return cascades_[cascade % kCascades]; }
    void SetCachedCascade(std::uint32_t cascade, const CachedCascade& entry) noexcept {
        if (cascade < kCascades) cascades_[cascade] = entry;
    }
    void DiscardCachedCascade(std::uint32_t cascade) noexcept {
        if (cascade < kCascades) cascades_[cascade].key = 0U;
    }
    void DiscardAllCachedCascades() noexcept { for (CachedCascade& entry : cascades_) entry.key = 0U; }
    // The scene (RenderScene::InstanceStamp) the cached shadow was made for (0 = none yet).
    [[nodiscard]] std::uint64_t CasterChangesScene() const noexcept { return casterChangesScene_; }
    void SetCasterChangesScene(std::uint64_t scene) noexcept { casterChangesScene_ = scene; }
    // The last motion frame whose shadow caster changes were applied to the cached tiles (0 = none yet).
    [[nodiscard]] std::uint64_t CasterChangesFrame() const noexcept { return casterChangesFrame_; }
    void SetCasterChangesFrame(std::uint64_t frame) noexcept { casterChangesFrame_ = frame; }
    // The scene's mesh content revision those changes were applied at: equal again, nothing changed since.
    [[nodiscard]] std::uint64_t CasterChangesRevision() const noexcept { return casterChangesRevision_; }
    void SetCasterChangesRevision(std::uint64_t revision) noexcept { casterChangesRevision_ = revision; }

private:
    std::array<std::uint64_t, kPointRows> rowOwners_{};
    std::array<std::uint64_t, kPointRows> cachedTiles_{};
    std::array<CachedCascade, kCascades> cascades_{};
    std::uint64_t casterChangesScene_ = 0U;
    std::uint64_t casterChangesFrame_ = 0U;
    std::uint64_t casterChangesRevision_ = 0U;
    bgfx::TextureHandle depthTexture_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frameBuffer_ = BGFX_INVALID_HANDLE;
    std::uint32_t size_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t bytesPerTexel_ = 4U;
    bool compactDepth_ = false;
};

} // namespace kb::render
