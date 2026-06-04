#include "renderer/RendererViewportPassResolver.hpp"

namespace kb::render {

MeshPassType RendererViewportPassResolver::MeshPassFor(const RenderViewportPlan& viewportPlan, RenderPassKind kind, MeshPassType fallback) noexcept {
    for (const RenderPassDesc& pass : viewportPlan.passes) {
        if (pass.kind == kind) {
            return pass.meshPass.value_or(fallback);
        }
    }
    return fallback;
}

} // namespace kb::render
