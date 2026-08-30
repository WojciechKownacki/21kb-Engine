#pragma once

#include "kb/render/resources/RenderResources.hpp"

namespace kb::render {

class RenderMeshDescGeometry final {
public:
    [[nodiscard]] static const void* VertexData(const RenderMeshDesc& desc) noexcept;
    [[nodiscard]] static std::uint32_t IndexAt(const RenderMeshDesc& desc, std::uint32_t index) noexcept;
    [[nodiscard]] static std::uint32_t SectionVertexCount(
        const RenderMeshDesc& desc,
        const RenderMeshSectionDesc& section) noexcept;
    [[nodiscard]] static RenderBoundsSphere ComputeBounds(
        const RenderMeshDesc& desc,
        std::uint32_t indexStart,
        std::uint32_t indexCount,
        std::uint32_t vertexStart = 0U) noexcept;
};

} // namespace kb::render
