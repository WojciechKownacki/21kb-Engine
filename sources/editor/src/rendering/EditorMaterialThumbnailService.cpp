#include "rendering/EditorMaterialThumbnailService.hpp"

#if defined(_WIN32)
#include "app/EditorCrashBreadcrumbs.hpp"
#include "project/EditorProjectPaths.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "rendering/HeroIconGdiplusRuntime.hpp"
#include "rendering/MaterialPreviewRenderPolicy.hpp"
#include "rendering/MaterialPreviewViewportKeys.hpp"
#include "rendering/ProjectFilesAssetIconResolver.hpp"
#include "scene/EditorSceneContext.hpp"

#include <bx/math.h>

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <utility>
#include <vector>
#include <system_error>

namespace kb::editor {
namespace {

// Rendered 4x oversized and box-filtered down on load: the capture has no MSAA, so the silhouette would
// otherwise be a hard staircase. Downsampling a binary coverage mask is what turns it into a smooth,
// anti-aliased edge - and it costs one capture, once per material.
constexpr std::uint32_t kThumbnailRenderSize = 1024U;
constexpr int kThumbnailSize = 256;
constexpr int kSupersample = 4;
constexpr float kBallFraction = kMaterialPreviewBallFraction;
// A capture normally lands within a few frames. The budget bounds a backend that can never deliver one -
// but a capture can also lose its turn to something else (a material re-saved mid-flight, the panel hidden
// while it was in flight), and that is transient, so a lapsed attempt is retried a couple of times before
// the tile is left with its painted stand-in for good.
constexpr int kCaptureFrameBudget = 600;
constexpr int kCaptureAttempts = 3;

[[nodiscard]] std::string Hex64(std::uint64_t value) {
    std::ostringstream output;
    output << std::hex << std::nouppercase;
    output.width(16);
    output.fill('0');
    output << value;
    return output.str();
}

[[nodiscard]] std::filesystem::path ThumbnailPath(const kb::assets::AssetMetadata& metadata) {
    const std::filesystem::path directory = EditorProjectPaths::ProjectRoot() / "Saved" / "Cache" / "Thumbnails";
    return directory / ("material_" + Hex64(metadata.id.value) + "_" + Hex64(metadata.contentHash) + "_v4.png");
}

[[nodiscard]] std::filesystem::path ThumbnailPath(std::uint64_t assetId, std::uint64_t contentHash) {
    const std::filesystem::path directory = EditorProjectPaths::ProjectRoot() / "Saved" / "Cache" / "Thumbnails";
    return directory / ("material_" + Hex64(assetId) + "_" + Hex64(contentHash) + "_v4.png");
}

// The renderer captures the scene colour as-is: a linear, un-tonemapped snapshot (documented on
// RendererScreenCapture). The panel previews reach the screen through the display transform, so the
// thumbnail has to run the same one on the CPU - exposure, ACES, gamma - or it looks washed-out dark
// next to the very preview it is supposed to mirror.
[[nodiscard]] float AcesToneMap(float value) noexcept {
    constexpr float a = 2.51F;
    constexpr float b = 0.03F;
    constexpr float c = 2.43F;
    constexpr float d = 0.59F;
    constexpr float e = 0.14F;
    const float mapped = (value * (a * value + b)) / (value * (c * value + d) + e);
    return std::clamp(mapped, 0.0F, 1.0F);
}

[[nodiscard]] std::uint32_t ApplyDisplayTransform(std::uint32_t bgra, float exposure, float invGamma) noexcept {
    const auto channel = [](std::uint32_t value) noexcept { return static_cast<float>(value & 0xFFU) / 255.0F; };
    const float blue = AcesToneMap(channel(bgra) * exposure);
    const float green = AcesToneMap(channel(bgra >> 8U) * exposure);
    const float red = AcesToneMap(channel(bgra >> 16U) * exposure);
    const auto encode = [invGamma](float value) noexcept {
        return static_cast<std::uint32_t>(std::clamp(std::pow(value, invGamma), 0.0F, 1.0F) * 255.0F + 0.5F);
    };
    return (bgra & 0xFF000000U) | (encode(red) << 16U) | (encode(green) << 8U) | encode(blue);
}

[[nodiscard]] bool LoadPng(const std::filesystem::path& path, EditorMaterialThumbnailImage& image) {
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        return false;
    }
    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Bitmap bitmap(path.wstring().c_str());
    if (bitmap.GetLastStatus() != Gdiplus::Ok) {
        return false;
    }
    const int width = static_cast<int>(bitmap.GetWidth());
    const int height = static_cast<int>(bitmap.GetHeight());
    if (width <= 0 || height <= 0) {
        return false;
    }

