#include "scene/AuxFrameRenderer.hpp"

#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderer.hpp"
#include "engine/scene/AuxFrameComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAuxFrameComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "scene/RenderSceneProxyConverters.hpp"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kb::render {
namespace {

constexpr std::uint64_t kAuxFrameTextureFlags = BGFX_TEXTURE_RT | BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;
constexpr std::uint64_t kAuxFrameDepthFlags = BGFX_TEXTURE_RT | BGFX_TEXTURE_BLIT_DST;

struct AuxFrameKey {
    std::uint64_t sceneId = 0U;
    std::uint64_t entityId = 0U;

    [[nodiscard]] friend constexpr bool operator==(AuxFrameKey lhs, AuxFrameKey rhs) noexcept = default;
};

struct AuxFrameKeyHash {
    [[nodiscard]] std::size_t operator()(AuxFrameKey key) const noexcept {
        return static_cast<std::size_t>(key.sceneId ^ (key.entityId + 0x9e3779b97f4a7c15ULL + (key.sceneId << 6U) + (key.sceneId >> 2U)));
    }
};

[[nodiscard]] std::uint16_t ClampExtent(std::uint16_t value) noexcept {
    return std::max<std::uint16_t>(1U, value);
}

[[nodiscard]] bgfx::TextureHandle InvalidTexture() noexcept { return BGFX_INVALID_HANDLE; }

class Target2D final {
public:
    [[nodiscard]] bool Ensure(RenderResourceRegistry& resources, std::uint16_t width, std::uint16_t height) {
        width = ClampExtent(width);
        height = ClampExtent(height);
        if (color_.IsValid() && width_ == width && height_ == height && bgfx::isValid(frameBuffer_)) {
            return true;
        }
        Shutdown(resources);
        color_ = resources.RegisterTexture(RenderTextureDesc{
            .width = width,
            .height = height,
            .format = bgfx::TextureFormat::RGBA8,
            .flags = kAuxFrameTextureFlags,
            .colorSpace = RenderTextureColorSpace::Linear,
        });
        const RenderTextureResource* color = resources.FindTexture(color_);
        if (color == nullptr || !bgfx::isValid(color->texture)) {
            Shutdown(resources);
            return false;
        }
        depth_ = bgfx::createTexture2D(width, height, false, 1U, bgfx::TextureFormat::D24S8, kAuxFrameDepthFlags);
        if (!bgfx::isValid(depth_)) {
            Shutdown(resources);
            return false;
        }
        bgfx::Attachment attachments[2]{};
        attachments[0].init(color->texture);
        attachments[1].init(depth_);
        frameBuffer_ = bgfx::createFrameBuffer(2U, attachments, false);
        if (!bgfx::isValid(frameBuffer_)) {
            Shutdown(resources);
            return false;
        }
        width_ = width;
        height_ = height;
        return true;
    }

    void Shutdown(RenderResourceRegistry& resources) noexcept {
        if (bgfx::isValid(frameBuffer_)) {
            bgfx::destroy(frameBuffer_);
        }
        if (bgfx::isValid(depth_)) {
            bgfx::destroy(depth_);
        }
        if (color_.IsValid()) {
            resources.DestroyTexture(color_);
        }
        color_ = {};
        frameBuffer_ = BGFX_INVALID_HANDLE;
        depth_ = BGFX_INVALID_HANDLE;
        width_ = 0U;
        height_ = 0U;
    }

    [[nodiscard]] RenderSceneTargetBinding Binding(std::uint32_t viewportId, std::uint32_t viewportIndex, const RenderResourceRegistry& resources) const noexcept {
        const RenderTextureResource* color = resources.FindTexture(color_);
        return RenderSceneTargetBinding{
            .frameBuffer = frameBuffer_,
            .colorTexture = color == nullptr ? InvalidTexture() : color->texture,
            .resolvedColorTexture = color == nullptr ? InvalidTexture() : color->texture,
            .depthTexture = depth_,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{.value = viewportId},
                .extent = RenderExtent{.width = width_, .height = height_},
                .viewportIndex = viewportIndex,
            },
            .colorFormat = bgfx::TextureFormat::RGBA8,
        };
    }

