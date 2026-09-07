#include "GameProjectRuntime.hpp"
#include "PackagedGameRuntime.hpp"
#include "PackagedRuntimeModules.hpp"

#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/input/InputTouchPoint.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/script/ScriptModule.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "kb/render/DisplayConfig.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/RuntimeAssetShaderProvider.hpp"

#include <bgfx/bgfx.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view kLogTag = "21kb";
constexpr std::string_view kPackagedAssetPack = "game.kbpack";

void LogError(std::string_view message) noexcept {
    const std::size_t length =
        (std::min)(message.size(), static_cast<std::size_t>(std::numeric_limits<int>::max()));
    __android_log_print(
        ANDROID_LOG_ERROR,
        kLogTag.data(),
        "%.*s",
        static_cast<int>(length),
        message.empty() ? "" : message.data());
}

void LogInfo(std::string_view message) noexcept {
    const std::size_t length =
        (std::min)(message.size(), static_cast<std::size_t>(std::numeric_limits<int>::max()));
    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag.data(),
        "%.*s",
        static_cast<int>(length),
        message.empty() ? "" : message.data());
}

class AndroidAssetMapping {
public:
    AndroidAssetMapping() = default;
    AndroidAssetMapping(const AndroidAssetMapping&) = delete;
    AndroidAssetMapping& operator=(const AndroidAssetMapping&) = delete;

    ~AndroidAssetMapping() {
        Reset();
    }

    [[nodiscard]] bool Open(AAssetManager* manager, std::string_view assetPath) {
        Reset();
        if (manager == nullptr || assetPath.empty()) {
            return false;
        }

        const std::string terminatedPath{ assetPath };
        AAsset* const asset =
            AAssetManager_open(manager, terminatedPath.c_str(), AASSET_MODE_RANDOM);
        if (asset == nullptr) {
            LogError("packaged asset pack was not found");
            return false;
        }

        off64_t assetOffset = 0;
        off64_t assetLength = 0;
        const int descriptor =
            AAsset_openFileDescriptor64(asset, &assetOffset, &assetLength);
        AAsset_close(asset);
        if (descriptor < 0) {
            LogError("asset pack has no file descriptor; APK compression must stay disabled");
            return false;
        }

        const long pageSize = getpagesize();
        const bool validRange = pageSize > 0 && assetOffset >= 0 && assetLength > 0 &&
            static_cast<std::uint64_t>(assetLength) <=
                kb::assets::bake::kMaxAssetPackBytes &&
            static_cast<std::uint64_t>(assetLength) <=
                static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
        if (!validRange) {
            close(descriptor);
            LogError("asset pack descriptor is invalid or exceeds the package budget");
            return false;
        }

        // APK entries are zipaligned, but their data offset is not required to equal the
        // device's page size. Map from the preceding live page boundary and expose only the
        // asset subrange. This is the AAsset_openFileDescriptor pattern and works on both
        // 4 KiB and 16 KiB Android devices without baking either page size into the host.
        const off64_t mappingOffset = assetOffset - (assetOffset % pageSize);
        const std::uint64_t assetDelta =
            static_cast<std::uint64_t>(assetOffset - mappingOffset);
        if (assetDelta > std::numeric_limits<std::size_t>::max() -
                static_cast<std::uint64_t>(assetLength)) {
            close(descriptor);
            LogError("asset pack mapping range exceeds the process address space");
            return false;
        }
        const std::size_t mappingBytes =
            static_cast<std::size_t>(assetDelta + static_cast<std::uint64_t>(assetLength));
        void* const mapped = mmap(
            nullptr,
            mappingBytes,
            PROT_READ,
            MAP_PRIVATE,
            descriptor,
            mappingOffset);
        const int mapError = errno;
        close(descriptor);
        if (mapped == MAP_FAILED) {
            __android_log_print(
                ANDROID_LOG_ERROR,
                kLogTag.data(),
                "asset pack mmap failed: errno=%d",
                mapError);
            return false;
        }

        mapping_ = mapped;
        mappingBytes_ = mappingBytes;
        data_ = static_cast<const std::uint8_t*>(mapped) + assetDelta;
        size_ = static_cast<std::size_t>(assetLength);
        return true;
    }

