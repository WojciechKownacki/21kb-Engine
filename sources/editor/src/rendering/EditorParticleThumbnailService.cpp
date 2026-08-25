#include "rendering/EditorParticleThumbnailService.hpp"

#include "app/EditorCrashBreadcrumbs.hpp"
#include "diagnostics/EditorLagTrace.hpp"
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

constexpr std::uint32_t kThumbnailRenderSize = 512U;
constexpr int kThumbnailSize = 128;
constexpr int kMinimumSimulationSteps = 6;
constexpr int kMaximumSimulationSteps = 30;
constexpr int kAnimationFrameCount = 3;
constexpr int kCaptureFrameBudget = 600;
constexpr std::size_t kImageWorkerCount = 2U;
constexpr std::uint64_t kParticleThumbnailCaptureViewportKey =
    0x5041525454484D42ULL;

enum class EntryState : std::uint8_t {
    CacheLoading,
    Queued,
    Capturing,
    Processing,
    Ready,
    Failed,
};

enum class ImageWorkKind : std::uint8_t {
    LoadCache,
    ProcessCapture,
};

struct ImageWorkItem {
    ImageWorkKind kind = ImageWorkKind::LoadCache;
    std::uint64_t assetId = 0U;
    std::uint64_t contentHash = 0U;
    int frame = 0;
    std::filesystem::path cacheDirectory;
    kb::scene::SceneScreenCapturePixels pixels;
};

