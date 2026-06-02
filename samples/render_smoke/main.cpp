#include "kb/render/DisplayConfig.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/SceneRenderTarget.hpp"
#include "kb/render/SceneRenderTargetFormat.hpp"
#include "kb/render/post/ScenePostProcessTargets.hpp"
#include "kb/render/resources/RenderResources.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr wchar_t kWindowClassName[] = L"KBRenderSmokeWindow";
constexpr wchar_t kWindowTitle[] = L"21kb bgfx smoke";
constexpr int kInitialWidth = 1280;
constexpr int kInitialHeight = 720;
constexpr std::uint64_t kSmokeMeshAssetId = 1001U;

class Win32RenderSurface final : public kb::render::RenderSurface {
public:
    explicit Win32RenderSurface(HWND window) noexcept
        : window_(window) {}

    [[nodiscard]] std::uint32_t Width() const noexcept override {
        RECT rect{};
        if (GetClientRect(window_, &rect) == 0) {
            return 0;
        }
        return static_cast<std::uint32_t>(std::max<LONG>(0, rect.right - rect.left));
    }

    [[nodiscard]] std::uint32_t Height() const noexcept override {
        RECT rect{};
        if (GetClientRect(window_, &rect) == 0) {
            return 0;
        }
        return static_cast<std::uint32_t>(std::max<LONG>(0, rect.bottom - rect.top));
    }

    [[nodiscard]] void* NativeWindowHandle() const noexcept override {
        return window_;
    }

    [[nodiscard]] void* NativeDisplayHandle() const noexcept override {
        return nullptr;
    }

private:
    HWND window_ = nullptr;
};

struct SmokeWindowState {
    kb::render::Renderer* renderer = nullptr;
};

LRESULT CALLBACK SmokeWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<SmokeWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
    case WM_NCCREATE: {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_SIZE:
        if (state != nullptr && state->renderer != nullptr && wParam != SIZE_MINIMIZED) {
            const auto width = static_cast<std::uint32_t>(LOWORD(lParam));
            const auto height = static_cast<std::uint32_t>(HIWORD(lParam));
            state->renderer->OnResize(width, height);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

[[nodiscard]] HWND CreateSmokeWindow(HINSTANCE instance, SmokeWindowState& state) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &SmokeWindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        return nullptr;
    }

    RECT rect{0, 0, kInitialWidth, kInitialHeight};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    return CreateWindowExW(
        0,
        kWindowClassName,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        instance,
        &state);
}

[[nodiscard]] std::uint32_t ParseMaxFrames(int argc, char** argv) {
    constexpr std::uint32_t kDefaultFrames = 600;
    constexpr char kPrefix[] = "--max-frames=";
    constexpr std::size_t kPrefixLength = sizeof(kPrefix) - 1;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (arg == nullptr || strncmp(arg, kPrefix, kPrefixLength) != 0) {
            continue;
        }
        const long parsed = std::strtol(arg + kPrefixLength, nullptr, 10);
        if (parsed > 0 && parsed <= 100000) {
            return static_cast<std::uint32_t>(parsed);
        }
    }

    return kDefaultFrames;
}

} // namespace

