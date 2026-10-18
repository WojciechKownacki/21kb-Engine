#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <string>

namespace kb::scene {

class Scene;

struct SkeletalMeshSetupResult {
    bool succeeded = false;
    std::string error;
};

// What placing a Skeletal Mesh in a scene does, for the editor and kb_cli alike: the entity
// draws the mesh deformed by its skeleton - a Mesh Renderer, a Deformed Geometry and a Skeleton
// Binding, the skeleton and its signature read from the mesh asset, nothing for the caller to
// look up. An Animator added afterwards plays clips of that skeleton (or of another rig with the
// same bone names) on it.
class SceneSkeletalMeshSetup final {
public:
    SceneSkeletalMeshSetup() = delete;

    [[nodiscard]] static SkeletalMeshSetupResult Attach(Scene& scene, SceneEntity entity, kb::assets::AssetId skeletalMeshAssetId);
};

} // namespace kb::scene
