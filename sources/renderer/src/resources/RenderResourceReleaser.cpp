#include "resources/RenderResourceReleaser.hpp"

namespace kb::render {

void RenderResourceReleaser::ReleaseMesh(RenderMeshResource& resource) noexcept {
    if (bgfx::isValid(resource.indexBuffer)) {
        bgfx::destroy(resource.indexBuffer);
    }
    if (bgfx::isValid(resource.vertexBuffer)) {
        bgfx::destroy(resource.vertexBuffer);
    }
    resource = RenderMeshResource{};
}

void RenderResourceReleaser::ReleaseTexture(RenderTextureResource& resource) noexcept {
    if (bgfx::isValid(resource.texture)) {
        bgfx::destroy(resource.texture);
    }
    resource = RenderTextureResource{};
}

} // namespace kb::render