    Gdiplus::Rect bounds(0, 0, width, height);
    Gdiplus::BitmapData data{};
    if (bitmap.LockBits(&bounds, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
        return false;
    }
    image.width = width;
    image.height = height;
    image.bgra.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0U);
    for (int y = 0; y < height; ++y) {
        const auto* row = reinterpret_cast<const std::uint32_t*>(
            static_cast<const std::uint8_t*>(data.Scan0) + static_cast<std::ptrdiff_t>(y) * data.Stride);
        std::copy_n(row, width, image.bgra.begin() + static_cast<std::ptrdiff_t>(y) * width);
    }
    static_cast<void>(bitmap.UnlockBits(&data));
    return true;
}

// The renderer clears the capture to opaque black, so the raw thumbnail is a ball inside a black square.
// Tiles need just the ball: flood the background in from the borders and make exactly those pixels
// transparent. Flooding (rather than "every black pixel") keeps black pixels that belong to the material
// itself opaque - a black material still renders as a ball, not as a hole.
void PunchOutBackground(EditorMaterialThumbnailImage& image) {
    if (image.width <= 0 || image.height <= 0) {
        return;
    }
    const auto index = [&image](int x, int y) noexcept {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x);
    };
    const auto isBackground = [&image](std::size_t at) noexcept {
        return (image.bgra[at] & 0x00FFFFFFU) == 0U;
    };

    std::vector<std::uint8_t> visited(image.bgra.size(), 0U);
    std::vector<std::pair<int, int>> stack;
    const auto push = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= image.width || y >= image.height) {
            return;
        }
        const std::size_t at = index(x, y);
        if (visited[at] != 0U || !isBackground(at)) {
            return;
        }
        visited[at] = 1U;
        stack.emplace_back(x, y);
    };

    for (int x = 0; x < image.width; ++x) {
        push(x, 0);
        push(x, image.height - 1);
    }
    for (int y = 0; y < image.height; ++y) {
        push(0, y);
        push(image.width - 1, y);
    }
    while (!stack.empty()) {
        const auto [x, y] = stack.back();
        stack.pop_back();
        image.bgra[index(x, y)] = 0U;
        push(x - 1, y);
        push(x + 1, y);
        push(x, y - 1);
        push(x, y + 1);
    }

    // GDI's AlphaBlend consumes premultiplied pixels; the ball is fully opaque, the background fully clear.
    for (std::uint32_t& pixel : image.bgra) {
        if ((pixel & 0xFF000000U) == 0U) {
            pixel = 0U;
            continue;
        }
        pixel |= 0xFF000000U;
    }
}

// Box-filters the punched-out render down to tile resolution. Background pixels are transparent black, so
// averaging yields exactly premultiplied RGBA: colour weighted by coverage, alpha equal to coverage. That
// is the anti-aliased silhouette, and it is the form GDI's AlphaBlend wants anyway.
[[nodiscard]] RECT SilhouetteBounds(const EditorMaterialThumbnailImage& image) noexcept {
    RECT bounds{ image.width, image.height, -1, -1 };
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            if (((image.bgra[static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                    static_cast<std::size_t>(x)] >> 24U) & 0xFFU) < 24U) {
                continue;
            }
            bounds.left = std::min<LONG>(bounds.left, x);
            bounds.top = std::min<LONG>(bounds.top, y);
            bounds.right = std::max<LONG>(bounds.right, x);
            bounds.bottom = std::max<LONG>(bounds.bottom, y);
        }
    }
    return bounds;
}

