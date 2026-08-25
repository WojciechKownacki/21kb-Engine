#include "rendering/EditorParticleThumbnailService.hpp"
#include "rendering/ParticleThumbnailTimeline.hpp"

#include "app/EditorCrashBreadcrumbs.hpp"
#include "diagnostics/EditorLagTrace.hpp"
#include "editor/ParticlePreviewSession.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/particles/ParticlePlayback.hpp"
#include "engine/scene/ParticleEffectAsset.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "project/EditorProjectPaths.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "scene/EditorSceneContext.hpp"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <sstream>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>

namespace kb::editor {
namespace {

constexpr std::uint32_t kThumbnailRenderSize = 256U;
constexpr int kThumbnailSize = 128;
constexpr int kCaptureFrameBudget = 600;
constexpr std::size_t kImageWorkerCount = 2U;
constexpr int kMaximumSimulationStepsPerEditorFrame = 4;
constexpr double kSimulationBudgetMilliseconds = 1.5;
constexpr std::uint64_t kParticleThumbnailCaptureViewportKey =
    0x5041525454484D42ULL;

enum class EntryState : std::uint8_t {
    CacheLoading,
    AnimationCacheLoading,
    PosterQueued,
    PosterCapturing,
    PosterProcessing,
    Queued,
    Capturing,
    Processing,
    Ready,
    Failed,
};

enum class ImageWorkKind : std::uint8_t {
    LoadPosterCache,
    LoadAnimationCache,
    ProcessPosterCapture,
    ProcessAnimationCapture,
};

struct ImageWorkItem {
    ImageWorkKind kind = ImageWorkKind::LoadPosterCache;
    std::uint64_t assetId = 0U;
    std::uint64_t contentHash = 0U;
    int frame = 0;
    std::uint64_t generation = 0U;
    std::filesystem::path cacheDirectory;
    std::filesystem::path assetPath;
    std::shared_ptr<std::atomic_bool> cancelled;
    ParticleThumbnailTimelinePlan timeline{};
    kb::scene::SceneScreenCapturePixels pixels;
};

struct ImageWorkResult {
    ImageWorkKind kind = ImageWorkKind::LoadPosterCache;
    std::uint64_t assetId = 0U;
    std::uint64_t contentHash = 0U;
    int frame = 0;
    std::uint64_t generation = 0U;
    bool succeeded = false;
    bool cacheWriteSucceeded = true;
    std::shared_ptr<const kb::scene::ParticleEffectAsset> asset;
    ParticleThumbnailTimelinePlan timeline{};
    std::vector<EditorParticleThumbnailImage> frames;
};

[[nodiscard]] std::string Hex64(std::uint64_t value) {
    std::ostringstream output;
    output << std::hex << std::nouppercase;
    output.width(16);
    output.fill('0');
    output << value;
    return output.str();
}

[[nodiscard]] std::filesystem::path ThumbnailCacheDirectory() {
    return EditorProjectPaths::ProjectRoot() / "Saved" / "Cache" /
        "Thumbnails";
}

[[nodiscard]] std::filesystem::path ThumbnailPath(
    const std::filesystem::path& cacheDirectory,
    std::uint64_t assetId,
    std::uint64_t contentHash,
    int frame) {
    return cacheDirectory /
        ("particle_" + Hex64(assetId) + "_" + Hex64(contentHash) +
            "_f" + std::to_string(frame) + "_v5.kbthumb");
}

constexpr std::array<char, 8U> kThumbnailCacheMagic{
    '2', '1', 'K', 'B', 'T', 'H', 'M', '4'};

[[nodiscard]] bool LoadThumbnailCache(
    const std::filesystem::path& path,
    EditorParticleThumbnailImage& image) {
    std::ifstream input{path, std::ios::binary};
    std::array<char, kThumbnailCacheMagic.size()> magic{};
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t byteCount = 0U;
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    input.read(reinterpret_cast<char*>(&width), sizeof(width));
    input.read(reinterpret_cast<char*>(&height), sizeof(height));
    input.read(reinterpret_cast<char*>(&byteCount), sizeof(byteCount));
    constexpr std::uint32_t expectedBytes =
        kThumbnailSize * kThumbnailSize * sizeof(std::uint32_t);
    if (!input || magic != kThumbnailCacheMagic ||
        width != kThumbnailSize || height != kThumbnailSize ||
        byteCount != expectedBytes) {
        return false;
    }
    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.bgra.resize(byteCount / sizeof(std::uint32_t));
    input.read(
        reinterpret_cast<char*>(image.bgra.data()),
        static_cast<std::streamsize>(byteCount));
    return input && input.peek() == std::char_traits<char>::eof();
}

[[nodiscard]] bool SaveThumbnailCache(
    const std::filesystem::path& path,
    const EditorParticleThumbnailImage& image) {
    if (image.width != kThumbnailSize || image.height != kThumbnailSize ||
        image.bgra.size() != static_cast<std::size_t>(
            kThumbnailSize * kThumbnailSize)) {
        return false;
    }
    const std::filesystem::path temporary = path.wstring() + L".tmp." +
        std::to_wstring(std::hash<std::thread::id>{}(
            std::this_thread::get_id()));
    std::ofstream output{
        temporary, std::ios::binary | std::ios::out | std::ios::trunc};
    const std::uint32_t width = static_cast<std::uint32_t>(image.width);
    const std::uint32_t height = static_cast<std::uint32_t>(image.height);
    const std::uint32_t byteCount = static_cast<std::uint32_t>(
        image.bgra.size() * sizeof(std::uint32_t));
    output.write(
        kThumbnailCacheMagic.data(),
        static_cast<std::streamsize>(kThumbnailCacheMagic.size()));
    output.write(reinterpret_cast<const char*>(&width), sizeof(width));
    output.write(reinterpret_cast<const char*>(&height), sizeof(height));
    output.write(
        reinterpret_cast<const char*>(&byteCount), sizeof(byteCount));
    output.write(
        reinterpret_cast<const char*>(image.bgra.data()),
        static_cast<std::streamsize>(byteCount));
    output.flush();
    const bool written = output.good();
    output.close();
    if (!written || MoveFileExW(
            temporary.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING) == 0) {
        std::error_code removeError;
        std::filesystem::remove(temporary, removeError);
        return false;
    }
    return true;
}

[[nodiscard]] float AcesToneMap(float value) noexcept;

[[nodiscard]] std::uint8_t DisplayEncodedByte(float value) noexcept {
    if (!(value > 0.0F)) return 0U;
    if (!std::isfinite(value)) return 255U;
    constexpr float kInverseGamma = 1.0F / 2.2F;
    return static_cast<std::uint8_t>(
        std::clamp(
            std::pow(AcesToneMap(value), kInverseGamma), 0.0F, 1.0F) *
            255.0F +
        0.5F);
}

[[nodiscard]] bool DecodeCapturePixels(
    kb::scene::SceneScreenCapturePixels pixels,
    EditorParticleThumbnailImage& image,
    bool& displayEncoded) {
    const std::size_t pixelCount = static_cast<std::size_t>(pixels.width) *
        static_cast<std::size_t>(pixels.height);
    const bool sixteenBit =
        pixels.format == kb::scene::SceneScreenCapturePixelFormat::Rgba16 ||
        pixels.format ==
            kb::scene::SceneScreenCapturePixelFormat::Rgba16Float;
    const std::size_t bytesPerPixel = sixteenBit ? 8U : 4U;
    if (pixels.width == 0U || pixels.height == 0U ||
        pixels.bytes.size() != pixelCount * bytesPerPixel) {
        return false;
    }
    displayEncoded = false;
    image.width = static_cast<int>(pixels.width);
    image.height = static_cast<int>(pixels.height);
    if (pixels.format !=
        kb::scene::SceneScreenCapturePixelFormat::Rgba16Float) {
        image.bgra.resize(pixelCount);
    }
    const std::uint8_t* source = pixels.bytes.data();
    std::uint32_t* destination = image.bgra.data();
    if (pixels.format == kb::scene::SceneScreenCapturePixelFormat::Bgra8) {
        for (std::size_t pixel = 0U; pixel < pixelCount; ++pixel) {
            const std::uint8_t blue = *source++;
            const std::uint8_t green = *source++;
            const std::uint8_t red = *source++;
            const std::uint8_t alpha = *source++;
            *destination++ = (static_cast<std::uint32_t>(alpha) << 24U) |
                (static_cast<std::uint32_t>(red) << 16U) |
                (static_cast<std::uint32_t>(green) << 8U) | blue;
        }
    } else if (pixels.format ==
        kb::scene::SceneScreenCapturePixelFormat::Rgba8) {
        for (std::size_t pixel = 0U; pixel < pixelCount; ++pixel) {
            const std::uint8_t red = *source++;
            const std::uint8_t green = *source++;
            const std::uint8_t blue = *source++;
            const std::uint8_t alpha = *source++;
            *destination++ = (static_cast<std::uint32_t>(alpha) << 24U) |
                (static_cast<std::uint32_t>(red) << 16U) |
                (static_cast<std::uint32_t>(green) << 8U) | blue;
        }
    } else if (pixels.format ==
        kb::scene::SceneScreenCapturePixelFormat::Rgba16) {
        for (std::size_t pixel = 0U; pixel < pixelCount; ++pixel) {
            const std::uint32_t red = source[1U];
            const std::uint32_t green = source[3U];
            const std::uint32_t blue = source[5U];
            const std::uint32_t alpha = source[7U];
            source += 8U;
            *destination++ = (alpha << 24U) | (red << 16U) |
                (green << 8U) | blue;
        }
    } else {
        static const std::array<float, 65536U> halfToFloat = [] {
            std::array<float, 65536U> values{};
            for (std::size_t bits = 0U; bits < values.size(); ++bits) {
                values[bits] = bx::halfToFloat(
                    static_cast<std::uint16_t>(bits));
            }
            return values;
        }();
        constexpr int targetWidth = kThumbnailSize;
        constexpr int targetHeight = kThumbnailSize;
        image.width = targetWidth;
        image.height = targetHeight;
        image.bgra.assign(
            static_cast<std::size_t>(targetWidth * targetHeight),
            0xFF000000U);
        displayEncoded = true;
        const float* conversion = halfToFloat.data();
        destination = image.bgra.data();
        for (int y = 0; y < targetHeight; ++y) {
            const std::uint32_t top = static_cast<std::uint32_t>(y) *
                pixels.height / targetHeight;
            const std::uint32_t bottom = std::max(
                top + 1U,
                static_cast<std::uint32_t>(y + 1) * pixels.height /
                    targetHeight);
            for (int x = 0; x < targetWidth; ++x) {
                const std::uint32_t left = static_cast<std::uint32_t>(x) *
                    pixels.width / targetWidth;
                const std::uint32_t right = std::max(
                    left + 1U,
                    static_cast<std::uint32_t>(x + 1) * pixels.width /
                        targetWidth);
                float red = 0.0F;
                float green = 0.0F;
                float blue = 0.0F;
                std::uint32_t samples = 0U;
                for (std::uint32_t sourceY = top; sourceY < bottom;
                     ++sourceY) {
                    const std::uint8_t* row = pixels.bytes.data() +
                        (static_cast<std::size_t>(sourceY) * pixels.width +
                            left) *
                            8U;
                    for (std::uint32_t sourceX = left; sourceX < right;
                         ++sourceX) {
                        const std::uint16_t redBits =
                            static_cast<std::uint16_t>(
                                row[0U] |
                                (static_cast<std::uint16_t>(row[1U]) << 8U));
                        const std::uint16_t greenBits =
                            static_cast<std::uint16_t>(
                                row[2U] |
                                (static_cast<std::uint16_t>(row[3U]) << 8U));
                        const std::uint16_t blueBits =
                            static_cast<std::uint16_t>(
                                row[4U] |
                                (static_cast<std::uint16_t>(row[5U]) << 8U));
                        red += conversion[redBits];
                        green += conversion[greenBits];
                        blue += conversion[blueBits];
                        row += 8U;
                        ++samples;
                    }
                }
                const float inverseSamples = 1.0F /
                    static_cast<float>(samples);
                const std::uint32_t encodedRed = DisplayEncodedByte(
                    red * inverseSamples);
                const std::uint32_t encodedGreen = DisplayEncodedByte(
                    green * inverseSamples);
                const std::uint32_t encodedBlue = DisplayEncodedByte(
                    blue * inverseSamples);
                *destination++ = 0xFF000000U | (encodedRed << 16U) |
                    (encodedGreen << 8U) | encodedBlue;
            }
        }
    }
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
    static const std::array<std::uint8_t, 256U> encoded = [] {
        constexpr float kInverseGamma = 1.0F / 2.2F;
        std::array<std::uint8_t, 256U> values{};
        for (std::size_t index = 0U; index < values.size(); ++index) {
            const float linear = static_cast<float>(index) / 255.0F;
            values[index] = static_cast<std::uint8_t>(
                std::clamp(
                    std::pow(AcesToneMap(linear), kInverseGamma),
                    0.0F,
                    1.0F) * 255.0F + 0.5F);
        }
        return values;
    }();
    const std::uint8_t* lookup = encoded.data();
    std::uint32_t* pixels = image.bgra.data();
    const std::uint32_t* const end = pixels + image.bgra.size();
    for (; pixels != end; ++pixels) {
        const std::uint32_t pixel = *pixels;
        const std::uint32_t blue = lookup[pixel & 0xFFU];
        const std::uint32_t green = lookup[(pixel >> 8U) & 0xFFU];
        const std::uint32_t red = lookup[(pixel >> 16U) & 0xFFU];
        *pixels = 0xFF000000U | (red << 16U) | (green << 8U) | blue;
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
    const std::uint32_t* source = image.bgra.data();
    std::uint32_t* destination = reduced.data();
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
                    const std::uint32_t pixel = source[
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
            *destination++ =
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
        std::uint64_t generation = 0U;
        std::filesystem::path cacheDirectory;
        EntryState state = EntryState::CacheLoading;
        std::shared_ptr<const kb::scene::ParticleEffectAsset> asset;
        std::shared_ptr<std::atomic_bool> cancelled =
            std::make_shared<std::atomic_bool>(false);
        ParticleThumbnailTimelinePlan timeline{};
        std::vector<EditorParticleThumbnailImage> frames;
        int processedFrames = 0;
    };

    struct Capture {
        std::uint64_t id = 0U;
        int framesWaited = 0;
        std::uint32_t lastObservedRendererFrame = 0U;
    };

    std::unordered_map<std::uint64_t, Entry> entries;
    std::deque<kb::assets::AssetId> posterQueue;
    std::deque<kb::assets::AssetId> queue;
    std::mutex imageMutex;
    std::condition_variable imageWake;
    std::deque<ImageWorkItem> imageWork;
    std::deque<ImageWorkResult> imageResults;
    std::array<std::thread, kImageWorkerCount> imageWorkers;
    std::atomic<std::uint32_t> pendingImageWork{0U};
    bool stopImageWorkers = false;
    std::unique_ptr<kb::particle_editor::ParticlePreviewSession> session;
    kb::assets::AssetId activeAsset{};
    std::uint64_t activeContentHash = 0U;
    std::uint64_t activeGeneration = 0U;
    int simulatedSteps = 0;
    std::vector<int> captureFrames;
    int nextCapture = 0;
    bool activePoster = false;
    bool fullSyncRequired = true;
    bool presented = false;
    Capture capture{};
    std::uint64_t revision = 1U;
    std::uint64_t nextGeneration = 1U;

    Impl() {
        for (std::thread& worker : imageWorkers) {
            worker = std::thread{[this] { ImageWorkerLoop(); }};
        }
    }

    ~Impl() { StopImageWorkers(); }

    void EnqueueImageWork(ImageWorkItem work) {
        const bool urgent =
            work.kind == ImageWorkKind::LoadPosterCache ||
            work.kind == ImageWorkKind::ProcessPosterCapture;
        {
            std::scoped_lock lock{imageMutex};
            if (urgent) {
                imageWork.push_front(std::move(work));
            } else {
                imageWork.push_back(std::move(work));
            }
            pendingImageWork.fetch_add(1U, std::memory_order_release);
        }
        imageWake.notify_one();
    }

    void ImageWorkerLoop() {
        while (true) {
            ImageWorkItem work;
            {
                std::unique_lock lock{imageMutex};
                imageWake.wait(lock, [this] {
                    return stopImageWorkers || !imageWork.empty();
                });
                if (stopImageWorkers) return;
                work = std::move(imageWork.front());
                imageWork.pop_front();
            }

            ImageWorkResult result{
                .kind = work.kind,
                .assetId = work.assetId,
                .contentHash = work.contentHash,
                .frame = work.frame,
                .generation = work.generation,
            };
            const auto workStarted = std::chrono::steady_clock::now();
            double decodeMs = 0.0;
            double displayMs = 0.0;
            double downsampleMs = 0.0;
            double cacheMs = 0.0;
            const auto cancelled = [&work]() noexcept {
                return work.cancelled != nullptr &&
                    work.cancelled->load(std::memory_order_acquire);
            };
            if (cancelled()) {
                result.succeeded = false;
            } else if (work.kind == ImageWorkKind::LoadPosterCache) {
                const auto cacheStarted = std::chrono::steady_clock::now();
                std::error_code directoryError;
                std::filesystem::create_directories(
                    work.cacheDirectory,
                    directoryError);
                kb::scene::ParticleEffectLoadResult loaded =
                    kb::scene::ParticleEffectAssetIO::LoadDetailed(
                        work.assetPath);
                if (loaded.Succeeded()) {
                    result.asset = std::make_shared<
                        const kb::scene::ParticleEffectAsset>(
                        std::move(*loaded.asset));
                    result.timeline = ParticleThumbnailTimeline::Plan(
                        *result.asset);
                    if (result.timeline.usesBoundedPreviewWindow) {
                        std::ostringstream detail;
                        detail << "asset=" << work.assetId
                               << " authoredSeconds="
                               << result.asset->durationSeconds
                               << " previewSeconds="
                               << result.timeline.durationSeconds;
                        diagnostics::EditorLagTrace::Marker(
                            "particle-thumbnail-bounded-window",
                            detail.str());
                    }
                    result.frames.resize(result.timeline.frameCount);
                    const std::uint32_t posterFrame =
                        ParticleThumbnailTimeline::PosterFrame(
                            result.timeline);
                    if (!cancelled()) {
                        static_cast<void>(LoadThumbnailCache(
                            ThumbnailPath(
                                work.cacheDirectory, work.assetId,
                                work.contentHash,
                                static_cast<int>(posterFrame)),
                            result.frames[posterFrame]));
                    }
                    result.succeeded = !cancelled();
                } else {
                    std::ostringstream detail;
                    detail << "asset=" << work.assetId
                           << " path=" << work.assetPath.generic_string();
                    if (!loaded.diagnostics.empty()) {
                        detail << " reason="
                               << loaded.diagnostics.front().message;
                    }
                    diagnostics::EditorLagTrace::Marker(
                        "particle-thumbnail-load-failed",
                        detail.str());
                }
                result.succeeded = result.asset != nullptr &&
                    result.succeeded;
                cacheMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - cacheStarted).count();
            } else if (work.kind == ImageWorkKind::LoadAnimationCache) {
                const auto cacheStarted = std::chrono::steady_clock::now();
                result.timeline = work.timeline;
                result.frames.resize(result.timeline.frameCount);
                const std::uint32_t posterFrame =
                    ParticleThumbnailTimeline::PosterFrame(result.timeline);
                for (std::uint32_t frame = 0U;
                     frame < result.timeline.frameCount; ++frame) {
                    if (frame == posterFrame || cancelled()) continue;
                    static_cast<void>(LoadThumbnailCache(
                        ThumbnailPath(
                            work.cacheDirectory, work.assetId,
                            work.contentHash,
                            static_cast<int>(frame)),
                        result.frames[frame]));
                }
                result.succeeded = !cancelled();
                cacheMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - cacheStarted).count();
            } else {
                EditorParticleThumbnailImage image;
                bool displayEncoded = false;
                const auto decodeStarted = std::chrono::steady_clock::now();
                result.succeeded = DecodeCapturePixels(
                    std::move(work.pixels), image, displayEncoded);
                decodeMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - decodeStarted).count();
                if (result.succeeded && !cancelled()) {
                    const auto downsampleStarted =
                        std::chrono::steady_clock::now();
                    Downsample(image, kThumbnailSize);
                    downsampleMs = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - downsampleStarted)
                                           .count();
                    const auto displayStarted = std::chrono::steady_clock::now();
                    if (!displayEncoded) ApplyDisplayTransform(image);
                    displayMs = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - displayStarted).count();
                    const auto cacheStarted = std::chrono::steady_clock::now();
                    if (!cancelled()) {
                        result.cacheWriteSucceeded = SaveThumbnailCache(
                            ThumbnailPath(
                                work.cacheDirectory, work.assetId,
                                work.contentHash, work.frame),
                            image);
                    } else {
                        result.succeeded = false;
                    }
                    cacheMs = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - cacheStarted).count();
                    result.frames.push_back(std::move(image));
                }
            }
            const double workMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - workStarted).count();
            std::ostringstream workDetail;
            workDetail << "kind="
                       << (work.kind == ImageWorkKind::LoadPosterCache
                               ? "poster-cache-load"
                               : work.kind ==
                                      ImageWorkKind::LoadAnimationCache
                                   ? "animation-cache-load"
                                   : work.kind ==
                                          ImageWorkKind::ProcessPosterCapture
                                       ? "poster-process"
                                       : "animation-process")
                       << " asset=" << work.assetId
                       << " frame=" << work.frame
                       << " success=" << (result.succeeded ? 1 : 0)
                       << " cacheWrite="
                       << (result.cacheWriteSucceeded ? 1 : 0)
                       << " decodeMs=" << decodeMs
                       << " displayMs=" << displayMs
                       << " downsampleMs=" << downsampleMs
                       << " cacheMs=" << cacheMs;
            diagnostics::EditorLagTrace::Slow(
                "particle-thumbnail-worker",
                diagnostics::EditorLagTrace::NextEventId(),
                workMs,
                workDetail.str(),
                4.0);

            {
                std::scoped_lock lock{imageMutex};
                if (stopImageWorkers) return;
                imageResults.push_back(std::move(result));
            }
        }
    }

    void StopImageWorkers() noexcept {
        for (auto& [assetId, entry] : entries) {
            static_cast<void>(assetId);
            entry.cancelled->store(true, std::memory_order_release);
        }
        {
            std::scoped_lock lock{imageMutex};
            stopImageWorkers = true;
            imageWork.clear();
        }
        imageWake.notify_all();
        for (std::thread& worker : imageWorkers) {
            if (worker.joinable()) worker.join();
        }
        {
            std::scoped_lock lock{imageMutex};
            imageResults.clear();
        }
        pendingImageWork.store(0U, std::memory_order_release);
    }

    void CancelOutstandingImageWork(bool includeReadyEntries) noexcept {
        for (auto& [assetId, entry] : entries) {
            static_cast<void>(assetId);
            if (includeReadyEntries || entry.state != EntryState::Ready) {
                entry.cancelled->store(true, std::memory_order_release);
            }
        }
        std::uint32_t discarded = 0U;
        {
            std::scoped_lock lock{imageMutex};
            const auto queuedBefore = imageWork.size();
            std::erase_if(imageWork, [](const ImageWorkItem& work) {
                return work.cancelled != nullptr &&
                    work.cancelled->load(std::memory_order_acquire);
            });
            discarded += static_cast<std::uint32_t>(
                queuedBefore - imageWork.size());
            // Results have already completed their worker slot. All results
            // present during cancellation belong to entries that are about to
            // be removed or invalidated, so retire their accounting now.
            discarded += static_cast<std::uint32_t>(imageResults.size());
            imageResults.clear();
        }
        if (discarded != 0U) {
            pendingImageWork.fetch_sub(discarded, std::memory_order_acq_rel);
        }
    }

    void PumpImageResults() {
        std::deque<ImageWorkResult> completed;
        {
            std::scoped_lock lock{imageMutex};
            completed.swap(imageResults);
        }
        for (ImageWorkResult& result : completed) {
            pendingImageWork.fetch_sub(1U, std::memory_order_acq_rel);
            const auto found = entries.find(result.assetId);
            if (found == entries.end() ||
                found->second.contentHash != result.contentHash ||
                found->second.generation != result.generation) {
                continue;
            }
            Entry& entry = found->second;
            if (result.kind == ImageWorkKind::LoadPosterCache) {
                if (entry.state != EntryState::CacheLoading) continue;
                entry.asset = std::move(result.asset);
                entry.timeline = result.timeline;
                if (!result.succeeded || entry.asset == nullptr) {
                    entry.state = EntryState::Failed;
                } else {
                    entry.frames = std::move(result.frames);
                    entry.processedFrames = static_cast<int>(
                        std::ranges::count_if(
                            entry.frames,
                            [](const EditorParticleThumbnailImage& image) {
                                return !image.bgra.empty();
                            }));
                    const std::uint32_t posterFrame =
                        ParticleThumbnailTimeline::PosterFrame(
                            entry.timeline);
                    if (posterFrame < entry.frames.size() &&
                        !entry.frames[posterFrame].bgra.empty()) {
                        entry.state = EntryState::AnimationCacheLoading;
                        EnqueueImageWork(ImageWorkItem{
                            .kind = ImageWorkKind::LoadAnimationCache,
                            .assetId = result.assetId,
                            .contentHash = result.contentHash,
                            .generation = result.generation,
                            .cacheDirectory = entry.cacheDirectory,
                            .cancelled = entry.cancelled,
                            .timeline = entry.timeline,
                        });
                    } else {
                        entry.state = EntryState::PosterQueued;
                        if (std::ranges::find(
                                posterQueue,
                                kb::assets::AssetId{result.assetId}) ==
                            posterQueue.end()) {
                            posterQueue.push_back(
                                kb::assets::AssetId{result.assetId});
                        }
                    }
                }
                ++revision;
                continue;
            }

            if (result.kind == ImageWorkKind::LoadAnimationCache) {
                if (entry.state != EntryState::AnimationCacheLoading) {
                    continue;
                }
                if (!result.succeeded ||
                    result.frames.size() != entry.timeline.frameCount) {
                    entry.state = EntryState::Failed;
                    ++revision;
                    continue;
                }
                for (std::size_t frame = 0U;
                     frame < result.frames.size(); ++frame) {
                    if (entry.frames[frame].bgra.empty() &&
                        !result.frames[frame].bgra.empty()) {
                        entry.frames[frame] =
                            std::move(result.frames[frame]);
                        ++entry.processedFrames;
                    }
                }
                if (entry.processedFrames == static_cast<int>(
                        entry.timeline.frameCount)) {
                    entry.state = EntryState::Ready;
                } else {
                    entry.state = EntryState::Queued;
                    if (std::ranges::find(
                            queue, kb::assets::AssetId{result.assetId}) ==
                        queue.end()) {
                        queue.push_back(kb::assets::AssetId{result.assetId});
                    }
                }
                ++revision;
                continue;
            }

            if (entry.state != EntryState::PosterCapturing &&
                entry.state != EntryState::PosterProcessing &&
                entry.state != EntryState::Capturing &&
                entry.state != EntryState::Processing) {
                continue;
            }
            if (!result.succeeded || result.frames.size() != 1U ||
                result.frame < 0 ||
                result.frame >= static_cast<int>(entry.timeline.frameCount)) {
                entry.state = EntryState::Failed;
                ++revision;
                continue;
            }
            if (!result.cacheWriteSucceeded) {
                EditorCrashBreadcrumbs::WriteValue(
                    "particle_thumbnail", "cache write failed",
                    result.assetId);
            }
            if (entry.frames.size() != entry.timeline.frameCount) {
                entry.frames.resize(entry.timeline.frameCount);
            }
            EditorParticleThumbnailImage& target =
                entry.frames[static_cast<std::size_t>(result.frame)];
            if (target.bgra.empty()) {
                target = std::move(result.frames.front());
                ++entry.processedFrames;
            }
            if (entry.state == EntryState::PosterProcessing) {
                entry.state = EntryState::Queued;
                if (std::ranges::find(
                        queue, kb::assets::AssetId{result.assetId}) ==
                    queue.end()) {
                    queue.push_back(kb::assets::AssetId{result.assetId});
                }
                EditorCrashBreadcrumbs::WriteValue(
                    "particle_thumbnail", "poster ready",
                    result.assetId);
            } else if (entry.processedFrames == static_cast<int>(
                    entry.timeline.frameCount)) {
                entry.state = EntryState::Ready;
                EditorCrashBreadcrumbs::WriteValue(
                    "particle_thumbnail", "capture sequence ready",
                    result.assetId);
            }
            ++revision;
        }
    }

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
        activeGeneration = 0U;
        simulatedSteps = 0;
        captureFrames.clear();
        nextCapture = 0;
        activePoster = false;
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
        activeGeneration = 0U;
        simulatedSteps = 0;
        captureFrames.clear();
        nextCapture = 0;
        activePoster = false;
        capture = {};
        ++revision;
        if (posterQueue.empty() && queue.empty()) ResetSession(&viewport);
    }

    [[nodiscard]] bool HasUnresolvedPosters() const noexcept {
        return std::ranges::any_of(entries, [](const auto& item) {
            const EntryState state = item.second.state;
            return state == EntryState::CacheLoading ||
                state == EntryState::PosterQueued ||
                state == EntryState::PosterCapturing ||
                state == EntryState::PosterProcessing;
        });
    }

    [[nodiscard]] bool BeginNext(
        EditorSceneContext& sceneContext,
        EditorSceneBgfxViewport& viewport) {
        kb::assets::AssetManager& manager =
            sceneContext.Scene().Assets().Manager();
        while (true) {
            const bool posterPass = !posterQueue.empty();
            if (!posterPass && HasUnresolvedPosters()) return false;
            std::deque<kb::assets::AssetId>& selectedQueue =
                posterPass ? posterQueue : queue;
            if (selectedQueue.empty()) break;
            const kb::assets::AssetId id = selectedQueue.front();
            selectedQueue.pop_front();
            auto entry = entries.find(id.value);
            if (entry == entries.end() ||
                entry->second.state != (posterPass
                    ? EntryState::PosterQueued
                    : EntryState::Queued)) {
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
            if (entry->second.asset == nullptr) {
                entry->second.state = EntryState::Failed;
                ++revision;
                continue;
            }
            const kb::scene::ParticleEffectAsset& asset =
                *entry->second.asset;

            kb::particle_editor::ParticleEditorResult started;
            if (session == nullptr) {
                session = std::make_unique<
                    kb::particle_editor::ParticlePreviewSession>();
                started = session->Start(
                    sceneContext.Project(), manager.Registry(), id,
                    metadata->virtualPath, asset);
                fullSyncRequired = true;
                presented = false;
            } else {
                started = session->RetargetWorkingCopy(
                    id, metadata->virtualPath, asset);
            }
            if (!started.Succeeded()) {
                entry->second.state = EntryState::Failed;
                ResetSession(&viewport);
                ++revision;
                continue;
            }

            activeAsset = id;
            activeContentHash = metadata->contentHash;
            activeGeneration = entry->second.generation;
            simulatedSteps = 0;
            captureFrames.clear();
            if (posterPass) {
                captureFrames.push_back(static_cast<int>(
                    ParticleThumbnailTimeline::PosterFrame(
                        entry->second.timeline)));
            } else {
                if (entry->second.frames.size() !=
                    entry->second.timeline.frameCount) {
                    entry->second.frames.resize(
                        entry->second.timeline.frameCount);
                }
                for (std::uint32_t frame = 0U;
                     frame < entry->second.timeline.frameCount; ++frame) {
                    if (entry->second.frames[frame].bgra.empty()) {
                        captureFrames.push_back(static_cast<int>(frame));
                    }
                }
            }
            if (captureFrames.empty()) {
                entry->second.state = EntryState::Ready;
                ++revision;
                continue;
            }
            nextCapture = 0;
            activePoster = posterPass;
            capture = {};
            if (entry->second.frames.size() !=
                entry->second.timeline.frameCount) {
                entry->second.frames.resize(
                    entry->second.timeline.frameCount);
            }
            entry->second.state = posterPass
                ? EntryState::PosterCapturing
                : EntryState::Capturing;
            EditorCrashBreadcrumbs::WriteValue(
                "particle_thumbnail",
                posterPass ? "poster capture started"
                           : "capture sequence started",
                id.value);
            return true;
        }
        ResetSession(&viewport);
        return false;
    }

    void BeginCaptureFrame(
        EditorSceneBgfxViewport& viewport,
        HWND host,
        const RECT& stagingRect,
        bool ownsPaintLayout) {
        if (ownsPaintLayout) viewport.BeginPaintLayout(host);
        viewport.Present(
            host, stagingRect, session->PreviewScene(),
            ThumbnailSettings(*session, fullSyncRequired));
        fullSyncRequired = false;
        presented = true;
    }

    [[nodiscard]] bool Tick(
        EditorSceneContext& sceneContext,
        EditorSceneBgfxViewport& viewport,
        HWND host,
        const RECT& stagingRect,
        bool ownsPaintLayout) {
        PumpImageResults();
        if (host == nullptr || IsWindow(host) == 0 ||
            stagingRect.right <= stagingRect.left ||
            stagingRect.bottom <= stagingRect.top) {
            return false;
        }
        if (activeAsset.IsValid()) {
            const auto active = entries.find(activeAsset.value);
            if (active == entries.end() ||
                active->second.contentHash != activeContentHash ||
                active->second.generation != activeGeneration) {
                ResetSession(&viewport);
            }
        }
        if (!activeAsset.IsValid() &&
            !BeginNext(sceneContext, viewport)) {
            return pendingImageWork.load(std::memory_order_acquire) != 0U;
        }
        if (session == nullptr) return false;

        auto entry = entries.find(activeAsset.value);
        if (entry == entries.end()) {
            FinishActive(viewport, EntryState::Failed);
            return true;
        }
        if (entry->second.state == EntryState::Failed) {
            FinishActive(viewport, EntryState::Failed);
            return true;
        }

        if (capture.id != 0U) {
            const kb::scene::SceneScreenCaptureStatus status =
                kb::scene::SceneRenderFeedback::ScreenCaptureStatus(
                    session->PreviewScene(), capture.id);
            if (status == kb::scene::SceneScreenCaptureStatus::Completed) {
                std::optional<kb::scene::SceneScreenCapturePixels> pixels =
                    kb::scene::SceneRenderFeedback::TakeScreenCapturePixels(
                        session->PreviewScene(), capture.id);
                if (!pixels.has_value()) {
                    FinishActive(viewport, EntryState::Failed);
                    return true;
                }
                EnqueueImageWork(ImageWorkItem{
                    .kind = activePoster
                        ? ImageWorkKind::ProcessPosterCapture
                        : ImageWorkKind::ProcessAnimationCapture,
                    .assetId = activeAsset.value,
                    .contentHash = activeContentHash,
                    .frame = captureFrames[
                        static_cast<std::size_t>(nextCapture)],
                    .generation = activeGeneration,
                    .cacheDirectory = entry->second.cacheDirectory,
                    .cancelled = entry->second.cancelled,
                    .pixels = std::move(*pixels),
                });
                ++nextCapture;
                capture = {};
                ++revision;
                if (nextCapture >= static_cast<int>(
                        captureFrames.size())) {
                    FinishActive(
                        viewport,
                        activePoster ? EntryState::PosterProcessing
                                     : EntryState::Processing);
                }
                return true;
            }
            ++capture.framesWaited;
            if (status == kb::scene::SceneScreenCaptureStatus::Failed ||
                capture.framesWaited > kCaptureFrameBudget) {
                EditorCrashBreadcrumbs::WriteValue(
                    "particle_thumbnail",
                    status == kb::scene::SceneScreenCaptureStatus::Failed
                        ? "capture failed"
                        : "capture timed out",
                    activeAsset.value);
                FinishActive(viewport, EntryState::Failed);
                return true;
            }
            const std::uint32_t completedFrame =
                viewport.RendererCompletedFrame();
            if (ownsPaintLayout && completedFrame ==
                    capture.lastObservedRendererFrame) {
                static_cast<void>(viewport.AdvanceAsyncReadbacks());
            }
            capture.lastObservedRendererFrame =
                viewport.RendererCompletedFrame();
            return true;
        }

        if (nextCapture < 0 ||
            nextCapture >= static_cast<int>(captureFrames.size())) {
            FinishActive(viewport, EntryState::Failed);
            return true;
        }
        const int frame = captureFrames[
            static_cast<std::size_t>(nextCapture)];
        if (frame < 0 || frame >= static_cast<int>(
                entry->second.timeline.frameCount)) {
            FinishActive(viewport, EntryState::Failed);
            return true;
        }
        const int captureAt = static_cast<int>(
            ParticleThumbnailTimeline::CaptureStep(
                entry->second.timeline,
                static_cast<std::uint32_t>(frame)));
        const auto simulationStarted = std::chrono::steady_clock::now();
        int stepsThisFrame = 0;
        while (simulatedSteps < captureAt &&
               stepsThisFrame < kMaximumSimulationStepsPerEditorFrame) {
            const auto ticked = session->Tick(
                kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds);
            if (!ticked.Succeeded()) {
                FinishActive(viewport, EntryState::Failed);
                return true;
            }
            ++simulatedSteps;
            ++stepsThisFrame;
            if (std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - simulationStarted)
                    .count() >= kSimulationBudgetMilliseconds) {
                break;
            }
        }
        if (simulatedSteps < captureAt) return true;

        BeginCaptureFrame(
            viewport, host, stagingRect, ownsPaintLayout);
        const std::uint64_t captureId =
            kb::scene::SceneRenderFeedback::RequestScreenCapturePixels(
                session->PreviewScene());
        if (ownsPaintLayout) viewport.EndPaintLayout();
        if (captureId == 0U) return true;
        capture = Capture{
            .id = captureId,
            .framesWaited = 0,
            .lastObservedRendererFrame = viewport.RendererCompletedFrame(),
        };
        return true;
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
    impl_->PumpImageResults();
    auto found = impl_->entries.find(metadata.id.value);
    if (found != impl_->entries.end() &&
        found->second.contentHash != metadata.contentHash) {
        found->second.cancelled->store(true, std::memory_order_release);
        const std::uint64_t generation = impl_->nextGeneration++;
        found->second = Impl::Entry{
            .contentHash = metadata.contentHash,
            .generation = generation,
            .cacheDirectory = ThumbnailCacheDirectory(),
            .state = EntryState::CacheLoading,
        };
        impl_->EnqueueImageWork(ImageWorkItem{
            .kind = ImageWorkKind::LoadPosterCache,
            .assetId = metadata.id.value,
            .contentHash = metadata.contentHash,
            .generation = generation,
            .cacheDirectory = found->second.cacheDirectory,
            .assetPath = metadata.physicalPath,
            .cancelled = found->second.cancelled,
        });
        ++impl_->revision;
    } else if (found == impl_->entries.end()) {
        const std::uint64_t generation = impl_->nextGeneration++;
        Impl::Entry entry{
            .contentHash = metadata.contentHash,
            .generation = generation,
            .cacheDirectory = ThumbnailCacheDirectory(),
            .state = EntryState::CacheLoading,
        };
        found = impl_->entries.emplace(metadata.id.value, std::move(entry)).first;
        impl_->EnqueueImageWork(ImageWorkItem{
            .kind = ImageWorkKind::LoadPosterCache,
            .assetId = metadata.id.value,
            .contentHash = metadata.contentHash,
            .generation = generation,
            .cacheDirectory = found->second.cacheDirectory,
            .assetPath = metadata.physicalPath,
            .cancelled = found->second.cancelled,
        });
        ++impl_->revision;
    }
    if (found->second.frames.empty()) return nullptr;
    const std::size_t requested = static_cast<std::size_t>(
        animationFrame % found->second.frames.size());
    if (!found->second.frames[requested].bgra.empty()) {
        return &found->second.frames[requested];
    }
    const auto available = std::ranges::find_if(
        found->second.frames,
        [](const EditorParticleThumbnailImage& image) {
            return !image.bgra.empty();
        });
    return available == found->second.frames.end() ? nullptr :
        &*available;
}

