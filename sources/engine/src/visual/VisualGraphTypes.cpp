#include "engine/visual/VisualGraphTypes.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace kb::visual {
namespace {

template <typename T, std::size_t N>
[[nodiscard]] bool ParseNamedValue(std::string_view text, const std::array<std::pair<std::string_view, T>, N>& values, T& output) noexcept {
    const auto iter = std::ranges::find_if(values, [text](const auto& entry) {
        return entry.first == text;
    });
    if (iter == values.end()) {
        return false;
    }
    output = iter->second;
    return true;
}

} // namespace

const VisualGraphNode* VisualGraphAsset::FindNode(std::uint32_t id) const noexcept {
    const auto iter = std::ranges::find_if(nodes, [id](const VisualGraphNode& node) {
        return node.id == id;
    });
    return iter == nodes.end() ? nullptr : &*iter;
}

const VisualGraphPin* VisualGraphAsset::FindPin(std::uint32_t nodeId, std::string_view pinName, VisualGraphPinDirection direction) const noexcept {
    const auto iter = std::ranges::find_if(pins, [nodeId, pinName, direction](const VisualGraphPin& pin) {
        return pin.nodeId == nodeId && pin.name == pinName && pin.direction == direction;
    });
    return iter == pins.end() ? nullptr : &*iter;
}

const char* ToString(VisualGraphValueType type) noexcept {
    switch (type) {
    case VisualGraphValueType::Void:
        return "Void";
    case VisualGraphValueType::Bool:
        return "Bool";
    case VisualGraphValueType::Int:
        return "Int";
    case VisualGraphValueType::Float:
        return "Float";
    case VisualGraphValueType::String:
        return "String";
    case VisualGraphValueType::Entity:
        return "Entity";
    case VisualGraphValueType::Component:
        return "Component";
    case VisualGraphValueType::Int64:
        return "Int64";
    case VisualGraphValueType::UInt32:
        return "UInt32";
    case VisualGraphValueType::Double:
        return "Double";
    case VisualGraphValueType::Name:
        return "Name";
    case VisualGraphValueType::Guid:
        return "Guid";
    case VisualGraphValueType::Hash:
        return "Hash";
    }
    return "Void";
}

const char* ToString(VisualGraphLifecycleEvent event) noexcept {
    switch (event) {
    case VisualGraphLifecycleEvent::Created:
        return "Created";
    case VisualGraphLifecycleEvent::Activated:
        return "Activated";
    case VisualGraphLifecycleEvent::Ready:
        return "Ready";
    case VisualGraphLifecycleEvent::FixedTick:
        return "FixedTick";
    case VisualGraphLifecycleEvent::Tick:
        return "Tick";
    case VisualGraphLifecycleEvent::LateTick:
        return "LateTick";
    case VisualGraphLifecycleEvent::BeforeRender:
        return "BeforeRender";
    case VisualGraphLifecycleEvent::AfterRender:
        return "AfterRender";
    case VisualGraphLifecycleEvent::Deactivated:
        return "Deactivated";
    case VisualGraphLifecycleEvent::Destroyed:
        return "Destroyed";
    }
    return "Tick";
}

const char* ToString(VisualGraphNodeKind kind) noexcept {
    switch (kind) {
    case VisualGraphNodeKind::Event:
        return "Event";
    case VisualGraphNodeKind::CustomEvent:
        return "CustomEvent";
    case VisualGraphNodeKind::Sequence:
        return "Sequence";
    case VisualGraphNodeKind::Branch:
        return "Branch";
    case VisualGraphNodeKind::GetComponent:
        return "GetComponent";
    case VisualGraphNodeKind::GetProperty:
        return "GetProperty";
    case VisualGraphNodeKind::SetProperty:
        return "SetProperty";
    case VisualGraphNodeKind::CallNative:
        return "CallNative";
    case VisualGraphNodeKind::EmitEvent:
        return "EmitEvent";
    case VisualGraphNodeKind::Wait:
        return "Wait";
    case VisualGraphNodeKind::Comment:
        return "Comment";
    }
    return "Comment";
}

const char* ToString(VisualGraphPinDirection direction) noexcept {
    switch (direction) {
    case VisualGraphPinDirection::Input:
        return "Input";
    case VisualGraphPinDirection::Output:
        return "Output";
    }
    return "Input";
}

const char* ToString(VisualGraphEdgeKind kind) noexcept {
    switch (kind) {
    case VisualGraphEdgeKind::Execution:
        return "Execution";
    case VisualGraphEdgeKind::Data:
        return "Data";
    }
    return "Execution";
}