// Box-filters a window of the render into the thumbnail, chosen so the silhouette always ends up covering
// kBallFraction of it. That is what keeps the ball one size everywhere, whatever the render did.
void ResampleNormalised(EditorMaterialThumbnailImage& image, int target) {
    if (image.width <= 0 || image.height <= 0 || target <= 0) {
        return;
    }
    const RECT bounds = SilhouetteBounds(image);
    double windowSize = static_cast<double>(std::max(image.width, image.height));
    double centerX = static_cast<double>(image.width) * 0.5;
    double centerY = static_cast<double>(image.height) * 0.5;
    if (bounds.right >= bounds.left && bounds.bottom >= bounds.top) {
        const double silhouette = static_cast<double>(std::max(bounds.right - bounds.left + 1, bounds.bottom - bounds.top + 1));
        windowSize = std::max(4.0, silhouette / kBallFraction);
        centerX = (static_cast<double>(bounds.left) + static_cast<double>(bounds.right) + 1.0) * 0.5;
        centerY = (static_cast<double>(bounds.top) + static_cast<double>(bounds.bottom) + 1.0) * 0.5;
    }

    std::vector<std::uint32_t> reduced(static_cast<std::size_t>(target) * static_cast<std::size_t>(target), 0U);
    const double step = windowSize / static_cast<double>(target);
    const double originX = centerX - windowSize * 0.5;
    const double originY = centerY - windowSize * 0.5;
    for (int y = 0; y < target; ++y) {
        const int sourceTop = static_cast<int>(std::floor(originY + static_cast<double>(y) * step));
        const int sourceBottom = static_cast<int>(std::floor(originY + static_cast<double>(y + 1) * step));
        for (int x = 0; x < target; ++x) {
            const int sourceLeft = static_cast<int>(std::floor(originX + static_cast<double>(x) * step));
            const int sourceRight = static_cast<int>(std::floor(originX + static_cast<double>(x + 1) * step));
            std::uint32_t blue = 0U;
            std::uint32_t green = 0U;
            std::uint32_t red = 0U;
            std::uint32_t alpha = 0U;
            std::uint32_t samples = 0U;
            for (int sourceY = sourceTop; sourceY <= sourceBottom; ++sourceY) {
                if (sourceY < 0 || sourceY >= image.height) {
                    ++samples;
                    continue;
                }
                for (int sourceX = sourceLeft; sourceX <= sourceRight; ++sourceX) {
                    ++samples;
                    if (sourceX < 0 || sourceX >= image.width) {
                        continue;
                    }
                    const std::uint32_t pixel = image.bgra[static_cast<std::size_t>(sourceY) *
                        static_cast<std::size_t>(image.width) + static_cast<std::size_t>(sourceX)];
                    blue += pixel & 0xFFU;
                    green += (pixel >> 8U) & 0xFFU;
                    red += (pixel >> 16U) & 0xFFU;
                    alpha += (pixel >> 24U) & 0xFFU;
                }
            }
            if (samples == 0U) {
                continue;
            }
            reduced[static_cast<std::size_t>(y) * static_cast<std::size_t>(target) + static_cast<std::size_t>(x)] =
                ((alpha / samples) << 24U) | ((red / samples) << 16U) | ((green / samples) << 8U) | (blue / samples);
        }
    }
    image.width = target;
    image.height = target;
    image.bgra = std::move(reduced);
}

void Downsample(EditorMaterialThumbnailImage& image, int target) {
    if (image.width <= target || image.height <= target || target <= 0) {
        return;
    }
    const int factorX = image.width / target;
    const int factorY = image.height / target;
    if (factorX <= 1 || factorY <= 1) {
        return;
    }

    std::vector<std::uint32_t> reduced(static_cast<std::size_t>(target) * static_cast<std::size_t>(target), 0U);
    const int samples = factorX * factorY;
    for (int y = 0; y < target; ++y) {
        for (int x = 0; x < target; ++x) {
            std::uint32_t blue = 0U;
            std::uint32_t green = 0U;
            std::uint32_t red = 0U;
            std::uint32_t alpha = 0U;
            for (int sampleY = 0; sampleY < factorY; ++sampleY) {
                const int sourceY = y * factorY + sampleY;
                for (int sampleX = 0; sampleX < factorX; ++sampleX) {
                    const std::uint32_t pixel =
                        image.bgra[static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(image.width) +
                            static_cast<std::size_t>(x * factorX + sampleX)];
                    blue += pixel & 0xFFU;
                    green += (pixel >> 8U) & 0xFFU;
                    red += (pixel >> 16U) & 0xFFU;
                    alpha += (pixel >> 24U) & 0xFFU;
                }
            }
            reduced[static_cast<std::size_t>(y) * static_cast<std::size_t>(target) + static_cast<std::size_t>(x)] =
                ((alpha / static_cast<std::uint32_t>(samples)) << 24U) |
                ((red / static_cast<std::uint32_t>(samples)) << 16U) |
                ((green / static_cast<std::uint32_t>(samples)) << 8U) |
                (blue / static_cast<std::uint32_t>(samples));
        }
    }
    image.width = target;
    image.height = target;
    image.bgra = std::move(reduced);
}