const EditorParticleThumbnailImage*
EditorParticleThumbnailService::ThumbnailForTime(
    const kb::assets::AssetMetadata& metadata,
    double elapsedSeconds) {
    static_cast<void>(ThumbnailFor(metadata, 0U));
    const auto found = impl_->entries.find(metadata.id.value);
    if (found == impl_->entries.end() ||
        found->second.contentHash != metadata.contentHash ||
        found->second.frames.empty()) {
        return nullptr;
    }
    const Impl::Entry& entry = found->second;
    const std::uint32_t frame = entry.state == EntryState::Ready
        ? ParticleThumbnailTimeline::FrameAtSeconds(
              entry.timeline, elapsedSeconds)
        : 0U;
    if (frame < entry.frames.size() &&
        !entry.frames[frame].bgra.empty()) {
        return &entry.frames[frame];
    }
    const auto available = std::ranges::find_if(
        entry.frames,
        [](const EditorParticleThumbnailImage& image) {
            return !image.bgra.empty();
        });
    return available == entry.frames.end() ? nullptr : &*available;
}

std::uint32_t EditorParticleThumbnailService::AnimationFrameForTime(
    const kb::assets::AssetMetadata& metadata,
    double elapsedSeconds) const noexcept {
    const auto found = impl_->entries.find(metadata.id.value);
    if (found == impl_->entries.end() ||
        found->second.contentHash != metadata.contentHash ||
        found->second.state != EntryState::Ready) {
        return 0U;
    }
    return ParticleThumbnailTimeline::FrameAtSeconds(
        found->second.timeline, elapsedSeconds);
}

