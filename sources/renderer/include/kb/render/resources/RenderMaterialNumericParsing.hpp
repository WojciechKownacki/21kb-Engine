#pragma once

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace kb::render {

[[nodiscard]] inline bool ParseFiniteMaterialFloatToken(std::string_view text, float& output) noexcept {
    if (text.empty()) {
        return false;
    }
    float parsed = 0.0F;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(parsed)) {
        return false;
    }
    output = parsed;
    return true;
}

[[nodiscard]] inline bool ParseFiniteMaterialFloatSequence(
    std::string_view text,
    std::vector<float>& output,
    std::size_t minimumCount,
    std::size_t maximumCount,
    bool acceptCommaSeparators = true) {
    output.clear();
    if (minimumCount > maximumCount) {
        return false;
    }

    std::string normalized{ text };
    if (acceptCommaSeparators) {
        for (char& ch : normalized) {
            if (ch == ',') {
                ch = ' ';
            }
        }
    }

    std::size_t cursor = 0U;
    while (cursor < normalized.size()) {
        while (cursor < normalized.size() &&
               (normalized[cursor] == ' ' || normalized[cursor] == '\t' ||
                normalized[cursor] == '\r' || normalized[cursor] == '\n')) {
            ++cursor;
        }
        if (cursor == normalized.size()) {
            break;
        }
        const std::size_t begin = cursor;
        while (cursor < normalized.size() &&
               normalized[cursor] != ' ' && normalized[cursor] != '\t' &&
               normalized[cursor] != '\r' && normalized[cursor] != '\n') {
            ++cursor;
        }
        if (output.size() == maximumCount) {
            output.clear();
            return false;
        }
        float parsed = 0.0F;
        if (!ParseFiniteMaterialFloatToken(std::string_view{ normalized }.substr(begin, cursor - begin), parsed)) {
            output.clear();
            return false;
        }
        output.push_back(parsed);
    }

    if (output.size() < minimumCount || output.size() > maximumCount) {
        output.clear();
        return false;
    }
    return true;
}

template <std::size_t Count>
[[nodiscard]] inline bool ParseExactFiniteMaterialFloatTuple(
    std::string_view text,
    std::array<float, Count>& output,
    bool acceptCommaSeparators = true) {
    std::vector<float> parsed;
    if (!ParseFiniteMaterialFloatSequence(text, parsed, Count, Count, acceptCommaSeparators)) {
        return false;
    }
    for (std::size_t index = 0U; index < Count; ++index) {
        output[index] = parsed[index];
    }
    return true;
}

} // namespace kb::render
