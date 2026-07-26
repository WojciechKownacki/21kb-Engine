#include "StandaloneInputRuntime.hpp"

#if defined(_WIN32)

#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/input/InputActionAsset.hpp"
#include "engine/input/InputAssetIO.hpp"
#include "engine/input/InputContextPriority.hpp"
#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputMappingContextAsset.hpp"
#include "engine/input/InputModifierDesc.hpp"
#include "engine/input/InputRebinding.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/input/InputValue.hpp"
#include "engine/platform/win32/Win32InputCollector.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/InputComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneInputActivation.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptSharedState.hpp"
#include "engine/script/ScriptValue.hpp"

#include <cmath>
#include <cstdio>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr std::string_view kJumpActionPath = "/InputRuntime/Jump.21kbinputaction";
constexpr std::string_view kThrottleActionPath = "/InputRuntime/Throttle.21kbinputaction";
constexpr std::string_view kMoveActionPath = "/InputRuntime/Move.21kbinputaction";
constexpr std::string_view kTouchActionPath = "/InputRuntime/Touch.21kbinputaction";
constexpr std::string_view kUiActionPath = "/InputRuntime/UI.21kbinputaction";
constexpr std::string_view kConsoleActionPath = "/InputRuntime/Console.21kbinputaction";
constexpr std::string_view kDebugActionPath = "/InputRuntime/Debug.21kbinputaction";
constexpr std::string_view kPrimaryContextPath = "/InputRuntime/Primary.21kbinputcontext";
constexpr std::string_view kSecondaryContextPath = "/InputRuntime/Secondary.21kbinputcontext";
constexpr std::string_view kUiContextPath = "/InputRuntime/UI.21kbinputcontext";
constexpr std::string_view kConsoleContextPath = "/InputRuntime/Console.21kbinputcontext";
constexpr std::string_view kDebugContextPath = "/InputRuntime/Debug.21kbinputcontext";
constexpr std::string_view kScriptPath = "/InputRuntime/InputProbe.lua";

struct PriorityRuntimeAssets {
    std::uint64_t primaryContextId = 0U;
    std::uint64_t uiContextId = 0U;
    std::uint64_t consoleContextId = 0U;
    std::uint64_t debugContextId = 0U;
    std::filesystem::path rebindProfilePath;
};

void PumpWindowMessages() noexcept;

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

class ScopedFocusProbe {
public:
    [[nodiscard]] bool Launch() noexcept {
        const std::wstring eventName =
            L"Local\\21kbFocusProbeStop_" +
            std::to_wstring(
                static_cast<unsigned long long>(GetCurrentProcessId()));
        stopEvent_ = CreateEventW(
            nullptr, TRUE, FALSE, eventName.c_str());
        if (stopEvent_ == nullptr) {
            return false;
        }

        std::vector<wchar_t> executable(32768U, L'\0');
        const DWORD length = GetModuleFileNameW(
            nullptr, executable.data(),
            static_cast<DWORD>(executable.size()));
        if (length == 0U || length >= executable.size()) {
            return false;
        }
        const std::wstring command =
            L"\"" + std::wstring{executable.data(), length} +
            L"\" --focus-probe-stop-event=" + eventName;
        std::vector<wchar_t> mutableCommand{
            command.begin(), command.end()};
        mutableCommand.push_back(L'\0');

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (CreateProcessW(
                executable.data(), mutableCommand.data(), nullptr, nullptr,
                FALSE, 0U, nullptr, nullptr, &startup, &process) == 0) {
            return false;
        }
        CloseHandle(process.hThread);
        process_ = process.hProcess;
        processId_ = process.dwProcessId;

        for (std::uint32_t attempt = 0U; attempt < 250U; ++attempt) {
            PumpWindowMessages();
            DWORD foregroundProcess = 0U;
            static_cast<void>(GetWindowThreadProcessId(
                GetForegroundWindow(), &foregroundProcess));
            if (foregroundProcess == processId_) {
                return true;
            }
            if (WaitForSingleObject(process_, 0U) == WAIT_OBJECT_0) {
                return false;
            }
            Sleep(8U);
        }
        return false;
    }

    ~ScopedFocusProbe() {
        if (stopEvent_ != nullptr) {
            static_cast<void>(SetEvent(stopEvent_));
        }
        if (process_ != nullptr) {
            static_cast<void>(WaitForSingleObject(process_, 5000U));
            CloseHandle(process_);
        }
        if (stopEvent_ != nullptr) {
            CloseHandle(stopEvent_);
        }
    }

    ScopedFocusProbe(const ScopedFocusProbe&) = delete;
    ScopedFocusProbe& operator=(const ScopedFocusProbe&) = delete;
    ScopedFocusProbe() = default;

private:
    HANDLE stopEvent_ = nullptr;
    HANDLE process_ = nullptr;
    DWORD processId_ = 0U;
};

class ScopedSyntheticKeys {
public:
    [[nodiscard]] bool PressPrimary() noexcept {
        INPUT events[4]{};
        SetKeyboardEvent(events[0], 'W', false);
        SetKeyboardEvent(events[1], 'D', false);
        SetKeyboardEvent(events[2], VK_SPACE, false);
        SetKeyboardEvent(events[3], 'Q', false);
        primaryPressed_ = SendInput(4U, events, sizeof(INPUT)) == 4U;
        return primaryPressed_;
    }

    [[nodiscard]] bool ReleasePrimary() noexcept {
        if (!primaryPressed_) {
            return true;
        }
        INPUT events[4]{};
        SetKeyboardEvent(events[0], 'W', true);
        SetKeyboardEvent(events[1], 'D', true);
        SetKeyboardEvent(events[2], VK_SPACE, true);
        SetKeyboardEvent(events[3], 'Q', true);
        const bool released = SendInput(4U, events, sizeof(INPUT)) == 4U;
        if (released) {
            primaryPressed_ = false;
        }
        return released;
    }

    [[nodiscard]] bool PressSecondary() noexcept {
        INPUT events[4]{};
        SetKeyboardEvent(events[0], VK_RIGHT, false);
        SetKeyboardEvent(events[1], VK_UP, false);
        SetKeyboardEvent(events[2], VK_RETURN, false);
        SetKeyboardEvent(events[3], VK_F1, false);
        secondaryPressed_ = SendInput(4U, events, sizeof(INPUT)) == 4U;
        return secondaryPressed_;
    }

    [[nodiscard]] bool ReleaseSecondary() noexcept {
        if (!secondaryPressed_) {
            return true;
        }
        INPUT events[4]{};
        SetKeyboardEvent(events[0], VK_RIGHT, true);
        SetKeyboardEvent(events[1], VK_UP, true);
        SetKeyboardEvent(events[2], VK_RETURN, true);
        SetKeyboardEvent(events[3], VK_F1, true);
        const bool released = SendInput(4U, events, sizeof(INPUT)) == 4U;
        if (released) {
            secondaryPressed_ = false;
        }
        return released;
    }

    [[nodiscard]] bool PressPriority() noexcept {
        INPUT event{};
        SetKeyboardEvent(event, VK_ESCAPE, false);
        priorityPressed_ = SendInput(1U, &event, sizeof(INPUT)) == 1U;
        return priorityPressed_;
    }

    [[nodiscard]] bool ReleasePriority() noexcept {
        if (!priorityPressed_) {
            return true;
        }
        INPUT event{};
        SetKeyboardEvent(event, VK_ESCAPE, true);
        const bool released = SendInput(1U, &event, sizeof(INPUT)) == 1U;
        if (released) {
            priorityPressed_ = false;
        }
        return released;
    }

    [[nodiscard]] bool PressRebindKey(WORD key) noexcept {
        if (rebindKeyPressed_) {
            return false;
        }
        INPUT event{};
        SetKeyboardEvent(event, key, false);
        rebindKeyPressed_ =
            SendInput(1U, &event, sizeof(INPUT)) == 1U;
        if (rebindKeyPressed_) {
            rebindKey_ = key;
        }
        return rebindKeyPressed_;
    }

    [[nodiscard]] bool ReleaseRebindKey() noexcept {
        if (!rebindKeyPressed_) {
            return true;
        }
        INPUT event{};
        SetKeyboardEvent(event, rebindKey_, true);
        const bool released =
            SendInput(1U, &event, sizeof(INPUT)) == 1U;
        if (released) {
            rebindKeyPressed_ = false;
            rebindKey_ = 0U;
        }
        return released;
    }

    ~ScopedSyntheticKeys() {
        static_cast<void>(ReleasePrimary());
        static_cast<void>(ReleaseSecondary());
        static_cast<void>(ReleasePriority());
        static_cast<void>(ReleaseRebindKey());
    }

private:
    static void SetKeyboardEvent(INPUT& event, WORD key, bool release) noexcept {
        event.type = INPUT_KEYBOARD;
        event.ki.wVk = key;
        event.ki.dwFlags = release ? KEYEVENTF_KEYUP : 0U;
    }

    bool primaryPressed_ = false;
    bool secondaryPressed_ = false;
    bool priorityPressed_ = false;
    bool rebindKeyPressed_ = false;
    WORD rebindKey_ = 0U;
};

class ScopedSyntheticTouch {
public:
    [[nodiscard]] bool Initialize(HWND window) noexcept {
        window_ = window;
        if (window_ == nullptr || InitializeTouchInjection(1U, TOUCH_FEEDBACK_NONE) == 0) {
            return false;
        }
        return SetPosition(180, 120, POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE |
            POINTER_FLAG_INCONTACT);
    }

    [[nodiscard]] bool Move() noexcept {
        if (!active_) {
            return false;
        }
        return SetPosition(220, 145, POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE |
            POINTER_FLAG_INCONTACT);
    }

    [[nodiscard]] bool Refresh() noexcept {
        if (!active_) {
            return false;
        }
        return SetPosition(clientX_, clientY_, POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE |
            POINTER_FLAG_INCONTACT);
    }

    [[nodiscard]] bool Release() noexcept {
        if (!active_) {
            return true;
        }
        return SetPosition(220, 145, POINTER_FLAG_UP);
    }

    ~ScopedSyntheticTouch() {
        static_cast<void>(Release());
    }

private:
    [[nodiscard]] bool SetPosition(
        LONG clientX, LONG clientY, POINTER_FLAGS flags) noexcept {
        POINT screen{clientX, clientY};
        if (window_ == nullptr || ClientToScreen(window_, &screen) == 0) {
            return false;
        }

        POINTER_TOUCH_INFO touch{};
        touch.pointerInfo.pointerType = PT_TOUCH;
        touch.pointerInfo.pointerId = 0U;
        touch.pointerInfo.ptPixelLocation = screen;
        touch.pointerInfo.pointerFlags = flags;
        touch.touchFlags = TOUCH_FLAG_NONE;
        touch.touchMask =
            TOUCH_MASK_CONTACTAREA | TOUCH_MASK_ORIENTATION | TOUCH_MASK_PRESSURE;
        touch.rcContact = RECT{
            screen.x - 2,
            screen.y - 2,
            screen.x + 2,
            screen.y + 2,
        };
        touch.orientation = 90U;
        touch.pressure = 32000U;
        const bool injected = InjectTouchInput(1U, &touch) != 0;
        if (injected) {
            active_ = (flags & POINTER_FLAG_UP) == 0U;
            clientX_ = clientX;
            clientY_ = clientY;
        }
        return injected;
    }

