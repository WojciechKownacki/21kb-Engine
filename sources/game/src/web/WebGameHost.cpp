#include "GameProjectRuntime.hpp"
#include "PackagedGameRuntime.hpp"
#include "WebTouchInput.hpp"
#include "WebGamepadInput.hpp"
#include "WebDownloadProgress.hpp"
#include "WebDownloadBuffer.hpp"
#include "DigitalInputFrame.hpp"

#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/ui/interaction/UIInputField.hpp"
#include "kb/render/DisplayConfig.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"

#include <bgfx/bgfx.h>
#include <emscripten/emscripten.h>
#include <emscripten/eventloop.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <sstream>
#include <string_view>
#include <utility>

extern "C" EMSCRIPTEN_KEEPALIVE int kb_web_append_committed_text(
    void* host, std::uint32_t codePoint) noexcept;
extern "C" EMSCRIPTEN_KEEPALIVE int kb_web_download_begin(void* host, double length);
extern "C" EMSCRIPTEN_KEEPALIVE void* kb_web_download_append(void* host, std::size_t count);
extern "C" EMSCRIPTEN_KEEPALIVE void kb_web_download_progress(void* host);
extern "C" EMSCRIPTEN_KEEPALIVE void kb_web_download_complete(void* host);
extern "C" EMSCRIPTEN_KEEPALIVE void kb_web_download_error(void* host, int status);

// clang-format off
EM_JS(void, StartBrowserDownload, (void* host, const char* url), {
    const state = { controller: new AbortController(), cancelled: false };
    Module['kbPackDownload'] = state;
    const current = () => !state.cancelled && Module['kbPackDownload'] === state;
    const fail = (status) => {
        if (!current()) return;
        state.controller.abort();
        delete Module['kbPackDownload'];
        _kb_web_download_error(host, status);
    };
    const address = UTF8ToString(url);
    (async () => {
        let response;
        try {
            response = await fetch(address, { signal: state.controller.signal });
        } catch (error) {
            fail(0);
            return;
        }
        if (!current()) return;
        if (!response.ok) { fail(response.status); return; }
        if (!response.body) { fail(0); return; }
        // Content-Length describes encoded bytes when HTTP compression is enabled.
        const encoding = response.headers.get('Content-Encoding');
        const header = response.headers.get('Content-Length');
        const length = (!encoding || encoding.toLowerCase() === 'identity') && header !== null
            ? (/^[0-9]+$/.test(header) ? Number(header) : NaN) : 0;
        if (!_kb_web_download_begin(host, length)) return;
        const reader = response.body.getReader();
        let complete = false;
        try {
            while (current()) {
                let chunk;
                try { chunk = await reader.read(); }
                catch (error) { fail(0); return; }
                if (!current()) return;
                if (chunk.done) { complete = true; break; }
                if (!chunk.value.byteLength) continue;
                const destination = _kb_web_download_append(host, chunk.value.byteLength);
                if (!destination) return;
                // Allocation may grow WASM memory, so obtain its current view afterwards.
                HEAPU8.set(chunk.value, destination);
                _kb_web_download_progress(host);
            }
        } finally {
            reader.releaseLock();
        }
        if (complete && current()) {
            delete Module['kbPackDownload'];
            // Runtime initialization is outside the network exception handlers.
            _kb_web_download_complete(host);
        }
    })();
});

EM_JS(void, CancelBrowserDownload, (), {
    const state = Module['kbPackDownload'];
    if (!state) return;
    state.cancelled = true;
    delete Module['kbPackDownload'];
    state.controller.abort();
});