    [[nodiscard]] RenderTextureHandle ColorHandle() const noexcept { return color_; }
    [[nodiscard]] bgfx::TextureHandle ColorTexture(const RenderResourceRegistry& resources) const noexcept {
        const RenderTextureResource* color = resources.FindTexture(color_);
        return color == nullptr ? InvalidTexture() : color->texture;
    }
    [[nodiscard]] bgfx::FrameBufferHandle FrameBuffer() const noexcept { return frameBuffer_; }
    [[nodiscard]] std::uint16_t Width() const noexcept { return width_; }
    [[nodiscard]] std::uint16_t Height() const noexcept { return height_; }

private:
    RenderTextureHandle color_{};
    bgfx::FrameBufferHandle frameBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depth_ = BGFX_INVALID_HANDLE;
    std::uint16_t width_ = 0U;
    std::uint16_t height_ = 0U;
};

class TargetCube final {
public:
    TargetCube() {
        frameBuffers_.fill(BGFX_INVALID_HANDLE);
        depths_.fill(BGFX_INVALID_HANDLE);
    }

    [[nodiscard]] bool Ensure(RenderResourceRegistry& resources, std::uint16_t edge) {
        edge = ClampExtent(edge);
        if (color_.IsValid() && edge_ == edge && bgfx::isValid(frameBuffers_[0])) {
            return true;
        }
        Shutdown(resources);
        color_ = resources.RegisterTexture(RenderTextureDesc{
            .width = edge,
            .height = edge,
            .dimension = RenderTextureDimension::TextureCube,
            .format = bgfx::TextureFormat::RGBA8,
            .flags = kAuxFrameTextureFlags,
            .colorSpace = RenderTextureColorSpace::Linear,
        });
        const RenderTextureResource* color = resources.FindTexture(color_);
        if (color == nullptr || !bgfx::isValid(color->texture)) {
            Shutdown(resources);
            return false;
        }
        for (std::uint16_t face = 0U; face < frameBuffers_.size(); ++face) {
            depths_[face] = bgfx::createTexture2D(edge, edge, false, 1U, bgfx::TextureFormat::D24S8, kAuxFrameDepthFlags);
            if (!bgfx::isValid(depths_[face])) {
                Shutdown(resources);
                return false;
            }
            bgfx::Attachment attachments[2]{};
            attachments[0].init(color->texture, bgfx::Access::Write, face);
            attachments[1].init(depths_[face]);
            frameBuffers_[face] = bgfx::createFrameBuffer(2U, attachments, false);
            if (!bgfx::isValid(frameBuffers_[face])) {
                Shutdown(resources);
                return false;
            }
        }
        edge_ = edge;
        return true;
    }

    void Shutdown(RenderResourceRegistry& resources) noexcept {
        for (bgfx::FrameBufferHandle& frameBuffer : frameBuffers_) {
            if (bgfx::isValid(frameBuffer)) {
                bgfx::destroy(frameBuffer);
            }
            frameBuffer = BGFX_INVALID_HANDLE;
        }
        for (bgfx::TextureHandle& depth : depths_) {
            if (bgfx::isValid(depth)) {
                bgfx::destroy(depth);
            }
            depth = BGFX_INVALID_HANDLE;
        }
        if (color_.IsValid()) {
            resources.DestroyTexture(color_);
        }
        color_ = {};
        edge_ = 0U;
    }

    [[nodiscard]] RenderSceneTargetBinding Binding(std::uint16_t face, std::uint32_t viewportId, std::uint32_t viewportIndex, const RenderResourceRegistry& resources) const noexcept {
        const RenderTextureResource* color = resources.FindTexture(color_);
        return RenderSceneTargetBinding{
            .frameBuffer = frameBuffers_[face],
            .colorTexture = color == nullptr ? InvalidTexture() : color->texture,
            .resolvedColorTexture = color == nullptr ? InvalidTexture() : color->texture,
            .depthTexture = depths_[face],
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{.value = viewportId},
                .extent = RenderExtent{.width = edge_, .height = edge_},
                .viewportIndex = viewportIndex,
            },
            .colorFormat = bgfx::TextureFormat::RGBA8,
        };
    }

    [[nodiscard]] RenderTextureHandle ColorHandle() const noexcept { return color_; }
    [[nodiscard]] bgfx::TextureHandle ColorTexture(const RenderResourceRegistry& resources) const noexcept {
        const RenderTextureResource* color = resources.FindTexture(color_);
        return color == nullptr ? InvalidTexture() : color->texture;
    }

