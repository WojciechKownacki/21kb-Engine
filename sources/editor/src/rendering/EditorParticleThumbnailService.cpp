#include "rendering/EditorParticleThumbnailService.hpp"

#include "app/EditorCrashBreadcrumbs.hpp"
#include "editor/ParticlePreviewSession.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/particles/ParticlePlayback.hpp"
#include "engine/scene/ParticleEffectAsset.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "project/EditorProjectPaths.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "rendering/HeroIconGdiplusRuntime.hpp"
#include "scene/EditorSceneContext.hpp"

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <filesystem>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace kb::editor {
namespace {

constexpr std::uint32_t kThumbnailRenderSize = 512U;
constexpr int kThumbnailSize = 128;
constexpr int kMinimumSimulationSteps = 6;
constexpr int kMaximumSimulationSteps = 30;
constexpr int kAnimationFrameCount = 3;
constexpr int kCaptureFrameBudget = 600;
constexpr std::uint64_t kParticleThumbnailCaptureViewportKey =
    0x5041525454484D42ULL;

enum class EntryState : std::uint8_t {
    Queued,
    Capturing,
    Ready,
    Failed,
};

[[nodiscard]] std::string Hex64(std::uint64_t value) {
    std::ostringstream output;
    output << std::hex << std::nouppercase;
    output.width(16);
    output.fill('0');
    output << value;
    return output.str();
}

[[nodiscard]] std::filesystem::path ThumbnailPath(
    std::uint64_t assetId,
    std::uint64_t contentHash,
    int frame) {
    return EditorProjectPaths::ProjectRoot() / "Saved" / "Cache" /
        "Thumbnails" /
        ("particle_" + Hex64(assetId) + "_" + Hex64(contentHash) +
            "_f" + std::to_string(frame) + "_v1.png");
}

[[nodiscard]] bool LoadPng(
    const std::filesystem::path& path,
    EditorParticleThumbnailImage& image) {
    std::error_code error;
    if (!std::filesystem::exists(path, error)) return false;

    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Bitmap bitmap(path.wstring().c_str());
    if (bitmap.GetLastStatus() != Gdiplus::Ok) return false;
    const int width = static_cast<int>(bitmap.GetWidth());
    const int height = static_cast<int>(bitmap.GetHeight());
    if (width <= 0 || height <= 0) return false;

    Gdiplus::Rect bounds{0, 0, width, height};
    Gdiplus::BitmapData data{};
    if (bitmap.LockBits(
            &bounds, Gdiplus::ImageLockModeRead,
            PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
        return false;
    }
    image.width = width;
    image.height = height;
    image.bgra.assign(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
        0U);
    for (int y = 0; y < height; ++y) {
        const auto* row = reinterpret_cast<const std::uint32_t*>(
            static_cast<const std::uint8_t*>(data.Scan0) +
            static_cast<std::ptrdiff_t>(y) * data.Stride);
        std::copy_n(
            row, width,
            image.bgra.begin() + static_cast<std::ptrdiff_t>(y) * width);
    }
    static_cast<void>(bitmap.UnlockBits(&data));
    return true;
}

[[nodiscard]] float AcesToneMap(float value) noexcept {
    constexpr float a = 2.51F;
    constexpr float b = 0.03F;
    constexpr float c = 2.43F;
    constexpr float d = 0.59F;
    constexpr float e = 0.14F;
    return std::clamp(
        (value * (a * value + b)) /
            (value * (c * value + d) + e),
        0.0F, 1.0F);
}

void ApplyDisplayTransform(EditorParticleThumbnailImage& image) noexcept {
    constexpr float kInverseGamma = 1.0F / 2.2F;
    const auto decode = [](std::uint32_t value) noexcept {
        return static_cast<float>(value & 0xFFU) / 255.0F;
    };
    const auto encode = [](float value) noexcept {
        return static_cast<std::uint32_t>(
            std::clamp(std::pow(AcesToneMap(value), kInverseGamma),
                0.0F, 1.0F) * 255.0F + 0.5F);
    };
    for (std::uint32_t& pixel : image.bgra) {
        const std::uint32_t blue = encode(decode(pixel));
        const std::uint32_t green = encode(decode(pixel >> 8U));
        const std::uint32_t red = encode(decode(pixel >> 16U));
        pixel = 0xFF000000U | (red << 16U) | (green << 8U) | blue;
    }
}

void Downsample(EditorParticleThumbnailImage& image, int target) {
    if (image.width <= 0 || image.height <= 0 || target <= 0 ||
        (image.width == target && image.height == target)) {
        return;
    }
    std::vector<std::uint32_t> reduced(
        static_cast<std::size_t>(target) * static_cast<std::size_t>(target),
        0xFF000000U);
    for (int y = 0; y < target; ++y) {
        const int top = y * image.height / target;
        const int bottom = std::max(top + 1, (y + 1) * image.height / target);
        for (int x = 0; x < target; ++x) {
            const int left = x * image.width / target;
            const int right = std::max(left + 1, (x + 1) * image.width / target);
            std::uint64_t blue = 0U;
            std::uint64_t green = 0U;
            std::uint64_t red = 0U;
            std::uint64_t alpha = 0U;
            std::uint64_t samples = 0U;
            for (int sourceY = top; sourceY < bottom; ++sourceY) {
                for (int sourceX = left; sourceX < right; ++sourceX) {
                    const std::uint32_t pixel = image.bgra[
                        static_cast<std::size_t>(sourceY) *
                            static_cast<std::size_t>(image.width) +
                        static_cast<std::size_t>(sourceX)];
                    blue += pixel & 0xFFU;
                    green += (pixel >> 8U) & 0xFFU;
                    red += (pixel >> 16U) & 0xFFU;
                    alpha += (pixel >> 24U) & 0xFFU;
                    ++samples;
                }
            }
            reduced[static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(target) +
                static_cast<std::size_t>(x)] =
                (static_cast<std::uint32_t>(alpha / samples) << 24U) |
                (static_cast<std::uint32_t>(red / samples) << 16U) |
                (static_cast<std::uint32_t>(green / samples) << 8U) |
                static_cast<std::uint32_t>(blue / samples);
        }
    }
    image.width = target;
    image.height = target;
    image.bgra = std::move(reduced);
}

[[nodiscard]] EditorSceneBgfxViewport::PresentSettings ThumbnailSettings(
    const kb::particle_editor::ParticlePreviewSession& session,
    bool fullSyncRequired) {
    const auto snapshot = kb::particles::ParticlePlayback::ReadRenderSnapshot(
        session.PreviewScene());
    const std::uint64_t revision = snapshot == nullptr ? 1U :
        std::max<std::uint64_t>(1U, snapshot->Revision());
    kb::render::ScenePostProcessSettings postProcess{};
    postProcess.temporalAntiAliasingEnabled = false;
    postProcess.temporalJitterEnabled = false;
    postProcess.fxaaEnabled = false;
    postProcess.outputTransform.autoExposure.enabled = false;
    postProcess.outputTransform.autoExposure.temporalAdaptationEnabled = false;
    postProcess.outputTransform.exposureStops = 0.0F;
    postProcess.outputTransform.gamma = 2.2F;
    return EditorSceneBgfxViewport::PresentSettings{
        .renderWidth = kThumbnailRenderSize,
        .renderHeight = kThumbnailRenderSize,
        .fitMode = EditorViewportFitMode::Fit,
        .viewportKey = kParticleThumbnailCaptureViewportKey,
        .editorSceneOverlaysEnabled = false,
        .postProcessSettings = postProcess,
        .msaaSamples = 0U,
        .shadowPassEnabled = false,
        .postProcessEnabled = true,
        .selectionMaskEnabled = false,
        .selectionOutlineEnabled = false,
        .gpuDrivenRuntimeDispatchEnabled = true,
        .drawSafeArea = false,
        .sceneRevision = revision,
        .sceneDirtyBaseRevision = revision,
        .sceneFullSyncRequired = fullSyncRequired,
    };
}

} // namespace

struct EditorParticleThumbnailService::Impl {
    struct Entry {
        std::uint64_t contentHash = 0U;
        EntryState state = EntryState::Queued;
        std::vector<EditorParticleThumbnailImage> frames;
    };

    struct Capture {
        std::uint64_t id = 0U;
        int framesWaited = 0;
        std::filesystem::path path;
    };

    std::unordered_map<std::uint64_t, Entry> entries;
    std::deque<kb::assets::AssetId> queue;
    std::unique_ptr<kb::particle_editor::ParticlePreviewSession> session;
    kb::assets::AssetId activeAsset{};
    std::uint64_t activeContentHash = 0U;
    int simulatedSteps = 0;
    int targetSteps = kMinimumSimulationSteps;
    std::array<int, kAnimationFrameCount> captureSteps{};
    int nextFrame = 0;
    bool fullSyncRequired = true;
    bool presented = false;
    Capture capture{};
    std::uint64_t revision = 1U;

    void ResetSession(EditorSceneBgfxViewport* viewport) noexcept {
        if (session != nullptr && session->Active()) {
            if (presented && viewport != nullptr) {
                session->Release([viewport](const kb::scene::Scene& scene) {
                    viewport->ReleaseScene(scene);
                });
            } else {
                session->Release([](const kb::scene::Scene&) {});
            }
        }
        session.reset();
        activeAsset = {};
        activeContentHash = 0U;
        simulatedSteps = 0;
        nextFrame = 0;
        fullSyncRequired = true;
        presented = false;
        capture = {};
    }

    void FinishActive(EditorSceneBgfxViewport& viewport, EntryState state) {
        if (auto found = entries.find(activeAsset.value);
            found != entries.end()) {
            found->second.state = state;
        }
        activeAsset = {};
        activeContentHash = 0U;
        simulatedSteps = 0;
        nextFrame = 0;
        capture = {};
        ++revision;
        if (queue.empty()) ResetSession(&viewport);
    }

    [[nodiscard]] bool BeginNext(
        EditorSceneContext& sceneContext,
        EditorSceneBgfxViewport& viewport) {
        kb::assets::AssetManager& manager =
            sceneContext.Scene().Assets().Manager();
        while (!queue.empty()) {
            const kb::assets::AssetId id = queue.front();
            queue.pop_front();
            auto entry = entries.find(id.value);
            if (entry == entries.end() ||
                entry->second.state != EntryState::Queued) {
                continue;
            }
            const kb::assets::AssetMetadata* metadata =
                manager.Registry().Find(id);
            if (metadata == nullptr ||
                metadata->contentHash != entry->second.contentHash) {
                entry->second.state = EntryState::Failed;
                ++revision;
                continue;
            }
            const auto asset = manager.Load<kb::scene::ParticleEffectAsset>(id);
            if (!asset.IsLoaded()) {
                entry->second.state = EntryState::Failed;
                ++revision;
                continue;
            }

            kb::particle_editor::ParticleEditorResult started;
            if (session == nullptr) {
                session = std::make_unique<
                    kb::particle_editor::ParticlePreviewSession>();
                started = session->Start(
                    sceneContext.Project(), manager.Registry(), id,
                    metadata->virtualPath, *asset);
                fullSyncRequired = true;
                presented = false;
            } else {
                started = session->PublishWorkingCopy(*asset);
            }
            if (!started.Succeeded()) {
                entry->second.state = EntryState::Failed;
                ResetSession(&viewport);
                ++revision;
                continue;
            }

            activeAsset = id;
            activeContentHash = metadata->contentHash;
            simulatedSteps = 0;
            targetSteps = std::clamp(
                static_cast<int>(std::lround(
                    asset->durationSeconds * 0.35F /
                    kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds)),
                kMinimumSimulationSteps,
                kMaximumSimulationSteps);
            for (int frame = 0; frame < kAnimationFrameCount; ++frame) {
                captureSteps[static_cast<std::size_t>(frame)] =
                    std::max(1, ((frame + 1) * targetSteps) /
                        kAnimationFrameCount);
            }
            nextFrame = 0;
            capture = {};
            entry->second.frames.clear();
            entry->second.state = EntryState::Capturing;
            EditorCrashBreadcrumbs::WriteValue(
                "particle_thumbnail", "capture sequence started", id.value);
            return true;
        }
        ResetSession(&viewport);
        return false;
    }

    void Present(
        EditorSceneBgfxViewport& viewport,
        HWND host,
        const RECT& stagingRect) {
        viewport.Present(
            host, stagingRect, session->PreviewScene(),
            ThumbnailSettings(*session, fullSyncRequired));
        fullSyncRequired = false;
        presented = true;
    }
};

EditorParticleThumbnailService::EditorParticleThumbnailService()
    : impl_(std::make_unique<Impl>()) {}

EditorParticleThumbnailService::~EditorParticleThumbnailService() {
    impl_->ResetSession(nullptr);
}

const EditorParticleThumbnailImage*
EditorParticleThumbnailService::ThumbnailFor(
    const kb::assets::AssetMetadata& metadata,
    std::uint64_t animationFrame) {
    auto found = impl_->entries.find(metadata.id.value);
    if (found != impl_->entries.end() &&
        found->second.contentHash != metadata.contentHash) {
        found->second = Impl::Entry{
            .contentHash = metadata.contentHash,
            .state = EntryState::Queued,
        };
        if (std::ranges::find(impl_->queue, metadata.id) ==
            impl_->queue.end()) {
            impl_->queue.push_back(metadata.id);
        }
        ++impl_->revision;
    } else if (found == impl_->entries.end()) {
        Impl::Entry entry{
            .contentHash = metadata.contentHash,
            .state = EntryState::Queued,
        };
        for (int frame = 0; frame < kAnimationFrameCount; ++frame) {
            EditorParticleThumbnailImage image;
            if (!LoadPng(
                    ThumbnailPath(metadata.id.value, metadata.contentHash,
                        frame),
                    image)) {
                entry.frames.clear();
                break;
            }
            ApplyDisplayTransform(image);
            Downsample(image, kThumbnailSize);
            entry.frames.push_back(std::move(image));
        }
        if (entry.frames.size() == kAnimationFrameCount) {
            entry.state = EntryState::Ready;
        } else {
            impl_->queue.push_back(metadata.id);
        }
        found = impl_->entries.emplace(metadata.id.value, std::move(entry)).first;
        ++impl_->revision;
    }
    if (found->second.frames.empty()) return nullptr;
    return &found->second.frames[static_cast<std::size_t>(
        animationFrame % found->second.frames.size())];
}

bool EditorParticleThumbnailService::Tick(
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& viewport,
    HWND host,
    const RECT& stagingRect) {
    if (host == nullptr || IsWindow(host) == 0 ||
        stagingRect.right <= stagingRect.left ||
        stagingRect.bottom <= stagingRect.top) {
        return false;
    }
    if (impl_->activeAsset.IsValid()) {
        const auto active = impl_->entries.find(impl_->activeAsset.value);
        if (active == impl_->entries.end() ||
            active->second.contentHash != impl_->activeContentHash) {
            impl_->ResetSession(&viewport);
        }
    }
    if (!impl_->activeAsset.IsValid() &&
        !impl_->BeginNext(sceneContext, viewport)) {
        return false;
    }
    if (impl_->session == nullptr) return false;

    auto entry = impl_->entries.find(impl_->activeAsset.value);
    if (entry == impl_->entries.end()) {
        impl_->FinishActive(viewport, EntryState::Failed);
        return true;
    }

    if (impl_->capture.id != 0U) {
        const kb::scene::SceneScreenCaptureStatus status =
            kb::scene::SceneRenderFeedback::ScreenCaptureStatus(
                impl_->session->PreviewScene(), impl_->capture.id);
        if (status == kb::scene::SceneScreenCaptureStatus::Completed) {
            EditorParticleThumbnailImage image;
            if (!LoadPng(impl_->capture.path, image)) {
                EditorCrashBreadcrumbs::WriteValue(
                    "particle_thumbnail", "png load failed",
                    impl_->activeAsset.value);
                impl_->FinishActive(viewport, EntryState::Failed);
                return true;
            }
            ApplyDisplayTransform(image);
            Downsample(image, kThumbnailSize);
            entry->second.frames.push_back(std::move(image));
            ++impl_->nextFrame;
            impl_->capture = {};
            ++impl_->revision;
            if (impl_->nextFrame >= kAnimationFrameCount) {
                EditorCrashBreadcrumbs::WriteValue(
                    "particle_thumbnail", "capture sequence ready",
                    impl_->activeAsset.value);
                impl_->FinishActive(viewport, EntryState::Ready);
            }
            return true;
        }
        ++impl_->capture.framesWaited;
        if (status == kb::scene::SceneScreenCaptureStatus::Failed ||
            impl_->capture.framesWaited > kCaptureFrameBudget) {
            EditorCrashBreadcrumbs::WriteValue(
                "particle_thumbnail",
                status == kb::scene::SceneScreenCaptureStatus::Failed
                    ? "capture failed"
                    : "capture timed out",
                impl_->activeAsset.value);
            impl_->FinishActive(viewport, EntryState::Failed);
            return true;
        }
        // A pending readback advances only when this scene is submitted. Poll
        // before queuing that submission: when completion is already visible,
        // the session can be released without leaving a queued present that
        // still references its scene.
        impl_->Present(viewport, host, stagingRect);
        return true;
    }

    const int captureAt = impl_->captureSteps[
        static_cast<std::size_t>(impl_->nextFrame)];
    if (impl_->simulatedSteps < captureAt) {
        const auto ticked = impl_->session->Tick(
            kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds);
        if (!ticked.Succeeded()) {
            impl_->FinishActive(viewport, EntryState::Failed);
            return true;
        }
        ++impl_->simulatedSteps;
    }
    if (impl_->simulatedSteps < captureAt) return true;

    std::error_code directoryError;
    std::filesystem::create_directories(
        EditorProjectPaths::ProjectRoot() / "Saved" / "Cache" /
            "Thumbnails",
        directoryError);
    impl_->Present(viewport, host, stagingRect);
    const std::filesystem::path path = ThumbnailPath(
        impl_->activeAsset.value, impl_->activeContentHash,
        impl_->nextFrame);
    const std::uint64_t captureId =
        kb::scene::SceneRenderFeedback::RequestScreenCapture(
            impl_->session->PreviewScene(), path.generic_string());
    if (captureId == 0U) return true;
    impl_->capture = Impl::Capture{
        .id = captureId,
        .framesWaited = 0,
        .path = path,
    };
    return true;
}

bool EditorParticleThumbnailService::HasPendingWork() const noexcept {
    return impl_->activeAsset.IsValid() || !impl_->queue.empty();
}

std::uint64_t EditorParticleThumbnailService::Revision() const noexcept {
    return impl_->revision;
}

void EditorParticleThumbnailService::CancelPendingWork(
    EditorSceneBgfxViewport* viewport) noexcept {
    const bool changed = impl_->activeAsset.IsValid() ||
        !impl_->queue.empty();
    impl_->ResetSession(viewport);
    impl_->queue.clear();
    for (auto entry = impl_->entries.begin();
         entry != impl_->entries.end();) {
        if (entry->second.state == EntryState::Ready) {
            ++entry;
        } else {
            entry = impl_->entries.erase(entry);
        }
    }
    if (changed) ++impl_->revision;
}

void EditorParticleThumbnailService::Clear(
    EditorSceneBgfxViewport* viewport) noexcept {
    impl_->ResetSession(viewport);
    impl_->entries.clear();
    impl_->queue.clear();
    ++impl_->revision;
}

EditorParticleThumbnailService& EditorParticleThumbnailCache() {
    static EditorParticleThumbnailService cache;
    return cache;
}

} // namespace kb::editor
