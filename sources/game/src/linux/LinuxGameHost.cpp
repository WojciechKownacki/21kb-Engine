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

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#undef None
#undef Success

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include <unistd.h>

namespace {

constexpr std::uint32_t kInitialWidth = 1280U;
constexpr std::uint32_t kInitialHeight = 720U;

struct LinuxGameOptions {
    std::filesystem::path packPath;
    std::uint32_t frameLimit = 0U;
};

[[nodiscard]] bool ParsePositiveFrameCount(
    std::string_view text,
    std::uint32_t& count) noexcept {
    if (text.empty() || text.size() > 10U) return false;
    std::uint64_t value = 0U;
    for (const char digit : text) {
        if (digit < '0' || digit > '9') return false;
        value = value * 10U + static_cast<std::uint64_t>(digit - '0');
        if (value > 0xFFFFFFFFULL) return false;
    }
    if (value == 0U) return false;
    count = static_cast<std::uint32_t>(value);
    return true;
}

[[nodiscard]] bool ParseArguments(int argc, char** argv, LinuxGameOptions& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{ argv[index] };
        if (argument.starts_with("--frames=")) {
            if (!ParsePositiveFrameCount(argument.substr(9U), options.frameLimit)) {
                std::cerr << "kb_game_linux: --frames expects a positive frame count\n";
                return false;
            }
        } else if (!argument.starts_with("--") && options.packPath.empty()) {
            options.packPath = std::filesystem::path{ argument };
        } else {
            std::cerr << "kb_game_linux: unknown option '" << argument << "'\n";
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::filesystem::path ExecutableDirectory() {
    std::filesystem::path link{ "/proc/self/exe" };
    std::error_code error;
    const std::filesystem::path executable = std::filesystem::read_symlink(link, error);
    return error ? std::filesystem::current_path() : executable.parent_path();
}

[[nodiscard]] kb::input::InputKey LinuxKey(KeySym key) noexcept {
    using kb::input::InputKey;
    if (key >= XK_a && key <= XK_z) {
        return static_cast<InputKey>(
            static_cast<std::uint16_t>(InputKey::A) + static_cast<std::uint16_t>(key - XK_a));
    }
    if (key >= XK_A && key <= XK_Z) {
        return static_cast<InputKey>(
            static_cast<std::uint16_t>(InputKey::A) + static_cast<std::uint16_t>(key - XK_A));
    }
    if (key >= XK_0 && key <= XK_9) {
        return static_cast<InputKey>(
            static_cast<std::uint16_t>(InputKey::Num0) + static_cast<std::uint16_t>(key - XK_0));
    }
    if (key >= XK_F1 && key <= XK_F12) {
        return static_cast<InputKey>(
            static_cast<std::uint16_t>(InputKey::F1) + static_cast<std::uint16_t>(key - XK_F1));
    }
    switch (key) {
    case XK_Escape: return InputKey::Escape;
    case XK_Tab: return InputKey::Tab;
    case XK_Caps_Lock: return InputKey::CapsLock;
    case XK_space: return InputKey::Space;
    case XK_Return: return InputKey::Enter;
    case XK_BackSpace: return InputKey::Backspace;
    case XK_Delete: return InputKey::Delete;
    case XK_Insert: return InputKey::Insert;
    case XK_Home: return InputKey::Home;
    case XK_End: return InputKey::End;
    case XK_Page_Up: return InputKey::PageUp;
    case XK_Page_Down: return InputKey::PageDown;
    case XK_Up: return InputKey::ArrowUp;
    case XK_Down: return InputKey::ArrowDown;
    case XK_Left: return InputKey::ArrowLeft;
    case XK_Right: return InputKey::ArrowRight;
    case XK_Shift_L: return InputKey::LeftShift;
    case XK_Shift_R: return InputKey::RightShift;
    case XK_Control_L: return InputKey::LeftControl;
    case XK_Control_R: return InputKey::RightControl;
    case XK_Alt_L: return InputKey::LeftAlt;
    case XK_Alt_R: return InputKey::RightAlt;
    default: return InputKey::None;
    }
}

class LinuxRenderSurface final : public kb::render::RenderSurface {
public:
    LinuxRenderSurface(Display* display, Window window) noexcept
        : display_{ display }, window_{ window } {}

    void Resize(std::uint32_t width, std::uint32_t height) noexcept {
        width_ = width;
        height_ = height;
    }

    [[nodiscard]] std::uint32_t Width() const noexcept override { return width_; }
    [[nodiscard]] std::uint32_t Height() const noexcept override { return height_; }
    [[nodiscard]] void* NativeWindowHandle() const noexcept override {
        return reinterpret_cast<void*>(static_cast<std::uintptr_t>(window_));
    }
    [[nodiscard]] void* NativeDisplayHandle() const noexcept override { return display_; }

private:
    Display* display_ = nullptr;
    Window window_{};
    std::uint32_t width_ = kInitialWidth;
    std::uint32_t height_ = kInitialHeight;
};

class LinuxGameHost {
public:
    ~LinuxGameHost() { static_cast<void>(Shutdown()); }

    [[nodiscard]] bool Initialize(const std::filesystem::path& packPath) {
        if (XInitThreads() == 0) {
            std::cerr << "kb_game_linux: X11 thread support could not be initialized\n";
            return false;
        }
        display_ = XOpenDisplay(nullptr);
        if (display_ == nullptr) {
            std::cerr << "kb_game_linux: X11 display could not be opened\n";
            return false;
        }
        const int screen = DefaultScreen(display_);
        window_ = XCreateSimpleWindow(
            display_, RootWindow(display_, screen), 0, 0, kInitialWidth, kInitialHeight,
            0, BlackPixel(display_, screen), BlackPixel(display_, screen));
        if (window_ == 0U) {
            std::cerr << "kb_game_linux: X11 window could not be created\n";
            return false;
        }
        XSelectInput(
            display_, window_, StructureNotifyMask | FocusChangeMask | KeyPressMask |
                KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask);
        closeMessage_ = XInternAtom(display_, "WM_DELETE_WINDOW", False);
        static_cast<void>(XSetWMProtocols(display_, window_, &closeMessage_, 1));
        XStoreName(display_, window_, "21kb Game");
        XMapWindow(display_, window_);
        XFlush(display_);

        kb::assets::bake::BakeTargetProfile profile{};
        if (!kb::game::RuntimeHostBakeTargetProfile(profile)) {
            std::cerr << "kb_game_linux: host has no valid package target identity\n";
            return false;
        }
        pack_ = std::make_shared<kb::assets::bake::RuntimeAssetPack>();
        const kb::assets::bake::RuntimeAssetPackStatus status = pack_->Mount(packPath, profile);
        if (status != kb::assets::bake::RuntimeAssetPackStatus::Success) {
            std::cerr << "kb_game_linux: package mount failed: "
                      << kb::assets::bake::ToString(status) << '\n';
            return false;
        }
        if (!runtime_.Initialize(pack_, packPath.parent_path(), renderer_, std::cerr)) {
            return false;
        }
        if (!runtime_.Project().gameName.empty()) {
            XStoreName(display_, window_, runtime_.Project().gameName.c_str());
            XFlush(display_);
        }

        surface_ = std::make_unique<LinuxRenderSurface>(display_, window_);
        kb::render::DisplayConfig config{};
        config.enableEditorRendering = false;
        config.writableStorageRoot = packPath.parent_path().native();
        if (!renderer_.Initialize(*surface_, &config)) {
            std::cerr << "kb_game_linux: renderer initialization failed\n";
            return false;
        }
        previousTick_ = std::chrono::steady_clock::now();
        return true;
    }

    int Run(std::uint32_t frameLimit) {
        bool running = true;
        std::uint32_t frames = 0U;
        while (running) {
            while (XPending(display_) > 0) {
                XEvent event{};
                XNextEvent(display_, &event);
                running = HandleEvent(event) && running;
            }
            if (!running) {
                break;
            }
            if (surface_->Width() == 0U || surface_->Height() == 0U) {
                XEvent event{};
                XNextEvent(display_, &event);
                running = HandleEvent(event);
                kb::game::ResetRuntimeDeltaOrigin(
                    previousTick_, std::chrono::steady_clock::now());
                continue;
            }
            const auto now = std::chrono::steady_clock::now();
            const float delta = kb::game::RuntimeDeltaSeconds(previousTick_, now);
            previousTick_ = now;
            bool frameSubmitted = false;
            running = runtime_.Tick(renderer_, delta, &frameSubmitted);
            if (frameSubmitted) {
                ++frames;
            }
            if (kb::scene::Scene* scene = runtime_.Scene(); scene != nullptr) {
                scene->Input().MutableDeviceState().SetAnalog(kb::input::InputKey::MouseX, 0.0F);
                scene->Input().MutableDeviceState().SetAnalog(kb::input::InputKey::MouseY, 0.0F);
                scene->Input().MutableDeviceState().SetAnalog(kb::input::InputKey::MouseWheel, 0.0F);
            }
            if (frameLimit != 0U && frames >= frameLimit) running = false;
        }
        const bool clean = Shutdown();
        std::cout << "kb_game_linux: frames=" << frames
                  << " rendered=" << (frames > 0U ? 1 : 0)
                  << " shutdown=" << (clean ? "clean" : "incomplete") << '\n';
        return clean ? EXIT_SUCCESS : EXIT_FAILURE;
    }

private:
    [[nodiscard]] bool HandleEvent(const XEvent& event) {
        kb::scene::Scene* scene = runtime_.Scene();
        kb::input::InputDeviceState* input =
            scene == nullptr ? nullptr : &scene->Input().MutableDeviceState();
        switch (event.type) {
        case ClientMessage:
            return static_cast<Atom>(event.xclient.data.l[0]) != closeMessage_;
        case ConfigureNotify: {
            const std::uint32_t width = event.xconfigure.width > 0
                ? static_cast<std::uint32_t>(event.xconfigure.width) : 0U;
            const std::uint32_t height = event.xconfigure.height > 0
                ? static_cast<std::uint32_t>(event.xconfigure.height) : 0U;
            if (width != surface_->Width() || height != surface_->Height()) {
                surface_->Resize(width, height);
                if (width > 0U && height > 0U) {
                    renderer_.OnResize(width, height);
                }
            }
            break;
        }
        case FocusIn:
            if (input != nullptr) input->SetHasFocus(true);
            break;
        case FocusOut:
            if (input != nullptr) {
                input->Reset();
                input->SetHasFocus(false);
            }
            break;
        case KeyPress:
        case KeyRelease:
            if (input != nullptr) {
                const KeySym symbol = XLookupKeysym(const_cast<XKeyEvent*>(&event.xkey), 0);
                const kb::input::InputKey key = LinuxKey(symbol);
                if (key != kb::input::InputKey::None) {
                    input->SetKeyDown(key, event.type == KeyPress);
                }
            }
            break;
        case ButtonPress:
        case ButtonRelease:
            if (input != nullptr) {
                const bool down = event.type == ButtonPress;
                if (event.xbutton.button == Button1) input->SetKeyDown(kb::input::InputKey::MouseLeft, down);
                if (event.xbutton.button == Button2) input->SetKeyDown(kb::input::InputKey::MouseMiddle, down);
                if (event.xbutton.button == Button3) input->SetKeyDown(kb::input::InputKey::MouseRight, down);
                if (down && (event.xbutton.button == Button4 || event.xbutton.button == Button5)) {
                    input->SetAnalog(kb::input::InputKey::MouseWheel, event.xbutton.button == Button4 ? 1.0F : -1.0F);
                }
            }
            break;
        case MotionNotify:
            if (input != nullptr) {
                const float x = static_cast<float>(event.xmotion.x);
                const float y = static_cast<float>(event.xmotion.y);
                input->SetAnalog(kb::input::InputKey::MouseX, x - input->PointerX());
                input->SetAnalog(kb::input::InputKey::MouseY, y - input->PointerY());
                input->SetPointerPosition(x, y);
            }
            break;
        default:
            break;
        }
        return true;
    }

    [[nodiscard]] bool Shutdown() noexcept {
        if (shutdown_) return shutdownClean_;
        shutdown_ = true;
        shutdownClean_ = runtime_.Shutdown(renderer_, std::cerr);
        if (renderer_.IsInitialized()) renderer_.Shutdown();
        if (pack_ != nullptr) pack_->Unmount();
        pack_.reset();
        surface_.reset();
        if (display_ != nullptr && window_ != 0U) XDestroyWindow(display_, window_);
        window_ = 0U;
        if (display_ != nullptr) XCloseDisplay(display_);
        display_ = nullptr;
        return shutdownClean_;
    }

    Display* display_ = nullptr;
    Window window_{};
    Atom closeMessage_{};
    std::unique_ptr<LinuxRenderSurface> surface_;
    std::shared_ptr<kb::assets::bake::RuntimeAssetPack> pack_;
    kb::render::Renderer renderer_;
    kb::game::PackagedGameRuntime runtime_;
    std::chrono::steady_clock::time_point previousTick_{};
    bool shutdown_ = false;
    bool shutdownClean_ = true;
};

} // namespace

int main(int argc, char** argv) {
    try {
        LinuxGameOptions options{};
        if (!ParseArguments(argc, argv, options)) {
            return EXIT_FAILURE;
        }
        if (options.packPath.empty()) options.packPath = ExecutableDirectory() / "Game.kbpack";
        LinuxGameHost host;
        return host.Initialize(options.packPath) ? host.Run(options.frameLimit) : EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << "kb_game_linux: unrecoverable error: " << error.what() << '\n';
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "kb_game_linux: unrecoverable error\n";
        return EXIT_FAILURE;
    }
}