// A ball floating on nothing reads as a sticker; a soft contact shadow underneath gives it weight. Drawn
// under the silhouette that was actually rendered, so it follows the primitive instead of assuming a sphere.
void AddContactShadow(EditorMaterialThumbnailImage& image) {
    if (image.width <= 0 || image.height <= 0) {
        return;
    }
    int minX = image.width;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            if (((image.bgra[static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                    static_cast<std::size_t>(x)] >> 24U) & 0xFFU) < 24U) {
                continue;
            }
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }
    if (maxX < minX || maxY < 0) {
        return;
    }

    const float centerX = static_cast<float>(minX + maxX) * 0.5F;
    const float centerY = static_cast<float>(maxY) - static_cast<float>(image.height) * 0.012F;
    const float radiusX = std::max(4.0F, static_cast<float>(maxX - minX) * 0.46F);
    const float radiusY = std::max(2.0F, static_cast<float>(image.height) * 0.045F);
    constexpr float kShadowStrength = 0.55F;

    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const float dx = (static_cast<float>(x) - centerX) / radiusX;
            const float dy = (static_cast<float>(y) - centerY) / radiusY;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance >= 1.0F) {
                continue;
            }
            const float falloff = (1.0F - distance) * (1.0F - distance);
            const auto shadowAlpha = static_cast<std::uint32_t>(kShadowStrength * falloff * 255.0F + 0.5F);
            if (shadowAlpha == 0U) {
                continue;
            }
            std::uint32_t& pixel = image.bgra[static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                static_cast<std::size_t>(x)];
            // Premultiplied "shadow under ball": the ball keeps its own coverage, the shadow fills the rest.
            const std::uint32_t ballAlpha = (pixel >> 24U) & 0xFFU;
            const std::uint32_t combined = std::min<std::uint32_t>(255U, ballAlpha + (shadowAlpha * (255U - ballAlpha)) / 255U);
            pixel = (combined << 24U) | (pixel & 0x00FFFFFFU);
        }
    }
}

void ApplyDisplayTransformAndFinish(EditorMaterialThumbnailImage& image) {
    // Cached PNGs keep the raw linear capture, so the transform runs on every load - a thumbnail is a
    // property of the material, not of whatever exposure the session happens to have on its slider.
    const float exposure = std::pow(2.0F, EditorMaterialPreviewSceneSettings::Defaults().exposureStops);
    constexpr float kGamma = 2.2F;
    for (std::uint32_t& pixel : image.bgra) {
        pixel = ApplyDisplayTransform(pixel, exposure, 1.0F / kGamma);
    }
    PunchOutBackground(image);
    ResampleNormalised(image, kThumbnailSize);
    AddContactShadow(image);
}

