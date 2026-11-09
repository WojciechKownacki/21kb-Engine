#pragma once

#include "engine/input/InputText.hpp"
#include "engine/input/InputDeviceState.hpp"
#include "engine/scene/SceneUI.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace kb::game {

inline bool AppendWebCommittedText(input::InputDeviceState& state, std::string_view source) {
    if (source.size() > input::InputDeviceState::kMaxTextInputCodePoints * 4U ||
        source.find('\0') != std::string_view::npos) return false;
    std::array<char32_t, input::InputDeviceState::kMaxTextInputCodePoints> decoded{};
    const auto result = input::DecodeUtf8(source, decoded);
    return result.wellFormed && !result.truncated &&
        state.TryAddTextInput({decoded.data(), result.codePointCount});
}

inline std::optional<std::string> ReadWebClipboardSelection(scene::SceneUIAccess ui, scene::SceneEntity field) {
    if (!field.IsValid() || ui.Focused() != field) return std::nullopt;
    const auto selection = ui.SelectedText();
    if (!selection || selection->empty()) return std::nullopt;
    std::string encoded;
    for (const auto scalar : *selection) {
        std::array<char, 4U> bytes{};
        const auto count = input::EncodeUtf8(scalar, bytes);
        encoded.append(bytes.data(), count);
    }
    return encoded;
}

inline bool QueueWebClipboardText(scene::SceneUIAccess ui, scene::SceneEntity field, std::string_view source) {
    if (!field.IsValid() || ui.Focused() != field || source.size() > 16384U ||
        source.find('\0') != std::string_view::npos) return false;
    std::u32string decoded(std::min(source.size(), scene::UIText::MaxUtf8Bytes * 4U), U'\0');
    const auto result = input::DecodeUtf8(source, decoded);
    if (!result.wellFormed || result.truncated) return false;
    decoded.resize(result.codePointCount);
    return ui.QueueTextReplacement(std::move(decoded));
}

} // namespace kb::game
