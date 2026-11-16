#include "scene/AuxFrameRenderer.hpp"

#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderer.hpp"
#include "engine/scene/AuxFrameComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAuxFrameComponents.hpp"
#include "engine/scene/ScenePlanarReflections.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "renderer/RendererRenderOrigin.hpp"
#include "scene/RenderSceneProxyConverters.hpp"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
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
    [[nodiscard]] bool Ensure(RenderResourceRegistry& resources, std::uint16_t width, std::uint16_t height,
        bgfx::TextureFormat::Enum format = bgfx::TextureFormat::RGBA8) {
        width = ClampExtent(width);
        height = ClampExtent(height);
        if (color_.IsValid() && width_ == width && height_ == height && format_ == format && bgfx::isValid(frameBuffer_)) {
            return true;
        }
        Shutdown(resources);
        color_ = resources.RegisterTexture(RenderTextureDesc{
            .width = width,
            .height = height,
            .format = format,
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
        attachments[1].init(depth_, bgfx::Access::Write, 0U, 1U, 0U, BGFX_ATTACHMENT_NONE);
        frameBuffer_ = bgfx::createFrameBuffer(2U, attachments, false);
        if (!bgfx::isValid(frameBuffer_)) {
            Shutdown(resources);
            return false;
        }
        width_ = width;
        height_ = height;
        format_ = format;
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
        format_ = bgfx::TextureFormat::RGBA8;
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
            .colorFormat = format_,
            .depthFormat = bgfx::TextureFormat::D24S8,
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
    bgfx::TextureFormat::Enum format_ = bgfx::TextureFormat::RGBA8;
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
            attachments[1].init(depths_[face], bgfx::Access::Write, 0U, 1U, 0U, BGFX_ATTACHMENT_NONE);
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
            .depthFormat = bgfx::TextureFormat::D24S8,
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


using Matrix = std::array<float, 16>;

[[nodiscard]] Matrix Multiply(const Matrix& first, const Matrix& second) noexcept {
    Matrix result{};
    bx::mtxMul(result.data(), first.data(), second.data());
    return result;
}

[[nodiscard]] Matrix Inverse(const Matrix& matrix) noexcept {
    Matrix result{};
    bx::mtxInverse(result.data(), matrix.data());
    return result;
}

[[nodiscard]] Matrix Translation(float x, float y, float z) noexcept {
    Matrix result{};
    bx::mtxTranslate(result.data(), x, y, z);
    return result;
}

// The mirror picture is HDR where the backend can render to and filter a half-float texture, and 8-bit otherwise.
[[nodiscard]] bgfx::TextureFormat::Enum PlanarReflectionFormat() noexcept {
    const bgfx::Caps* caps = bgfx::getCaps();
    constexpr std::uint32_t kNeeded = BGFX_CAPS_FORMAT_TEXTURE_2D | BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER;
    return caps != nullptr && (caps->formats[bgfx::TextureFormat::RGBA16F] & kNeeded) == kNeeded
        ? bgfx::TextureFormat::RGBA16F : bgfx::TextureFormat::RGBA8;
}

// Replaces the near plane of a reverse-depth perspective projection with `plane` (view space, a point is kept where the
// plane's dot product with it is not negative), keeping the far plane where it was.
[[nodiscard]] bool ApplyObliqueNearPlane(Matrix& projection, const std::array<float, 4>& plane, bool homogeneousDepth) noexcept {
    const Matrix inverse = Inverse(projection);
    // The corner of the far plane that lies farthest along the plane's normal.
    const std::array<float, 4> clipCorner{plane[0] >= 0.0F ? 1.0F : -1.0F, plane[1] >= 0.0F ? 1.0F : -1.0F, homogeneousDepth ? -1.0F : 0.0F, 1.0F};
    std::array<float, 4> corner{};
    for (std::size_t column = 0U; column < 4U; ++column) {
        for (std::size_t row = 0U; row < 4U; ++row) corner[column] += clipCorner[row] * inverse[row * 4U + column];
    }
    float oldNear = 0.0F;
    float newNear = 0.0F;
    for (std::size_t index = 0U; index < 4U; ++index) {
        oldNear += (projection[index * 4U + 3U] - projection[index * 4U + 2U]) * corner[index];
        newNear += plane[index] * corner[index];
    }
    if (!std::isfinite(oldNear) || !std::isfinite(newNear) || std::abs(newNear) < 1.0e-6F) return false;
    const float scale = oldNear / newNear;
    if (!std::isfinite(scale) || scale <= 0.0F) return false;
    for (std::size_t index = 0U; index < 4U; ++index) projection[index * 4U + 2U] = projection[index * 4U + 3U] - scale * plane[index];
    return true;
}

[[nodiscard]] bx::Vec3 PlaneVector(const std::array<float, 3>& value) noexcept { return bx::Vec3{value[0], value[1], value[2]}; }

struct PlanarCandidate {
    const kb::scene::ScenePlanarReflectionPlane* plane = nullptr;
    float score = 0.0F;
};

// The mirror's rectangle (centred on `center`) is in view when its corners are not all behind the camera or all beyond one
// side of the frustum.
[[nodiscard]] bool PlaneInView(const kb::scene::ScenePlanarReflectionPlane& plane, const bx::Vec3& center, const Matrix& viewProjection) noexcept {
    const bx::Vec3 right = bx::mul(PlaneVector(plane.right), plane.halfWidth);
    const bx::Vec3 forward = bx::mul(PlaneVector(plane.forward), plane.halfDepth);
    unsigned outside[5]{};
    for (unsigned corner = 0U; corner < 4U; ++corner) {
        const bx::Vec3 point = bx::add(center, bx::add(bx::mul(right, (corner & 1U) != 0U ? 1.0F : -1.0F), bx::mul(forward, (corner & 2U) != 0U ? 1.0F : -1.0F)));
        std::array<float, 4> clip{};
        for (std::size_t column = 0U; column < 4U; ++column) {
            clip[column] = point.x * viewProjection[column] + point.y * viewProjection[4U + column] + point.z * viewProjection[8U + column] + viewProjection[12U + column];
        }
        outside[0] += clip[3] <= 0.0F ? 1U : 0U;
        outside[1] += clip[0] > clip[3] ? 1U : 0U;
        outside[2] += clip[0] < -clip[3] ? 1U : 0U;
        outside[3] += clip[1] > clip[3] ? 1U : 0U;
        outside[4] += clip[1] < -clip[3] ? 1U : 0U;
    }
    return std::none_of(std::begin(outside), std::end(outside), [](unsigned count) { return count == 4U; });
}

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

    // The mirror picture of one view of a scene (keyed by the view's viewport id).
    struct PlanarOutput {
        std::uint32_t viewportId = 0U;
        Target2D target{};
        std::uint64_t usedFrame = 0U;
        // Set for the frame whose mirror pass was collected; the picture is only offered to the lighting then.
        bool ready = false;
        kb::scene::ScenePlanarReflectionPlane plane{};
        // The world position the view-projection's points are relative to (the eye of the view), and the
        // view-projection of the mirrored camera over such points.
        kb::math::DVec3 base{};
        Matrix viewProjection{};
    };

    std::unordered_map<AuxFrameKey, Output, AuxFrameKeyHash> outputs{};
    std::unordered_map<AuxFrameKey, PlanarOutput, AuxFrameKeyHash> planar{};
    std::vector<PendingPanorama> pendingPanoramas{};
    std::uint32_t planarPasses = 0U;
    std::uint32_t nextViewportId = 0x80000000U;
};

AuxFrameRenderer::AuxFrameRenderer() : state_(std::make_unique<State>()) {}

AuxFrameRenderer::~AuxFrameRenderer() = default;

void AuxFrameRenderer::BeginFrame() {
    state_->pendingPanoramas.clear();
    state_->planarPasses = 0U;
    for (auto& [key, output] : state_->planar) {
        static_cast<void>(key);
        output.ready = false;
    }
}

bool AuxFrameRenderer::HasSceneOutputs(std::uint64_t sceneId) const noexcept {
    return std::ranges::any_of(state_->outputs, [sceneId](const auto& entry) {
        return entry.first.sceneId == sceneId;
    }) || std::ranges::any_of(state_->planar, [sceneId](const auto& entry) {
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
        desc.debugFillTriangles = {};
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

bool AuxFrameRenderer::CollectPlanarReflection(
    const kb::scene::Scene& scene,
    const RenderScene& renderScene,
    const RenderSceneSubmitDesc& source,
    std::span<const std::uint32_t> availableViewportIndices,
    std::uint64_t frameIndex,
    SceneRenderer& sceneRenderer,
    std::vector<AuxFramePreparedSubmission>& submissions) {
    RenderResourceRegistry& resources = sceneRenderer.Resources();
    const AuxFrameKey key{.sceneId = scene.Id(), .entityId = source.target.viewport.id.value};
    const auto release = [&]() {
        const auto found = state_->planar.find(key);
        if (found == state_->planar.end()) return;
        found->second.target.Shutdown(resources);
        state_->planar.erase(found);
    };
    // Pictures of views that stopped being submitted.
    for (auto found = state_->planar.begin(); found != state_->planar.end();) {
        if (found->first.sceneId == scene.Id() && found->second.usedFrame + 2U < frameIndex) {
            found->second.target.Shutdown(resources);
            found = state_->planar.erase(found);
        } else {
            ++found;
        }
    }
    if (source.reducedMirrorPass) return false;
    std::vector<kb::scene::ScenePlanarReflectionPlane> planes;
    kb::scene::ScenePlanarReflectionPlanes(scene, planes);
    // The mirror is worked out relative to the eye of the view (`base`, in double precision): the numbers stay small, so
    // it is exact far from the world origin too.
    std::optional<SceneRenderCamera> camera;
    kb::math::DVec3 base{};
    if (!planes.empty()) {
        const std::uint32_t width = source.target.viewport.extent.width;
        const std::uint32_t height = source.target.viewport.extent.height;
        if (source.cameraOverride.has_value()) {
            camera = *source.cameraOverride;
            base = source.cameraOverrideEye.value_or(RendererRenderOrigin::ViewEye(camera->view));
        } else if (std::optional<SceneRenderCamera> primary = renderScene.BuildPrimaryCamera(width, height, source.target.viewport.id.value)) {
            // Scene cameras are in the render space of the scene.
            camera = *primary;
            base = renderScene.RenderOrigin() + RendererRenderOrigin::ViewEye(camera->view);
        }
        // A mirror needs a perspective camera.
        if (camera.has_value() && std::abs(camera->projection[15]) > 1.0e-4F) camera.reset();
    }
    if (!camera.has_value()) {
        release();
        return false;
    }

    const auto relative = [&base](const kb::math::DVec3& point) {
        return bx::Vec3{static_cast<float>(point.x - base.x), static_cast<float>(point.y - base.y), static_cast<float>(point.z - base.z)};
    };
    Matrix cameraView = camera->view;
    cameraView[12] = cameraView[13] = cameraView[14] = 0.0F;
    const Matrix cameraInverse = Inverse(cameraView);
    const Matrix viewProjection = Multiply(cameraView, camera->projection);
    // The nearest and largest mirror the camera sees from its front side comes first.
    PlanarCandidate best{};
    for (const kb::scene::ScenePlanarReflectionPlane& candidate : planes) {
        const bx::Vec3 candidateCenter = relative(candidate.center);
        const bx::Vec3 toEye = bx::mul(candidateCenter, -1.0F);
        if (bx::dot(toEye, PlaneVector(candidate.normal)) <= 0.01F || !PlaneInView(candidate, candidateCenter, viewProjection)) continue;
        const float score = 4.0F * candidate.halfWidth * candidate.halfDepth / std::max(bx::dot(toEye, toEye), 1.0F);
        if (best.plane == nullptr || score > best.score) best = PlanarCandidate{.plane = &candidate, .score = score};
    }
    if (best.plane == nullptr) {
        release();
        return false;
    }
    if (availableViewportIndices.empty() || state_->planarPasses >= kMaxPlanarReflectionPasses) return false;
    const kb::scene::ScenePlanarReflectionPlane& plane = *best.plane;

    // The camera mirrored in the plane: its picture, seen at a surface point of the plane, is what that point reflects.
    const bx::Vec3 normal = PlaneVector(plane.normal);
    const bx::Vec3 center = relative(plane.center);
    const bx::Vec3 forward{cameraInverse[8], cameraInverse[9], cameraInverse[10]};
    const bx::Vec3 up{cameraInverse[4], cameraInverse[5], cameraInverse[6]};
    // The eye is the origin of these coordinates.
    const bx::Vec3 mirroredEye = bx::mul(normal, 2.0F * bx::dot(center, normal));
    SceneRenderCamera mirrored = *camera;
    mirrored.temporalProjectionOffset = {};
    mirrored.cullingGuardBand = {};
    mirrored.cullingMask = camera->cullingMask & plane.reflectedLayers;
    bx::mtxLookAt(mirrored.view.data(), mirroredEye, bx::add(mirroredEye, Reflect(forward, normal)), Reflect(up, normal));
    const float verticalScale = camera->projection[5];
    const float horizontalScale = camera->projection[0];
    if (!std::isfinite(verticalScale) || !std::isfinite(horizontalScale) || verticalScale <= 1.0e-4F || horizontalScale <= 1.0e-4F) return false;
    const bool homogeneousDepth = SceneDepthPolicy::HomogeneousDepth();
    SceneDepthPolicy::MakePerspective(mirrored.projection.data(), 2.0F * bx::toDeg(std::atan(1.0F / verticalScale)), verticalScale / horizontalScale,
        0.05F, plane.maxDistance, homogeneousDepth);
    // Everything on the camera's side of the mirror (a little above it, so the mirror's own surface is not drawn
    // into its picture) is cut away by moving the near plane onto the mirror.
    constexpr float kClipBias = 0.02F;
    const Matrix mirroredInverse = Inverse(mirrored.view);
    const std::array<float, 4> worldPlane{normal.x, normal.y, normal.z, -bx::dot(normal, center) - kClipBias};
    std::array<float, 4> viewPlane{};
    for (std::size_t row = 0U; row < 4U; ++row) {
        for (std::size_t column = 0U; column < 4U; ++column) viewPlane[row] += mirroredInverse[row * 4U + column] * worldPlane[column];
    }
    if (!ApplyObliqueNearPlane(mirrored.projection, viewPlane, homogeneousDepth)) return false;

    const float scale = std::clamp(plane.resolutionScale, 0.1F, 1.0F);
    const auto scaled = [scale](std::uint32_t extent) {
        return static_cast<std::uint16_t>(std::clamp<std::uint32_t>(static_cast<std::uint32_t>(std::lround(static_cast<float>(extent) * scale)), 16U, 4096U));
    };
    State::PlanarOutput& output = state_->planar[key];
    if (output.viewportId == 0U) {
        output.viewportId = state_->nextViewportId++;
        if (state_->nextViewportId == 0U) state_->nextViewportId = 0x80000000U;
    }
    if (!output.target.Ensure(resources, scaled(source.target.viewport.extent.width), scaled(source.target.viewport.extent.height), PlanarReflectionFormat())) {
        release();
        return false;
    }

    // The reduced frame of a mirror: no post-process, selection or editor drawing, no mirrors of its own, and the
    // screen-space effects off; shadows only when the mirror asks for them.
    RenderSceneSubmitDesc desc = source;
    desc.synchronizeScene = false;
    desc.transformAffineSync = false;
    desc.dirtySceneEntityIds = {};
    desc.selectedEntityIds = {};
    desc.editorCameraWireframes = {};
    desc.editorLightWireframes = {};
    desc.physicsDebugLines = {};
    desc.debugFillTriangles = {};
    desc.editorSceneOverlaysEnabled = false;
    desc.selectionMaskEnabled = false;
    desc.selectionOutlineEnabled = false;
    desc.postProcessEnabled = false;
    desc.postProcess.enabled = false;
    desc.finalComposite.enabled = false;
    desc.shadowPassEnabled = plane.reflectShadows;
    desc.gpuDrivenRuntimeDispatchEnabled = true;
    desc.reducedMirrorPass = true;
    desc.target = output.target.Binding(output.viewportId, availableViewportIndices.front(), resources);
    output.viewProjection = Multiply(mirrored.view, mirrored.projection);
    // Submitted like any camera override: a world-space view, rebuilt from the precise world position of its eye.
    const kb::math::DVec3 mirroredEyeWorld{base.x + mirroredEye.x, base.y + mirroredEye.y, base.z + mirroredEye.z};
    mirrored.view = RendererRenderOrigin::RelativeView(mirrored.view, kb::math::DVec3{}, mirroredEyeWorld);
    desc.cameraOverride = mirrored;
    desc.cameraOverrideEye = mirroredEyeWorld;
    submissions.push_back(AuxFramePreparedSubmission{.scene = &scene, .desc = std::move(desc)});

    output.ready = true;
    output.usedFrame = frameIndex;
    output.plane = plane;
    output.base = base;
    ++state_->planarPasses;
    return true;
}

SceneRenderPlanarReflection AuxFrameRenderer::PlanarReflectionFor(
    std::uint64_t sceneId, std::uint32_t viewportId, const kb::math::DVec3& renderOrigin, const RenderResourceRegistry& resources) const {
    const auto found = state_->planar.find(AuxFrameKey{.sceneId = sceneId, .entityId = viewportId});
    if (found == state_->planar.end() || !found->second.ready) return {};
    const State::PlanarOutput& output = found->second;
    SceneRenderPlanarReflection mirror;
    mirror.texture = output.target.ColorTexture(resources);
    mirror.extent = {output.target.Width(), output.target.Height()};
    // The lighting works in the render space of the scene: its points are the world points minus the origin, where the
    // view-projection takes them minus the eye of the view.
    const kb::math::DVec3 shift = renderOrigin - output.base;
    mirror.viewProjection = Multiply(Translation(static_cast<float>(shift.x), static_cast<float>(shift.y), static_cast<float>(shift.z)), output.viewProjection);
    mirror.center = {static_cast<float>(output.plane.center.x - renderOrigin.x), static_cast<float>(output.plane.center.y - renderOrigin.y),
        static_cast<float>(output.plane.center.z - renderOrigin.z)};
    mirror.normal = output.plane.normal;
    mirror.right = output.plane.right;
    mirror.forward = output.plane.forward;
    mirror.halfWidth = output.plane.halfWidth;
    mirror.halfDepth = output.plane.halfDepth;
    return mirror;
}


bool AuxFrameRenderer::SubmitPanoramaConversion(AuxFramePanoramaConversion conversion) const {
    const auto found = std::find_if(state_->pendingPanoramas.begin(), state_->pendingPanoramas.end(), [conversion](const State::PendingPanorama& pending) { return pending.viewId == conversion.viewId; });
    if (found == state_->pendingPanoramas.end() || found->target == nullptr || !bgfx::isValid(found->cubeTexture) || !bgfx::isValid(found->outputTexture)) {
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
            bgfx::TextureRegion{
                .handle = found->outputTexture,
                .x = static_cast<std::uint16_t>(kAtlasTiles[face][0] * found->faceEdge),
                .y = static_cast<std::uint16_t>(kAtlasTiles[face][1] * found->faceEdge),
                .width = found->faceEdge,
                .height = found->faceEdge,
            },
            bgfx::TextureRegion{
                .handle = found->cubeTexture,
                .z = face,
                .width = found->faceEdge,
                .height = found->faceEdge,
                .depth = 1U,
            });
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
    for (auto mirror = state_->planar.begin(); mirror != state_->planar.end();) {
        if (mirror->first.sceneId != sceneId) {
            ++mirror;
            continue;
        }
        mirror->second.target.Shutdown(resources);
        mirror = state_->planar.erase(mirror);
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
    for (auto& [key, mirror] : state_->planar) {
        static_cast<void>(key);
        mirror.target.Shutdown(resources);
    }
    state_->planar.clear();
    state_->pendingPanoramas.clear();
}

} // namespace kb::render