[[nodiscard]] EditorSceneBgfxViewport::PresentSettings ThumbnailPresentSettings(const EditorSceneContext& sceneContext) {
    const EditorMaterialPreviewSceneSettings& previewSettings = sceneContext.MaterialPreviewSceneSettings();
    kb::render::SceneRenderCamera camera{};
    bx::mtxLookAt(
        camera.view.data(),
        bx::Vec3{ 0.0F, 0.0F, -previewSettings.cameraDistance },
        bx::Vec3{ 0.0F, 0.0F, 0.0F },
        bx::Vec3{ 0.0F, 1.0F, 0.0F });
    kb::render::SceneDepthPolicy::MakePerspective(
        camera.projection.data(),
        previewSettings.verticalFovDegrees,
        1.0F,
        0.05F,
        50.0F,
        kb::render::SceneDepthPolicy::HomogeneousDepth());

    return EditorSceneBgfxViewport::PresentSettings{
        .renderWidth = kThumbnailRenderSize,
        .renderHeight = kThumbnailRenderSize,
        .fitMode = EditorViewportFitMode::Fit,
        .cameraOverride = camera,
        .viewportKey = kMaterialThumbnailCaptureViewportKey,
        .editorSceneOverlaysEnabled = false,
        .meshPassMode = kb::render::SceneRenderMeshPassMode::OpaqueAndTransparent,
        .lightingConfig = MaterialPreviewRenderPolicy::NeutralPbrLightingConfig(
            previewSettings,
            sceneContext.Project().sceneLightingPath),
        .postProcessSettings = MaterialPreviewRenderPolicy::StableExposurePostProcessSettings(previewSettings),
        .shadowPassEnabled = false,
        .postProcessEnabled = previewSettings.postProcessEnabled && !previewSettings.normalDebugView,
        .selectionMaskEnabled = false,
        .selectionOutlineEnabled = false,
        .gpuDrivenRuntimeDispatchEnabled = false,
        .drawSafeArea = false,
        .sceneRevision = sceneContext.MaterialThumbnailSceneRevision(),
        .sceneDirtyBaseRevision = sceneContext.MaterialThumbnailSceneRevision(),
        .sceneFullSyncRequired = true,
    };
}

// Scales the master thumbnail to exactly the size the tile draws, with an area (box) filter, then a light
// unsharp pass. At tile sizes a stretched blit turns a grass texture into a flat green disc; area-filtering
// keeps the local contrast and the sharpen puts back the bite that any downscale takes away. The sharpen is
// applied only to fully opaque pixels so the anti-aliased silhouette does not grow a halo.
[[nodiscard]] EditorMaterialThumbnailImage ScaleForDisplay(const EditorMaterialThumbnailImage& source, int size) {
    EditorMaterialThumbnailImage scaled{ .width = size, .height = size };
    scaled.bgra.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size), 0U);
    if (source.width <= 0 || source.height <= 0 || size <= 0) {
        return scaled;
    }

    for (int y = 0; y < size; ++y) {
        const int sourceTop = y * source.height / size;
        const int sourceBottom = std::max(sourceTop + 1, (y + 1) * source.height / size);
        for (int x = 0; x < size; ++x) {
            const int sourceLeft = x * source.width / size;
            const int sourceRight = std::max(sourceLeft + 1, (x + 1) * source.width / size);
            std::uint32_t blue = 0U;
            std::uint32_t green = 0U;
            std::uint32_t red = 0U;
            std::uint32_t alpha = 0U;
            std::uint32_t samples = 0U;
            for (int sourceY = sourceTop; sourceY < sourceBottom; ++sourceY) {
                for (int sourceX = sourceLeft; sourceX < sourceRight; ++sourceX) {
                    const std::uint32_t pixel = source.bgra[static_cast<std::size_t>(sourceY) *
                        static_cast<std::size_t>(source.width) + static_cast<std::size_t>(sourceX)];
                    blue += pixel & 0xFFU;
                    green += (pixel >> 8U) & 0xFFU;
                    red += (pixel >> 16U) & 0xFFU;
                    alpha += (pixel >> 24U) & 0xFFU;
                    ++samples;
                }
            }
            if (samples == 0U) {
                continue;
            }
            scaled.bgra[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)] =
                ((alpha / samples) << 24U) | ((red / samples) << 16U) | ((green / samples) << 8U) | (blue / samples);
        }
    }

    constexpr float kSharpen = 0.55F;
    const std::vector<std::uint32_t> blurred = scaled.bgra;
    const auto at = [&blurred, size](int x, int y) {
        return blurred[static_cast<std::size_t>(std::clamp(y, 0, size - 1)) * static_cast<std::size_t>(size) +
            static_cast<std::size_t>(std::clamp(x, 0, size - 1))];
    };
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            std::uint32_t& pixel = scaled.bgra[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                static_cast<std::size_t>(x)];
            if (((pixel >> 24U) & 0xFFU) != 255U) {
                continue;
            }
            for (int shift = 0; shift < 24; shift += 8) {
                float average = 0.0F;
                for (int offsetY = -1; offsetY <= 1; ++offsetY) {
                    for (int offsetX = -1; offsetX <= 1; ++offsetX) {
                        average += static_cast<float>((at(x + offsetX, y + offsetY) >> shift) & 0xFFU);
                    }
                }
                average /= 9.0F;
                const float value = static_cast<float>((pixel >> shift) & 0xFFU);
                const auto sharpened = static_cast<std::uint32_t>(
                    std::clamp(value + kSharpen * (value - average), 0.0F, 255.0F) + 0.5F);
                pixel = (pixel & ~(0xFFU << shift)) | (sharpened << shift);
            }
        }
    }
    return scaled;
}

} // namespace

