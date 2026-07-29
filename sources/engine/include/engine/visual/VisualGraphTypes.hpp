#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kb::visual {

enum class VisualGraphValueType : std::uint8_t {
    Void,
    Bool,
    Int,
    Float,
    String,
    Entity,
    Component,
    // LIB-041: appended after Component (not inserted between existing
    // values) so the numeric value of every pre-existing enumerator stays
    // stable for anything that persists it by integer (e.g.
    // kb::library::DescribeType's array index).
    Int64,
    UInt32,
    Double,
    Name,
    Guid,
    Hash,
};

enum class VisualGraphLifecycleEvent : std::uint8_t {
    Created,
    Activated,
    Ready,
    FixedTick,
    Tick,
    LateTick,
    BeforeRender,
    AfterRender,
    Deactivated,
    Destroyed,
};

enum class VisualGraphNodeKind : std::uint8_t {
    Event,
    CustomEvent,
    Sequence,
    Branch,
    GetComponent,
    GetProperty,
    SetProperty,
    CallNative,
    EmitEvent,
    Wait,
    Comment,
};

enum class VisualGraphPinDirection : std::uint8_t {
    Input,
    Output,
};

enum class VisualGraphEdgeKind : std::uint8_t {
    Execution,
    Data,
};

// Data edges may use only conversions which preserve every source value.
// Lossy conversions require an explicit authoring node so a graph cannot hide
// truncation or rounding behind an ordinary wire.
enum class VisualGraphValueConversion : std::uint8_t {
    Identity,
    Lossless,
    Lossy,
    Incompatible,
};

struct VisualGraphVariable {
    std::string name;
    VisualGraphValueType type = VisualGraphValueType::Void;
    std::string defaultValue;
};

struct VisualGraphNode {
    std::uint32_t id = 0;
    VisualGraphNodeKind kind = VisualGraphNodeKind::Comment;
    VisualGraphLifecycleEvent lifecycle = VisualGraphLifecycleEvent::Tick;
    std::string symbol;
};

struct VisualGraphPin {
    std::uint32_t nodeId = 0;
    VisualGraphPinDirection direction = VisualGraphPinDirection::Input;
    std::string name;
    VisualGraphValueType type = VisualGraphValueType::Void;
};

struct VisualGraphEdge {
    std::uint32_t fromNode = 0;
    std::string fromPin;
    std::uint32_t toNode = 0;
    std::string toPin;
    VisualGraphEdgeKind kind = VisualGraphEdgeKind::Execution;
};

struct VisualGraphAsset {
    static constexpr std::uint32_t kCurrentVersion = 1U;

    std::uint32_t version = kCurrentVersion;
    std::string name;
    std::vector<VisualGraphVariable> variables;
    std::vector<VisualGraphNode> nodes;
    std::vector<VisualGraphPin> pins;
    std::vector<VisualGraphEdge> edges;

    [[nodiscard]] const VisualGraphNode* FindNode(std::uint32_t id) const noexcept;
    [[nodiscard]] const VisualGraphPin* FindPin(std::uint32_t nodeId, std::string_view pinName, VisualGraphPinDirection direction) const noexcept;
};

[[nodiscard]] const char* ToString(VisualGraphValueType type) noexcept;
[[nodiscard]] const char* ToString(VisualGraphLifecycleEvent event) noexcept;
[[nodiscard]] const char* ToString(VisualGraphNodeKind kind) noexcept;
[[nodiscard]] const char* ToString(VisualGraphPinDirection direction) noexcept;
[[nodiscard]] const char* ToString(VisualGraphEdgeKind kind) noexcept;
[[nodiscard]] VisualGraphValueConversion ClassifyVisualGraphValueConversion(VisualGraphValueType source, VisualGraphValueType target) noexcept;
[[nodiscard]] bool IsImplicitVisualGraphValueConversion(VisualGraphValueType source, VisualGraphValueType target) noexcept;
[[nodiscard]] bool TryParseVisualGraphValueType(std::string_view text, VisualGraphValueType& output) noexcept;
[[nodiscard]] bool TryParseVisualGraphLifecycleEvent(std::string_view text, VisualGraphLifecycleEvent& output) noexcept;
[[nodiscard]] bool TryParseVisualGraphNodeKind(std::string_view text, VisualGraphNodeKind& output) noexcept;
[[nodiscard]] bool TryParseVisualGraphPinDirection(std::string_view text, VisualGraphPinDirection& output) noexcept;
[[nodiscard]] bool TryParseVisualGraphEdgeKind(std::string_view text, VisualGraphEdgeKind& output) noexcept;

} // namespace kb::visual
