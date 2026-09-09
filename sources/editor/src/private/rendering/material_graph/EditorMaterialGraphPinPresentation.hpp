#pragma once

#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <string>
#include <vector>

namespace kb::editor {

struct EditorMaterialGraphPinPresentation final {
    std::string name;
    std::string label;
};

// Owns editor-only pin order and labels. Pin membership always comes from the
// renderer's material graph schema and cannot be expanded or reduced here.
class EditorMaterialGraphPinPresentationCatalog final {
  public:
    EditorMaterialGraphPinPresentationCatalog() = delete;

    [[nodiscard]] static std::vector<EditorMaterialGraphPinPresentation>
    InputPins(kb::render::RenderMaterialGraphNodeKind kind);
    [[nodiscard]] static std::vector<EditorMaterialGraphPinPresentation>
    InputPins(const kb::render::RenderMaterialGraphNode& node);
    [[nodiscard]] static std::vector<EditorMaterialGraphPinPresentation>
    OutputPins(kb::render::RenderMaterialGraphNodeKind kind);
    [[nodiscard]] static std::vector<EditorMaterialGraphPinPresentation>
    OutputPins(const kb::render::RenderMaterialGraphNode& node);

    [[nodiscard]] static std::vector<std::string> InputPinNames(kb::render::RenderMaterialGraphNodeKind kind);
    [[nodiscard]] static std::vector<std::string> OutputPinNames(kb::render::RenderMaterialGraphNodeKind kind);
};

} // namespace kb::editor
