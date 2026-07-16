#pragma once

namespace kb::script {

class ScriptRuntimeHost;

// LIB-142: registers PostProcess.SetProfile/ClearProfile/ActiveProfile as script functions -
// the scene-global, asset-based, serializable post-process parameter set the ticket asks for
// "wyłącznie" (exclusively). SetProfile resolves its "profile" argument (numeric asset id or
// virtual path, exactly like Audio.Play's "clip" argument) against the scene's AssetRegistry
// and validates its type ("PostProcessProfile") before assigning it - mirroring
// MeshRenderer.SetMaterial's exact "assign a whole asset reference, never expose individual
// fields to script" convention, DELIBERATELY not the generic per-field reflection mutation
// Light/Camera/MeshRenderer's own component fields use (see
// ScenePostProcessAccess.hpp/PostProcessProfileAssetLoader.hpp's own doc comments for why -
// the actual tunable values, kb::render::ScenePostProcessSettings, live entirely in
// kb::render, a library kb::scene/kb::script never depend on; the resolved value is computed
// once, lazily, at render-submission time).
struct ScriptPostProcessApi {
    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