EM_JS(void, RegisterBrowserTextInput, (void* host), {
    if (Module['kbTextInput']) return;
    const control = document.createElement('textarea');
    control.tabIndex = -1;
    control.autocomplete = 'off';
    control.autocapitalize = 'off';
    control.spellcheck = false;
    control.setAttribute('aria-label', 'Game text input');
    Object.assign(control.style, {
        position: 'fixed',
        left: '0',
        top: '0',
        width: '1px',
        height: '1px',
        opacity: '0.01',
        pointerEvents: 'none',
        zIndex: '-1'
    });

    const state = { control, active: false, composing: false, suppressedComposition: null };
    const append = (text) => {
        if (!state.active || !text) return;
        for (const character of text) {
            if (!_kb_web_append_committed_text(host, character.codePointAt(0))) break;
        }
    };
    control.addEventListener('compositionstart', () => {
        state.composing = true;
        state.suppressedComposition = null;
    });
    control.addEventListener('compositionend', (event) => {
        state.composing = false;
        state.suppressedComposition = event.data || "";
        append(state.suppressedComposition);
        control.value = "";
    });
    control.addEventListener('beforeinput', (event) => {
        if (!state.active || state.composing || event.isComposing) return;
        if (event.inputType.startsWith('deleteContentBackward') ||
            event.inputType.startsWith('deleteWordBackward') ||
            event.inputType.startsWith('deleteSoftLineBackward') ||
            event.inputType.startsWith('deleteHardLineBackward')) {
            _kb_web_append_committed_text(host, 8);
            event.preventDefault();
            control.value = "";
        } else if (event.inputType.startsWith('deleteContentForward') ||
                   event.inputType.startsWith('deleteWordForward') ||
                   event.inputType.startsWith('deleteSoftLineForward') ||
                   event.inputType.startsWith('deleteHardLineForward')) {
            _kb_web_append_committed_text(host, 127);
            event.preventDefault();
            control.value = "";
        } else if (event.inputType === 'insertLineBreak' ||
                   event.inputType === 'insertParagraph') {
            _kb_web_append_committed_text(host, 10);
            event.preventDefault();
            control.value = "";
        }
    });
    control.addEventListener('input', (event) => {
        if (!state.active || state.composing || event.isComposing ||
            event.inputType.startsWith('delete')) return;
        const text = typeof event.data === 'string' ? event.data : control.value;
        const compositionInput = event.inputType === 'insertCompositionText' ||
            event.inputType === 'insertFromComposition';
        if (compositionInput && state.suppressedComposition !== null &&
            text === state.suppressedComposition) {
            state.suppressedComposition = null;
        } else {
            state.suppressedComposition = null;
            append(text);
        }
        control.value = "";
    });
    document.body.appendChild(control);
    Module['kbTextInput'] = state;
});

EM_JS(void, SetBrowserTextInputActive, (int active), {
    const state = Module['kbTextInput'];
    if (!state) return;
    state.active = !!active;
    state.composing = false;
    state.suppressedComposition = null;
    state.control.value = "";
    if (state.active) {
        state.control.focus({ preventScroll: true });
    } else if (document.activeElement === state.control) {
        state.control.blur();
    }
});

EM_JS(void, DestroyBrowserTextInput, (), {
    const state = Module['kbTextInput'];
    if (!state) return;
    state.control.remove();
    delete Module['kbTextInput'];
});

EM_JS(int, RecreateBrowserCanvas, (), {
    const previous = document.querySelector('#canvas');
    if (!previous || !previous.parentNode) return 0;
    const canvas = previous.cloneNode(false);
    if (Object.prototype.hasOwnProperty.call(previous, 'kbOriginalTouchAction')) {
        canvas.kbOriginalTouchAction = previous.kbOriginalTouchAction;
    }
    previous.replaceWith(canvas);
    Module['canvas'] = canvas;
    return 1;
});
EM_JS(void, InitializeBrowserStorage, (), { Module['kbInitializeStorage'](); });
EM_JS(int, BrowserStorageStartup, (), { return Module['kbUserStorage'].startup; });
EM_JS(char*, BrowserStorageRoot, (), { return stringToNewUTF8(Module['kbUserStorage'].root); });
EM_JS(char*, BrowserStorageDiagnostic, (), { return stringToNewUTF8(Module['kbUserStorage'].diagnostic); });
// clang-format on

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
        downloadProgress_.Start(kb::game::WebDownloadProgress::Clock::now());
        downloadActive_ = true;
        ScheduleFetchWatchdog();
        StartBrowserDownload(this, kAssetPackUrl);
    }

