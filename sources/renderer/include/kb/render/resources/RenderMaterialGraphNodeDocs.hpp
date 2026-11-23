#pragma once

#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <span>
#include <string_view>

namespace kb::render {

// What a material graph node is for, written once beside the node registry: the pins, their types and the value
// hint come from the registry itself, so only the words are kept here. A node without an entry fails the tests.
struct RenderMaterialGraphNodeDoc {
    RenderMaterialGraphNodeKind kind = RenderMaterialGraphNodeKind::MaterialOutput;
    std::string_view group;    // Constant, Parameter, Texture, Math, Vector, Color, Pattern, Coordinate, Time, Object, Camera, Scene, Attributes, Switch, Function, Utility
    std::string_view purpose;  // one sentence
    std::string_view value;    // the node's value text ("speedU speedV"); empty when the node takes none
};

[[nodiscard]] std::span<const RenderMaterialGraphNodeDoc> RenderMaterialGraphNodeDocs() noexcept;
[[nodiscard]] const RenderMaterialGraphNodeDoc* FindRenderMaterialGraphNodeDoc(RenderMaterialGraphNodeKind kind) noexcept;

} // namespace kb::render
