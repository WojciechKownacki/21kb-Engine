#include "engine/platform/win32/Win32InputCollector.hpp"
#include "engine/platform/win32/Win32ClipboardText.hpp"
#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneUI.hpp"

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
} // namespace kb::input