private:
    RenderTextureHandle color_{};
    std::array<bgfx::FrameBufferHandle, 6U> frameBuffers_{};
    std::array<bgfx::TextureHandle, 6U> depths_{};
    std::uint16_t edge_ = 0U;
};

struct Basis {
    bx::Vec3 right{0.0F, 0.0F, 0.0F};
    bx::Vec3 up{0.0F, 0.0F, 0.0F};
    bx::Vec3 forward{0.0F, 0.0F, 0.0F};
};

[[nodiscard]] Basis BasisFromQuaternion(const std::array<float, 4>& q) noexcept {
    const float x = q[0]; const float y = q[1]; const float z = q[2]; const float w = q[3];
    const float x2 = x + x; const float y2 = y + y; const float z2 = z + z;
    const float xx = x * x2; const float xy = x * y2; const float xz = x * z2;
    const float yy = y * y2; const float yz = y * z2; const float zz = z * z2;
    const float wx = w * x2; const float wy = w * y2; const float wz = w * z2;
    return Basis{
        .right = bx::Vec3{1.0F - (yy + zz), xy + wz, xz - wy},
        .up = bx::Vec3{xy - wz, 1.0F - (xx + zz), yz + wx},
        .forward = bx::Vec3{xz + wy, yz - wx, 1.0F - (xx + yy)},
    };
}

[[nodiscard]] bx::Vec3 Reflect(bx::Vec3 value, bx::Vec3 normal) noexcept {
    return bx::sub(value, bx::mul(normal, 2.0F * bx::dot(value, normal)));
}

[[nodiscard]] SceneRenderCamera BuildPerspectiveCamera(
    bx::Vec3 eye, bx::Vec3 forward, bx::Vec3 up, float fovDegrees, float aspect, const CameraRenderProxyDesc& proxy) {
    SceneRenderCamera camera{
        .cullingMask = proxy.cullingMask,
        .clearMode = proxy.clearMode == RenderCameraClearMode::DepthOnly ? SceneRenderCameraClearMode::DepthOnly :
            proxy.clearMode == RenderCameraClearMode::DontClear ? SceneRenderCameraClearMode::DontClear : SceneRenderCameraClearMode::SolidColor,
        .clearColor = proxy.clearColor,
    };
    bx::mtxLookAt(camera.view.data(), eye, bx::add(eye, forward), up);
    SceneDepthPolicy::MakePerspective(camera.projection.data(), fovDegrees, aspect, proxy.nearClip, proxy.farClip, SceneDepthPolicy::HomogeneousDepth());
    return camera;
}

[[nodiscard]] SceneRenderCamera BuildMirrorCamera(const CameraRenderProxyDesc& proxy, const kb::scene::AuxFrameComponent& component) {
    const Basis basis = BasisFromQuaternion(proxy.rotation);
    const bx::Vec3 normal = bx::normalize(bx::Vec3{component.mirrorPlaneNormal.x, component.mirrorPlaneNormal.y, component.mirrorPlaneNormal.z});
    const bx::Vec3 eye{proxy.position[0], proxy.position[1], proxy.position[2]};
    const bx::Vec3 reflectedEye = bx::sub(eye, bx::mul(normal, 2.0F * (bx::dot(normal, eye) + component.mirrorPlaneOffset)));
    return BuildPerspectiveCamera(reflectedEye, Reflect(basis.forward, normal), Reflect(basis.up, normal), proxy.verticalFovDegrees, 1.0F, proxy);
}

