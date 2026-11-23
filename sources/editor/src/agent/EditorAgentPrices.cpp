#include "agent/EditorAgentPrices.hpp"

#include <algorithm>
#include <cctype>

namespace kb::editor {
namespace {

[[nodiscard]] std::string Lowered(std::string_view text) {
    std::string lowered{ text };
    std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}

[[nodiscard]] double Number(const kb::core::JsonValue& entry, std::string_view key, double fallback) {
    const kb::core::JsonValue* value = entry.Find(key);
    return value != nullptr && value->GetKind() == kb::core::JsonValue::Kind::Number ? std::max(0.0, value->AsNumber()) : fallback;
}

} // namespace

EditorAgentPrices EditorAgentPrices::FromJson(const kb::core::JsonValue& document) {
    EditorAgentPrices prices;
    if (const kb::core::JsonValue* currency = document.Find("currency");
        currency != nullptr && currency->GetKind() == kb::core::JsonValue::Kind::String && !currency->AsString().empty()) {
        prices.currency_ = currency->AsString();
    }
    const kb::core::JsonValue* list = document.Find("prices");
    if (list == nullptr || list->GetKind() != kb::core::JsonValue::Kind::Object) return prices;
    for (std::size_t index = 0U; index < list->Size(); ++index) {
        const kb::core::JsonValue* entry = list->At(index);
        if (entry == nullptr || entry->GetKind() != kb::core::JsonValue::Kind::Object || list->MemberName(index).empty()) continue;
        EditorAgentPrice price;
        price.model = Lowered(list->MemberName(index));
        price.input = Number(*entry, "input", 0.0);
        price.cacheRead = Number(*entry, "cacheRead", price.input);
        price.cacheWrite = Number(*entry, "cacheWrite", price.input);
        price.output = Number(*entry, "output", 0.0);
        prices.prices_.push_back(std::move(price));
    }
    return prices;
}

const EditorAgentPrice* EditorAgentPrices::Find(std::string_view model) const noexcept {
    const std::string name = Lowered(model);
    const EditorAgentPrice* best = nullptr;
    for (const EditorAgentPrice& price : prices_) {
        if (name.find(price.model) != std::string::npos && (best == nullptr || price.model.size() > best->model.size())) best = &price;
    }
    return best;
}

double EditorAgentPrices::Cost(const EditorAgentPrice& price, const EditorAgentTokens& tokens) noexcept {
    return (static_cast<double>(tokens.uncached) * price.input + static_cast<double>(tokens.cacheRead) * price.cacheRead +
               static_cast<double>(tokens.cacheWrite) * price.cacheWrite + static_cast<double>(tokens.output) * price.output) /
        1000000.0;
}

} // namespace kb::editor
