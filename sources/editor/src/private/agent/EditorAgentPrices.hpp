#pragma once

#include "engine/core/JsonValue.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

// What one request or one conversation used, counted apart: input the model read fresh, input it took from the agent's cache,
// input it wrote to that cache, and what it wrote back.
struct EditorAgentTokens {
    std::uint64_t uncached = 0U;
    std::uint64_t cacheRead = 0U;
    std::uint64_t cacheWrite = 0U;
    std::uint64_t output = 0U;
};

// The price of one model per million tokens, in the table's currency. A price left out is the input price.
struct EditorAgentPrice {
    std::string model;
    double input = 0.0;
    double cacheRead = 0.0;
    double cacheWrite = 0.0;
    double output = 0.0;
};

// The user's price table, read from the agents file next to the editor's other preferences ("currency" and "prices": a model
// name or a part of it, with input, cacheRead, cacheWrite and output). Nothing is built in: a model with no entry has no price.
class EditorAgentPrices final {
public:
    [[nodiscard]] static EditorAgentPrices FromJson(const kb::core::JsonValue& document);

    // The entry whose name is part of `model`, the longest such name when several are; nullptr when none is.
    [[nodiscard]] const EditorAgentPrice* Find(std::string_view model) const noexcept;
    [[nodiscard]] static double Cost(const EditorAgentPrice& price, const EditorAgentTokens& tokens) noexcept;
    [[nodiscard]] const std::string& Currency() const noexcept { return currency_; }

private:
    std::string currency_ = "USD";
    std::vector<EditorAgentPrice> prices_;
};

} // namespace kb::editor