[[nodiscard]] std::array<SceneRenderCamera, 6U> BuildCubeCameras(const CameraRenderProxyDesc& proxy) {
    const Basis basis = BasisFromQuaternion(proxy.rotation);
    const bx::Vec3 eye{proxy.position[0], proxy.position[1], proxy.position[2]};
    return {
        BuildPerspectiveCamera(eye, basis.right, basis.up, 90.0F, 1.0F, proxy),
        BuildPerspectiveCamera(eye, bx::mul(basis.right, -1.0F), basis.up, 90.0F, 1.0F, proxy),
        BuildPerspectiveCamera(eye, basis.up, bx::mul(basis.forward, -1.0F), 90.0F, 1.0F, proxy),
        BuildPerspectiveCamera(eye, bx::mul(basis.up, -1.0F), basis.forward, 90.0F, 1.0F, proxy),
        BuildPerspectiveCamera(eye, basis.forward, basis.up, 90.0F, 1.0F, proxy),
        BuildPerspectiveCamera(eye, bx::mul(basis.forward, -1.0F), basis.up, 90.0F, 1.0F, proxy),
    };
}

} // namespace

struct AuxFrameRenderer::State {
    struct Output {
        std::uint32_t viewportId = 0U;
        std::uint64_t imageTargetId = 0U;
        Target2D flat{};
        Target2D panorama{};
        TargetCube cube{};
    };
    struct PendingPanorama {
        bgfx::ViewId viewId = 0U;
        Target2D* target = nullptr;
        bgfx::TextureHandle cubeTexture = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle outputTexture = BGFX_INVALID_HANDLE;
        std::uint16_t faceEdge = 0U;
    };

    std::unordered_map<AuxFrameKey, Output, AuxFrameKeyHash> outputs{};
    std::vector<PendingPanorama> pendingPanoramas{};
    std::uint32_t nextViewportId = 0x80000000U;
};

AuxFrameRenderer::AuxFrameRenderer() : state_(std::make_unique<State>()) {}

AuxFrameRenderer::~AuxFrameRenderer() = default;

void AuxFrameRenderer::BeginFrame() {
    state_->pendingPanoramas.clear();
}

bool AuxFrameRenderer::HasSceneOutputs(std::uint64_t sceneId) const noexcept {
    return std::ranges::any_of(state_->outputs, [sceneId](const auto& entry) {
        return entry.first.sceneId == sceneId;
    });
}