int main(int argc, char** argv) {
    const std::uint32_t maxFrames = ParseMaxFrames(argc, argv);
    HINSTANCE instance = GetModuleHandleW(nullptr);
    std::fprintf(stdout, "kb_render_smoke: start\n");
    std::fflush(stdout);

    kb::render::Renderer renderer;
    SmokeWindowState windowState{.renderer = &renderer};
    HWND window = CreateSmokeWindow(instance, windowState);
    if (window == nullptr) {
        std::fprintf(stderr, "kb_render_smoke: CreateSmokeWindow failed\n");
        std::fflush(stderr);
        return EXIT_FAILURE;
    }
    std::fprintf(stdout, "kb_render_smoke: window created\n");
    std::fflush(stdout);

    Win32RenderSurface surface(window);
    kb::render::DisplayConfig displayConfig{};
    displayConfig.syncMode = kb::render::DisplaySyncMode::VSync;
    displayConfig.targetFps = 180;

    if (!renderer.Initialize(surface, &displayConfig)) {
        std::fprintf(stderr, "kb_render_smoke: renderer.Initialize failed\n");
        std::fflush(stderr);
        DestroyWindow(window);
        return EXIT_FAILURE;
    }
    std::fprintf(stdout, "kb_render_smoke: renderer initialized\n");
    std::fflush(stdout);

    kb::scene::Scene scene;
    const kb::scene::SceneEntity camera = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Smoke Camera",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{0.0F, 2.0F, -6.0F},
        },
    });
    scene.Components().Cameras().Set(camera, kb::scene::CameraComponent{.primary = true});

    const kb::render::RenderVertexP3C3 smokeVertices[] = {
        {-0.8F, -0.6F, 0.0F, 0.0F, 0.75F, 1.0F},
        {0.8F, -0.6F, 0.0F, 1.0F, 0.75F, 0.0F},
        {0.0F, 0.8F, 0.0F, 1.0F, 1.0F, 1.0F},
    };
    const std::uint16_t smokeIndices[] = {0U, 1U, 2U};
    kb::render::RenderResourceRegistry* sceneResources = renderer.SceneResources();
    kb::render::SceneRenderResourceMap* sceneResourceMap = renderer.SceneResourceMap();
    if (sceneResources == nullptr || sceneResourceMap == nullptr) {
        std::fprintf(stderr, "kb_render_smoke: renderer scene resources unavailable\n");
        std::fflush(stderr);
        renderer.Shutdown();
        DestroyWindow(window);
        return EXIT_FAILURE;
    }
    const kb::render::RenderMeshHandle smokeMesh = sceneResources->RegisterMesh(kb::render::RenderMeshDesc{
        .vertices = smokeVertices,
        .vertexCount = static_cast<std::uint32_t>(std::size(smokeVertices)),
        .indices = smokeIndices,
        .indexCount = static_cast<std::uint32_t>(std::size(smokeIndices)),
    });
    if (!smokeMesh.IsValid()) {
        std::fprintf(stderr, "kb_render_smoke: RegisterMesh failed\n");
        std::fflush(stderr);
        renderer.Shutdown();
        DestroyWindow(window);
        return EXIT_FAILURE;
    }
    sceneResourceMap->BindMesh(kSmokeMeshAssetId, smokeMesh);

    const kb::scene::SceneEntity firstMesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Smoke Mesh A",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{-0.6F, 0.0F, 0.0F},
        },
    });
    scene.Components().MeshRenderers().Set(firstMesh, kb::scene::MeshRendererComponent{.meshAssetId = kSmokeMeshAssetId});
    const kb::scene::SceneEntity secondMesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Smoke Mesh B",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{0.6F, 0.0F, 0.0F},
        },
    });
    scene.Components().MeshRenderers().Set(secondMesh, kb::scene::MeshRendererComponent{.meshAssetId = kSmokeMeshAssetId});

    kb::render::SceneRenderTarget sceneTarget;
    kb::render::ScenePostProcessTargets postProcessTargets;
    if (!sceneTarget.Ensure(kb::render::SceneRenderTargetDesc{
            .extent = kb::render::RenderExtent{320U, 180U},
            .colorPolicy = kb::render::SceneColorFormatPolicy::Auto,
        })) {
        std::fprintf(stderr, "kb_render_smoke: SceneRenderTarget.Ensure failed\n");
        std::fflush(stderr);
        renderer.Shutdown();
        DestroyWindow(window);
        return EXIT_FAILURE;
    }
    if (!postProcessTargets.Ensure(kb::render::ScenePostProcessTargetsDesc{
            .extent = kb::render::RenderExtent{320U, 180U},
            .colorPolicy = kb::render::SceneColorFormatPolicy::Auto,
        })) {
        std::fprintf(stderr, "kb_render_smoke: ScenePostProcessTargets.Ensure failed\n");
        std::fflush(stderr);
        sceneTarget.Shutdown();
        renderer.Shutdown();
        DestroyWindow(window);
        return EXIT_FAILURE;
    }
    std::fprintf(
        stdout,
        "kb_render_smoke: scene target color=%s status=%s depth=%s depth_status=%s post_color=%s post_status=%s\n",
        kb::render::SceneTextureFormatName(sceneTarget.ColorSelection().format),
        kb::render::SceneTargetFormatSelectionStatusName(sceneTarget.ColorSelection().status),
        kb::render::SceneTextureFormatName(sceneTarget.DepthSelection().format),
        kb::render::SceneTargetFormatSelectionStatusName(sceneTarget.DepthSelection().status),
        kb::render::SceneTextureFormatName(postProcessTargets.ColorSelection().format),
        kb::render::SceneTargetFormatSelectionStatusName(postProcessTargets.ColorSelection().status));
    std::fflush(stdout);

    std::uint32_t frameCount = 0;
    bool running = true;
    while (running && frameCount < maxFrames) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
            if (message.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (!running) {
            break;
        }

        if (renderer.BeginFrame()) {
            const kb::render::RenderSceneSubmitDesc desc{
                .target = kb::render::RenderSceneTargetBinding{
                    .frameBuffer = sceneTarget.FrameBuffer(),
                    .colorTexture = sceneTarget.ColorTexture(),
                    .viewport = kb::render::RenderViewportDesc{
                        .id = kb::render::RenderViewportId{1U},
                        .extent = kb::render::RenderExtent{320U, 180U},
                        .viewportIndex = 0U,
                    },
                },
                .postProcess = postProcessTargets.Binding(),
                .finalComposite = kb::render::RenderFinalCompositeTargetBinding{
                    .frameBuffer = BGFX_INVALID_HANDLE,
                    .extent = kb::render::RenderExtent{320U, 180U},
                    .enabled = true,
                },
                .clearRgba = 0x101018FFU,
            };
            if (!renderer.SubmitScene(scene, desc)) {
                std::fprintf(stderr, "kb_render_smoke: renderer.SubmitScene failed\n");
                std::fflush(stderr);
                renderer.EndFrame();
                postProcessTargets.Shutdown();
                sceneTarget.Shutdown();
                renderer.Shutdown();
                DestroyWindow(window);
                UnregisterClassW(kWindowClassName, instance);
                return EXIT_FAILURE;
            }
            const kb::render::SceneRenderSubmitStats sceneStats = renderer.LastSceneSubmitStats();
            if (sceneStats.visibleMeshCount != 2U ||
                sceneStats.submittedMeshCount != 2U ||
                sceneStats.submittedDrawGroupCount != 1U ||
                sceneStats.submittedDrawCallCount != 1U ||
                sceneStats.HasMissingResources()) {
                std::fprintf(
                    stderr,
                    "kb_render_smoke: unexpected scene stats visible=%u submitted=%u groups=%u draws=%u missing=%u\n",
                    sceneStats.visibleMeshCount,
                    sceneStats.submittedMeshCount,
                    sceneStats.submittedDrawGroupCount,
                    sceneStats.submittedDrawCallCount,
                    sceneStats.HasMissingResources() ? 1U : 0U);
                std::fflush(stderr);
                renderer.EndFrame();
                postProcessTargets.Shutdown();
                sceneTarget.Shutdown();
                renderer.Shutdown();
                DestroyWindow(window);
                UnregisterClassW(kWindowClassName, instance);
                return EXIT_FAILURE;
            }
            renderer.EndFrame();
            ++frameCount;
        }
    }

    postProcessTargets.Shutdown();
    sceneTarget.Shutdown();
    renderer.Shutdown();
    std::fprintf(stdout, "kb_render_smoke: renderer shutdown\n");
    std::fflush(stdout);
    DestroyWindow(window);
    UnregisterClassW(kWindowClassName, instance);
    std::fprintf(stdout, "kb_render_smoke: rendered %u frames\n", frameCount);
    std::fflush(stdout);
    return EXIT_SUCCESS;
}