    void Reset() noexcept {
        if (mapping_ != nullptr) {
            static_cast<void>(munmap(mapping_, mappingBytes_));
        }
        mapping_ = nullptr;
        mappingBytes_ = 0U;
        data_ = nullptr;
        size_ = 0U;
    }

    [[nodiscard]] std::span<const std::uint8_t> Bytes() const noexcept {
        return { data_, size_ };
    }

private:
    void* mapping_ = nullptr;
    std::size_t mappingBytes_ = 0U;
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0U;
};

class AndroidRenderSurface final : public kb::render::RenderSurface {
public:
    void SetWindow(ANativeWindow* window) noexcept {
        window_ = window;
    }

    [[nodiscard]] std::uint32_t Width() const noexcept override {
        const std::int32_t width =
            window_ == nullptr ? 0 : ANativeWindow_getWidth(window_);
        return width > 0 ? static_cast<std::uint32_t>(width) : 0U;
    }

    [[nodiscard]] std::uint32_t Height() const noexcept override {
        const std::int32_t height =
            window_ == nullptr ? 0 : ANativeWindow_getHeight(window_);
        return height > 0 ? static_cast<std::uint32_t>(height) : 0U;
    }

    [[nodiscard]] void* NativeWindowHandle() const noexcept override {
        return window_;
    }

    [[nodiscard]] void* NativeDisplayHandle() const noexcept override {
        return nullptr;
    }

private:
    ANativeWindow* window_ = nullptr;
};

[[nodiscard]] kb::input::InputKey AndroidKey(int32_t keyCode) noexcept {
    using kb::input::InputKey;
    if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z) {
        return static_cast<InputKey>(
            static_cast<std::uint16_t>(InputKey::A) +
            static_cast<std::uint16_t>(keyCode - AKEYCODE_A));
    }
    if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9) {
        return static_cast<InputKey>(
            static_cast<std::uint16_t>(InputKey::Num0) +
            static_cast<std::uint16_t>(keyCode - AKEYCODE_0));
    }
    if (keyCode >= AKEYCODE_F1 && keyCode <= AKEYCODE_F12) {
        return static_cast<InputKey>(
            static_cast<std::uint16_t>(InputKey::F1) +
            static_cast<std::uint16_t>(keyCode - AKEYCODE_F1));
    }
    switch (keyCode) {
    case AKEYCODE_BACK: return InputKey::Escape;
    case AKEYCODE_TAB: return InputKey::Tab;
    case AKEYCODE_CAPS_LOCK: return InputKey::CapsLock;
    case AKEYCODE_SPACE: return InputKey::Space;
    case AKEYCODE_ENTER: return InputKey::Enter;
    case AKEYCODE_DEL: return InputKey::Backspace;
    case AKEYCODE_FORWARD_DEL: return InputKey::Delete;
    case AKEYCODE_INSERT: return InputKey::Insert;
    case AKEYCODE_MOVE_HOME: return InputKey::Home;
    case AKEYCODE_MOVE_END: return InputKey::End;
    case AKEYCODE_PAGE_UP: return InputKey::PageUp;
    case AKEYCODE_PAGE_DOWN: return InputKey::PageDown;
    case AKEYCODE_DPAD_UP: return InputKey::ArrowUp;
    case AKEYCODE_DPAD_DOWN: return InputKey::ArrowDown;
    case AKEYCODE_DPAD_LEFT: return InputKey::ArrowLeft;
    case AKEYCODE_DPAD_RIGHT: return InputKey::ArrowRight;
    case AKEYCODE_SHIFT_LEFT: return InputKey::LeftShift;
    case AKEYCODE_SHIFT_RIGHT: return InputKey::RightShift;
    case AKEYCODE_CTRL_LEFT: return InputKey::LeftControl;
    case AKEYCODE_CTRL_RIGHT: return InputKey::RightControl;
    case AKEYCODE_ALT_LEFT: return InputKey::LeftAlt;
    case AKEYCODE_ALT_RIGHT: return InputKey::RightAlt;
    case AKEYCODE_BUTTON_A: return InputKey::GamepadFaceBottom;
    case AKEYCODE_BUTTON_B: return InputKey::GamepadFaceRight;
    case AKEYCODE_BUTTON_X: return InputKey::GamepadFaceLeft;
    case AKEYCODE_BUTTON_Y: return InputKey::GamepadFaceTop;
    case AKEYCODE_BUTTON_L1: return InputKey::GamepadLeftShoulder;
    case AKEYCODE_BUTTON_R1: return InputKey::GamepadRightShoulder;
    case AKEYCODE_BUTTON_THUMBL: return InputKey::GamepadLeftThumb;
    case AKEYCODE_BUTTON_THUMBR: return InputKey::GamepadRightThumb;
    case AKEYCODE_BUTTON_START: return InputKey::GamepadStart;
    case AKEYCODE_BUTTON_SELECT: return InputKey::GamepadBack;
    default: return InputKey::None;
    }
}