struct ImageWorkResult {
    ImageWorkKind kind = ImageWorkKind::LoadCache;
    std::uint64_t assetId = 0U;
    std::uint64_t contentHash = 0U;
    int frame = 0;
    bool succeeded = false;
    bool cacheWriteSucceeded = true;
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
            "_f" + std::to_string(frame) + "_v4.kbthumb");
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
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
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
        std::filesystem::path cacheDirectory;
        EntryState state = EntryState::CacheLoading;
        std::vector<EditorParticleThumbnailImage> frames;
        int processedFrames = 0;
    };

    struct Capture {
        std::uint64_t id = 0U;
        int framesWaited = 0;
        std::uint32_t lastObservedRendererFrame = 0U;
    };

    std::unordered_map<std::uint64_t, Entry> entries;
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
    int simulatedSteps = 0;
    int targetSteps = kMinimumSimulationSteps;
    std::array<int, kAnimationFrameCount> captureSteps{};
    int nextFrame = 0;
    bool fullSyncRequired = true;
    bool presented = false;
    Capture capture{};
    std::uint64_t revision = 1U;

    Impl() {
        for (std::thread& worker : imageWorkers) {
            worker = std::thread{[this] { ImageWorkerLoop(); }};
        }
    }

    ~Impl() { StopImageWorkers(); }

    void EnqueueImageWork(ImageWorkItem work) {
        {
            std::scoped_lock lock{imageMutex};
            imageWork.push_back(std::move(work));
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
            };
            const auto workStarted = std::chrono::steady_clock::now();
            double decodeMs = 0.0;
            double displayMs = 0.0;
            double downsampleMs = 0.0;
            double cacheMs = 0.0;
            if (work.kind == ImageWorkKind::LoadCache) {
                const auto cacheStarted = std::chrono::steady_clock::now();
                std::error_code directoryError;
                std::filesystem::create_directories(
                    work.cacheDirectory,
                    directoryError);
                result.frames.reserve(kAnimationFrameCount);
                for (int frame = 0; frame < kAnimationFrameCount; ++frame) {
                    EditorParticleThumbnailImage image;
                    if (!LoadThumbnailCache(
                            ThumbnailPath(
                                work.cacheDirectory, work.assetId,
                                work.contentHash, frame),
                            image)) {
                        result.frames.clear();
                        break;
                    }
                    result.frames.push_back(std::move(image));
                }
                result.succeeded =
                    result.frames.size() == kAnimationFrameCount;
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
                if (result.succeeded) {
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
                    result.cacheWriteSucceeded = SaveThumbnailCache(
                        ThumbnailPath(
                            work.cacheDirectory, work.assetId,
                            work.contentHash, work.frame),
                        image);
                    cacheMs = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - cacheStarted).count();
                    result.frames.push_back(std::move(image));
                }
            }
            const double workMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - workStarted).count();
            std::ostringstream workDetail;
            workDetail << "kind="
                       << (work.kind == ImageWorkKind::LoadCache
                               ? "cache-load"
                               : "raw-process")
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
                found->second.contentHash != result.contentHash) {
                continue;
            }
            Entry& entry = found->second;
            if (result.kind == ImageWorkKind::LoadCache) {
                if (entry.state != EntryState::CacheLoading) continue;
                if (result.succeeded) {
                    entry.frames = std::move(result.frames);
                    entry.processedFrames = kAnimationFrameCount;
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

            if (entry.state != EntryState::Capturing &&
                entry.state != EntryState::Processing) {
                continue;
            }
            if (!result.succeeded || result.frames.size() != 1U ||
                result.frame < 0 || result.frame >= kAnimationFrameCount) {
                entry.state = EntryState::Failed;
                ++revision;
                continue;
            }
            if (!result.cacheWriteSucceeded) {
                EditorCrashBreadcrumbs::WriteValue(
                    "particle_thumbnail", "cache write failed",
                    result.assetId);
            }
            if (entry.frames.size() != kAnimationFrameCount) {
                entry.frames.resize(kAnimationFrameCount);
            }
            EditorParticleThumbnailImage& target =
                entry.frames[static_cast<std::size_t>(result.frame)];
            if (target.bgra.empty()) {
                target = std::move(result.frames.front());
                ++entry.processedFrames;
            }
            if (entry.processedFrames == kAnimationFrameCount) {
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
            entry->second.frames.assign(
                kAnimationFrameCount, EditorParticleThumbnailImage{});
            entry->second.processedFrames = 0;
            entry->second.state = EntryState::Capturing;
            EditorCrashBreadcrumbs::WriteValue(
                "particle_thumbnail", "capture sequence started", id.value);
            return true;
        }
        ResetSession(&viewport);
        return false;
    }

    void BeginPresent(
        EditorSceneBgfxViewport& viewport,
        HWND host,
        const RECT& stagingRect) {
        viewport.BeginPaintLayout(host);
        viewport.Present(
            host, stagingRect, session->PreviewScene(),
            ThumbnailSettings(*session, fullSyncRequired));
        fullSyncRequired = false;
        presented = true;
    }

    static void EndPresent(EditorSceneBgfxViewport& viewport) {
        viewport.EndPaintLayout();
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
        found->second = Impl::Entry{
            .contentHash = metadata.contentHash,
            .cacheDirectory = ThumbnailCacheDirectory(),
            .state = EntryState::CacheLoading,
        };
        impl_->EnqueueImageWork(ImageWorkItem{
            .kind = ImageWorkKind::LoadCache,
            .assetId = metadata.id.value,
            .contentHash = metadata.contentHash,
            .cacheDirectory = found->second.cacheDirectory,
        });
        ++impl_->revision;
    } else if (found == impl_->entries.end()) {
        Impl::Entry entry{
            .contentHash = metadata.contentHash,
            .cacheDirectory = ThumbnailCacheDirectory(),
            .state = EntryState::CacheLoading,
        };
        found = impl_->entries.emplace(metadata.id.value, std::move(entry)).first;
        impl_->EnqueueImageWork(ImageWorkItem{
            .kind = ImageWorkKind::LoadCache,
            .assetId = metadata.id.value,
            .contentHash = metadata.contentHash,
            .cacheDirectory = found->second.cacheDirectory,
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

bool EditorParticleThumbnailService::Tick(
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& viewport,
    HWND host,
    const RECT& stagingRect) {
    impl_->PumpImageResults();
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
        return impl_->pendingImageWork.load(std::memory_order_acquire) != 0U;
    }
    if (impl_->session == nullptr) return false;

    auto entry = impl_->entries.find(impl_->activeAsset.value);
    if (entry == impl_->entries.end()) {
        impl_->FinishActive(viewport, EntryState::Failed);
        return true;
    }
    if (entry->second.state == EntryState::Failed) {
        impl_->FinishActive(viewport, EntryState::Failed);
        return true;
    }

    if (impl_->capture.id != 0U) {
        const kb::scene::SceneScreenCaptureStatus status =
            kb::scene::SceneRenderFeedback::ScreenCaptureStatus(
                impl_->session->PreviewScene(), impl_->capture.id);
        if (status == kb::scene::SceneScreenCaptureStatus::Completed) {
            std::optional<kb::scene::SceneScreenCapturePixels> pixels =
                kb::scene::SceneRenderFeedback::TakeScreenCapturePixels(
                    impl_->session->PreviewScene(), impl_->capture.id);
            if (!pixels.has_value()) {
                impl_->FinishActive(viewport, EntryState::Failed);
                return true;
            }
            impl_->EnqueueImageWork(ImageWorkItem{
                .kind = ImageWorkKind::ProcessCapture,
                .assetId = impl_->activeAsset.value,
                .contentHash = impl_->activeContentHash,
                .frame = impl_->nextFrame,
                .cacheDirectory = entry->second.cacheDirectory,
                .pixels = std::move(*pixels),
            });
            ++impl_->nextFrame;
            impl_->capture = {};
            ++impl_->revision;
            if (impl_->nextFrame >= kAnimationFrameCount) {
                impl_->FinishActive(viewport, EntryState::Processing);
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
        const std::uint32_t completedFrame =
            viewport.RendererCompletedFrame();
        if (completedFrame ==
            impl_->capture.lastObservedRendererFrame) {
            static_cast<void>(viewport.AdvanceAsyncReadbacks());
        }
        impl_->capture.lastObservedRendererFrame =
            viewport.RendererCompletedFrame();
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

    impl_->BeginPresent(viewport, host, stagingRect);
    const std::uint64_t captureId =
        kb::scene::SceneRenderFeedback::RequestScreenCapturePixels(
            impl_->session->PreviewScene());
    impl_->EndPresent(viewport);
    if (captureId == 0U) return true;
    impl_->capture = Impl::Capture{
        .id = captureId,
        .framesWaited = 0,
        .lastObservedRendererFrame = viewport.RendererCompletedFrame(),
    };
    return true;
}

bool EditorParticleThumbnailService::HasPendingWork() const noexcept {
    return impl_->activeAsset.IsValid() || !impl_->queue.empty() ||
        impl_->pendingImageWork.load(std::memory_order_acquire) != 0U;
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
