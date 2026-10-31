#include "engine/visual/VisualGraphLiteral.hpp"

#include <charconv>
#include <cmath>
#include <limits>
#include <type_traits>

namespace kb::visual {
namespace {
template<class T> std::optional<T> Number(std::string_view text) {
    if (text.empty()) return std::nullopt;
    T value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return std::nullopt;
    if constexpr (std::is_floating_point_v<T>) if (!std::isfinite(value)) return std::nullopt;
    return value;
}
int Hex(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}
template<class T> std::string Format(T value) {
    char buffer[128]{};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return result.ec == std::errc{} ? std::string(buffer, result.ptr) : std::string{};
}
} // namespace

bool VisualGraphLiteral::IsSymbol(std::string_view symbol) noexcept { return symbol.starts_with("Literal:"); }
std::string VisualGraphLiteral::Symbol(std::string_view text) {
    constexpr char digits[] = "0123456789abcdef";
    std::string symbol = "Literal:";
    for (const unsigned char ch : text) { symbol.push_back(digits[ch >> 4]); symbol.push_back(digits[ch & 15]); }
    return symbol;
}
std::optional<std::string> VisualGraphLiteral::Text(std::string_view symbol) {
    if (!IsSymbol(symbol)) return std::nullopt;
    symbol.remove_prefix(8);
    if (symbol.size() % 2 != 0 || symbol.size() > 131072) return std::nullopt;
    std::string text;
    for (std::size_t i = 0; i < symbol.size(); i += 2) {
        const int high = Hex(symbol[i]), low = Hex(symbol[i + 1]);
        if (high < 0 || low < 0) return std::nullopt;
        const char ch = static_cast<char>(high * 16 + low);
        if (static_cast<unsigned char>(ch) < 32 && ch != '\n' && ch != '\r' && ch != '\t') return std::nullopt;
        text.push_back(ch);
    }
    return text;
}
std::optional<VisualGraphRuntimeValue> VisualGraphLiteral::Parse(VisualGraphValueType type, std::string_view text) {
    switch (type) {
    case VisualGraphValueType::Bool:
        if (text == "true") return VisualGraphRuntimeValue{true};
        if (text == "false") return VisualGraphRuntimeValue{false};
        return std::nullopt;
    case VisualGraphValueType::Int:
        if (const auto value = Number<int>(text)) return VisualGraphRuntimeValue{*value};
        break;
    case VisualGraphValueType::Int64:
        if (const auto value = Number<std::int64_t>(text)) return VisualGraphRuntimeValue{*value};
        break;
    case VisualGraphValueType::Float:
        if (const auto value = Number<float>(text)) return VisualGraphRuntimeValue{*value};
        break;
    case VisualGraphValueType::Double:
        if (const auto value = Number<double>(text)) return VisualGraphRuntimeValue{*value};
        break;
    case VisualGraphValueType::UInt32:
    case VisualGraphValueType::Hash:
        if (const auto value = Number<std::uint64_t>(text)) {
            if (type != VisualGraphValueType::UInt32 || *value <= std::numeric_limits<std::uint32_t>::max())
                return VisualGraphRuntimeValue{*value, type};
        }
        break;
    case VisualGraphValueType::String:
    case VisualGraphValueType::Name:
    case VisualGraphValueType::Guid:
        if (Text(Symbol(text))) return VisualGraphRuntimeValue{std::string(text), type};
        break;
    case VisualGraphValueType::Entity:
    case VisualGraphValueType::Component:
    case VisualGraphValueType::Void:
        break; // Runtime handles come from live bindings, never persisted literals.
    }
    return std::nullopt;
}
std::optional<VisualGraphRuntimeValue> VisualGraphLiteral::Read(VisualGraphValueType type, std::string_view symbol) {
    const auto text = Text(symbol);
    return text ? Parse(type, *text) : std::nullopt;
}
std::string VisualGraphLiteral::Display(const VisualGraphRuntimeValue& value) {
    switch (value.Type()) {
    case VisualGraphValueType::Bool: return value.AsBool() ? "true" : "false";
    case VisualGraphValueType::Int: return Format(value.AsInt());
    case VisualGraphValueType::Int64: return Format(value.AsInt64());
    case VisualGraphValueType::Float: return Format(value.AsFloat());
    case VisualGraphValueType::Double: return Format(value.AsDouble());
    case VisualGraphValueType::String:
    case VisualGraphValueType::Name:
    case VisualGraphValueType::Guid: return value.AsString();
    case VisualGraphValueType::Entity:
    case VisualGraphValueType::Component:
    case VisualGraphValueType::UInt32:
    case VisualGraphValueType::Hash: return Format(value.AsUInt64());
    case VisualGraphValueType::Void: return {};
    }
    return {};
}
} // namespace kb::visual
