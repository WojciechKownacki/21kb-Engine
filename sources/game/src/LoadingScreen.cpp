#include "LoadingScreen.hpp"

#include "GameProjectRuntime.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePostProcessAccess.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/scene/SceneUIComponentSet.hpp"
#include "engine/ui/DynamicImageAsset.hpp"
#include "engine/ui/UIComponentCatalog.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/resources/PostProcessProfileAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <ostream>
#include <utility>

namespace kb::game {
namespace {

using kb::math::Vec2;
using kb::scene::UIComponentSet;
using kb::scene::UIRectTransform;

// The layout is written for the canvas's reference resolution and scales with the screen from there.
constexpr float kMarkSize = 256.0F;
constexpr float kBarWidth = 640.0F;
constexpr float kBarHeight = 8.0F;
// How far down the screen the bar sits, as a fraction of its height.
constexpr float kBarAnchorY = 0.88F;
constexpr std::uint32_t kMarkPixels = 192U;

[[nodiscard]] UIRectTransform Anchored(Vec2 anchorMin, Vec2 anchorMax, Vec2 offsetMin, Vec2 offsetMax) {
    UIRectTransform rect;
    rect.anchorMin = anchorMin;
    rect.anchorMax = anchorMax;
    rect.offsetMin = offsetMin;
    rect.offsetMax = offsetMax;
    return rect;
}

[[nodiscard]] UIRectTransform WholeParent() {
    return Anchored({ 0.0F, 0.0F }, { 1.0F, 1.0F }, { 0.0F, 0.0F }, { 0.0F, 0.0F });
}

[[nodiscard]] kb::scene::SceneObject AddElement(
    kb::scene::Scene& scene, kb::scene::SceneObject parent, const UIComponentSet& components) {
    kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .parent = parent });
    kb::scene::ApplySceneUIComponents(scene.Components().UI(), object.Entity(), components);
    return object;
}

[[nodiscard]] bool IsTextureAsset(const kb::assets::AssetMetadata& metadata) {
    return metadata.type == "RenderTexture" || metadata.type == "Texture" || metadata.importCategory == "Texture";
}

// The engine's own mark: a rounded ring around a dot, drawn once into pixels with a signed distance per pixel.
[[nodiscard]] std::shared_ptr<kb::scene::DynamicImageAsset> BuildEngineMark() {
    auto image = std::make_shared<kb::scene::DynamicImageAsset>();
    image->width = kMarkPixels;
    image->height = kMarkPixels;
    image->revision = 1U;
    image->rgba.resize(static_cast<std::size_t>(kMarkPixels) * kMarkPixels * 4U);
    const float pixelsPerUnit = static_cast<float>(kMarkPixels) * 0.5F;
    for (std::uint32_t y = 0U; y < kMarkPixels; ++y) {
        for (std::uint32_t x = 0U; x < kMarkPixels; ++x) {
            const float px = (static_cast<float>(x) + 0.5F) / pixelsPerUnit - 1.0F;
            const float py = (static_cast<float>(y) + 0.5F) / pixelsPerUnit - 1.0F;
            const float qx = std::fabs(px) - 0.8F + 0.28F;
            const float qy = std::fabs(py) - 0.8F + 0.28F;
            const float box = std::hypot(std::max(qx, 0.0F), std::max(qy, 0.0F)) +
                std::min(std::max(qx, qy), 0.0F) - 0.28F;
            const float ring = std::fabs(box + 0.07F) - 0.07F;
            const float dot = std::hypot(px, py) - 0.24F;
            const float coverage = std::clamp(0.5F - std::min(ring, dot) * pixelsPerUnit, 0.0F, 1.0F);
            std::uint8_t* pixel = &image->rgba[(static_cast<std::size_t>(y) * kMarkPixels + x) * 4U];
            pixel[0] = 235U;
            pixel[1] = 240U;
            pixel[2] = 247U;
            pixel[3] = static_cast<std::uint8_t>(std::lround(coverage * 255.0F));
        }
    }
    return image;
}

} // namespace

LoadingScreen::LoadingScreen() = default;
LoadingScreen::~LoadingScreen() = default;