class AndroidGameHost {
public:
    explicit AndroidGameHost(android_app& app) noexcept
        : app_{ app } {}

    ~AndroidGameHost() {
        Shutdown();
    }

    [[nodiscard]] bool Initialize() {
        if (app_.activity == nullptr || app_.activity->assetManager == nullptr ||
            app_.activity->internalDataPath == nullptr) {
            LogError("GameActivity did not provide required application storage");
            return false;
        }

        if (!packMapping_.Open(app_.activity->assetManager, kPackagedAssetPack)) {
            return false;
        }
        kb::assets::bake::BakeTargetProfile profile{};
        if (!kb::game::RuntimeHostBakeTargetProfile(profile)) {
            LogError("Android host has no valid package target identity");
            return false;
        }
        targetProfileId_.assign(profile.identifier);
        pack_ = std::make_shared<kb::assets::bake::RuntimeAssetPack>();
        const kb::assets::bake::RuntimeAssetPackStatus mountStatus =
            pack_->MountMemory(packMapping_.Bytes(), profile);
        if (mountStatus != kb::assets::bake::RuntimeAssetPackStatus::Success) {
            __android_log_print(
                ANDROID_LOG_ERROR,
                kLogTag.data(),
                "asset pack mount failed: %.*s",
                static_cast<int>(kb::assets::bake::ToString(mountStatus).size()),
                kb::assets::bake::ToString(mountStatus).data());
            return false;
        }
        std::string providerError;
        shaderProvider_ = kb::render::RuntimeAssetShaderProvider::Create(pack_, providerError);
        if (shaderProvider_ == nullptr || !renderer_.SetShaderBinaryProvider(shaderProvider_)) {
            LogError(providerError.empty() ? "packaged shader provider could not be configured" : providerError);
            return false;
        }

        storageRoot_ = app_.activity->internalDataPath;
        std::ostringstream projectError;
        if (!kb::game::ReadMountedGameProjectRuntime(
                pack_, storageRoot_, {}, projectRuntime_, projectError)) {
            LogError(projectError.str());
            return false;
        }
        kb::game::PackagedRuntimeModules staticModules{};
        std::string moduleError;
        if (!kb::game::CreatePackagedRuntimeModules(
                projectRuntime_.descriptor, staticModules, moduleError)) {
            LogError(moduleError);
            return false;
        }
        scriptModule_ = staticModules.script;
        scene_ = std::make_unique<kb::scene::Scene>(
            std::move(projectRuntime_.descriptor), std::move(staticModules.modules));
        const bool scriptActive = scene_->IsModuleActive("Script");
        if (scriptActive &&
            (scriptModule_ == nullptr || !scriptModule_->Succeeded() || scriptModule_->Host() == nullptr)) {
            LogError("script module initialization failed");
            if (scriptModule_ != nullptr) {
                for (const std::string& diagnostic : scriptModule_->Diagnostics()) {
                    LogError(diagnostic);
                }
            }
            return false;
        }
        std::filesystem::path scenePath;
        std::size_t discoveredAssets = 0U;
        if (!kb::game::LoadGameProjectScene(
                projectRuntime_, *scene_, scenePath, discoveredAssets, projectError)) {
            LogError(projectError.str());
            return false;
        }
        std::ostringstream ready;
        ready << "Android project loaded: scene=" << scenePath.generic_string()
              << " entities=" << scene_->Entities().Count()
              << " assets=" << discoveredAssets
              << " modules=" << scene_->ActiveModuleCount();
        LogInfo(ready.str());

        app_.userData = this;
        app_.onAppCmd = &AndroidGameHost::OnAppCommand;
        android_app_set_key_event_filter(&app_, nullptr);
        android_app_set_motion_event_filter(&app_, nullptr);
        LogInfo("Android runtime host initialized");
        return true;
    }