std::uint32_t EditorParticleThumbnailService::AnimationFrameCount(
    const kb::assets::AssetMetadata& metadata) const noexcept {
    const auto found = impl_->entries.find(metadata.id.value);
    return found == impl_->entries.end() ||
            found->second.contentHash != metadata.contentHash ||
            found->second.state != EntryState::Ready
        ? 0U
        : static_cast<std::uint32_t>(found->second.frames.size());
}

float EditorParticleThumbnailService::AnimationDurationSeconds(
    const kb::assets::AssetMetadata& metadata) const noexcept {
    const auto found = impl_->entries.find(metadata.id.value);
    return found == impl_->entries.end() ||
            found->second.contentHash != metadata.contentHash
        ? 0.0F
        : found->second.timeline.durationSeconds;
}

bool EditorParticleThumbnailService::Tick(
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& viewport,
    HWND host,
    const RECT& stagingRect) {
    const auto started = std::chrono::steady_clock::now();
    const bool progressed = impl_->Tick(
        sceneContext, viewport, host, stagingRect, true);
    const double elapsedMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    if (elapsedMs >= 2.0) {
        std::ostringstream detail;
        detail << "mode=standalone progressed=" << (progressed ? 1 : 0)
               << " active=" << (impl_->activeAsset.IsValid() ? 1 : 0)
               << " posters=" << impl_->posterQueue.size()
               << " queued=" << impl_->queue.size()
               << " worker="
               << impl_->pendingImageWork.load(std::memory_order_acquire);
        diagnostics::EditorLagTrace::Slow(
            "particle-thumbnail-scheduler",
            diagnostics::EditorLagTrace::NextEventId(),
            elapsedMs, detail.str(), 2.0);
    }
    return progressed;
}