    HWND window_ = nullptr;
    bool active_ = false;
    LONG clientX_ = 0;
    LONG clientY_ = 0;
};

class ScopedSyntheticPointer {
public:
    explicit ScopedSyntheticPointer(HWND window) noexcept
        : window_(window) {}

    [[nodiscard]] bool InjectFrame(
        LONG clientX, LONG clientY, bool buttonDown, LONG wheelDelta) noexcept {
        POINT screen{clientX, clientY};
        if (window_ == nullptr || ClientToScreen(window_, &screen) == 0 ||
            SetCursorPos(screen.x, screen.y) == 0) {
            return false;
        }

        INPUT events[2]{};
        UINT count = 0U;
        if (buttonDown != buttonDown_) {
            events[count].type = INPUT_MOUSE;
            events[count].mi.dwFlags =
                buttonDown ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            ++count;
        }
        if (wheelDelta != 0) {
            events[count].type = INPUT_MOUSE;
            events[count].mi.dwFlags = MOUSEEVENTF_WHEEL;
            events[count].mi.mouseData = static_cast<DWORD>(wheelDelta);
            ++count;
        }
        if (count > 0U && SendInput(count, events, sizeof(INPUT)) != count) {
            return false;
        }
        buttonDown_ = buttonDown;
        return true;
    }

    [[nodiscard]] bool Release() noexcept {
        if (!buttonDown_) {
            return true;
        }
        INPUT event{};
        event.type = INPUT_MOUSE;
        event.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        const bool released = SendInput(1U, &event, sizeof(INPUT)) == 1U;
        if (released) {
            buttonDown_ = false;
        }
        return released;
    }

    ~ScopedSyntheticPointer() {
        static_cast<void>(Release());
    }

private:
    HWND window_ = nullptr;
    bool buttonDown_ = false;
};

void PumpWindowMessages() noexcept {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE) != 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

[[nodiscard]] bool ActivateWindowForInput(HWND window) noexcept {
    const DWORD currentThread = GetCurrentThreadId();
    const HWND previousForeground = GetForegroundWindow();
    const DWORD foregroundThread = previousForeground != nullptr
        ? GetWindowThreadProcessId(previousForeground, nullptr)
        : 0U;
    const bool attached = foregroundThread != 0U && foregroundThread != currentThread &&
        AttachThreadInput(currentThread, foregroundThread, TRUE) != 0;

    ShowWindow(window, SW_SHOWNORMAL);
    static_cast<void>(BringWindowToTop(window));
    static_cast<void>(SetForegroundWindow(window));
    static_cast<void>(SetActiveWindow(window));
    SetFocus(window);

    if (attached) {
        static_cast<void>(AttachThreadInput(currentThread, foregroundThread, FALSE));
    }
    PumpWindowMessages();
    return GetForegroundWindow() == window && GetFocus() == window;
}

[[nodiscard]] bool PrepareAssets(
    kb::scene::Scene& scene,
    const std::filesystem::path& packageRoot,
    PriorityRuntimeAssets& priorityAssets) {
    const kb::input::InputActionAsset jumpAction{
        .name = "Jump",
        .valueType = kb::input::InputActionValueType::Bool,
        .consumeInput = true,
    };
    const kb::input::InputActionAsset throttleAction{
        .name = "Throttle",
        .valueType = kb::input::InputActionValueType::Axis1D,
        .consumeInput = true,
    };
    const kb::input::InputActionAsset moveAction{
        .name = "Move",
        .valueType = kb::input::InputActionValueType::Axis2D,
        .consumeInput = true,
    };
    const kb::input::InputActionAsset touchAction{
        .name = "Touch",
        .valueType = kb::input::InputActionValueType::Bool,
        .consumeInput = true,
    };
    const kb::input::InputActionAsset uiAction{
        .name = "UIContext",
        .valueType = kb::input::InputActionValueType::Bool,
        .consumeInput = true,
    };
    const kb::input::InputActionAsset consoleAction{
        .name = "ConsoleContext",
        .valueType = kb::input::InputActionValueType::Bool,
        .consumeInput = true,
    };
    const kb::input::InputActionAsset debugAction{
        .name = "DebugContext",
        .valueType = kb::input::InputActionValueType::Bool,
        .consumeInput = true,
    };
    if (!kb::input::WriteInputAction(packageRoot / "Jump.21kbinputaction", jumpAction) ||
        !kb::input::WriteInputAction(packageRoot / "Throttle.21kbinputaction", throttleAction) ||
        !kb::input::WriteInputAction(packageRoot / "Move.21kbinputaction", moveAction) ||
        !kb::input::WriteInputAction(packageRoot / "Touch.21kbinputaction", touchAction) ||
        !kb::input::WriteInputAction(packageRoot / "UI.21kbinputaction", uiAction) ||
        !kb::input::WriteInputAction(packageRoot / "Console.21kbinputaction", consoleAction) ||
        !kb::input::WriteInputAction(packageRoot / "Debug.21kbinputaction", debugAction)) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime action_writes=failed\n");
        return false;
    }

    std::ofstream script{packageRoot / "InputProbe.lua", std::ios::trunc};
    script << R"(
function Tick(self, dt)
    SetShared("input.tick", (GetShared("input.tick") or 0) + 1)
    local rebindCommand = GetShared("input.rebind.command")
    if rebindCommand and rebindCommand ~= "" then
        local context = GetShared("input.rebind.context")
        local profile = GetShared("input.rebind.profile")
        if rebindCommand == "conflict" then
            local result = Input.Rebind(context, 1, "Q")
            SetShared("input.rebind.applied", result.applied)
            SetShared("input.rebind.conflict", result.conflict)
        elseif rebindCommand == "apply" then
            local result = Input.Rebind(context, 1, "R")
            SetShared("input.rebind.applied", result.applied)
            SetShared("input.rebind.conflict", result.conflict)
        elseif rebindCommand == "save" then
            local result = Input.SaveRebindProfile(context, profile)
            SetShared("input.rebind.saved", result.saved)
            SetShared("input.rebind.error", result.error)
        elseif rebindCommand == "restore" then
            local result = Input.Rebind(context, 1, "Space")
            SetShared("input.rebind.applied", result.applied)
            SetShared("input.rebind.conflict", result.conflict)
        elseif rebindCommand == "load" then
            local result = Input.LoadRebindProfile(context, profile)
            SetShared("input.rebind.loaded", result.loaded)
            SetShared("input.rebind.error", result.error)
        end
        SetShared("input.rebind.command", "")
    end
    local function Capture(prefix, player)
        local move = Input.Action2D("Move", player)
        SetShared("input." .. prefix .. ".actionBool", Input.ActionBool("Jump", player))
        SetShared("input." .. prefix .. ".actionFloat", Input.ActionFloat("Throttle", player))
        SetShared("input." .. prefix .. ".action2DX", move.x)
        SetShared("input." .. prefix .. ".action2DY", move.y)
        SetShared("input." .. prefix .. ".pressed", Input.Pressed("Jump", player))
        SetShared("input." .. prefix .. ".released", Input.Released("Jump", player))
        SetShared("input." .. prefix .. ".held", Input.Held("Jump", player))
    end
    Capture("p1", 0)
    Capture("p2", 2)
    SetShared("input.touch.actionBool", Input.ActionBool("Touch", 0))
    SetShared("input.touch.pressed", Input.Pressed("Touch", 0))
    SetShared("input.touch.released", Input.Released("Touch", 0))
    SetShared("input.touch.held", Input.Held("Touch", 0))
    local pointerPosition = Pointer.Position()
    local pointerDelta = Pointer.Delta()
    local pointerRay = Pointer.Ray()
    SetShared("input.pointer.x", pointerPosition.x)
    SetShared("input.pointer.y", pointerPosition.y)
    SetShared("input.pointer.dx", pointerDelta.x)
    SetShared("input.pointer.dy", pointerDelta.y)
    SetShared("input.pointer.left", Pointer.Button(0))
    SetShared("input.pointer.scroll", Pointer.Scroll())
    SetShared("input.pointer.rayValid", pointerRay.valid)
    SetShared("input.pointer.originX", pointerRay.originX)
    SetShared("input.pointer.originY", pointerRay.originY)
    SetShared("input.pointer.originZ", pointerRay.originZ)
    SetShared("input.pointer.directionX", pointerRay.directionX)
    SetShared("input.pointer.directionY", pointerRay.directionY)
    SetShared("input.pointer.directionZ", pointerRay.directionZ)
    SetShared("input.priority.gameplayActive", Input.ActionBool("Jump", 0))
    SetShared("input.priority.uiActive", Input.ActionBool("UIContext", 0))
    SetShared("input.priority.consoleActive", Input.ActionBool("ConsoleContext", 0))
    SetShared("input.priority.debugActive", Input.ActionBool("DebugContext", 0))
    SetShared("input.priority.gameplayValue", Input.PriorityGameplay())
    SetShared("input.priority.uiValue", Input.PriorityUI())
    SetShared("input.priority.consoleValue", Input.PriorityConsole())
    SetShared("input.priority.debugValue", Input.PriorityDebugOverlay())
    SetShared("input.status.focus", Input.HasFocus())
    SetShared("input.status.gamepad0", Input.IsGamepadConnected(0))
    SetShared("input.status.gamepad1", Input.IsGamepadConnected(1))
    SetShared("input.status.gamepad2", Input.IsGamepadConnected(2))
    SetShared("input.status.gamepad3", Input.IsGamepadConnected(3))