    void Run() {
        while (app_.destroyRequested == 0 && !failed_) {
            const int timeoutMilliseconds = CanRender() ? 0 : -1;
            int pollResult = 0;
            do {
                android_poll_source* source = nullptr;
                pollResult = ALooper_pollOnce(
                    pollResult == 0 ? timeoutMilliseconds : 0,
                    nullptr,
                    nullptr,
                    reinterpret_cast<void**>(&source));
                if (source != nullptr) {
                    source->process(&app_, source);
                }
                if (app_.destroyRequested != 0 || failed_) {
                    break;
                }
            } while (pollResult >= 0);

            ApplyWindowState();
            DrainInput();
            if (CanRender()) {
                RenderFrame();
            }
        }
        Shutdown();
    }

private:
    static void OnAppCommand(android_app* app, int32_t command) noexcept {
        if (app == nullptr || app->userData == nullptr) {
            return;
        }
        static_cast<AndroidGameHost*>(app->userData)->HandleAppCommand(command);
    }

    void HandleAppCommand(int32_t command) noexcept {
        switch (command) {
        case APP_CMD_INIT_WINDOW:
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONTENT_RECT_CHANGED:
            windowChanged_ = true;
            break;
        case APP_CMD_TERM_WINDOW:
            windowTerminated_ = true;
            break;
        case APP_CMD_GAINED_FOCUS:
            focused_ = true;
            break;
        case APP_CMD_LOST_FOCUS:
            focused_ = false;
            tickClockReady_ = false;
            break;
        case APP_CMD_RESUME:
            resumed_ = true;
            break;
        case APP_CMD_PAUSE:
        case APP_CMD_STOP:
            resumed_ = false;
            tickClockReady_ = false;
            break;
        case APP_CMD_START:
        case APP_CMD_SAVE_STATE:
        case APP_CMD_CONFIG_CHANGED:
        case APP_CMD_LOW_MEMORY:
        case APP_CMD_WINDOW_REDRAW_NEEDED:
        case APP_CMD_DESTROY:
            break;
        default:
            break;
        }
    }

    void ApplyWindowState() {
        if (windowTerminated_) {
            ShutdownRenderer();
            surface_.SetWindow(nullptr);
            windowTerminated_ = false;
            windowChanged_ = false;
        }
        if (!windowChanged_ || app_.window == nullptr) {
            return;
        }

        surface_.SetWindow(app_.window);
        if (!renderer_.IsInitialized()) {
            kb::render::DisplayConfig config{};
            config.writableStorageRoot = storageRoot_;
            config.enableEditorRendering = false;
            if (!renderer_.Initialize(surface_, &config)) {
                LogError("renderer initialization failed");
                failed_ = true;
                return;
            }
            if (!HasRequiredTextureCapabilities()) {
                std::ostringstream message;
                message << "profile=" << targetProfileId_
                        << " required-texture-capability=missing";
                LogError(message.str());
                failed_ = true;
                return;
            }
        } else {
            renderer_.OnResize(surface_.Width(), surface_.Height());
        }
        windowChanged_ = false;
    }

