#include "GameProjectRuntime.hpp"
#include "PackagedGameRuntime.hpp"

#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/scene/Scene.hpp"
#include "kb/render/DisplayConfig.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"

#include <bgfx/bgfx.h>
#include <emscripten/emscripten.h>
#include <emscripten/fetch.h>
#include <emscripten/html5.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <sstream>
#include <string_view>

namespace {

constexpr const char* kCanvas = "#canvas";
constexpr const char* kAssetPackUrl = "Game.kbpack";

void SetBrowserBackendReady() noexcept {
#if defined(KB_WEB_RENDERER_WEBGPU)
    EM_ASM({
        document.documentElement.dataset.kbBackend = 'webgpu';
        location.hash = 'kb-backend-webgpu';
    });
#else
    EM_ASM({
        document.documentElement.dataset.kbBackend = 'webgl';
        location.hash = 'kb-backend-webgl';
    });
#endif
    EM_ASM({ delete document.documentElement.dataset.kbError; });
}

void SetBrowserFirstFrameReady() noexcept {
    EM_ASM({
        document.documentElement.dataset.kbFirstFrame = 'true';
        location.hash = 'kb-ready-' + document.documentElement.dataset.kbBackend;
    });
}

void SetBrowserStartupError(const char* code) noexcept {
    EM_ASM({
        const error = UTF8ToString($0);
        document.documentElement.dataset.kbError = error;
        location.hash = 'kb-error-' + encodeURIComponent(error);
    }, code);
}

[[nodiscard]] kb::input::InputKey WebKey(std::string_view code) noexcept {
    using kb::input::InputKey;
    if (code.size() == 4U && code.starts_with("Key") && code[3] >= 'A' && code[3] <= 'Z') {
        return static_cast<InputKey>(
            static_cast<std::uint16_t>(InputKey::A) + static_cast<std::uint16_t>(code[3] - 'A'));
    }
    if (code.size() == 6U && code.starts_with("Digit") && code[5] >= '0' && code[5] <= '9') {
        return static_cast<InputKey>(
            static_cast<std::uint16_t>(InputKey::Num0) + static_cast<std::uint16_t>(code[5] - '0'));
    }
    if (code.size() >= 2U && code[0] == 'F') {
        std::uint32_t number = 0U;
        for (const char digit : code.substr(1U)) {
            if (digit < '0' || digit > '9') return InputKey::None;
            number = number * 10U + static_cast<std::uint32_t>(digit - '0');
        }
        if (number >= 1U && number <= 12U) {
            return static_cast<InputKey>(
                static_cast<std::uint16_t>(InputKey::F1) + static_cast<std::uint16_t>(number - 1U));
        }
    }
    if (code == "Escape") return InputKey::Escape;
    if (code == "Tab") return InputKey::Tab;
    if (code == "CapsLock") return InputKey::CapsLock;
    if (code == "Space") return InputKey::Space;
    if (code == "Enter" || code == "NumpadEnter") return InputKey::Enter;
    if (code == "Backspace") return InputKey::Backspace;
    if (code == "Delete") return InputKey::Delete;
    if (code == "Insert") return InputKey::Insert;
    if (code == "Home") return InputKey::Home;
    if (code == "End") return InputKey::End;
    if (code == "PageUp") return InputKey::PageUp;
    if (code == "PageDown") return InputKey::PageDown;
    if (code == "ArrowUp") return InputKey::ArrowUp;
    if (code == "ArrowDown") return InputKey::ArrowDown;
    if (code == "ArrowLeft") return InputKey::ArrowLeft;
    if (code == "ArrowRight") return InputKey::ArrowRight;
    if (code == "ShiftLeft") return InputKey::LeftShift;
    if (code == "ShiftRight") return InputKey::RightShift;
    if (code == "ControlLeft") return InputKey::LeftControl;
    if (code == "ControlRight") return InputKey::RightControl;
    if (code == "AltLeft") return InputKey::LeftAlt;
    if (code == "AltRight") return InputKey::RightAlt;
    return InputKey::None;
}

class WebRenderSurface final : public kb::render::RenderSurface {
public:
    [[nodiscard]] bool RefreshSize() noexcept {
        double cssWidth = 0.0;
        double cssHeight = 0.0;
        if (emscripten_get_element_css_size(kCanvas, &cssWidth, &cssHeight) != EMSCRIPTEN_RESULT_SUCCESS) {
            return false;
        }
        const double scale = (std::max)(1.0, emscripten_get_device_pixel_ratio());
        const std::uint32_t width = (std::max)(1U, static_cast<std::uint32_t>(cssWidth * scale));
        const std::uint32_t height = (std::max)(1U, static_cast<std::uint32_t>(cssHeight * scale));
        if (width == width_ && height == height_) return false;
        width_ = width;
        height_ = height;
        static_cast<void>(emscripten_set_canvas_element_size(kCanvas, width, height));
        return true;
    }

