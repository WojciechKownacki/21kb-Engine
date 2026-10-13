#include "kb/render/resources/RenderResources.hpp"

namespace kb::render {

bgfx::VertexLayout RenderStaticMeshVertexLayout() {
    return RenderStaticMeshVertexLayout(RenderVertexFormat::P3C3);
}

bgfx::VertexLayout RenderStaticMeshVertexLayout(RenderVertexFormat format) {
    bgfx::VertexLayout layout;
    layout.begin();
    switch (format) {
    case RenderVertexFormat::P3C3:
        layout
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 3, bgfx::AttribType::Float);
        break;
    case RenderVertexFormat::P3N3UV2:
        layout
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float);
        break;
    case RenderVertexFormat::P3N3T4UV2:
        layout
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
            // UV2..UV7: the instance data holds TexCoord3..7, so the extra sets ride in Color1..3 (two per attribute).
            .add(bgfx::Attrib::Color1, 4, bgfx::AttribType::Half)
            .add(bgfx::Attrib::Color2, 4, bgfx::AttribType::Half)
            .add(bgfx::Attrib::Color3, 4, bgfx::AttribType::Half);
        break;
    case RenderVertexFormat::SkinnedP3N3T4UV2J4W4:
        layout
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Uint16)
            .add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Float);
        break;
    }
    layout.end();
    return layout;
}

std::uint32_t RenderStaticMeshVertexStride(RenderVertexFormat format) noexcept {
    switch (format) {
    case RenderVertexFormat::P3C3:
        return sizeof(RenderStaticMeshVertex);
    case RenderVertexFormat::P3N3UV2:
        return sizeof(RenderStaticMeshVertexP3N3UV2);
    case RenderVertexFormat::P3N3T4UV2:
        return sizeof(RenderStaticMeshVertexP3N3T4UV2);
    case RenderVertexFormat::SkinnedP3N3T4UV2J4W4:
        return sizeof(RenderStaticMeshVertexSkinned);
    }
    return 0U;
}

} // namespace kb::render
