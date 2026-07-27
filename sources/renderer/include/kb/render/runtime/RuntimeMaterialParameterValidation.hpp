#pragma once

namespace kb::scene {
class Scene;
}

namespace kb::render {

// Installs the production material-graph schema validator used by
// SceneMaterialInstances::SetParameter*. Call this after registering render asset loaders
// and before scripts are allowed to update the scene. Idempotent for an already-configured
// scene.
void InstallRuntimeMaterialParameterValidation(kb::scene::Scene& scene);

} // namespace kb::render