    void DrainInput() noexcept {
        kb::input::InputDeviceState* state = nullptr;
        if (scene_ != nullptr) {
            state = &scene_->Input().MutableDeviceState();
            state->SetHasFocus(focused_);
            if (!focused_) {
                state->Reset();
                state->SetHasFocus(false);
                touchPoints_.clear();
            }
        }
        android_input_buffer* const input = android_app_swap_input_buffers(&app_);
        if (input == nullptr) {
            return;
        }
        if (state != nullptr && focused_) {
            for (std::uint64_t index = 0U; index < input->keyEventsCount; ++index) {
                const GameActivityKeyEvent& event = input->keyEvents[index];
                const kb::input::InputKey key = AndroidKey(event.keyCode);
                if (key != kb::input::InputKey::None &&
                    (event.action == AKEY_EVENT_ACTION_DOWN || event.action == AKEY_EVENT_ACTION_UP)) {
                    state->SetKeyDown(key, event.action == AKEY_EVENT_ACTION_DOWN);
                    if (kb::input::DeviceKindOf(key) == kb::input::InputDeviceKind::Gamepad) {
                        state->SetGamepadConnected(0U, true);
                    }
                }
            }
            for (std::uint64_t index = 0U; index < input->motionEventsCount; ++index) {
                ApplyTouchEvent(input->motionEvents[index]);
            }
            state->SetTouchPoints(touchPoints_);
            if (!touchPoints_.empty()) {
                state->SetPointerPosition(touchPoints_.front().x, touchPoints_.front().y);
            }
        }
        android_app_clear_motion_events(input);
        android_app_clear_key_events(input);
    }

