#pragma once

#include "engine/visual/VisualGraphRuntimeValue.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace kb::visual {

// Literal producers use ordinary GetProperty nodes, preserving the graph format.
// Text is encoded in the symbol so whitespace and comment markers round-trip.
class VisualGraphLiteral final {
public:
    [[nodiscard]] static bool IsSymbol(std::string_view symbol) noexcept;
    [[nodiscard]] static std::string Symbol(std::string_view text);
    [[nodiscard]] static std::optional<std::string> Text(std::string_view symbol);
    [[nodiscard]] static std::optional<VisualGraphRuntimeValue> Parse(VisualGraphValueType type, std::string_view text);
    [[nodiscard]] static std::optional<VisualGraphRuntimeValue> Read(VisualGraphValueType type, std::string_view symbol);
    [[nodiscard]] static std::string Display(const VisualGraphRuntimeValue& value);
};

} // namespace kb::visual
