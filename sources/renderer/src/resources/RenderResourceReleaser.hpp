#pragma once

#include "kb/render/resources/RenderResources.hpp"

namespace kb::render {

class RenderResourceReleaser final {
public:
    static void ReleaseMesh(RenderMeshResource& resource) noexcept;
    static void ReleaseTexture(RenderTextureResource& resource) noexcept;
};

} // namespace kb::render