private:
    void CancelFetchWatchdog() noexcept {
        if (fetchWatchdog_ >= 0) {
            emscripten_clear_timeout(fetchWatchdog_);
            fetchWatchdog_ = -1;
        }
    }

    void ScheduleFetchWatchdog() {
        const auto remaining = downloadProgress_.Remaining(kb::game::WebDownloadProgress::Clock::now());
        fetchWatchdog_ = emscripten_set_timeout(&WebGameHost::CheckFetchProgress,
            static_cast<double>(remaining.count()), this);
    }

    static void CheckFetchProgress(void* pointer) {
        auto* host = static_cast<WebGameHost*>(pointer);
        host->fetchWatchdog_ = -1;
        if (host->shuttingDown_ || !host->downloadActive_) return;
        if (host->downloadProgress_.Remaining(kb::game::WebDownloadProgress::Clock::now()).count() == 0) {
            host->Fail("fetch-timeout", "asset pack download made no progress for 30 seconds; reload to retry");
        } else {
            host->ScheduleFetchWatchdog();
        }
    }

    int BeginDownload(double length) {
        if (shuttingDown_ || !downloadActive_) return 0;
        if (!std::isfinite(length) || length < 0 || std::floor(length) != length ||
            length > static_cast<double>(kDownloadLimit)) {
            Fail("pack-size", "asset pack response exceeds the in-memory package size limit");
            return 0;
        }
        if (!downloadBytes_.Reserve(static_cast<std::size_t>(length))) {
            Fail("fetch-memory", "asset pack download buffer could not be allocated");
            return 0;
        }
        return 1;
    }

    void* AppendDownload(std::size_t count) {
        if (shuttingDown_ || !downloadActive_) return nullptr;
        auto* destination = downloadBytes_.Append(count);
        if (destination == nullptr) {
            if (downloadBytes_.LastError() == kb::game::WebDownloadBuffer::Error::SizeLimit) {
                Fail("pack-size", "asset pack response exceeds the in-memory package size limit");
            } else {
                Fail("fetch-memory", "asset pack download buffer could not be allocated");
            }
        }
        return destination;
    }

    void CompleteDownload() {
        if (shuttingDown_ || !downloadActive_) return;
        downloadActive_ = false;
        CancelFetchWatchdog();
        EM_ASM({ location.hash = 'kb-pack-loaded'; });
        Initialize();
    }

    void Initialize() {
        if (downloadBytes_.Bytes().empty()) {
            Fail("pack-empty", "asset pack response was empty");
            return;
        }
        kb::assets::bake::BakeTargetProfile profile{};
        if (!kb::game::RuntimeHostBakeTargetProfile(profile)) {
            Fail("target-identity", "browser host has no valid package target identity");
            return;
        }
        pack_ = std::make_shared<kb::assets::bake::RuntimeAssetPack>();
        const kb::assets::bake::RuntimeAssetPackStatus status = pack_->MountMemory(
            downloadBytes_.Bytes(), profile);
        if (status != kb::assets::bake::RuntimeAssetPackStatus::Success) {
            std::ostringstream message;
            message << "asset pack mount failed: " << kb::assets::bake::ToString(status);
            Fail("pack-mount", message.str());
            return;
        }
        InitializeBrowserStorage();
        storageStartupTime_ = std::chrono::steady_clock::now();
        emscripten_async_call(&WebGameHost::PollStorage, this, 0);
    }

    static void PollStorage(void* userData) {
        auto* host = static_cast<WebGameHost*>(userData);
        if (host == nullptr || host->shuttingDown_) return;
        const int status = BrowserStorageStartup();
        if (status == 0 && std::chrono::steady_clock::now() - host->storageStartupTime_ < std::chrono::seconds{15}) {
            emscripten_async_call(&WebGameHost::PollStorage, host, 10);
            return;
        }
        if (status != 1) {
            char* diagnostic = BrowserStorageDiagnostic();
            const std::string message = status == 0 ? "persistent storage initialization timed out" : diagnostic;
            std::free(diagnostic);
            host->Fail("storage-init", message);
            return;
        }
        host->InitializeRuntime();
    }

    void InitializeRuntime() {
        char* root = BrowserStorageRoot();
        const std::filesystem::path storageRoot{root};
        std::free(root);
        std::ostringstream diagnostics;
        if (!runtime_.Initialize(pack_, storageRoot, renderer_, diagnostics)) {
            Fail("runtime-init", diagnostics.str());
            return;
        }
        EM_ASM({ location.hash = 'kb-runtime-ready'; });

        if (!InitializeRenderer()) {
            Fail("renderer-init", "required browser renderer could not be initialized");
            return;
        }
        if (!RegisterGraphicsEvents()) {
            Fail("graphics-events", "browser graphics loss handler could not be registered");
            return;
        }
        SetBrowserBackendReady();
        if (!RegisterInput()) {
            Fail("input-init", "browser touch handlers could not be registered");
            return;
        }
        previousTick_ = std::chrono::steady_clock::now();
        std::fprintf(stdout, "kb_game_web: %s", diagnostics.str().c_str());
        emscripten_set_main_loop_arg(&WebGameHost::Frame, this, 0, false);
    }

    bool InitializeRenderer() {
        kb::render::DisplayConfig config{};
        config.enableEditorRendering = false;
#if defined(KB_WEB_RENDERER_WEBGPU)
        constexpr bgfx::RendererType::Enum kRequiredRenderer = bgfx::RendererType::WebGPU;
#else
        constexpr bgfx::RendererType::Enum kRequiredRenderer = bgfx::RendererType::OpenGLES;
#endif
        config.preferredBgfxRendererType = static_cast<std::int32_t>(kRequiredRenderer);
        return renderer_.Initialize(surface_, &config) &&
            renderer_.CapabilityReport().selectedBackend == kRequiredRenderer;
    }

    bool RegisterGraphicsEvents() noexcept {
        graphicsEventsRegistered_ = true;
        const auto lost = emscripten_set_webglcontextlost_callback(
            kCanvas, this, true, &WebGameHost::OnGraphicsContextLost);
        const auto restored = emscripten_set_webglcontextrestored_callback(
            kCanvas, this, true, &WebGameHost::OnGraphicsContextRestored);
        return lost == EMSCRIPTEN_RESULT_SUCCESS && restored == EMSCRIPTEN_RESULT_SUCCESS;
    }

    void UnregisterGraphicsEvents() noexcept {
        if (!graphicsEventsRegistered_) return;
        static_cast<void>(emscripten_set_webglcontextlost_callback(kCanvas, nullptr, true, nullptr));
        static_cast<void>(emscripten_set_webglcontextrestored_callback(kCanvas, nullptr, true, nullptr));
        graphicsEventsRegistered_ = false;
    }

    bool RegisterInput(bool restore = false) noexcept {
        static_cast<void>(emscripten_set_keydown_callback(
            EMSCRIPTEN_EVENT_TARGET_DOCUMENT, this, true, &WebGameHost::OnKey));
        static_cast<void>(emscripten_set_keyup_callback(
            EMSCRIPTEN_EVENT_TARGET_DOCUMENT, this, true, &WebGameHost::OnKey));
        static_cast<void>(emscripten_set_mousedown_callback(kCanvas, this, true, &WebGameHost::OnMouse));
        static_cast<void>(emscripten_set_mouseup_callback(kCanvas, this, true, &WebGameHost::OnMouse));
        static_cast<void>(emscripten_set_mousemove_callback(kCanvas, this, true, &WebGameHost::OnMouse));
        static_cast<void>(emscripten_set_wheel_callback(kCanvas, this, true, &WebGameHost::OnWheel));
        static_cast<void>(emscripten_set_pointerlockchange_callback(
            EMSCRIPTEN_EVENT_TARGET_DOCUMENT, this, true, &WebGameHost::OnPointerLock));
        if (restore) {
            if (!touches_.Resume()) return false;
        } else if (!touches_.Attach(kCanvas, this, &WebGameHost::ResolveInput,
                &WebGameHost::OnTouchBegan, &WebGameHost::OnInputFocus)) return false;
        RegisterBrowserTextInput(this);
        return true;
    }

    static void Frame(void* userData) {
        auto* host = static_cast<WebGameHost*>(userData);
        if (host == nullptr || host->frameInProgress_ || host->shuttingDown_) return;
        host->frameInProgress_ = true;
        if (host->graphicsContextLost_ && !host->RestoreGraphics()) {
            host->frameInProgress_ = false;
            return;
        }
        if (host->surface_.RefreshSize()) {
            host->renderer_.OnResize(host->surface_.Width(), host->surface_.Height());
        }
        const auto now = std::chrono::steady_clock::now();
        const float delta = kb::game::RuntimeDeltaSeconds(host->previousTick_, now);
        host->previousTick_ = now;
        if (kb::scene::Scene* scene = host->runtime_.Scene(); scene != nullptr) {
            scene->Input().MutableDeviceState().SetPointerViewportExtent(
                host->surface_.Width(), host->surface_.Height());
            kb::game::PollWebGamepads(scene->Input().MutableDeviceState());
            host->digital_.Publish(scene->Input().MutableDeviceState());
        }
        host->touches_.SetViewport(host->surface_.Width(), host->surface_.Height());
        bool frameSubmitted = false;
        if (!host->runtime_.Tick(host->renderer_, delta, std::cerr, &frameSubmitted)) {
            host->frameInProgress_ = false;
            emscripten_cancel_main_loop();
            host->Shutdown();
            return;
        }
        if (host->graphicsContextLost_) {
            host->frameInProgress_ = false;
            return;
        }
        if (frameSubmitted && !host->firstFrameReported_) {
            host->firstFrameReported_ = true;
            SetBrowserFirstFrameReady();
        }
        if (frameSubmitted && host->healthyGraphicsFrames_ < 300U && ++host->healthyGraphicsFrames_ == 300U) {
            host->graphicsRecreationAttempts_ = 0U;
        }
        if (kb::scene::Scene* scene = host->runtime_.Scene(); scene != nullptr) {
            host->SetTextInputActive(scene->UI().HasFocusedTextInput());
            auto& state = scene->Input().MutableDeviceState();
            if (host->inputResumePending_) {
                state.SetHasFocus(EM_ASM_INT({ return document.hasFocus() ? 1 : 0; }) != 0);
                host->inputResumePending_ = false;
            }
            state.SetAnalog(kb::input::InputKey::MouseX, 0.0F);
            state.SetAnalog(kb::input::InputKey::MouseY, 0.0F);
            state.SetAnalog(kb::input::InputKey::MouseWheel, 0.0F);
            state.ClearTextInput();
        }
        host->touches_.FinishFrame();
        host->frameInProgress_ = false;
    }

    static EM_BOOL OnGraphicsContextLost(int, const void*, void* userData) {
        auto* host = static_cast<WebGameHost*>(userData);
        if (host != nullptr && !host->graphicsContextLost_) {
            host->graphicsContextLost_ = true;
            host->graphicsContextRestored_ = false;
            host->graphicsResourcesReleased_ = false;
            host->healthyGraphicsFrames_ = 0U;
            host->graphicsLossTime_ = std::chrono::steady_clock::now();
            host->touches_.Suspend();
            host->digital_.Clear();
            host->SetTextInputActive(false);
            SetBrowserStartupError("graphics-context-lost");
            std::fputs("kb_game_web: graphics context lost; waiting for browser restoration\n", stderr);
        }
        return EM_TRUE;
    }

    static EM_BOOL OnGraphicsContextRestored(int, const void*, void* userData) {
        auto* host = static_cast<WebGameHost*>(userData);
        if (host != nullptr && host->graphicsContextLost_) host->graphicsContextRestored_ = true;
        return EM_FALSE;
    }

    bool RestoreGraphics() {
        // Releasing the SDK context also removes canvas event handlers.
        // Do this outside the browser event callback and restore our handlers.
        if (!graphicsResourcesReleased_) {
            runtime_.ReleaseRenderResources(renderer_);
            renderer_.Shutdown();
            graphicsResourcesReleased_ = true;
            if (!RegisterGraphicsEvents()) {
                Fail("graphics-events", "browser graphics handlers could not be restored");
                return false;
            }
        }
        if (!graphicsContextRestored_) {
            if (std::chrono::steady_clock::now() - graphicsLossTime_ < std::chrono::seconds{2}) {
                return false;
            }
            if (graphicsRecreationAttempts_ >= 3U) {
                Fail("graphics-restore-limit", "browser graphics repeatedly failed; reload the game");
                return false;
            }
            ++graphicsRecreationAttempts_;
            UnregisterGraphicsEvents();
            if (RecreateBrowserCanvas() == 0) {
                Fail("graphics-surface", "browser graphics surface could not be recreated; reload the game");
                return false;
            }
            std::fputs("kb_game_web: replacing unrestored graphics surface\n", stderr);
        }
        static_cast<void>(surface_.RefreshSize());
        if (!InitializeRenderer() || !RegisterGraphicsEvents() || !RegisterInput(true)) {
            Fail("graphics-restore", "browser graphics resources could not be rebuilt; reload the game");
            return false;
        }
        previousTick_ = std::chrono::steady_clock::now();
        graphicsContextLost_ = false;
        inputResumePending_ = true;
        firstFrameReported_ = false;
        SetBrowserBackendReady();
        std::fputs("kb_game_web: graphics resources restored\n", stderr);
        return true;
    }

    static EM_BOOL OnKey(int eventType, const EmscriptenKeyboardEvent* event, void* userData) {
        auto* host = static_cast<WebGameHost*>(userData);
        kb::scene::Scene* scene = host == nullptr ? nullptr : host->runtime_.Scene();
        if (scene == nullptr || event == nullptr || host->graphicsContextLost_) return EM_FALSE;
        const kb::input::InputKey key = WebKey(event->code);
        if (key == kb::input::InputKey::None) return EM_FALSE;
        static_cast<void>(host->digital_.Queue(scene->Input().MutableDeviceState(),
            key, eventType == EMSCRIPTEN_EVENT_KEYDOWN));
        return host->textInputActive_ && eventType == EMSCRIPTEN_EVENT_KEYDOWN
            ? EM_FALSE
            : EM_TRUE;
    }

    static EM_BOOL OnPointerLock(int, const EmscriptenPointerlockChangeEvent*, void* userData) {
        auto* host = static_cast<WebGameHost*>(userData);
        if (host == nullptr) return EM_FALSE;
        host->digital_.Clear();
        host->pointerNeedsBaseline_ = true;
        if (auto* scene = host->runtime_.Scene()) {
            auto& state = scene->Input().MutableDeviceState();
            for (std::size_t i = 0; i < kb::input::InputDeviceState::kKeyCount; ++i) {
                const auto key = static_cast<kb::input::InputKey>(i);
                if (kb::input::DeviceKindOf(key) != kb::input::InputDeviceKind::Gamepad)
                    state.SetKeyDown(key, false);
            }
            state.SetAnalog(kb::input::InputKey::MouseX, 0.0F);
            state.SetAnalog(kb::input::InputKey::MouseY, 0.0F);
        }
        return EM_FALSE;
    }

    static EM_BOOL OnMouse(int eventType, const EmscriptenMouseEvent* event, void* userData) {
        auto* host = static_cast<WebGameHost*>(userData);
        kb::scene::Scene* scene = host == nullptr ? nullptr : host->runtime_.Scene();
        if (scene == nullptr || event == nullptr || host->graphicsContextLost_) return EM_FALSE;
        auto& state = scene->Input().MutableDeviceState();
        double cssWidth = 0.0;
        double cssHeight = 0.0;
        const bool hasCssExtent =
            emscripten_get_element_css_size(kCanvas, &cssWidth, &cssHeight) ==
                EMSCRIPTEN_RESULT_SUCCESS &&
            cssWidth > 0.0 && cssHeight > 0.0;
        const float scaleX = hasCssExtent ? static_cast<float>(host->surface_.Width() / cssWidth) : 1.0F;
        const float scaleY = hasCssExtent ? static_cast<float>(host->surface_.Height() / cssHeight) : 1.0F;
        EmscriptenPointerlockChangeEvent lock{};
        const bool locked = emscripten_get_pointerlock_status(&lock) == EMSCRIPTEN_RESULT_SUCCESS && lock.isActive;
        const float x = locked ? state.PointerX() : static_cast<float>(event->targetX) * scaleX;
        const float y = locked ? state.PointerY() : static_cast<float>(event->targetY) * scaleY;
        if (eventType == EMSCRIPTEN_EVENT_MOUSEMOVE) {
            const float dx = locked ? static_cast<float>(event->movementX) * scaleX :
                (host->pointerNeedsBaseline_ ? 0.0F : x - state.PointerX());
            const float dy = locked ? static_cast<float>(event->movementY) * scaleY :
                (host->pointerNeedsBaseline_ ? 0.0F : y - state.PointerY());
            state.SetAnalog(kb::input::InputKey::MouseX, state.GetValue(kb::input::InputKey::MouseX) + dx);
            state.SetAnalog(kb::input::InputKey::MouseY, state.GetValue(kb::input::InputKey::MouseY) + dy);
            host->pointerNeedsBaseline_ = false;
        }
        state.SetPointerPosition(x, y);
        host->digital_.SetPointerPosition(x, y);
        if (eventType == EMSCRIPTEN_EVENT_MOUSEDOWN || eventType == EMSCRIPTEN_EVENT_MOUSEUP) {
            const bool down = eventType == EMSCRIPTEN_EVENT_MOUSEDOWN;
            if (event->button == 0) {
                static_cast<void>(host->digital_.Queue(state, kb::input::InputKey::MouseLeft, down, true, x, y));
                if (down) {
                    const kb::scene::SceneEntity target = scene->UI().HitTest({x, y});
                    if (target.IsValid() &&
                        scene->Components().UI().Has<kb::scene::UIInputField>(target)) {
                        host->SetTextInputActive(true);
                    }
                }
            }
            if (event->button == 1)
                static_cast<void>(host->digital_.Queue(state, kb::input::InputKey::MouseMiddle, down, true, x, y));
            if (event->button == 2)
                static_cast<void>(host->digital_.Queue(state, kb::input::InputKey::MouseRight, down, true, x, y));
        }
        return EM_TRUE;
    }

    static EM_BOOL OnWheel(int, const EmscriptenWheelEvent* event, void* userData) {
        auto* host = static_cast<WebGameHost*>(userData);
        kb::scene::Scene* scene = host == nullptr ? nullptr : host->runtime_.Scene();
        if (scene == nullptr || event == nullptr || host->graphicsContextLost_) return EM_FALSE;
        scene->Input().MutableDeviceState().SetAnalog(
            kb::input::InputKey::MouseWheel, static_cast<float>(-event->deltaY));
        return EM_TRUE;
    }

    static kb::input::InputDeviceState* ResolveInput(void* userData) noexcept {
        auto* host = static_cast<WebGameHost*>(userData);
        kb::scene::Scene* scene = host == nullptr ? nullptr : host->runtime_.Scene();
        return scene == nullptr ? nullptr : &scene->Input().MutableDeviceState();
    }

    static void OnTouchBegan(void* userData, float x, float y) noexcept {
        auto* host = static_cast<WebGameHost*>(userData);
        kb::scene::Scene* scene = host == nullptr ? nullptr : host->runtime_.Scene();
        if (scene == nullptr) return;
        const auto target = scene->UI().HitTest({x, y});
        if (target.IsValid() && scene->Components().UI().Has<kb::scene::UIInputField>(target)) host->SetTextInputActive(true);
    }

    static void OnInputFocus(void* userData, bool focused) noexcept {
        auto* host = static_cast<WebGameHost*>(userData);
        if (host != nullptr && !focused) {
            host->digital_.Clear();
            host->pointerNeedsBaseline_ = true;
            host->SetTextInputActive(false);
        }
    }

    void SetTextInputActive(bool active) noexcept {
        if (textInputActive_ == active) return;
        textInputActive_ = active;
        SetBrowserTextInputActive(active ? 1 : 0);
    }

    [[nodiscard]] bool AppendCommittedText(char32_t codePoint) noexcept {
        kb::scene::Scene* scene = runtime_.Scene();
        return textInputActive_ && scene != nullptr &&
            scene->Input().MutableDeviceState().AddTextInput(codePoint);
    }

    void Fail(const char* code, std::string_view message) {
        SetBrowserStartupError(code);
        std::fprintf(stderr, "kb_game_web: %.*s\n", static_cast<int>(message.size()), message.data());
        Shutdown();
    }

    void Shutdown() noexcept {
        shuttingDown_ = true;
        downloadActive_ = false;
        CancelFetchWatchdog();
        CancelBrowserDownload();
        emscripten_cancel_main_loop();
        UnregisterGraphicsEvents();
        touches_.Detach();
        SetTextInputActive(false);
        DestroyBrowserTextInput();
        if (!runtime_.Shutdown(renderer_, std::cerr)) {
            SetBrowserStartupError("runtime-failed");
        }
        if (renderer_.IsInitialized()) renderer_.Shutdown();
        if (pack_ != nullptr) pack_->Unmount();
        pack_.reset();
        downloadBytes_.Reset();
    }

    WebRenderSurface surface_;
    static constexpr std::size_t kDownloadLimit = static_cast<std::size_t>(
        kb::assets::bake::kMaxWholeFileAssetPackBytes + kb::assets::bake::kMaxAssetPackSealBytes);
    kb::game::WebDownloadBuffer downloadBytes_{kDownloadLimit};
    bool downloadActive_ = false;
    kb::game::WebDownloadProgress downloadProgress_;
    int fetchWatchdog_ = -1;
    std::shared_ptr<kb::assets::bake::RuntimeAssetPack> pack_;
    kb::render::Renderer renderer_;
    kb::game::PackagedGameRuntime runtime_;
    kb::game::WebTouchInput touches_;
    kb::game::DigitalInputFrame digital_;
    std::chrono::steady_clock::time_point previousTick_{};
    std::chrono::steady_clock::time_point storageStartupTime_{};
    std::chrono::steady_clock::time_point graphicsLossTime_{};
    bool firstFrameReported_ = false;
    bool frameInProgress_ = false;
    bool textInputActive_ = false;
    bool pointerNeedsBaseline_ = true;
    bool graphicsContextLost_ = false;
    bool graphicsEventsRegistered_ = false;
    bool graphicsContextRestored_ = false;
    bool graphicsResourcesReleased_ = false;
    bool inputResumePending_ = false;
    bool shuttingDown_ = false;
    std::uint32_t graphicsRecreationAttempts_ = 0U;
    std::uint32_t healthyGraphicsFrames_ = 0U;

    friend int ::kb_web_append_committed_text(void* host, std::uint32_t codePoint) noexcept;
    friend int ::kb_web_download_begin(void* host, double length);
    friend void* ::kb_web_download_append(void* host, std::size_t count);
    friend void ::kb_web_download_progress(void* host);
    friend void ::kb_web_download_complete(void* host);
    friend void ::kb_web_download_error(void* host, int status);
};

