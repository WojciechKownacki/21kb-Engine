#pragma once

namespace kb::script {

class ScriptRuntimeHost;

// Registers MeshRenderer.SetMesh/SetMaterial as script functions. Both resolve their
// asset argument (numeric asset id or virtual path, exactly like Audio.Play's "clip"
// argument) against the scene's AssetRegistry and validate its type before assigning it -
// there is no way to reach these functions with a fabricated or unresolved asset reference,
// and neither function (nor the MeshRendererComponent it writes) ever touches a GPU handle;
// resource resolution happens entirely inside kb::render, a library kb::scene/kb::script
// never depends on. LIB-137.
struct ScriptMeshRendererApi {
    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
