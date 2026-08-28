#pragma once

namespace kb::script {

class ScriptRuntimeHost;

// Registers MeshRenderer.SetMesh/SetMaterial/SetMaterialSlot/GetMaterialSlot/
// ClearMaterialSlot as script functions. Every asset-bearing argument (numeric asset id or
// virtual path, exactly like Audio.Play's "clip" argument) is resolved against the scene's
// AssetRegistry and type-validated before assigning it - there is no way to reach these
// functions with a fabricated or unresolved asset reference, and none of them (nor the
// MeshRendererComponent they write) ever touch a GPU handle; resource resolution happens
// entirely inside kb::render, a library kb::scene/kb::script never depends on. LIB-137
// (SetMesh/SetMaterial); LIB-138 (SetMaterialSlot/GetMaterialSlot/ClearMaterialSlot - slot
// index N matches whichever mesh sections declare materialSlot==N; kb::scene has no mesh
// section/slot-count query of its own, so a slot index unused by the current mesh is simply
// a harmless no-op, exactly like indexing past the end of a material slot array).
struct ScriptMeshRendererApi {
    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