    [[nodiscard]] std::uint32_t Width() const noexcept override { return width_; }
    [[nodiscard]] std::uint32_t Height() const noexcept override { return height_; }
    [[nodiscard]] void* NativeWindowHandle() const noexcept override {
        return const_cast<char*>(kCanvas);
    }
    [[nodiscard]] void* NativeDisplayHandle() const noexcept override { return nullptr; }

private:
    std::uint32_t width_ = 1U;
    std::uint32_t height_ = 1U;
};

class WebGameHost {
public:
    WebGameHost() { static_cast<void>(surface_.RefreshSize()); }
    ~WebGameHost() { Shutdown(); }

    void StartFetch() {
        emscripten_fetch_attr_t attributes{};
        emscripten_fetch_attr_init(&attributes);
        std::snprintf(attributes.requestMethod, sizeof(attributes.requestMethod), "GET");
        attributes.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
        attributes.userData = this;
        attributes.onsuccess = &WebGameHost::OnFetchSuccess;
        attributes.onerror = &WebGameHost::OnFetchError;
        fetch_ = emscripten_fetch(&attributes, kAssetPackUrl);
        if (fetch_ == nullptr) Fail("fetch-start", "asset pack request could not be started");
    }

private:
    static void OnFetchSuccess(emscripten_fetch_t* fetch) {
        auto* host = static_cast<WebGameHost*>(fetch->userData);
        if (host != nullptr) {
            EM_ASM({ location.hash = 'kb-pack-loaded'; });
            host->Initialize(fetch);
        }
    }

    static void OnFetchError(emscripten_fetch_t* fetch) {
        auto* host = static_cast<WebGameHost*>(fetch->userData);
        if (host != nullptr) {
            std::ostringstream message;
            message << "asset pack request failed: HTTP " << fetch->status;
            host->Fail("fetch-http", message.str());
        }
    }

    void Initialize(emscripten_fetch_t* fetch) {
        if (fetch == nullptr || fetch->data == nullptr || fetch->numBytes <= 0) {
            Fail("pack-empty", "asset pack response was empty");
            return;
        }
        kb::assets::bake::BakeTargetProfile profile{};
        if (!kb::game::RuntimeHostBakeTargetProfile(profile)) {
            Fail("target-identity", "browser host has no valid package target identity");
            return;
        }
        pack_ = std::make_shared<kb::assets::bake::RuntimeAssetPack>();
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(fetch->data);
        const kb::assets::bake::RuntimeAssetPackStatus status = pack_->MountMemory(
            std::span<const std::uint8_t>{ bytes, static_cast<std::size_t>(fetch->numBytes) },
            profile);
        if (status != kb::assets::bake::RuntimeAssetPackStatus::Success) {
            std::ostringstream message;
            message << "asset pack mount failed: " << kb::assets::bake::ToString(status);
            Fail("pack-mount", message.str());
            return;
        }
        std::ostringstream diagnostics;
        if (!runtime_.Initialize(pack_, {}, renderer_, diagnostics)) {
            Fail("runtime-init", diagnostics.str());
            return;
        }
        EM_ASM({ location.hash = 'kb-runtime-ready'; });

        kb::render::DisplayConfig config{};
        config.enableEditorRendering = false;
#if defined(KB_WEB_RENDERER_WEBGPU)
        constexpr bgfx::RendererType::Enum kRequiredRenderer = bgfx::RendererType::WebGPU;
#else
        constexpr bgfx::RendererType::Enum kRequiredRenderer = bgfx::RendererType::OpenGLES;
#endif
        config.preferredBgfxRendererType = static_cast<std::int32_t>(kRequiredRenderer);
        if (!renderer_.Initialize(surface_, &config) ||
            renderer_.CapabilityReport().selectedBackend != kRequiredRenderer) {
            Fail("renderer-init", "required browser renderer could not be initialized");
            return;
        }
        SetBrowserBackendReady();
        RegisterInput();
        previousTick_ = std::chrono::steady_clock::now();
        std::fprintf(stdout, "kb_game_web: %s", diagnostics.str().c_str());
        emscripten_set_main_loop_arg(&WebGameHost::Frame, this, 0, false);
    }