void EditorMaterialThumbnailService::ProcessCapture(EditorMaterialThumbnailImage& image) {
    ApplyDisplayTransformAndFinish(image);
}

EditorMaterialThumbnailImage EditorMaterialThumbnailService::ScaleForDisplaySize(
    const EditorMaterialThumbnailImage& source,
    int size) {
    return ScaleForDisplay(source, size);
}

const EditorMaterialThumbnailImage* EditorMaterialThumbnailService::ThumbnailFor(
    const kb::assets::AssetMetadata& metadata,
    int displaySize) {
    if (!metadata.id.IsValid() || !ProjectFilesAssetIconResolver::IsMaterial(metadata)) {
        return nullptr;
    }

    const auto found = std::ranges::find_if(entries_, [&metadata](const auto& entry) {
        return entry.first == metadata.id.value;
    });
    const auto scaledFor = [displaySize](Entry& entry) -> const EditorMaterialThumbnailImage* {
        // Whatever render we have is shown, even while a newer one is being captured - only a material
        // that has never been rendered falls back to the painted stand-in.
        if (entry.image.bgra.empty() || displaySize <= 0) {
            return nullptr;
        }
        const auto cached = std::ranges::find_if(entry.scaled, [displaySize](const auto& candidate) {
            return candidate.first == displaySize;
        });
        if (cached != entry.scaled.end()) {
            return &cached->second;
        }
        entry.scaled.emplace_back(displaySize, ScaleForDisplay(entry.image, displaySize));
        return &entry.scaled.back().second;
    };

    if (found != entries_.end() && found->second.contentHash == metadata.contentHash) {
        return scaledFor(found->second);
    }

    Entry entry{ .contentHash = metadata.contentHash };
    if (found != entries_.end() && found->second.state == EntryState::Ready) {
        // Keep showing the previous render while the new one is being produced: a tile that flips back to
        // the painted stand-in on every re-render reads as a bug, not as a refresh.
        entry.image = found->second.image;
        entry.scaled = found->second.scaled;
    }
    // A material rendered in an earlier session is already on disk: no GPU work, no waiting.
    if (LoadPng(ThumbnailPath(metadata), entry.image)) {
        EditorMaterialThumbnailService::ProcessCapture(entry.image);
        entry.state = EntryState::Ready;
    } else if (std::ranges::find(queue_, metadata.id.value) == queue_.end()) {
        queue_.push_back(metadata.id.value);
        EditorCrashBreadcrumbs::WriteValue("material_thumbnail", "queued", metadata.id.value);
    }

    if (found != entries_.end()) {
        found->second = std::move(entry);
        ++revision_;
        return scaledFor(found->second);
    }
    entries_.emplace_back(metadata.id.value, std::move(entry));
    ++revision_;
    return scaledFor(entries_.back().second);
}

void EditorMaterialThumbnailService::Tick(
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& viewport,
    HWND host,
    const RECT& stagingRect) {
    if (host == nullptr || IsWindow(host) == 0 || stagingRect.right <= stagingRect.left || stagingRect.bottom <= stagingRect.top) {
        return;
    }
    if (capture_.captureId != 0U || capture_.assetId != 0U) {
        PollCapture(sceneContext, viewport, host, stagingRect);
        return;
    }
    BeginNextCapture(sceneContext, viewport, host, stagingRect);
}

