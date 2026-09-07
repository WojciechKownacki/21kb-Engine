#pragma once

#include "engine/math/EngineMath.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace kb::scene {

enum class UITextHorizontalAlignment : std::uint8_t {
    Left,
    Center,
    Right,
};

enum class UITextVerticalAlignment : std::uint8_t {
    Top,
    Center,
    Bottom,
};

enum class UITextWrapMode : std::uint8_t {
    NoWrap,
    Word,
    Character,
};

struct UIText {
    static constexpr std::string_view StableId = "kb21.ui.text";
    static constexpr std::uint32_t SchemaVersion = 1U;
    static constexpr std::size_t MaxUtf8Bytes = 1024U;

    std::array<char, MaxUtf8Bytes> content{};
    std::uint64_t fontAssetId = 0U;
    float fontSize = 16.0F;
    kb::math::Color color{};
    UITextHorizontalAlignment horizontalAlignment = UITextHorizontalAlignment::Left;
    UITextVerticalAlignment verticalAlignment = UITextVerticalAlignment::Top;
    UITextWrapMode wrapMode = UITextWrapMode::Word;
    float lineSpacing = 1.0F;
    bool richText = true;
};

[[nodiscard]] inline std::string_view UITextContent(const UIText& value) noexcept {
    const auto end = std::find(value.content.begin(), value.content.end(), '\0');
    return std::string_view{value.content.data(), static_cast<std::size_t>(end - value.content.begin())};
}

[[nodiscard]] inline bool SetUITextContent(UIText& value, std::string_view text) noexcept {
    if (text.size() >= value.content.size() || text.find('\0') != std::string_view::npos) {
        return false;
    }
    value.content.fill('\0');
    std::copy(text.begin(), text.end(), value.content.begin());
    return true;
}

} // namespace kb::scene
