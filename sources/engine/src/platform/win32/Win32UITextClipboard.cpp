#include "engine/platform/win32/Win32InputCollector.hpp"
#include "engine/platform/win32/Win32ClipboardText.hpp"
#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneUI.hpp"

#include <imm.h>
#include <algorithm>
#include <utility>

namespace kb::input {
namespace {
std::string EncodeClipboardText(std::u32string_view text) {
    std::string result;
    for (const auto scalar : text) {
        std::array<char, 4U> encoded{};
        const auto count = EncodeUtf8(scalar, encoded);
        result.append(encoded.data(), count);
    }
    return result;
}
}

void Win32InputCollector::Collect(kb::scene::Scene& scene, HWND ownerWindow) {
    auto& input = scene.Input().MutableDeviceState();
    Collect(input, ownerWindow);
    ApplyTextClipboard(scene, ownerWindow);
}

void Win32InputCollector::ApplyTextClipboard(kb::scene::Scene& scene, HWND ownerWindow) {
    ApplyImeText(scene, ownerWindow);
    if (imeComposing_) return;
    const auto& input = scene.Input().DeviceState();
    const bool control = input.HasFocus() &&
        (input.IsKeyDown(InputKey::LeftControl) || input.IsKeyDown(InputKey::RightControl)) &&
        !input.IsKeyDown(InputKey::LeftAlt) && !input.IsKeyDown(InputKey::RightAlt);
    const bool copyDown = control && input.IsKeyDown(InputKey::C);
    const bool cutDown = control && input.IsKeyDown(InputKey::X);
    const bool pasteDown = control && input.IsKeyDown(InputKey::V);
    const bool copy = copyDown && !copyWasDown_;
    const bool cut = cutDown && !cutWasDown_;
    const bool paste = pasteDown && !pasteWasDown_;
    copyWasDown_ = copyDown;
    cutWasDown_ = cutDown;
    pasteWasDown_ = pasteDown;
    auto ui = scene.UI();
    if (copy || cut) {
        const auto selection = ui.SelectedText();
        if (selection && !selection->empty() && kb::platform::WriteWin32ClipboardText(ownerWindow, EncodeClipboardText(*selection))) {
            if (cut) static_cast<void>(ui.QueueTextReplacement({}));
        }
    } else if (paste && ui.HasFocusedTextInput()) {
        const auto clipboard = kb::platform::ReadWin32ClipboardText(ownerWindow, kb::scene::UIText::MaxUtf8Bytes * 4U);
        if (clipboard) {
            std::u32string decoded(clipboard->size(), U'\0');
            const auto result = DecodeUtf8(*clipboard, decoded);
            if (result.wellFormed && !result.truncated) {
                decoded.resize(result.codePointCount);
                static_cast<void>(ui.QueueTextReplacement(std::move(decoded)));
            }
        }
    }
}

void Win32InputCollector::SynchronizeTextInput(kb::scene::Scene& scene, HWND ownerWindow) {
    const auto field = scene.Input().DeviceState().HasFocus() ? scene.UI().TextInputState() : std::nullopt;
    const std::uint64_t entity = field ? field->entity.Id() : 0U;
    const bool changed = textScene_ != scene.Id() || textEntity_ != entity;
    textScene_ = scene.Id();
    textEntity_ = entity;
    if (changed) {
        if (imeComposing_ && ownerWindow != nullptr) {
            if (const HIMC context = ImmGetContext(ownerWindow)) {
                ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0U);
                ImmReleaseContext(ownerWindow, context);
            }
        }
        imeComposing_ = false;
        pendingImeCount_ = 0U;
        imeEntity_ = 0U;
        imeOverflow_ = false;
        imeDecoder_.Reset();
    }
    if (!field || ownerWindow == nullptr) return;
    const auto& elements = scene.UI().Frame().elements;
    const auto found = std::find_if(elements.begin(), elements.end(), [&](const auto& element) {
        return element.entity == field->entity;
    });
    if (found == elements.end()) return;
    POINT point{static_cast<LONG>(found->rect.x), static_cast<LONG>(found->rect.y + found->rect.height)};
    if (pointerCoordinateWindow_ != nullptr && pointerRenderWidth_ != 0U && pointerRenderHeight_ != 0U) {
        point.x = pointerClientViewport_.left + static_cast<LONG>(static_cast<float>(point.x) *
            static_cast<float>(pointerClientViewport_.right - pointerClientViewport_.left) / static_cast<float>(pointerRenderWidth_));
        point.y = pointerClientViewport_.top + static_cast<LONG>(static_cast<float>(point.y) *
            static_cast<float>(pointerClientViewport_.bottom - pointerClientViewport_.top) / static_cast<float>(pointerRenderHeight_));
        MapWindowPoints(pointerCoordinateWindow_, ownerWindow, &point, 1U);
    }
    if (const HIMC context = ImmGetContext(ownerWindow)) {
        COMPOSITIONFORM composition{};
        composition.dwStyle = CFS_POINT;
        composition.ptCurrentPos = point;
        CANDIDATEFORM candidates{};
        candidates.dwStyle = CFS_CANDIDATEPOS;
        candidates.ptCurrentPos = point;
        ImmSetCompositionWindow(context, &composition);
        ImmSetCandidateWindow(context, &candidates);
        ImmReleaseContext(ownerWindow, context);
    }
}

void Win32InputCollector::ApplyImeText(kb::scene::Scene& scene, HWND ownerWindow) {
    SynchronizeTextInput(scene, ownerWindow);
    auto& input = scene.Input().MutableDeviceState();
    if (imeComposing_ || imeKeysPendingRelease_) {
        bool held = false;
        for (const InputKey key : {InputKey::Enter, InputKey::Escape, InputKey::Tab, InputKey::Space,
                InputKey::Backspace, InputKey::Delete, InputKey::Home, InputKey::End,
                InputKey::ArrowLeft, InputKey::ArrowRight, InputKey::ArrowUp, InputKey::ArrowDown}) {
            held = held || input.IsKeyDown(key);
            input.SetKeyDown(key, false);
        }
        if (!imeComposing_ && !held) imeKeysPendingRelease_ = false;
        input.ClearTextInput();
    }
    if (pendingImeCount_ != 0U) {
        if (!imeOverflow_ && imeScene_ == scene.Id() && imeEntity_ == textEntity_ && textEntity_ != 0U)
            static_cast<void>(scene.UI().QueueTextReplacement({pendingImeText_.data(), pendingImeCount_}));
        pendingImeCount_ = 0U;
        imeOverflow_ = false;
    }
}
} // namespace kb::input
