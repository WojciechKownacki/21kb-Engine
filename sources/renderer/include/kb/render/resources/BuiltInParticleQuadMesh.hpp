#pragma once

#include "engine/assets/AssetId.hpp"

namespace kb::render {

struct RenderMeshAssetData;

// LIB-143: a procedural, file-less unit quad (1x1, centered at the origin, normal +Z, in the
// XY plane) that kb::render's SceneParticleRenderSynchronizer billboards and scales per
// particle - the SAME shape kb::math::LookRotation's own "local +Z points along forward"
// convention (EngineMath.hpp) expects, so a billboard rotation built from a particle's
// direction-to-camera orients this quad's front face at the camera without any extra
// per-frame basis math. `doubleSided=true` on the mesh (not a material flag) sidesteps any
// winding-order assumption entirely.
//
// Deliberately NEVER registered as a real kb::assets::AssetRegistry entry (no
// AssetManager::RegisterAsset/Upsert call anywhere for this id): AssetRegistry's metadata
// storage is a plain std::vector, so any later Upsert call can reallocate it and invalidate
// every raw AssetMetadata* a caller obtained earlier from Find/FindByPath - a real hazard for
// code registering a NEW entry from inside the render-submit hot path, where other code may
// already be holding such a pointer from earlier in the same call chain. RuntimeMeshResource
// Ensurer::Ensure (RuntimeMeshResourceEnsurer.cpp) special-cases BuiltInParticleQuadMeshAsset
// Id() and calls BuildBuiltInParticleQuadMesh() directly, bypassing AssetRegistry/AssetManager
// ::Load entirely for this one synthetic id - this header only owns the id and the procedural
// geometry builder, not a loader or a registration function.
[[nodiscard]] kb::assets::AssetId BuiltInParticleQuadMeshAssetId() noexcept;

[[nodiscard]] RenderMeshAssetData BuildBuiltInParticleQuadMesh();

} // namespace kb::render