bool LoadingScreen::Initialize(const GameProjectRuntime& project, std::ostream& err) {
    if (scene_ != nullptr) {
        err << "loading screen was initialized twice\n";
        return false;
    }
    settings_ = project.display.splash;
    // A scene that holds only these elements: no input, audio or script modules start for it.
    scene_ = std::make_unique<kb::scene::Scene>(kb::scene::SceneMode::PrefabPrivate);
    kb::scene::Scene& scene = *scene_;
    kb::assets::AssetManager& assets = scene.Assets().Manager();
    if (!RegisterGameAssetLoaders(scene, err)) {
        return false;
    }

    std::uint64_t imageId = 0U;
    float imageWidth = 0.0F;
    float imageHeight = 0.0F;
    if (!settings_.image.empty()) {
        if (project.IsPackaged()) {
            if (!assets.MountRuntimePack(project.assetPack)) {
                err << "loading screen could not mount the package: " << assets.LastError() << '\n';
                return false;
            }
        } else if (!scene.Assets().MountProject(project.projectRoot) || scene.Assets().Discover() == 0U) {
            err << "loading screen could not read the project assets\n";
            return false;
        }
        const kb::assets::AssetMetadata* metadata = assets.Registry().FindByPath(settings_.image);
        if (metadata == nullptr || !IsTextureAsset(*metadata)) {
            err << "loading screen image was not found or is not a texture: " << settings_.image << '\n';
            return false;
        }
        imageId = metadata->id.value;
        if (settings_.scale == kb::project::ProjectSplashScale::Center) {
            const kb::assets::AssetHandle<kb::render::RenderTextureAssetData> texture =
                assets.Load<kb::render::RenderTextureAssetData>(metadata->id);
            if (!texture.IsLoaded()) {
                err << "loading screen image could not be read: " << assets.LastError() << '\n';
                return false;
            }
            imageWidth = static_cast<float>(texture->width);
            imageHeight = static_cast<float>(texture->height);
        }
    } else {
        const kb::assets::AssetId markId = kb::assets::MakeAssetId("runtime-ui/loading-screen/mark");
        kb::assets::AssetMetadata info;
        info.id = markId;
        info.type = "DynamicImage";
        info.virtualPath = "/Runtime/UI/LoadingScreenMark";
        if (!assets.RegisterGeneratedAsset(std::move(info)) || !assets.PublishRuntimeAsset(markId, BuildEngineMark())) {
            err << "loading screen mark could not be published: " << assets.LastError() << '\n';
            return false;
        }
        imageId = markId.value;
    }

    // The screen is flat colour and UI: no exposure metering, anti-aliasing history, bloom or tone mapping, so it
    // leaves no trace in the temporal state the renderer carries from one frame to the next, and the scene that
    // follows starts from its own. The profile is published after the package is mounted, which replaces the registry.
    {
        const kb::assets::AssetId lookId = kb::assets::MakeAssetId("runtime-ui/loading-screen/look");
        kb::assets::AssetMetadata lookInfo;
        lookInfo.id = lookId;
        lookInfo.type = kb::render::kPostProcessProfileAssetType;
        lookInfo.virtualPath = "/Runtime/UI/LoadingScreenLook";
        auto look = std::make_shared<kb::render::ScenePostProcessSettings>();
        look->bloomEnabled = false;
        look->temporalAntiAliasingEnabled = false;
        look->temporalJitterEnabled = false;
        look->tonemapEnabled = false;
        look->outputTransform.autoExposure.enabled = false;
        if (!assets.RegisterGeneratedAsset(std::move(lookInfo)) || !assets.PublishRuntimeAsset(lookId, std::move(look))) {
            err << "loading screen look could not be published: " << assets.LastError() << '\n';
            return false;
        }
        kb::scene::ScenePostProcessAccess::SetActiveProfile(scene, lookId.value);
    }

    UIComponentSet canvasSet = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Canvas);
    canvasSet.canvasScaler->scaleMode = kb::scene::UICanvasScaleMode::ScaleWithScreenSize;
    canvasSet.canvasScaler->referenceResolution = { 1920.0F, 1080.0F };
    // The colour covers the whole screen, also behind the notch and system bars.
    canvasSet.canvas->respectSafeArea = false;
    const kb::scene::SceneObject canvas = AddElement(scene, {}, canvasSet);

    UIComponentSet background;
    background.rectTransform = WholeParent();
    background.border.emplace();
    background.border->backgroundColor = { settings_.backgroundColor[0], settings_.backgroundColor[1], settings_.backgroundColor[2], 1.0F };
    static_cast<void>(AddElement(scene, canvas, background));

    UIComponentSet picture;
    picture.image.emplace();
    picture.image->imageAssetId = imageId;
    if (settings_.image.empty()) {
        // The engine's mark sits above the middle at a fixed size, whatever scale mode the project names.
        picture.rectTransform = Anchored({ 0.5F, 0.45F }, { 0.5F, 0.45F },
            { -kMarkSize * 0.5F, -kMarkSize * 0.5F }, { kMarkSize * 0.5F, kMarkSize * 0.5F });
    } else if (settings_.scale == kb::project::ProjectSplashScale::Center) {
        picture.rectTransform = Anchored({ 0.5F, 0.5F }, { 0.5F, 0.5F },
            { -imageWidth * 0.5F, -imageHeight * 0.5F }, { imageWidth * 0.5F, imageHeight * 0.5F });
    } else {
        picture.rectTransform = WholeParent();
        picture.image->scaleMode = settings_.scale == kb::project::ProjectSplashScale::Fill
            ? kb::scene::UIImageScaleMode::Cover : kb::scene::UIImageScaleMode::Contain;
    }
    static_cast<void>(AddElement(scene, canvas, picture));

    if (settings_.showProgress) {
        UIComponentSet track;
        track.rectTransform = Anchored({ 0.5F, kBarAnchorY }, { 0.5F, kBarAnchorY },
            { -kBarWidth * 0.5F, -kBarHeight * 0.5F }, { kBarWidth * 0.5F, kBarHeight * 0.5F });
        track.border.emplace();
        track.border->backgroundColor = { 1.0F, 1.0F, 1.0F, 0.18F };
        track.border->cornerRadius = { kBarHeight * 0.5F, kBarHeight * 0.5F, kBarHeight * 0.5F, kBarHeight * 0.5F };
        track.progressBar.emplace();
        const kb::scene::SceneObject trackObject = AddElement(scene, canvas, track);
        track_ = trackObject.Entity();

        // The first child of a progress bar is the part it stretches from the start to the value.
        UIComponentSet fill;
        fill.rectTransform = Anchored({ 0.0F, 0.0F }, { 0.0F, 0.0F }, { 0.0F, 0.0F }, { kBarWidth, kBarHeight });
        fill.border.emplace();
        fill.border->backgroundColor = { 1.0F, 1.0F, 1.0F, 0.9F };
        fill.border->cornerRadius = track.border->cornerRadius;
        fill_ = AddElement(scene, trackObject, fill).Entity();
    }
    return true;
}

