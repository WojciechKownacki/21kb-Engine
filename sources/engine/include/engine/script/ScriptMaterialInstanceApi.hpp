#pragma once

namespace kb::script {

class ScriptRuntimeHost;

// Registers MaterialInstance.Create/Release/Exists/Parent/SetParameterScalar/
// SetParameterBool/ClearParameter as script functions - LIB-139's "runtime
// instance with explicit lifetime and a variant limit", extended by LIB-140
// with per-parameter Scalar/Bool overrides.
// Create resolves its "material" argument (numeric asset id or virtual path,
// exactly like Audio.Play's "clip" argument) against the scene's
// AssetRegistry and validates its type before creating the instance record
// (kb::scene::SceneMaterialInstances), same as ScriptMeshRendererApi's
// asset-bearing functions - there is no way to reach Create with a
// fabricated or unresolved parent material. The instance itself carries no
// GPU-adjacent state - it is purely a scene-owned indirection record plus a
// small list of named overrides, explicitly released by the script that
// created it (never garbage-collected, never tied to an entity's own
// lifetime). SetParameterScalar/SetParameterBool/ClearParameter cannot
// validate `name` against the parent material's real graph schema (kb::scene
// never depends on kb::render) - an unresolvable/wrong-type override is
// silently dropped at render-resolution time instead (see
// RuntimeMaterialResourceEnsurer's material-instance path).
struct ScriptMaterialInstanceApi {
    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
