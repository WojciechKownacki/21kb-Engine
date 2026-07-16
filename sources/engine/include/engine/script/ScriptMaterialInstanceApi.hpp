#pragma once

namespace kb::script {

class ScriptRuntimeHost;

// Registers MaterialInstance.Create/Release/Exists/Parent as script functions -
// LIB-139's "runtime instance with explicit lifetime and a variant limit".
// Create resolves its "material" argument (numeric asset id or virtual path,
// exactly like Audio.Play's "clip" argument) against the scene's
// AssetRegistry and validates its type before creating the instance record
// (kb::scene::SceneMaterialInstances), same as ScriptMeshRendererApi's
// asset-bearing functions - there is no way to reach Create with a
// fabricated or unresolved parent material. The instance itself carries no
// GPU-adjacent state at all in this ticket's scope (LIB-140 adds
// per-parameter overrides on top) - it is purely a scene-owned indirection
// record, explicitly released by the script that created it (never
// garbage-collected, never tied to an entity's own lifetime).
struct ScriptMaterialInstanceApi {
    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