    void ApplyTouchEvent(const GameActivityMotionEvent& event) noexcept {
        if ((event.source & AINPUT_SOURCE_TOUCHSCREEN) != AINPUT_SOURCE_TOUCHSCREEN) {
            return;
        }
        const int32_t action = event.action & AMOTION_EVENT_ACTION_MASK;
        const std::uint32_t actionIndex = static_cast<std::uint32_t>(
            (event.action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
            AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
        touchPoints_.clear();
        const std::uint32_t count = (std::min)(
            event.pointerCount,
            static_cast<std::uint32_t>(kb::input::InputDeviceState::kMaxTouchPoints));
        touchPoints_.reserve(count);
        for (std::uint32_t index = 0U; index < count; ++index) {
            const GameActivityPointerAxes& pointer = event.pointers[index];
            kb::input::InputTouchPhase phase = kb::input::InputTouchPhase::Moved;
            if ((action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN) &&
                index == actionIndex) {
                phase = kb::input::InputTouchPhase::Began;
            } else if (action == AMOTION_EVENT_ACTION_CANCEL ||
                ((action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP) &&
                    index == actionIndex)) {
                phase = kb::input::InputTouchPhase::Ended;
            }
            touchPoints_.push_back(kb::input::InputTouchPoint{
                .id = pointer.id < 0 ? 0U : static_cast<std::uint32_t>(pointer.id),
                .x = GameActivityPointerAxes_getX(&pointer),
                .y = GameActivityPointerAxes_getY(&pointer),
                .phase = phase,
            });
        }
    }

    void FinalizeInputFrame() noexcept {
        touchPoints_.erase(
            std::remove_if(
                touchPoints_.begin(),
                touchPoints_.end(),
                [](const kb::input::InputTouchPoint& point) {
                    return point.phase == kb::input::InputTouchPhase::Ended;
                }),
            touchPoints_.end());
        for (kb::input::InputTouchPoint& point : touchPoints_) {
            point.phase = kb::input::InputTouchPhase::Moved;
        }
        if (scene_ != nullptr) {
            scene_->Input().MutableDeviceState().SetTouchPoints(touchPoints_);
        }
    }

    [[nodiscard]] bool CanRender() const noexcept {
        return resumed_ && focused_ && app_.window != nullptr &&
            renderer_.IsInitialized() && surface_.Width() > 0U && surface_.Height() > 0U;
    }

    void RenderFrame() {
        if (scene_ == nullptr) {
            failed_ = true;
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        const float deltaSeconds = tickClockReady_
            ? kb::game::RuntimeDeltaSeconds(previousTick_, now)
            : 0.0F;
        previousTick_ = now;
        tickClockReady_ = true;
        static_cast<void>(scene_->Runtime().Update(deltaSeconds));
        FinalizeInputFrame();
        if (renderer_.BeginFrame()) {
            renderer_.SubmitScene(*scene_);
            renderer_.EndFrame();
            if (!firstFrameReported_) {
                std::ostringstream message;
                message << "profile=" << targetProfileId_ << " first-frame=rendered";
                LogInfo(message.str());
                firstFrameReported_ = true;
            }
        }
    }

    [[nodiscard]] bool HasRequiredTextureCapabilities() const noexcept {
        const bgfx::Caps* capabilities = bgfx::getCaps();
        const auto supportsTexture2D = [capabilities](bgfx::TextureFormat::Enum format) {
            return (capabilities->formats[static_cast<std::size_t>(format)] &
                    static_cast<std::uint32_t>(BGFX_CAPS_FORMAT_TEXTURE_2D)) != 0U;
        };
        if (targetProfileId_ == "Android.ASTC.arm64") {
            return supportsTexture2D(bgfx::TextureFormat::ASTC4x4);
        }
        if (targetProfileId_ == "Android.ETC2.arm64") {
            return supportsTexture2D(bgfx::TextureFormat::ETC2) &&
                supportsTexture2D(bgfx::TextureFormat::ETC2A);
        }
        return false;
    }

    void ShutdownRenderer() noexcept {
        if (renderer_.IsInitialized()) {
            if (scene_ != nullptr) {
                renderer_.ReleaseScene(*scene_);
            }
            renderer_.Shutdown();
        }
        tickClockReady_ = false;
    }

    void Shutdown() noexcept {
        if (!scriptShutdownDispatched_ && scene_ != nullptr &&
            scene_->IsModuleActive("Script") && scriptModule_ != nullptr &&
            scriptModule_->Host() != nullptr) {
            if (!scriptModule_->Host()->DispatchShutdownLifecycle(0.0F)) {
                LogError("script shutdown lifecycle could not be dispatched");
            }
            scriptShutdownDispatched_ = true;
        }
        ShutdownRenderer();
        scene_.reset();
        scriptModule_ = nullptr;
        static_cast<void>(renderer_.SetShaderBinaryProvider({}));
        shaderProvider_.reset();
        if (pack_ != nullptr) {
            pack_->Unmount();
            pack_.reset();
        }
        packMapping_.Reset();
        if (app_.userData == this) {
            app_.userData = nullptr;
            app_.onAppCmd = nullptr;
        }
    }

    android_app& app_;
    AndroidAssetMapping packMapping_;
    std::shared_ptr<kb::assets::bake::RuntimeAssetPack> pack_;
    std::shared_ptr<kb::render::RuntimeAssetShaderProvider> shaderProvider_;
    kb::game::GameProjectRuntime projectRuntime_;
    std::unique_ptr<kb::scene::Scene> scene_;
    kb::script::ScriptModule* scriptModule_ = nullptr;
    AndroidRenderSurface surface_;
    kb::render::Renderer renderer_;
    std::string storageRoot_;
    std::string targetProfileId_;
    bool resumed_ = false;
    bool focused_ = false;
    bool windowChanged_ = false;
    bool windowTerminated_ = false;
    bool failed_ = false;
    bool tickClockReady_ = false;
    bool scriptShutdownDispatched_ = false;
    bool firstFrameReported_ = false;
    std::chrono::steady_clock::time_point previousTick_{};
    std::vector<kb::input::InputTouchPoint> touchPoints_;
};

} // namespace

extern "C" void android_main(android_app* app) {
    if (app == nullptr) {
        return;
    }
    try {
        AndroidGameHost host{ *app };
        if (host.Initialize()) {
            host.Run();
        }
    } catch (const std::exception& error) {
        __android_log_print(
            ANDROID_LOG_ERROR, kLogTag.data(), "unrecoverable error: %s", error.what());
    } catch (...) {
        LogError("unrecoverable native error");
    }
}
