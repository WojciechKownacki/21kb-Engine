#include "engine/visual/VisualGraphNodeDefinitionRegistry.hpp"

#include <ranges>
#include <utility>

namespace kb::visual {
namespace {

[[nodiscard]] VisualGraphPinTemplate Input(std::string name, VisualGraphValueType type = VisualGraphValueType::Void) {
    return VisualGraphPinTemplate{
        .direction = VisualGraphPinDirection::Input,
        .name = std::move(name),
        .type = type,
    };
}

[[nodiscard]] VisualGraphPinTemplate Output(std::string name, VisualGraphValueType type = VisualGraphValueType::Void) {
    return VisualGraphPinTemplate{
        .direction = VisualGraphPinDirection::Output,
        .name = std::move(name),
        .type = type,
    };
}

} // namespace

VisualGraphNodeDefinitionRegistry VisualGraphNodeDefinitionRegistry::CreateDefault() {
    VisualGraphNodeDefinitionRegistry registry;
    static_cast<void>(registry.Register(VisualGraphNodeDefinition{
        .kind = VisualGraphNodeKind::Event,
        .displayName = "Event",
        .requiresSymbol = false,
        .pins = {Output("then")},
    }));
    static_cast<void>(registry.Register(VisualGraphNodeDefinition{
        .kind = VisualGraphNodeKind::CustomEvent,
        .displayName = "Custom Event",
        .requiresSymbol = true,
        .pins = {Output("then")},
    }));
    static_cast<void>(registry.Register(VisualGraphNodeDefinition{
        .kind = VisualGraphNodeKind::Sequence,
        .displayName = "Sequence",
        .requiresSymbol = false,
        .pins = {Input("exec"), Output("then")},
    }));
    static_cast<void>(registry.Register(VisualGraphNodeDefinition{
        .kind = VisualGraphNodeKind::Branch,
        .displayName = "Branch",
        .requiresSymbol = false,
        .pins = {Input("exec"), Input("condition", VisualGraphValueType::Bool), Output("true"), Output("false")},
    }));
    static_cast<void>(registry.Register(VisualGraphNodeDefinition{
        .kind = VisualGraphNodeKind::GetComponent,
        .displayName = "Get Component",
        .requiresSymbol = true,
        .pins = {Input("exec"), Output("then"), Output("component", VisualGraphValueType::Component)},
    }));
    static_cast<void>(registry.Register(VisualGraphNodeDefinition{
        .kind = VisualGraphNodeKind::GetProperty,
        .displayName = "Get Property",
        .requiresSymbol = true,
        .pins = {Output("value")},
    }));
    static_cast<void>(registry.Register(VisualGraphNodeDefinition{
        .kind = VisualGraphNodeKind::SetProperty,
        .displayName = "Set Property",
        .requiresSymbol = true,
        .pins = {Input("exec"), Input("value"), Output("then")},
    }));
    static_cast<void>(registry.Register(VisualGraphNodeDefinition{
        .kind = VisualGraphNodeKind::CallNative,
        .displayName = "Call Native",
        .requiresSymbol = true,
        // LIB-061: "then" is the success path (unchanged name, for
        // backward compatibility with existing graphs) and "failed" is a
        // new, optional failure path — mirroring Branch's "true"/"false"
        // pair. Leaving "failed" unwired preserves the historical
        // fail-loud default (see VisualGraphRuntimeExecutor::ExecuteNode).
        .pins = {Input("exec"), Output("then"), Output("failed")},
    }));
    static_cast<void>(registry.Register(VisualGraphNodeDefinition{
        .kind = VisualGraphNodeKind::EmitEvent,
        .displayName = "Emit Event",
        .requiresSymbol = true,
        .pins = {Input("exec"), Output("then")},
    }));
    static_cast<void>(registry.Register(VisualGraphNodeDefinition{
        .kind = VisualGraphNodeKind::Wait,
        .displayName = "Wait",
        .requiresSymbol = false,
        .pins = {Input("exec"), Output("then")},
    }));
    static_cast<void>(registry.Register(VisualGraphNodeDefinition{
        .kind = VisualGraphNodeKind::Comment,
        .displayName = "Comment",
        .requiresSymbol = false,
        .pins = {},
    }));
    return registry;
}

bool VisualGraphNodeDefinitionRegistry::Register(VisualGraphNodeDefinition definition) {
    if (definition.displayName.empty() || Find(definition.kind) != nullptr) {
        return false;
    }
    definitions_.push_back(std::move(definition));
    return true;
}

const VisualGraphNodeDefinition* VisualGraphNodeDefinitionRegistry::Find(VisualGraphNodeKind kind) const noexcept {
    const auto iter = std::ranges::find_if(definitions_, [kind](const VisualGraphNodeDefinition& definition) {
        return definition.kind == kind;
    });
    return iter == definitions_.end() ? nullptr : &*iter;
}

std::vector<VisualGraphPin> VisualGraphNodeDefinitionRegistry::CreatePinsForNode(const VisualGraphNode& node) const {
    const VisualGraphNodeDefinition* definition = Find(node.kind);
    if (definition == nullptr) {
        return {};
    }

    std::vector<VisualGraphPin> pins;
    pins.reserve(definition->pins.size());
    for (const VisualGraphPinTemplate& pinTemplate : definition->pins) {
        pins.push_back(VisualGraphPin{
            .nodeId = node.id,
            .direction = pinTemplate.direction,
            .name = pinTemplate.name,
            .type = pinTemplate.type,
        });
    }
    return pins;
}

const std::vector<VisualGraphNodeDefinition>& VisualGraphNodeDefinitionRegistry::Definitions() const noexcept {
    return definitions_;
}

} // namespace kb::visual