    void RegisterInput() noexcept {
        static_cast<void>(emscripten_set_keydown_callback(
            EMSCRIPTEN_EVENT_TARGET_DOCUMENT, this, true, &WebGameHost::OnKey));
        static_cast<void>(emscripten_set_keyup_callback(
            EMSCRIPTEN_EVENT_TARGET_DOCUMENT, this, true, &WebGameHost::OnKey));
        static_cast<void>(emscripten_set_mousedown_callback(kCanvas, this, true, &WebGameHost::OnMouse));
        static_cast<void>(emscripten_set_mouseup_callback(kCanvas, this, true, &WebGameHost::OnMouse));
        static_cast<void>(emscripten_set_mousemove_callback(kCanvas, this, true, &WebGameHost::OnMouse));
        static_cast<void>(emscripten_set_wheel_callback(kCanvas, this, true, &WebGameHost::OnWheel));
        static_cast<void>(emscripten_set_focus_callback(
            EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, &WebGameHost::OnFocus));
        static_cast<void>(emscripten_set_blur_callback(
            EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, &WebGameHost::OnFocus));
        if (kb::scene::Scene* scene = runtime_.Scene(); scene != nullptr) {
            scene->Input().MutableDeviceState().SetHasFocus(true);
        }
    }

    static void Frame(void* userData) {
        auto* host = static_cast<WebGameHost*>(userData);
        if (host == nullptr || host->frameInProgress_) return;
        host->frameInProgress_ = true;
        if (host->surface_.RefreshSize()) {
            host->renderer_.OnResize(host->surface_.Width(), host->surface_.Height());
        }
        const auto now = std::chrono::steady_clock::now();
        const float delta = kb::game::RuntimeDeltaSeconds(host->previousTick_, now);
        host->previousTick_ = now;
        bool frameSubmitted = false;
        if (!host->runtime_.Tick(host->renderer_, delta, &frameSubmitted)) {
            host->frameInProgress_ = false;
            emscripten_cancel_main_loop();
            host->Shutdown();
            return;
        }
        if (frameSubmitted && !host->firstFrameReported_) {
            host->firstFrameReported_ = true;
            SetBrowserFirstFrameReady();
        }
        if (kb::scene::Scene* scene = host->runtime_.Scene(); scene != nullptr) {
            auto& state = scene->Input().MutableDeviceState();
            state.SetAnalog(kb::input::InputKey::MouseX, 0.0F);
            state.SetAnalog(kb::input::InputKey::MouseY, 0.0F);
            state.SetAnalog(kb::input::InputKey::MouseWheel, 0.0F);
        }
        host->frameInProgress_ = false;
    }

    static EM_BOOL OnKey(int eventType, const EmscriptenKeyboardEvent* event, void* userData) {
        auto* host = static_cast<WebGameHost*>(userData);
        kb::scene::Scene* scene = host == nullptr ? nullptr : host->runtime_.Scene();
        if (scene == nullptr || event == nullptr) return EM_FALSE;
        const kb::input::InputKey key = WebKey(event->code);
        if (key == kb::input::InputKey::None) return EM_FALSE;
        scene->Input().MutableDeviceState().SetKeyDown(key, eventType == EMSCRIPTEN_EVENT_KEYDOWN);
        return EM_TRUE;
    }

