#pragma once

#include "engine/visual/VisualGraphTypes.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace kb::visual {

struct VisualGraphPinTemplate {
    VisualGraphPinDirection direction = VisualGraphPinDirection::Input;
    std::string name;
    VisualGraphValueType type = VisualGraphValueType::Void;
    bool required = true;
};

struct VisualGraphNodeDefinition {
    VisualGraphNodeKind kind = VisualGraphNodeKind::Comment;
    std::string displayName;
    bool requiresSymbol = false;
    std::vector<VisualGraphPinTemplate> pins;
};

class VisualGraphNodeDefinitionRegistry final {
public:
    [[nodiscard]] static VisualGraphNodeDefinitionRegistry CreateDefault();

    [[nodiscard]] bool Register(VisualGraphNodeDefinition definition);
    [[nodiscard]] const VisualGraphNodeDefinition* Find(VisualGraphNodeKind kind) const noexcept;
    [[nodiscard]] std::vector<VisualGraphPin> CreatePinsForNode(const VisualGraphNode& node) const;
    [[nodiscard]] const std::vector<VisualGraphNodeDefinition>& Definitions() const noexcept;

private:
    std::vector<VisualGraphNodeDefinition> definitions_;
};

} // namespace kb::visual
