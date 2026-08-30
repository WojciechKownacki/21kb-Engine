#pragma once

#include "kb/render/resources/RenderResources.hpp"

namespace kb::render {

class RenderMeshResourceBuilder final {
public:
    [[nodiscard]] static bool IsValidDesc(const RenderMeshDesc& desc) noexcept;
    [[nodiscard]] static const void* VertexData(const RenderMeshDesc& desc) noexcept;
    [[nodiscard]] static RenderBoundsSphere ComputeBounds(
        const RenderMeshDesc& desc,
        std::uint32_t indexStart,
        std::uint32_t indexCount,
        std::uint32_t vertexStart = 0U) noexcept;
    [[nodiscard]] static RenderMeshResource Build(
        const RenderMeshDesc& desc,
        bgfx::VertexBufferHandle vertexBuffer,
        bgfx::DynamicVertexBufferHandle dynamicVertexBuffer,
        bgfx::IndexBufferHandle indexBuffer);
};

} // namespace kb::render