void AuxFrameRenderer::Collect(
    const kb::scene::Scene& scene,
    const RenderScene& renderScene,
    const RenderSceneSubmitDesc& sourceDesc,
    std::span<const std::uint32_t> availableViewportIndices,
    std::uint64_t frameIndex,
    SceneRenderer& sceneRenderer,
    std::vector<AuxFramePreparedSubmission>& submissions,
    std::vector<AuxFramePanoramaConversion>& panoramaConversions) {
    struct Candidate { kb::scene::SceneEntity entity{}; kb::scene::AuxFrameComponent component{}; };
    std::vector<Candidate> candidates;
    scene.Components().AuxFrames().ForEach([](kb::scene::SceneEntity entity, const kb::scene::AuxFrameComponent& component, void* raw) {
        auto& target = *static_cast<std::vector<Candidate>*>(raw);
        if (component.enabled && kb::scene::IsAuxFrameComponentValid(component)) {
            target.push_back(Candidate{.entity = entity, .component = component});
        }
    }, &candidates);
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) { return lhs.entity.Id() < rhs.entity.Id(); });
    RenderResourceRegistry& resources = sceneRenderer.Resources();
    SceneRenderResourceMap& resourceMap = sceneRenderer.ResourceMap();
    for (auto output = state_->outputs.begin(); output != state_->outputs.end();) {
        if (output->first.sceneId != scene.Id()) {
            ++output;
            continue;
        }
        const auto current = std::lower_bound(candidates.begin(), candidates.end(), output->first.entityId, [](const Candidate& candidate, std::uint64_t entityId) {
            return candidate.entity.Id() < entityId;
        });
        if (current != candidates.end() && current->entity.Id() == output->first.entityId && current->component.imageTargetId == output->second.imageTargetId) {
            ++output;
            continue;
        }
        if (output->second.imageTargetId != 0U) {
            resourceMap.UnbindDynamicTexture(output->second.imageTargetId, RenderTextureColorSpace::Linear);
        }
        output->second.flat.Shutdown(resources);
        output->second.panorama.Shutdown(resources);
        output->second.cube.Shutdown(resources);
        output = state_->outputs.erase(output);
    }
    if (candidates.empty() || availableViewportIndices.empty()) {
        return;
    }
    const std::size_t rotate = static_cast<std::size_t>(frameIndex % candidates.size());
    std::rotate(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(rotate), candidates.end());
    std::vector<std::uint32_t> slots{availableViewportIndices.begin(), availableViewportIndices.end()};
    for (const Candidate& candidate : candidates) {
        const CameraRenderProxy* cameraProxy = renderScene.FindCameraByEntity(candidate.entity.Id());
        if (cameraProxy == nullptr || !cameraProxy->desc.visible) {
            continue;
        }
        const bool cube = candidate.component.mode == kb::scene::AuxFrameMode::Cube || candidate.component.mode == kb::scene::AuxFrameMode::Panoramic;
        const std::size_t requiredSlots = cube ? 6U : 1U;
        if (slots.size() < requiredSlots) {
            continue;
        }
        const AuxFrameKey key{.sceneId = scene.Id(), .entityId = candidate.entity.Id()};
        State::Output& output = state_->outputs[key];
        if (output.viewportId == 0U) {
            output.viewportId = state_->nextViewportId++;
            if (state_->nextViewportId == 0U) {
                state_->nextViewportId = 0x80000000U;
            }
        }
        output.imageTargetId = candidate.component.imageTargetId;
        const std::uint16_t width = ClampExtent(candidate.component.width);
        const std::uint16_t height = ClampExtent(candidate.component.height);
        const std::uint16_t cubeEdge = candidate.component.mode == kb::scene::AuxFrameMode::Cube
            ? width
            : std::max<std::uint16_t>(1U, std::min<std::uint16_t>(width / 3U, height / 2U));
        if (!cube && !output.flat.Ensure(resources, width, height)) {
            continue;
        }
        if (cube) {
            if (!output.cube.Ensure(resources, cubeEdge) ||
                (candidate.component.mode == kb::scene::AuxFrameMode::Panoramic && !output.panorama.Ensure(resources, width, height))) {
                continue;
            }
        }

        RenderSceneSubmitDesc desc = sourceDesc;
        desc.synchronizeScene = false;
        desc.transformAffineSync = false;
        desc.dirtySceneEntityIds = {};
        desc.selectedEntityIds = {};
        desc.editorCameraWireframes = {};
        desc.editorLightWireframes = {};
        desc.physicsDebugLines = {};
        desc.editorSceneOverlaysEnabled = false;
        desc.selectionMaskEnabled = false;
        desc.selectionOutlineEnabled = false;
        desc.postProcessEnabled = false;
        desc.postProcess.enabled = false;
        desc.finalComposite.enabled = false;
        desc.shadowPassEnabled = false;
        desc.gpuDrivenRuntimeDispatchEnabled = true;

        if (!cube) {
            desc.target = output.flat.Binding(output.viewportId, slots.front(), resources);
            desc.cameraOverride = candidate.component.mode == kb::scene::AuxFrameMode::Mirror
                ? BuildMirrorCamera(cameraProxy->desc, candidate.component)
                : RenderSceneCameraBuilder::Build(cameraProxy->desc, width, height);
            resourceMap.BindDynamicTexture(candidate.component.imageTargetId, RenderTextureColorSpace::Linear, output.flat.ColorHandle());
            submissions.push_back(AuxFramePreparedSubmission{.scene = &scene, .desc = std::move(desc)});
            slots.erase(slots.begin());
            continue;
        }

        const std::array<SceneRenderCamera, 6U> cameras = BuildCubeCameras(cameraProxy->desc);
        for (std::uint16_t face = 0U; face < 6U; ++face) {
            RenderSceneSubmitDesc faceDesc = desc;
            faceDesc.target = output.cube.Binding(face, output.viewportId + face, slots[face], resources);
            faceDesc.cameraOverride = cameras[face];
            submissions.push_back(AuxFramePreparedSubmission{.scene = &scene, .desc = std::move(faceDesc)});
        }
        if (candidate.component.mode == kb::scene::AuxFrameMode::Cube) {
            resourceMap.BindDynamicTexture(candidate.component.imageTargetId, RenderTextureColorSpace::Linear, output.cube.ColorHandle());
        } else {
            resourceMap.BindDynamicTexture(candidate.component.imageTargetId, RenderTextureColorSpace::Linear, output.panorama.ColorHandle());
            const bgfx::ViewId conversionView = RenderViewportViewIdAllocator::ForViewportIndex(slots.back()).finalComposite;
            state_->pendingPanoramas.push_back(State::PendingPanorama{
                .viewId = conversionView,
                .target = &output.panorama,
                .cubeTexture = output.cube.ColorTexture(resources),
                .outputTexture = output.panorama.ColorTexture(resources),
                .faceEdge = cubeEdge,
            });
            panoramaConversions.push_back(AuxFramePanoramaConversion{.viewId = conversionView});
        }
        slots.erase(slots.begin(), slots.begin() + static_cast<std::ptrdiff_t>(requiredSlots));
    }
}

