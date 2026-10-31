#include "GameWindow.hpp"
#include "engine/platform/win32/Win32InputCollector.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneUIComponentSet.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/ui/UIComponentCatalog.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        Require(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != 0,
            "Cannot enable per-monitor DPI awareness");
        kb::input::Win32InputCollector input;
        kb::game::GameWindow window;
        Require(window.Open(L"Responsive UI test", 640U, 480U, false, input, false), "Cannot open hidden host");
        std::uint32_t width{}, height{};
        Require(window.ConsumeResize(width, height) && width == 640U && height == 480U,
            "Requested initial client dimensions changed");
        using namespace kb::scene;
        Scene scene{SceneMode::PrefabPrivate};
        const auto canvas = scene.Entities().CreateObject();
        ApplySceneUIComponents(scene.Components().UI(), canvas.Entity(), BuildUIComponentPreset(UIComponentPreset::Canvas));
        const auto button = scene.Entities().CreateObject(SceneObjectDesc{.parent = canvas});
        auto components = BuildUIComponentPreset(UIComponentPreset::Button);
        components.rectTransform->anchorMin = {.25F, .25F};
        components.rectTransform->anchorMax = {.75F, .75F};
        components.rectTransform->offsetMin = {0.0F, 0.0F};
        components.rectTransform->offsetMax = {0.0F, 0.0F};
        ApplySceneUIComponents(scene.Components().UI(), button.Entity(), components);

        struct Transition { unsigned int dpi; LONG width; LONG height; };
        constexpr std::array transitions{
            Transition{144U, 960, 720}, Transition{192U, 1280, 960},
            Transition{120U, 600, 800}, Transition{96U, 640, 480}};
        for (const auto& transition : transitions) {
            RECT suggested{40, 50, 40 + transition.width, 50 + transition.height};
            // Deliver the real host message in-process so its RECT has the same lifetime
            // and address space as an OS notification. No physical display is changed.
            SendMessageW(window.Handle(), WM_DPICHANGED, MAKELONG(transition.dpi, transition.dpi),
                reinterpret_cast<LPARAM>(&suggested));
            RECT actual{}, client{};
            Require(GetWindowRect(window.Handle(), &actual) != 0 && GetClientRect(window.Handle(), &client) != 0,
                "Cannot read resized window");
            Require(actual.left == suggested.left && actual.top == suggested.top &&
                actual.right == suggested.right && actual.bottom == suggested.bottom,
                "DPI change must apply the suggested window bounds");
            Require(window.ConsumeResize(width, height) && width == static_cast<unsigned int>(client.right) &&
                height == static_cast<unsigned int>(client.bottom), "DPI resize must reach the runtime surface");
            Require(!window.ConsumeResize(width, height), "DPI resize must be consumed exactly once");
            Require(IsWindowVisible(window.Handle()) == 0, "DPI change exposed the hidden host");
            Require(scene.UI().Update(static_cast<float>(width), static_cast<float>(height), {}, .016F),
                "UI failed to update after DPI change");
            Require(scene.UI().HitTest({static_cast<float>(width) * .5F, static_cast<float>(height) * .5F}) == button.Entity(),
                "Anchored control became unreachable after DPI change");
            Require(scene.UI().HitTest({static_cast<float>(width) * .9F, static_cast<float>(height) * .9F}) != button.Entity(),
                "Control hit bounds escaped its anchors after DPI change");
        }
        std::cout << "Hidden native DPI transitions and UI hit testing passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