void EditorMaterialThumbnailService::BeginNextCapture(
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& viewport,
    HWND host,
    const RECT& stagingRect) {
    while (!queue_.empty()) {
        const std::uint64_t assetId = queue_.front();
        queue_.erase(queue_.begin());
        const auto entry = std::ranges::find_if(entries_, [assetId](const auto& candidate) {
            return candidate.first == assetId;
        });
        if (entry == entries_.end() || entry->second.state != EntryState::Queued) {
            continue;
        }

        std::error_code error;
        std::filesystem::create_directories(
            EditorProjectPaths::ProjectRoot() / "Saved" / "Cache" / "Thumbnails",
            error);

        const kb::scene::Scene& previewScene = sceneContext.MaterialThumbnailScene(kb::assets::AssetId{ assetId });
        viewport.Present(host, stagingRect, previewScene, ThumbnailPresentSettings(sceneContext));
        const std::uint64_t captureId =
            sceneContext.RequestMaterialThumbnailCapture(ThumbnailPath(assetId, entry->second.contentHash));
        if (captureId == 0U) {
            // Another capture still owns the channel; try again on a later frame.
            EditorCrashBreadcrumbs::WriteValue("material_thumbnail", "capture request refused", assetId);
            queue_.push_back(assetId);
            return;
        }
        EditorCrashBreadcrumbs::WriteValue("material_thumbnail", "capture started", assetId);
        entry->second.state = EntryState::Capturing;
        ++entry->second.attempts;
        capture_ = Capture{ .assetId = assetId, .captureId = captureId, .framesWaited = 0, .presented = true };
        return;
    }
}

void EditorMaterialThumbnailService::PollCapture(
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& viewport,
    HWND host,
    const RECT& stagingRect) {
    const auto entry = std::ranges::find_if(entries_, [this](const auto& candidate) {
        return candidate.first == capture_.assetId;
    });
    if (entry == entries_.end()) {
        capture_ = Capture{};
        return;
    }

    // Keep the material on screen until the renderer has taken the request off the scene.
    const kb::scene::Scene& previewScene = sceneContext.MaterialThumbnailScene(kb::assets::AssetId{ capture_.assetId });
    viewport.Present(host, stagingRect, previewScene, ThumbnailPresentSettings(sceneContext));

    ++capture_.framesWaited;
    const kb::scene::SceneScreenCaptureStatus status = sceneContext.MaterialThumbnailCaptureStatus(capture_.captureId);
    if (status == kb::scene::SceneScreenCaptureStatus::Completed) {
        if (LoadPng(ThumbnailPath(capture_.assetId, entry->second.contentHash), entry->second.image)) {
            EditorMaterialThumbnailService::ProcessCapture(entry->second.image);
            entry->second.state = EntryState::Ready;
            EditorCrashBreadcrumbs::WriteValue("material_thumbnail", "ready", capture_.assetId);
        } else {
            entry->second.state = EntryState::Failed;
            EditorCrashBreadcrumbs::WriteValue("material_thumbnail", "png load failed", capture_.assetId);
        }
        ++revision_;
        capture_ = Capture{};
        return;
    }
    if (status == kb::scene::SceneScreenCaptureStatus::Failed || capture_.framesWaited > kCaptureFrameBudget) {
        const bool retryable = entry->second.attempts < kCaptureAttempts;
        EditorCrashBreadcrumbs::WriteValue(
            "material_thumbnail",
            status == kb::scene::SceneScreenCaptureStatus::Failed
                ? (retryable ? "capture failed, retrying" : "capture failed")
                : (retryable ? "capture timed out, retrying" : "capture timed out"),
            capture_.assetId);
        if (retryable) {
            entry->second.state = EntryState::Queued;
            queue_.push_back(capture_.assetId);
        } else {
            // Honest terminal answer: the tile keeps its painted stand-in instead of retrying forever.
            entry->second.state = EntryState::Failed;
        }
        ++revision_;
        capture_ = Capture{};
    }
}

std::uint64_t EditorMaterialThumbnailService::Revision() const noexcept {
    return revision_;
}

bool EditorMaterialThumbnailService::HasPendingWork() const noexcept {
    return !queue_.empty() || capture_.assetId != 0U;
}

void EditorMaterialThumbnailService::Clear() noexcept {
    entries_.clear();
    queue_.clear();
    capture_ = Capture{};
    ++revision_;
}

EditorMaterialThumbnailService& EditorMaterialThumbnailCache() {
    static EditorMaterialThumbnailService cache;
    return cache;
}

} // namespace kb::editor

#endif