bool TryParseVisualGraphValueType(std::string_view text, VisualGraphValueType& output) noexcept {
    static constexpr std::array values{
        std::pair<std::string_view, VisualGraphValueType>{"Void", VisualGraphValueType::Void},
        std::pair<std::string_view, VisualGraphValueType>{"Bool", VisualGraphValueType::Bool},
        std::pair<std::string_view, VisualGraphValueType>{"Int", VisualGraphValueType::Int},
        std::pair<std::string_view, VisualGraphValueType>{"Float", VisualGraphValueType::Float},
        std::pair<std::string_view, VisualGraphValueType>{"String", VisualGraphValueType::String},
        std::pair<std::string_view, VisualGraphValueType>{"Entity", VisualGraphValueType::Entity},
        std::pair<std::string_view, VisualGraphValueType>{"Component", VisualGraphValueType::Component},
        std::pair<std::string_view, VisualGraphValueType>{"Int64", VisualGraphValueType::Int64},
        std::pair<std::string_view, VisualGraphValueType>{"UInt32", VisualGraphValueType::UInt32},
        std::pair<std::string_view, VisualGraphValueType>{"Double", VisualGraphValueType::Double},
        std::pair<std::string_view, VisualGraphValueType>{"Name", VisualGraphValueType::Name},
        std::pair<std::string_view, VisualGraphValueType>{"Guid", VisualGraphValueType::Guid},
        std::pair<std::string_view, VisualGraphValueType>{"Hash", VisualGraphValueType::Hash},
    };
    return ParseNamedValue(text, values, output);
}

bool TryParseVisualGraphLifecycleEvent(std::string_view text, VisualGraphLifecycleEvent& output) noexcept {
    static constexpr std::array values{
        std::pair<std::string_view, VisualGraphLifecycleEvent>{"Created", VisualGraphLifecycleEvent::Created},
        std::pair<std::string_view, VisualGraphLifecycleEvent>{"Activated", VisualGraphLifecycleEvent::Activated},
        std::pair<std::string_view, VisualGraphLifecycleEvent>{"Ready", VisualGraphLifecycleEvent::Ready},
        std::pair<std::string_view, VisualGraphLifecycleEvent>{"FixedTick", VisualGraphLifecycleEvent::FixedTick},
        std::pair<std::string_view, VisualGraphLifecycleEvent>{"Tick", VisualGraphLifecycleEvent::Tick},
        std::pair<std::string_view, VisualGraphLifecycleEvent>{"LateTick", VisualGraphLifecycleEvent::LateTick},
        std::pair<std::string_view, VisualGraphLifecycleEvent>{"BeforeRender", VisualGraphLifecycleEvent::BeforeRender},
        std::pair<std::string_view, VisualGraphLifecycleEvent>{"AfterRender", VisualGraphLifecycleEvent::AfterRender},
        std::pair<std::string_view, VisualGraphLifecycleEvent>{"Deactivated", VisualGraphLifecycleEvent::Deactivated},
        std::pair<std::string_view, VisualGraphLifecycleEvent>{"Destroyed", VisualGraphLifecycleEvent::Destroyed},
    };
    return ParseNamedValue(text, values, output);
}

bool TryParseVisualGraphNodeKind(std::string_view text, VisualGraphNodeKind& output) noexcept {
    static constexpr std::array values{
        std::pair<std::string_view, VisualGraphNodeKind>{"Event", VisualGraphNodeKind::Event},
        std::pair<std::string_view, VisualGraphNodeKind>{"CustomEvent", VisualGraphNodeKind::CustomEvent},
        std::pair<std::string_view, VisualGraphNodeKind>{"Sequence", VisualGraphNodeKind::Sequence},
        std::pair<std::string_view, VisualGraphNodeKind>{"Branch", VisualGraphNodeKind::Branch},
        std::pair<std::string_view, VisualGraphNodeKind>{"GetComponent", VisualGraphNodeKind::GetComponent},
        std::pair<std::string_view, VisualGraphNodeKind>{"GetProperty", VisualGraphNodeKind::GetProperty},
        std::pair<std::string_view, VisualGraphNodeKind>{"SetProperty", VisualGraphNodeKind::SetProperty},
        std::pair<std::string_view, VisualGraphNodeKind>{"CallNative", VisualGraphNodeKind::CallNative},
        std::pair<std::string_view, VisualGraphNodeKind>{"EmitEvent", VisualGraphNodeKind::EmitEvent},
        std::pair<std::string_view, VisualGraphNodeKind>{"Wait", VisualGraphNodeKind::Wait},
        std::pair<std::string_view, VisualGraphNodeKind>{"Comment", VisualGraphNodeKind::Comment},
    };
    return ParseNamedValue(text, values, output);
}

bool TryParseVisualGraphPinDirection(std::string_view text, VisualGraphPinDirection& output) noexcept {
    static constexpr std::array values{
        std::pair<std::string_view, VisualGraphPinDirection>{"Input", VisualGraphPinDirection::Input},
        std::pair<std::string_view, VisualGraphPinDirection>{"Output", VisualGraphPinDirection::Output},
        std::pair<std::string_view, VisualGraphPinDirection>{"input", VisualGraphPinDirection::Input},
        std::pair<std::string_view, VisualGraphPinDirection>{"output", VisualGraphPinDirection::Output},
    };
    return ParseNamedValue(text, values, output);
}

bool TryParseVisualGraphEdgeKind(std::string_view text, VisualGraphEdgeKind& output) noexcept {
    static constexpr std::array values{
        std::pair<std::string_view, VisualGraphEdgeKind>{"Execution", VisualGraphEdgeKind::Execution},
        std::pair<std::string_view, VisualGraphEdgeKind>{"Data", VisualGraphEdgeKind::Data},
        std::pair<std::string_view, VisualGraphEdgeKind>{"exec", VisualGraphEdgeKind::Execution},
        std::pair<std::string_view, VisualGraphEdgeKind>{"data", VisualGraphEdgeKind::Data},
    };
    return ParseNamedValue(text, values, output);
}

} // namespace kb::visual