bool EditorParticleThumbnailService::TickWithinFrame(
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& viewport,
    HWND host,
    const RECT& stagingRect) {
    const auto started = std::chrono::steady_clock::now();
    const bool progressed = impl_->Tick(
        sceneContext, viewport, host, stagingRect, false);
    const double elapsedMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    if (elapsedMs >= 2.0) {
        std::ostringstream detail;
        detail << "mode=shared-frame progressed=" << (progressed ? 1 : 0)
               << " active=" << (impl_->activeAsset.IsValid() ? 1 : 0)
               << " posters=" << impl_->posterQueue.size()
               << " queued=" << impl_->queue.size()
               << " worker="
               << impl_->pendingImageWork.load(std::memory_order_acquire);
        diagnostics::EditorLagTrace::Slow(
            "particle-thumbnail-scheduler",
            diagnostics::EditorLagTrace::NextEventId(),
            elapsedMs, detail.str(), 2.0);
    }
    return progressed;
}

bool EditorParticleThumbnailService::HasPendingWork() const noexcept {
    return impl_->activeAsset.IsValid() || !impl_->posterQueue.empty() ||
        !impl_->queue.empty() ||
        impl_->pendingImageWork.load(std::memory_order_acquire) != 0U;
}

std::uint64_t EditorParticleThumbnailService::Revision() const noexcept {
    return impl_->revision;
}

void EditorParticleThumbnailService::CancelPendingWork(
    EditorSceneBgfxViewport* viewport) noexcept {
    const bool changed = impl_->activeAsset.IsValid() ||
        !impl_->posterQueue.empty() || !impl_->queue.empty() ||
        impl_->pendingImageWork.load(std::memory_order_acquire) != 0U;
    impl_->CancelOutstandingImageWork(false);
    impl_->ResetSession(viewport);
    impl_->posterQueue.clear();
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
    impl_->CancelOutstandingImageWork(true);
    impl_->ResetSession(viewport);
    impl_->entries.clear();
    impl_->posterQueue.clear();
    impl_->queue.clear();
    ++impl_->revision;
}

EditorParticleThumbnailService& EditorParticleThumbnailCache() {
    static EditorParticleThumbnailService cache;
    return cache;
}

} // namespace kb::editor
