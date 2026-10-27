#pragma once

#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputText.hpp"
#include "DigitalInputFrame.hpp"
#include <game-activity/GameActivity.h>
#include <array>
#include <algorithm>
#include <span>
#include <string_view>

namespace kb::game {
inline bool QueueAndroidImeDismissal(bool visible, bool requested, bool& previouslyVisible,
    input::InputDeviceState& state, DigitalInputFrame& digital) noexcept {
    const bool dismissed = previouslyVisible && !visible && requested;
    previouslyVisible = visible;
    if (dismissed) {
        static_cast<void>(digital.Queue(state, input::InputKey::Escape, true));
        static_cast<void>(digital.Queue(state, input::InputKey::Escape, false));
    }
    return dismissed;
}

inline bool QueueAndroidEditorAction(int action, input::InputDeviceState& state,
    DigitalInputFrame& digital) noexcept {
    // Android IME_ACTION_GO, SEARCH, SEND, NEXT and DONE submit the field.
    if (action < 2 || action > 6) return false;
    return digital.Queue(state, input::InputKey::Enter, true) &&
        digital.Queue(state, input::InputKey::Enter, false);
}

struct AndroidCommittedTextContext {
    std::array<char32_t, 1024U> text{};
    std::size_t count = 0U;
    std::size_t caret = 0U;
    bool valid = false;
};

inline void ReadAndroidCommittedText(
    void* rawContext,
    const GameTextInputState* textState) noexcept {
    auto* context = static_cast<AndroidCommittedTextContext*>(rawContext);
    if (context != nullptr) context->valid = false;
    if (context == nullptr || textState == nullptr ||
        textState->text_length < 0 || textState->text_length > 65'536 ||
        (textState->text_length > 0 && textState->text_UTF8 == nullptr)) {
        return;
    }
    if (textState->composingRegion.start != SPAN_UNDEFINED ||
        textState->composingRegion.end != SPAN_UNDEFINED) {
        return;
    }

    const std::string_view text{textState->text_UTF8 == nullptr ? "" : textState->text_UTF8,
        static_cast<std::size_t>(textState->text_length)};
    // Current GameTextInput emits standard UTF-8, while older JNI bridges use
    // modified UTF-8 surrogate pairs. Validate either complete representation.
    auto result = kb::input::DecodeUtf8(text, context->text);
    if (!result.wellFormed) result = kb::input::DecodeModifiedUtf8(text, context->text);
    if (!result.wellFormed || result.truncated) return;
    context->count = result.codePointCount;
    // Android selection offsets count UTF-16 code units, not bytes or scalars.
    std::size_t units = 0U;
    context->caret = 0U;
    while (context->caret < context->count && units < static_cast<std::size_t>(std::max(0, textState->selection.end))) {
        units += context->text[context->caret] > 0xFFFFU ? 2U : 1U;
        ++context->caret;
    }
    context->valid = true;
}

// Set only when opening a field. Resetting after each commit restarts the IME
// asynchronously and can accidentally finish the next in-flight composition.
inline void SetAndroidTextInputState(GameActivity* activity, std::span<const char32_t> text,
    std::size_t caret) noexcept {
    if (activity == nullptr || text.size() > 1024U || caret > text.size()) return;
    std::array<char, 6145U> encoded{};
    std::size_t length = 0U;
    int selection = 0;
    const auto append = [&](char32_t unit) {
        if (unit > 0U && unit < 0x80U) encoded[length++] = static_cast<char>(unit);
        else if (unit < 0x800U) {
            encoded[length++] = static_cast<char>(0xC0U | (unit >> 6U));
            encoded[length++] = static_cast<char>(0x80U | (unit & 0x3FU));
        } else {
            encoded[length++] = static_cast<char>(0xE0U | (unit >> 12U));
            encoded[length++] = static_cast<char>(0x80U | ((unit >> 6U) & 0x3FU));
            encoded[length++] = static_cast<char>(0x80U | (unit & 0x3FU));
        }
    };
    for (std::size_t index = 0U; index < text.size(); ++index) {
        const char32_t value = text[index];
        if (!input::IsUnicodeScalar(value)) return;
        if (index < caret) selection += value > 0xFFFFU ? 2 : 1;
        if (value <= 0xFFFFU) append(value);
        else { append(0xD800U + ((value - 0x10000U) >> 10U)); append(0xDC00U + ((value - 0x10000U) & 0x3FFU)); }
    }
    const GameTextInputState state{
        .text_UTF8 = encoded.data(),
        .text_length = static_cast<int>(length),
        .selection = {.start = selection, .end = selection},
        .composingRegion = {
            .start = SPAN_UNDEFINED,
            .end = SPAN_UNDEFINED,
        },
    };
    GameActivity_setTextInputState(activity, &state);
}

} // namespace kb::game