end
)";
    script.close();
    if (!script) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime lua_write=failed\n");
        return false;
    }

    kb::assets::AssetManager& assets = scene.Assets().Manager();
    if (!assets.Mounts().Mount("InputRuntime", packageRoot) || assets.DiscoverMountedAssets() != 8U) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime initial_asset_discovery=failed\n");
        return false;
    }
    const kb::assets::AssetMetadata* jumpMetadata = assets.Registry().FindByPath(kJumpActionPath);
    const kb::assets::AssetMetadata* throttleMetadata = assets.Registry().FindByPath(kThrottleActionPath);
    const kb::assets::AssetMetadata* moveMetadata = assets.Registry().FindByPath(kMoveActionPath);
    const kb::assets::AssetMetadata* touchMetadata = assets.Registry().FindByPath(kTouchActionPath);
    const kb::assets::AssetMetadata* uiMetadata = assets.Registry().FindByPath(kUiActionPath);
    const kb::assets::AssetMetadata* consoleMetadata = assets.Registry().FindByPath(kConsoleActionPath);
    const kb::assets::AssetMetadata* debugMetadata = assets.Registry().FindByPath(kDebugActionPath);
    const kb::assets::AssetMetadata* scriptMetadata = assets.Registry().FindByPath(kScriptPath);
    if (jumpMetadata == nullptr || throttleMetadata == nullptr ||
        moveMetadata == nullptr || touchMetadata == nullptr ||
        uiMetadata == nullptr || consoleMetadata == nullptr ||
        debugMetadata == nullptr || scriptMetadata == nullptr) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime initial_asset_metadata=missing\n");
        return false;
    }
    const std::uint64_t scriptAssetId = scriptMetadata->id.value;
    kb::input::InputMappingContextAsset primaryContext{};
    primaryContext.mappings.push_back(kb::input::InputKeyMapping{
        .bindingId = 1U,
        .actionId = jumpMetadata->id.value,
        .key = kb::input::InputKey::Space,
    });
    primaryContext.mappings.push_back(kb::input::InputKeyMapping{
        .bindingId = 2U,
        .actionId = throttleMetadata->id.value,
        .key = kb::input::InputKey::Q,
    });
    primaryContext.mappings.push_back(kb::input::InputKeyMapping{
        .bindingId = 4U,
        .actionId = touchMetadata->id.value,
        .key = kb::input::InputKey::TouchDown,
    });
    primaryContext.mappings.push_back(kb::input::InputKeyMapping{
        .bindingId = 5U,
        .actionId = jumpMetadata->id.value,
        .key = kb::input::InputKey::Escape,
    });
    primaryContext.composites.push_back(kb::input::InputCompositeBinding{
        .bindingId = 3U,
        .actionId = moveMetadata->id.value,
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

    kb::input::InputMappingContextAsset secondaryContext{};
    secondaryContext.mappings.push_back(kb::input::InputKeyMapping{
        .bindingId = 11U,
        .actionId = jumpMetadata->id.value,
        .key = kb::input::InputKey::Enter,
    });
    secondaryContext.mappings.push_back(kb::input::InputKeyMapping{
        .bindingId = 12U,
        .actionId = throttleMetadata->id.value,
        .key = kb::input::InputKey::F1,
    });
    secondaryContext.composites.push_back(kb::input::InputCompositeBinding{
        .bindingId = 13U,
        .actionId = moveMetadata->id.value,
        .slots = {
            kb::input::InputCompositeSlot{.key = kb::input::InputKey::ArrowLeft, .axis = 0U, .scale = -1.0F},
            kb::input::InputCompositeSlot{.key = kb::input::InputKey::ArrowRight, .axis = 0U, .scale = 1.0F},
            kb::input::InputCompositeSlot{.key = kb::input::InputKey::ArrowDown, .axis = 1U, .scale = -1.0F},
            kb::input::InputCompositeSlot{.key = kb::input::InputKey::ArrowUp, .axis = 1U, .scale = 1.0F},
        },
        .modifiers = {
            kb::input::InputModifierDesc{
                .type = kb::input::InputModifierType::DeadZone,
                .params = {0.2F, 1.0F, static_cast<float>(kb::input::InputDeadZoneType::Radial), 0.0F},
            },
        },
    });

    kb::input::InputMappingContextAsset uiContext{};
    uiContext.mappings.push_back(kb::input::InputKeyMapping{
        .bindingId = 21U,
        .actionId = uiMetadata->id.value,
        .key = kb::input::InputKey::Escape,
    });
    kb::input::InputMappingContextAsset consoleContext{};
    consoleContext.mappings.push_back(kb::input::InputKeyMapping{
        .bindingId = 31U,
        .actionId = consoleMetadata->id.value,
        .key = kb::input::InputKey::Escape,
    });
    kb::input::InputMappingContextAsset debugContext{};
    debugContext.mappings.push_back(kb::input::InputKeyMapping{
        .bindingId = 41U,
        .actionId = debugMetadata->id.value,
        .key = kb::input::InputKey::Escape,
    });
    if (!kb::input::WriteInputMappingContext(
            packageRoot / "Primary.21kbinputcontext", primaryContext) ||
        !kb::input::WriteInputMappingContext(
            packageRoot / "Secondary.21kbinputcontext", secondaryContext) ||
        !kb::input::WriteInputMappingContext(
            packageRoot / "UI.21kbinputcontext", uiContext) ||
        !kb::input::WriteInputMappingContext(
            packageRoot / "Console.21kbinputcontext", consoleContext) ||
        !kb::input::WriteInputMappingContext(
            packageRoot / "Debug.21kbinputcontext", debugContext)) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime context_writes=failed\n");
        return false;
    }
    if (assets.DiscoverMountedAssets() != 13U) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime context_discovery=failed\n");
        return false;
    }
    const kb::assets::AssetMetadata* primaryMetadata =
        assets.Registry().FindByPath(kPrimaryContextPath);
    const kb::assets::AssetMetadata* secondaryMetadata =
        assets.Registry().FindByPath(kSecondaryContextPath);
    const kb::assets::AssetMetadata* uiContextMetadata =
        assets.Registry().FindByPath(kUiContextPath);
    const kb::assets::AssetMetadata* consoleContextMetadata =
        assets.Registry().FindByPath(kConsoleContextPath);
    const kb::assets::AssetMetadata* debugContextMetadata =
        assets.Registry().FindByPath(kDebugContextPath);
    if (primaryMetadata == nullptr || secondaryMetadata == nullptr ||
        uiContextMetadata == nullptr || consoleContextMetadata == nullptr ||
        debugContextMetadata == nullptr) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime context_metadata=missing\n");
        return false;
    }

    const kb::scene::SceneObject primaryInput =
        scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "Primary Local User Input"});
    scene.Components().Inputs().Set(primaryInput.Entity(), kb::scene::InputComponent{
        .mappingContextAssetId = primaryMetadata->id.value,
        .priority = 0,
        .enabled = true,
        .localUser = kb::input::kPrimaryLocalUser,
    });
    const kb::scene::SceneObject secondaryInput =
        scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "Secondary Local User Input"});
    scene.Components().Inputs().Set(secondaryInput.Entity(), kb::scene::InputComponent{
        .mappingContextAssetId = secondaryMetadata->id.value,
        .priority = 0,
        .enabled = true,
        .localUser = kb::input::LocalUserId{2U},
    });
    const auto addPriorityInput = [&](std::string_view name,
                                      std::uint64_t contextId,
                                      std::int32_t priority) {
        const kb::scene::SceneObject object =
            scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
                .name = std::string{name},
            });
        scene.Components().Inputs().Set(object.Entity(), kb::scene::InputComponent{
            .mappingContextAssetId = contextId,
            .priority = priority,
            .enabled = true,
            .localUser = kb::input::kPrimaryLocalUser,
        });
    };
    addPriorityInput(
        "UI Input Context",
        uiContextMetadata->id.value,
        kb::input::InputContextPriority::UI);
    addPriorityInput(
        "Console Input Context",
        consoleContextMetadata->id.value,
        kb::input::InputContextPriority::Console);
    addPriorityInput(
        "Debug Overlay Input Context",
        debugContextMetadata->id.value,
        kb::input::InputContextPriority::DebugOverlay);
    kb::scene::SceneInputActivation::Apply(scene);
    const kb::input::InputSubsystem* player2 =
        scene.TryGetInput(kb::input::LocalUserId{2U});
    if (!scene.Input().HasMappingContext(primaryMetadata->id.value) ||
        scene.Input().HasMappingContext(secondaryMetadata->id.value) ||
        !scene.Input().HasMappingContext(uiContextMetadata->id.value) ||
        !scene.Input().HasMappingContext(consoleContextMetadata->id.value) ||
        !scene.Input().HasMappingContext(debugContextMetadata->id.value) ||
        player2 == nullptr ||
        !player2->HasMappingContext(secondaryMetadata->id.value) ||
        player2->HasMappingContext(primaryMetadata->id.value)) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime local_user_activation=failed\n");
        return false;
    }
    priorityAssets = PriorityRuntimeAssets{
        .primaryContextId = primaryMetadata->id.value,
        .uiContextId = uiContextMetadata->id.value,
        .consoleContextId = consoleContextMetadata->id.value,
        .debugContextId = debugContextMetadata->id.value,
        .rebindProfilePath = packageRoot / "UserInput.21kbrebind",
    };

    const kb::scene::SceneObject scriptObject =
        scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "Input Runtime Lua Probe"});
    scene.Components().Behaviours().Set(scriptObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = scriptAssetId,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
        .tickGroup = kb::scene::BehaviourTickGroup::Input,
        .executionOrder = 0,
    });
    return true;
}

struct PlayerInputSnapshot {
    bool actionBool = false;
    float actionFloat = 0.0F;
    float action2DX = 0.0F;
    float action2DY = 0.0F;
    bool pressed = false;
    bool released = false;
    bool held = false;
};

struct ScriptInputSnapshot {
    int tick = 0;
    PlayerInputSnapshot player1{};
    PlayerInputSnapshot player2{};
    bool touchActionBool = false;
    bool touchPressed = false;
    bool touchReleased = false;
    bool touchHeld = false;
    float pointerX = 0.0F;
    float pointerY = 0.0F;
    float pointerDeltaX = 0.0F;
    float pointerDeltaY = 0.0F;
    bool pointerLeft = false;
    float pointerScroll = 0.0F;
    bool pointerRayValid = false;
    float pointerOriginX = 0.0F;
    float pointerOriginY = 0.0F;
    float pointerOriginZ = 0.0F;
    float pointerDirectionX = 0.0F;
    float pointerDirectionY = 0.0F;
    float pointerDirectionZ = 0.0F;
    bool priorityGameplayActive = false;
    bool priorityUiActive = false;
    bool priorityConsoleActive = false;
    bool priorityDebugActive = false;
    int priorityGameplayValue = 0;
    int priorityUiValue = 0;
    int priorityConsoleValue = 0;
    int priorityDebugValue = 0;
    bool hasFocus = false;
    std::array<bool, kb::input::InputDeviceState::kMaxGamepads>
        gamepadConnected{};
};

[[nodiscard]] std::optional<PlayerInputSnapshot> ReadPlayerSnapshot(
    const kb::script::ScriptSharedState& shared,
    std::string_view player) {
    const std::string prefix = "input." + std::string{player} + ".";
    const std::optional<kb::script::ScriptValue> actionBool = shared.Get(prefix + "actionBool");
    const std::optional<kb::script::ScriptValue> actionFloat = shared.Get(prefix + "actionFloat");
    const std::optional<kb::script::ScriptValue> action2DX = shared.Get(prefix + "action2DX");
    const std::optional<kb::script::ScriptValue> action2DY = shared.Get(prefix + "action2DY");
    const std::optional<kb::script::ScriptValue> pressed = shared.Get(prefix + "pressed");
    const std::optional<kb::script::ScriptValue> released = shared.Get(prefix + "released");
    const std::optional<kb::script::ScriptValue> held = shared.Get(prefix + "held");
    if (!actionBool || !actionFloat || !action2DX || !action2DY ||
        !pressed || !released || !held) {
        return std::nullopt;
    }
    return PlayerInputSnapshot{
        .actionBool = actionBool->AsBool(),
        .actionFloat = actionFloat->AsFloat(),
        .action2DX = action2DX->AsFloat(),
        .action2DY = action2DY->AsFloat(),
        .pressed = pressed->AsBool(),
        .released = released->AsBool(),
        .held = held->AsBool(),
    };
}

