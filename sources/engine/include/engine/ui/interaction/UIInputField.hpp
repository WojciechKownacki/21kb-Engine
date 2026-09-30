#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace kb::scene {

enum class UIInputContentType : std::uint8_t {
    Standard,
    IntegerNumber,
    DecimalNumber,
    Alphanumeric,
    EmailAddress,
    // Shown as dots; the text itself stays readable to scripts.
    Password,
    // Digits only, shown as dots.
    Pin,
};

struct UIInputField {
    static constexpr std::string_view StableId = "kb21.ui.input-field";
    static constexpr std::uint32_t SchemaVersion = 2U;
    static constexpr std::size_t MaxPlaceholderUtf8Bytes = 128U;
    std::uint32_t characterLimit = 0U;
    bool multiline = false;
    bool readOnly = false;
    UIInputContentType contentType = UIInputContentType::Standard;
    // Shown dimmed while the field is empty.
    std::array<char, MaxPlaceholderUtf8Bytes> placeholder{};
};

[[nodiscard]] inline std::string_view UIInputPlaceholder(const UIInputField& value) noexcept {
    const auto end = std::find(value.placeholder.begin(), value.placeholder.end(), '\0');
    return std::string_view{value.placeholder.data(), static_cast<std::size_t>(end - value.placeholder.begin())};
}

[[nodiscard]] inline bool SetUIInputPlaceholder(UIInputField& value, std::string_view text) noexcept {
    if (text.size() >= value.placeholder.size() || text.find('\0') != std::string_view::npos) return false;
    value.placeholder.fill('\0');
    std::copy(text.begin(), text.end(), value.placeholder.begin());
    return true;
}

} // namespace kb::scene
