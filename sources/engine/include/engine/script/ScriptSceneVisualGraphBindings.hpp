#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/visual/VisualGraphNativeBindingRegistry.hpp"
#include "engine/visual/VisualGraphRuntimeBindingRegistry.hpp"

namespace kb::script {

class ScriptSceneVisualGraphBindings final {
public:
    ScriptSceneVisualGraphBindings() = delete;

    [[nodiscard]] static bool Register(kb::visual::VisualGraphRuntimeBindingRegistry& registry, kb::scene::Scene& scene);
    [[nodiscard]] static bool RegisterNative(kb::visual::VisualGraphNativeBindingRegistry& registry);
};

} // namespace kb::script