[[nodiscard]] std::optional<ScriptInputSnapshot> ReadScriptSnapshot(
    const kb::script::ScriptSharedState& shared) {
    const std::optional<kb::script::ScriptValue> tick = shared.Get("input.tick");
    const std::optional<PlayerInputSnapshot> player1 = ReadPlayerSnapshot(shared, "p1");
    const std::optional<PlayerInputSnapshot> player2 = ReadPlayerSnapshot(shared, "p2");
    const std::optional<kb::script::ScriptValue> touchActionBool =
        shared.Get("input.touch.actionBool");
    const std::optional<kb::script::ScriptValue> touchPressed =
        shared.Get("input.touch.pressed");
    const std::optional<kb::script::ScriptValue> touchReleased =
        shared.Get("input.touch.released");
    const std::optional<kb::script::ScriptValue> touchHeld =
        shared.Get("input.touch.held");
    const std::optional<kb::script::ScriptValue> pointerX = shared.Get("input.pointer.x");
    const std::optional<kb::script::ScriptValue> pointerY = shared.Get("input.pointer.y");
    const std::optional<kb::script::ScriptValue> pointerDeltaX = shared.Get("input.pointer.dx");
    const std::optional<kb::script::ScriptValue> pointerDeltaY = shared.Get("input.pointer.dy");
    const std::optional<kb::script::ScriptValue> pointerLeft = shared.Get("input.pointer.left");
    const std::optional<kb::script::ScriptValue> pointerScroll = shared.Get("input.pointer.scroll");
    const std::optional<kb::script::ScriptValue> pointerRayValid = shared.Get("input.pointer.rayValid");
    const std::optional<kb::script::ScriptValue> pointerOriginX = shared.Get("input.pointer.originX");
    const std::optional<kb::script::ScriptValue> pointerOriginY = shared.Get("input.pointer.originY");
    const std::optional<kb::script::ScriptValue> pointerOriginZ = shared.Get("input.pointer.originZ");
    const std::optional<kb::script::ScriptValue> pointerDirectionX = shared.Get("input.pointer.directionX");
    const std::optional<kb::script::ScriptValue> pointerDirectionY = shared.Get("input.pointer.directionY");
    const std::optional<kb::script::ScriptValue> pointerDirectionZ = shared.Get("input.pointer.directionZ");
    const std::optional<kb::script::ScriptValue> priorityGameplayActive =
        shared.Get("input.priority.gameplayActive");
    const std::optional<kb::script::ScriptValue> priorityUiActive =
        shared.Get("input.priority.uiActive");
    const std::optional<kb::script::ScriptValue> priorityConsoleActive =
        shared.Get("input.priority.consoleActive");
    const std::optional<kb::script::ScriptValue> priorityDebugActive =
        shared.Get("input.priority.debugActive");
    const std::optional<kb::script::ScriptValue> priorityGameplayValue =
        shared.Get("input.priority.gameplayValue");
    const std::optional<kb::script::ScriptValue> priorityUiValue =
        shared.Get("input.priority.uiValue");
    const std::optional<kb::script::ScriptValue> priorityConsoleValue =
        shared.Get("input.priority.consoleValue");
    const std::optional<kb::script::ScriptValue> priorityDebugValue =
        shared.Get("input.priority.debugValue");
    const std::optional<kb::script::ScriptValue> hasFocus =
        shared.Get("input.status.focus");
    const std::optional<kb::script::ScriptValue> gamepad0 =
        shared.Get("input.status.gamepad0");
    const std::optional<kb::script::ScriptValue> gamepad1 =
        shared.Get("input.status.gamepad1");
    const std::optional<kb::script::ScriptValue> gamepad2 =
        shared.Get("input.status.gamepad2");
    const std::optional<kb::script::ScriptValue> gamepad3 =
        shared.Get("input.status.gamepad3");
    if (!tick || !player1 || !player2 || !touchActionBool ||
        !touchPressed || !touchReleased || !touchHeld ||
        !pointerX || !pointerY || !pointerDeltaX || !pointerDeltaY ||
        !pointerLeft || !pointerScroll || !pointerRayValid ||
        !pointerOriginX || !pointerOriginY || !pointerOriginZ ||
        !pointerDirectionX || !pointerDirectionY || !pointerDirectionZ ||
        !priorityGameplayActive || !priorityUiActive ||
        !priorityConsoleActive || !priorityDebugActive ||
        !priorityGameplayValue || !priorityUiValue ||
        !priorityConsoleValue || !priorityDebugValue || !hasFocus ||
        !gamepad0 || !gamepad1 || !gamepad2 || !gamepad3) {
        return std::nullopt;
    }
    return ScriptInputSnapshot{
        .tick = tick->AsInt(),
        .player1 = *player1,
        .player2 = *player2,
        .touchActionBool = touchActionBool->AsBool(),
        .touchPressed = touchPressed->AsBool(),
        .touchReleased = touchReleased->AsBool(),
        .touchHeld = touchHeld->AsBool(),
        .pointerX = pointerX->AsFloat(),
        .pointerY = pointerY->AsFloat(),
        .pointerDeltaX = pointerDeltaX->AsFloat(),
        .pointerDeltaY = pointerDeltaY->AsFloat(),
        .pointerLeft = pointerLeft->AsBool(),
        .pointerScroll = pointerScroll->AsFloat(),
        .pointerRayValid = pointerRayValid->AsBool(),
        .pointerOriginX = pointerOriginX->AsFloat(),
        .pointerOriginY = pointerOriginY->AsFloat(),
        .pointerOriginZ = pointerOriginZ->AsFloat(),
        .pointerDirectionX = pointerDirectionX->AsFloat(),
        .pointerDirectionY = pointerDirectionY->AsFloat(),
        .pointerDirectionZ = pointerDirectionZ->AsFloat(),
        .priorityGameplayActive = priorityGameplayActive->AsBool(),
        .priorityUiActive = priorityUiActive->AsBool(),
        .priorityConsoleActive = priorityConsoleActive->AsBool(),
        .priorityDebugActive = priorityDebugActive->AsBool(),
        .priorityGameplayValue = priorityGameplayValue->AsInt(),
        .priorityUiValue = priorityUiValue->AsInt(),
        .priorityConsoleValue = priorityConsoleValue->AsInt(),
        .priorityDebugValue = priorityDebugValue->AsInt(),
        .hasFocus = hasFocus->AsBool(),
        .gamepadConnected = {
            gamepad0->AsBool(),
            gamepad1->AsBool(),
            gamepad2->AsBool(),
            gamepad3->AsBool(),
        },
    };
}

[[nodiscard]] bool WaitForTouchState(
    kb::scene::Scene& scene,
    kb::input::Win32InputCollector& collector,
    HWND window,
    bool down,
    kb::input::InputTouchPhase phase,
    float expectedX,
    float expectedY,
    ScopedSyntheticTouch* activeTouch = nullptr) {
    std::size_t lastCount = 0U;
    bool lastDown = false;
    kb::input::InputTouchPhase lastPhase = kb::input::InputTouchPhase::Ended;
    for (std::uint32_t attempt = 0U; attempt < 120U; ++attempt) {
        PumpWindowMessages();
        collector.Collect(scene.Input().MutableDeviceState(), window);
        const kb::input::InputDeviceState& device = scene.Input().DeviceState();
        const std::span<const kb::input::InputTouchPoint> points = device.TouchPoints();
        lastCount = points.size();
        lastDown = device.IsKeyDown(kb::input::InputKey::TouchDown);
        if (!points.empty()) {
            lastPhase = points.front().phase;
        }
        if (device.HasFocus() &&
            device.IsKeyDown(kb::input::InputKey::TouchDown) == down &&
            points.size() == 1U &&
            std::fabs(points.front().x - expectedX) < 1.0F &&
            std::fabs(points.front().y - expectedY) < 1.0F &&
            (points.front().phase == phase ||
                (down && phase == kb::input::InputTouchPhase::Began &&
                    points.front().phase == kb::input::InputTouchPhase::Moved))) {
            return true;
        }
        if (activeTouch != nullptr && attempt % 4U == 3U &&
            !activeTouch->Refresh()) {
            return false;
        }
        Sleep(8U);
    }
    std::fprintf(stderr,
        "kb_standalone_player: input_runtime touch_wait_timeout expected_down=%u "
        "expected_phase=%u actual_down=%u actual_count=%llu actual_phase=%u\n",
        down ? 1U : 0U,
        static_cast<unsigned>(phase),
        lastDown ? 1U : 0U,
        static_cast<unsigned long long>(lastCount),
        static_cast<unsigned>(lastPhase));
    return false;
}

[[nodiscard]] bool CollectPointerFrame(
    kb::scene::Scene& scene,
    kb::input::Win32InputCollector& collector,
    HWND window,
    ScopedSyntheticPointer& pointer,
    LONG clientX,
    LONG clientY,
    float expectedX,
    float expectedY,
    bool buttonDown,
    LONG wheelDelta) {
    if (!pointer.InjectFrame(clientX, clientY, buttonDown, wheelDelta)) {
        return false;
    }
    for (std::uint32_t attempt = 0U; attempt < 8U; ++attempt) {
        Sleep(4U);
        PumpWindowMessages();
    }
    collector.Collect(scene.Input().MutableDeviceState(), window);
    const kb::input::InputDeviceState& device = scene.Input().DeviceState();
    const float expectedWheel =
        static_cast<float>(wheelDelta) / static_cast<float>(WHEEL_DELTA);
    return device.HasFocus() &&
        std::fabs(device.PointerX() - expectedX) < 1.0F &&
        std::fabs(device.PointerY() - expectedY) < 1.0F &&
        device.IsKeyDown(kb::input::InputKey::MouseLeft) == buttonDown &&
        std::fabs(device.GetValue(kb::input::InputKey::MouseWheel) - expectedWheel) < 0.001F;
}