bool LoadingScreen::Draw(kb::render::Renderer& renderer, float progress, bool held, const kb::scene::Scene* prewarmed) {
    if (scene_ == nullptr) {
        return false;
    }
    if (track_.IsValid()) {
        kb::scene::UIProgressBar bar{};
        bar.value = progress;
        scene_->Components().UI().Set(track_, bar);
    }
    static_cast<void>(scene_->UI().SetViewport(
        static_cast<float>(renderer.BackbufferWidth()),
        static_cast<float>(renderer.BackbufferHeight())));
    if (!renderer.BeginFrame()) {
        return false;
    }
    const bool submitted = prewarmed != nullptr
        ? renderer.SubmitRuntimeSceneWithPrewarm(*scene_, *prewarmed)
        : sync_.Submit(*scene_, renderer);
    renderer.EndFrame();
    if (submitted) {
        drawn_.push_back(DrawnFrame{ .progress = progress, .held = held, .prewarm = prewarmed != nullptr });
    }
    return submitted;
}

void LoadingScreen::Report(kb::render::Renderer& renderer, float fraction, const kb::scene::Scene* loading) {
    if (scene_ == nullptr) {
        return;
    }
    fraction = std::clamp(fraction, 0.0F, 1.0F);
    if (!showing_ || finished_) {
        // The first report of a load: the screen comes up, its clock starts and its bar starts from empty.
        showing_ = true;
        finished_ = false;
        progress_ = 0.0F;
        shownAt_ = std::chrono::steady_clock::now();
    }
    progress_ = std::max(progress_, fraction);
    finished_ = fraction >= 1.0F;
    if (!finished_ || loading == nullptr) {
        static_cast<void>(Draw(renderer, progress_, false, nullptr));
        return;
    }
    // The load is done: from now until the screen goes away its scene is drawn behind it, so the programs, pipeline
    // states and GPU resources its first frame needs are made while the screen is still up and stay in use. The
    // second frame waits for the renderer to finish the first, which keeps that wait on the loading screen too.
    prewarmed_ = loading;
    static_cast<void>(Draw(renderer, progress_, false, prewarmed_));
    static_cast<void>(Draw(renderer, progress_, false, prewarmed_));
}

bool LoadingScreen::PresentHeld(kb::render::Renderer& renderer) {
    if (!showing_ || !finished_) {
        return false;
    }
    const auto held = std::chrono::steady_clock::now() - shownAt_;
    if (held >= std::chrono::milliseconds(settings_.minimumDisplayMilliseconds)) {
        showing_ = false;
        prewarmed_ = nullptr;
        return false;
    }
    static_cast<void>(Draw(renderer, 1.0F, true, prewarmed_));
    return true;
}

void LoadingScreen::Release(kb::render::Renderer& renderer) {
    if (scene_ != nullptr && renderer.IsInitialized()) {
        renderer.ReleaseScene(*scene_);
    }
    sync_.Reset();
}

} // namespace kb::game
