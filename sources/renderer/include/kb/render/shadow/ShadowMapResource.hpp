#pragma once

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

    [[nodiscard]] bool Ensure(std::uint32_t size);
    // Rectangular map, e.g. the point-light face atlas.
    [[nodiscard]] bool Ensure(std::uint32_t width, std::uint32_t height);
    void Shutdown() noexcept;

    [[nodiscard]] bgfx::TextureHandle DepthTexture() const noexcept;
    [[nodiscard]] bgfx::FrameBufferHandle FrameBuffer() const noexcept;
    [[nodiscard]] std::uint32_t Size() const noexcept;
    [[nodiscard]] std::uint32_t Height() const noexcept;
    [[nodiscard]] bool IsAllocated() const noexcept;
    [[nodiscard]] std::uint64_t AllocationBytes() const noexcept;

    [[nodiscard]] static std::uint64_t AllocationBytesFor(std::uint32_t shadowMapSize) noexcept;
    [[nodiscard]] static std::uint64_t AllocationBytesFor(std::uint32_t width, std::uint32_t height) noexcept;

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
    // The last motion frame whose shadow caster changes were applied to the cached tiles (0 = none yet).
    [[nodiscard]] std::uint64_t CasterChangesFrame() const noexcept { return casterChangesFrame_; }
    void SetCasterChangesFrame(std::uint64_t frame) noexcept { casterChangesFrame_ = frame; }
    // The scene's mesh content revision those changes were applied at: equal again, nothing changed since.
    [[nodiscard]] std::uint64_t CasterChangesRevision() const noexcept { return casterChangesRevision_; }
    void SetCasterChangesRevision(std::uint64_t revision) noexcept { casterChangesRevision_ = revision; }

private:
    std::array<std::uint64_t, kPointRows> rowOwners_{};
    std::array<std::uint64_t, kPointRows> cachedTiles_{};
    std::uint64_t casterChangesFrame_ = 0U;
    std::uint64_t casterChangesRevision_ = 0U;
    bgfx::TextureHandle depthTexture_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frameBuffer_ = BGFX_INVALID_HANDLE;
    std::uint32_t size_ = 0;
    std::uint32_t height_ = 0;
};

} // namespace kb::render
