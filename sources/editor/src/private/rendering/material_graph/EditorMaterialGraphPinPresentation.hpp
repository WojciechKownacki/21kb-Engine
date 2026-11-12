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
// The lists are built once per node type (and once per set of pins for a node whose pins are its own, such
// as custom code) and handed out by reference, so drawing a node allocates nothing. A reference stays valid
// until the editor language changes or the same node's pins are edited.
class EditorMaterialGraphPinPresentationCatalog final {
  public:
    EditorMaterialGraphPinPresentationCatalog() = delete;

    [[nodiscard]] static const std::vector<EditorMaterialGraphPinPresentation>&
    InputPins(kb::render::RenderMaterialGraphNodeKind kind);
    [[nodiscard]] static const std::vector<EditorMaterialGraphPinPresentation>&
    InputPins(const kb::render::RenderMaterialGraphNode& node);
    [[nodiscard]] static const std::vector<EditorMaterialGraphPinPresentation>&
    OutputPins(kb::render::RenderMaterialGraphNodeKind kind);
    [[nodiscard]] static const std::vector<EditorMaterialGraphPinPresentation>&
    OutputPins(const kb::render::RenderMaterialGraphNode& node);

    [[nodiscard]] static std::vector<std::string> InputPinNames(kb::render::RenderMaterialGraphNodeKind kind);
    [[nodiscard]] static std::vector<std::string> OutputPinNames(kb::render::RenderMaterialGraphNodeKind kind);
};

} // namespace kb::editor