    static EM_BOOL OnMouse(int eventType, const EmscriptenMouseEvent* event, void* userData) {
        auto* host = static_cast<WebGameHost*>(userData);
        kb::scene::Scene* scene = host == nullptr ? nullptr : host->runtime_.Scene();
        if (scene == nullptr || event == nullptr) return EM_FALSE;
        auto& state = scene->Input().MutableDeviceState();
        const float x = static_cast<float>(event->targetX);
        const float y = static_cast<float>(event->targetY);
        state.SetAnalog(kb::input::InputKey::MouseX, x - state.PointerX());
        state.SetAnalog(kb::input::InputKey::MouseY, y - state.PointerY());
        state.SetPointerPosition(x, y);
        if (eventType == EMSCRIPTEN_EVENT_MOUSEDOWN || eventType == EMSCRIPTEN_EVENT_MOUSEUP) {
            const bool down = eventType == EMSCRIPTEN_EVENT_MOUSEDOWN;
            if (event->button == 0) state.SetKeyDown(kb::input::InputKey::MouseLeft, down);
            if (event->button == 1) state.SetKeyDown(kb::input::InputKey::MouseMiddle, down);
            if (event->button == 2) state.SetKeyDown(kb::input::InputKey::MouseRight, down);
        }
        return EM_TRUE;
    }

    static EM_BOOL OnWheel(int, const EmscriptenWheelEvent* event, void* userData) {
        auto* host = static_cast<WebGameHost*>(userData);
        kb::scene::Scene* scene = host == nullptr ? nullptr : host->runtime_.Scene();
        if (scene == nullptr || event == nullptr) return EM_FALSE;
        scene->Input().MutableDeviceState().SetAnalog(
            kb::input::InputKey::MouseWheel, static_cast<float>(-event->deltaY));
        return EM_TRUE;
    }

    static EM_BOOL OnFocus(int eventType, const EmscriptenFocusEvent*, void* userData) {
        auto* host = static_cast<WebGameHost*>(userData);
        kb::scene::Scene* scene = host == nullptr ? nullptr : host->runtime_.Scene();
        if (scene == nullptr) return EM_FALSE;
        auto& state = scene->Input().MutableDeviceState();
        const bool focused = eventType == EMSCRIPTEN_EVENT_FOCUS;
        if (!focused) state.Reset();
        state.SetHasFocus(focused);
        return EM_FALSE;
    }

    void Fail(const char* code, std::string_view message) {
        SetBrowserStartupError(code);
        std::fprintf(stderr, "kb_game_web: %.*s\n", static_cast<int>(message.size()), message.data());
        Shutdown();
    }

    void Shutdown() noexcept {
        static_cast<void>(runtime_.Shutdown(renderer_, std::cerr));
        if (renderer_.IsInitialized()) renderer_.Shutdown();
        if (pack_ != nullptr) pack_->Unmount();
        pack_.reset();
        if (fetch_ != nullptr) {
            emscripten_fetch_close(fetch_);
            fetch_ = nullptr;
        }
    }

    WebRenderSurface surface_;
    emscripten_fetch_t* fetch_ = nullptr;
    std::shared_ptr<kb::assets::bake::RuntimeAssetPack> pack_;
    kb::render::Renderer renderer_;
    kb::game::PackagedGameRuntime runtime_;
    std::chrono::steady_clock::time_point previousTick_{};
    bool firstFrameReported_ = false;
    bool frameInProgress_ = false;
};

std::unique_ptr<WebGameHost> g_host;

} // namespace

int main() {
    EM_ASM({ location.hash = 'kb-starting'; });
    g_host = std::make_unique<WebGameHost>();
    g_host->StartFetch();
    return EXIT_SUCCESS;
}