[[nodiscard]] bool WaitForPhysicalState(
    kb::scene::Scene& scene,
    kb::input::Win32InputCollector& collector,
    HWND window,
    bool primaryDown,
    bool secondaryDown) {
    for (std::uint32_t attempt = 0U; attempt < 120U; ++attempt) {
        PumpWindowMessages();
        collector.Collect(scene.Input().MutableDeviceState(), window);
        const kb::input::InputDeviceState& device = scene.Input().DeviceState();
        const bool matches = device.HasFocus() &&
            device.IsKeyDown(kb::input::InputKey::W) == primaryDown &&
            device.IsKeyDown(kb::input::InputKey::D) == primaryDown &&
            device.IsKeyDown(kb::input::InputKey::Space) == primaryDown &&
            device.IsKeyDown(kb::input::InputKey::Q) == primaryDown &&
            device.IsKeyDown(kb::input::InputKey::ArrowRight) == secondaryDown &&
            device.IsKeyDown(kb::input::InputKey::ArrowUp) == secondaryDown &&
            device.IsKeyDown(kb::input::InputKey::Enter) == secondaryDown &&
            device.IsKeyDown(kb::input::InputKey::F1) == secondaryDown;
        if (matches) {
            return true;
        }
        Sleep(8U);
    }
    return false;
}

[[nodiscard]] bool WaitForPriorityKey(
    kb::scene::Scene& scene,
    kb::input::Win32InputCollector& collector,
    HWND window,
    bool down) {
    for (std::uint32_t attempt = 0U; attempt < 120U; ++attempt) {
        PumpWindowMessages();
        collector.Collect(scene.Input().MutableDeviceState(), window);
        const kb::input::InputDeviceState& device = scene.Input().DeviceState();
        if (device.HasFocus() &&
            device.IsKeyDown(kb::input::InputKey::Escape) == down) {
            return true;
        }
        Sleep(8U);
    }
    return false;
}

[[nodiscard]] bool WaitForKey(
    kb::scene::Scene& scene,
    kb::input::Win32InputCollector& collector,
    HWND window,
    kb::input::InputKey key,
    bool down) {
    for (std::uint32_t attempt = 0U; attempt < 120U; ++attempt) {
        PumpWindowMessages();
        collector.Collect(scene.Input().MutableDeviceState(), window);
        const kb::input::InputDeviceState& device =
            scene.Input().DeviceState();
        if (device.HasFocus() && device.IsKeyDown(key) == down) {
            return true;
        }
        Sleep(8U);
    }
    return false;
}

[[nodiscard]] bool UpdateAndRead(
    kb::scene::Scene& scene,
    kb::script::ScriptRuntimeHost& scriptHost,
    ScriptInputSnapshot& snapshot) {
    if (!scene.Runtime().Update(1.0F / 60.0F)) {
        return false;
    }
    const std::vector<std::string> diagnostics = scriptHost.DrainSceneSystemDiagnostics();
    for (const std::string& diagnostic : diagnostics) {
        std::fprintf(
            stderr, "kb_standalone_player: input_runtime script_diagnostic=%s\n", diagnostic.c_str());
    }
    if (!diagnostics.empty()) {
        return false;
    }
    const std::optional<ScriptInputSnapshot> read = ReadScriptSnapshot(scriptHost.SharedState());
    if (!read) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime lua_snapshot=missing\n");
        return false;
    }
    snapshot = *read;
    return true;
}

void LogSnapshot(std::string_view phase, const ScriptInputSnapshot& snapshot) {
    const auto logPlayer = [&](std::string_view player, const PlayerInputSnapshot& value) {
        std::fprintf(stdout,
        "kb_standalone_player: input_script phase=%.*s tick=%d player=%.*s "
        "ActionBool=%u ActionFloat=%.4f Action2D=(%.4f,%.4f) "
        "Pressed=%u Released=%u Held=%u\n",
        static_cast<int>(phase.size()),
        phase.data(),
        snapshot.tick,
        static_cast<int>(player.size()),
        player.data(),
        value.actionBool ? 1U : 0U,
        static_cast<double>(value.actionFloat),
        static_cast<double>(value.action2DX),
        static_cast<double>(value.action2DY),
        value.pressed ? 1U : 0U,
        value.released ? 1U : 0U,
        value.held ? 1U : 0U);
    };
    logPlayer("p1", snapshot.player1);
    logPlayer("p2", snapshot.player2);
    std::fprintf(stdout,
        "kb_standalone_player: input_script phase=%.*s tick=%d device=touch "
        "ActionBool=%u Pressed=%u Released=%u Held=%u\n",
        static_cast<int>(phase.size()),
        phase.data(),
        snapshot.tick,
        snapshot.touchActionBool ? 1U : 0U,
        snapshot.touchPressed ? 1U : 0U,
        snapshot.touchReleased ? 1U : 0U,
        snapshot.touchHeld ? 1U : 0U);
    std::fprintf(stdout,
        "kb_standalone_player: input_script phase=%.*s tick=%d device=pointer "
        "Position=(%.1f,%.1f) Delta=(%.1f,%.1f) Button=%u Scroll=%.1f "
        "RayValid=%u RayOrigin=(%.4f,%.4f,%.4f) RayDirection=(%.4f,%.4f,%.4f)\n",
        static_cast<int>(phase.size()),
        phase.data(),
        snapshot.tick,
        static_cast<double>(snapshot.pointerX),
        static_cast<double>(snapshot.pointerY),
        static_cast<double>(snapshot.pointerDeltaX),
        static_cast<double>(snapshot.pointerDeltaY),
        snapshot.pointerLeft ? 1U : 0U,
        static_cast<double>(snapshot.pointerScroll),
        snapshot.pointerRayValid ? 1U : 0U,
        static_cast<double>(snapshot.pointerOriginX),
        static_cast<double>(snapshot.pointerOriginY),
        static_cast<double>(snapshot.pointerOriginZ),
        static_cast<double>(snapshot.pointerDirectionX),
        static_cast<double>(snapshot.pointerDirectionY),
        static_cast<double>(snapshot.pointerDirectionZ));
    std::fprintf(stdout,
        "kb_standalone_player: input_script phase=%.*s tick=%d device=priority "
        "active=(gameplay:%u,ui:%u,console:%u,debug:%u) "
        "bands=(%d,%d,%d,%d)\n",
        static_cast<int>(phase.size()),
        phase.data(),
        snapshot.tick,
        snapshot.priorityGameplayActive ? 1U : 0U,
        snapshot.priorityUiActive ? 1U : 0U,
        snapshot.priorityConsoleActive ? 1U : 0U,
        snapshot.priorityDebugActive ? 1U : 0U,
        snapshot.priorityGameplayValue,
        snapshot.priorityUiValue,
        snapshot.priorityConsoleValue,
        snapshot.priorityDebugValue);
    std::fprintf(stdout,
        "kb_standalone_player: input_script phase=%.*s tick=%d device=status "
        "focus=%u gamepads=(%u,%u,%u,%u)\n",
        static_cast<int>(phase.size()),
        phase.data(),
        snapshot.tick,
        snapshot.hasFocus ? 1U : 0U,
        snapshot.gamepadConnected[0] ? 1U : 0U,
        snapshot.gamepadConnected[1] ? 1U : 0U,
        snapshot.gamepadConnected[2] ? 1U : 0U,
        snapshot.gamepadConnected[3] ? 1U : 0U);
}

} // namespace