std::unique_ptr<WebGameHost> g_host;

} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE int kb_web_download_begin(void* host, double length) {
    return static_cast<WebGameHost*>(host)->BeginDownload(length);
}
extern "C" EMSCRIPTEN_KEEPALIVE void* kb_web_download_append(void* host, std::size_t count) {
    return static_cast<WebGameHost*>(host)->AppendDownload(count);
}
extern "C" EMSCRIPTEN_KEEPALIVE void kb_web_download_progress(void* host) {
    auto* gameHost = static_cast<WebGameHost*>(host);
    if (!gameHost->shuttingDown_ && gameHost->downloadActive_) {
        gameHost->downloadProgress_.Observe(gameHost->downloadBytes_.Bytes().size(),
            kb::game::WebDownloadProgress::Clock::now());
    }
}
extern "C" EMSCRIPTEN_KEEPALIVE void kb_web_download_complete(void* host) {
    static_cast<WebGameHost*>(host)->CompleteDownload();
}
extern "C" EMSCRIPTEN_KEEPALIVE void kb_web_download_error(void* host, int status) {
    auto* gameHost = static_cast<WebGameHost*>(host);
    if (!gameHost->shuttingDown_ && gameHost->downloadActive_) {
        std::ostringstream message;
        message << "asset pack request failed: HTTP " << status;
        gameHost->Fail("fetch-http", message.str());
    }
}

extern "C" EMSCRIPTEN_KEEPALIVE int kb_web_append_committed_text(
    void* host, std::uint32_t codePoint) noexcept {
    auto* gameHost = static_cast<WebGameHost*>(host);
    return gameHost != nullptr && gameHost->AppendCommittedText(static_cast<char32_t>(codePoint))
        ? 1
        : 0;
}

int main() {
    EM_ASM({ location.hash = 'kb-starting'; });
    g_host = std::make_unique<WebGameHost>();
    g_host->StartFetch();
    return EXIT_SUCCESS;
}
