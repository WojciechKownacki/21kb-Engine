#pragma once
#include "scene/BlendSpace2DEditorDocumentState.hpp"
#include "scene/EditorBlendSpace2DPreviewSession.hpp"

#include <array>
#include <limits>

namespace kb::editor {
struct BlendSpace2DEditorState final {
    BlendSpace2DEditorDocumentState document;
    EditorBlendSpace2DPreviewSession preview;
    kb::scene::SceneEntity owner{};
    std::size_t selected = std::numeric_limits<std::size_t>::max(), firstRow = 0;
    float x = 0, y = 0, phase = 0;
    std::array<double,4> bounds{-1,1,-1,1};
    bool dragging = false, dragEdited = false, openRequested = true;
    std::string error;
    std::uint64_t revision = 1;
};
} // namespace kb::editor
