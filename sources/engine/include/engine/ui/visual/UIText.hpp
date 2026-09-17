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

enum class UITextOverflow : std::uint8_t {
    // Lines past the bottom of the box still draw.
    Overflow,
    // Lines that do not fit are left out.
    Truncate,
    // Lines that do not fit are left out and the last one shown ends in an ellipsis.
    Ellipsis,
};

struct UIText {
    static constexpr std::string_view StableId = "kb21.ui.text";
    static constexpr std::uint32_t SchemaVersion = 2U;
    static constexpr std::size_t MaxUtf8Bytes = 1024U;
    static constexpr std::size_t MaxLocalizationKeyBytes = 128U;

    std::array<char, MaxUtf8Bytes> content{};
    std::uint64_t fontAssetId = 0U;
    float fontSize = 16.0F;
    kb::math::Color color{};
    UITextHorizontalAlignment horizontalAlignment = UITextHorizontalAlignment::Left;
    UITextVerticalAlignment verticalAlignment = UITextVerticalAlignment::Top;
    UITextWrapMode wrapMode = UITextWrapMode::Word;
    float lineSpacing = 1.0F;
    bool richText = true;
    // When set, the text shows this key's translation in the current language instead of `content`,
    // and follows every language change.
    std::array<char, MaxLocalizationKeyBytes> localizationKey{};
    UITextOverflow overflow = UITextOverflow::Overflow;
    // Shrinks the font, down to `minFontSize`, until the text fits its box.
    bool autoSize = false;
    float minFontSize = 8.0F;
    // 0 shows every line.
    std::uint32_t maxLines = 0U;
    // Extra space between characters, in canvas units.
    float characterSpacing = 0.0F;
};

[[nodiscard]] inline std::string_view UITextLocalizationKey(const UIText& value) noexcept {
    const auto end = std::find(value.localizationKey.begin(), value.localizationKey.end(), '\0');
    return std::string_view{value.localizationKey.data(), static_cast<std::size_t>(end - value.localizationKey.begin())};
}
[[nodiscard]] inline bool SetUITextLocalizationKey(UIText& value, std::string_view key) noexcept {
    if (key.size() >= value.localizationKey.size() || key.find('\0') != std::string_view::npos) return false;
    value.localizationKey.fill('\0');
    std::copy(key.begin(), key.end(), value.localizationKey.begin());
    return true;
}

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
