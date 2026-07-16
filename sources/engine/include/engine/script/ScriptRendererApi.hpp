#pragma once

namespace kb::script {

class ScriptRuntimeHost;

// LIB-144: registers Renderer.IsVisible/GetBounds/TestFrustum/HasFrame as script functions -
// the read side of the renderer-published, scene-held visibility feedback frame
// (kb::scene::SceneRenderFeedback). All four are pure CPU reads of state the renderer
// already computed while culling the scene's most recent submit: no GPU occlusion query,
// readback, fence, or any other GPU synchronization is ever involved, and the results carry
// the inherent one-frame latency Unity's Renderer.isVisible has for the same reason (see
// SceneRenderFeedback.hpp's own doc comment for the full publish/latency contract).
//
// IsVisible/GetBounds take an entity (defaulting to the calling entity, like
// Particles.Create's owner) and error honestly for a dead one; an alive entity that simply
// has no entry in the published frame (no MeshRenderer, or created after the last submit)
// reports visible=false / found=false rather than erroring - "not rendered" is an answer,
// not a failure. TestFrustum is fail-closed: before any frame is published (or when the
// last submit resolved no camera) it reports inside=false, never a pretended visibility;
// HasFrame exists so a script can distinguish that "no data yet" state explicitly.
struct ScriptRendererApi {
    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