bool RunStandaloneInputRuntimeVerification(
    kb::scene::Scene& scene,
    kb::input::Win32InputCollector& collector,
    kb::script::ScriptRuntimeHost& scriptHost,
    HWND window) {
    ScopedInputPackage package;
    PriorityRuntimeAssets priorityAssets{};
    if (!package.Ready() ||
        !PrepareAssets(scene, package.Root(), priorityAssets)) {
        return false;
    }
    if (!scriptHost.SharedState().Set(
            "input.rebind.context",
            kb::script::ScriptValue{
                std::to_string(priorityAssets.primaryContextId)}) ||
        !scriptHost.SharedState().Set(
            "input.rebind.profile",
            kb::script::ScriptValue{
                priorityAssets.rebindProfilePath.string()}) ||
        !scriptHost.SharedState().Set(
            "input.rebind.command",
            kb::script::ScriptValue{std::string{}})) {
        std::fprintf(
            stderr,
            "kb_standalone_player: input_runtime rebind_configuration=failed\n");
        return false;
    }

    if (!ActivateWindowForInput(window)) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime window_activation=failed\n");
        return false;
    }
    UpdateWindow(window);

    ScopedSyntheticKeys keys;
    if (!WaitForPhysicalState(scene, collector, window, false, false)) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime baseline_device_state=failed\n");
        return false;
    }

    ScriptInputSnapshot baseline{};
    if (!UpdateAndRead(scene, scriptHost, baseline)) {
        return false;
    }
    LogSnapshot("baseline", baseline);

    if (!keys.PressPrimary() || !WaitForPhysicalState(scene, collector, window, true, false)) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime primary_key_press=failed\n");
        return false;
    }
    ScriptInputSnapshot primaryPressed{};
    if (!UpdateAndRead(scene, scriptHost, primaryPressed)) {
        return false;
    }
    LogSnapshot("p1_pressed", primaryPressed);

    collector.Collect(scene.Input().MutableDeviceState(), window);
    ScriptInputSnapshot primaryHeld{};
    if (!UpdateAndRead(scene, scriptHost, primaryHeld)) {
        return false;
    }
    LogSnapshot("p1_held", primaryHeld);

    if (!keys.ReleasePrimary() || !WaitForPhysicalState(scene, collector, window, false, false)) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime primary_key_release=failed\n");
        return false;
    }
    ScriptInputSnapshot primaryReleased{};
    if (!UpdateAndRead(scene, scriptHost, primaryReleased)) {
        return false;
    }
    LogSnapshot("p1_released", primaryReleased);

    collector.Collect(scene.Input().MutableDeviceState(), window);
    ScriptInputSnapshot primarySettled{};
    if (!UpdateAndRead(scene, scriptHost, primarySettled)) {
        return false;
    }
    LogSnapshot("p1_settled", primarySettled);

    if (!keys.PressSecondary() || !WaitForPhysicalState(scene, collector, window, false, true)) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime secondary_key_press=failed\n");
        return false;
    }
    ScriptInputSnapshot secondaryPressed{};
    if (!UpdateAndRead(scene, scriptHost, secondaryPressed)) {
        return false;
    }
    LogSnapshot("p2_pressed", secondaryPressed);

    collector.Collect(scene.Input().MutableDeviceState(), window);
    ScriptInputSnapshot secondaryHeld{};
    if (!UpdateAndRead(scene, scriptHost, secondaryHeld)) {
        return false;
    }
    LogSnapshot("p2_held", secondaryHeld);

    if (!keys.ReleaseSecondary() || !WaitForPhysicalState(scene, collector, window, false, false)) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime secondary_key_release=failed\n");
        return false;
    }
    ScriptInputSnapshot secondaryReleased{};
    if (!UpdateAndRead(scene, scriptHost, secondaryReleased)) {
        return false;
    }
    LogSnapshot("p2_released", secondaryReleased);

    collector.Collect(scene.Input().MutableDeviceState(), window);
    ScriptInputSnapshot secondarySettled{};
    if (!UpdateAndRead(scene, scriptHost, secondarySettled)) {
        return false;
    }
    LogSnapshot("p2_settled", secondarySettled);

    ScopedSyntheticTouch touch;
    if (!touch.Initialize(window) ||
        !WaitForTouchState(
            scene,
            collector,
            window,
            true,
            kb::input::InputTouchPhase::Began,
            180.0F,
            120.0F,
            &touch)) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime touch_press=failed error=%lu\n",
            static_cast<unsigned long>(GetLastError()));
        return false;
    }
    const kb::input::InputTouchPoint touchBegan =
        scene.Input().DeviceState().TouchPoints().front();
    ScriptInputSnapshot touchPressed{};
    if (!UpdateAndRead(scene, scriptHost, touchPressed)) {
        return false;
    }
    LogSnapshot("touch_pressed", touchPressed);

    if (!touch.Move() ||
        !WaitForTouchState(
            scene,
            collector,
            window,
            true,
            kb::input::InputTouchPhase::Moved,
            220.0F,
            145.0F,
            &touch)) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime touch_move=failed error=%lu\n",
            static_cast<unsigned long>(GetLastError()));
        return false;
    }
    const kb::input::InputTouchPoint touchMoved =
        scene.Input().DeviceState().TouchPoints().front();
    ScriptInputSnapshot touchHeld{};
    if (!UpdateAndRead(scene, scriptHost, touchHeld)) {
        return false;
    }
    LogSnapshot("touch_moved", touchHeld);

    if (!touch.Release() ||
        !WaitForTouchState(
            scene,
            collector,
            window,
            false,
            kb::input::InputTouchPhase::Ended,
            220.0F,
            145.0F)) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime touch_release=failed error=%lu\n",
            static_cast<unsigned long>(GetLastError()));
        return false;
    }
    const kb::input::InputTouchPoint touchEnded =
        scene.Input().DeviceState().TouchPoints().front();
    ScriptInputSnapshot touchReleased{};
    if (!UpdateAndRead(scene, scriptHost, touchReleased)) {
        return false;
    }
    LogSnapshot("touch_released", touchReleased);

    collector.Collect(scene.Input().MutableDeviceState(), window);
    const bool touchContactsCleared = scene.Input().DeviceState().TouchPoints().empty();
    ScriptInputSnapshot touchSettled{};
    if (!UpdateAndRead(scene, scriptHost, touchSettled)) {
        return false;
    }
    LogSnapshot("touch_settled", touchSettled);

    RECT pointerClient{};
    if (GetClientRect(window, &pointerClient) == 0 ||
        pointerClient.right <= pointerClient.left ||
        pointerClient.bottom <= pointerClient.top) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime pointer_client_rect=failed\n");
        return false;
    }
    const LONG pointerClientWidth = pointerClient.right - pointerClient.left;
    const LONG pointerClientHeight = pointerClient.bottom - pointerClient.top;
    ScopedSyntheticPointer pointer{window};
    if (!CollectPointerFrame(
            scene,
            collector,
            window,
            pointer,
            pointerClientWidth / 2,
            pointerClientHeight / 2,
            32.0F,
            32.0F,
            true,
            WHEEL_DELTA)) {
        std::fprintf(stderr,
            "kb_standalone_player: input_runtime pointer_center_frame=failed error=%lu\n",
            static_cast<unsigned long>(GetLastError()));
        return false;
    }
    const kb::input::InputDeviceState& pointerCenterDevice = scene.Input().DeviceState();
    const float pointerCenterDeltaX =
        pointerCenterDevice.GetValue(kb::input::InputKey::MouseX);
    const float pointerCenterDeltaY =
        pointerCenterDevice.GetValue(kb::input::InputKey::MouseY);
    const kb::scene::SceneRenderCameraRay pointerCenterRay =
        kb::scene::SceneRenderFeedback::ScreenPointToRay(
            scene, pointerCenterDevice.PointerX(), pointerCenterDevice.PointerY());
    ScriptInputSnapshot pointerCenter{};
    if (!UpdateAndRead(scene, scriptHost, pointerCenter)) {
        return false;
    }
    LogSnapshot("pointer_center", pointerCenter);

    if (!CollectPointerFrame(
            scene,
            collector,
            window,
            pointer,
            (pointerClientWidth * 3) / 4,
            (pointerClientHeight * 3) / 8,
            48.0F,
            24.0F,
            false,
            -2 * WHEEL_DELTA)) {
        std::fprintf(stderr,
            "kb_standalone_player: input_runtime pointer_offset_frame=failed error=%lu\n",
            static_cast<unsigned long>(GetLastError()));
        return false;
    }
    const kb::input::InputDeviceState& pointerOffsetDevice = scene.Input().DeviceState();
    const float pointerOffsetDeltaX =
        pointerOffsetDevice.GetValue(kb::input::InputKey::MouseX);
    const float pointerOffsetDeltaY =
        pointerOffsetDevice.GetValue(kb::input::InputKey::MouseY);
    const kb::scene::SceneRenderCameraRay pointerOffsetRay =
        kb::scene::SceneRenderFeedback::ScreenPointToRay(
            scene, pointerOffsetDevice.PointerX(), pointerOffsetDevice.PointerY());
    ScriptInputSnapshot pointerOffset{};
    if (!UpdateAndRead(scene, scriptHost, pointerOffset)) {
        return false;
    }
    LogSnapshot("pointer_offset", pointerOffset);

    collector.Collect(scene.Input().MutableDeviceState(), window);
    ScriptInputSnapshot pointerSettled{};
    if (!UpdateAndRead(scene, scriptHost, pointerSettled)) {
        return false;
    }
    LogSnapshot("pointer_settled", pointerSettled);

    const auto capturePriorityPress = [&](std::string_view phase,
                                          ScriptInputSnapshot& snapshot) {
        if (!keys.PressPriority() ||
            !WaitForPriorityKey(scene, collector, window, true) ||
            !UpdateAndRead(scene, scriptHost, snapshot)) {
            return false;
        }
        LogSnapshot(phase, snapshot);
        return true;
    };
    const auto capturePriorityRelease = [&](std::string_view phase,
                                            ScriptInputSnapshot& snapshot) {
        if (!keys.ReleasePriority() ||
            !WaitForPriorityKey(scene, collector, window, false) ||
            !UpdateAndRead(scene, scriptHost, snapshot)) {
            return false;
        }
        LogSnapshot(phase, snapshot);
        return true;
    };

    ScriptInputSnapshot priorityDebug{};
    ScriptInputSnapshot priorityDebugReleased{};
    if (!capturePriorityPress("priority_debug", priorityDebug) ||
        !capturePriorityRelease("priority_debug_released", priorityDebugReleased)) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime priority_debug=failed\n");
        return false;
    }
    scene.Input().RemoveMappingContext(priorityAssets.debugContextId);

    ScriptInputSnapshot priorityConsole{};
    ScriptInputSnapshot priorityConsoleReleased{};
    if (!capturePriorityPress("priority_console", priorityConsole) ||
        !capturePriorityRelease("priority_console_released", priorityConsoleReleased)) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime priority_console=failed\n");
        return false;
    }
    scene.Input().RemoveMappingContext(priorityAssets.consoleContextId);

    ScriptInputSnapshot priorityUi{};
    ScriptInputSnapshot priorityUiReleased{};
    if (!capturePriorityPress("priority_ui", priorityUi) ||
        !capturePriorityRelease("priority_ui_released", priorityUiReleased)) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime priority_ui=failed\n");
        return false;
    }
    scene.Input().RemoveMappingContext(priorityAssets.uiContextId);

    ScriptInputSnapshot priorityGameplay{};
    ScriptInputSnapshot priorityGameplayReleased{};
    if (!capturePriorityPress("priority_gameplay", priorityGameplay) ||
        !capturePriorityRelease("priority_gameplay_released", priorityGameplayReleased)) {
        std::fprintf(stderr, "kb_standalone_player: input_runtime priority_gameplay=failed\n");
        return false;
    }

    const auto runRebindCommand =
        [&](std::string command, std::string_view phase,
            ScriptInputSnapshot& snapshot) {
            if (!scriptHost.SharedState().Set(
                    "input.rebind.command",
                    kb::script::ScriptValue{std::move(command)})) {
                return false;
            }
            collector.Collect(scene.Input().MutableDeviceState(), window);
            if (!UpdateAndRead(scene, scriptHost, snapshot)) {
                return false;
            }
            LogSnapshot(phase, snapshot);
            return true;
        };
    const auto captureRebindPress =
        [&](WORD virtualKey, kb::input::InputKey inputKey,
            std::string_view phase, ScriptInputSnapshot& snapshot) {
            if (!keys.PressRebindKey(virtualKey) ||
                !WaitForKey(scene, collector, window, inputKey, true) ||
                !UpdateAndRead(scene, scriptHost, snapshot)) {
                return false;
            }
            LogSnapshot(phase, snapshot);
            return true;
        };
    const auto captureRebindRelease =
        [&](kb::input::InputKey inputKey, std::string_view phase,
            ScriptInputSnapshot& snapshot) {
            if (!keys.ReleaseRebindKey() ||
                !WaitForKey(scene, collector, window, inputKey, false) ||
                !UpdateAndRead(scene, scriptHost, snapshot)) {
                return false;
            }
            LogSnapshot(phase, snapshot);
            return true;
        };

    ScriptInputSnapshot rebindConflict{};
    if (!runRebindCommand(
            "conflict", "rebind_conflict", rebindConflict)) {
        std::fprintf(
            stderr,
            "kb_standalone_player: input_runtime rebind_conflict=failed\n");
        return false;
    }
    const std::optional<kb::script::ScriptValue> conflictApplied =
        scriptHost.SharedState().Get("input.rebind.applied");
    const std::optional<kb::script::ScriptValue> conflictBinding =
        scriptHost.SharedState().Get("input.rebind.conflict");

    ScriptInputSnapshot rebindApplied{};
    if (!runRebindCommand("apply", "rebind_apply", rebindApplied)) {
        std::fprintf(
            stderr,
            "kb_standalone_player: input_runtime rebind_apply=failed\n");
        return false;
    }
    const std::optional<kb::script::ScriptValue> appliedResult =
        scriptHost.SharedState().Get("input.rebind.applied");
    const std::optional<kb::script::ScriptValue> appliedConflict =
        scriptHost.SharedState().Get("input.rebind.conflict");

    ScriptInputSnapshot reboundOldPressed{};
    ScriptInputSnapshot reboundOldReleased{};
    ScriptInputSnapshot reboundNewPressed{};
    ScriptInputSnapshot reboundNewReleased{};
    if (!captureRebindPress(
            VK_SPACE, kb::input::InputKey::Space,
            "rebind_old_pressed", reboundOldPressed) ||
        !captureRebindRelease(
            kb::input::InputKey::Space,
            "rebind_old_released", reboundOldReleased) ||
        !captureRebindPress(
            'R', kb::input::InputKey::R,
            "rebind_new_pressed", reboundNewPressed) ||
        !captureRebindRelease(
            kb::input::InputKey::R,
            "rebind_new_released", reboundNewReleased)) {
        std::fprintf(
            stderr,
            "kb_standalone_player: input_runtime rebind_live_keys=failed\n");
        return false;
    }

    ScriptInputSnapshot rebindSaved{};
    if (!runRebindCommand("save", "rebind_save", rebindSaved)) {
        std::fprintf(
            stderr,
            "kb_standalone_player: input_runtime rebind_save=failed\n");
        return false;
    }
    const std::optional<kb::script::ScriptValue> savedResult =
        scriptHost.SharedState().Get("input.rebind.saved");
    const std::optional<kb::script::ScriptValue> savedError =
        scriptHost.SharedState().Get("input.rebind.error");
    const kb::input::InputAssetLoadResult<
        std::vector<kb::input::InputRebindOverride>>
        savedProfile =
            kb::input::ReadRebindProfile(
                priorityAssets.rebindProfilePath);

    ScriptInputSnapshot rebindRestored{};
    ScriptInputSnapshot restoredOldPressed{};
    ScriptInputSnapshot restoredOldReleased{};
    if (!runRebindCommand(
            "restore", "rebind_restore_default", rebindRestored) ||
        !captureRebindPress(
            VK_SPACE, kb::input::InputKey::Space,
            "rebind_default_pressed", restoredOldPressed) ||
        !captureRebindRelease(
            kb::input::InputKey::Space,
            "rebind_default_released", restoredOldReleased)) {
        std::fprintf(
            stderr,
            "kb_standalone_player: input_runtime rebind_restore=failed\n");
        return false;
    }

    ScriptInputSnapshot rebindLoaded{};
    if (!runRebindCommand("load", "rebind_load", rebindLoaded)) {
        std::fprintf(
            stderr,
            "kb_standalone_player: input_runtime rebind_load=failed\n");
        return false;
    }
    const std::optional<kb::script::ScriptValue> loadedResult =
        scriptHost.SharedState().Get("input.rebind.loaded");
    const std::optional<kb::script::ScriptValue> loadedError =
        scriptHost.SharedState().Get("input.rebind.error");

    ScriptInputSnapshot loadedOldPressed{};
    ScriptInputSnapshot loadedOldReleased{};
    ScriptInputSnapshot loadedNewPressed{};
    ScriptInputSnapshot loadedNewReleased{};
    if (!captureRebindPress(
            VK_SPACE, kb::input::InputKey::Space,
            "rebind_loaded_old_pressed", loadedOldPressed) ||
        !captureRebindRelease(
            kb::input::InputKey::Space,
            "rebind_loaded_old_released", loadedOldReleased) ||
        !captureRebindPress(
            'R', kb::input::InputKey::R,
            "rebind_loaded_new_pressed", loadedNewPressed) ||
        !captureRebindRelease(
            kb::input::InputKey::R,
            "rebind_loaded_new_released", loadedNewReleased)) {
        std::fprintf(
            stderr,
            "kb_standalone_player: input_runtime rebind_loaded_keys=failed\n");
        return false;
    }

    kb::input::InputDeviceState& mutableDevice =
        scene.Input().MutableDeviceState();
    for (std::uint8_t index = 0U;
         index < kb::input::InputDeviceState::kMaxGamepads; ++index) {
        mutableDevice.SetGamepadConnected(index, true);
        mutableDevice.SetKeyDown(
            kb::input::InputKey::GamepadFaceBottom, true, index);
    }
    collector.Collect(mutableDevice, window);
    std::array<bool, kb::input::InputDeviceState::kMaxGamepads>
        gamepadConnected{};
    bool observedPhysicalDisconnect = false;
    bool disconnectedStateCleared = true;
    for (std::uint8_t index = 0U;
         index < kb::input::InputDeviceState::kMaxGamepads; ++index) {
        gamepadConnected[index] =
            mutableDevice.IsGamepadConnected(index);
        if (!gamepadConnected[index]) {
            observedPhysicalDisconnect = true;
            disconnectedStateCleared =
                disconnectedStateCleared &&
                !mutableDevice.IsKeyDown(
                    kb::input::InputKey::GamepadFaceBottom, index);
        }
    }
    ScriptInputSnapshot deviceDisconnect{};
    if (!UpdateAndRead(scene, scriptHost, deviceDisconnect)) {
        std::fprintf(
            stderr,
            "kb_standalone_player: input_runtime device_disconnect=failed\n");
        return false;
    }
    LogSnapshot("device_disconnect", deviceDisconnect);

    ScriptInputSnapshot beforeFocusLoss{};
    if (!captureRebindPress(
            'R', kb::input::InputKey::R,
            "focus_loss_key_pressed", beforeFocusLoss)) {
        std::fprintf(
            stderr,
            "kb_standalone_player: input_runtime focus_loss_press=failed\n");
        return false;
    }
    ScopedFocusProbe focusProbe;
    if (!focusProbe.Launch()) {
        std::fprintf(
            stderr,
            "kb_standalone_player: input_runtime focus_probe=failed\n");
        return false;
    }
    collector.Collect(scene.Input().MutableDeviceState(), window);
    const bool backgroundStateReset =
        !scene.Input().DeviceState().HasFocus() &&
        !scene.Input().DeviceState().IsKeyDown(kb::input::InputKey::R) &&
        scene.Input().DeviceState().TouchPoints().empty();
    ScriptInputSnapshot focusLost{};
    if (!UpdateAndRead(scene, scriptHost, focusLost)) {
        std::fprintf(
            stderr,
            "kb_standalone_player: input_runtime focus_lost_update=failed\n");
        return false;
    }
    LogSnapshot("focus_lost", focusLost);

    if (!keys.ReleaseRebindKey()) {
        std::fprintf(
            stderr,
            "kb_standalone_player: input_runtime background_key_release=failed\n");
        return false;
    }
    PumpWindowMessages();
    if (!ActivateWindowForInput(window) ||
        !WaitForKey(
            scene, collector, window, kb::input::InputKey::R, false)) {
        std::fprintf(
            stderr,
            "kb_standalone_player: input_runtime focus_restore=failed\n");
        return false;
    }
    ScriptInputSnapshot focusRestored{};
    if (!UpdateAndRead(scene, scriptHost, focusRestored)) {
        return false;
    }
    LogSnapshot("focus_restored", focusRestored);

    constexpr float kDiagonal = 0.70710678F;
    const auto zeroValues = [](const PlayerInputSnapshot& value) {
        return !value.actionBool && std::fabs(value.actionFloat) < 0.001F &&
            std::fabs(value.action2DX) < 0.001F && std::fabs(value.action2DY) < 0.001F;
    };
    const auto neutral = [&zeroValues](const PlayerInputSnapshot& value) {
        return zeroValues(value) && !value.pressed && !value.released && !value.held;
    };
    const auto pressed = [kDiagonal](const PlayerInputSnapshot& value) {
        return value.actionBool && std::fabs(value.actionFloat - 1.0F) < 0.001F &&
            std::fabs(value.action2DX - kDiagonal) < 0.001F &&
            std::fabs(value.action2DY - kDiagonal) < 0.001F &&
            value.pressed && !value.released && value.held;
    };
    const auto held = [kDiagonal](const PlayerInputSnapshot& value) {
        return value.actionBool && std::fabs(value.actionFloat - 1.0F) < 0.001F &&
            std::fabs(value.action2DX - kDiagonal) < 0.001F &&
            std::fabs(value.action2DY - kDiagonal) < 0.001F &&
            !value.pressed && !value.released && value.held;
    };
    const auto released = [&zeroValues](const PlayerInputSnapshot& value) {
        return zeroValues(value) && !value.pressed && value.released && !value.held;
    };
    const auto jumpPressedOnly = [](const PlayerInputSnapshot& value) {
        return value.actionBool &&
            std::fabs(value.actionFloat) < 0.001F &&
            std::fabs(value.action2DX) < 0.001F &&
            std::fabs(value.action2DY) < 0.001F &&
            value.pressed && !value.released && value.held;
    };
    const auto jumpReleasedOnly = [](const PlayerInputSnapshot& value) {
        return !value.actionBool &&
            std::fabs(value.actionFloat) < 0.001F &&
            std::fabs(value.action2DX) < 0.001F &&
            std::fabs(value.action2DY) < 0.001F &&
            !value.pressed && value.released && !value.held;
    };
    const auto touchNeutral = [](const ScriptInputSnapshot& value) {
        return !value.touchActionBool && !value.touchPressed &&
            !value.touchReleased && !value.touchHeld;
    };
    const auto pointerMatchesRay = [](const ScriptInputSnapshot& value,
                                      const kb::scene::SceneRenderCameraRay& ray) {
        return value.pointerRayValid == ray.valid &&
            std::fabs(value.pointerOriginX - ray.ray.origin.x) < 0.001F &&
            std::fabs(value.pointerOriginY - ray.ray.origin.y) < 0.001F &&
            std::fabs(value.pointerOriginZ - ray.ray.origin.z) < 0.001F &&
            std::fabs(value.pointerDirectionX - ray.ray.direction.x) < 0.001F &&
            std::fabs(value.pointerDirectionY - ray.ray.direction.y) < 0.001F &&
            std::fabs(value.pointerDirectionZ - ray.ray.direction.z) < 0.001F;
    };
    const auto priorityBandsValid = [](const ScriptInputSnapshot& value) {
        return value.priorityGameplayValue == kb::input::InputContextPriority::Gameplay &&
            value.priorityUiValue == kb::input::InputContextPriority::UI &&
            value.priorityConsoleValue == kb::input::InputContextPriority::Console &&
            value.priorityDebugValue == kb::input::InputContextPriority::DebugOverlay;
    };
    const auto priorityNone = [](const ScriptInputSnapshot& value) {
        return !value.priorityGameplayActive && !value.priorityUiActive &&
            !value.priorityConsoleActive && !value.priorityDebugActive;
    };
    const bool succeeded =
        baseline.tick == 1 && neutral(baseline.player1) && neutral(baseline.player2) && touchNeutral(baseline) &&
        primaryPressed.tick == 2 && pressed(primaryPressed.player1) && neutral(primaryPressed.player2) && touchNeutral(primaryPressed) &&
        primaryHeld.tick == 3 && held(primaryHeld.player1) && neutral(primaryHeld.player2) && touchNeutral(primaryHeld) &&
        primaryReleased.tick == 4 && released(primaryReleased.player1) && neutral(primaryReleased.player2) && touchNeutral(primaryReleased) &&
        primarySettled.tick == 5 && neutral(primarySettled.player1) && neutral(primarySettled.player2) && touchNeutral(primarySettled) &&
        secondaryPressed.tick == 6 && neutral(secondaryPressed.player1) && pressed(secondaryPressed.player2) && touchNeutral(secondaryPressed) &&
        secondaryHeld.tick == 7 && neutral(secondaryHeld.player1) && held(secondaryHeld.player2) && touchNeutral(secondaryHeld) &&
        secondaryReleased.tick == 8 && neutral(secondaryReleased.player1) && released(secondaryReleased.player2) && touchNeutral(secondaryReleased) &&
        secondarySettled.tick == 9 && neutral(secondarySettled.player1) && neutral(secondarySettled.player2) && touchNeutral(secondarySettled) &&
        touchPressed.tick == 10 && neutral(touchPressed.player1) && neutral(touchPressed.player2) &&
            touchPressed.touchActionBool && touchPressed.touchPressed &&
            !touchPressed.touchReleased && touchPressed.touchHeld &&
        touchHeld.tick == 11 && neutral(touchHeld.player1) && neutral(touchHeld.player2) &&
            touchHeld.touchActionBool && !touchHeld.touchPressed &&
            !touchHeld.touchReleased && touchHeld.touchHeld &&
        touchReleased.tick == 12 && neutral(touchReleased.player1) && neutral(touchReleased.player2) &&
            !touchReleased.touchActionBool && !touchReleased.touchPressed &&
            touchReleased.touchReleased && !touchReleased.touchHeld &&
        touchSettled.tick == 13 && neutral(touchSettled.player1) && neutral(touchSettled.player2) &&
            touchNeutral(touchSettled) && touchContactsCleared &&
        (touchBegan.phase == kb::input::InputTouchPhase::Began ||
            touchBegan.phase == kb::input::InputTouchPhase::Moved) &&
            std::fabs(touchBegan.x - 180.0F) < 1.0F &&
            std::fabs(touchBegan.y - 120.0F) < 1.0F &&
        touchMoved.phase == kb::input::InputTouchPhase::Moved &&
            std::fabs(touchMoved.x - 220.0F) < 1.0F &&
            std::fabs(touchMoved.y - 145.0F) < 1.0F &&
        touchEnded.phase == kb::input::InputTouchPhase::Ended &&
        pointerCenter.tick == 14 &&
            neutral(pointerCenter.player1) && neutral(pointerCenter.player2) &&
            touchNeutral(pointerCenter) &&
            std::fabs(pointerCenter.pointerX - 32.0F) < 1.0F &&
            std::fabs(pointerCenter.pointerY - 32.0F) < 1.0F &&
            std::fabs(pointerCenter.pointerDeltaX - pointerCenterDeltaX) < 0.001F &&
            std::fabs(pointerCenter.pointerDeltaY - pointerCenterDeltaY) < 0.001F &&
            pointerCenter.pointerLeft &&
            std::fabs(pointerCenter.pointerScroll - 1.0F) < 0.001F &&
            pointerCenterRay.valid && pointerMatchesRay(pointerCenter, pointerCenterRay) &&
            std::fabs(pointerCenter.pointerDirectionX) < 0.001F &&
            std::fabs(pointerCenter.pointerDirectionY) < 0.001F &&
            std::fabs(pointerCenter.pointerDirectionZ - 1.0F) < 0.001F &&
        pointerOffset.tick == 15 &&
            neutral(pointerOffset.player1) && neutral(pointerOffset.player2) &&
            touchNeutral(pointerOffset) &&
            std::fabs(pointerOffset.pointerX - 48.0F) < 1.0F &&
            std::fabs(pointerOffset.pointerY - 24.0F) < 1.0F &&
            std::fabs(pointerOffset.pointerDeltaX - pointerOffsetDeltaX) < 0.001F &&
            std::fabs(pointerOffset.pointerDeltaY - pointerOffsetDeltaY) < 0.001F &&
            !pointerOffset.pointerLeft &&
            std::fabs(pointerOffset.pointerScroll + 2.0F) < 0.001F &&
            pointerOffsetRay.valid && pointerMatchesRay(pointerOffset, pointerOffsetRay) &&
        pointerSettled.tick == 16 &&
            neutral(pointerSettled.player1) && neutral(pointerSettled.player2) &&
            touchNeutral(pointerSettled) &&
            std::fabs(pointerSettled.pointerX - 48.0F) < 1.0F &&
            std::fabs(pointerSettled.pointerY - 24.0F) < 1.0F &&
            std::fabs(pointerSettled.pointerDeltaX) < 0.001F &&
            std::fabs(pointerSettled.pointerDeltaY) < 0.001F &&
            !pointerSettled.pointerLeft &&
            std::fabs(pointerSettled.pointerScroll) < 0.001F &&
        priorityDebug.tick == 17 && priorityBandsValid(priorityDebug) &&
            !priorityDebug.priorityGameplayActive && !priorityDebug.priorityUiActive &&
            !priorityDebug.priorityConsoleActive && priorityDebug.priorityDebugActive &&
        priorityDebugReleased.tick == 18 && priorityNone(priorityDebugReleased) &&
        priorityConsole.tick == 19 && priorityBandsValid(priorityConsole) &&
            !priorityConsole.priorityGameplayActive && !priorityConsole.priorityUiActive &&
            priorityConsole.priorityConsoleActive && !priorityConsole.priorityDebugActive &&
        priorityConsoleReleased.tick == 20 && priorityNone(priorityConsoleReleased) &&
        priorityUi.tick == 21 && priorityBandsValid(priorityUi) &&
            !priorityUi.priorityGameplayActive && priorityUi.priorityUiActive &&
            !priorityUi.priorityConsoleActive && !priorityUi.priorityDebugActive &&
        priorityUiReleased.tick == 22 && priorityNone(priorityUiReleased) &&
        priorityGameplay.tick == 23 && priorityBandsValid(priorityGameplay) &&
            priorityGameplay.priorityGameplayActive && !priorityGameplay.priorityUiActive &&
            !priorityGameplay.priorityConsoleActive && !priorityGameplay.priorityDebugActive &&
        priorityGameplayReleased.tick == 24 && priorityNone(priorityGameplayReleased) &&
        rebindConflict.tick == 25 && neutral(rebindConflict.player1) &&
            conflictApplied.has_value() && !conflictApplied->AsBool() &&
            conflictBinding.has_value() && conflictBinding->AsString() == "2" &&
        rebindApplied.tick == 26 && neutral(rebindApplied.player1) &&
            appliedResult.has_value() && appliedResult->AsBool() &&
            appliedConflict.has_value() && appliedConflict->AsString().empty() &&
        reboundOldPressed.tick == 27 && neutral(reboundOldPressed.player1) &&
        reboundOldReleased.tick == 28 && neutral(reboundOldReleased.player1) &&
        reboundNewPressed.tick == 29 &&
            jumpPressedOnly(reboundNewPressed.player1) &&
        reboundNewReleased.tick == 30 &&
            jumpReleasedOnly(reboundNewReleased.player1) &&
        rebindSaved.tick == 31 && neutral(rebindSaved.player1) &&
            savedResult.has_value() && savedResult->AsBool() &&
            savedError.has_value() && savedError->AsString().empty() &&
            savedProfile.succeeded && savedProfile.asset.size() == 1U &&
            savedProfile.asset.front().bindingId == 1U &&
            savedProfile.asset.front().key == kb::input::InputKey::R &&
        rebindRestored.tick == 32 && neutral(rebindRestored.player1) &&
        restoredOldPressed.tick == 33 &&
            jumpPressedOnly(restoredOldPressed.player1) &&
        restoredOldReleased.tick == 34 &&
            jumpReleasedOnly(restoredOldReleased.player1) &&
        rebindLoaded.tick == 35 && neutral(rebindLoaded.player1) &&
            loadedResult.has_value() && loadedResult->AsBool() &&
            loadedError.has_value() && loadedError->AsString().empty() &&
        loadedOldPressed.tick == 36 && neutral(loadedOldPressed.player1) &&
        loadedOldReleased.tick == 37 && neutral(loadedOldReleased.player1) &&
        loadedNewPressed.tick == 38 &&
            jumpPressedOnly(loadedNewPressed.player1) &&
        loadedNewReleased.tick == 39 &&
            jumpReleasedOnly(loadedNewReleased.player1) &&
        deviceDisconnect.tick == 40 &&
            neutral(deviceDisconnect.player1) &&
            deviceDisconnect.hasFocus &&
            deviceDisconnect.gamepadConnected == gamepadConnected &&
            observedPhysicalDisconnect && disconnectedStateCleared &&
        beforeFocusLoss.tick == 41 &&
            jumpPressedOnly(beforeFocusLoss.player1) &&
            beforeFocusLoss.hasFocus &&
        focusLost.tick == 42 &&
            jumpReleasedOnly(focusLost.player1) &&
            !focusLost.hasFocus && backgroundStateReset &&
            focusLost.gamepadConnected == gamepadConnected &&
        focusRestored.tick == 43 &&
            neutral(focusRestored.player1) &&
            focusRestored.hasFocus &&
            focusRestored.gamepadConnected == gamepadConnected;

    std::fprintf(succeeded ? stdout : stderr,
        "kb_standalone_player: input_runtime result=%s source=win32 "
        "action_assets=7 context_assets=5 lua_asset=loaded script_module=active "
        "local_users=2 scene_frames=%llu focus=%u "
        "transitions=baseline,p1_pressed,p1_held,p1_released,p1_settled,"
        "p2_pressed,p2_held,p2_released,p2_settled,"
        "touch_pressed,touch_moved,touch_released,touch_settled,"
        "pointer_center,pointer_offset,pointer_settled,"
        "priority_debug,priority_console,priority_ui,priority_gameplay,"
        "rebind_conflict,rebind_apply,old_key_rejected,new_key_live,"
        "profile_save,default_restore,profile_load,persisted_key_live,"
        "device_disconnect,focus_loss_key_pressed,focus_lost,focus_restored "
        "touch_points=began(%.0f,%.0f),moved(%.0f,%.0f),ended "
        "pointer=position,delta,button,scroll,ray camera_frame=%llu "
        "priority_bands=0,1000,2000,3000 "
        "rebind=binding1:Space->R conflict=binding2 "
        "profile=atomic_save_reload "
        "device_disconnect=xinput_actual mask=(%u,%u,%u,%u) "
        "focus_loss=foreign_process released_once=1 recovered=1 "
        "composites=wasd,arrows deadzone=radial gamepad_slots_polled=4\n",
        succeeded ? "pass" : "fail",
        static_cast<unsigned long long>(scene.Runtime().FrameIndex()),
        scene.Input().DeviceState().HasFocus() ? 1U : 0U,
        static_cast<double>(touchBegan.x),
        static_cast<double>(touchBegan.y),
        static_cast<double>(touchMoved.x),
        static_cast<double>(touchMoved.y),
        static_cast<unsigned long long>(
            kb::scene::SceneRenderFeedback::PublishCount(scene)),
        gamepadConnected[0] ? 1U : 0U,
        gamepadConnected[1] ? 1U : 0U,
        gamepadConnected[2] ? 1U : 0U,
        gamepadConnected[3] ? 1U : 0U);
    std::fflush(succeeded ? stdout : stderr);
    return succeeded;
}

#endif
