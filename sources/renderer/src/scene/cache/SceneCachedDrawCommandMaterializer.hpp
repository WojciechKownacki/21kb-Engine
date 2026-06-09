#pragma once

#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/cache/SceneCachedDrawCommand.hpp"

namespace kb::render {

class SceneCachedDrawCommandMaterializer {
public:
    SceneCachedDrawCommandMaterializer() = delete;

    static void ApplyTemplate(const SceneCachedDrawCommand& cachedCommand, MeshDrawCommand& outCommand) noexcept;
};

} // namespace kb::render