bool AuxFrameRenderer::SubmitPanoramaConversion(AuxFramePanoramaConversion conversion) const {
    const auto found = std::find_if(state_->pendingPanoramas.begin(), state_->pendingPanoramas.end(), [conversion](const State::PendingPanorama& pending) { return pending.viewId == conversion.viewId; });
    if (found == state_->pendingPanoramas.end() || found->target == nullptr || !bgfx::isValid(found->cubeTexture) || !bgfx::isValid(found->outputTexture) ||
        (bgfx::getCaps()->supported & BGFX_CAPS_TEXTURE_BLIT) == 0U) {
        return false;
    }
    bgfx::setViewName(conversion.viewId, "KB Aux Frame Panorama");
    bgfx::setViewFrameBuffer(conversion.viewId, found->target->FrameBuffer());
    bgfx::setViewRect(conversion.viewId, 0U, 0U, found->target->Width(), found->target->Height());
    bgfx::setViewClear(conversion.viewId, BGFX_CLEAR_COLOR, 0x000000FFU);
    bgfx::touch(conversion.viewId);
    constexpr std::array<std::array<std::uint16_t, 2U>, 6U> kAtlasTiles{{
        {0U, 0U}, {1U, 0U}, {2U, 0U}, {0U, 1U}, {1U, 1U}, {2U, 1U},
    }};
    for (std::uint16_t face = 0U; face < kAtlasTiles.size(); ++face) {
        bgfx::blit(
            conversion.viewId,
            found->outputTexture,
            0U,
            static_cast<std::uint16_t>(kAtlasTiles[face][0] * found->faceEdge),
            static_cast<std::uint16_t>(kAtlasTiles[face][1] * found->faceEdge),
            0U,
            found->cubeTexture,
            0U, 0U, 0U, face,
            found->faceEdge, found->faceEdge, 1U);
    }
    return true;
}

void AuxFrameRenderer::ReleaseScene(std::uint64_t sceneId, SceneRenderer* sceneRenderer) noexcept {
    if (sceneRenderer == nullptr) {
        return;
    }
    RenderResourceRegistry& resources = sceneRenderer->Resources();
    SceneRenderResourceMap& map = sceneRenderer->ResourceMap();
    for (auto output = state_->outputs.begin(); output != state_->outputs.end();) {
        if (output->first.sceneId != sceneId) {
            ++output;
            continue;
        }
        if (output->second.imageTargetId != 0U) {
            map.UnbindDynamicTexture(output->second.imageTargetId, RenderTextureColorSpace::Linear);
        }
        output->second.flat.Shutdown(resources);
        output->second.panorama.Shutdown(resources);
        output->second.cube.Shutdown(resources);
        output = state_->outputs.erase(output);
    }
}

void AuxFrameRenderer::Shutdown(SceneRenderer* sceneRenderer) noexcept {
    if (sceneRenderer == nullptr) {
        return;
    }
    RenderResourceRegistry& resources = sceneRenderer->Resources();
    SceneRenderResourceMap& map = sceneRenderer->ResourceMap();
    for (auto& [key, output] : state_->outputs) {
        static_cast<void>(key);
        if (output.imageTargetId != 0U) {
            map.UnbindDynamicTexture(output.imageTargetId, RenderTextureColorSpace::Linear);
        }
        output.flat.Shutdown(resources);
        output.panorama.Shutdown(resources);
        output.cube.Shutdown(resources);
    }
    state_->outputs.clear();
    state_->pendingPanoramas.clear();
}

} // namespace kb::render
