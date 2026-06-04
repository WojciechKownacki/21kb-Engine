#pragma once

#include "kb/render/scene/MeshPipeline.hpp"

#include <cstddef>
#include <vector>

namespace kb::render {

class MeshPipelineCommandBuilder {
public:
    MeshPipelineCommandBuilder() = delete;

    [[nodiscard]] static MeshDrawCommand& WritableCommand(MeshPipelineBuildResult& result, std::size_t index);
    static void FinalizeCommands(MeshPipelineBuildResult& result, MeshPassType pass, std::size_t commandCount) noexcept;
    static void CountCommandsAsSubmitted(SceneRenderSubmitStats& stats, const std::vector<MeshDrawCommand>& commands) noexcept;
};

} // namespace kb::render
