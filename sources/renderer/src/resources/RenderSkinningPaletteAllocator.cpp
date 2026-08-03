#include "kb/render/resources/RenderSkinningPaletteAllocator.hpp"

#include <cmath>
#include <cstring>

namespace kb::render {
namespace {

[[nodiscard]] bool IsFinite(std::span<const RenderSkinningMatrix> matrices) noexcept {
    for (const RenderSkinningMatrix& matrix : matrices) {
        for (const float value : matrix) {
            if (!std::isfinite(value)) return false;
        }
    }
    return true;
}

} // namespace

RenderSkinningPaletteAllocator::RenderSkinningPaletteAllocator(
    RenderSkinningPaletteAllocatorDesc desc) : desc_(desc) {
    for (std::vector<RenderSkinningMatrix>& staging : staging_) {
        staging.resize(desc_.matrixCapacityPerFrame);
    }
}

RenderSkinningPaletteAllocator::~RenderSkinningPaletteAllocator() {
    Shutdown();
}

bool RenderSkinningPaletteAllocator::BeginFrame(
    std::uint64_t frame, std::uint64_t completedFrame) noexcept {
    if (frame == 0U || frame <= activeFrame_ || completedFrame < completedFrame_) {
        return false;
    }
    completedFrame_ = completedFrame;
    const std::uint8_t bufferIndex = static_cast<std::uint8_t>(frame & 1U);
    if (fenceFrames_[bufferIndex] > completedFrame_) {
        ++allocationFailures_;
        return false;
    }
    activeFrame_ = frame;
    allocatedMatrices_ = 0U;
    allocatedMatricesByBuffer_[bufferIndex] = 0U;
    uploadedBytes_ = 0U;
    fenceFrames_[bufferIndex] = frame;
    return true;
}

RenderSkinningPaletteHandle RenderSkinningPaletteAllocator::Allocate(
    std::uint32_t matrixCount) noexcept {
    if (activeFrame_ == 0U || matrixCount == 0U ||
        matrixCount > desc_.matrixCapacityPerFrame - allocatedMatrices_) {
        ++allocationFailures_;
        return {};
    }
    const RenderSkinningPaletteHandle handle{
        .frame = activeFrame_,
        .firstMatrix = allocatedMatrices_,
        .matrixCount = matrixCount,
        .bufferIndex = static_cast<std::uint8_t>(activeFrame_ & 1U),
    };
    allocatedMatrices_ += matrixCount;
    allocatedMatricesByBuffer_[handle.bufferIndex] = allocatedMatrices_;
    return handle;
}

bool RenderSkinningPaletteAllocator::Upload(
    RenderSkinningPaletteHandle handle,
    std::span<const RenderSkinningMatrix> matrices) noexcept {
    if (!Owns(handle) || matrices.size() != handle.matrixCount || !IsFinite(matrices) ||
        !EnsureBuffers()) {
        return false;
    }
    std::vector<RenderSkinningMatrix>& staging = staging_[handle.bufferIndex];
    std::memcpy(staging.data() + handle.firstMatrix, matrices.data(),
        matrices.size_bytes());
    const std::uint32_t byteCount = static_cast<std::uint32_t>(matrices.size_bytes());
    const bgfx::Memory* memory = bgfx::copy(matrices.data(), byteCount);
    if (memory == nullptr) return false;
    bgfx::updateTexture2D(textures_[handle.bufferIndex], 0U, 0U, 0U,
        static_cast<std::uint16_t>(handle.firstMatrix), 4U,
        static_cast<std::uint16_t>(handle.matrixCount), memory);
    uploadedBytes_ += byteCount;
    return true;
}

bgfx::TextureHandle RenderSkinningPaletteAllocator::Texture(
    RenderSkinningPaletteHandle handle) const noexcept {
    return IsResident(handle) ? textures_[handle.bufferIndex] :
        bgfx::TextureHandle{ UINT16_MAX };
}

RenderSkinningPaletteAllocatorStats RenderSkinningPaletteAllocator::Stats() const noexcept {
    return {
        .activeFrame = activeFrame_, .completedFrame = completedFrame_,
        .matrixCapacityPerFrame = desc_.matrixCapacityPerFrame,
        .allocatedMatrices = allocatedMatrices_, .allocationFailures = allocationFailures_,
        .uploadedBytes = uploadedBytes_,
    };
}

void RenderSkinningPaletteAllocator::Shutdown() noexcept {
    for (bgfx::TextureHandle& texture : textures_) {
        if (bgfx::isValid(texture)) bgfx::destroy(texture);
        texture = BGFX_INVALID_HANDLE;
    }
    for (std::vector<RenderSkinningMatrix>& staging : staging_) staging.clear();
    fenceFrames_ = {};
    allocatedMatricesByBuffer_ = {};
    activeFrame_ = 0U;
    completedFrame_ = 0U;
    allocatedMatrices_ = 0U;
    uploadedBytes_ = 0U;
}

bool RenderSkinningPaletteAllocator::EnsureBuffers() noexcept {
    if (desc_.matrixCapacityPerFrame == 0U) return false;
    if (desc_.matrixCapacityPerFrame > UINT16_MAX) return false;
    for (std::vector<RenderSkinningMatrix>& staging : staging_) {
        if (staging.size() != desc_.matrixCapacityPerFrame) {
            staging.resize(desc_.matrixCapacityPerFrame);
        }
    }
    for (bgfx::TextureHandle& texture : textures_) {
        if (bgfx::isValid(texture)) continue;
        texture = bgfx::createTexture2D(4U,
            static_cast<std::uint16_t>(desc_.matrixCapacityPerFrame), false,
            1U, bgfx::TextureFormat::RGBA32F,
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        if (!bgfx::isValid(texture)) {
            Shutdown();
            return false;
        }
    }
    return true;
}

bool RenderSkinningPaletteAllocator::Owns(
    RenderSkinningPaletteHandle handle) const noexcept {
    return handle.IsValid() && handle.frame == activeFrame_ &&
        handle.bufferIndex == static_cast<std::uint8_t>(activeFrame_ & 1U) &&
        handle.firstMatrix <= allocatedMatrices_ &&
        handle.matrixCount <= allocatedMatrices_ - handle.firstMatrix;
}

bool RenderSkinningPaletteAllocator::IsResident(
    RenderSkinningPaletteHandle handle) const noexcept {
    if (!handle.IsValid() || handle.bufferIndex >= textures_.size() ||
        fenceFrames_[handle.bufferIndex] != handle.frame) {
        return false;
    }
    const std::uint32_t allocated = allocatedMatricesByBuffer_[handle.bufferIndex];
    return handle.firstMatrix <= allocated &&
        handle.matrixCount <= allocated - handle.firstMatrix;
}

} // namespace kb::render
