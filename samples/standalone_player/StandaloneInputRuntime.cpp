#include "StandaloneInputRuntime.hpp"

#if defined(_WIN32)

#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/input/InputActionAsset.hpp"
#include "engine/input/InputAssetIO.hpp"
#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputMappingContextAsset.hpp"
#include "engine/input/InputModifierDesc.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/input/InputValue.hpp"
#include "engine/platform/win32/Win32InputCollector.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneRuntime.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

constexpr std::string_view kActionPath = "/InputRuntime/Move.21kbinputaction";
constexpr std::string_view kContextPath = "/InputRuntime/Gameplay.21kbinputcontext";

class ScopedInputPackage {
public:
    ScopedInputPackage()
        : root_(std::filesystem::temp_directory_path() /
              ("21kb_input_runtime_" + std::to_string(
                  static_cast<unsigned long long>(GetCurrentProcessId())))) {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
        error.clear();
        std::filesystem::create_directories(root_, error);
        ready_ = !error;
    }

    ~ScopedInputPackage() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    ScopedInputPackage(const ScopedInputPackage&) = delete;
    ScopedInputPackage& operator=(const ScopedInputPackage&) = delete;

    [[nodiscard]] const std::filesystem::path& Root() const noexcept {
        return root_;
    }

    [[nodiscard]] bool Ready() const noexcept {
        return ready_;
    }

private:
    std::filesystem::path root_;
    bool ready_ = false;
};

class ScopedSyntheticKeys {
public:
    [[nodiscard]] bool PressWasdDiagonal() noexcept {
        INPUT events[2]{};
        events[0].type = INPUT_KEYBOARD;
        events[0].ki.wVk = 'W';
        events[1].type = INPUT_KEYBOARD;
        events[1].ki.wVk = 'D';
        pressed_ = SendInput(2U, events, sizeof(INPUT)) == 2U;
        return pressed_;
    }

    ~ScopedSyntheticKeys() {
        if (!pressed_) {
            return;
        }
        INPUT events[2]{};
        events[0].type = INPUT_KEYBOARD;
        events[0].ki.wVk = 'W';
        events[0].ki.dwFlags = KEYEVENTF_KEYUP;
        events[1].type = INPUT_KEYBOARD;
        events[1].ki.wVk = 'D';
        events[1].ki.dwFlags = KEYEVENTF_KEYUP;
        static_cast<void>(SendInput(2U, events, sizeof(INPUT)));
    }

private:
    bool pressed_ = false;
};

void PumpWindowMessages() noexcept {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE) != 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

[[nodiscard]] bool PrepareAssets(
    kb::scene::Scene& scene,
    const std::filesystem::path& packageRoot,
    std::uint64_t& contextId) {
    const kb::input::InputActionAsset action{
        .name = "Move",
        .valueType = kb::input::InputActionValueType::Axis2D,
        .consumeInput = true,
    };
    if (!kb::input::WriteInputAction(packageRoot / "Move.21kbinputaction", action)) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime action_write=failed\n");
        return false;
    }

    kb::assets::AssetManager& assets = scene.Assets().Manager();
    if (!assets.Mounts().Mount("InputRuntime", packageRoot) || assets.DiscoverMountedAssets() != 1U) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime action_discovery=failed\n");
        return false;
    }
    const kb::assets::AssetMetadata* actionMetadata = assets.Registry().FindByPath(kActionPath);
    if (actionMetadata == nullptr) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime action_metadata=missing\n");
        return false;
    }

    kb::input::InputMappingContextAsset context{};
    context.composites.push_back(kb::input::InputCompositeBinding{
        .bindingId = 1U,
        .actionId = actionMetadata->id.value,
        .slots = {
            kb::input::InputCompositeSlot{.key = kb::input::InputKey::A, .axis = 0U, .scale = -1.0F},
            kb::input::InputCompositeSlot{.key = kb::input::InputKey::D, .axis = 0U, .scale = 1.0F},
            kb::input::InputCompositeSlot{.key = kb::input::InputKey::S, .axis = 1U, .scale = -1.0F},
            kb::input::InputCompositeSlot{.key = kb::input::InputKey::W, .axis = 1U, .scale = 1.0F},
        },
        .modifiers = {
            kb::input::InputModifierDesc{
                .type = kb::input::InputModifierType::DeadZone,
                .params = {0.2F, 1.0F, static_cast<float>(kb::input::InputDeadZoneType::Radial), 0.0F},
            },
        },
    });
    if (!kb::input::WriteInputMappingContext(
            packageRoot / "Gameplay.21kbinputcontext", context)) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime context_write=failed\n");
        return false;
    }
    if (assets.DiscoverMountedAssets() != 2U) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime context_discovery=failed\n");
        return false;
    }
    const kb::assets::AssetMetadata* contextMetadata = assets.Registry().FindByPath(kContextPath);
    if (contextMetadata == nullptr) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime context_metadata=missing\n");
        return false;
    }
    contextId = contextMetadata->id.value;
    return scene.Input().AddMappingContext(contextId, 0);
}

} // namespace

bool RunStandaloneInputRuntimeVerification(
    kb::scene::Scene& scene,
    kb::input::Win32InputCollector& collector,
    HWND window) {
    ScopedInputPackage package;
    std::uint64_t contextId = 0U;
    if (!package.Ready() || !PrepareAssets(scene, package.Root(), contextId)) {
        return false;
    }

    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);
    SetForegroundWindow(window);
    SetFocus(window);
    PumpWindowMessages();

    ScopedSyntheticKeys keys;
    if (!keys.PressWasdDiagonal()) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime send_input=failed\n");
        return false;
    }

    bool succeeded = false;
    kb::input::InputValue move{};
    for (std::uint32_t attempt = 0U; attempt < 120U; ++attempt) {
        PumpWindowMessages();
        collector.Collect(scene.Input().MutableDeviceState(), window);
        const bool runtimeUpdated = scene.Runtime().Update(1.0F / 60.0F);
        move = scene.Input().GetActionValue("Move");
        const bool rawW = scene.Input().DeviceState().IsKeyDown(kb::input::InputKey::W);
        const bool rawD = scene.Input().DeviceState().IsKeyDown(kb::input::InputKey::D);
        if (runtimeUpdated && scene.Input().DeviceState().HasFocus() && rawW && rawD &&
            move.x > 0.70F && move.y > 0.70F && std::fabs(move.Magnitude() - 1.0F) < 0.001F) {
            succeeded = true;
            break;
        }
        Sleep(8U);
    }

    std::fprintf(succeeded ? stdout : stderr,
        "kb_standalone_player: input_runtime result=%s source=win32 action_asset=loaded "
        "context_asset=loaded scene_frame=%llu focus=%u raw_w=%u raw_d=%u "
        "move=(%.4f,%.4f) magnitude=%.4f composite=wasd deadzone=radial\n",
        succeeded ? "pass" : "fail",
        static_cast<unsigned long long>(scene.Runtime().FrameIndex()),
        scene.Input().DeviceState().HasFocus() ? 1U : 0U,
        scene.Input().DeviceState().IsKeyDown(kb::input::InputKey::W) ? 1U : 0U,
        scene.Input().DeviceState().IsKeyDown(kb::input::InputKey::D) ? 1U : 0U,
        static_cast<double>(move.x),
        static_cast<double>(move.y),
        static_cast<double>(move.Magnitude()));
    std::fflush(succeeded ? stdout : stderr);
    return succeeded;
}

#endif
