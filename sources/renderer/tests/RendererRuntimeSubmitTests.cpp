#include "RendererTestSupport.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/particles/ParticlePlayback.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/AuxFrameComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneAuxFrameComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneLightingAccess.hpp"
#include "engine/scene/SceneMaterialInstances.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePostProcessAccess.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/scene/SceneUIComponents.hpp"
#include "engine/ui/visual/UIText.hpp"
#include "engine/scene/VisibilityComponent.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/SceneRenderTarget.hpp"
#include "kb/render/overlay/SceneGizmoPass.hpp"
#include "kb/render/post/ScenePostProcessTargets.hpp"
#include "kb/render/scene/SceneRenderer.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialTypeAssetLoader.hpp"
#include "kb/render/resources/PostProcessProfileAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "kb/render/runtime/RuntimeMaterialResolver.hpp"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <limits>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace kb::render::tests {
namespace {

class HeadlessSurface final : public RenderSurface {
public:
    [[nodiscard]] std::uint32_t Width() const noexcept override {
        return 64U;
    }

    [[nodiscard]] std::uint32_t Height() const noexcept override {
        return 64U;
    }

    [[nodiscard]] void* NativeWindowHandle() const noexcept override {
        return nullptr;
    }

    [[nodiscard]] void* NativeDisplayHandle() const noexcept override {
        return nullptr;
    }
};

#if defined(_WIN32)
class NativeTestSurface final : public RenderSurface {
public:
    explicit NativeTestSurface(std::uint16_t width = kExtent, std::uint16_t height = kExtent)
        : width_(width), height_(height) {
        window_ = CreateWindowExW(
            0U,
            L"STATIC",
            L"KB Particle Mesh Readback",
            WS_OVERLAPPEDWINDOW,
            0,
            0,
            static_cast<int>(width_),
            static_cast<int>(height_),
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
    }

    ~NativeTestSurface() override {
        if (window_ != nullptr) {
            DestroyWindow(window_);
        }
    }

    NativeTestSurface(const NativeTestSurface&) = delete;
    NativeTestSurface& operator=(const NativeTestSurface&) = delete;

    [[nodiscard]] bool IsValid() const noexcept {
        return window_ != nullptr;
    }

    [[nodiscard]] std::uint32_t Width() const noexcept override {
        return width_;
    }

    [[nodiscard]] std::uint32_t Height() const noexcept override {
        return height_;
    }

    [[nodiscard]] void* NativeWindowHandle() const noexcept override {
        return window_;
    }

    [[nodiscard]] void* NativeDisplayHandle() const noexcept override {
        return nullptr;
    }

    static constexpr std::uint16_t kExtent = 64U;

private:
    HWND window_ = nullptr;
    std::uint16_t width_ = kExtent;
    std::uint16_t height_ = kExtent;
};

class ParticleMeshReadbackTarget final {
public:
    ParticleMeshReadbackTarget() = default;

    ~ParticleMeshReadbackTarget() {
        Shutdown();
    }

    ParticleMeshReadbackTarget(const ParticleMeshReadbackTarget&) = delete;
    ParticleMeshReadbackTarget& operator=(const ParticleMeshReadbackTarget&) = delete;

    [[nodiscard]] bool Initialize(
        std::uint16_t width = NativeTestSurface::kExtent,
        std::uint16_t height = NativeTestSurface::kExtent) {
        width_ = width;
        height_ = height;
        color_ = bgfx::createTexture2D(
            width_,
            height_,
            false,
            1U,
            bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_RT | BGFX_TEXTURE_BLIT_DST);
        depth_ = bgfx::createTexture2D(
            width_,
            height_,
            false,
            1U,
            bgfx::TextureFormat::D24S8,
            BGFX_TEXTURE_RT);
        readback_ = bgfx::createTexture2D(
            width_,
            height_,
            false,
            1U,
            bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
        if (!bgfx::isValid(color_) || !bgfx::isValid(depth_) || !bgfx::isValid(readback_)) {
            Shutdown();
            return false;
        }
        const std::array attachments{color_, depth_};
        frameBuffer_ = bgfx::createFrameBuffer(static_cast<std::uint8_t>(attachments.size()), attachments.data(), false);
        if (!bgfx::isValid(frameBuffer_)) {
            Shutdown();
            return false;
        }
        return true;
    }

    [[nodiscard]] RenderSceneTargetBinding Binding() const noexcept {
        return RenderSceneTargetBinding{
            .frameBuffer = frameBuffer_,
            .colorTexture = color_,
            .depthTexture = depth_,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{1U},
                .extent = RenderExtent{width_, height_},
                .viewportIndex = 0U,
            },
            .colorFormat = bgfx::TextureFormat::RGBA8,
        };
    }

    [[nodiscard]] std::vector<std::uint8_t> ReadPixels() const {
        bgfx::blit(kReadbackView, readback_, 0U, 0U, color_, 0U, 0U, width_, height_);
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width_) * height_ * 4U);
        const std::uint32_t readyFrame = bgfx::readTexture(readback_, pixels.data());
        std::uint32_t frame = bgfx::frame();
        for (std::uint32_t guard = 0U; frame < readyFrame && guard < 8U; ++guard) {
            frame = bgfx::frame();
        }
        Require(frame >= readyFrame, "Particle mesh readback did not complete within the bounded frame wait");
        return pixels;
    }

    void Shutdown() noexcept {
        if (bgfx::isValid(frameBuffer_)) {
            bgfx::destroy(frameBuffer_);
            frameBuffer_ = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(readback_)) {
            bgfx::destroy(readback_);
            readback_ = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(depth_)) {
            bgfx::destroy(depth_);
            depth_ = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(color_)) {
            bgfx::destroy(color_);
            color_ = BGFX_INVALID_HANDLE;
        }
    }

private:
    static constexpr bgfx::ViewId kReadbackView = 250U;

    bgfx::FrameBufferHandle frameBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle color_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depth_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle readback_ = BGFX_INVALID_HANDLE;
    std::uint16_t width_ = NativeTestSurface::kExtent;
    std::uint16_t height_ = NativeTestSurface::kExtent;
};

class FinalCompositeReadbackTarget final {
public:
    ~FinalCompositeReadbackTarget() {
        Shutdown();
    }

    FinalCompositeReadbackTarget() = default;
    FinalCompositeReadbackTarget(const FinalCompositeReadbackTarget&) = delete;
    FinalCompositeReadbackTarget& operator=(const FinalCompositeReadbackTarget&) = delete;

    [[nodiscard]] bool Initialize() {
        output_ = bgfx::createTexture2D(
            NativeTestSurface::kExtent,
            NativeTestSurface::kExtent,
            false,
            1U,
            bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_RT | BGFX_TEXTURE_BLIT_DST);
        readback_ = bgfx::createTexture2D(
            NativeTestSurface::kExtent,
            NativeTestSurface::kExtent,
            false,
            1U,
            bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
        if (!bgfx::isValid(output_) || !bgfx::isValid(readback_)) {
            Shutdown();
            return false;
        }
        frameBuffer_ = bgfx::createFrameBuffer(1U, &output_, false);
        if (!bgfx::isValid(frameBuffer_)) {
            Shutdown();
            return false;
        }
        return true;
    }

    [[nodiscard]] RenderFinalCompositeTargetBinding Binding() const noexcept {
        return RenderFinalCompositeTargetBinding{
            .frameBuffer = frameBuffer_,
            .extent = RenderExtent{
                NativeTestSurface::kExtent,
                NativeTestSurface::kExtent,
            },
            .outputRect = RenderViewportRect{
                .extent = RenderExtent{
                    NativeTestSurface::kExtent,
                    NativeTestSurface::kExtent,
                },
            },
            .enabled = true,
            .clearTarget = true,
        };
    }

    [[nodiscard]] std::vector<std::uint8_t> ReadPixels() const {
        bgfx::blit(
            kReadbackView,
            readback_,
            0U,
            0U,
            output_,
            0U,
            0U,
            NativeTestSurface::kExtent,
            NativeTestSurface::kExtent);
        std::vector<std::uint8_t> pixels(
            static_cast<std::size_t>(NativeTestSurface::kExtent) *
            NativeTestSurface::kExtent * 4U);
        const std::uint32_t readyFrame =
            bgfx::readTexture(readback_, pixels.data());
        std::uint32_t frame = bgfx::frame();
        for (std::uint32_t guard = 0U;
             frame < readyFrame && guard < 8U;
             ++guard) {
            frame = bgfx::frame();
        }
        Require(
            frame >= readyFrame,
            "Detached viewport final-composite readback timed out");
        return pixels;
    }

    void Shutdown() noexcept {
        if (bgfx::isValid(frameBuffer_)) {
            bgfx::destroy(frameBuffer_);
            frameBuffer_ = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(readback_)) {
            bgfx::destroy(readback_);
            readback_ = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(output_)) {
            bgfx::destroy(output_);
            output_ = BGFX_INVALID_HANDLE;
        }
    }

private:
    static constexpr bgfx::ViewId kReadbackView = 250U;

    bgfx::FrameBufferHandle frameBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle output_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle readback_ = BGFX_INVALID_HANDLE;
};
#endif

class SnapshotBackend final : public kb::particles::IParticleSimulationBackend {
public:
    kb::particles::ParticleRuntimeResult Create(kb::scene::Scene&, std::uint64_t, kb::scene::SceneEntity) override { return Success(); }
    kb::particles::ParticleRuntimeResult Release(kb::scene::Scene&, std::uint64_t) noexcept override { return Success(); }
    kb::particles::ParticleRuntimeResult Play(kb::scene::Scene&, std::uint64_t) noexcept override { return Success(); }
    kb::particles::ParticleRuntimeResult Pause(kb::scene::Scene&, std::uint64_t) noexcept override { return Success(); }
    kb::particles::ParticleRuntimeResult Stop(kb::scene::Scene&, std::uint64_t) noexcept override { return Success(); }
    kb::particles::ParticleRuntimeResult Restart(kb::scene::Scene&, std::uint64_t) noexcept override { return Success(); }
    kb::particles::ParticleRuntimeResult SetSeed(kb::scene::Scene&, std::uint64_t, std::uint64_t) noexcept override { return Success(); }
    kb::particles::ParticleRuntimeResult SetParameterScalar(
        kb::scene::Scene&, std::uint64_t, std::string_view, float) noexcept override { return Success(); }
    kb::particles::ParticleRuntimeResult ClearParameter(
        kb::scene::Scene&, std::uint64_t, std::string_view) noexcept override { return Success(); }
    kb::particles::ParticleRuntimeResult Emit(kb::scene::Scene&, std::uint64_t, std::uint32_t) override { return Success(); }
    kb::particles::ParticleRuntimeQueryResult Query(const kb::scene::Scene&, std::uint64_t) const noexcept override {
        return {.status = kb::particles::ParticleRuntimeStatus::Success};
    }
    std::size_t CopyLiveInstanceIds(const kb::scene::Scene&, std::span<std::uint64_t>) const noexcept override { return 0U; }
    std::size_t CopyLiveParticleStates(
        const kb::scene::Scene&, std::uint64_t, std::span<kb::particles::ParticleRuntimeState>) const noexcept override {
        return 0U;
    }

private:
    [[nodiscard]] static kb::particles::ParticleRuntimeResult Success() noexcept {
        return {.status = kb::particles::ParticleRuntimeStatus::Success};
    }
};

[[nodiscard]] kb::scene::TransformComponent TransformAt(float x, float y, float z) {
    return kb::scene::TransformComponent{
        .localPosition = kb::scene::Vec3{ x, y, z },
        .worldPosition = kb::scene::Vec3{ x, y, z },
        .worldDirty = false,
    };
}

[[nodiscard]] SceneRenderCamera IdentityCamera() noexcept {
    return SceneRenderCamera{
        .view = {
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F,
        },
        .projection = {
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F,
        },
    };
}

void WriteTriangleObj(const std::filesystem::path& path) {
    std::ofstream output{ path, std::ios::trunc };
    output
        << "v -0.1 -0.1 0.0\n"
        << "v 0.1 -0.1 0.0\n"
        << "v 0.0 0.1 0.0\n"
        << "vt 0 0\n"
        << "vt 1 0\n"
        << "vt 0.5 1\n"
        << "vn 0 0 1\n"
        << "f 1/1/1 2/2/1 3/3/1\n";
}

void WriteBoundsTriangleObj(const std::filesystem::path& path) {
    std::ofstream output{path, std::ios::trunc};
    output << "v -0.1 -0.1 -0.1\n"
           << "v 0.1 -0.1 0.1\n"
           << "v 0.0 0.1 0.0\n"
           << "vt 0 0\nvt 1 0\nvt 0.5 1\n"
           << "vn 0 0 1\n"
           << "f 1/1/1 2/2/1 3/3/1\n";
}

void WriteTexture(const std::filesystem::path& path, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    std::ofstream output{ path, std::ios::trunc };
    output
        << "size 1 1\n"
        << "rgba8 "
        << static_cast<std::uint32_t>(r) << " "
        << static_cast<std::uint32_t>(g) << " "
        << static_cast<std::uint32_t>(b) << " 255\n";
}

void WriteEmbeddedMaterialTriangleGltf(const std::filesystem::path& root) {
    const std::filesystem::path binPath = root / "embedded_mesh.bin";
    {
        const std::vector<float> positions{
            -0.1F, -0.1F, 0.0F,
            0.1F, -0.1F, 0.0F,
            0.0F, 0.1F, 0.0F,
        };
        const std::vector<float> normals{
            0.0F, 0.0F, 1.0F,
            0.0F, 0.0F, 1.0F,
            0.0F, 0.0F, 1.0F,
        };
        const std::vector<float> tangents{
            1.0F, 0.0F, 0.0F, 1.0F,
            1.0F, 0.0F, 0.0F, 1.0F,
            1.0F, 0.0F, 0.0F, 1.0F,
        };
        const std::vector<float> texCoords{
            0.0F, 0.0F,
            1.0F, 0.0F,
            0.5F, 1.0F,
        };
        const std::uint16_t indices[]{ 0U, 1U, 2U };

        std::ofstream output{ binPath, std::ios::binary | std::ios::trunc };
        output.write(reinterpret_cast<const char*>(positions.data()), static_cast<std::streamsize>(positions.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(normals.data()), static_cast<std::streamsize>(normals.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(tangents.data()), static_cast<std::streamsize>(tangents.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(texCoords.data()), static_cast<std::streamsize>(texCoords.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(indices), static_cast<std::streamsize>(sizeof(indices)));
        const std::uint16_t padding = 0U;
        output.write(reinterpret_cast<const char*>(&padding), static_cast<std::streamsize>(sizeof(padding)));
    }

    std::ofstream output{ root / "embedded_triangle.gltf", std::ios::trunc };
    output
        << "{\n"
        << "  \"asset\": { \"version\": \"2.0\" },\n"
        << "  \"scene\": 0,\n"
        << "  \"scenes\": [{ \"nodes\": [0] }],\n"
        << "  \"nodes\": [{ \"mesh\": 0 }],\n"
        << "  \"materials\": [{\n"
        << "    \"name\": \"embedded_surface\",\n"
        << "    \"pbrMetallicRoughness\": {\n"
        << "      \"baseColorFactor\": [0.7, 0.8, 0.9, 0.5],\n"
        << "      \"metallicFactor\": 0.25,\n"
        << "      \"roughnessFactor\": 0.45,\n"
        << "      \"baseColorTexture\": { \"index\": 0 },\n"
        << "      \"metallicRoughnessTexture\": { \"index\": 1 }\n"
        << "    },\n"
        << "    \"normalTexture\": { \"index\": 2, \"scale\": 0.8 },\n"
        << "    \"occlusionTexture\": { \"index\": 3, \"strength\": 0.7 },\n"
        << "    \"emissiveFactor\": [0.05, 0.1, 0.2],\n"
        << "    \"emissiveTexture\": { \"index\": 4 },\n"
        << "    \"alphaMode\": \"MASK\"\n"
        << "  }],\n"
        << "  \"textures\": [{ \"source\": 0 }, { \"source\": 1 }, { \"source\": 2 }, { \"source\": 3 }, { \"source\": 4 }],\n"
        << "  \"images\": [{ \"uri\": \"embedded_albedo.kbtex\" }, { \"uri\": \"embedded_mr.kbtex\" }, { \"uri\": \"embedded_normal.kbtex\" }, { \"uri\": \"embedded_ao.kbtex\" }, { \"uri\": \"embedded_emissive.kbtex\" }],\n"
        << "  \"meshes\": [{ \"primitives\": [{ \"attributes\": { \"POSITION\": 0, \"NORMAL\": 1, \"TANGENT\": 2, \"TEXCOORD_0\": 3 }, \"indices\": 4, \"material\": 0 }] }],\n"
        << "  \"buffers\": [{ \"uri\": \"embedded_mesh.bin\", \"byteLength\": 152 }],\n"
        << "  \"bufferViews\": [\n"
        << "    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 36, \"target\": 34962 },\n"
        << "    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 36, \"target\": 34962 },\n"
        << "    { \"buffer\": 0, \"byteOffset\": 72, \"byteLength\": 48, \"target\": 34962 },\n"
        << "    { \"buffer\": 0, \"byteOffset\": 120, \"byteLength\": 24, \"target\": 34962 },\n"
        << "    { \"buffer\": 0, \"byteOffset\": 144, \"byteLength\": 6, \"target\": 34963 }\n"
        << "  ],\n"
        << "  \"accessors\": [\n"
        << "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\", \"min\": [-0.1, -0.1, 0], \"max\": [0.1, 0.1, 0] },\n"
        << "    { \"bufferView\": 1, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        << "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC4\" },\n"
        << "    { \"bufferView\": 3, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC2\" },\n"
        << "    { \"bufferView\": 4, \"componentType\": 5123, \"count\": 3, \"type\": \"SCALAR\" }\n"
        << "  ]\n"
        << "}\n";
}

void WriteMaterial(
    const std::filesystem::path& path,
    std::uint64_t albedoTextureId,
    std::uint64_t normalTextureId,
    std::uint64_t metallicRoughnessTextureId,
    std::uint64_t occlusionTextureId,
    std::uint64_t emissiveTextureId,
    const char* alphaMode = "OPAQUE",
    float alpha = 1.0F) {
    std::ofstream output{ path, std::ios::trunc };
    output
        << "baseColor 0.8 0.7 0.6 " << alpha << "\n"
        << "emissiveColor 0.1 0.2 0.3\n"
        << "metallicFactor 0.2\n"
        << "roughnessFactor 0.55\n"
        << "normalScale 0.9\n"
        << "occlusionStrength 0.85\n"
        << "emissiveStrength 1.5\n"
        << "alphaMode " << alphaMode << "\n"
        << "albedoTextureAssetId " << albedoTextureId << "\n"
        << "normalTextureAssetId " << normalTextureId << "\n"
        << "metallicRoughnessTextureAssetId " << metallicRoughnessTextureId << "\n"
        << "occlusionTextureAssetId " << occlusionTextureId << "\n"
        << "emissiveTextureAssetId " << emissiveTextureId << "\n";
}

void WriteMaterialInstance(const std::filesystem::path& path, kb::assets::AssetId parentMaterialAssetId) {
    RenderMaterialInstanceAssetData instance{};
    instance.parentMaterialAssetId = parentMaterialAssetId;
    Require(RenderMaterialInstanceAssetWriter::Save(path, instance), "Material instance fixture could not be written");
}

void WriteMaterialWithTexturePaths(
    const std::filesystem::path& path,
    const char* alphaMode = "OPAQUE",
    float alpha = 1.0F,
    const char* emissiveTexturePath = "emissive.kbtex") {
    std::ofstream output{ path, std::ios::trunc };
    output
        << "baseColor 0.8 0.7 0.6 " << alpha << "\n"
        << "emissiveColor 0.1 0.2 0.3\n"
        << "metallicFactor 0.2\n"
        << "roughnessFactor 0.55\n"
        << "normalScale 0.9\n"
        << "occlusionStrength 0.85\n"
        << "emissiveStrength 1.5\n"
        << "alphaMode " << alphaMode << "\n"
        << "baseColorTexture albedo.kbtex\n"
        << "normalTexture normal.kbtex\n"
        << "metallicRoughnessTexture metallic_roughness.kbtex\n"
        << "occlusionTexture occlusion.kbtex\n"
        << "emissiveTexture " << emissiveTexturePath << "\n";
}

[[nodiscard]] std::optional<std::filesystem::path> FindWorkspaceProjectAssets() {
    std::filesystem::path cursor = std::filesystem::current_path();
    for (int depth = 0; depth < 8; ++depth) {
        const std::filesystem::path assets = cursor / "Project" / "Assets";
        std::error_code error;
        if (std::filesystem::is_regular_file(assets / "Cube.21kb", error) &&
            std::filesystem::is_regular_file(assets / "Scenes" / "Main.21kbscene", error)) {
            return assets;
        }
        if (!cursor.has_parent_path()) {
            break;
        }
        cursor = cursor.parent_path();
    }
    return std::nullopt;
}

void RunRuntimeMaterialResolverReturnsTypedFallbacksAndDiagnosticsTest() {
    const RuntimeFallbackMaterialProfile defaultProfile = RuntimeMaterialResolver::FallbackMaterialProfile(RuntimeFallbackMaterialKind::Default);
    const RuntimeFallbackMaterialProfile errorProfile = RuntimeMaterialResolver::FallbackMaterialProfile(RuntimeFallbackMaterialKind::Error);
    Require(defaultProfile.kind == RuntimeFallbackMaterialKind::Default &&
            defaultProfile.status == RuntimeMaterialResolveStatus::DefaultMaterial &&
            defaultProfile.stableName == "runtime.default_material",
        "KBMAT-1005: Default material profile should be explicit and stable");
    Require(errorProfile.kind == RuntimeFallbackMaterialKind::Error &&
            errorProfile.status == RuntimeMaterialResolveStatus::ErrorMaterial &&
            errorProfile.stableName == "runtime.error_material",
        "KBMAT-1005: Error material profile should be explicit and stable");
    Require(NearlyEqual(defaultProfile.desc.baseColor[0], 1.0F) &&
            NearlyEqual(defaultProfile.desc.baseColor[1], 1.0F) &&
            NearlyEqual(defaultProfile.desc.baseColor[2], 1.0F) &&
            NearlyEqual(defaultProfile.desc.roughnessFactor, RuntimeMaterialResolver::DefaultMaterialDesc().roughnessFactor),
        "KBMAT-1005: Default material profile should expose the runtime default descriptor");
    Require(NearlyEqual(errorProfile.desc.baseColor[0], 1.0F) &&
            NearlyEqual(errorProfile.desc.baseColor[1], 0.0F) &&
            NearlyEqual(errorProfile.desc.baseColor[2], 1.0F) &&
            NearlyEqual(errorProfile.desc.roughnessFactor, RuntimeMaterialResolver::ErrorMaterialDesc().roughnessFactor),
        "KBMAT-1005: Error material profile should expose the runtime error descriptor");

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_runtime_material_resolver";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Runtime material resolver test could not create temp root");

    const std::filesystem::path brokenMaterialPath = root / "broken.kbmat";
    {
        std::ofstream output{ brokenMaterialPath, std::ios::trunc };
        output << "roughnessFactor broken\n";
    }

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Runtime material resolver test could not register material loader");
    Require(manager.Mounts().Mount("Game", root), "Runtime material resolver test could not mount asset root");
    Require(manager.DiscoverMountedAssets() == 1U, "Runtime material resolver test did not discover the broken material");
    const kb::assets::AssetMetadata* brokenMetadata = manager.Registry().FindByPath("/Game/broken.kbmat");
    Require(brokenMetadata != nullptr && brokenMetadata->type == "RenderMaterial", "Runtime material resolver test discovered wrong material metadata");

    RuntimeMaterialResolver resolver;
    const ResolvedRuntimeMaterialAsset missing = resolver.ResolveAsset(manager, kb::assets::AssetId{ 404404U });
    Require(missing.resolved, "Runtime material resolver should resolve missing materials to a fallback");
    Require(missing.status == RuntimeMaterialResolveStatus::DefaultMaterial, "Missing material asset should use the default material fallback");
    Require(missing.diagnostics.size() == 1U && missing.diagnostics[0].kind == RuntimeMaterialResolveDiagnosticKind::MissingMaterialAsset, "Missing material asset should report a typed diagnostic");
    Require(NearlyEqual(missing.material.desc.baseColor[0], 1.0F) && NearlyEqual(missing.material.desc.baseColor[1], 1.0F), "Default material fallback should be white");
    Require(NearlyEqual(missing.material.desc.baseColor[2], defaultProfile.desc.baseColor[2]), "KBMAT-1005: Missing material fallback should use the explicit default material profile");

    const ResolvedRuntimeMaterialAsset broken = resolver.ResolveAsset(manager, *brokenMetadata);
    Require(broken.resolved, "Runtime material resolver should resolve invalid materials to a fallback");
    Require(broken.status == RuntimeMaterialResolveStatus::ErrorMaterial, "Invalid material asset should use the error material fallback");
    Require(!broken.diagnostics.empty(), "Invalid material asset should expose parser diagnostics");
    Require(broken.diagnostics[0].kind == RuntimeMaterialResolveDiagnosticKind::MaterialLoadFailed, "Invalid material diagnostic should be typed as a load failure");
    Require(broken.diagnostics[0].message.find("invalid_float") != std::string::npos, "Invalid material diagnostic should preserve parser diagnostic code");
    Require(NearlyEqual(broken.material.desc.baseColor[0], 1.0F) && NearlyEqual(broken.material.desc.baseColor[1], 0.0F) && NearlyEqual(broken.material.desc.baseColor[2], 1.0F), "Error material fallback should be magenta");
    Require(NearlyEqual(broken.material.desc.roughnessFactor, errorProfile.desc.roughnessFactor), "KBMAT-1005: Broken material fallback should use the explicit error material profile");

    std::filesystem::remove_all(root, error);
}

[[nodiscard]] bool ContainsAssetDependency(const std::vector<kb::assets::AssetId>& dependencies, kb::assets::AssetId id) noexcept {
    for (const kb::assets::AssetId dependency : dependencies) {
        if (dependency == id) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] RenderMaterialGraphLink MakeGraphLink(
    RenderMaterialGraphNodeKind fromKind,
    std::uint32_t fromNodeId,
    std::string fromPin,
    RenderMaterialGraphNodeKind toKind,
    std::uint32_t toNodeId,
    std::string toPin) {
    RenderMaterialGraphLink link{
        .fromNodeId = fromNodeId,
        .fromPinId = RenderMaterialGraphStablePinId(fromKind, fromPin, true),
        .fromPin = std::move(fromPin),
        .toNodeId = toNodeId,
        .toPinId = RenderMaterialGraphStablePinId(toKind, toPin, false),
        .toPin = std::move(toPin),
    };
    link.id = MakeRenderMaterialGraphLinkId(link);
    return link;
}

void WriteReloadableMaterial(
    const std::filesystem::path& path,
    float red,
    float roughness,
    std::uint64_t albedoTextureId) {
    std::ofstream output{ path, std::ios::trunc };
    output
        << "baseColor " << red << " 0.25 0.5 1\n"
        << "roughnessFactor " << roughness << "\n"
        << "alphaMode OPAQUE\n"
        << "albedoTextureAssetId " << albedoTextureId << "\n";
}

void WriteGraphBackedReloadableMaterial(
    const std::filesystem::path& path,
    float red,
    float roughness,
    std::uint64_t materialTypeAssetId,
    std::string_view materialTypeAssetPath,
    std::uint64_t artifactAssetId,
    std::uint64_t artifactContentHash) {
    std::ofstream output{ path, std::ios::trunc };
    output
        << "version 1\n"
        << "materialType " << kRenderMaterialAssetBuiltInPbrType << "\n"
        << "materialTypeVersion " << kRenderMaterialAssetBuiltInPbrTypeVersion << "\n"
        << "materialTypeAssetId " << materialTypeAssetId << "\n"
        << "materialTypeAsset " << materialTypeAssetPath << "\n"
        << "baseColor 0.01 0.02 0.03 1\n"
        << "roughnessFactor 0.99\n"
        << "graphParameterValue baseColor Color " << red << " 0.35 0.15 1\n"
        << "graphParameterValue roughnessFactor Scalar " << roughness << "\n"
        << "alphaMode OPAQUE\n"
        << "graphLastGoodArtifactAssetId " << artifactAssetId << "\n"
        << "graphLastGoodArtifactHash " << artifactContentHash << "\n";
}

void WriteGraphValidationMaterial(const std::filesystem::path& path, float red, bool connectBaseColor) {
    std::ofstream output{ path, std::ios::trunc };
    output
        << "version 1\n"
        << "materialType builtin.pbr\n"
        << "materialTypeVersion 1\n"
        << "baseColor " << red << " 0.25 0.5 1\n"
        << "roughnessFactor 0.45\n"
        << "alphaMode OPAQUE\n"
        << "graphNode 1 MaterialOutput 640 240\n"
        << "graphNode 2 ConstantColor 120 80\n";
    if (connectBaseColor) {
        const RenderMaterialGraphLink link = MakeGraphLink(
            RenderMaterialGraphNodeKind::ConstantColor,
            2U,
            "rgba",
            RenderMaterialGraphNodeKind::MaterialOutput,
            1U,
            "baseColor");
        output
            << "graphLink " << link.id << ' '
            << link.fromNodeId << ' ' << link.fromPinId << ' ' << link.fromPin << ' '
            << link.toNodeId << ' ' << link.toPinId << ' ' << link.toPin << "\n";
    } else {
        const RenderMaterialGraphLink addToMultiply = MakeGraphLink(
            RenderMaterialGraphNodeKind::Add,
            3U,
            "value",
            RenderMaterialGraphNodeKind::Multiply,
            4U,
            "a");
        const RenderMaterialGraphLink multiplyToAdd = MakeGraphLink(
            RenderMaterialGraphNodeKind::Multiply,
            4U,
            "value",
            RenderMaterialGraphNodeKind::Add,
            3U,
            "a");
        const RenderMaterialGraphLink addToOutput = MakeGraphLink(
            RenderMaterialGraphNodeKind::Add,
            3U,
            "value",
            RenderMaterialGraphNodeKind::MaterialOutput,
            1U,
            "baseColor");
        output
            << "graphNode 3 Add 300 80\n"
            << "graphNode 4 Multiply 460 80\n"
            << "graphLink " << addToMultiply.id << ' '
            << addToMultiply.fromNodeId << ' ' << addToMultiply.fromPinId << ' ' << addToMultiply.fromPin << ' '
            << addToMultiply.toNodeId << ' ' << addToMultiply.toPinId << ' ' << addToMultiply.toPin << "\n"
            << "graphLink " << multiplyToAdd.id << ' '
            << multiplyToAdd.fromNodeId << ' ' << multiplyToAdd.fromPinId << ' ' << multiplyToAdd.fromPin << ' '
            << multiplyToAdd.toNodeId << ' ' << multiplyToAdd.toPinId << ' ' << multiplyToAdd.toPin << "\n"
            << "graphLink " << addToOutput.id << ' '
            << addToOutput.fromNodeId << ' ' << addToOutput.fromPinId << ' ' << addToOutput.fromPin << ' '
            << addToOutput.toNodeId << ' ' << addToOutput.toPinId << ' ' << addToOutput.toPin << "\n";
    }
}

[[nodiscard]] RenderMaterialGraphNode MakeGraphNode(
    std::uint32_t id,
    RenderMaterialGraphNodeKind kind,
    std::string stableId = {},
    std::string defaultValueHint = {}) {
    RenderMaterialGraphNode node{
        .id = id,
        .kind = kind,
        .positionX = static_cast<std::int32_t>(id * 140U),
        .positionY = 120,
    };
    node.parameter.stableId = std::move(stableId);
    node.parameter.defaultValueHint = std::move(defaultValueHint);
    return node;
}

[[nodiscard]] RenderMaterialGraphParameterValue MakeTextureGraphValue(std::string stableId, std::uint64_t assetId) {
    return RenderMaterialGraphParameterValue{
        .stableId = std::move(stableId),
        .type = RenderMaterialParameterType::Texture,
        .assetId = assetId,
    };
}

void RunRuntimeMaterialResolverEvaluatesMaterialOutputTextureGraphTest() {
    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    const kb::assets::AssetMetadata materialMetadata{
        .id = kb::assets::AssetId{ 71001U },
        .type = "RenderMaterial",
        .name = "GraphTextureMaterial",
    };

    RenderMaterialAssetData material{};
    material.desc.baseColor[0] = 0.25F;
    material.desc.baseColor[1] = 0.25F;
    material.desc.baseColor[2] = 0.25F;
    material.desc.baseColor[3] = 1.0F;
    material.desc.roughnessFactor = 0.25F;
    material.desc.metallicFactor = 0.0F;
    material.desc.occlusionStrength = 0.5F;

    material.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::TextureSample),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::TextureSample),
        MakeGraphNode(4U, RenderMaterialGraphNodeKind::NormalUnpack),
        MakeGraphNode(5U, RenderMaterialGraphNodeKind::TextureSample),
        MakeGraphNode(6U, RenderMaterialGraphNodeKind::TextureSample),
        MakeGraphNode(7U, RenderMaterialGraphNodeKind::TextureSample),
        MakeGraphNode(8U, RenderMaterialGraphNodeKind::ParameterScalar, "opacity"),
        MakeGraphNode(9U, RenderMaterialGraphNodeKind::ParameterTexture, "albedoParam"),
    };
    material.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ParameterTexture, 9U, "texture", RenderMaterialGraphNodeKind::TextureSample, 2U, "texture"),
        MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 2U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"),
        MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 3U, "color", RenderMaterialGraphNodeKind::NormalUnpack, 4U, "color"),
        MakeGraphLink(RenderMaterialGraphNodeKind::NormalUnpack, 4U, "normal", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "normal"),
        MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 5U, "g", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"),
        MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 5U, "b", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"),
        MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 6U, "r", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "occlusion"),
        MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 7U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ParameterScalar, 8U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"),
    };
    material.graphParameterValues = {
        MakeTextureGraphValue("albedoParam", 101U),
        MakeTextureGraphValue("textureSample2", 999U),
        MakeTextureGraphValue("textureSample3", 102U),
        MakeTextureGraphValue("textureSample5", 103U),
        MakeTextureGraphValue("textureSample6", 104U),
        MakeTextureGraphValue("textureSample7", 105U),
        RenderMaterialGraphParameterValue{
            .stableId = "opacity",
            .type = RenderMaterialParameterType::Scalar,
            .numbers = { 0.42F, 0.0F, 0.0F, 0.0F },
        },
    };

    const ResolvedRuntimeMaterialDesc resolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, material);
    Require(resolved.desc.albedoTextureAssetId == 101U, "KBMAT-RUNTIME: Texture Sample Color did not drive Material Output Base Color texture");
    Require(resolved.desc.normalTextureAssetId == 102U, "KBMAT-RUNTIME: Texture Sample through Normal Unpack did not drive Material Output Normal texture");
    Require(resolved.desc.metallicRoughnessTextureAssetId == 103U, "KBMAT-RUNTIME: Texture Sample G/B did not drive metallic-roughness texture");
    Require(resolved.desc.occlusionTextureAssetId == 104U, "KBMAT-RUNTIME: Texture Sample R did not drive occlusion texture");
    Require(resolved.desc.emissiveTextureAssetId == 105U, "KBMAT-RUNTIME: Texture Sample Color did not drive emissive texture");
    Require(NearlyEqual(resolved.desc.baseColor[0], 1.0F) && NearlyEqual(resolved.desc.baseColor[3], 0.42F), "KBMAT-RUNTIME: Material Output graph factors were not applied to base color/alpha");
    Require(NearlyEqual(resolved.desc.roughnessFactor, 1.0F) && NearlyEqual(resolved.desc.metallicFactor, 1.0F), "KBMAT-RUNTIME: Material Output scalar channels were not evaluated");

    RenderMaterialAssetData textureObjectNormal{};
    textureObjectNormal.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::TextureObject, "normalObject"),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::TextureSample),
        MakeGraphNode(4U, RenderMaterialGraphNodeKind::NormalUnpack),
    };
    textureObjectNormal.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::TextureObject, 2U, "texture", RenderMaterialGraphNodeKind::TextureSample, 3U, "texture"),
        MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 3U, "color", RenderMaterialGraphNodeKind::NormalUnpack, 4U, "color"),
        MakeGraphLink(RenderMaterialGraphNodeKind::NormalUnpack, 4U, "normal", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "normal"),
    };
    textureObjectNormal.graphParameterValues = {
        MakeTextureGraphValue("normalObject", 202U),
    };
    const ResolvedRuntimeMaterialDesc textureObjectResolved =
        RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, textureObjectNormal);
    Require(textureObjectResolved.desc.normalTextureAssetId == 202U,
        "KBMAT-RUNTIME: TextureObject through TextureSample and Normal Unpack did not drive Material Output Normal texture");

    RenderMaterialAssetData disconnected{};
    disconnected.desc.baseColor[0] = 0.75F;
    disconnected.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::TextureSample),
    };
    const ResolvedRuntimeMaterialDesc disconnectedResolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, disconnected);
    Require(
        NearlyEqual(disconnectedResolved.desc.baseColor[0], 1.0F) &&
            NearlyEqual(disconnectedResolved.desc.baseColor[1], 1.0F) &&
            NearlyEqual(disconnectedResolved.desc.baseColor[2], 1.0F) &&
            NearlyEqual(disconnectedResolved.desc.baseColor[3], 1.0F),
        "KBMAT-RUNTIME: Disconnected graph Base Color should resolve using MaterialSurface white default");
}

void RunRuntimeMaterialResolverEvaluatesConstantAndMathGraphTest() {
    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    const kb::assets::AssetMetadata materialMetadata{
        .id = kb::assets::AssetId{ 71002U },
        .type = "RenderMaterial",
        .name = "GraphConstantMaterial",
    };

    RenderMaterialAssetData material{};
    material.desc.baseColor[0] = 0.9F;
    material.desc.baseColor[1] = 0.9F;
    material.desc.baseColor[2] = 0.9F;
    material.desc.baseColor[3] = 1.0F;
    material.desc.roughnessFactor = 1.0F;
    material.desc.metallicFactor = 0.0F;
    material.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantColor, {}, "0 0 0 1"),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::ConstantColor, {}, "0.8 0.4 0.2 1"),
        MakeGraphNode(4U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.25"),
        MakeGraphNode(5U, RenderMaterialGraphNodeKind::Lerp),
        MakeGraphNode(6U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.8"),
        MakeGraphNode(7U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.3"),
        MakeGraphNode(8U, RenderMaterialGraphNodeKind::Subtract),
        MakeGraphNode(9U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "2"),
        MakeGraphNode(10U, RenderMaterialGraphNodeKind::Power),
        MakeGraphNode(11U, RenderMaterialGraphNodeKind::OneMinus),
        MakeGraphNode(12U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.15"),
        MakeGraphNode(13U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.3"),
        MakeGraphNode(14U, RenderMaterialGraphNodeKind::Divide),
    };
    material.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::Lerp, 5U, "a"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba", RenderMaterialGraphNodeKind::Lerp, 5U, "b"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 4U, "value", RenderMaterialGraphNodeKind::Lerp, 5U, "t"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Lerp, 5U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 6U, "value", RenderMaterialGraphNodeKind::Subtract, 8U, "a"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 7U, "value", RenderMaterialGraphNodeKind::Subtract, 8U, "b"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Subtract, 8U, "value", RenderMaterialGraphNodeKind::Power, 10U, "base"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 9U, "value", RenderMaterialGraphNodeKind::Power, 10U, "exponent"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Power, 10U, "value", RenderMaterialGraphNodeKind::OneMinus, 11U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::OneMinus, 11U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 12U, "value", RenderMaterialGraphNodeKind::Divide, 14U, "a"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 13U, "value", RenderMaterialGraphNodeKind::Divide, 14U, "b"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Divide, 14U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"),
    };

    const ResolvedRuntimeMaterialDesc resolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, material);
    Require(NearlyEqual(resolved.desc.baseColor[0], 0.2F) &&
            NearlyEqual(resolved.desc.baseColor[1], 0.1F) &&
            NearlyEqual(resolved.desc.baseColor[2], 0.05F) &&
            NearlyEqual(resolved.desc.baseColor[3], 1.0F),
        "KBMAT-RUNTIME: Constant Color through Lerp did not evaluate into Material Output Base Color");
    Require(NearlyEqual(resolved.desc.roughnessFactor, 0.75F), "KBMAT-RUNTIME: Subtract/Power/OneMinus graph did not evaluate into Roughness");
    Require(NearlyEqual(resolved.desc.metallicFactor, 0.5F), "KBMAT-RUNTIME: Divide graph did not evaluate into Metallic");

    RenderMaterialAssetData utilityMaterial{};
    utilityMaterial.desc.baseColor[0] = 1.0F;
    utilityMaterial.desc.baseColor[1] = 1.0F;
    utilityMaterial.desc.baseColor[2] = 1.0F;
    utilityMaterial.desc.baseColor[3] = 1.0F;
    utilityMaterial.desc.roughnessFactor = 1.0F;
    utilityMaterial.desc.metallicFactor = 0.0F;
    utilityMaterial.desc.occlusionStrength = 1.0F;
    utilityMaterial.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "-0.25"),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::Absolute),
        MakeGraphNode(4U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.4"),
        MakeGraphNode(5U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.8"),
        MakeGraphNode(6U, RenderMaterialGraphNodeKind::Minimum),
        MakeGraphNode(7U, RenderMaterialGraphNodeKind::Maximum),
        MakeGraphNode(8U, RenderMaterialGraphNodeKind::Saturate),
        MakeGraphNode(9U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "1.75"),
        MakeGraphNode(10U, RenderMaterialGraphNodeKind::Fraction),
        MakeGraphNode(11U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.25"),
        MakeGraphNode(12U, RenderMaterialGraphNodeKind::SquareRoot),
        MakeGraphNode(13U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.9"),
        MakeGraphNode(14U, RenderMaterialGraphNodeKind::Floor),
        MakeGraphNode(15U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.1"),
        MakeGraphNode(16U, RenderMaterialGraphNodeKind::Ceil),
        MakeGraphNode(17U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0"),
        MakeGraphNode(18U, RenderMaterialGraphNodeKind::Sine),
        MakeGraphNode(19U, RenderMaterialGraphNodeKind::Cosine),
    };
    utilityMaterial.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 2U, "value", RenderMaterialGraphNodeKind::Absolute, 3U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 4U, "value", RenderMaterialGraphNodeKind::Minimum, 6U, "a"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 5U, "value", RenderMaterialGraphNodeKind::Minimum, 6U, "b"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Absolute, 3U, "value", RenderMaterialGraphNodeKind::Maximum, 7U, "a"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Minimum, 6U, "value", RenderMaterialGraphNodeKind::Maximum, 7U, "b"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Maximum, 7U, "value", RenderMaterialGraphNodeKind::Saturate, 8U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Saturate, 8U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 9U, "value", RenderMaterialGraphNodeKind::Fraction, 10U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Fraction, 10U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 11U, "value", RenderMaterialGraphNodeKind::SquareRoot, 12U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::SquareRoot, 12U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "occlusion"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 13U, "value", RenderMaterialGraphNodeKind::Floor, 14U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Floor, 14U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 15U, "value", RenderMaterialGraphNodeKind::Ceil, 16U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Ceil, 16U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 17U, "value", RenderMaterialGraphNodeKind::Sine, 18U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Sine, 18U, "value", RenderMaterialGraphNodeKind::Cosine, 19U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Cosine, 19U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"),
    };
    const ResolvedRuntimeMaterialDesc utilityResolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, utilityMaterial);
    Require(NearlyEqual(utilityResolved.desc.roughnessFactor, 0.4F), "KBMAT-RUNTIME: Abs/Min/Max/Saturate graph did not evaluate into Roughness");
    Require(NearlyEqual(utilityResolved.desc.metallicFactor, 0.75F), "KBMAT-RUNTIME: Fraction graph did not evaluate into Metallic");
    Require(NearlyEqual(utilityResolved.desc.occlusionStrength, 0.5F), "KBMAT-RUNTIME: SquareRoot graph did not evaluate into Occlusion");
    Require(NearlyEqual(utilityResolved.desc.baseColor[0], 1.0F), "KBMAT-RUNTIME: Ceil graph did not evaluate into Base Color");
    Require(NearlyEqual(utilityResolved.desc.baseColor[3], 0.0F), "KBMAT-RUNTIME: Floor graph did not evaluate into Alpha");
    Require(NearlyEqual(utilityResolved.desc.emissiveColor[0], 1.0F), "KBMAT-RUNTIME: Sine/Cosine graph did not evaluate into Emissive");

    RenderMaterialAssetData vectorMaterial{};
    vectorMaterial.desc.baseColor[0] = 1.0F;
    vectorMaterial.desc.baseColor[1] = 1.0F;
    vectorMaterial.desc.baseColor[2] = 1.0F;
    vectorMaterial.desc.baseColor[3] = 1.0F;
    vectorMaterial.desc.roughnessFactor = 1.0F;
    vectorMaterial.desc.metallicFactor = 0.0F;
    vectorMaterial.desc.occlusionStrength = 1.0F;
    vectorMaterial.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantVector, {}, "1 0 0"),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::ConstantVector, {}, "0.5 0 0"),
        MakeGraphNode(4U, RenderMaterialGraphNodeKind::DotProduct),
        MakeGraphNode(5U, RenderMaterialGraphNodeKind::ConstantVector, {}, "1 0 0"),
        MakeGraphNode(6U, RenderMaterialGraphNodeKind::ConstantVector, {}, "0 1 0"),
        MakeGraphNode(7U, RenderMaterialGraphNodeKind::CrossProduct),
        MakeGraphNode(8U, RenderMaterialGraphNodeKind::Normalize),
        MakeGraphNode(9U, RenderMaterialGraphNodeKind::Length),
        MakeGraphNode(10U, RenderMaterialGraphNodeKind::ConstantVector, {}, "0 0 0"),
        MakeGraphNode(11U, RenderMaterialGraphNodeKind::ConstantVector, {}, "0 0 0.25"),
        MakeGraphNode(12U, RenderMaterialGraphNodeKind::Distance),
        MakeGraphNode(13U, RenderMaterialGraphNodeKind::ConstantColor, {}, "1 1 1 1"),
    };
    vectorMaterial.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 13U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 2U, "xyz", RenderMaterialGraphNodeKind::DotProduct, 4U, "a"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 3U, "xyz", RenderMaterialGraphNodeKind::DotProduct, 4U, "b"),
        MakeGraphLink(RenderMaterialGraphNodeKind::DotProduct, 4U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 5U, "xyz", RenderMaterialGraphNodeKind::CrossProduct, 7U, "a"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 6U, "xyz", RenderMaterialGraphNodeKind::CrossProduct, 7U, "b"),
        MakeGraphLink(RenderMaterialGraphNodeKind::CrossProduct, 7U, "value", RenderMaterialGraphNodeKind::Normalize, 8U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Normalize, 8U, "value", RenderMaterialGraphNodeKind::Length, 9U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Length, 9U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 10U, "xyz", RenderMaterialGraphNodeKind::Distance, 12U, "a"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 11U, "xyz", RenderMaterialGraphNodeKind::Distance, 12U, "b"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Distance, 12U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "occlusion"),
    };
    const ResolvedRuntimeMaterialDesc vectorResolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, vectorMaterial);
    Require(NearlyEqual(vectorResolved.desc.roughnessFactor, 0.5F), "KBMAT-RUNTIME: DotProduct graph did not evaluate into Roughness");
    Require(NearlyEqual(vectorResolved.desc.metallicFactor, 1.0F), "KBMAT-RUNTIME: CrossProduct/Normalize/Length graph did not evaluate into Metallic");
    Require(NearlyEqual(vectorResolved.desc.occlusionStrength, 0.25F), "KBMAT-RUNTIME: Distance graph did not evaluate into Occlusion");

    RenderMaterialAssetData channelMaterial{};
    channelMaterial.desc.baseColor[0] = 1.0F;
    channelMaterial.desc.baseColor[1] = 1.0F;
    channelMaterial.desc.baseColor[2] = 1.0F;
    channelMaterial.desc.baseColor[3] = 1.0F;
    channelMaterial.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantColor, {}, "0.2 0.4 0.6 0.8"),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::BreakVector),
        MakeGraphNode(4U, RenderMaterialGraphNodeKind::MakeVector),
    };
    channelMaterial.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::BreakVector, 3U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::BreakVector, 3U, "z", RenderMaterialGraphNodeKind::MakeVector, 4U, "x"),
        MakeGraphLink(RenderMaterialGraphNodeKind::BreakVector, 3U, "y", RenderMaterialGraphNodeKind::MakeVector, 4U, "y"),
        MakeGraphLink(RenderMaterialGraphNodeKind::BreakVector, 3U, "x", RenderMaterialGraphNodeKind::MakeVector, 4U, "z"),
        MakeGraphLink(RenderMaterialGraphNodeKind::BreakVector, 3U, "w", RenderMaterialGraphNodeKind::MakeVector, 4U, "w"),
        MakeGraphLink(RenderMaterialGraphNodeKind::MakeVector, 4U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"),
    };
    const ResolvedRuntimeMaterialDesc channelResolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, channelMaterial);
    Require(NearlyEqual(channelResolved.desc.baseColor[0], 0.6F) &&
            NearlyEqual(channelResolved.desc.baseColor[1], 0.4F) &&
            NearlyEqual(channelResolved.desc.baseColor[2], 0.2F) &&
            NearlyEqual(channelResolved.desc.baseColor[3], 0.8F),
        "KBMAT-RUNTIME: BreakVector/MakeVector graph did not remap Base Color channels");

    RenderMaterialAssetData conditionalMaterial{};
    conditionalMaterial.desc.baseColor[0] = 1.0F;
    conditionalMaterial.desc.baseColor[1] = 1.0F;
    conditionalMaterial.desc.baseColor[2] = 1.0F;
    conditionalMaterial.desc.baseColor[3] = 1.0F;
    conditionalMaterial.desc.roughnessFactor = 0.0F;
    conditionalMaterial.desc.metallicFactor = 0.0F;
    conditionalMaterial.desc.occlusionStrength = 1.0F;
    conditionalMaterial.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantColor, {}, "1 1 1 1"),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"),
        MakeGraphNode(4U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.75"),
        MakeGraphNode(5U, RenderMaterialGraphNodeKind::Step),
        MakeGraphNode(6U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0"),
        MakeGraphNode(7U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "1"),
        MakeGraphNode(8U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"),
        MakeGraphNode(9U, RenderMaterialGraphNodeKind::SmoothStep),
        MakeGraphNode(10U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.25"),
        MakeGraphNode(11U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"),
        MakeGraphNode(12U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.2"),
        MakeGraphNode(13U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.4"),
        MakeGraphNode(14U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.8"),
        MakeGraphNode(15U, RenderMaterialGraphNodeKind::If),
        MakeGraphNode(16U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"),
        MakeGraphNode(17U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"),
        MakeGraphNode(18U, RenderMaterialGraphNodeKind::If),
        MakeGraphNode(19U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.75"),
        MakeGraphNode(20U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"),
        MakeGraphNode(21U, RenderMaterialGraphNodeKind::If),
    };
    conditionalMaterial.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 3U, "value", RenderMaterialGraphNodeKind::Step, 5U, "edge"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 4U, "value", RenderMaterialGraphNodeKind::Step, 5U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Step, 5U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 6U, "value", RenderMaterialGraphNodeKind::SmoothStep, 9U, "min"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 7U, "value", RenderMaterialGraphNodeKind::SmoothStep, 9U, "max"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 8U, "value", RenderMaterialGraphNodeKind::SmoothStep, 9U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::SmoothStep, 9U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 10U, "value", RenderMaterialGraphNodeKind::If, 15U, "a"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 11U, "value", RenderMaterialGraphNodeKind::If, 15U, "b"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 12U, "value", RenderMaterialGraphNodeKind::If, 15U, "less"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 13U, "value", RenderMaterialGraphNodeKind::If, 15U, "equal"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 14U, "value", RenderMaterialGraphNodeKind::If, 15U, "greater"),
        MakeGraphLink(RenderMaterialGraphNodeKind::If, 15U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "occlusion"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 16U, "value", RenderMaterialGraphNodeKind::If, 18U, "a"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 17U, "value", RenderMaterialGraphNodeKind::If, 18U, "b"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 12U, "value", RenderMaterialGraphNodeKind::If, 18U, "less"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 13U, "value", RenderMaterialGraphNodeKind::If, 18U, "equal"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 14U, "value", RenderMaterialGraphNodeKind::If, 18U, "greater"),
        MakeGraphLink(RenderMaterialGraphNodeKind::If, 18U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 19U, "value", RenderMaterialGraphNodeKind::If, 21U, "a"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 20U, "value", RenderMaterialGraphNodeKind::If, 21U, "b"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 12U, "value", RenderMaterialGraphNodeKind::If, 21U, "less"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 13U, "value", RenderMaterialGraphNodeKind::If, 21U, "equal"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 14U, "value", RenderMaterialGraphNodeKind::If, 21U, "greater"),
        MakeGraphLink(RenderMaterialGraphNodeKind::If, 21U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"),
    };
    const ResolvedRuntimeMaterialDesc conditionalResolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, conditionalMaterial);
    Require(NearlyEqual(conditionalResolved.desc.roughnessFactor, 1.0F), "KBMAT-RUNTIME: Step graph did not evaluate into Roughness");
    Require(NearlyEqual(conditionalResolved.desc.metallicFactor, 0.5F), "KBMAT-RUNTIME: SmoothStep graph did not evaluate into Metallic");
    Require(NearlyEqual(conditionalResolved.desc.occlusionStrength, 0.2F), "KBMAT-RUNTIME: If less branch did not evaluate into Occlusion");
    Require(NearlyEqual(conditionalResolved.desc.baseColor[3], 0.4F), "KBMAT-RUNTIME: If equal branch did not evaluate into Alpha");
    Require(NearlyEqual(conditionalResolved.desc.emissiveColor[0], 0.8F), "KBMAT-RUNTIME: If greater branch did not evaluate into Emissive");

    RenderMaterialAssetData switchMaterial{};
    switchMaterial.desc.baseColor[0] = 1.0F;
    switchMaterial.desc.baseColor[1] = 1.0F;
    switchMaterial.desc.baseColor[2] = 1.0F;
    switchMaterial.desc.baseColor[3] = 1.0F;
    switchMaterial.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "2.4"),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "9"),
        MakeGraphNode(4U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.1"),
        MakeGraphNode(5U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.2"),
        MakeGraphNode(6U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.4"),
        MakeGraphNode(7U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.7"),
        MakeGraphNode(8U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.9"),
        MakeGraphNode(9U, RenderMaterialGraphNodeKind::RuntimeSwitch),
        MakeGraphNode(10U, RenderMaterialGraphNodeKind::RuntimeSwitch),
    };
    switchMaterial.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 2U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 9U, "index"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 4U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 9U, "default"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 5U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 9U, "case0"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 6U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 9U, "case1"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 7U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 9U, "case2"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 8U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 9U, "case3"),
        MakeGraphLink(RenderMaterialGraphNodeKind::RuntimeSwitch, 9U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 3U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 10U, "index"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 4U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 10U, "default"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 5U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 10U, "case0"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 6U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 10U, "case1"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 7U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 10U, "case2"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 8U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 10U, "case3"),
        MakeGraphLink(RenderMaterialGraphNodeKind::RuntimeSwitch, 10U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"),
    };
    const ResolvedRuntimeMaterialDesc switchResolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, switchMaterial);
    Require(NearlyEqual(switchResolved.desc.roughnessFactor, 0.7F), "KBMAT-RUNTIME: Switch case2 branch did not evaluate into Roughness");
    Require(NearlyEqual(switchResolved.desc.baseColor[3], 0.1F), "KBMAT-RUNTIME: Switch default branch did not evaluate into Alpha");

    RenderMaterialAssetData sobolMaterial{};
    sobolMaterial.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "1"),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::Sobol),
    };
    sobolMaterial.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 2U, "value", RenderMaterialGraphNodeKind::Sobol, 3U, "index"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Sobol, 3U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Sobol, 3U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"),
    };
    const ResolvedRuntimeMaterialDesc sobolResolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, sobolMaterial);
    Require(NearlyEqual(sobolResolved.desc.baseColor[0], 34432.0F / 65536.0F) &&
            NearlyEqual(sobolResolved.desc.baseColor[1], 19584.0F / 65536.0F) &&
            NearlyEqual(sobolResolved.desc.baseColor[2], 0.0F) &&
            NearlyEqual(sobolResolved.desc.baseColor[3], 1.0F),
        "KBMAT-RUNTIME: Sobol graph did not evaluate its deterministic Float2 sample into Base Color");
    Require(NearlyEqual(sobolResolved.desc.roughnessFactor, 34432.0F / 65536.0F),
        "KBMAT-RUNTIME: Sobol graph did not coerce its Float2 sample into Roughness");

    RenderMaterialAssetData surfaceMaterial{};
    surfaceMaterial.desc.baseColor[0] = 1.0F;
    surfaceMaterial.desc.baseColor[1] = 1.0F;
    surfaceMaterial.desc.baseColor[2] = 1.0F;
    surfaceMaterial.desc.baseColor[3] = 1.0F;
    surfaceMaterial.desc.roughnessFactor = 0.0F;
    surfaceMaterial.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantColor, {}, "1 0 0 1"),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "1"),
        MakeGraphNode(4U, RenderMaterialGraphNodeKind::Desaturate),
        MakeGraphNode(5U, RenderMaterialGraphNodeKind::ConstantVector, {}, "0 0 1"),
        MakeGraphNode(6U, RenderMaterialGraphNodeKind::ConstantVector, {}, "0 0 -1"),
        MakeGraphNode(7U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "2"),
        MakeGraphNode(8U, RenderMaterialGraphNodeKind::Fresnel),
    };
    surfaceMaterial.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::Desaturate, 4U, "color"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 3U, "value", RenderMaterialGraphNodeKind::Desaturate, 4U, "fraction"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Desaturate, 4U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 5U, "xyz", RenderMaterialGraphNodeKind::Fresnel, 8U, "normal"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 6U, "xyz", RenderMaterialGraphNodeKind::Fresnel, 8U, "view"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 7U, "value", RenderMaterialGraphNodeKind::Fresnel, 8U, "exponent"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Fresnel, 8U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"),
    };
    const ResolvedRuntimeMaterialDesc surfaceResolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, surfaceMaterial);
    Require(NearlyEqual(surfaceResolved.desc.baseColor[0], 0.299F) &&
            NearlyEqual(surfaceResolved.desc.baseColor[1], 0.299F) &&
            NearlyEqual(surfaceResolved.desc.baseColor[2], 0.299F),
        "KBMAT-RUNTIME: Desaturate graph did not evaluate into Base Color");
    Require(NearlyEqual(surfaceResolved.desc.roughnessFactor, 1.0F), "KBMAT-RUNTIME: Fresnel graph did not evaluate into Roughness");

    RenderMaterialAssetData advancedMathMaterial{};
    advancedMathMaterial.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "-0.25"),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::Negate),
        MakeGraphNode(4U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.6"),
        MakeGraphNode(5U, RenderMaterialGraphNodeKind::Round),
        MakeGraphNode(6U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.75"),
        MakeGraphNode(7U, RenderMaterialGraphNodeKind::Truncate),
        MakeGraphNode(8U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"),
        MakeGraphNode(9U, RenderMaterialGraphNodeKind::Sign),
        MakeGraphNode(10U, RenderMaterialGraphNodeKind::MakeVector),
        MakeGraphNode(11U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0"),
        MakeGraphNode(12U, RenderMaterialGraphNodeKind::Tangent),
        MakeGraphNode(13U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"),
        MakeGraphNode(14U, RenderMaterialGraphNodeKind::ArcSine),
        MakeGraphNode(15U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"),
        MakeGraphNode(16U, RenderMaterialGraphNodeKind::ArcCosine),
        MakeGraphNode(17U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "1"),
        MakeGraphNode(18U, RenderMaterialGraphNodeKind::ArcTangent),
        MakeGraphNode(19U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "1"),
        MakeGraphNode(20U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "1"),
        MakeGraphNode(21U, RenderMaterialGraphNodeKind::ArcTangent2),
    };
    advancedMathMaterial.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 2U, "value", RenderMaterialGraphNodeKind::Negate, 3U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Negate, 3U, "value", RenderMaterialGraphNodeKind::MakeVector, 10U, "x"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 4U, "value", RenderMaterialGraphNodeKind::Round, 5U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Round, 5U, "value", RenderMaterialGraphNodeKind::MakeVector, 10U, "y"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 6U, "value", RenderMaterialGraphNodeKind::Truncate, 7U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Truncate, 7U, "value", RenderMaterialGraphNodeKind::MakeVector, 10U, "z"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 8U, "value", RenderMaterialGraphNodeKind::Sign, 9U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Sign, 9U, "value", RenderMaterialGraphNodeKind::MakeVector, 10U, "w"),
        MakeGraphLink(RenderMaterialGraphNodeKind::MakeVector, 10U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 11U, "value", RenderMaterialGraphNodeKind::Tangent, 12U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Tangent, 12U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 13U, "value", RenderMaterialGraphNodeKind::ArcSine, 14U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ArcSine, 14U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 15U, "value", RenderMaterialGraphNodeKind::ArcCosine, 16U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ArcCosine, 16U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 17U, "value", RenderMaterialGraphNodeKind::ArcTangent, 18U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ArcTangent, 18U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "occlusion"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 19U, "value", RenderMaterialGraphNodeKind::ArcTangent2, 21U, "y"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 20U, "value", RenderMaterialGraphNodeKind::ArcTangent2, 21U, "x"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ArcTangent2, 21U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"),
    };
    const ResolvedRuntimeMaterialDesc advancedMathResolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, advancedMathMaterial);
    Require(NearlyEqual(advancedMathResolved.desc.baseColor[0], 0.25F) &&
            NearlyEqual(advancedMathResolved.desc.baseColor[1], 1.0F) &&
            NearlyEqual(advancedMathResolved.desc.baseColor[2], 0.0F),
        "KBMAT-RUNTIME: Negate/Round/Truncate graph did not evaluate into Base Color");
    Require(NearlyEqual(advancedMathResolved.desc.baseColor[3], 0.785398F), "KBMAT-RUNTIME: Atan2 graph did not evaluate into Alpha");
    Require(NearlyEqual(advancedMathResolved.desc.roughnessFactor, 0.0F), "KBMAT-RUNTIME: Tangent graph did not evaluate into Roughness");
    Require(NearlyEqual(advancedMathResolved.desc.metallicFactor, 0.523599F), "KBMAT-RUNTIME: ArcSine graph did not evaluate into Metallic");
    Require(NearlyEqual(advancedMathResolved.desc.occlusionStrength, 0.785398F), "KBMAT-RUNTIME: ArcTangent graph did not evaluate into Occlusion");
    Require(NearlyEqual(advancedMathResolved.desc.emissiveColor[0], 1.047198F), "KBMAT-RUNTIME: ArcCosine graph did not evaluate into Emissive");

    RenderMaterialAssetData fastTrigMaterial{};
    fastTrigMaterial.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::ArcSineFast),
        MakeGraphNode(4U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"),
        MakeGraphNode(5U, RenderMaterialGraphNodeKind::ArcCosineFast),
        MakeGraphNode(6U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "1"),
        MakeGraphNode(7U, RenderMaterialGraphNodeKind::ArcTangentFast),
        MakeGraphNode(8U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "1"),
        MakeGraphNode(9U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "1"),
        MakeGraphNode(10U, RenderMaterialGraphNodeKind::ArcTangent2Fast),
    };
    fastTrigMaterial.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 2U, "value", RenderMaterialGraphNodeKind::ArcSineFast, 3U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ArcSineFast, 3U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 4U, "value", RenderMaterialGraphNodeKind::ArcCosineFast, 5U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ArcCosineFast, 5U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 6U, "value", RenderMaterialGraphNodeKind::ArcTangentFast, 7U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ArcTangentFast, 7U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "occlusion"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 8U, "value", RenderMaterialGraphNodeKind::ArcTangent2Fast, 10U, "y"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 9U, "value", RenderMaterialGraphNodeKind::ArcTangent2Fast, 10U, "x"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ArcTangent2Fast, 10U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"),
    };
    const ResolvedRuntimeMaterialDesc fastTrigResolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, fastTrigMaterial);
    Require(NearlyEqual(fastTrigResolved.desc.metallicFactor, 0.523403F), "KBMAT-RUNTIME: ArcSineFast graph did not evaluate into Metallic");
    Require(NearlyEqual(fastTrigResolved.desc.emissiveColor[0], 1.047394F), "KBMAT-RUNTIME: ArcCosineFast graph did not evaluate into Emissive");
    Require(NearlyEqual(fastTrigResolved.desc.occlusionStrength, 0.785814F), "KBMAT-RUNTIME: ArcTangentFast graph did not evaluate into Occlusion");
    Require(NearlyEqual(fastTrigResolved.desc.baseColor[3], 0.785814F), "KBMAT-RUNTIME: ArcTangent2Fast graph did not evaluate into Alpha");
}

[[nodiscard]] RenderMaterialAssetData MakeDimensionGraphMaterial(
    RenderMaterialGraphNodeKind sampleKind,
    std::string stableId,
    std::uint64_t textureAssetId) {
    RenderMaterialAssetData material{};
    material.graph = MakeDefaultRenderMaterialGraphDocument();
    material.graph.shadingModel = "unlit";
    RenderMaterialGraphNode sample = MakeGraphNode(2U, sampleKind, stableId);
    sample.parameter.textureRole = "occlusion";
    sample.parameter.expectedTextureColorSpace = RenderMaterialTextureColorSpace::Linear;
    material.graph.nodes.push_back(std::move(sample));
    material.graph.links.push_back(MakeGraphLink(
        sampleKind,
        2U,
        "a",
        RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "alpha"));
    material.graphParameterValues.push_back(MakeTextureGraphValue(std::move(stableId), textureAssetId));
    return material;
}

void WriteDimensionTexture(
    const std::filesystem::path& path,
    RenderTextureDimension dimension,
    std::uint16_t depth = 1U,
    std::uint16_t layers = 1U) {
    std::ofstream output{ path, std::ios::trunc };
    output << "dimension ";
    switch (dimension) {
    case RenderTextureDimension::Texture2D: output << "2d\n"; break;
    case RenderTextureDimension::TextureCube: output << "cube\n"; break;
    case RenderTextureDimension::Texture3D: output << "3d\n"; break;
    case RenderTextureDimension::Texture2DArray: output << "2dArray\n"; break;
    }
    output << "size 2 2\n";
    if (dimension == RenderTextureDimension::Texture3D) {
        output << "depth " << depth << "\n";
    }
    if (dimension == RenderTextureDimension::Texture2DArray) {
        output << "layers " << layers << "\n";
    }
    output << "rgba8 32 96 224 255\n";
}

void RunRendererUsesResolverDefaultFallbackForMissingMaterialTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_missing_material_fallback";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Missing material fallback test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    WriteTriangleObj(meshPath);

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Missing material fallback test could not register mesh loader");
    Require(manager.Mounts().Mount("Game", root), "Missing material fallback test could not mount asset root");
    Require(manager.DiscoverMountedAssets() == 1U, "Missing material fallback test did not discover mesh asset");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Missing material fallback test discovered wrong mesh metadata");

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Missing Material Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialAssetId = 404404U,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Missing material fallback test renderer did not initialize");
    Require(renderer.BeginFrame(), "Missing material fallback test renderer did not begin frame");
    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
    };
    Require(renderer.SubmitScene(scene, desc), "Missing material fallback test renderer did not submit scene");

    const Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
    Require(runtimeStats.defaultMaterialFallbackCount == 1U, "Missing material fallback should increment runtime default material fallback stats");
    Require(runtimeStats.errorMaterialFallbackCount == 0U, "Missing material fallback should not increment runtime error material fallback stats");
    Require(runtimeStats.materialLoadedCount == 1U, "KBMAT-0901: Missing material fallback should count one material load");
    Require(runtimeStats.materialFallbackCount == 1U, "KBMAT-0901: Missing material fallback should count one material fallback");
    Require(runtimeStats.materialErrorCount == 0U, "KBMAT-0901: Missing material fallback should not count as material error");
    Require(runtimeStats.materialReloadCount == 0U, "KBMAT-0901: Missing material fallback should not count as material reload");
    Require(runtimeStats.materialResolverDiagnosticCount == 1U, "Missing material fallback should report one resolver diagnostic");
    Require(runtimeStats.cachedMaterialCount == 1U, "Missing material fallback should register a runtime material resource");
    Require(!renderer.LastSceneSubmitStats().HasMissingResources(), "Missing material fallback should keep submit resources valid");

    bool foundMissingMaterialDiagnostic = false;
    for (const SceneRenderDiagnosticEvent& event : renderer.LastSceneDiagnostics().events) {
        if (event.kind == SceneRenderDiagnosticKind::MissingMaterialAsset && event.materialAssetId == 404404U) {
            foundMissingMaterialDiagnostic = true;
        }
    }
    Require(foundMissingMaterialDiagnostic, "Missing material fallback should emit a render diagnostic");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunRuntimeGraphMaterialRenderModeReportingTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_runtime_graph_render_mode";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Graph render mode test could not create temp root");

    RenderMaterialAssetData gpuMaterial{};
    gpuMaterial.graph = MakeDefaultRenderMaterialGraphDocument();
    gpuMaterial.graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .positionX = -160, .positionY = 64 });
    gpuMaterial.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    Require(RenderMaterialAssetWriter::Save(root / "gpu.kbmat", gpuMaterial), "Graph render mode test could not save GPU graph material");

    RenderMaterialAssetData builtinMaterial{};
    builtinMaterial.desc.baseColor[0] = 0.5F;
    Require(RenderMaterialAssetWriter::Save(root / "builtin.kbmat", builtinMaterial), "Graph render mode test could not save builtin material");

    // A valid MaterialOutput subgraph plus an orphan cycle: the runtime MaterialOutput subset stays valid (no error
    // material), but the full shader compile rejects the cycle, so no GPU program is produced and the runtime must
    // fall back to CPU PBR flattening with an explicit reason.
    RenderMaterialAssetData cpuMaterial{};
    cpuMaterial.graph = MakeDefaultRenderMaterialGraphDocument();
    cpuMaterial.graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .positionX = -160, .positionY = 64 });
    cpuMaterial.graph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::Add, .positionX = -360, .positionY = 200 });
    cpuMaterial.graph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::Add, .positionX = -360, .positionY = 320 });
    cpuMaterial.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    cpuMaterial.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Add, 3U, "value", RenderMaterialGraphNodeKind::Add, 4U, "a"));
    cpuMaterial.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Add, 4U, "value", RenderMaterialGraphNodeKind::Add, 3U, "a"));
    Require(RenderMaterialAssetWriter::Save(root / "cpu.kbmat", cpuMaterial), "Graph render mode test could not save CPU fallback material");

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Graph render mode test could not register material loader");
    Require(manager.Mounts().Mount("Game", root), "Graph render mode test could not mount asset root");
    Require(manager.DiscoverMountedAssets() == 3U, "Graph render mode test did not discover three materials");

    RuntimeMaterialResolver resolver;

    const kb::assets::AssetMetadata* gpuMeta = manager.Registry().FindByPath("/Game/gpu.kbmat");
    Require(gpuMeta != nullptr, "Graph render mode test did not find GPU material metadata");
    const ResolvedRuntimeMaterialAsset gpu = resolver.ResolveAsset(manager, *gpuMeta);
    Require(gpu.resolved && gpu.status == RuntimeMaterialResolveStatus::Resolved, "MAT-26: A valid graph material must resolve");
    Require(gpu.renderMode == RuntimeMaterialRenderMode::GpuMaterialGraph, "MAT-26: A valid graph material must resolve to the GPU material graph path, not CPU flattening");
    Require(gpu.material.graphProgram.active && gpu.material.graphProgram.graphSourceHash != 0U, "MAT-26: GPU graph program must be bound with a non-zero program key");
    Require(gpu.cpuFallbackReason == RuntimeMaterialCpuFallbackReason::None, "MAT-26: A GPU graph material must not carry a CPU fallback reason");

    const kb::assets::AssetMetadata* builtinMeta = manager.Registry().FindByPath("/Game/builtin.kbmat");
    Require(builtinMeta != nullptr, "Graph render mode test did not find builtin material metadata");
    const ResolvedRuntimeMaterialAsset builtin = resolver.ResolveAsset(manager, *builtinMeta);
    Require(builtin.resolved && builtin.renderMode == RuntimeMaterialRenderMode::BuiltinPbr, "MAT-27: A non-graph material must keep the builtin PBR path working");
    Require(!builtin.material.graphProgram.active, "MAT-27: A builtin PBR material must not bind a graph program");

    const kb::assets::AssetMetadata* cpuMeta = manager.Registry().FindByPath("/Game/cpu.kbmat");
    Require(cpuMeta != nullptr, "Graph render mode test did not find CPU fallback material metadata");
    const ResolvedRuntimeMaterialAsset cpu = resolver.ResolveAsset(manager, *cpuMeta);
    Require(cpu.resolved && cpu.status == RuntimeMaterialResolveStatus::Resolved, "MAT-27: A graph material without a GPU program must still resolve instead of erroring");
    Require(cpu.renderMode == RuntimeMaterialRenderMode::CpuPbrFlatteningFallback, "MAT-27: A graph material without a GPU program must fall back to CPU PBR flattening");
    Require(cpu.cpuFallbackReason == RuntimeMaterialCpuFallbackReason::GraphProgramUnavailable, "MAT-27: CPU fallback must carry an explicit reason code");
    Require(!cpu.material.graphProgram.active, "MAT-27: CPU fallback material must not advertise an active GPU program");
    bool foundFallbackDiagnostic = false;
    for (const RuntimeMaterialResolveDiagnostic& diagnostic : cpu.diagnostics) {
        if (diagnostic.message.find("CPU PBR flattening") != std::string::npos) {
            foundFallbackDiagnostic = true;
        }
    }
    Require(foundFallbackDiagnostic, "MAT-27: CPU fallback must be reported through a resolver diagnostic, not hidden");

    Require(std::string_view{ "GpuMaterialGraph" } == RuntimeMaterialRenderModeName(RuntimeMaterialRenderMode::GpuMaterialGraph) &&
            std::string_view{ "CpuPbrFlatteningFallback" } == RuntimeMaterialRenderModeName(RuntimeMaterialRenderMode::CpuPbrFlatteningFallback) &&
            std::string_view{ "BuiltinPbr" } == RuntimeMaterialRenderModeName(RuntimeMaterialRenderMode::BuiltinPbr),
        "MAT-26: Render mode names must be stable for telemetry");
    Require(std::string_view{ "GraphProgramUnavailable" } == RuntimeMaterialCpuFallbackReasonName(RuntimeMaterialCpuFallbackReason::GraphProgramUnavailable),
        "MAT-26: CPU fallback reason names must be stable for telemetry");

    std::filesystem::remove_all(root, error);
}

void RunRuntimeMaterialInstanceDynamicParameterOverrideTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_runtime_material_instance_dynamic";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "MAT-40 instance dynamic test could not create temp root");

    RenderMaterialAssetData parent{};
    parent.graph = MakeDefaultRenderMaterialGraphDocument();
    parent.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ParameterColor,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "tint",
            .displayName = "Tint",
            .defaultValueHint = "1 0 0 1",
            .overrideSupported = true,
        },
    });
    parent.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ParameterColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    parent.graphParameterValues.push_back(RenderMaterialGraphParameterValue{
        .stableId = "tint",
        .type = RenderMaterialParameterType::Color,
        .numbers = { 1.0F, 0.0F, 0.0F, 1.0F },
    });
    Require(RenderMaterialAssetWriter::Save(root / "Parent.kbmat", parent), "MAT-40 instance dynamic test could not save parent material");

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "MAT-40 instance dynamic test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialInstanceAssetLoader>()), "MAT-40 instance dynamic test could not register instance loader");
    Require(manager.Mounts().Mount("Game", root), "MAT-40 instance dynamic test could not mount asset root");
    Require(manager.DiscoverMountedAssets() == 1U, "MAT-40 instance dynamic test did not discover parent material");
    const kb::assets::AssetMetadata* parentMeta = manager.Registry().FindByPath("/Game/Parent.kbmat");
    Require(parentMeta != nullptr, "MAT-40 instance dynamic test did not find parent material metadata");

    RenderMaterialInstanceAssetData instance{};
    instance.parentMaterialAssetId = parentMeta->id;
    instance.hasOverrides = true;
    instance.overrides = parent;
    instance.overrides.graphParameterValues.clear();
    instance.overrides.graphParameterValues.push_back(RenderMaterialGraphParameterValue{
        .stableId = "tint",
        .type = RenderMaterialParameterType::Color,
        .numbers = { 0.0F, 1.0F, 0.0F, 1.0F },
    });
    Require(RenderMaterialInstanceAssetWriter::Save(root / "Parent_Inst.kbmatinst", instance), "MAT-40 instance dynamic test could not save material instance");

    Require(manager.DiscoverMountedAssets() == 2U, "MAT-40 instance dynamic test did not discover parent and instance");
    parentMeta = manager.Registry().FindByPath("/Game/Parent.kbmat");
    const kb::assets::AssetMetadata* instanceMeta = manager.Registry().FindByPath("/Game/Parent_Inst.kbmatinst");
    Require(parentMeta != nullptr && instanceMeta != nullptr, "MAT-40 instance dynamic test did not find discovered materials");
    Require(instance.parentMaterialAssetId == parentMeta->id, "MAT-40 instance dynamic test parent id must match discovered parent metadata");

    RuntimeMaterialResolver resolver;
    const ResolvedRuntimeMaterialAsset resolvedParent = resolver.ResolveAsset(manager, *parentMeta);
    const ResolvedRuntimeMaterialAsset resolvedInstance = resolver.ResolveAsset(manager, *instanceMeta);
    Require(resolvedParent.resolved && resolvedParent.renderMode == RuntimeMaterialRenderMode::GpuMaterialGraph,
        "MAT-40 parent graph material must resolve to GPU material graph");
    Require(resolvedInstance.resolved && resolvedInstance.renderMode == RuntimeMaterialRenderMode::GpuMaterialGraph,
        "MAT-40 material instance override must resolve to GPU material graph");
    Require(resolvedParent.material.graphProgram.active && resolvedInstance.material.graphProgram.active,
        "MAT-40 parent and instance must both bind active graph programs");
    Require(resolvedParent.material.graphProgram.graphSourceHash == resolvedInstance.material.graphProgram.graphSourceHash &&
            resolvedParent.material.graphProgram.materialTypeId == resolvedInstance.material.graphProgram.materialTypeId &&
            resolvedParent.material.graphProgram.materialTypeVersion == resolvedInstance.material.graphProgram.materialTypeVersion,
        "MAT-40 material instance dynamic override must keep the same graph program key");

    const auto findTint = [](const RenderMaterialGraphProgramBinding& binding) -> const RenderMaterialGraphUniformBinding* {
        for (const RenderMaterialGraphUniformBinding& uniform : binding.uniforms) {
            if (uniform.stableId == "tint") {
                return &uniform;
            }
        }
        return nullptr;
    };
    const RenderMaterialGraphUniformBinding* parentTint = findTint(resolvedParent.material.graphProgram);
    const RenderMaterialGraphUniformBinding* instanceTint = findTint(resolvedInstance.material.graphProgram);
    Require(parentTint != nullptr && instanceTint != nullptr, "MAT-40 graph program binding must expose the tint uniform");
    Require(NearlyEqual(parentTint->value[0], 1.0F) && NearlyEqual(parentTint->value[1], 0.0F),
        "MAT-40 parent graph uniform must carry the parent tint value");
    Require(NearlyEqual(instanceTint->value[0], 0.0F) && NearlyEqual(instanceTint->value[1], 1.0F),
        "MAT-40 material instance override must update the tint uniform without changing program key");

    RenderMaterialInstanceAssetData invalid = instance;
    invalid.overrides.graphParameterValues.front().stableId = "missingTint";
    Require(RenderMaterialInstanceAssetWriter::Save(root / "Invalid_Inst.kbmatinst", invalid), "MAT-40 instance dynamic test could not save invalid instance");
    static_cast<void>(manager.DiscoverMountedAssets());
    const kb::assets::AssetMetadata* invalidMeta = manager.Registry().FindByPath("/Game/Invalid_Inst.kbmatinst");
    Require(invalidMeta != nullptr, "MAT-40 instance dynamic test did not discover invalid instance");
    const ResolvedRuntimeMaterialAsset invalidResolved = resolver.ResolveAsset(manager, *invalidMeta);
    Require(invalidResolved.status == RuntimeMaterialResolveStatus::ErrorMaterial &&
            std::ranges::any_of(invalidResolved.diagnostics, [](const RuntimeMaterialResolveDiagnostic& diagnostic) {
                return diagnostic.kind == RuntimeMaterialResolveDiagnosticKind::MaterialInstanceValidationFailed &&
                    diagnostic.message.find("unknown_override_parameter") != std::string::npos;
            }),
        "MAT-40 invalid material instance override must fail with typed validation diagnostics");

    std::filesystem::remove_all(root, error);
}

// LIB-140: proves RuntimeMaterialResolver::ResolveAssetWithParameterOverrides - the isolated
// resolution path RuntimeMaterialResourceEnsurer uses for a runtime kb::scene::MaterialInstance
// handle - actually changes the resolved material's roughness when a Scalar override is
// supplied (not just accepted-but-ignored), keeps a no-override resolve at the parent's own
// baked default, honestly falls back to the default material for an unresolvable parent, and
// rejects a RenderMaterialInstance parent as out of the LIB-140 v1 scope cut (error material).
void RunRuntimeMaterialResolverResolvesInstanceParameterOverridesTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_runtime_material_instance_scalar_override";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "MAT-140 instance override test could not create temp root");

    RenderMaterialAssetData parent{};
    parent.graph = MakeDefaultRenderMaterialGraphDocument();
    parent.graph.nodes.push_back(MakeGraphNode(2U, RenderMaterialGraphNodeKind::ParameterScalar, "roughnessOverride", "0.1"));
    parent.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ParameterScalar, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"));
    Require(RenderMaterialAssetWriter::Save(root / "Parent.kbmat", parent), "MAT-140 instance override test could not save parent material");

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "MAT-140 instance override test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialInstanceAssetLoader>()), "MAT-140 instance override test could not register instance loader");
    Require(manager.Mounts().Mount("Game", root), "MAT-140 instance override test could not mount asset root");
    Require(manager.DiscoverMountedAssets() == 1U, "MAT-140 instance override test did not discover parent material");
    const kb::assets::AssetMetadata* parentMeta = manager.Registry().FindByPath("/Game/Parent.kbmat");
    Require(parentMeta != nullptr, "MAT-140 instance override test did not find parent material metadata");

    RuntimeMaterialResolver resolver;

    const ResolvedRuntimeMaterialAsset withoutOverride = resolver.ResolveAssetWithParameterOverrides(manager, parentMeta->id, {});
    Require(withoutOverride.resolved && withoutOverride.status == RuntimeMaterialResolveStatus::Resolved,
        "MAT-140: parent material with no overrides must resolve cleanly");
    Require(NearlyEqual(withoutOverride.material.desc.roughnessFactor, 0.1F),
        "MAT-140: parent material with no overrides must keep its baked roughness default");

    const std::vector<RenderMaterialGraphParameterValue> overrides{
        RenderMaterialGraphParameterValue{
            .stableId = "roughnessOverride",
            .type = RenderMaterialParameterType::Scalar,
            .numbers = { 0.9F, 0.0F, 0.0F, 0.0F },
        },
    };
    const ResolvedRuntimeMaterialAsset withOverride = resolver.ResolveAssetWithParameterOverrides(manager, parentMeta->id, overrides);
    Require(withOverride.resolved && withOverride.status == RuntimeMaterialResolveStatus::Resolved,
        "MAT-140: parent material with a scalar override must resolve cleanly");
    Require(NearlyEqual(withOverride.material.desc.roughnessFactor, 0.9F),
        "MAT-140: a runtime MaterialInstance parameter override must change the resolved roughness value");

    const ResolvedRuntimeMaterialAsset missingParent = resolver.ResolveAssetWithParameterOverrides(manager, kb::assets::AssetId{ 999999U }, overrides);
    Require(missingParent.resolved && missingParent.status == RuntimeMaterialResolveStatus::DefaultMaterial,
        "MAT-140: an unresolvable parent material asset must fall back to the default material");

    RenderMaterialInstanceAssetData instanceAsset{};
    instanceAsset.parentMaterialAssetId = parentMeta->id;
    Require(RenderMaterialInstanceAssetWriter::Save(root / "Parent_Inst.kbmatinst", instanceAsset), "MAT-140 instance override test could not save a material instance parent fixture");
    Require(manager.DiscoverMountedAssets() == 2U, "MAT-140 instance override test did not discover the instance fixture");
    const kb::assets::AssetMetadata* instanceMeta = manager.Registry().FindByPath("/Game/Parent_Inst.kbmatinst");
    Require(instanceMeta != nullptr, "MAT-140 instance override test did not find the instance fixture metadata");
    const ResolvedRuntimeMaterialAsset instanceParent = resolver.ResolveAssetWithParameterOverrides(manager, instanceMeta->id, overrides);
    Require(instanceParent.resolved && instanceParent.status == RuntimeMaterialResolveStatus::ErrorMaterial,
        "MAT-140: a RenderMaterialInstance parent must be rejected as out of scope (error material), not silently accepted");

    std::filesystem::remove_all(root, error);
}

// LIB-140: end-to-end through Renderer::SubmitScene - proves a live kb::scene::MaterialInstance
// handle on a MeshRendererComponent actually drives material resolution (materialLoadedCount
// increments once for the first submit), that a SetParameterScalar call between frames is
// detected and reloads the bound material (materialReloadCount increments), and that
// resubmitting with no further change is a cache hit (no further reload).
void RunRendererAppliesMaterialInstanceParameterOverrideTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_material_instance_override";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "MAT-140 renderer override test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    WriteTriangleObj(meshPath);

    RenderMaterialAssetData parent{};
    parent.graph = MakeDefaultRenderMaterialGraphDocument();
    parent.graph.nodes.push_back(MakeGraphNode(2U, RenderMaterialGraphNodeKind::ParameterScalar, "roughnessOverride", "0.1"));
    parent.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ParameterScalar, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"));
    Require(RenderMaterialAssetWriter::Save(root / "Parent.kbmat", parent), "MAT-140 renderer override test could not save parent material");

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "MAT-140 renderer override test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "MAT-140 renderer override test could not register material loader");
    Require(manager.Mounts().Mount("Game", root), "MAT-140 renderer override test could not mount asset root");
    Require(manager.DiscoverMountedAssets() == 2U, "MAT-140 renderer override test did not discover mesh and material assets");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/Parent.kbmat");
    Require(meshMetadata != nullptr && materialMetadata != nullptr, "MAT-140 renderer override test discovered wrong metadata");

    const std::uint64_t instance = scene.MaterialInstances().Create(materialMetadata->id.value);
    Require(instance != 0U, "MAT-140 renderer override test could not create a material instance");

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Instance Override Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialInstanceHandle = instance,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "MAT-140 renderer override test renderer did not initialize");
    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
    };

    Require(renderer.BeginFrame(), "MAT-140 renderer override test renderer did not begin first frame");
    Require(renderer.SubmitScene(scene, desc), "MAT-140 renderer override test renderer did not submit first frame");
    const Renderer::RuntimeSceneResourceStats afterFirstSubmit = renderer.RuntimeResourceStats();
    Require(afterFirstSubmit.materialLoadedCount == 1U, "MAT-140: the first submit must resolve and load the instance's material exactly once");
    Require(afterFirstSubmit.materialReloadCount == 0U, "MAT-140: the first submit is not a reload");
    Require(afterFirstSubmit.materialErrorCount == 0U, "MAT-140: the first submit must not report a material error");
    renderer.EndFrame();

    Require(!scene.MaterialInstances().SetParameterScalar(instance, "missingParameter", 0.9F),
        "MAT-140: renderer schema bridge must reject an unknown parameter at the API call");
    Require(!scene.MaterialInstances().SetParameterBool(instance, "roughnessOverride", true),
        "MAT-140: renderer schema bridge must reject a wrong parameter type at the API call");
    Require(scene.MaterialInstances().Parameters(instance).empty(),
        "MAT-140: rejected schema mutations must not reach scene override storage");
    Require(scene.MaterialInstances().SetParameterScalar(instance, "roughnessOverride", 0.9F),
        "MAT-140 renderer override test could not set a parameter override");
    Require(renderer.BeginFrame(), "MAT-140 renderer override test renderer did not begin second frame");
    Require(renderer.SubmitScene(scene, desc), "MAT-140 renderer override test renderer did not submit second frame");
    const Renderer::RuntimeSceneResourceStats afterOverride = renderer.RuntimeResourceStats();
    Require(afterOverride.materialReloadCount == 1U,
        "MAT-140: changing a material instance parameter must invalidate the cache and reload the bound material");
    renderer.EndFrame();

    Require(renderer.BeginFrame(), "MAT-140 renderer override test renderer did not begin third frame");
    Require(renderer.SubmitScene(scene, desc), "MAT-140 renderer override test renderer did not submit third frame");
    const Renderer::RuntimeSceneResourceStats afterStableResubmit = renderer.RuntimeResourceStats();
    // Renderer::SubmitScene(s) resets these stats to zero at the start of every call (see
    // Renderer::SubmitScenes), so "no reload this frame" is materialReloadCount == 0, not a
    // carried-over count from the previous frame's genuine reload.
    Require(afterStableResubmit.materialReloadCount == 0U,
        "MAT-140: resubmitting with no further parameter change must be a cache hit, not another reload");
    Require(afterStableResubmit.materialErrorCount == 0U, "MAT-140: a stable resubmit must not report a material error");
    renderer.EndFrame();

    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

// LIB-142: proves PostProcessProfileAssetLoader::SaveProfile/LoadProfile round-trips
// ScenePostProcessSettings through the real on-disk text format - representative fields
// across the flat top-level struct, the nested outputTransform, and the doubly-nested
// autoExposure, plus a non-default value for every enum.
void RunPostProcessProfileAssetSaveLoadRoundTripTest() {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "21kb_post_process_profile_round_trip.kbppfx";
    std::error_code removeError;
    std::filesystem::remove(path, removeError);

    ScenePostProcessSettings settings{};
    settings.bloomEnabled = false;
    settings.bloomStrength = 0.42F;
    settings.bloomThreshold = 1.8F;
    settings.bloomSoftKnee = 0.3F;
    settings.bloomRadiusPixels = 2.5F;
    settings.temporalAntiAliasingEnabled = false;
    settings.temporalJitterEnabled = false;
    settings.temporalHistoryBlend = 0.5F;
    settings.fxaaEnabled = true;
    settings.tonemapEnabled = false;
    settings.autoExposureMetering = ScenePostProcessSettings::AutoExposureMeteringMode::Manual;
    settings.outputTransform.exposureStops = 1.25F;
    settings.outputTransform.gamma = 2.4F;
    settings.outputTransform.tonemap = FullscreenTextureTonemapOperator::AgxApprox;
    settings.outputTransform.colorGradingLutStrength = 0.0F;
    settings.outputTransform.autoExposure.enabled = false;
    settings.outputTransform.autoExposure.meteredAverageLuminance = 0.3F;
    settings.outputTransform.autoExposure.middleGray = 0.22F;
    settings.outputTransform.autoExposure.minExposureStops = -4.0F;
    settings.outputTransform.autoExposure.maxExposureStops = 6.0F;
    settings.outputTransform.autoExposure.biasStops = 0.5F;
    settings.outputTransform.autoExposure.temporalAdaptationEnabled = false;
    settings.outputTransform.autoExposure.brightAdaptationRate = 3.0F;
    settings.outputTransform.autoExposure.darkAdaptationRate = 1.0F;

    Require(PostProcessProfileAssetLoader::SaveProfile(path, settings), "PostProcess profile asset save failed");
    const std::optional<ScenePostProcessSettings> loaded = PostProcessProfileAssetLoader::LoadProfile(path);
    Require(loaded.has_value(), "PostProcess profile asset did not load");
    Require(!loaded->bloomEnabled && NearlyEqual(loaded->bloomStrength, 0.42F) && NearlyEqual(loaded->bloomThreshold, 1.8F) &&
            NearlyEqual(loaded->bloomSoftKnee, 0.3F) && NearlyEqual(loaded->bloomRadiusPixels, 2.5F),
        "PostProcess profile asset round trip lost bloom fields");
    Require(!loaded->temporalAntiAliasingEnabled && !loaded->temporalJitterEnabled && NearlyEqual(loaded->temporalHistoryBlend, 0.5F) && loaded->fxaaEnabled,
        "PostProcess profile asset round trip lost anti-aliasing fields");
    Require(!loaded->tonemapEnabled && loaded->autoExposureMetering == ScenePostProcessSettings::AutoExposureMeteringMode::Manual,
        "PostProcess profile asset round trip lost tonemapEnabled/autoExposureMetering");
    Require(NearlyEqual(loaded->outputTransform.exposureStops, 1.25F) && NearlyEqual(loaded->outputTransform.gamma, 2.4F) &&
            loaded->outputTransform.tonemap == FullscreenTextureTonemapOperator::AgxApprox && NearlyEqual(loaded->outputTransform.colorGradingLutStrength, 0.0F),
        "PostProcess profile asset round trip lost outputTransform fields");
    Require(!loaded->outputTransform.autoExposure.enabled && NearlyEqual(loaded->outputTransform.autoExposure.meteredAverageLuminance, 0.3F) &&
            NearlyEqual(loaded->outputTransform.autoExposure.middleGray, 0.22F) && NearlyEqual(loaded->outputTransform.autoExposure.minExposureStops, -4.0F) &&
            NearlyEqual(loaded->outputTransform.autoExposure.maxExposureStops, 6.0F) && NearlyEqual(loaded->outputTransform.autoExposure.biasStops, 0.5F) &&
            !loaded->outputTransform.autoExposure.temporalAdaptationEnabled && NearlyEqual(loaded->outputTransform.autoExposure.brightAdaptationRate, 3.0F) &&
            NearlyEqual(loaded->outputTransform.autoExposure.darkAdaptationRate, 1.0F),
        "PostProcess profile asset round trip lost outputTransform.autoExposure fields");

    const std::optional<ScenePostProcessSettings> missing = PostProcessProfileAssetLoader::LoadProfile(
        std::filesystem::temp_directory_path() / "21kb_post_process_profile_does_not_exist.kbppfx");
    Require(!missing.has_value(), "PostProcess profile asset load must honestly fail for a nonexistent file, not return defaults");

    ScenePostProcessSettings unsupportedLut = settings;
    unsupportedLut.outputTransform.colorGradingLutStrength = 0.75F;
    Require(!PostProcessProfileAssetLoader::SaveProfile(path, unsupportedLut),
        "PostProcess profile save must reject a non-neutral LUT strength while no custom LUT asset path exists");

    std::filesystem::remove(path, removeError);
}

// LIB-142: end-to-end through Renderer::SubmitScene - proves a scene's asset-based active
// PostProcessProfile actually drives the resolved post-process settings when the caller
// supplies no explicit per-submit override, and that an explicit per-submit override still
// wins over the scene's profile (the override is purely additive, not a new precedence rule
// - see Renderer.cpp's ResolveScenePostProcessProfile doc comment).
void RunRendererAppliesScenePostProcessProfileTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_post_process_profile";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Post process profile renderer test could not create temp root");

    ScenePostProcessSettings profileSettings{};
    profileSettings.bloomEnabled = true;
    profileSettings.bloomStrength = 0.77F;
    profileSettings.fxaaEnabled = true;
    profileSettings.tonemapEnabled = false;
    Require(PostProcessProfileAssetLoader::SaveProfile(root / "Profile.kbppfx", profileSettings), "Post process profile renderer test could not save profile asset");

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<PostProcessProfileAssetLoader>()), "Post process profile renderer test could not register loader");
    Require(manager.Mounts().Mount("Game", root), "Post process profile renderer test could not mount asset root");
    Require(manager.DiscoverMountedAssets() == 1U, "Post process profile renderer test did not discover the profile asset");
    const kb::assets::AssetMetadata* profileMetadata = manager.Registry().FindByPath("/Game/Profile.kbppfx");
    Require(profileMetadata != nullptr, "Post process profile renderer test did not find profile metadata");

    kb::scene::ScenePostProcessAccess::SetActiveProfile(scene, profileMetadata->id.value);

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Post process profile renderer test renderer did not initialize");
    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
    };

    Require(renderer.BeginFrame(), "Post process profile renderer test renderer did not begin first frame");
    Require(renderer.SubmitScene(scene, desc), "Post process profile renderer test renderer did not submit first frame");
    const std::optional<ScenePostProcessSettings> withProfile = renderer.LastResolvedPostProcessSettings();
    Require(withProfile.has_value() && NearlyEqual(withProfile->bloomStrength, 0.77F) && withProfile->fxaaEnabled && !withProfile->tonemapEnabled,
        "Renderer did not apply the scene's active PostProcessProfile when no explicit submit override was supplied");
    renderer.EndFrame();

    RenderSceneSubmitDesc overriddenDesc = desc;
    overriddenDesc.postProcessSettings = ScenePostProcessSettings{
        .bloomEnabled = true,
        .bloomStrength = 0.11F,
        .fxaaEnabled = false,
        .tonemapEnabled = true,
    };
    Require(renderer.BeginFrame(), "Post process profile renderer test renderer did not begin second frame");
    Require(renderer.SubmitScene(scene, overriddenDesc), "Post process profile renderer test renderer did not submit second frame");
    const std::optional<ScenePostProcessSettings> withExplicitOverride = renderer.LastResolvedPostProcessSettings();
    Require(withExplicitOverride.has_value() && NearlyEqual(withExplicitOverride->bloomStrength, 0.11F) && !withExplicitOverride->fxaaEnabled && withExplicitOverride->tonemapEnabled,
        "An explicit per-submit postProcessSettings override must still win over the scene's active PostProcessProfile");
    renderer.EndFrame();

    kb::scene::ScenePostProcessAccess::SetActiveProfile(scene, 0U);
    Require(renderer.BeginFrame(), "Post process profile renderer test renderer did not begin third frame");
    Require(renderer.SubmitScene(scene, desc), "Post process profile renderer test renderer did not submit third frame");
    Require(!renderer.LastResolvedPostProcessSettings().has_value(),
        "With neither an explicit submit override nor an active scene profile, the renderer must honestly resolve to no override, not stale state from a previous frame");
    renderer.EndFrame();

    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunRuntimeMaterialInstanceStaticBaseOverrideChainTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_runtime_material_instance_static_base_chain";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "MAT-47 static/base chain test could not create temp root");

    RenderMaterialAssetData parent{};
    parent.graph = MakeDefaultRenderMaterialGraphDocument();
    parent.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::StaticBoolParameter,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "useRed", .displayName = "Use Red", .defaultValueHint = "true" },
    });
    parent.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 0 0 1" },
    });
    parent.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 4U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0 0 1 1" },
    });
    parent.graph.nodes.push_back(RenderMaterialGraphNode{ .id = 5U, .kind = RenderMaterialGraphNodeKind::StaticSwitch });
    parent.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::StaticBoolParameter, 2U, "value", RenderMaterialGraphNodeKind::StaticSwitch, 5U, "value"));
    parent.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba", RenderMaterialGraphNodeKind::StaticSwitch, 5U, "true"));
    parent.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 4U, "rgba", RenderMaterialGraphNodeKind::StaticSwitch, 5U, "false"));
    parent.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::StaticSwitch, 5U, "result", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    Require(RenderMaterialAssetWriter::Save(root / "Parent.kbmat", parent), "MAT-47 static/base chain test could not save parent material");

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "MAT-47 static/base chain test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialInstanceAssetLoader>()), "MAT-47 static/base chain test could not register instance loader");
    Require(manager.Mounts().Mount("Game", root), "MAT-47 static/base chain test could not mount asset root");
    Require(manager.DiscoverMountedAssets() == 1U, "MAT-47 static/base chain test did not discover parent material");
    const kb::assets::AssetMetadata* parentMeta = manager.Registry().FindByPath("/Game/Parent.kbmat");
    Require(parentMeta != nullptr && parentMeta->type == "RenderMaterial", "MAT-47 static/base chain test did not find parent material metadata");

    RenderMaterialInstanceAssetData mid{};
    mid.parentMaterialAssetId = parentMeta->id;
    mid.staticParameterOverrides.push_back(RenderMaterialInstanceStaticParameterOverride{
        .stableId = "useRed",
        .nodeKind = RenderMaterialGraphNodeKind::StaticBoolParameter,
        .value = "false",
    });
    mid.basePropertyOverrides.overrideBlendMode = true;
    mid.basePropertyOverrides.blendMode = RenderMaterialGraphBlendMode::Additive;
    mid.basePropertyOverrides.overrideShadingModel = true;
    mid.basePropertyOverrides.shadingModel = RenderMaterialShadingModel::Unlit;
    mid.basePropertyOverrides.overrideTwoSided = true;
    mid.basePropertyOverrides.twoSided = true;
    mid.basePropertyOverrides.overrideOpacityMaskClip = true;
    mid.basePropertyOverrides.opacityMaskClip = 0.33F;
    Require(RenderMaterialInstanceAssetWriter::Save(root / "Mid.kbmatinst", mid), "MAT-47 static/base chain test could not save mid instance");
    Require(manager.DiscoverMountedAssets() == 2U, "MAT-47 static/base chain test did not discover mid instance");
    const kb::assets::AssetMetadata* midMeta = manager.Registry().FindByPath("/Game/Mid.kbmatinst");
    Require(midMeta != nullptr && midMeta->type == "RenderMaterialInstance", "MAT-47 static/base chain test did not find mid metadata");

    RenderMaterialInstanceAssetData child{};
    child.parentMaterialAssetId = midMeta->id;
    Require(RenderMaterialInstanceAssetWriter::Save(root / "Child.kbmatinst", child), "MAT-47 static/base chain test could not save child instance");
    Require(manager.DiscoverMountedAssets() == 3U, "MAT-47 static/base chain test did not discover child instance");
    parentMeta = manager.Registry().FindByPath("/Game/Parent.kbmat");
    midMeta = manager.Registry().FindByPath("/Game/Mid.kbmatinst");
    const kb::assets::AssetMetadata* childMeta = manager.Registry().FindByPath("/Game/Child.kbmatinst");
    Require(parentMeta != nullptr && midMeta != nullptr && childMeta != nullptr, "MAT-47 static/base chain test lost discovered metadata");

    RuntimeMaterialResolver resolver;
    const ResolvedRuntimeMaterialAsset resolvedParent = resolver.ResolveAsset(manager, *parentMeta);
    const ResolvedRuntimeMaterialAsset resolvedMid = resolver.ResolveAsset(manager, *midMeta);
    const ResolvedRuntimeMaterialAsset resolvedChild = resolver.ResolveAsset(manager, *childMeta);
    Require(resolvedParent.resolved && resolvedParent.renderMode == RuntimeMaterialRenderMode::GpuMaterialGraph,
        "MAT-47 parent graph material must resolve to a GPU graph");
    Require(resolvedMid.resolved, "MAT-47 mid instance static override did not resolve");
    Require(resolvedMid.material.graphProgram.active, "MAT-47 mid instance static override did not bind an active graph program");
    Require(resolvedMid.renderMode == RuntimeMaterialRenderMode::GpuMaterialGraph,
        "MAT-47 mid instance static override did not report GPU graph render mode");
    Require(resolvedChild.resolved, "MAT-47 child instance did not resolve");
    Require(resolvedChild.material.graphProgram.active, "MAT-47 child instance did not bind an inherited graph program");
    Require(resolvedChild.renderMode == RuntimeMaterialRenderMode::GpuMaterialGraph,
        "MAT-47 child instance did not report inherited GPU graph render mode");
    Require(resolvedParent.material.graphProgram.graphSourceHash != resolvedMid.material.graphProgram.graphSourceHash,
        "MAT-47 static override must produce a different runtime graph variant key");
    Require(resolvedMid.material.graphProgram.graphSourceHash == resolvedChild.material.graphProgram.graphSourceHash,
        "MAT-47 child instance did not inherit the static variant key from its parent instance");
    Require(resolvedChild.material.graphProgram.alphaMode == RenderMaterialAlphaMode::Blend &&
            resolvedChild.material.graphProgram.translucencyBlend == RenderMaterialTranslucencyBlend::Additive,
        "MAT-47 blendMode override did not propagate to the child graph render state");
    Require(resolvedChild.material.desc.doubleSided && NearlyEqual(resolvedChild.material.desc.alphaCutoff, 0.33F),
        "MAT-47 base property overrides did not propagate through the instance chain");
    Require(resolvedChild.contentHash != childMeta->contentHash,
        "MAT-47 child runtime content hash must include parent instance/material content");

    const std::uint64_t childRuntimeHashBeforeParentReload =
        RuntimeMaterialResolver::MaterialRuntimeContentHash(manager, *childMeta);
    parent.desc.roughnessFactor = 0.42F;
    Require(RenderMaterialAssetWriter::Save(root / "Parent.kbmat", parent), "MAT-47 static/base chain test could not rewrite parent material");
    Require(manager.DiscoverMountedAssets() == 3U, "MAT-47 static/base chain test did not rediscover parent material reload");
    childMeta = manager.Registry().FindByPath("/Game/Child.kbmatinst");
    Require(childMeta != nullptr && childMeta->type == "RenderMaterialInstance", "MAT-47 static/base chain test lost child metadata after parent reload");
    const std::uint64_t childRuntimeHashAfterParentReload =
        RuntimeMaterialResolver::MaterialRuntimeContentHash(manager, *childMeta);
    Require(childRuntimeHashAfterParentReload != childRuntimeHashBeforeParentReload,
        "MAT-47 parent reload must invalidate the child instance runtime material hash through the chain");

    std::filesystem::remove_all(root, error);
}

void RunRendererBindsGraphMaterialGpuProgramTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_graph_gpu_program";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Graph GPU program submit test could not create temp root");

    WriteTriangleObj(root / "triangle.obj");

    RenderMaterialAssetData gpuMaterial{};
    gpuMaterial.graph = MakeDefaultRenderMaterialGraphDocument();
    gpuMaterial.graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .positionX = -160, .positionY = 64 });
    gpuMaterial.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    Require(RenderMaterialAssetWriter::Save(root / "graph.kbmat", gpuMaterial), "Graph GPU program submit test could not save graph material");

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Graph GPU program submit test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Graph GPU program submit test could not register material loader");
    Require(manager.Mounts().Mount("Game", root), "Graph GPU program submit test could not mount asset root");
    Require(manager.DiscoverMountedAssets() == 2U, "Graph GPU program submit test did not discover mesh and material");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/graph.kbmat");
    Require(meshMetadata != nullptr && materialMetadata != nullptr, "Graph GPU program submit test did not discover both assets");

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Graph Material Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialAssetId = materialMetadata->id.value,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Graph GPU program submit test renderer did not initialize");
    Require(renderer.BeginFrame(), "Graph GPU program submit test renderer did not begin frame");
    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
    };
    Require(renderer.SubmitScene(scene, desc), "Graph GPU program submit test renderer did not submit scene");

    const Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
    // Finding 3: this graph material is saved but never cooked, so no GPU program binary exists.
    // graphMaterialGpuCount/CpuFallback report the ACTUAL draw outcome (not the resolve-time renderMode
    // intent), so a material that renders the builtin flatten because its cooked binary is missing must
    // be counted as a CPU fallback, NOT as a live GPU graph material. (MAT-31 covers the cooked GPU path.)
    Require(runtimeStats.graphMaterialGpuCount == 0U, "MAT-26: A graph material with no cooked GPU binary must NOT be counted as a GPU material (it renders the builtin flatten)");
    Require(runtimeStats.graphMaterialCpuFallbackCount == 1U, "MAT-26: A graph material that falls back to the builtin flatten at draw must count as exactly one CPU fallback");
    Require(runtimeStats.materialErrorCount == 0U, "MAT-26: A valid but uncooked graph material must still resolve (not an error material)");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

#if defined(KB_TEST_GRAPH_SHADERC_PATH)
// MAT-31: the public Renderer::SetGraphShaderCacheRoot must reach the mesh pass resources so a
// cooked graph binary is loaded as the bound program (not the builtin fallback). This proves the
// full forwarding chain Renderer -> SceneRenderer -> SceneMeshSubmitter -> SceneMeshPassResources.
void RunRendererPublicGraphShaderCacheRootBindsCookedProgramTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_public_cache_root";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "MAT-31 public cache root test could not create temp root");

    WriteTriangleObj(root / "triangle.obj");

    RenderMaterialAssetData gpuMaterial{};
    gpuMaterial.graph = MakeDefaultRenderMaterialGraphDocument();
    gpuMaterial.graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .positionX = -160, .positionY = 64 });
    gpuMaterial.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    RenderMaterialGraphNode transitionUniform{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ParameterScalar,
        .positionX = -160,
        .positionY = 220,
    };
    transitionUniform.parameter.stableId = "transition.roughness";
    transitionUniform.parameter.defaultValueHint = "0.25";
    gpuMaterial.graph.nodes.push_back(std::move(transitionUniform));
    gpuMaterial.graph.links.push_back(MakeGraphLink(
        RenderMaterialGraphNodeKind::ParameterScalar,
        3U,
        "value",
        RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "roughness"));
    Require(RenderMaterialAssetWriter::Save(root / "graph.kbmat", gpuMaterial), "MAT-31 public cache root test could not save graph material");

    // Cook the graph's BaseOpaque binary for the headless Noop renderer (dxbc directory) into a
    // dedicated cache root that we will hand to the renderer through its public setter.
    const std::filesystem::path cacheRoot = root / "graph_shaders";
    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(gpuMaterial.graph, RenderMaterialGraphBuildContext{ .assetId = 0x3100U });
    Require(compiled.Succeeded(), "MAT-31 public cache root test graph must compile");
    RenderMaterialGraphShaderArtifactRequest request{};
    request.shadercPath = KB_TEST_GRAPH_SHADERC_PATH;
    request.varyingDefPath = KB_TEST_GRAPH_SHADER_VARYING_DEF;
    request.includeDirs = { KB_TEST_GRAPH_SHADER_INCLUDE_DIR, KB_TEST_GRAPH_BGFX_SHADER_INCLUDE_DIR };
    request.cacheRoot = cacheRoot.generic_string();
    request.pass = "BaseOpaque";
    request.shaderPlatform = kb::assets::bake::ShaderBakePlatform::Windows;
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };
    Require(CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, request).Succeeded(),
        "MAT-31 public cache root test must cook a DXBC graph binary");

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "MAT-31 public cache root test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "MAT-31 public cache root test could not register material loader");
    Require(manager.Mounts().Mount("Game", root), "MAT-31 public cache root test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 2U, "MAT-31 public cache root test did not discover mesh and material");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/graph.kbmat");
    Require(meshMetadata != nullptr && materialMetadata != nullptr, "MAT-31 public cache root test did not discover both assets");

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Cooked Graph Material Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialAssetId = materialMetadata->id.value,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    // Set the cache root BEFORE Initialize to exercise the deferred-apply path added in MAT-31.
    renderer.SetGraphShaderCacheRoot(cacheRoot.generic_string());
    Require(renderer.GraphShaderCacheRoot() == cacheRoot.generic_string(),
        "MAT-31: Renderer must retain the graph shader cache root through its public setter");
    Require(renderer.Initialize(surface, &config), "MAT-31 public cache root test renderer did not initialize");
    Require(renderer.BeginFrame(), "MAT-31 public cache root test renderer did not begin frame");
    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
    };
    Require(renderer.SubmitScene(scene, desc), "MAT-31 public cache root test renderer did not submit scene");

    const Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
    Require(runtimeStats.graphMaterialGpuCount == 1U, "MAT-31: A cooked graph material must resolve to the GPU graph path");
    const MaterialProgramRegistryStats programStats = renderer.MaterialProgramStats();
    Require(programStats.loads >= 1U,
        "MAT-31: Setting the cache root through the public renderer must load the cooked graph program from disk");
    Require(programStats.failures == 0U,
        "MAT-31: A present cooked graph binary must load without a program failure/fallback");
    Require(programStats.liveProgramCount >= 1U,
        "MAT-31: The cooked graph program must be retained live in the material program registry");

    renderer.EndFrame();

    // Regression for Material Editor -> Scene View: the graph frame has a reflected, graph-only
    // numeric uniform in flight. Render an empty frame, return to the graph during the grace
    // period, then leave it long enough to retire resources. Before the deferred-uniform fix,
    // EndFrame destroyed the renderer-side uniform slot immediately and the render thread crashed
    // in RendererContextD3D12::updateUniform while consuming the previous frame.
    kb::scene::Scene emptyScene;
    const auto submitTransitionFrame = [&](const kb::scene::Scene& submittedScene, const char* failure) {
        Require(renderer.BeginFrame(), failure);
        Require(renderer.SubmitScene(submittedScene, desc), failure);
        renderer.EndFrame();
    };
    submitTransitionFrame(emptyScene, "Material graph transition test could not submit the first Scene View frame");
    submitTransitionFrame(scene, "Material graph transition test could not reactivate the graph preview frame");
    submitTransitionFrame(emptyScene, "Material graph transition test could not submit retirement frame one");
    submitTransitionFrame(emptyScene, "Material graph transition test could not submit retirement frame two");
    submitTransitionFrame(emptyScene, "Material graph transition test could not submit retirement frame three");
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

// MAT-84/85: a scene that references several distinct graph materials (none of them "open" in an
// editor) must render each through its own cooked GPU program once the cache root is supplied.
void RunRendererSceneRendersMultipleCookedGraphMaterialsTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_scene_multi_graph";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "MAT-84 scene multi-graph test could not create temp root");

    WriteTriangleObj(root / "triangle.obj");
    const std::filesystem::path cacheRoot = root / "graph_shaders";

    const std::array<std::string_view, 3U> colors{ "0.9 0.1 0.1 1", "0.1 0.9 0.1 1", "0.1 0.1 0.9 1" };
    std::array<std::string, 3U> materialFiles{ "red.kbmat", "green.kbmat", "blue.kbmat" };
    for (std::size_t index = 0U; index < colors.size(); ++index) {
        RenderMaterialAssetData material{};
        material.graph = MakeDefaultRenderMaterialGraphDocument();
        material.graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .positionX = -160, .positionY = 64 });
        material.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        material.graph.nodes[1].parameter.defaultValueHint = std::string{ colors[index] };
        Require(RenderMaterialAssetWriter::Save(root / materialFiles[index], material), "MAT-84 scene multi-graph test could not save a graph material");

        const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(material.graph, RenderMaterialGraphBuildContext{ .assetId = 0x8400U + index });
        Require(compiled.Succeeded(), "MAT-84 scene multi-graph test material must compile");
        RenderMaterialGraphShaderArtifactRequest request{};
        request.shadercPath = KB_TEST_GRAPH_SHADERC_PATH;
        request.varyingDefPath = KB_TEST_GRAPH_SHADER_VARYING_DEF;
        request.includeDirs = { KB_TEST_GRAPH_SHADER_INCLUDE_DIR, KB_TEST_GRAPH_BGFX_SHADER_INCLUDE_DIR };
        request.cacheRoot = cacheRoot.generic_string();
        request.pass = "BaseOpaque";
        request.shaderPlatform = kb::assets::bake::ShaderBakePlatform::Windows;
        const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };
        Require(CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, request).Succeeded(),
            "MAT-84 scene multi-graph test must cook each graph material");
    }

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "MAT-84 scene multi-graph test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "MAT-84 scene multi-graph test could not register material loader");
    Require(manager.Mounts().Mount("Game", root), "MAT-84 scene multi-graph test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 4U, "MAT-84 scene multi-graph test did not discover mesh and materials");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    Require(meshMetadata != nullptr, "MAT-84 scene multi-graph test did not discover the mesh");

    for (std::size_t index = 0U; index < materialFiles.size(); ++index) {
        const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath(std::string{ "/Game/" } + materialFiles[index]);
        Require(materialMetadata != nullptr, "MAT-84 scene multi-graph test did not discover a graph material");
        const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
            .name = "Graph Mesh " + std::to_string(index),
            // Keep every mesh inside the identity camera's clip volume ([-1,1]) so all three actually
            // DRAW — graphMaterialGpuCount now reflects the draw-time outcome, so a mesh culled off
            // screen would (correctly) not count as a rendered GPU material.
            .transform = TransformAt((static_cast<float>(index) - 1.0F) * 0.3F, 0.0F, 0.0F),
        });
        scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
            .meshAssetId = meshMetadata->id.value,
            .materialAssetId = materialMetadata->id.value,
        });
    }

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.SetGraphShaderCacheRoot(cacheRoot.generic_string());
    Require(renderer.Initialize(surface, &config), "MAT-84 scene multi-graph test renderer did not initialize");
    Require(renderer.BeginFrame(), "MAT-84 scene multi-graph test renderer did not begin frame");
    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
    };
    Require(renderer.SubmitScene(scene, desc), "MAT-84 scene multi-graph test renderer did not submit scene");

    const Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
    Require(runtimeStats.graphMaterialGpuCount == 3U,
        "MAT-84: A scene with three distinct graph materials must bind three GPU graph programs without opening them");
    Require(runtimeStats.graphMaterialCpuFallbackCount == 0U,
        "MAT-84: Cooked scene graph materials must not fall back to CPU flattening");
    const MaterialProgramRegistryStats programStats = renderer.MaterialProgramStats();
    Require(programStats.liveProgramCount >= 3U,
        "MAT-85: Three distinct cooked graph materials must retain three distinct live programs");
    Require(programStats.failures == 0U,
        "MAT-85: Present cooked scene graph binaries must all load without a program failure");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}
#endif

// An authored UI mistake - two layout components on one widget, reachable from the Inspector
// and from a single Lua property write - used to abort SubmitSceneToViewport before any 3D
// pass ran, so the shipped game rendered a black screen with no attribution. The UI is one
// layer of the frame: its refusal must cost the UI and nothing else, and must arrive as a
// diagnostic that names the widget. Real headless Noop submit, real scene, no GPU readback.
void RunRendererKeepsSceneWhenUIFrameRefusesTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_ui_refusal";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "UI refusal test could not create temp root");
    const std::filesystem::path meshPath = root / "triangle.obj";
    WriteTriangleObj(meshPath);

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "UI refusal test could not register mesh loader");
    Require(manager.Mounts().Mount("Game", root), "UI refusal test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 1U, "UI refusal test did not discover the mesh asset");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    Require(meshMetadata != nullptr, "UI refusal test discovered no mesh metadata");

    const kb::scene::SceneEntity meshEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Gameplay Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(meshEntity, kb::scene::MeshRendererComponent{ .meshAssetId = meshMetadata->id.value });

    // A canvas that lays out cleanly, plus one child broken the way an author breaks it.
    kb::scene::SceneUIComponents ui = scene.Components().UI();
    const kb::scene::SceneEntity canvas = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Canvas" });
    ui.Set(canvas, kb::scene::UIRectTransform{ .anchorMax = { 1.0F, 1.0F }, .offsetMax = {} });
    ui.Set(canvas, kb::scene::UICanvas{});
    const kb::scene::SceneEntity widget = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Broken Panel" });
    Require(scene.Hierarchy().SetParent(widget, canvas), "UI refusal test could not parent the widget under the canvas");
    ui.Set(widget, kb::scene::UIRectTransform{});

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "UI refusal test renderer did not initialize");
    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
    };

    // Baseline: the sound scene submits and reports no UI refusal.
    Require(renderer.BeginFrame(), "UI refusal test renderer did not begin the baseline frame");
    Require(renderer.SubmitScene(scene, desc), "A scene with a sound canvas must submit");
    renderer.EndFrame();
    for (const SceneRenderDiagnosticEvent& event : renderer.LastSceneDiagnostics().events) {
        Require(event.kind != SceneRenderDiagnosticKind::UIFrameRefused, "A sound canvas must not report a UI refusal");
    }

    // Break it: one widget now carries two layout components.
    ui.Set(widget, kb::scene::UIHorizontalLayout{});
    ui.Set(widget, kb::scene::UIVerticalLayout{});
    kb::scene::SceneUIFrame refusedFrame;
    Require(!kb::scene::SceneUIQueries{ scene }.BuildFrame(64.0F, 64.0F, refusedFrame),
        "UI refusal test fixture must actually make the frame builder refuse");

    Require(renderer.BeginFrame(), "UI refusal test renderer did not begin the refused frame");
    Require(renderer.SubmitScene(scene, desc),
        "A refused UI frame must not abort the scene submit - losing the HUD may not cost the whole image");
    renderer.EndFrame();

    // The 3D half of the frame still ran: visibility feedback is published after the UI step.
    Require(kb::scene::SceneRenderFeedback::HasFrame(scene),
        "The scene must still be submitted past the UI step when the UI frame is refused");
    Require(kb::scene::SceneRenderFeedback::IsVisible(scene, meshEntity),
        "The gameplay mesh must still be rendered when the UI frame is refused");

    const SceneRenderDiagnosticEvent* refusal = nullptr;
    for (const SceneRenderDiagnosticEvent& event : renderer.LastSceneDiagnostics().events) {
        if (event.kind == SceneRenderDiagnosticKind::UIFrameRefused) {
            refusal = &event;
        }
    }
    Require(refusal != nullptr, "A refused UI frame must publish a UIFrameRefused diagnostic");
    Require(refusal->severity == SceneRenderDiagnosticSeverity::Error, "A refused UI frame must be reported as an error");
    Require(refusal->entityId == widget.Id(), "A UI refusal diagnostic must name the widget that caused it");
    Require(refusedFrame.refusal.reason != nullptr &&
            std::string_view{refusedFrame.refusal.reason}.find("layout") != std::string_view::npos,
        "A UI refusal must carry the reason the widget could not be laid out");

    std::filesystem::remove_all(root, error);
}

// A label whose font has not loaded - the normal state for the first frames after a level
// load - used to make ScreenUIRenderer::Submit return false, which took every rectangle,
// image and border in the UI with it and then failed the whole scene submit. The label must
// lose its text and nothing else, and the drop must be published by entity.
void RunRendererKeepsUIWhenTextCannotBePreparedTest() {
    kb::scene::Scene scene;
    kb::scene::SceneUIComponents ui = scene.Components().UI();
    const kb::scene::SceneEntity canvas = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Canvas" });
    ui.Set(canvas, kb::scene::UIRectTransform{ .anchorMax = { 1.0F, 1.0F }, .offsetMax = {} });
    ui.Set(canvas, kb::scene::UICanvas{});

    const kb::scene::SceneEntity panel = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Panel" });
    Require(scene.Hierarchy().SetParent(panel, canvas), "UI text drop test could not parent the panel");
    ui.Set(panel, kb::scene::UIRectTransform{ .offsetMax = { 200.0F, 80.0F } });
    ui.Set(panel, kb::scene::UIBorder{ .backgroundColor = { 0.0F, 1.0F, 0.0F, 1.0F } });

    const kb::scene::SceneEntity label = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Label" });
    Require(scene.Hierarchy().SetParent(label, panel), "UI text drop test could not parent the label");
    ui.Set(label, kb::scene::UIRectTransform{ .offsetMax = { 180.0F, 40.0F } });
    kb::scene::UIText text{ .fontAssetId = 424242U };
    Require(kb::scene::SetUITextContent(text, "Play"), "UI text drop test could not author the label content");
    ui.Set(label, text);

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "UI text drop test renderer did not initialize");
    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
    };

    kb::scene::SceneUIFrame frame;
    Require(kb::scene::SceneUIQueries{ scene }.BuildFrame(64.0F, 64.0F, frame),
        "UI text drop test fixture must lay out - the label is authored, only its font is missing");
    Require(frame.elements.size() >= 3U, "UI text drop test fixture must produce a canvas, a panel and a label");

    Require(renderer.BeginFrame(), "UI text drop test renderer did not begin the frame");
    Require(renderer.SubmitScene(scene, desc),
        "A label whose font is unavailable must not fail the scene submit - it may only lose its own text");
    renderer.EndFrame();

    const SceneRenderDiagnosticEvent* dropped = nullptr;
    for (const SceneRenderDiagnosticEvent& event : renderer.LastSceneDiagnostics().events) {
        if (event.kind == SceneRenderDiagnosticKind::UITextUnavailable) {
            dropped = &event;
        }
    }
    Require(dropped != nullptr, "Text the UI renderer had to drop must publish a UITextUnavailable diagnostic");
    Require(dropped->entityId == label.Id(), "A dropped-text diagnostic must name the label that lost its text");
}

// LIB-144: the end-to-end proof that a real Renderer::SubmitScene publishes the CPU-side
// per-entity visibility/bounds feedback frame into the scene (SceneRenderFeedback) - real
// mesh asset on disk, real headless Noop submit, real bounds resolved from the mesh
// resource, real frustum cull against the camera the submit rendered with, zero GPU
// readback anywhere. IdentityCamera()'s identity view*projection extracts the NDC unit cube
// as the frustum, so |x|,|y|,|z| <= 1 is inside.
void RunRendererPublishesSceneVisibilityFeedbackTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("21kb_renderer_visibility_feedback_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;
    std::filesystem::create_directories(root, error);
    Require(!error, "Visibility feedback test could not create temp root");
    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path wideMeshPath = root / "wide.obj";
    WriteBoundsTriangleObj(meshPath);
    {
        std::ofstream wide{wideMeshPath, std::ios::trunc};
        Require(wide.is_open(), "Visibility feedback test could not write its second mesh");
        wide << "v -0.5 -0.1 -0.1\n"
             << "v 0.5 -0.1 0.1\n"
             << "v 0.0 0.1 0.0\n"
             << "vt 0 0\nvt 1 0\nvt 0.5 1\n"
             << "vn 0 0 1\n"
             << "f 1/1/1 2/2/1 3/3/1\n";
    }

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Visibility feedback test could not register mesh loader");
    Require(manager.Mounts().Mount("Game", root), "Visibility feedback test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 2U, "Visibility feedback test did not discover both mesh assets");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* wideMeshMetadata = manager.Registry().FindByPath("/Game/wide.obj");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh" &&
            wideMeshMetadata != nullptr && wideMeshMetadata->type == "RenderMesh",
        "Visibility feedback test discovered wrong mesh metadata");
    const std::uint64_t meshAssetId = meshMetadata->id.value;
    const std::uint64_t wideMeshAssetId = wideMeshMetadata->id.value;

    const kb::scene::SceneEntity onScreenEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "On Screen Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(onScreenEntity, kb::scene::MeshRendererComponent{ .meshAssetId = meshAssetId });
    const kb::scene::SceneEntity wideEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Wide Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(wideEntity, kb::scene::MeshRendererComponent{ .meshAssetId = wideMeshAssetId });
    const kb::scene::SceneEntity offScreenEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Off Screen Mesh",
        .transform = TransformAt(100.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(offScreenEntity, kb::scene::MeshRendererComponent{ .meshAssetId = meshAssetId });
    kb::scene::TransformComponent rotatedTransform = TransformAt(0.0F, 0.0F, 0.0F);
    rotatedTransform.localRotation = {0.0F, 0.0F, 0.258819045F, 0.965925826F};
    rotatedTransform.worldRotation = rotatedTransform.localRotation;
    rotatedTransform.localScale = {2.0F, 1.0F, 1.0F};
    rotatedTransform.worldScale = rotatedTransform.localScale;
    const kb::scene::SceneEntity rotatedEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Rotated Mesh",
        .transform = rotatedTransform,
    });
    scene.Components().MeshRenderers().Set(rotatedEntity, kb::scene::MeshRendererComponent{ .meshAssetId = meshAssetId });
    const kb::scene::SceneEntity hiddenEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Hidden Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(hiddenEntity, kb::scene::MeshRendererComponent{ .meshAssetId = meshAssetId });
    scene.Components().Visibility().Set(hiddenEntity, kb::scene::VisibilityComponent{ .visible = false });
    const kb::scene::SceneEntity meshlessEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "No Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });

    Require(!kb::scene::SceneRenderFeedback::HasFrame(scene), "A never-submitted scene must not report a published visibility frame");

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Visibility feedback test renderer did not initialize");
    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
    };

    Require(renderer.BeginFrame(), "Visibility feedback test renderer did not begin first frame");
    Require(renderer.SubmitScene(scene, desc), "Visibility feedback test renderer did not submit first frame");
    renderer.EndFrame();

    Require(kb::scene::SceneRenderFeedback::HasFrame(scene), "SubmitScene must publish a visibility feedback frame into the scene");
    Require(kb::scene::SceneRenderFeedback::PublishCount(scene) == 1U, "The first submit must publish exactly one visibility frame");
    Require(kb::scene::SceneRenderFeedback::IsVisible(scene, onScreenEntity), "An in-frustum, visible mesh entity must be reported visible");
    Require(!kb::scene::SceneRenderFeedback::IsVisible(scene, offScreenEntity), "A mesh entity far outside the camera frustum must be reported not visible");
    Require(!kb::scene::SceneRenderFeedback::IsVisible(scene, hiddenEntity), "A VisibilityComponent-disabled mesh entity must be reported not visible");
    Require(!kb::scene::SceneRenderFeedback::IsVisible(scene, meshlessEntity), "An entity with no MeshRenderer must have no visibility entry");

    // Bounds come from the real mesh resource (triangle.obj),
    // transformed by each entity's own model matrix - tracked even for culled/hidden
    // entities (bounds answer "where is it", not "was it drawn").
    const kb::scene::SceneRenderBounds onScreenBounds = kb::scene::SceneRenderFeedback::WorldBounds(scene, onScreenEntity);
    Require(onScreenBounds.IsValid() && onScreenBounds.radius > 0.05F && onScreenBounds.radius < 1.0F,
        "The on-screen entity's world bounds must carry the real mesh-resource bounding sphere");
    Require(std::abs(onScreenBounds.center.x) < 0.2F, "The on-screen entity's world bounds must be centered near its origin transform");
    const kb::scene::SceneRenderBounds wideBounds = kb::scene::SceneRenderFeedback::WorldBounds(scene, wideEntity);
    Require(wideBounds.IsValid() && wideBounds.halfExtents.x > onScreenBounds.halfExtents.x * 4.0F,
        "Visibility feedback reused the first mesh bounds across a different mesh asset");
    const kb::scene::SceneRenderBounds offScreenBounds = kb::scene::SceneRenderFeedback::WorldBounds(scene, offScreenEntity);
    Require(offScreenBounds.IsValid() && std::abs(offScreenBounds.center.x - 100.0F) < 0.2F,
        "The off-screen entity's world bounds must be transformed by its own model matrix");
    Require(std::abs(offScreenBounds.halfExtents.x - onScreenBounds.halfExtents.x) < 0.001F,
        "Visibility feedback did not restore the original mesh bounds after another mesh asset");
    Require(kb::scene::SceneRenderFeedback::WorldBounds(scene, hiddenEntity).IsValid(),
        "A hidden entity keeps valid bounds - bounds report placement, not draw status");
    const kb::scene::SceneRenderBounds rotatedBounds = kb::scene::SceneRenderFeedback::WorldBounds(scene, rotatedEntity);
    Require(std::abs(rotatedBounds.halfExtents.x - 0.223205F) < 0.005F &&
            std::abs(rotatedBounds.halfExtents.y - 0.186603F) < 0.005F &&
            std::abs(rotatedBounds.halfExtents.z - 0.1F) < 0.005F,
        "A rotated, nonuniformly scaled mesh must publish its world-space box extents");
    Require(!kb::scene::SceneRenderFeedback::WorldBounds(scene, meshlessEntity).IsValid(),
        "An entity with no MeshRenderer must report invalid bounds");

    // The published frustum is queryable directly (identity clip = NDC unit cube).
    Require(kb::scene::SceneRenderFeedback::TestFrustum(scene, kb::math::Vec3{}, 0.0F), "TestFrustum must accept the origin inside the identity-camera frustum");
    Require(!kb::scene::SceneRenderFeedback::TestFrustum(scene, kb::math::Vec3{ 100.0F, 0.0F, 0.0F }, 0.0F), "TestFrustum must reject a point far outside the identity-camera frustum");
    Require(kb::scene::SceneRenderFeedback::TestFrustum(scene, kb::math::Vec3{ 1.5F, 0.0F, 0.0F }, 1.0F), "TestFrustum must accept a sphere straddling the identity-camera frustum boundary");

    // Every subsequent submit republishes - the feedback tracks the latest frame, never
    // frozen first-frame state.
    Require(renderer.BeginFrame(), "Visibility feedback test renderer did not begin second frame");
    Require(renderer.SubmitScene(scene, desc), "Visibility feedback test renderer did not submit second frame");
    renderer.EndFrame();
    Require(kb::scene::SceneRenderFeedback::PublishCount(scene) == 2U, "A second submit must publish a second visibility frame");
    Require(kb::scene::SceneRenderFeedback::IsVisible(scene, onScreenEntity), "The second published frame must still track the on-screen entity");

    // LIB-145: the published frame carries the submit camera, so the screen/world
    // conversions work end-to-end. IdentityCamera + 64x64 viewport: the world origin
    // projects to the viewport center, and the center pixel's ray runs along +Z.
    const kb::scene::SceneRenderScreenPoint projected = kb::scene::SceneRenderFeedback::WorldToScreen(scene, kb::math::Vec3{});
    Require(projected.valid && projected.onScreen && std::abs(projected.screenX - 32.0F) < 0.01F && std::abs(projected.screenY - 32.0F) < 0.01F,
        "WorldToScreen must project the world origin to the identity camera's viewport center after a real submit");
    const kb::scene::SceneRenderCameraRay centerRay = kb::scene::SceneRenderFeedback::ScreenPointToRay(scene, 32.0F, 32.0F);
    Require(centerRay.valid && std::abs(centerRay.ray.direction.z - 1.0F) < 0.01F,
        "ScreenPointToRay must build a forward ray through the identity camera's viewport center after a real submit");

    // LIB-145: async screen capture through a real submit. The headless Noop backend has
    // no TEXTURE_READ_BACK/TEXTURE_BLIT caps, so the honest terminal answer is Failed -
    // delivered through the request->consume->complete channel, never a forever-Pending
    // hang and never a fake success.
    const std::filesystem::path capturePath = root / "capture.png";
    const std::uint64_t captureId = kb::scene::SceneRenderFeedback::RequestScreenCapture(scene, capturePath.string());
    Require(captureId != 0U, "Visibility feedback test could not request a screen capture");
    Require(kb::scene::SceneRenderFeedback::ScreenCaptureStatus(scene, captureId) == kb::scene::SceneScreenCaptureStatus::Pending,
        "A requested capture must report Pending before the next submit");
    Require(renderer.BeginFrame(), "Visibility feedback test renderer did not begin capture frame");
    Require(renderer.SubmitScene(scene, desc), "Visibility feedback test renderer did not submit capture frame");
    renderer.EndFrame();
    Require(kb::scene::SceneRenderFeedback::ScreenCaptureStatus(scene, captureId) == kb::scene::SceneScreenCaptureStatus::Failed,
        "A capture that can never succeed under the Noop backend must be honestly completed as Failed on its submit");
    Require(!std::filesystem::exists(capturePath), "A failed capture must not leave a file behind");
    Require(kb::scene::SceneRenderFeedback::RequestScreenCapture(scene, capturePath.string()) != 0U,
        "A terminal capture result must free the single pending slot for the next request");

    renderer.Shutdown();
    std::filesystem::remove(meshPath, error);
    std::filesystem::remove(wideMeshPath, error);
    std::filesystem::remove(root, error);
}

// LIB-146: shared harness bits for the render-resource lifecycle tests below - a scene
// with one mesh entity backed by a real on-disk triangle.obj under its own asset root.
struct LifecycleSceneFixture {
    kb::scene::Scene scene;
    std::uint64_t meshAssetId = 0;
    kb::scene::SceneEntity entity{};
};

void PrepareLifecycleScene(LifecycleSceneFixture& fixture, const std::filesystem::path& root) {
    std::error_code error;
    std::filesystem::create_directories(root, error);
    Require(!error, "LIB-146 lifecycle test could not create asset root");
    WriteTriangleObj(root / "triangle.obj");
    kb::assets::AssetManager& manager = fixture.scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "LIB-146 lifecycle test could not register mesh loader");
    Require(manager.Mounts().Mount("Game", root), "LIB-146 lifecycle test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 1U, "LIB-146 lifecycle test did not discover the mesh asset");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "LIB-146 lifecycle test discovered wrong mesh metadata");
    fixture.meshAssetId = meshMetadata->id.value;
    fixture.entity = fixture.scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Lifecycle Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    fixture.scene.Components().MeshRenderers().Set(fixture.entity, kb::scene::MeshRendererComponent{ .meshAssetId = fixture.meshAssetId });
}

[[nodiscard]] RenderSceneSubmitDesc LifecycleSubmitDesc(std::uint32_t viewportId) {
    return RenderSceneSubmitDesc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ viewportId },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
    };
}

void SubmitLifecycleFrame(Renderer& renderer, const kb::scene::Scene& scene, const RenderSceneSubmitDesc& desc, const char* failure) {
    Require(renderer.BeginFrame(), failure);
    Require(renderer.SubmitScene(scene, desc), failure);
    renderer.EndFrame();
}

void RunRendererSubmitsParticleMeshSnapshotAsOneDrawTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_particle_mesh_submit";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    LifecycleSceneFixture fixture;
    PrepareLifecycleScene(fixture, root);
    fixture.scene.Components().MeshRenderers().Remove(fixture.entity);
    kb::assets::AssetManager& manager = fixture.scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()),
        "Particle mesh submit test could not register its material loader");
    WriteMaterial(root / "particle.kbmat", 0U, 0U, 0U, 0U, 0U);
    Require(manager.DiscoverMountedAssets() >= 2U,
        "Particle mesh submit test could not discover its material asset");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/particle.kbmat");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial",
        "Particle mesh submit test discovered the wrong material metadata");

    SnapshotBackend backend;
    Require(kb::particles::ParticlePlayback::RegisterBackend(fixture.scene, backend).Succeeded() &&
            kb::particles::ParticlePlayback::WarmupRenderSnapshots(fixture.scene).Succeeded(),
        "Particle mesh submit test could not initialize its snapshot channel");
    kb::particles::ParticleRenderEmitterRecord emitter{};
    emitter.instanceId = 1U;
    emitter.effectAssetId = 2U;
    emitter.emitterId = 3U;
    emitter.assetGeneration = 1U;
    emitter.materialAssetId = 1U;
    emitter.meshAssetId = fixture.meshAssetId;
    emitter.materialAssetId = materialMetadata->id.value;
    emitter.firstParticle = 0U;
    emitter.particleCount = 3U;
    emitter.liveParticleCount = 3U;
    emitter.output = kb::particles::ParticleRenderOutput::Mesh;
    emitter.status = kb::particles::ParticleRenderEmitterStatus::Playing;
    emitter.flags = kb::particles::ParticleRenderEmitterFlag::CastsShadow |
        kb::particles::ParticleRenderEmitterFlag::ReceivesShadow;
    emitter.localBasisQuaternionSnorm = {0, 0, 0, 32'767};
    emitter.boundsMinimum = {-1.0F, -1.0F, -1.0F};
    emitter.boundsMaximum = {1.0F, 1.0F, 1.0F};
    const std::array particles{
        kb::particles::ParticleRenderRecord{.position = {-0.2F, 0.0F, 0.2F}, .size = 1.0F, .particleId = 11U, .packedColor = 0xFFFFFFFFU},
        kb::particles::ParticleRenderRecord{.position = {0.0F, 0.0F, 0.3F}, .size = 1.0F, .particleId = 12U, .packedColor = 0xFFFFFFFFU},
        kb::particles::ParticleRenderRecord{.position = {0.2F, 0.0F, 0.4F}, .size = 1.0F, .particleId = 13U, .packedColor = 0xFFFFFFFFU},
    };
    const std::array emitters{emitter};

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Particle mesh submit test renderer did not initialize");
    for (std::uint64_t revision = 1U; revision <= 100U; ++revision) {
        Require(kb::particles::ParticlePlayback::PublishRenderSnapshot(fixture.scene, backend, {
                    .revision = revision, .fixedStepIndex = revision, .emitters = emitters, .particles = particles}).Succeeded(),
            "Particle mesh submit test could not publish its Mesh snapshot");
        SubmitLifecycleFrame(renderer, fixture.scene, LifecycleSubmitDesc(1U),
            "Particle mesh submit test did not submit the snapshot");
        const SceneRenderSubmitStats stats = renderer.LastSceneSubmitStats();
        Require(stats.visibleMeshCount == 3U && stats.submittedMeshCount == 3U &&
                stats.submittedDrawCallCount == 1U,
            "Three Mesh-output particles with one mesh/material were not submitted as one draw");
        Require(renderer.RuntimeResourceStats().cachedMeshCount == 1U,
            "Particle mesh submit did not retain its referenced mesh resource");
        renderer.ReleaseScene(fixture.scene);
        Require(renderer.RuntimeResourceStats().cachedMeshCount == 0U,
            "Releasing a particle-mesh scene retained a mesh resource reference");
    }
    renderer.Shutdown();
    Require(kb::particles::ParticlePlayback::UnregisterBackend(fixture.scene, backend).Succeeded(),
        "Particle mesh submit test could not unregister its snapshot backend");
    std::filesystem::remove_all(root, error);
}

void RunRendererSubmitsParticleStripSnapshotsTest() {
    kb::scene::Scene scene;
    SnapshotBackend backend;
    Require(kb::particles::ParticlePlayback::RegisterBackend(scene, backend).Succeeded() &&
            kb::particles::ParticlePlayback::WarmupRenderSnapshots(scene).Succeeded(),
        "Particle strip submit test could not initialize its snapshot channel");
    kb::particles::ParticleRenderEmitterRecord emitter{};
    emitter.instanceId = 1U;
    emitter.effectAssetId = 2U;
    emitter.emitterId = 3U;
    emitter.assetGeneration = 1U;
    emitter.materialAssetId = 1U;
    emitter.firstParticle = 0U;
    emitter.particleCount = 2U;
    emitter.liveParticleCount = 2U;
    emitter.status = kb::particles::ParticleRenderEmitterStatus::Playing;
    emitter.blend = kb::particles::ParticleRenderBlendMode::Alpha;
    emitter.depth = kb::particles::ParticleRenderDepthMode::ReadOnly;
    emitter.trailSampleIntervalSeconds = 1.0F / 60.0F;
    emitter.trailMaxSamplesPerParticle = 4U;
    emitter.trailWidth = 0.25F;
    emitter.ribbonMaxSegments = 4U;
    emitter.ribbonWidth = 0.25F;
    emitter.outputOrigin = {-0.5F, 0.0F, 0.0F};
    emitter.beamEnd = {0.5F, 0.0F, 0.0F};
    emitter.beamSegments = 3U;
    emitter.beamWidth = 0.25F;
    emitter.beamLocalEnd = {1.0F, 0.0F, 0.0F};
    emitter.boundsMinimum = {-1.0F, -1.0F, -1.0F};
    emitter.boundsMaximum = {1.0F, 1.0F, 1.0F};
    std::array particles{
        kb::particles::ParticleRenderRecord{.position = {-0.25F, 0.0F, 0.2F}, .particleId = 11U,
            .spawnOrdinal = 1U, .packedColor = 0xFFFFFFFFU},
        kb::particles::ParticleRenderRecord{.position = {0.25F, 0.0F, 0.2F}, .particleId = 12U,
            .spawnOrdinal = 2U, .packedColor = 0xFFFFFFFFU},
    };
    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Particle strip submit test renderer did not initialize");
    const auto submit = [&](std::uint64_t revision, kb::particles::ParticleRenderOutput output,
                            std::span<const kb::particles::ParticleRenderRecord> records, bool expectDraw,
                            bool releaseScene) {
        emitter.output = output;
        emitter.particleCount = static_cast<std::uint32_t>(records.size());
        emitter.liveParticleCount = emitter.particleCount;
        const std::array emitters{emitter};
        const auto published = kb::particles::ParticlePlayback::PublishRenderSnapshot(scene, backend, {
            .revision = revision, .fixedStepIndex = revision, .emitters = emitters, .particles = records});
        if (!published.Succeeded()) {
            throw std::runtime_error{"Particle strip submit test could not publish revision " + std::to_string(revision)};
        }
        SubmitLifecycleFrame(renderer, scene, LifecycleSubmitDesc(17U),
            "Particle strip submit test did not submit the snapshot");
        const SceneRenderSubmitStats stats = renderer.LastSceneSubmitStats();
        if (expectDraw) {
            Require(stats.submittedParticleDrawCallCount == 1U && stats.submittedParticleStripSegmentCount != 0U &&
                    stats.failedParticleBatchCount == 0U && stats.failedParticleStripBatchCount == 0U,
                "Particle strip snapshot was not submitted as one renderer-owned dynamic draw");
        }
        if (releaseScene) renderer.ReleaseScene(scene);
    };
    submit(1U, kb::particles::ParticleRenderOutput::Trail, particles, false, false);
    particles[0].position.x += 0.1F;
    particles[1].position.x += 0.1F;
    submit(2U, kb::particles::ParticleRenderOutput::Trail, particles, true, true);
    submit(3U, kb::particles::ParticleRenderOutput::Ribbon, particles, true, true);
    submit(4U, kb::particles::ParticleRenderOutput::Beam, {}, true, true);
    emitter.output = kb::particles::ParticleRenderOutput::Volumetric;
    emitter.particleCount = 1U;
    emitter.liveParticleCount = 1U;
    emitter.volumetricDensity = 0.75F;
    emitter.volumetricRadiusScale = 0.5F;
    emitter.volumetricLowQualitySteps = 8U;
    emitter.volumetricHighQualitySteps = 24U;
    const std::array volumetricEmitters{emitter};
    const std::array volumetricParticles{
        kb::particles::ParticleRenderRecord{.position = {0.0F, 0.0F, 0.2F}, .size = 1.0F,
            .particleId = 13U, .packedColor = 0xFFFFFFFFU}};
    Require(kb::particles::ParticlePlayback::PublishRenderSnapshot(scene, backend, {
                .revision = 5U, .fixedStepIndex = 5U, .emitters = volumetricEmitters,
                .particles = volumetricParticles}).Succeeded(),
        "Volumetric no-depth contract test could not publish its snapshot");
    SubmitLifecycleFrame(renderer, scene, LifecycleSubmitDesc(17U),
        "Volumetric no-depth contract test did not submit its scene");
    const SceneRenderSubmitStats noDepthStats = renderer.LastSceneSubmitStats();
    Require(noDepthStats.failedParticleBatchCount == 1U &&
            noDepthStats.submittedVolumetricParticleCount == 0U &&
            noDepthStats.volumetricParticleRaymarchStepCount == 0U,
        "Volumetric particles fell back to a quad when the opaque depth texture was unavailable");
    renderer.Shutdown();
    Require(kb::particles::ParticlePlayback::UnregisterBackend(scene, backend).Succeeded(),
        "Particle strip submit test could not unregister its snapshot backend");
}

#if defined(_WIN32)
void RunRendererDrawsDetachedViewportFinalCompositePixelsTest() {
    kb::scene::Scene scene;
    NativeTestSurface surface;
    Require(
        surface.IsValid(),
        "Detached viewport final-composite test could not create a hidden native surface");

    DisplayConfig config{};
    config.syncMode = DisplaySyncMode::Uncapped;
    config.preferredBgfxRendererType =
        static_cast<std::int32_t>(bgfx::RendererType::Direct3D11);
    Renderer renderer;
    Require(
        renderer.Initialize(surface, &config),
        "Detached viewport final-composite renderer did not initialize");

    const RenderSceneSubmitDesc primaryDesc{
        .target = RenderSceneTargetBinding{
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .editorSceneOverlaysEnabled = false,
        .shadowPassEnabled = false,
        .postProcessEnabled = false,
        .selectionMaskEnabled = false,
        .selectionOutlineEnabled = false,
    };
    SubmitLifecycleFrame(
        renderer,
        scene,
        primaryDesc,
        "Detached viewport final-composite test could not seed the primary viewport frame");

    {
        SceneRenderTarget sceneTarget;
        ScenePostProcessTargets postProcessTargets;
        FinalCompositeReadbackTarget finalTarget;
        Require(
            sceneTarget.Ensure(SceneRenderTargetDesc{
                .extent = RenderExtent{ 64U, 64U },
            }) &&
                postProcessTargets.Ensure(ScenePostProcessTargetsDesc{
                    .extent = RenderExtent{ 64U, 64U },
                }) &&
                finalTarget.Initialize(),
            "Detached viewport final-composite test could not allocate render targets");

        SceneRenderCamera camera{};
        bx::mtxLookAt(
            camera.view.data(),
            bx::Vec3{ 4.0F, 3.0F, 4.0F },
            bx::Vec3{ 0.0F, 0.0F, 0.0F });
        SceneDepthPolicy::MakePerspective(
            camera.projection.data(),
            60.0F,
            1.0F,
            0.05F,
            100.0F,
            SceneDepthPolicy::HomogeneousDepth());

        ScenePostProcessSettings postProcessSettings{};
        postProcessSettings.temporalAntiAliasingEnabled = false;
        postProcessSettings.temporalJitterEnabled = false;
        postProcessSettings.outputTransform.autoExposure.enabled = false;
        const bgfx::TextureHandle sampledDepth =
            sceneTarget.DepthTextureSampled()
            ? sceneTarget.DepthTexture()
            : bgfx::TextureHandle{ bgfx::kInvalidHandle };
        const RenderSceneSubmitDesc detachedDesc{
            .target = RenderSceneTargetBinding{
                .frameBuffer = sceneTarget.FrameBuffer(),
                .colorTexture = sceneTarget.ColorTexture(),
                .resolvedColorTexture = sceneTarget.ResolvedColorTexture(),
                .depthTexture = sampledDepth,
                .viewport = RenderViewportDesc{
                    .id = RenderViewportId{ 2U },
                    .extent = RenderExtent{ 64U, 64U },
                    .viewportIndex = 1U,
                },
                .msaaSamples = sceneTarget.MsaaSamples(),
                .colorFormat = sceneTarget.ColorSelection().format,
            },
            .postProcess = postProcessTargets.Binding(),
            .finalComposite = finalTarget.Binding(),
            .cameraOverride = camera,
            .postProcessSettings = postProcessSettings,
            .clearRgba = 0x000000FFU,
            .editorSceneOverlaysEnabled = true,
            .shadowPassEnabled = false,
            .postProcessEnabled = true,
            .selectionMaskEnabled = true,
            .selectionOutlineEnabled = true,
            .gpuDrivenRuntimeDispatchEnabled = false,
        };
        SubmitLifecycleFrame(
            renderer,
            scene,
            detachedDesc,
            "Detached viewport final-composite test could not submit viewport index 1");

        const std::vector<std::uint8_t> pixels = finalTarget.ReadPixels();
        const std::array<std::uint8_t, 3U> first{
            pixels[0], pixels[1], pixels[2],
        };
        std::size_t variedPixelCount = 0U;
        for (std::size_t offset = 4U; offset < pixels.size(); offset += 4U) {
            if (pixels[offset] != first[0] ||
                pixels[offset + 1U] != first[1] ||
                pixels[offset + 2U] != first[2]) {
                ++variedPixelCount;
            }
        }
        Require(
            variedPixelCount >= 32U,
            "Detached viewport final composite stayed uniform after switching from viewport index 0");

        postProcessTargets.Shutdown();
        sceneTarget.Shutdown();
    }
    renderer.Shutdown();
}

void RunRendererDrawsParticleMeshSnapshotPixelsTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_particle_mesh_pixels";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    LifecycleSceneFixture fixture;
    PrepareLifecycleScene(fixture, root);
    fixture.scene.Components().MeshRenderers().Remove(fixture.entity);
    kb::assets::AssetManager& manager = fixture.scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()),
        "Particle mesh pixel test could not register its material loader");
    WriteMaterial(root / "particle.kbmat", 0U, 0U, 0U, 0U, 0U);
    {
        std::ofstream output{root / "particle.kbmat", std::ios::app};
        Require(output.is_open(), "Particle mesh pixel test could not make its material double-sided");
        output << "doubleSided true\n";
    }
    Require(manager.DiscoverMountedAssets() >= 2U,
        "Particle mesh pixel test could not discover its material asset");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/particle.kbmat");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial",
        "Particle mesh pixel test discovered the wrong material metadata");

    SnapshotBackend backend;
    Require(kb::particles::ParticlePlayback::RegisterBackend(fixture.scene, backend).Succeeded() &&
            kb::particles::ParticlePlayback::WarmupRenderSnapshots(fixture.scene).Succeeded(),
        "Particle mesh pixel test could not initialize its snapshot channel");
    kb::particles::ParticleRenderEmitterRecord emitter{};
    emitter.instanceId = 1U;
    emitter.effectAssetId = 2U;
    emitter.emitterId = 3U;
    emitter.assetGeneration = 1U;
    emitter.meshAssetId = fixture.meshAssetId;
    emitter.materialAssetId = materialMetadata->id.value;
    emitter.firstParticle = 0U;
    emitter.particleCount = 3U;
    emitter.liveParticleCount = 3U;
    emitter.output = kb::particles::ParticleRenderOutput::Mesh;
    emitter.status = kb::particles::ParticleRenderEmitterStatus::Playing;
    emitter.flags = kb::particles::ParticleRenderEmitterFlag::CastsShadow |
        kb::particles::ParticleRenderEmitterFlag::ReceivesShadow;
    emitter.localBasisQuaternionSnorm = {0, 0, 0, 32'767};
    emitter.boundsMinimum = {-1.0F, -1.0F, -1.0F};
    emitter.boundsMaximum = {1.0F, 1.0F, 1.0F};
    const std::array particles{
        kb::particles::ParticleRenderRecord{.position = {-0.2F, 0.0F, 0.2F}, .size = 1.0F, .particleId = 11U, .packedColor = 0xFFFFFFFFU},
        kb::particles::ParticleRenderRecord{.position = {0.0F, 0.0F, 0.3F}, .size = 1.0F, .particleId = 12U, .packedColor = 0xFFFFFFFFU},
        kb::particles::ParticleRenderRecord{.position = {0.2F, 0.0F, 0.4F}, .size = 1.0F, .particleId = 13U, .packedColor = 0xFFFFFFFFU},
    };
    const std::array emitters{emitter};

    NativeTestSurface surface;
    Require(surface.IsValid(), "Particle mesh pixel test could not create a native render surface");
    DisplayConfig config{};
    config.syncMode = DisplaySyncMode::Uncapped;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Direct3D11);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Particle mesh pixel test renderer did not initialize");
    {
        ParticleMeshReadbackTarget target;
        Require(target.Initialize(), "Particle mesh pixel test could not create its readback target");
        Require(kb::particles::ParticlePlayback::PublishRenderSnapshot(fixture.scene, backend, {
                    .revision = 1U, .fixedStepIndex = 1U, .emitters = emitters, .particles = particles}).Succeeded(),
            "Particle mesh pixel test could not publish its Mesh snapshot");
        const RenderSceneSubmitDesc desc{
            .target = target.Binding(),
            .cameraOverride = IdentityCamera(),
            .clearRgba = 0x101820FFU,
            .editorSceneOverlaysEnabled = false,
            .postProcessEnabled = false,
            .selectionMaskEnabled = false,
            .selectionOutlineEnabled = false,
        };
        SubmitLifecycleFrame(renderer, fixture.scene, desc,
            "Particle mesh pixel test did not submit the snapshot");
        const SceneRenderSubmitStats stats = renderer.LastSceneSubmitStats();
        Require(stats.visibleMeshCount == 3U && stats.submittedMeshCount == 3U &&
                stats.submittedDrawCallCount == 1U,
            "Particle mesh pixel test did not submit three Mesh particles as one draw");
        const std::vector<std::uint8_t> pixels = target.ReadPixels();
        const std::array<std::uint8_t, 3U> background{pixels[0], pixels[1], pixels[2]};
        std::size_t geometryPixelCount = 0U;
        for (std::size_t offset = 0U; offset < pixels.size(); offset += 4U) {
            if (pixels[offset] != background[0] || pixels[offset + 1U] != background[1] ||
                pixels[offset + 2U] != background[2]) {
                ++geometryPixelCount;
            }
        }
        Require(geometryPixelCount >= 32U,
            "Particle mesh pixel test read back only the clear color instead of rendered mesh pixels");
    }
    renderer.Shutdown();
    Require(kb::particles::ParticlePlayback::UnregisterBackend(fixture.scene, backend).Succeeded(),
        "Particle mesh pixel test could not unregister its snapshot backend");
    std::filesystem::remove_all(root, error);
}

void RunRendererDrawsParticleStripSnapshotPixelsTest() {
    kb::scene::Scene scene;
    SnapshotBackend backend;
    Require(kb::particles::ParticlePlayback::RegisterBackend(scene, backend).Succeeded() &&
            kb::particles::ParticlePlayback::WarmupRenderSnapshots(scene).Succeeded(),
        "Particle strip pixel test could not initialize its snapshot channel");
    kb::particles::ParticleRenderEmitterRecord emitter{};
    emitter.instanceId = 1U;
    emitter.effectAssetId = 2U;
    emitter.emitterId = 3U;
    emitter.assetGeneration = 1U;
    emitter.materialAssetId = 1U;
    emitter.output = kb::particles::ParticleRenderOutput::Beam;
    emitter.status = kb::particles::ParticleRenderEmitterStatus::Playing;
    emitter.beamLocalEnd = {1.0F, 0.0F, 0.0F};
    emitter.outputOrigin = {-0.6F, 0.0F, 0.1F};
    emitter.beamEnd = {0.6F, 0.0F, 0.1F};
    emitter.beamSegments = 4U;
    emitter.beamWidth = 0.25F;
    emitter.boundsMinimum = {-1.0F, -1.0F, -1.0F};
    emitter.boundsMaximum = {1.0F, 1.0F, 1.0F};
    const std::array emitters{emitter};
    NativeTestSurface surface;
    Require(surface.IsValid(), "Particle strip pixel test could not create a native render surface");
    DisplayConfig config{};
    config.syncMode = DisplaySyncMode::Uncapped;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Direct3D11);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Particle strip pixel test renderer did not initialize");
    {
        ParticleMeshReadbackTarget target;
        Require(target.Initialize(), "Particle strip pixel test could not create its readback target");
        Require(kb::particles::ParticlePlayback::PublishRenderSnapshot(scene, backend, {
                    .revision = 1U, .fixedStepIndex = 1U, .emitters = emitters,
                    .particles = std::span<const kb::particles::ParticleRenderRecord>{}}).Succeeded(),
            "Particle strip pixel test could not publish its Beam snapshot");
        const RenderSceneSubmitDesc desc{
            .target = target.Binding(), .cameraOverride = IdentityCamera(), .clearRgba = 0x101820FFU,
            .editorSceneOverlaysEnabled = false, .postProcessEnabled = false,
            .selectionMaskEnabled = false, .selectionOutlineEnabled = false,
        };
        SubmitLifecycleFrame(renderer, scene, desc, "Particle strip pixel test did not submit the snapshot");
        const SceneRenderSubmitStats stats = renderer.LastSceneSubmitStats();
        Require(stats.submittedParticleDrawCallCount == 1U && stats.submittedParticleStripSegmentCount == 4U,
            "Particle strip pixel test did not submit the Beam's dynamic segments");
        const std::vector<std::uint8_t> pixels = target.ReadPixels();
        const std::array<std::uint8_t, 3U> background{pixels[0], pixels[1], pixels[2]};
        std::size_t geometryPixelCount = 0U;
        for (std::size_t offset = 0U; offset < pixels.size(); offset += 4U) {
            if (pixels[offset] != background[0] || pixels[offset + 1U] != background[1] ||
                pixels[offset + 2U] != background[2]) ++geometryPixelCount;
        }
        Require(geometryPixelCount >= 16U,
            "Particle strip pixel test read back only the clear color instead of dynamic-strip pixels");
    }
    renderer.Shutdown();
    Require(kb::particles::ParticlePlayback::UnregisterBackend(scene, backend).Succeeded(),
        "Particle strip pixel test could not unregister its snapshot backend");
}

void RunRendererDrawsVolumetricParticleSnapshotPixelsTest() {
    kb::scene::Scene scene;
    SnapshotBackend backend;
    Require(kb::particles::ParticlePlayback::RegisterBackend(scene, backend).Succeeded() &&
            kb::particles::ParticlePlayback::WarmupRenderSnapshots(scene).Succeeded(),
        "Volumetric pixel test could not initialize its snapshot channel");
    kb::particles::ParticleRenderEmitterRecord emitter{};
    emitter.instanceId = 1U;
    emitter.effectAssetId = 2U;
    emitter.emitterId = 3U;
    emitter.assetGeneration = 1U;
    emitter.materialAssetId = 1U;
    emitter.firstParticle = 0U;
    emitter.particleCount = 1U;
    emitter.liveParticleCount = 1U;
    emitter.output = kb::particles::ParticleRenderOutput::Volumetric;
    emitter.status = kb::particles::ParticleRenderEmitterStatus::Playing;
    emitter.volumetricDensity = 0.75F;
    emitter.volumetricRadiusScale = 0.35F;
    emitter.volumetricLowQualitySteps = 8U;
    emitter.volumetricHighQualitySteps = 24U;
    emitter.boundsMinimum = {-1.0F, -1.0F, -1.0F};
    emitter.boundsMaximum = {1.0F, 1.0F, 1.0F};
    const std::array emitters{emitter};
    const std::array particles{
        kb::particles::ParticleRenderRecord{.position = {0.0F, 0.0F, 0.2F}, .size = 1.0F,
            .particleId = 11U, .packedColor = 0xFFFFFFFFU}};

    NativeTestSurface surface;
    Require(surface.IsValid(), "Volumetric pixel test could not create a native render surface");
    DisplayConfig config{};
    config.syncMode = DisplaySyncMode::Uncapped;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Direct3D11);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Volumetric pixel test renderer did not initialize");
    {
        ParticleMeshReadbackTarget target;
        Require(target.Initialize(), "Volumetric pixel test could not create its readback target");
        Require(kb::particles::ParticlePlayback::PublishRenderSnapshot(scene, backend, {
                    .revision = 1U, .fixedStepIndex = 1U, .emitters = emitters,
                    .particles = particles}).Succeeded(),
            "Volumetric pixel test could not publish its snapshot");
        const RenderSceneSubmitDesc desc{
            .target = target.Binding(),
            .cameraOverride = IdentityCamera(),
            .lightingConfig = SceneRenderLightingConfig{.lightingPath = SceneRenderLightingPath::Forward},
            .clearRgba = 0x101820FFU,
            .editorSceneOverlaysEnabled = false, .postProcessEnabled = false,
            .selectionMaskEnabled = false, .selectionOutlineEnabled = false,
        };
        SubmitLifecycleFrame(renderer, scene, desc, "Volumetric pixel test did not submit its snapshot");
        const SceneRenderSubmitStats stats = renderer.LastSceneSubmitStats();
        Require(stats.submittedParticleDrawCallCount == 1U &&
                stats.submittedVolumetricParticleCount == 1U &&
                stats.volumetricParticleRaymarchStepCount == 24U &&
                stats.failedParticleBatchCount == 0U,
            "Volumetric pixel test did not submit the depth-aware high-quality raymarch path");
        RenderSceneSubmitDesc lowQualityDesc = desc;
        lowQualityDesc.materialGraphContext.qualityLevel = RenderMaterialGraphQualityLevel::Low;
        SubmitLifecycleFrame(renderer, scene, lowQualityDesc,
            "Volumetric pixel test did not submit its low-quality snapshot");
        const SceneRenderSubmitStats lowQualityStats = renderer.LastSceneSubmitStats();
        Require(lowQualityStats.submittedParticleDrawCallCount == 1U &&
                lowQualityStats.submittedVolumetricParticleCount == 1U &&
                lowQualityStats.volumetricParticleRaymarchStepCount == 8U &&
                lowQualityStats.failedParticleBatchCount == 0U,
            "Volumetric low quality changed more than the authored raymarch step budget");
        const std::vector<std::uint8_t> pixels = target.ReadPixels();
        const std::array<std::uint8_t, 3U> background{pixels[0], pixels[1], pixels[2]};
        std::size_t geometryPixelCount = 0U;
        for (std::size_t offset = 0U; offset < pixels.size(); offset += 4U) {
            if (pixels[offset] != background[0] || pixels[offset + 1U] != background[1] ||
                pixels[offset + 2U] != background[2]) ++geometryPixelCount;
        }
        Require(geometryPixelCount >= 16U,
            "Volumetric pixel test read back only the clear color instead of the raymarched impostor");
    }
    renderer.Shutdown();
    Require(kb::particles::ParticlePlayback::UnregisterBackend(scene, backend).Succeeded(),
        "Volumetric pixel test could not unregister its snapshot backend");
}
#endif

// LIB-146 (scene unload): Renderer::ReleaseScene must destroy exactly the released
// scene's runtime GPU resources ({sceneId, assetId}-keyed isolation - a second live scene
// keeps its own), the released scene must remain fully re-submittable (release is a clean
// unload, not poisoning), and ReleaseAllScenes must drop everything.
void RunRendererReleaseSceneDropsRuntimeResourcesTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_release_scene";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    LifecycleSceneFixture first;
    LifecycleSceneFixture second;
    PrepareLifecycleScene(first, root / "a");
    PrepareLifecycleScene(second, root / "b");

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "LIB-146 release test renderer did not initialize");
    const RenderSceneSubmitDesc desc = LifecycleSubmitDesc(1U);

    SubmitLifecycleFrame(renderer, first.scene, desc, "LIB-146 release test did not submit the first scene");
    SubmitLifecycleFrame(renderer, second.scene, desc, "LIB-146 release test did not submit the second scene");
    Require(renderer.RuntimeResourceStats().cachedMeshCount == 2U, "Two submitted scenes must cache one mesh resource each");

    renderer.ReleaseScene(first.scene);
    Require(renderer.RuntimeResourceStats().cachedMeshCount == 1U, "ReleaseScene must destroy exactly the released scene's cached resources");
    SubmitLifecycleFrame(renderer, second.scene, desc, "LIB-146 release test did not resubmit the surviving scene");
    Require(renderer.LastSceneSubmitStats().visibleMeshCount > 0U, "Releasing one scene must not break another scene's rendering");
    Require(renderer.RuntimeResourceStats().cachedMeshCount == 1U, "The surviving scene's cache entry must be reused, not rebuilt");

    SubmitLifecycleFrame(renderer, first.scene, desc, "LIB-146 release test did not resubmit the released scene");
    Require(renderer.LastSceneSubmitStats().visibleMeshCount > 0U, "A released scene must be cleanly re-submittable");
    Require(renderer.RuntimeResourceStats().cachedMeshCount == 2U, "Resubmitting a released scene must re-ensure its resources");

    renderer.ReleaseAllScenes();
    Require(renderer.RuntimeResourceStats().cachedMeshCount == 0U && renderer.RuntimeResourceStats().cachedMaterialCount == 0U && renderer.RuntimeResourceStats().cachedTextureCount == 0U,
        "ReleaseAllScenes must drop every cached runtime resource");
    SubmitLifecycleFrame(renderer, first.scene, desc, "LIB-146 release test did not resubmit after ReleaseAllScenes");
    Require(renderer.LastSceneSubmitStats().visibleMeshCount > 0U, "ReleaseAllScenes must leave the renderer fully usable");

    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

// LIB-146 (entity destroy + retention eviction): destroying the last entity referencing a
// mesh removes its render proxy immediately, but the GPU resource stays cached BY DESIGN
// for kRuntimeAssetRetentionFrames (120) - and PruneUnused then actually evicts it. A new
// entity using the same asset after eviction re-ensures the resource from scratch.
void RunRendererPrunesUnreferencedResourcesAfterRetentionTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_retention_prune";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    LifecycleSceneFixture fixture;
    PrepareLifecycleScene(fixture, root);

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "LIB-146 retention test renderer did not initialize");
    const RenderSceneSubmitDesc desc = LifecycleSubmitDesc(1U);

    SubmitLifecycleFrame(renderer, fixture.scene, desc, "LIB-146 retention test did not submit the initial frame");
    Require(renderer.LastSceneSubmitStats().visibleMeshCount > 0U && renderer.RuntimeResourceStats().cachedMeshCount == 1U,
        "LIB-146 retention test initial submit did not cache the mesh resource");

    fixture.scene.Entities().Destroy(fixture.entity);
    SubmitLifecycleFrame(renderer, fixture.scene, desc, "LIB-146 retention test did not submit after entity destroy");
    Require(renderer.LastSceneSubmitStats().visibleMeshCount == 0U, "A destroyed entity's proxy must stop rendering on the next full sync");
    Require(renderer.RuntimeResourceStats().cachedMeshCount == 1U,
        "An unreferenced resource must stay cached inside the retention window (destroying the entity is not an immediate GPU destroy)");

    // Age the resource past the retention window; PruneUnused runs at the end of every
    // SubmitScenes, keyed to the completed-frame counter.
    for (std::uint32_t frame = 0U; frame < Renderer::kRuntimeAssetRetentionFrames + 8U; ++frame) {
        SubmitLifecycleFrame(renderer, fixture.scene, desc, "LIB-146 retention test did not submit an aging frame");
    }
    Require(renderer.RuntimeResourceStats().cachedMeshCount == 0U,
        "PruneUnused must actually evict a resource once nothing referenced it for kRuntimeAssetRetentionFrames");

    const kb::scene::SceneEntity revived = fixture.scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Revived Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    fixture.scene.Components().MeshRenderers().Set(revived, kb::scene::MeshRendererComponent{ .meshAssetId = fixture.meshAssetId });
    SubmitLifecycleFrame(renderer, fixture.scene, desc, "LIB-146 retention test did not submit the revived entity");
    Require(renderer.LastSceneSubmitStats().visibleMeshCount > 0U && renderer.RuntimeResourceStats().cachedMeshCount == 1U,
        "A pruned asset must be cleanly re-ensured when a new entity references it again");

    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

// LIB-146 (asset reload): a mesh asset whose on-disk content (and therefore contentHash)
// changed must be rebuilt into a NEW GPU handle on the next submit, with the old handle
// honestly unresolvable - the mesh sibling of the long-standing material/texture reload
// tests (RunRendererReloadsChangedRuntimeMaterialAssetTest).
void RunRendererReloadsChangedRuntimeMeshAssetTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_mesh_reload";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    LifecycleSceneFixture fixture;
    PrepareLifecycleScene(fixture, root);

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "LIB-146 mesh reload test renderer did not initialize");
    const RenderSceneSubmitDesc desc = LifecycleSubmitDesc(1U);

    SubmitLifecycleFrame(renderer, fixture.scene, desc, "LIB-146 mesh reload test did not submit the initial frame");
    const RenderMeshHandle firstHandle = renderer.SceneResourceMap()->ResolveMesh(fixture.meshAssetId);
    const RenderMeshResource* firstResource = renderer.SceneResources()->FindMesh(firstHandle);
    Require(firstHandle.IsValid() && firstResource != nullptr,
        "LIB-146 mesh reload test initial submit did not bind a live mesh handle");
    const float firstRadius = firstResource->bounds.radius;

    // Rewrite the mesh with different geometry -> new contentHash on rediscovery.
    {
        std::ofstream output{ root / "triangle.obj", std::ios::trunc };
        output
            << "v -0.3 -0.3 0.0\n"
            << "v 0.3 -0.3 0.0\n"
            << "v 0.0 0.3 0.1\n"
            << "vt 0 0\n"
            << "vt 1 0\n"
            << "vt 0.5 1\n"
            << "vn 0 0 1\n"
            << "f 1/1/1 2/2/1 3/3/1\n";
    }
    Require(fixture.scene.Assets().Manager().DiscoverMountedAssets() >= 1U, "LIB-146 mesh reload test rediscovery failed");
    SubmitLifecycleFrame(renderer, fixture.scene, desc, "LIB-146 mesh reload test did not submit the reload frame");

    const RenderMeshHandle secondHandle = renderer.SceneResourceMap()->ResolveMesh(fixture.meshAssetId);
    Require(secondHandle.IsValid() && secondHandle.value != firstHandle.value,
        "A changed mesh contentHash must rebuild the GPU mesh into a new handle on the next submit");
    Require(renderer.SceneResources()->FindMesh(firstHandle) == nullptr,
        "The replaced mesh's old handle must be honestly unresolvable after the reload");
    const RenderMeshResource* secondResource = renderer.SceneResources()->FindMesh(secondHandle);
    Require(secondResource != nullptr && renderer.RuntimeResourceStats().cachedMeshCount == 1U,
        "The reloaded mesh must be live and cached exactly once");
    // A new handle alone would still be satisfied by a rebuild that re-uploaded the OLD geometry, so
    // assert the geometry itself moved: the rewritten triangle is three times larger, making the GPU
    // resource's own bounds the honest witness that the bytes on disk actually reached the GPU.
    Require(secondResource->bounds.radius > firstRadius * 2.0F,
        "The rebuilt GPU mesh must carry the rewritten geometry, not a re-upload of the stale asset");
    Require(renderer.LastSceneSubmitStats().visibleMeshCount > 0U, "The reloaded mesh must keep rendering");

    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunRendererSubmitsRuntimeMeshAssetInHeadlessNoopTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_runtime_submit";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Runtime submit test could not create temp root");
    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path albedoPath = root / "albedo.kbtex";
    const std::filesystem::path normalPath = root / "normal.kbtex";
    const std::filesystem::path metallicRoughnessPath = root / "metallic_roughness.kbtex";
    const std::filesystem::path occlusionPath = root / "occlusion.kbtex";
    const std::filesystem::path emissivePath = root / "emissive.kbtex";
    const std::filesystem::path materialPath = root / "paint.kbmat";
    const std::filesystem::path transparentMaterialPath = root / "glass.kbmat";
    WriteTriangleObj(meshPath);
    WriteTexture(albedoPath, 180U, 160U, 140U);
    WriteTexture(normalPath, 128U, 128U, 255U);
    WriteTexture(metallicRoughnessPath, 0U, 180U, 80U);
    WriteTexture(occlusionPath, 192U, 192U, 192U);
    WriteTexture(emissivePath, 16U, 32U, 64U);

    kb::scene::Scene scene;
    kb::scene::SceneLightingAccess::SetBasicLightingEnabled(scene, true);
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Runtime submit test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Runtime submit test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderTextureAssetLoader>()), "Runtime submit test could not register texture loader");
    Require(manager.Mounts().Mount("Game", root), "Runtime submit test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 5U, "Runtime submit test did not discover mesh and texture assets");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Runtime submit test discovered wrong mesh metadata");
    const kb::assets::AssetMetadata* albedoMetadata = manager.Registry().FindByPath("/Game/albedo.kbtex");
    const kb::assets::AssetMetadata* normalMetadata = manager.Registry().FindByPath("/Game/normal.kbtex");
    const kb::assets::AssetMetadata* metallicRoughnessMetadata = manager.Registry().FindByPath("/Game/metallic_roughness.kbtex");
    const kb::assets::AssetMetadata* occlusionMetadata = manager.Registry().FindByPath("/Game/occlusion.kbtex");
    const kb::assets::AssetMetadata* emissiveMetadata = manager.Registry().FindByPath("/Game/emissive.kbtex");
    Require(albedoMetadata != nullptr && normalMetadata != nullptr && metallicRoughnessMetadata != nullptr && occlusionMetadata != nullptr && emissiveMetadata != nullptr, "Runtime submit test did not discover texture metadata");
    WriteMaterial(materialPath, albedoMetadata->id.value, normalMetadata->id.value, metallicRoughnessMetadata->id.value, occlusionMetadata->id.value, emissiveMetadata->id.value);
    WriteMaterialWithTexturePaths(transparentMaterialPath, "BLEND", 0.5F, "missing_emissive.kbtex");
    Require(manager.DiscoverMountedAssets() >= 7U, "Runtime submit test did not discover material assets");
    meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Runtime submit test lost mesh metadata after material discovery");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/paint.kbmat");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Runtime submit test discovered wrong material metadata");
    const kb::assets::AssetMetadata* transparentMaterialMetadata = manager.Registry().FindByPath("/Game/glass.kbmat");
    Require(transparentMaterialMetadata != nullptr && transparentMaterialMetadata->type == "RenderMaterial", "Runtime submit test discovered wrong transparent material metadata");

    constexpr std::uint32_t instanceCount = 16U;
    for (std::uint32_t index = 0U; index < instanceCount; ++index) {
        const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
            .name = "Runtime Mesh",
            .transform = TransformAt(0.0F, 0.0F, 0.0F),
        });
        scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
            .meshAssetId = meshMetadata->id.value,
            .materialAssetId = materialMetadata->id.value,
        });
    }
    const kb::scene::SceneEntity transparentEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Transparent Runtime Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(transparentEntity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialAssetId = transparentMaterialMetadata->id.value,
    });
    const kb::scene::SceneEntity light = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Runtime Directional Light",
        .transform = TransformAt(0.0F, 10.0F, -10.0F),
    });
    scene.Components().Lights().Set(light, kb::scene::LightComponent{
        .kind = kb::scene::LightKind::Directional,
        .intensity = 1.0F,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 4U,
        .cachedMaterials = 4U,
        .cachedTextures = 8U,
        .frameReferencedMeshes = 4U,
        .frameReferencedMaterials = 4U,
        .frameReferencedTextures = 8U,
        .scenePassSubmitStats = 1U,
        .renderSceneMeshProxies = 32U,
        .renderSceneDrawGroupKeys = 8U,
        .meshResourceSlots = 4U,
        .materialResourceSlots = 4U,
        .textureResourceSlots = 8U,
        .meshBindings = 4U,
        .materialBindings = 4U,
        .textureBindings = 8U,
        .syncMeshProxies = 32U,
        .syncTransformCacheEntries = 32U,
        .syncTransformResolvingEntries = 32U,
    });
    Require(renderer.Initialize(surface, &config), "Renderer did not initialize in explicit headless Noop mode");

    const MaterialProgramRegistryStats programStats = renderer.MaterialProgramStats();
    Require(programStats.loads == 4U,
        "KBMAT-MAT05: Renderer init must load the builtin mesh/GBuffer/shadow/selection programs through the MaterialProgramRegistry");
    Require(programStats.liveProgramCount == 4U,
        "KBMAT-MAT05: MaterialProgramRegistry stats must be exposed through renderer diagnostics with the live builtin programs");
    Require(programStats.failures == 0U,
        "KBMAT-MAT05: Builtin program loading must not report failures under the headless Noop backend");

    Require(renderer.BeginFrame(), "Renderer did not begin headless runtime frame");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 8U,
            .maxVisibleInstances = 64U,
        },
    };
    Require(renderer.SubmitScene(scene, desc), "Renderer did not submit runtime mesh asset scene");

    const SceneRenderSubmitStats submitStats = renderer.LastSceneSubmitStats();
    // 16 opaque instances render in the opaque + shadow passes (32); the 1 blended instance renders in
    // the now-active transparent pass (MAT-80), so the visible/submitted totals are instanceCount*2 + 1.
    Require(submitStats.visibleMeshCount == instanceCount * 2U + 1U, "Runtime submit did not keep opaque (opaque+shadow) and blended (transparent) mesh instances visible");
    Require(submitStats.submittedMeshCount == instanceCount * 2U + 1U, "Runtime submit did not submit opaque (opaque+shadow) and blended (transparent) mesh instances");
    // Opaque instances = 1 instanced draw in the opaque pass + 1 in the shadow pass; the blended instance
    // adds 1 draw in the transparent pass (MAT-80) = 3 draw calls.
    Require(submitStats.submittedDrawCallCount == 3U, "Runtime submit must draw opaque (opaque+shadow) and blended (transparent) materials");
    Require(submitStats.shadowCasterCount == instanceCount, "Runtime submit did not count shadow casters");
    Require(submitStats.submittedShadowCasterCount == instanceCount, "Runtime submit did not submit shadow casters");
    Require(submitStats.submittedShadowDrawCallCount == 1U, "Runtime submit did not draw one shadow caster batch");
    Require(submitStats.shadowFilterSampleCount == 9U, "Runtime submit did not report PCF shadow filter sample count");
    Require(submitStats.shadowLightEntityId == light.Id(), "Runtime submit did not report selected shadow light entity");
    Require(submitStats.shadowMapAllocationBytes == 1024ULL * 1024ULL * 4ULL, "Runtime submit did not report shadow map allocation bytes");
    Require(submitStats.submittedEnvironmentLightingCount == 3U, "Runtime submit did not report environment lighting for every mesh pass");
    Require(submitStats.environmentLightingMode == static_cast<std::uint32_t>(SceneRenderEnvironmentMode::Constant) + 1U, "Runtime submit did not report default environment lighting mode");
    Require(submitStats.environmentLightingSampleCount == 1U, "Runtime submit did not report default environment sample count");
    Require(!submitStats.HasMissingResources(), "Runtime submit reported missing resources for discovered mesh asset");
    const std::span<const SceneRenderPassSubmitStats> passStats = renderer.LastScenePassSubmitStats();
    Require(passStats.size() == 3U, "Runtime submit did not report shadow, opaque, and transparent pass stats");
    Require(passStats[0].pass == MeshPassType::ShadowDepth && passStats[0].stats.submittedShadowCasterCount == instanceCount, "Runtime submit shadow pass stats are wrong");
    Require(passStats[0].stats.shadowFilterSampleCount == 9U, "Runtime submit shadow pass did not report PCF filter sample count");
    Require(passStats[0].stats.shadowLightEntityId == light.Id(), "Runtime submit shadow pass did not report selected shadow light entity");
    Require(passStats[0].stats.shadowMapAllocationBytes == 1024ULL * 1024ULL * 4ULL, "Runtime submit shadow pass did not report shadow map allocation bytes");
    Require(passStats[1].pass == MeshPassType::BaseOpaque && passStats[1].stats.submittedMeshCount == instanceCount, "Runtime submit opaque pass stats are wrong");
    Require(passStats[2].pass == MeshPassType::BaseTransparent && passStats[2].stats.submittedMeshCount == 1U, "Runtime submit transparent pass must submit the blended material (MAT-80)");
    Require(renderer.LastSceneExposureStats().empty(), "Runtime submit without post-process unexpectedly reported exposure stats");

    const Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
    Require(runtimeStats.cachedMeshCount == 1U, "Runtime submit did not cache exactly one mesh resource");
    Require(runtimeStats.cachedMaterialCount == 2U, "Runtime submit did not cache opaque and transparent material resources");
    Require(runtimeStats.cachedTextureCount == 5U, "Runtime submit did not cache every referenced material texture");
    Require(runtimeStats.materialLoadedCount == 2U, "KBMAT-0901: Runtime submit should count loaded material resources");
    Require(runtimeStats.materialFallbackCount == 0U, "KBMAT-0901: Runtime submit should not count material fallbacks for valid materials");
    Require(runtimeStats.materialErrorCount == 0U, "KBMAT-0901: Runtime submit should not count material errors for valid materials");
    Require(runtimeStats.materialReloadCount == 0U, "KBMAT-0901: First runtime submit should not count material reloads");
    Require(runtimeStats.referencedMeshAssetCount == 1U, "Runtime submit did not reference exactly one mesh asset");
    Require(runtimeStats.referencedMaterialAssetCount == 2U, "Runtime submit did not reference opaque and transparent material assets");
    Require(runtimeStats.referencedTextureAssetCount == 5U, "Runtime submit did not reference every material texture asset");
    Require(runtimeStats.unresolvedMaterialTexturePathCount == 1U, "Runtime submit did not report unresolved material texture paths");
    Require(runtimeStats.shadowMapAllocated && runtimeStats.shadowMapSize == 1024U, "Runtime submit did not allocate the configured runtime shadow map");
    Require(runtimeStats.shadowMapAllocationBytes == 1024ULL * 1024ULL * 4ULL, "Runtime submit did not expose shadow map allocation bytes");
    Require(runtimeStats.defaultEnvironmentLightingMode == static_cast<std::uint32_t>(SceneRenderEnvironmentMode::Constant) + 1U, "Runtime submit did not expose default environment lighting mode");
    Require(runtimeStats.defaultEnvironmentLightingSampleCount == 1U, "Runtime submit did not expose default environment sample count");
    Require(runtimeStats.defaultShadowFilterSampleCount == 9U, "Runtime submit did not expose default shadow filter sample count");
    bool foundUnresolvedTexturePathDiagnostic = false;
    bool foundDisabledBlendDiagnostic = false;
    for (const SceneRenderDiagnosticEvent& event : renderer.LastSceneDiagnostics().events) {
        if (event.severity == SceneRenderDiagnosticSeverity::Warning &&
            event.kind == SceneRenderDiagnosticKind::UnresolvedMaterialTexturePath &&
            event.materialAssetId == transparentMaterialMetadata->id.value &&
            event.instanceCount == 1U) {
            foundUnresolvedTexturePathDiagnostic = true;
        }
        if (event.kind == SceneRenderDiagnosticKind::UnsupportedMaterialAlphaBlend) {
            foundDisabledBlendDiagnostic = true;
        }
    }
    Require(foundUnresolvedTexturePathDiagnostic, "Runtime submit did not emit unresolved material texture path diagnostic");
    // MAT-80: blended materials are now supported via the transparent pass, so they must NOT raise the
    // "unsupported alpha blend" diagnostic anymore.
    Require(!foundDisabledBlendDiagnostic, "Runtime submit must not flag blended materials as unsupported once the transparent pass is active");
    Require(runtimeStats.renderSceneMeshProxyCount == instanceCount + 1U, "Runtime submit did not keep scene mesh proxies");
    Require(runtimeStats.meshResourceSlotCapacity >= 4U, "Runtime submit did not apply mesh resource slot reserve");
    Require(runtimeStats.materialResourceSlotCapacity >= 4U, "Runtime submit did not apply material resource slot reserve");
    Require(runtimeStats.textureResourceSlotCapacity >= 8U, "Runtime submit did not apply texture resource slot reserve");
    Require(runtimeStats.meshBindingCapacity >= 4U, "Runtime submit did not apply mesh binding reserve");
    Require(runtimeStats.materialBindingCapacity >= 4U, "Runtime submit did not apply material binding reserve");
    Require(runtimeStats.textureBindingCapacity >= 8U, "Runtime submit did not apply texture binding reserve");
    Require(runtimeStats.renderSceneMeshProxyCapacity >= 32U, "Runtime submit did not apply render scene mesh proxy reserve");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunRendererReloadsChangedRuntimeMaterialAssetTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_runtime_material_reload";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Runtime material reload test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path texturePath = root / "albedo.kbtex";
    const std::filesystem::path materialPath = root / "reloadable.kbmat";
    const std::filesystem::path stableMaterialPath = root / "stable.kbmat";
    WriteTriangleObj(meshPath);
    WriteTexture(texturePath, 180U, 160U, 140U);

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Runtime material reload test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Runtime material reload test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderTextureAssetLoader>()), "Runtime material reload test could not register texture loader");
    Require(manager.Mounts().Mount("Game", root), "Runtime material reload test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 2U, "Runtime material reload test did not discover mesh and texture assets");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* textureMetadata = manager.Registry().FindByPath("/Game/albedo.kbtex");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Runtime material reload test discovered wrong mesh metadata");
    Require(textureMetadata != nullptr && textureMetadata->type == "RenderTexture", "Runtime material reload test discovered wrong texture metadata");
    const std::uint64_t meshAssetId = meshMetadata->id.value;
    const std::uint64_t textureAssetId = textureMetadata->id.value;

    WriteReloadableMaterial(materialPath, 0.2F, 0.7F, textureAssetId);
    WriteReloadableMaterial(stableMaterialPath, 0.4F, 0.8F, 0U);
    Require(manager.DiscoverMountedAssets() >= 4U, "Runtime material reload test did not discover material assets");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/reloadable.kbmat");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Runtime material reload test discovered wrong material metadata");
    const kb::assets::AssetMetadata* stableMaterialMetadata = manager.Registry().FindByPath("/Game/stable.kbmat");
    Require(stableMaterialMetadata != nullptr && stableMaterialMetadata->type == "RenderMaterial", "Runtime material reload test discovered wrong stable material metadata");
    const std::uint64_t materialAssetId = materialMetadata->id.value;
    const std::uint64_t stableMaterialAssetId = stableMaterialMetadata->id.value;
    const std::uint64_t firstContentHash = materialMetadata->contentHash;

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Reloadable Runtime Material Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshAssetId,
        .materialAssetId = materialAssetId,
    });
    const kb::scene::SceneEntity stableEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Stable Runtime Material Mesh",
        .transform = TransformAt(2.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(stableEntity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshAssetId,
        .materialAssetId = stableMaterialAssetId,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 2U,
        .cachedMaterials = 2U,
        .cachedTextures = 2U,
        .frameReferencedMeshes = 2U,
        .frameReferencedMaterials = 2U,
        .frameReferencedTextures = 2U,
        .scenePassSubmitStats = 1U,
        .renderSceneMeshProxies = 2U,
        .renderSceneDrawGroupKeys = 2U,
        .meshResourceSlots = 2U,
        .materialResourceSlots = 2U,
        .textureResourceSlots = 2U,
        .meshBindings = 2U,
        .materialBindings = 2U,
        .textureBindings = 2U,
        .syncMeshProxies = 2U,
        .syncTransformCacheEntries = 2U,
        .syncTransformResolvingEntries = 2U,
    });
    Require(renderer.Initialize(surface, &config), "Renderer did not initialize in runtime material reload test");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 4U,
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
    };

    Require(renderer.BeginFrame(), "Renderer did not begin first material reload frame");
    Require(renderer.SubmitScene(scene, desc), "Renderer did not submit first material reload frame");
    const SceneRenderSubmitStats firstSubmitStats = renderer.LastSceneSubmitStats();
    Require(firstSubmitStats.meshDrawCommandCacheMissCount == 1U, "First material reload submit did not build one draw command cache entry");
    Require(firstSubmitStats.meshDrawCommandCacheBuildCount == 1U, "First material reload submit did not report one cache build");
    Require(firstSubmitStats.meshDrawCommandCacheHitCount == 0U, "First material reload submit unexpectedly hit draw command cache");

    const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const RenderResourceRegistry* resources = renderer.SceneResources();
    Require(resourceMap != nullptr && resources != nullptr, "Runtime material reload test could not access scene resources");
    const RenderMaterialHandle firstHandle = resourceMap->ResolveMaterial(materialAssetId);
    const RenderMaterialResource* firstMaterial = resources->FindMaterial(firstHandle);
    Require(firstHandle.IsValid() && firstMaterial != nullptr, "Runtime material reload test did not bind initial material resource");
    const RenderMaterialHandle stableFirstHandle = resourceMap->ResolveMaterial(stableMaterialAssetId);
    const RenderMaterialResource* stableFirstMaterial = resources->FindMaterial(stableFirstHandle);
    Require(stableFirstHandle.IsValid() && stableFirstMaterial != nullptr, "Runtime material reload test did not bind stable material resource");
    const std::uint64_t firstVersion = firstMaterial->version;
    Require(firstVersion != 0U, "Initial material resource did not receive a version");
    Require(NearlyEqual(firstMaterial->baseColor[0], 0.2F), "Initial runtime material resource did not use first asset color");
    Require(NearlyEqual(firstMaterial->roughnessFactor, 0.7F), "Initial runtime material resource did not use first asset roughness");
    const RenderTextureHandle firstTextureHandle = resourceMap->ResolveTexture(textureAssetId, RenderTextureColorSpace::Srgb);
    const RenderTextureResource* firstTexture = resources->FindTexture(firstTextureHandle);
    Require(firstTextureHandle.IsValid() && firstTexture != nullptr, "Runtime material reload test did not bind initial albedo texture");
    const std::uint64_t firstTextureVersion = firstTexture->version;
    renderer.EndFrame();

    WriteTexture(texturePath, 24U, 96U, 220U);
    Require(manager.DiscoverMountedAssets() >= 3U, "Runtime material reload test did not rediscover changed texture asset");
    textureMetadata = manager.Registry().FindByPath("/Game/albedo.kbtex");
    Require(textureMetadata != nullptr && textureMetadata->type == "RenderTexture", "Runtime material reload test lost texture metadata after rediscovery");

    Require(renderer.BeginFrame(), "Renderer did not begin texture reload frame");
    Require(renderer.SubmitScene(scene, desc), "Renderer did not submit texture reload frame");
    const SceneRenderSubmitStats textureReloadStats = renderer.LastSceneSubmitStats();
    Require(textureReloadStats.meshDrawCommandCacheMissCount == 1U, "KBMAT-0902: Changed material texture should invalidate cached draw command");
    Require(textureReloadStats.meshDrawCommandCacheBuildCount == 1U, "KBMAT-0902: Changed material texture should rebuild cached draw command");
    Require(textureReloadStats.meshDrawCommandCachePruneCount == 1U, "KBMAT-0902: Changed material texture should prune stale draw command");
    const RenderMaterialHandle textureReloadMaterialHandle = resourceMap->ResolveMaterial(materialAssetId);
    Require(textureReloadMaterialHandle == firstHandle, "KBMAT-0902: Texture-only reload should not reload the material handle");
    Require(resourceMap->ResolveMaterial(stableMaterialAssetId) == stableFirstHandle, "KBMAT-0903: Texture-only reload should not change unrelated material handle");
    const RenderTextureHandle secondTextureHandle = resourceMap->ResolveTexture(textureAssetId, RenderTextureColorSpace::Srgb);
    const RenderTextureResource* secondTexture = resources->FindTexture(secondTextureHandle);
    Require(secondTextureHandle.IsValid() && secondTexture != nullptr, "KBMAT-0902: Texture-only reload did not bind a live texture");
    Require(secondTextureHandle != firstTextureHandle, "KBMAT-0902: Texture-only reload reused the stale texture handle");
    Require(resources->FindTexture(firstTextureHandle) == nullptr, "KBMAT-0902: Texture-only reload kept stale texture handle resolvable");
    Require(secondTexture->version != firstTextureVersion, "KBMAT-0902: Texture-only reload did not receive a new texture version");
    renderer.EndFrame();

    {
        std::ofstream output{ materialPath, std::ios::trunc };
        output
            << "version 1\n"
            << "materialType builtin.pbr\n"
            << "materialTypeVersion 1\n"
            << "roughnessFactor broken\n";
    }
    Require(renderer.BeginFrame(), "Renderer did not begin steady-state material cache frame");
    Require(renderer.SubmitScene(scene, desc), "Renderer did not submit steady-state material cache frame");
    const SceneRenderSubmitStats steadySubmitStats = renderer.LastSceneSubmitStats();
    Require(steadySubmitStats.meshDrawCommandCacheHitCount == 1U, "Unchanged material metadata should reuse the cached draw command");
    Require(steadySubmitStats.meshDrawCommandCacheMissCount == 0U, "Unchanged material metadata should not rebuild draw command cache");
    const Renderer::RuntimeSceneResourceStats steadyRuntimeStats = renderer.RuntimeResourceStats();
    Require(steadyRuntimeStats.materialLoadedCount == 0U, "KBMAT-0907: Runtime steady-state should not load material resources in render frame");
    Require(steadyRuntimeStats.materialReloadCount == 0U, "KBMAT-0907: Runtime steady-state should not reload material resources in render frame");
    Require(steadyRuntimeStats.materialFallbackCount == 0U, "KBMAT-0907: Runtime steady-state should not create material fallbacks in render frame");
    Require(steadyRuntimeStats.materialErrorCount == 0U, "KBMAT-0907: Runtime steady-state should not hit material errors in render frame");
    const RenderMaterialHandle steadyHandle = resourceMap->ResolveMaterial(materialAssetId);
    const RenderMaterialResource* steadyMaterial = resources->FindMaterial(steadyHandle);
    Require(steadyHandle.IsValid() && steadyMaterial != nullptr, "Unchanged material metadata should keep a valid cached material resource");
    Require(steadyHandle == firstHandle, "Unchanged material metadata should keep the cached material handle");
    Require(steadyMaterial == firstMaterial, "Unchanged material metadata should keep the cached material resource");
    Require(NearlyEqual(steadyMaterial->baseColor[0], 0.2F), "Runtime steady-state should not reparse material files without registry rediscovery");
    Require(NearlyEqual(steadyMaterial->roughnessFactor, 0.7F), "Runtime steady-state should not replace cached material with an undiscovered file edit");
    renderer.EndFrame();

    Require(manager.DiscoverMountedAssets() >= 3U, "Runtime material reload test did not rediscover broken material asset");
    materialMetadata = manager.Registry().FindByPath("/Game/reloadable.kbmat");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Runtime material reload test lost broken material metadata after rediscovery");
    Require(materialMetadata->id.value == materialAssetId, "Runtime material reload test changed material asset id after broken rediscovery");
    const std::uint64_t brokenContentHash = materialMetadata->contentHash;
    Require(brokenContentHash != firstContentHash, "Runtime material reload test did not update broken material content hash");

    Require(renderer.BeginFrame(), "Renderer did not begin broken material reload frame");
    Require(renderer.SubmitScene(scene, desc), "Renderer did not submit broken material reload frame");
    const RenderMaterialHandle brokenHandle = resourceMap->ResolveMaterial(materialAssetId);
    const RenderMaterialResource* brokenMaterial = resources->FindMaterial(brokenHandle);
    Require(brokenHandle == firstHandle, "KBMAT-0810: Broken material reload should keep the last-good material handle");
    Require(brokenMaterial == firstMaterial, "KBMAT-0810: Broken material reload should keep the last-good material resource");
    Require(NearlyEqual(brokenMaterial->baseColor[0], 0.2F), "KBMAT-0810: Broken material reload replaced last-good base color");
    Require(NearlyEqual(brokenMaterial->roughnessFactor, 0.7F), "KBMAT-0810: Broken material reload replaced last-good roughness");
    const Renderer::RuntimeSceneResourceStats brokenRuntimeStats = renderer.RuntimeResourceStats();
    Require(brokenRuntimeStats.materialResolverDiagnosticCount >= 1U, "KBMAT-0810: Broken material reload should report resolver diagnostics while keeping last-good");
    Require(brokenRuntimeStats.errorMaterialFallbackCount == 0U, "KBMAT-0810: Broken material reload should not render the error material while last-good exists");
    Require(brokenRuntimeStats.materialLoadedCount == 0U, "KBMAT-0901: Broken material reload with last-good should not count a new material load");
    Require(brokenRuntimeStats.materialFallbackCount == 0U, "KBMAT-0901: Broken material reload with last-good should not count a rendered fallback");
    Require(brokenRuntimeStats.materialErrorCount == 1U, "KBMAT-0901: Broken material reload should count one material error");
    Require(brokenRuntimeStats.materialReloadCount == 0U, "KBMAT-0901: Broken material reload with last-good should not count a successful reload");
    renderer.EndFrame();

    WriteReloadableMaterial(materialPath, 0.9F, 0.35F, textureAssetId);
    Require(manager.DiscoverMountedAssets() >= 3U, "Runtime material reload test did not rediscover changed material asset");
    materialMetadata = manager.Registry().FindByPath("/Game/reloadable.kbmat");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Runtime material reload test lost material metadata after rediscovery");
    Require(materialMetadata->id.value == materialAssetId, "Runtime material reload test changed material asset id after rediscovery");
    Require(materialMetadata->contentHash != firstContentHash, "Runtime material reload test did not update material content hash");
    Require(materialMetadata->contentHash != brokenContentHash, "Runtime material reload test did not update fixed material content hash");

    Require(renderer.BeginFrame(), "Renderer did not begin second material reload frame");
    Require(renderer.SubmitScene(scene, desc), "Renderer did not submit second material reload frame");
    const SceneRenderSubmitStats secondSubmitStats = renderer.LastSceneSubmitStats();
    Require(secondSubmitStats.meshDrawCommandCacheMissCount == 1U, "Changed material did not invalidate draw command cache");
    Require(secondSubmitStats.meshDrawCommandCacheBuildCount == 1U, "Changed material did not rebuild draw command cache");
    Require(secondSubmitStats.meshDrawCommandCachePruneCount == 1U, "Changed material did not prune the stale draw command cache entry");

    const RenderMaterialHandle secondHandle = resourceMap->ResolveMaterial(materialAssetId);
    const RenderMaterialResource* secondMaterial = resources->FindMaterial(secondHandle);
    Require(secondHandle.IsValid() && secondMaterial != nullptr, "Runtime material reload test did not bind reloaded material resource");
    Require(secondHandle != firstHandle, "Runtime material reload test reused stale material handle after content change");
    Require(resourceMap->ResolveMaterial(stableMaterialAssetId) == stableFirstHandle, "KBMAT-0903: Changed material should not reload unrelated material handle");
    Require(resources->FindMaterial(firstHandle) == nullptr, "Runtime material reload test kept stale material handle resolvable");
    Require(secondMaterial->version != firstVersion, "Reloaded material resource did not receive a new version");
    Require(NearlyEqual(secondMaterial->baseColor[0], 0.9F), "Reloaded runtime material resource did not use changed asset color");
    Require(NearlyEqual(secondMaterial->roughnessFactor, 0.35F), "Reloaded runtime material resource did not use changed asset roughness");
    const Renderer::RuntimeSceneResourceStats reloadedRuntimeStats = renderer.RuntimeResourceStats();
    Require(reloadedRuntimeStats.cachedMaterialCount == 2U, "Runtime material reload test should keep exactly the changed and stable material resources after reload");
    Require(reloadedRuntimeStats.materialLoadedCount == 1U, "KBMAT-0901: Fixed material reload should count one loaded material");
    Require(reloadedRuntimeStats.materialFallbackCount == 0U, "KBMAT-0901: Fixed material reload should not count a fallback");
    Require(reloadedRuntimeStats.materialErrorCount == 0U, "KBMAT-0901: Fixed material reload should not count an error");
    Require(reloadedRuntimeStats.materialReloadCount == 1U, "KBMAT-0901: Fixed material reload should count one material reload");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunRendererEnsuresGraphProgramTextureResourcesTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_graph_texture_resources";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Graph texture resource test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path texturePath = root / "graph_specular.kbtex";
    const std::filesystem::path materialPath = root / "graph_texture_only.kbmat";
    WriteTriangleObj(meshPath);
    WriteTexture(texturePath, 220U, 64U, 32U);

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Graph texture resource test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Graph texture resource test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderTextureAssetLoader>()), "Graph texture resource test could not register texture loader");
    Require(manager.Mounts().Mount("Game", root), "Graph texture resource test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 2U, "Graph texture resource test did not discover mesh and texture assets");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* textureMetadata = manager.Registry().FindByPath("/Game/graph_specular.kbtex");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Graph texture resource test discovered wrong mesh metadata");
    Require(textureMetadata != nullptr && textureMetadata->type == "RenderTexture", "Graph texture resource test discovered wrong texture metadata");
    const std::uint64_t meshAssetId = meshMetadata->id.value;
    const std::uint64_t textureAssetId = textureMetadata->id.value;

    RenderMaterialAssetData material{};
    material.graph = MakeDefaultRenderMaterialGraphDocument();
    material.graph.nodes.push_back(MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantColor, {}, "0.18 0.24 0.32 1"));
    RenderMaterialGraphNode textureParameter = MakeGraphNode(3U, RenderMaterialGraphNodeKind::ParameterTexture, "graphSpecularTexture");
    textureParameter.parameter.textureRole = "baseColor";
    textureParameter.parameter.expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb;
    material.graph.nodes.push_back(std::move(textureParameter));
    material.graph.nodes.push_back(MakeGraphNode(4U, RenderMaterialGraphNodeKind::TextureSample));
    material.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ParameterTexture, 3U, "texture", RenderMaterialGraphNodeKind::TextureSample, 4U, "texture"),
        MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 4U, "r", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "specular"),
    };
    material.graphParameterValues.push_back(MakeTextureGraphValue("graphSpecularTexture", textureAssetId));
    Require(RenderMaterialAssetWriter::Save(materialPath, material), "Graph texture resource test could not save graph material");
    Require(manager.DiscoverMountedAssets() >= 3U, "Graph texture resource test did not discover graph material asset");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/graph_texture_only.kbmat");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Graph texture resource test discovered wrong material metadata");
    const std::uint64_t materialAssetId = materialMetadata->id.value;

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Graph Texture Resource Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshAssetId,
        .materialAssetId = materialAssetId,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 1U,
        .cachedMaterials = 1U,
        .cachedTextures = 1U,
        .frameReferencedMeshes = 1U,
        .frameReferencedMaterials = 1U,
        .frameReferencedTextures = 1U,
        .scenePassSubmitStats = 1U,
        .renderSceneMeshProxies = 1U,
        .renderSceneDrawGroupKeys = 1U,
        .meshResourceSlots = 1U,
        .materialResourceSlots = 1U,
        .textureResourceSlots = 1U,
        .meshBindings = 1U,
        .materialBindings = 1U,
        .textureBindings = 1U,
        .syncMeshProxies = 1U,
        .syncTransformCacheEntries = 1U,
        .syncTransformResolvingEntries = 1U,
    });
    Require(renderer.Initialize(surface, &config), "Graph texture resource test renderer did not initialize");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 2U,
            .maxVisibleInstances = 2U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
    };

    Require(renderer.BeginFrame(), "Graph texture resource test did not begin frame");
    Require(renderer.SubmitScene(scene, desc), "Graph texture resource test did not submit scene");
    const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const RenderResourceRegistry* resources = renderer.SceneResources();
    Require(resourceMap != nullptr && resources != nullptr, "Graph texture resource test could not inspect scene resources");
    const RenderMaterialHandle materialHandle = resourceMap->ResolveMaterial(materialAssetId);
    const RenderMaterialResource* materialResource = resources->FindMaterial(materialHandle);
    Require(materialHandle.IsValid() && materialResource != nullptr, "Graph texture resource test did not bind material resource");
    Require(materialResource->albedoTextureAssetId == 0U,
        "KBMAT-MAT99-16: graph-only texture test must not pass through the legacy albedo texture slot");
    const auto graphTextureIt = std::find_if(
        materialResource->graphProgram.textures.begin(),
        materialResource->graphProgram.textures.end(),
        [textureAssetId](const RenderMaterialGraphTextureBinding& binding) {
            return binding.stableId == "graphSpecularTexture" && binding.textureAssetId == textureAssetId;
        });
    Require(graphTextureIt != materialResource->graphProgram.textures.end(),
        "KBMAT-MAT99-16: graph material did not expose the authored texture parameter binding");
    const RenderTextureHandle textureHandle = resourceMap->ResolveTexture(textureAssetId, graphTextureIt->colorSpace);
    const RenderTextureResource* textureResource = resources->FindTexture(textureHandle);
    Require(textureHandle.IsValid() && textureResource != nullptr,
        "KBMAT-MAT99-16: runtime submit must ensure textures referenced only by graphProgram.textures");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

#if defined(KB_TEST_GRAPH_SHADERC_PATH)
void RunRendererPreservesGraphTextureDimensionsThroughProductionPipelineTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_graph_texture_dimensions";
    const std::filesystem::path cacheRoot = root / "graph_shaders";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Graph texture dimension test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    WriteTriangleObj(meshPath);
    WriteDimensionTexture(root / "cube.kbtex", RenderTextureDimension::TextureCube);
    WriteDimensionTexture(root / "volume.kbtex", RenderTextureDimension::Texture3D, 3U);
    WriteDimensionTexture(root / "array.kbtex", RenderTextureDimension::Texture2DArray, 1U, 3U);
    WriteDimensionTexture(root / "flat.kbtex", RenderTextureDimension::Texture2D);

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Graph texture dimension test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Graph texture dimension test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderTextureAssetLoader>()), "Graph texture dimension test could not register texture loader");
    Require(manager.Mounts().Mount("Game", root), "Graph texture dimension test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 5U, "Graph texture dimension test did not discover mesh and texture assets");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* cubeMetadata = manager.Registry().FindByPath("/Game/cube.kbtex");
    const kb::assets::AssetMetadata* volumeMetadata = manager.Registry().FindByPath("/Game/volume.kbtex");
    const kb::assets::AssetMetadata* arrayMetadata = manager.Registry().FindByPath("/Game/array.kbtex");
    const kb::assets::AssetMetadata* flatMetadata = manager.Registry().FindByPath("/Game/flat.kbtex");
    Require(meshMetadata != nullptr && cubeMetadata != nullptr && volumeMetadata != nullptr &&
            arrayMetadata != nullptr && flatMetadata != nullptr,
        "Graph texture dimension test could not resolve discovered asset metadata");
    const std::uint64_t meshAssetId = meshMetadata->id.value;
    const std::uint64_t cubeAssetId = cubeMetadata->id.value;
    const std::uint64_t volumeAssetId = volumeMetadata->id.value;
    const std::uint64_t arrayAssetId = arrayMetadata->id.value;
    const std::uint64_t flatAssetId = flatMetadata->id.value;

    struct MaterialCase {
        const char* filename;
        const char* virtualPath;
        RenderMaterialGraphNodeKind sampleKind;
        const char* stableId;
        std::uint64_t textureAssetId;
        RenderTextureDimension expectedDimension;
        bool expectMismatch;
        std::uint64_t materialAssetId = 0U;
    };
    std::array materialCases{
        // The first stable id intentionally aliases a legacy PBR field. A Cube graph parameter
        // must still remain graph-only instead of being mirrored into the sampler2D slot.
        MaterialCase{ "cube_material.kbmat", "/Game/cube_material.kbmat", RenderMaterialGraphNodeKind::TextureSampleCube, "occlusionTexture", cubeAssetId, RenderTextureDimension::TextureCube, false },
        MaterialCase{ "volume_material.kbmat", "/Game/volume_material.kbmat", RenderMaterialGraphNodeKind::TextureSampleVolume, "volumeTex", volumeAssetId, RenderTextureDimension::Texture3D, false },
        MaterialCase{ "array_material.kbmat", "/Game/array_material.kbmat", RenderMaterialGraphNodeKind::TextureSample2DArray, "arrayTex", arrayAssetId, RenderTextureDimension::Texture2DArray, false },
        MaterialCase{ "mismatch_material.kbmat", "/Game/mismatch_material.kbmat", RenderMaterialGraphNodeKind::TextureSampleCube, "mismatchCubeTex", flatAssetId, RenderTextureDimension::TextureCube, true },
    };

    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };
    std::uint64_t compileAssetId = 0xD140U;
    for (const MaterialCase& materialCase : materialCases) {
        const RenderMaterialAssetData material = MakeDimensionGraphMaterial(
            materialCase.sampleKind,
            materialCase.stableId,
            materialCase.textureAssetId);
        const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(
            material.graph,
            RenderMaterialGraphBuildContext{ .assetId = compileAssetId++ });
        if (!compiled.Succeeded()) {
            std::cerr << "Graph texture dimension compile failed for " << materialCase.stableId << ':\n';
            for (const RenderMaterialGraphDiagnostic& diagnostic : compiled.diagnostics) {
                std::cerr << "  " << diagnostic.message << '\n';
            }
        }
        Require(compiled.Succeeded() && compiled.shader.reflection.textures.size() == 1U &&
                compiled.shader.reflection.textures[0].dimension == materialCase.expectedDimension,
            "Graph texture dimension test graph did not compile with the expected sampler dimension");
        RenderMaterialGraphShaderArtifactRequest request{};
        request.shadercPath = KB_TEST_GRAPH_SHADERC_PATH;
        request.varyingDefPath = KB_TEST_GRAPH_SHADER_VARYING_DEF;
        request.includeDirs = { KB_TEST_GRAPH_SHADER_INCLUDE_DIR, KB_TEST_GRAPH_BGFX_SHADER_INCLUDE_DIR };
        request.cacheRoot = cacheRoot.generic_string();
        request.pass = "BaseOpaque";
        request.shaderPlatform = kb::assets::bake::ShaderBakePlatform::Windows;
        Require(CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, request).Succeeded(),
            "Graph texture dimension test could not cook the production graph program");
        Require(RenderMaterialAssetWriter::Save(root / materialCase.filename, material),
            "Graph texture dimension test could not save material asset");
    }

    Require(manager.DiscoverMountedAssets() >= 9U, "Graph texture dimension test did not discover material assets");
    for (MaterialCase& materialCase : materialCases) {
        const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath(materialCase.virtualPath);
        Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial",
            "Graph texture dimension test discovered wrong material metadata");
        materialCase.materialAssetId = materialMetadata->id.value;
        const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
            .name = materialCase.stableId,
            .transform = TransformAt(static_cast<float>(materialCase.materialAssetId & 3U) * 0.2F, 0.0F, 0.0F),
        });
        scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
            .meshAssetId = meshAssetId,
            .materialAssetId = materialCase.materialAssetId,
        });
    }

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Graph texture dimension test renderer did not initialize");
    renderer.SetGraphShaderCacheRoot(cacheRoot.generic_string());

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 8U,
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
    };
    Require(renderer.BeginFrame(), "Graph texture dimension test did not begin frame");
    Require(renderer.SubmitScene(scene, desc), "Graph texture dimension test did not submit scene");

    const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const RenderResourceRegistry* resources = renderer.SceneResources();
    Require(resourceMap != nullptr && resources != nullptr, "Graph texture dimension test could not inspect production resources");
    for (const MaterialCase& materialCase : materialCases) {
        const RenderMaterialResource* material = resources->FindMaterial(resourceMap->ResolveMaterial(materialCase.materialAssetId));
        Require(material != nullptr && material->graphProgram.textures.size() == 1U,
            "Graph texture dimension test did not build the graph material binding");
        Require(material->occlusionTextureAssetId == 0U,
            "Non-2D graph texture was incorrectly mirrored into the legacy sampler2D PBR slot");
        const RenderMaterialGraphTextureBinding& binding = material->graphProgram.textures[0];
        Require(binding.dimension == materialCase.expectedDimension,
            "Graph texture dimension was lost before runtime binding");
        const RenderTextureResource* texture = resources->FindTexture(
            resourceMap->ResolveTexture(materialCase.textureAssetId, binding.colorSpace));
        const RenderTextureDimension expectedResourceDimension = materialCase.expectMismatch
            ? RenderTextureDimension::Texture2D
            : materialCase.expectedDimension;
        Require(texture != nullptr && texture->dimension == expectedResourceDimension,
            "Production graph texture resource did not preserve its asset dimension");
    }

    // Re-check resources with the authored asset dimension, including depth/layer metadata.
    const RenderTextureResource* cubeResource = resources->FindTexture(resourceMap->ResolveTexture(cubeAssetId, RenderTextureColorSpace::Linear));
    const RenderTextureResource* volumeResource = resources->FindTexture(resourceMap->ResolveTexture(volumeAssetId, RenderTextureColorSpace::Linear));
    const RenderTextureResource* arrayResource = resources->FindTexture(resourceMap->ResolveTexture(arrayAssetId, RenderTextureColorSpace::Linear));
    const RenderTextureResource* flatResource = resources->FindTexture(resourceMap->ResolveTexture(flatAssetId, RenderTextureColorSpace::Linear));
    Require(cubeResource != nullptr && cubeResource->dimension == RenderTextureDimension::TextureCube && cubeResource->layers == 1U,
        "Production resource pipeline did not create a cube texture resource");
    Require(volumeResource != nullptr && volumeResource->dimension == RenderTextureDimension::Texture3D && volumeResource->depth == 3U,
        "Production resource pipeline did not create a 3D texture resource");
    Require(arrayResource != nullptr && arrayResource->dimension == RenderTextureDimension::Texture2DArray && arrayResource->layers == 3U,
        "Production resource pipeline did not create a 2D array texture resource");
    Require(flatResource != nullptr && flatResource->dimension == RenderTextureDimension::Texture2D &&
            flatResource->mipCount == 2U,
        "Production resource pipeline did not create a complete mip chain for the 2D texture");

    Require(renderer.LastSceneSubmitStats().textureDimensionMismatchCount == 1U,
        "Production submit must count exactly the authored 2D-to-Cube mismatch");
    std::uint32_t mismatchDiagnosticCount = 0U;
    for (const SceneRenderDiagnosticEvent& event : renderer.LastSceneDiagnostics().events) {
        if (event.kind != SceneRenderDiagnosticKind::TextureDimensionMismatch) {
            continue;
        }
        ++mismatchDiagnosticCount;
        Require(event.textureAssetId == flatAssetId &&
                event.expectedTextureDimension == RenderTextureDimension::TextureCube &&
                event.actualTextureDimension == RenderTextureDimension::Texture2D &&
                event.fallbackTextureDimension == RenderTextureDimension::TextureCube,
            "Texture dimension mismatch diagnostic did not identify the asset, actual sampler type and Cube fallback");
    }
    Require(mismatchDiagnosticCount == 1U,
        "Production submit must emit one deterministic texture dimension mismatch diagnostic");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}
#endif

void RunRendererRoutesGraphBlendModeToTransparentPassTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_graph_blend_pass";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Graph blend pass test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path materialPath = root / "graph_additive.kbmat";
    WriteTriangleObj(meshPath);

    RenderMaterialAssetData material{};
    material.graph = MakeDefaultRenderMaterialGraphDocument();
    material.graph.blendMode = "additive";
    material.graph.nodes.push_back(MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantColor, {}, "0.8 0.2 0.1 0.5"));
    material.graph.nodes.push_back(MakeGraphNode(3U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"));
    material.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 3U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"),
    };
    Require(RenderMaterialAssetWriter::Save(materialPath, material), "Graph blend pass test could not save additive graph material");

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Graph blend pass test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Graph blend pass test could not register material loader");
    Require(manager.Mounts().Mount("Game", root), "Graph blend pass test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 2U, "Graph blend pass test did not discover mesh/material assets");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/graph_additive.kbmat");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Graph blend pass test discovered wrong mesh metadata");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Graph blend pass test discovered wrong material metadata");
    const std::uint64_t materialAssetId = materialMetadata->id.value;

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Graph Additive Material Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialAssetId = materialAssetId,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 1U,
        .cachedMaterials = 1U,
        .frameReferencedMeshes = 1U,
        .frameReferencedMaterials = 1U,
        .scenePassSubmitStats = 2U,
        .renderSceneMeshProxies = 1U,
        .renderSceneDrawGroupKeys = 1U,
        .meshResourceSlots = 1U,
        .materialResourceSlots = 1U,
        .meshBindings = 1U,
        .materialBindings = 1U,
        .syncMeshProxies = 1U,
        .syncTransformCacheEntries = 1U,
        .syncTransformResolvingEntries = 1U,
    });
    Require(renderer.Initialize(surface, &config), "Graph blend pass test renderer did not initialize");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 2U,
            .maxVisibleInstances = 2U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueAndTransparent,
        .shadowPassEnabled = false,
    };

    Require(renderer.BeginFrame(), "Graph blend pass test did not begin frame");
    Require(renderer.SubmitScene(scene, desc), "Graph blend pass test did not submit scene");
    const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const RenderResourceRegistry* resources = renderer.SceneResources();
    Require(resourceMap != nullptr && resources != nullptr, "Graph blend pass test could not inspect scene resources");
    const RenderMaterialHandle materialHandle = resourceMap->ResolveMaterial(materialAssetId);
    const RenderMaterialResource* materialResource = resources->FindMaterial(materialHandle);
    Require(materialHandle.IsValid() && materialResource != nullptr, "Graph blend pass test did not bind material resource");
    Require(materialResource->graphProgram.alphaMode == RenderMaterialAlphaMode::Blend &&
            materialResource->alphaMode == RenderMaterialAlphaMode::Blend,
        "KBMAT-MAT99-25: graph blend mode must drive material alpha mode used by the scene pipeline");
    Require(materialResource->graphProgram.translucencyBlend == RenderMaterialTranslucencyBlend::Additive &&
            materialResource->translucencyBlend == RenderMaterialTranslucencyBlend::Additive,
        "KBMAT-MAT99-25: graph additive blend mode must drive the scene translucency blend state");
    const std::span<const SceneRenderPassSubmitStats> passStats = renderer.LastScenePassSubmitStats();
    Require(passStats.size() == 2U, "KBMAT-MAT99-25: graph blend pass test should report opaque and transparent passes");
    Require(passStats[0].pass == MeshPassType::BaseOpaque && passStats[0].stats.submittedMeshCount == 0U,
        "KBMAT-MAT99-25: additive graph material must not submit in the opaque scene pass");
    Require(passStats[1].pass == MeshPassType::BaseTransparent && passStats[1].stats.submittedMeshCount == 1U &&
            passStats[1].stats.submittedDrawCallCount == 1U,
        "KBMAT-MAT99-25: additive graph material must submit in the transparent scene pass");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunGraphBackedMaterialArtifactDependencyReloadInvalidatesOnlyTouchedBindingTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_graph_material_artifact_reload";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Graph material artifact reload test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path typePath = root / "BuiltinPbr.kbmaterialtype";
    const std::filesystem::path artifactPath = root / "GraphArtifact.kbmaterialgraph";
    const std::filesystem::path materialPath = root / "graph_backed.kbmat";
    const std::filesystem::path stableMaterialPath = root / "stable.kbmat";
    WriteTriangleObj(meshPath);
    Require(RenderMaterialTypeAssetLoader::SaveType(typePath, GetBuiltInPbrMaterialTypeDocument()), "KBMAT-GRAPH-0405: could not write material type fixture");
    RenderMaterialGraphDocument artifact = MakeDefaultRenderMaterialGraphDocument();
    Require(RenderMaterialGraphAssetLoader::SaveGraph(artifactPath, artifact), "KBMAT-GRAPH-0405: could not write graph artifact fixture");

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Graph material artifact reload test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Graph material artifact reload test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialGraphAssetLoader>()), "Graph material artifact reload test could not register material graph loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialTypeAssetLoader>()), "Graph material artifact reload test could not register material type loader");
    Require(manager.Mounts().Mount("Game", root), "Graph material artifact reload test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 3U, "Graph material artifact reload test did not discover mesh/type/artifact assets");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* typeMetadata = manager.Registry().FindByPath("/Game/BuiltinPbr.kbmaterialtype");
    const kb::assets::AssetMetadata* artifactMetadata = manager.Registry().FindByPath("/Game/GraphArtifact.kbmaterialgraph");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Graph material artifact reload test discovered wrong mesh metadata");
    Require(typeMetadata != nullptr && typeMetadata->type == kRenderMaterialTypeAssetType, "Graph material artifact reload test discovered wrong material type metadata");
    Require(artifactMetadata != nullptr && artifactMetadata->type == kRenderMaterialGraphAssetType, "Graph material artifact reload test discovered wrong artifact metadata");
    const std::uint64_t meshAssetId = meshMetadata->id.value;
    const kb::assets::AssetId artifactAssetId = artifactMetadata->id;
    const std::uint64_t firstArtifactContentHash = artifactMetadata->contentHash;

    WriteGraphBackedReloadableMaterial(
        materialPath,
        0.25F,
        0.55F,
        typeMetadata->id.value,
        "/Game/BuiltinPbr.kbmaterialtype",
        artifactAssetId.value,
        firstArtifactContentHash);
    WriteReloadableMaterial(stableMaterialPath, 0.75F, 0.3F, 0U);
    Require(manager.DiscoverMountedAssets() >= 5U, "Graph material artifact reload test did not discover material assets");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/graph_backed.kbmat");
    const kb::assets::AssetMetadata* stableMaterialMetadata = manager.Registry().FindByPath("/Game/stable.kbmat");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Graph material artifact reload test lost graph material metadata");
    Require(stableMaterialMetadata != nullptr && stableMaterialMetadata->type == "RenderMaterial", "Graph material artifact reload test lost stable material metadata");
    const std::uint64_t materialAssetId = materialMetadata->id.value;
    const std::uint64_t stableMaterialAssetId = stableMaterialMetadata->id.value;
    Require(ContainsAssetDependency(materialMetadata->dependencies, artifactAssetId),
        "KBMAT-GRAPH-0405: graph-backed material metadata did not depend on its compile artifact");

    const kb::scene::SceneEntity graphEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Graph Artifact Material Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(graphEntity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshAssetId,
        .materialAssetId = materialAssetId,
    });
    const kb::scene::SceneEntity stableEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Stable Material Mesh",
        .transform = TransformAt(0.25F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(stableEntity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshAssetId,
        .materialAssetId = stableMaterialAssetId,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 1U,
        .cachedMaterials = 2U,
        .frameReferencedMeshes = 2U,
        .frameReferencedMaterials = 2U,
        .scenePassSubmitStats = 1U,
        .renderSceneMeshProxies = 2U,
        .renderSceneDrawGroupKeys = 2U,
        .meshResourceSlots = 1U,
        .materialResourceSlots = 2U,
        .meshBindings = 1U,
        .materialBindings = 2U,
        .syncMeshProxies = 2U,
        .syncTransformCacheEntries = 2U,
        .syncTransformResolvingEntries = 2U,
    });
    Require(renderer.Initialize(surface, &config), "Graph material artifact reload test renderer did not initialize");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 4U,
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
    };

    Require(renderer.BeginFrame(), "Graph material artifact reload test did not begin first frame");
    Require(renderer.SubmitScene(scene, desc), "Graph material artifact reload test did not submit first frame");
    const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const RenderResourceRegistry* resources = renderer.SceneResources();
    Require(resourceMap != nullptr && resources != nullptr, "Graph material artifact reload test could not inspect resource map");
    const RenderMaterialHandle firstGraphHandle = resourceMap->ResolveMaterial(materialAssetId);
    const RenderMaterialHandle firstStableHandle = resourceMap->ResolveMaterial(stableMaterialAssetId);
    const RenderMaterialResource* firstGraphMaterial = resources->FindMaterial(firstGraphHandle);
    Require(firstGraphHandle.IsValid() && firstStableHandle.IsValid() && firstGraphMaterial != nullptr,
        "KBMAT-GRAPH-0405: first submit did not bind graph/stable material resources");
    Require(NearlyEqual(firstGraphMaterial->baseColor[0], 0.25F), "KBMAT-GRAPH-0405: graph material fixture did not resolve initial base color");
    Require(NearlyEqual(firstGraphMaterial->roughnessFactor, 0.55F), "KBMAT-GRAPH-0405: graph material fixture did not resolve initial roughness parameter");
    renderer.EndFrame();

    artifact.nodes.push_back(RenderMaterialGraphNode{
        .id = 77U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 240,
        .positionY = 80,
    });
    Require(RenderMaterialGraphAssetLoader::SaveGraph(artifactPath, artifact), "KBMAT-GRAPH-0405: could not update graph artifact fixture");
    Require(manager.DiscoverMountedAssets() >= 5U, "Graph material artifact reload test did not rediscover changed artifact");
    artifactMetadata = manager.Registry().FindByPath("/Game/GraphArtifact.kbmaterialgraph");
    Require(artifactMetadata != nullptr && artifactMetadata->contentHash != 0U, "KBMAT-GRAPH-0405: changed artifact metadata was not current");
    Require(artifactMetadata->contentHash != firstArtifactContentHash, "KBMAT-GRAPH-0405: changed artifact did not update its metadata content hash");

    Require(renderer.BeginFrame(), "Graph material artifact reload test did not begin artifact reload frame");
    Require(renderer.SubmitScene(scene, desc), "Graph material artifact reload test did not submit artifact reload frame");
    const RenderMaterialHandle secondGraphHandle = resourceMap->ResolveMaterial(materialAssetId);
    const RenderMaterialHandle secondStableHandle = resourceMap->ResolveMaterial(stableMaterialAssetId);
    const RenderMaterialResource* secondGraphMaterial = resources->FindMaterial(secondGraphHandle);
    Require(secondGraphHandle.IsValid() && secondGraphMaterial != nullptr, "KBMAT-GRAPH-0405: graph material was not rebound after artifact change");
    Require(secondGraphHandle != firstGraphHandle, "KBMAT-GRAPH-0405: graph artifact change did not reload the graph-backed material binding");
    Require(secondStableHandle == firstStableHandle, "KBMAT-GRAPH-0405: graph artifact change reloaded an unrelated stable material binding");
    Require(resources->FindMaterial(firstGraphHandle) == nullptr, "KBMAT-GRAPH-0405: stale graph material handle remained live after artifact change");
    Require(NearlyEqual(secondGraphMaterial->baseColor[0], 0.25F), "KBMAT-GRAPH-0405: graph artifact reload changed material parameters unexpectedly");
    Require(NearlyEqual(secondGraphMaterial->roughnessFactor, 0.55F), "KBMAT-GRAPH-0405: graph artifact reload lost material scalar parameters");
    const Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
    Require(runtimeStats.materialReloadCount == 1U && runtimeStats.cachedMaterialCount == 2U,
        "KBMAT-GRAPH-0405: artifact change should count one material reload and keep only graph/stable resources cached");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunCookedGraphBackedMaterialRuntimeDoesNotCompileGraphTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_graph_material_no_runtime_compile";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Cooked graph material no-compile test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path typePath = root / "BuiltinPbr.kbmaterialtype";
    const std::filesystem::path artifactPath = root / "GraphArtifact.kbmaterialgraph";
    const std::filesystem::path materialPath = root / "cooked_graph.kbmat";
    WriteTriangleObj(meshPath);
    Require(RenderMaterialTypeAssetLoader::SaveType(typePath, GetBuiltInPbrMaterialTypeDocument()), "KBMAT-GRAPH-0505: could not write material type fixture");
    RenderMaterialGraphDocument artifact = MakeDefaultRenderMaterialGraphDocument();
    Require(RenderMaterialGraphAssetLoader::SaveGraph(artifactPath, artifact), "KBMAT-GRAPH-0505: could not write graph artifact fixture");

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Cooked graph material no-compile test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Cooked graph material no-compile test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialGraphAssetLoader>()), "Cooked graph material no-compile test could not register graph loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialTypeAssetLoader>()), "Cooked graph material no-compile test could not register type loader");
    Require(manager.Mounts().Mount("Game", root), "Cooked graph material no-compile test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 3U, "Cooked graph material no-compile test did not discover mesh/type/artifact assets");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* typeMetadata = manager.Registry().FindByPath("/Game/BuiltinPbr.kbmaterialtype");
    const kb::assets::AssetMetadata* artifactMetadata = manager.Registry().FindByPath("/Game/GraphArtifact.kbmaterialgraph");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Cooked graph material no-compile test discovered wrong mesh metadata");
    Require(typeMetadata != nullptr && typeMetadata->type == kRenderMaterialTypeAssetType, "Cooked graph material no-compile test discovered wrong material type metadata");
    Require(artifactMetadata != nullptr && artifactMetadata->type == kRenderMaterialGraphAssetType, "Cooked graph material no-compile test discovered wrong artifact metadata");
    const std::uint64_t meshAssetId = meshMetadata->id.value;
    const std::uint64_t typeAssetId = typeMetadata->id.value;
    const kb::assets::AssetId artifactAssetId = artifactMetadata->id;
    const std::uint64_t artifactContentHash = artifactMetadata->contentHash;

    WriteGraphBackedReloadableMaterial(
        materialPath,
        0.31F,
        0.47F,
        typeAssetId,
        "/Game/BuiltinPbr.kbmaterialtype",
        artifactAssetId.value,
        artifactContentHash);
    Require(manager.DiscoverMountedAssets() >= 4U, "Cooked graph material no-compile test did not discover cooked material");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/cooked_graph.kbmat");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Cooked graph material no-compile test lost material metadata");
    Require(ContainsAssetDependency(materialMetadata->dependencies, artifactAssetId),
        "KBMAT-GRAPH-0505: cooked graph material should depend on its last-good artifact");

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Cooked Graph Material Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshAssetId,
        .materialAssetId = materialMetadata->id.value,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 1U,
        .cachedMaterials = 1U,
        .frameReferencedMeshes = 1U,
        .frameReferencedMaterials = 1U,
        .scenePassSubmitStats = 1U,
        .renderSceneMeshProxies = 1U,
        .renderSceneDrawGroupKeys = 1U,
        .meshResourceSlots = 1U,
        .materialResourceSlots = 1U,
        .meshBindings = 1U,
        .materialBindings = 1U,
        .syncMeshProxies = 1U,
        .syncTransformCacheEntries = 1U,
        .syncTransformResolvingEntries = 1U,
    });
    Require(renderer.Initialize(surface, &config), "Cooked graph material no-compile test renderer did not initialize");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 4U,
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
    };

    const std::uint64_t compileCountBeforeRuntime = RenderMaterialGraphCompileInvocationCount();
    Require(renderer.BeginFrame(), "Cooked graph material no-compile test did not begin first frame");
    Require(renderer.SubmitScene(scene, desc), "Cooked graph material no-compile test did not submit first frame");
    Require(RenderMaterialGraphCompileInvocationCount() == compileCountBeforeRuntime,
        "KBMAT-GRAPH-0505: runtime initial submit compiled a cooked graph-backed material graph");
    const SceneRenderSubmitStats firstSubmitStats = renderer.LastSceneSubmitStats();
    if (firstSubmitStats.submittedMeshCount != 1U || firstSubmitStats.submittedDrawCallCount != 1U || firstSubmitStats.HasMissingResources()) {
        const Renderer::RuntimeSceneResourceStats firstRuntimeStats = renderer.RuntimeResourceStats();
        std::cerr
            << "KBMAT-GRAPH-0505 first frame stats: visible=" << firstSubmitStats.visibleMeshCount
            << " submittedMesh=" << firstSubmitStats.submittedMeshCount
            << " drawCalls=" << firstSubmitStats.submittedDrawCallCount
            << " missingMeshBinding=" << firstSubmitStats.missingMeshBindingCount
            << " missingMeshResource=" << firstSubmitStats.missingMeshResourceCount
            << " missingMaterialBinding=" << firstSubmitStats.missingMaterialBindingCount
            << " missingMaterialResource=" << firstSubmitStats.missingMaterialResourceCount
            << " materialLoaded=" << firstRuntimeStats.materialLoadedCount
            << " materialErrors=" << firstRuntimeStats.materialErrorCount
            << " materialDiagnostics=" << firstRuntimeStats.materialResolverDiagnosticCount << '\n';
    }
    Require(firstSubmitStats.submittedMeshCount == 1U && firstSubmitStats.submittedDrawCallCount == 1U && !firstSubmitStats.HasMissingResources(),
        "KBMAT-GRAPH-0505: first cooked graph frame should submit one complete draw call");
    const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const RenderResourceRegistry* resources = renderer.SceneResources();
    Require(resourceMap != nullptr && resources != nullptr, "KBMAT-GRAPH-0505: first cooked graph frame could not inspect resources");
    const RenderMaterialHandle firstHandle = resourceMap->ResolveMaterial(materialMetadata->id.value);
    const RenderMaterialResource* firstMaterial = resources->FindMaterial(firstHandle);
    Require(firstHandle.IsValid() && firstMaterial != nullptr && NearlyEqual(firstMaterial->baseColor[0], 0.31F),
        "KBMAT-GRAPH-0505: first cooked graph frame did not bind the authored graph-backed material");
    Require(NearlyEqual(firstMaterial->roughnessFactor, 0.47F),
        "KBMAT-GRAPH-0505: first cooked graph frame did not bind the authored graph scalar parameter");
    renderer.EndFrame();

    Require(renderer.BeginFrame(), "Cooked graph material no-compile test did not begin steady frame");
    Require(renderer.SubmitScene(scene, desc), "Cooked graph material no-compile test did not submit steady frame");
    Require(RenderMaterialGraphCompileInvocationCount() == compileCountBeforeRuntime,
        "KBMAT-GRAPH-0505: runtime steady frame compiled a cooked graph-backed material graph");
    const SceneRenderSubmitStats steadySubmitStats = renderer.LastSceneSubmitStats();
    const Renderer::RuntimeSceneResourceStats steadyRuntimeStats = renderer.RuntimeResourceStats();
    Require(steadySubmitStats.meshDrawCommandCacheHitCount == 1U &&
            steadySubmitStats.meshDrawCommandCacheMissCount == 0U &&
            steadySubmitStats.meshDrawCommandCacheBuildCount == 0U &&
            steadySubmitStats.meshDrawCommandCachePruneCount == 0U,
        "KBMAT-GRAPH-0505: cooked graph steady frame should reuse cached draw command without pipeline rebuild");
    Require(steadyRuntimeStats.materialLoadedCount == 0U &&
            steadyRuntimeStats.materialReloadCount == 0U &&
            steadyRuntimeStats.materialErrorCount == 0U,
        "KBMAT-GRAPH-0505: cooked graph steady frame should not reload or error material resources");
    renderer.EndFrame();

    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunInvalidGraphMaterialUsesLastGoodThenRefreshesAfterFixTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_graph_material_invalid_fix";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Invalid graph material reload test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path materialPath = root / "graph_invalidatable.kbmat";
    WriteTriangleObj(meshPath);
    WriteGraphValidationMaterial(materialPath, 0.2F, true);

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Invalid graph material reload test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Invalid graph material reload test could not register material loader");
    Require(manager.Mounts().Mount("Game", root), "Invalid graph material reload test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 2U, "Invalid graph material reload test did not discover mesh/material assets");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/graph_invalidatable.kbmat");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Invalid graph material reload test discovered wrong mesh metadata");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Invalid graph material reload test discovered wrong material metadata");
    const std::uint64_t meshAssetId = meshMetadata->id.value;
    const std::uint64_t materialAssetId = materialMetadata->id.value;

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Invalid Graph Material Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshAssetId,
        .materialAssetId = materialAssetId,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 1U,
        .cachedMaterials = 1U,
        .frameReferencedMeshes = 1U,
        .frameReferencedMaterials = 1U,
        .scenePassSubmitStats = 1U,
        .renderSceneMeshProxies = 1U,
        .renderSceneDrawGroupKeys = 1U,
        .meshResourceSlots = 1U,
        .materialResourceSlots = 1U,
        .meshBindings = 1U,
        .materialBindings = 1U,
        .syncMeshProxies = 1U,
        .syncTransformCacheEntries = 2U,
        .syncTransformResolvingEntries = 2U,
    });
    Require(renderer.Initialize(surface, &config), "Invalid graph material reload test renderer did not initialize");
    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 4U,
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
    };

    Require(renderer.BeginFrame(), "Invalid graph material reload test did not begin first frame");
    Require(renderer.SubmitScene(scene, desc), "Invalid graph material reload test did not submit first frame");
    const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const RenderResourceRegistry* resources = renderer.SceneResources();
    Require(resourceMap != nullptr && resources != nullptr, "Invalid graph material reload test could not inspect resources");
    const RenderMaterialHandle firstHandle = resourceMap->ResolveMaterial(materialAssetId);
    const RenderMaterialResource* firstMaterial = resources->FindMaterial(firstHandle);
    Require(firstHandle.IsValid() && firstMaterial != nullptr, "KBMAT-GRAPH-0502: valid graph material did not bind before invalid edit");
    Require(NearlyEqual(firstMaterial->baseColor[0], 0.2F), "KBMAT-GRAPH-0502: valid graph material did not preserve initial base color");
    renderer.EndFrame();

    WriteGraphValidationMaterial(materialPath, 0.9F, false);
    Require(manager.DiscoverMountedAssets() >= 2U, "Invalid graph material reload test did not rediscover invalid material");
    Require(renderer.BeginFrame(), "Invalid graph material reload test did not begin invalid frame");
    Require(renderer.SubmitScene(scene, desc), "Invalid graph material reload test did not submit invalid frame");
    const RenderMaterialHandle invalidHandle = resourceMap->ResolveMaterial(materialAssetId);
    const RenderMaterialResource* invalidMaterial = resources->FindMaterial(invalidHandle);
    Require(invalidHandle == firstHandle && invalidMaterial == firstMaterial,
        "KBMAT-GRAPH-0502: invalid graph should keep last-good material handle when one exists");
    const Renderer::RuntimeSceneResourceStats invalidStats = renderer.RuntimeResourceStats();
    Require(invalidStats.materialErrorCount == 1U && invalidStats.materialResolverDiagnosticCount >= 1U,
        "KBMAT-GRAPH-0502: invalid graph material should report runtime diagnostics");
    bool foundGraphDiagnostic = false;
    for (const SceneRenderDiagnosticEvent& event : renderer.LastSceneDiagnostics().events) {
        if (event.kind == SceneRenderDiagnosticKind::InvalidMaterialAsset && event.materialAssetId == materialAssetId) {
            foundGraphDiagnostic = true;
        }
    }
    Require(foundGraphDiagnostic, "KBMAT-GRAPH-0502: invalid graph material diagnostic was not visible in scene diagnostics");
    renderer.EndFrame();

    WriteGraphValidationMaterial(materialPath, 0.65F, true);
    Require(manager.DiscoverMountedAssets() >= 2U, "Invalid graph material reload test did not rediscover fixed material");
    Require(renderer.BeginFrame(), "Invalid graph material reload test did not begin fixed frame");
    Require(renderer.SubmitScene(scene, desc), "Invalid graph material reload test did not submit fixed frame");
    const RenderMaterialHandle fixedHandle = resourceMap->ResolveMaterial(materialAssetId);
    const RenderMaterialResource* fixedMaterial = resources->FindMaterial(fixedHandle);
    Require(fixedHandle.IsValid() && fixedMaterial != nullptr && fixedHandle != firstHandle,
        "KBMAT-GRAPH-0502: fixed graph material did not refresh runtime material binding");
    Require(NearlyEqual(fixedMaterial->baseColor[0], 0.65F), "KBMAT-GRAPH-0502: fixed graph material did not load repaired material data");
    const Renderer::RuntimeSceneResourceStats fixedStats = renderer.RuntimeResourceStats();
    Require(fixedStats.materialReloadCount == 1U && fixedStats.materialErrorCount == 0U,
        "KBMAT-GRAPH-0502: fixed graph material should reload cleanly without material errors");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunRendererSubmitsMaterialInstanceAssetInHeadlessNoopTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_runtime_material_instance";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Runtime material instance test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path materialPath = root / "parent.kbmat";
    const std::filesystem::path instancePath = root / "parent_instance.kbmatinst";
    WriteTriangleObj(meshPath);

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Runtime material instance test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Runtime material instance test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialInstanceAssetLoader>()), "Runtime material instance test could not register material instance loader");
    Require(manager.Mounts().Mount("Game", root), "Runtime material instance test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 1U, "Runtime material instance test did not discover mesh asset");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Runtime material instance test discovered wrong mesh metadata");
    WriteMaterial(materialPath, 0U, 0U, 0U, 0U, 0U);
    Require(manager.DiscoverMountedAssets() >= 2U, "Runtime material instance test did not discover parent material");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/parent.kbmat");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Runtime material instance test discovered wrong parent material metadata");
    const kb::assets::AssetId parentMaterialId = materialMetadata->id;
    RenderMaterialInstanceAssetData instance{};
    instance.parentMaterialAssetId = parentMaterialId;
    instance.hasOverrides = true;
    instance.overrides.materialType = kRenderMaterialAssetBuiltInPbrType;
    instance.overrides.materialTypeVersion = kRenderMaterialAssetBuiltInPbrTypeVersion;
    instance.overrides.hasExplicitMaterialType = true;
    instance.overrides.hasExplicitMaterialTypeVersion = true;
    instance.overrides.desc.baseColor[0] = 0.25F;
    instance.overrides.desc.baseColor[1] = 0.5F;
    instance.overrides.desc.baseColor[2] = 0.75F;
    instance.overrides.desc.baseColor[3] = 1.0F;
    instance.overrides.desc.roughnessFactor = 0.25F;
    Require(RenderMaterialInstanceAssetWriter::Save(instancePath, instance), "Runtime material instance fixture with override could not be written");
    Require(manager.DiscoverMountedAssets() >= 3U, "Runtime material instance test did not discover material instance");
    meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* instanceMetadata = manager.Registry().FindByPath("/Game/parent_instance.kbmatinst");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Runtime material instance test lost mesh metadata");
    Require(instanceMetadata != nullptr && instanceMetadata->type == "RenderMaterialInstance", "Runtime material instance test discovered wrong material instance metadata");

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Runtime Material Instance Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialAssetId = instanceMetadata->id.value,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 1U,
        .cachedMaterials = 1U,
        .frameReferencedMeshes = 1U,
        .frameReferencedMaterials = 1U,
        .scenePassSubmitStats = 1U,
        .renderSceneMeshProxies = 1U,
        .renderSceneDrawGroupKeys = 1U,
        .meshResourceSlots = 1U,
        .materialResourceSlots = 1U,
        .meshBindings = 1U,
        .materialBindings = 1U,
        .syncMeshProxies = 1U,
        .syncTransformCacheEntries = 1U,
        .syncTransformResolvingEntries = 1U,
    });
    Require(renderer.Initialize(surface, &config), "Renderer did not initialize in runtime material instance test");
    Require(renderer.BeginFrame(), "Renderer did not begin runtime material instance frame");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 4U,
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
    };
    Require(renderer.SubmitScene(scene, desc), "Renderer did not submit runtime material instance scene");
    const SceneRenderSubmitStats submitStats = renderer.LastSceneSubmitStats();
    Require(!submitStats.HasMissingResources(), "Runtime material instance submit reported missing resources");
    Require(submitStats.submittedMeshCount == 1U, "Runtime material instance submit did not submit one mesh");

    const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const RenderResourceRegistry* resources = renderer.SceneResources();
    Require(resourceMap != nullptr && resources != nullptr, "Runtime material instance test could not access scene resources");
    const RenderMaterialHandle instanceHandle = resourceMap->ResolveMaterial(instanceMetadata->id.value);
    const RenderMaterialResource* materialResource = resources->FindMaterial(instanceHandle);
    Require(instanceHandle.IsValid() && materialResource != nullptr, "Runtime material instance did not bind a material resource under the instance asset id");
    Require(NearlyEqual(materialResource->baseColor[0], 0.25F), "Runtime material instance did not apply override base color");
    Require(NearlyEqual(materialResource->baseColor[1], 0.5F), "Runtime material instance did not apply override base color green channel");
    Require(NearlyEqual(materialResource->roughnessFactor, 0.25F), "Runtime material instance did not apply override roughness");

    const Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
    Require(runtimeStats.cachedMaterialCount == 1U, "Runtime material instance test should cache one resolved material resource");
    Require(runtimeStats.referencedMaterialAssetCount == 1U, "Runtime material instance test should reference the assigned instance material asset");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunRendererMaterialInstanceInheritsGraphBackedParentParametersTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_runtime_graph_material_instance";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Graph material instance inheritance test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path typePath = root / "BuiltinPbr.kbmaterialtype";
    const std::filesystem::path artifactPath = root / "GraphArtifact.kbmaterialgraph";
    const std::filesystem::path parentPath = root / "graph_parent.kbmat";
    const std::filesystem::path instancePath = root / "graph_parent_instance.kbmatinst";
    WriteTriangleObj(meshPath);
    Require(RenderMaterialTypeAssetLoader::SaveType(typePath, GetBuiltInPbrMaterialTypeDocument()), "Graph material instance inheritance test could not write material type fixture");
    RenderMaterialGraphDocument artifact = MakeDefaultRenderMaterialGraphDocument();
    Require(RenderMaterialGraphAssetLoader::SaveGraph(artifactPath, artifact), "Graph material instance inheritance test could not write graph artifact fixture");

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Graph material instance inheritance test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Graph material instance inheritance test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialInstanceAssetLoader>()), "Graph material instance inheritance test could not register material instance loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialGraphAssetLoader>()), "Graph material instance inheritance test could not register graph loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialTypeAssetLoader>()), "Graph material instance inheritance test could not register material type loader");
    Require(manager.Mounts().Mount("Game", root), "Graph material instance inheritance test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 3U, "Graph material instance inheritance test did not discover mesh/type/artifact assets");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* typeMetadata = manager.Registry().FindByPath("/Game/BuiltinPbr.kbmaterialtype");
    const kb::assets::AssetMetadata* artifactMetadata = manager.Registry().FindByPath("/Game/GraphArtifact.kbmaterialgraph");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Graph material instance inheritance test discovered wrong mesh metadata");
    Require(typeMetadata != nullptr && typeMetadata->type == kRenderMaterialTypeAssetType, "Graph material instance inheritance test discovered wrong material type metadata");
    Require(artifactMetadata != nullptr && artifactMetadata->type == kRenderMaterialGraphAssetType, "Graph material instance inheritance test discovered wrong graph artifact metadata");

    WriteGraphBackedReloadableMaterial(
        parentPath,
        0.41F,
        0.68F,
        typeMetadata->id.value,
        "/Game/BuiltinPbr.kbmaterialtype",
        artifactMetadata->id.value,
        artifactMetadata->contentHash);
    Require(manager.DiscoverMountedAssets() >= 4U, "Graph material instance inheritance test did not discover graph parent material");
    const kb::assets::AssetMetadata* parentMetadata = manager.Registry().FindByPath("/Game/graph_parent.kbmat");
    Require(parentMetadata != nullptr && parentMetadata->type == "RenderMaterial", "Graph material instance inheritance test discovered wrong parent material metadata");

    RenderMaterialInstanceAssetData instance{};
    instance.parentMaterialAssetId = parentMetadata->id;
    instance.hasOverrides = true;
    instance.overrides.materialType = kRenderMaterialAssetBuiltInPbrType;
    instance.overrides.materialTypeVersion = kRenderMaterialAssetBuiltInPbrTypeVersion;
    instance.overrides.hasExplicitMaterialType = true;
    instance.overrides.hasExplicitMaterialTypeVersion = true;
    instance.overrides.desc.baseColor[0] = 0.02F;
    instance.overrides.desc.baseColor[1] = 0.03F;
    instance.overrides.desc.baseColor[2] = 0.04F;
    instance.overrides.desc.roughnessFactor = 0.97F;
    RenderMaterialGraphParameterValue roughnessOverride{};
    roughnessOverride.stableId = "roughnessFactor";
    roughnessOverride.type = RenderMaterialParameterType::Scalar;
    roughnessOverride.numbers[0] = 0.22F;
    instance.overrides.graphParameterValues.push_back(roughnessOverride);
    Require(RenderMaterialInstanceAssetWriter::Save(instancePath, instance), "Graph material instance inheritance test could not write material instance fixture");
    Require(manager.DiscoverMountedAssets() >= 5U, "Graph material instance inheritance test did not discover material instance");

    meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* instanceMetadata = manager.Registry().FindByPath("/Game/graph_parent_instance.kbmatinst");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Graph material instance inheritance test lost mesh metadata");
    Require(instanceMetadata != nullptr && instanceMetadata->type == "RenderMaterialInstance", "Graph material instance inheritance test discovered wrong material instance metadata");

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Graph Material Instance Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialAssetId = instanceMetadata->id.value,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 1U,
        .cachedMaterials = 1U,
        .frameReferencedMeshes = 1U,
        .frameReferencedMaterials = 1U,
        .scenePassSubmitStats = 1U,
        .renderSceneMeshProxies = 1U,
        .renderSceneDrawGroupKeys = 1U,
        .meshResourceSlots = 1U,
        .materialResourceSlots = 1U,
        .meshBindings = 1U,
        .materialBindings = 1U,
        .syncMeshProxies = 1U,
        .syncTransformCacheEntries = 1U,
        .syncTransformResolvingEntries = 1U,
    });
    Require(renderer.Initialize(surface, &config), "Graph material instance inheritance test renderer did not initialize");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 4U,
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
    };

    Require(renderer.BeginFrame(), "Graph material instance inheritance test did not begin frame");
    Require(renderer.SubmitScene(scene, desc), "Graph material instance inheritance test did not submit scene");
    const SceneRenderSubmitStats submitStats = renderer.LastSceneSubmitStats();
    Require(submitStats.submittedMeshCount == 1U && !submitStats.HasMissingResources(),
        "Graph material instance inheritance test should submit one complete draw call");
    const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const RenderResourceRegistry* resources = renderer.SceneResources();
    Require(resourceMap != nullptr && resources != nullptr, "Graph material instance inheritance test could not inspect resources");
    const RenderMaterialHandle instanceHandle = resourceMap->ResolveMaterial(instanceMetadata->id.value);
    const RenderMaterialResource* materialResource = resources->FindMaterial(instanceHandle);
    Require(instanceHandle.IsValid() && materialResource != nullptr,
        "Graph material instance inheritance test did not bind a material resource under the instance asset id");
    Require(NearlyEqual(materialResource->baseColor[0], 0.41F),
        "Graph material instance should inherit parent graph baseColor parameter when the instance does not override it");
    Require(NearlyEqual(materialResource->roughnessFactor, 0.22F),
        "Graph material instance should override only the authored graph roughness parameter");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunRendererReloadsMaterialInstanceWhenParentMaterialChangesTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_runtime_material_instance_parent_reload";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Runtime material instance parent reload test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path materialPath = root / "parent.kbmat";
    const std::filesystem::path instancePath = root / "parent_instance.kbmatinst";
    WriteTriangleObj(meshPath);

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Runtime material instance parent reload test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Runtime material instance parent reload test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialInstanceAssetLoader>()), "Runtime material instance parent reload test could not register material instance loader");
    Require(manager.Mounts().Mount("Game", root), "Runtime material instance parent reload test could not mount asset root");

    WriteReloadableMaterial(materialPath, 0.2F, 0.7F, 0U);
    Require(manager.DiscoverMountedAssets() >= 2U, "Runtime material instance parent reload test did not discover mesh and parent material");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* parentMetadata = manager.Registry().FindByPath("/Game/parent.kbmat");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Runtime material instance parent reload test discovered wrong mesh metadata");
    Require(parentMetadata != nullptr && parentMetadata->type == "RenderMaterial", "Runtime material instance parent reload test discovered wrong parent material metadata");
    const kb::assets::AssetId parentMaterialId = parentMetadata->id;
    const std::uint64_t firstParentHash = parentMetadata->contentHash;

    WriteMaterialInstance(instancePath, parentMaterialId);
    Require(manager.DiscoverMountedAssets() >= 3U, "Runtime material instance parent reload test did not discover material instance");
    meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* instanceMetadata = manager.Registry().FindByPath("/Game/parent_instance.kbmatinst");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Runtime material instance parent reload test lost mesh metadata");
    Require(instanceMetadata != nullptr && instanceMetadata->type == "RenderMaterialInstance", "Runtime material instance parent reload test discovered wrong material instance metadata");
    const kb::assets::AssetId instanceMaterialId = instanceMetadata->id;
    const std::uint64_t instanceHash = instanceMetadata->contentHash;

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Runtime Material Instance Parent Reload Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialAssetId = instanceMaterialId.value,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 1U,
        .cachedMaterials = 1U,
        .frameReferencedMeshes = 1U,
        .frameReferencedMaterials = 1U,
        .scenePassSubmitStats = 1U,
        .renderSceneMeshProxies = 1U,
        .renderSceneDrawGroupKeys = 1U,
        .meshResourceSlots = 1U,
        .materialResourceSlots = 2U,
        .meshBindings = 1U,
        .materialBindings = 1U,
        .syncMeshProxies = 1U,
        .syncTransformCacheEntries = 1U,
        .syncTransformResolvingEntries = 1U,
    });
    Require(renderer.Initialize(surface, &config), "Renderer did not initialize in runtime material instance parent reload test");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 4U,
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
    };

    Require(renderer.BeginFrame(), "Renderer did not begin first runtime material instance parent reload frame");
    Require(renderer.SubmitScene(scene, desc), "Renderer did not submit first runtime material instance parent reload frame");
    const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const RenderResourceRegistry* resources = renderer.SceneResources();
    Require(resourceMap != nullptr && resources != nullptr, "Runtime material instance parent reload test could not access scene resources");
    const RenderMaterialHandle firstHandle = resourceMap->ResolveMaterial(instanceMaterialId.value);
    const RenderMaterialResource* firstMaterial = resources->FindMaterial(firstHandle);
    Require(firstHandle.IsValid() && firstMaterial != nullptr, "Runtime material instance parent reload test did not bind initial instance material");
    const std::uint64_t firstVersion = firstMaterial->version;
    Require(NearlyEqual(firstMaterial->baseColor[0], 0.2F), "Runtime material instance parent reload test did not inherit initial parent base color");
    Require(NearlyEqual(firstMaterial->roughnessFactor, 0.7F), "Runtime material instance parent reload test did not inherit initial parent roughness");
    renderer.EndFrame();

    WriteReloadableMaterial(materialPath, 0.85F, 0.33F, 0U);
    Require(manager.DiscoverMountedAssets() >= 3U, "Runtime material instance parent reload test did not rediscover changed parent material");
    parentMetadata = manager.Registry().FindByPath("/Game/parent.kbmat");
    instanceMetadata = manager.Registry().FindByPath("/Game/parent_instance.kbmatinst");
    Require(parentMetadata != nullptr && parentMetadata->id == parentMaterialId && parentMetadata->contentHash != firstParentHash,
        "Runtime material instance parent reload test did not update parent material metadata hash");
    Require(instanceMetadata != nullptr && instanceMetadata->id == instanceMaterialId && instanceMetadata->contentHash == instanceHash,
        "Runtime material instance parent reload test should keep the instance asset metadata stable while parent changes");

    Require(renderer.BeginFrame(), "Renderer did not begin second runtime material instance parent reload frame");
    Require(renderer.SubmitScene(scene, desc), "Renderer did not submit second runtime material instance parent reload frame");
    const SceneRenderSubmitStats reloadSubmitStats = renderer.LastSceneSubmitStats();
    Require(reloadSubmitStats.meshDrawCommandCacheMissCount == 1U, "KBMAT-1010: Parent material reload through instance should invalidate cached draw command");
    Require(reloadSubmitStats.meshDrawCommandCacheBuildCount == 1U, "KBMAT-1010: Parent material reload through instance should rebuild cached draw command");
    Require(reloadSubmitStats.meshDrawCommandCachePruneCount == 1U, "KBMAT-1010: Parent material reload through instance should prune stale draw command");

    const RenderMaterialHandle secondHandle = resourceMap->ResolveMaterial(instanceMaterialId.value);
    const RenderMaterialResource* secondMaterial = resources->FindMaterial(secondHandle);
    Require(secondHandle.IsValid() && secondMaterial != nullptr, "KBMAT-1010: Reloaded material instance did not bind a live material resource");
    Require(secondHandle != firstHandle, "KBMAT-1010: Material instance parent reload reused the stale material handle");
    Require(resources->FindMaterial(firstHandle) == nullptr, "KBMAT-1010: Material instance parent reload kept stale material handle resolvable");
    Require(secondMaterial->version != firstVersion, "KBMAT-1010: Material instance parent reload did not allocate a fresh material resource version");
    Require(NearlyEqual(secondMaterial->baseColor[0], 0.85F), "KBMAT-1010: Reloaded material instance did not inherit changed parent base color");
    Require(NearlyEqual(secondMaterial->roughnessFactor, 0.33F), "KBMAT-1010: Reloaded material instance did not inherit changed parent roughness");
    const Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
    Require(runtimeStats.cachedMaterialCount == 1U, "KBMAT-1010: Material instance parent reload should keep only the live instance material cached");
    Require(runtimeStats.materialReloadCount == 1U, "KBMAT-1010: Material instance parent reload should count one successful material reload");
    renderer.EndFrame();

    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunRendererSubmitsWorkspaceSceneCubeMaterialAfterReopenTest() {
    const std::optional<std::filesystem::path> projectAssets = FindWorkspaceProjectAssets();
    if (!projectAssets.has_value()) {
        return;
    }

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Workspace scene test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Workspace scene test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialInstanceAssetLoader>()), "Workspace scene test could not register material instance loader");
    Require(manager.Mounts().Mount("Game", *projectAssets), "Workspace scene test could not mount Project/Assets");
    Require(manager.DiscoverMountedAssets() >= 1U, "Workspace scene test did not discover project assets");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/Cube.21kb");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/NewMaterial.kbmat");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Workspace Cube.21kb was not discovered as RenderMesh");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Workspace NewMaterial.kbmat was not discovered as RenderMaterial");

    const kb::assets::AssetHandle<RenderMeshAssetData> loadedMesh = manager.Load<RenderMeshAssetData>(meshMetadata->id);
    const kb::assets::AssetHandle<RenderMaterialAssetData> loadedMaterial = manager.Load<RenderMaterialAssetData>(materialMetadata->id);
    Require(loadedMesh.IsLoaded(), "Workspace Cube.21kb could not be loaded as RenderMeshAssetData before scene reopen");
    Require(loadedMaterial.IsLoaded(), "Workspace NewMaterial.kbmat could not be loaded as RenderMaterialAssetData before scene reopen");

    const std::filesystem::path scenePath = *projectAssets / "Scenes" / "Main.21kbscene";
    Require(kb::scene::SceneDocumentService::LoadFileIntoScene(scene, scenePath), "Workspace Main.21kbscene could not be loaded into a scene");
    struct WorkspaceSceneMeshVisit {
        std::uint64_t expectedMeshAssetId = 0U;
        std::uint64_t expectedMaterialAssetId = 0U;
        std::uint32_t meshRendererCount = 0U;
        std::uint32_t expectedMeshRendererCount = 0U;
    } meshVisit{
        .expectedMeshAssetId = meshMetadata->id.value,
        .expectedMaterialAssetId = materialMetadata->id.value,
    };
    scene.Components().Visitors().ForEachMeshRenderer(
        [](kb::scene::SceneEntity, const kb::scene::TransformComponent&, const kb::scene::MeshRendererComponent& renderer, void* context) {
            auto* visit = static_cast<WorkspaceSceneMeshVisit*>(context);
            ++visit->meshRendererCount;
            if (renderer.meshAssetId == visit->expectedMeshAssetId &&
                renderer.materialAssetId == visit->expectedMaterialAssetId) {
                ++visit->expectedMeshRendererCount;
            }
        },
        &meshVisit);
    Require(meshVisit.expectedMeshRendererCount == 1U,
        "Workspace Main.21kbscene did not expose the expected Cube.21kb/NewMaterial.kbmat Mesh Renderer to ECS render iteration");

    const kb::scene::SceneEntity cameraEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Workspace Runtime Test Camera",
        .transform = TransformAt(0.0F, 0.0F, -6.0F),
    });
    scene.Components().Cameras().Set(cameraEntity, kb::scene::CameraComponent{
        .projection = kb::scene::CameraProjection::Perspective,
        .verticalFovDegrees = 60.0F,
        .orthographicHeight = 10.0F,
        .nearClip = 0.01F,
        .farClip = 100.0F,
        .primary = true,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 2U,
        .cachedMaterials = 4U,
        .frameReferencedMeshes = 2U,
        .frameReferencedMaterials = 4U,
        .scenePassSubmitStats = 2U,
        .renderSceneMeshProxies = 4U,
        .renderSceneDrawGroupKeys = 4U,
        .meshResourceSlots = 2U,
        .materialResourceSlots = 4U,
        .meshBindings = 2U,
        .materialBindings = 4U,
        .syncMeshProxies = 4U,
        .syncTransformCacheEntries = 4U,
        .syncTransformResolvingEntries = 4U,
    });
    Require(renderer.Initialize(surface, &config), "Workspace scene renderer did not initialize in headless Noop mode");
    RenderResourceRegistry directResources;
    const RenderMeshHandle directMeshHandle = directResources.RegisterMesh(loadedMesh->desc);
    Require(directMeshHandle.IsValid(), "Workspace Cube.21kb loaded but its mesh desc could not register as a runtime mesh resource");
    directResources.Shutdown();
    Require(renderer.BeginFrame(), "Workspace scene renderer did not begin a frame");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 4U,
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
    };
    Require(renderer.SubmitScene(scene, desc), "Workspace reopened scene did not submit to the runtime renderer");

    const SceneRenderSubmitStats submitStats = renderer.LastSceneSubmitStats();
    const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const RenderResourceRegistry* resources = renderer.SceneResources();
    Require(resourceMap != nullptr && resources != nullptr, "Workspace scene test could not inspect runtime render resources");
    const RenderMeshHandle meshHandle = resourceMap->ResolveMesh(meshMetadata->id.value);
    const RenderMaterialHandle materialHandle = resourceMap->ResolveMaterial(materialMetadata->id.value);
    const RenderMaterialResource* materialResource = resources->FindMaterial(materialHandle);
    const ResolvedRuntimeMaterialAsset expectedMaterial = RuntimeMaterialResolver{}.ResolveAsset(manager, materialMetadata->id);
    Require(meshHandle.IsValid(), "Workspace reopened scene did not bind Cube.21kb to a runtime mesh handle");
    Require(materialHandle.IsValid() && materialResource != nullptr, "Workspace reopened scene did not bind NewMaterial.kbmat to a runtime material handle");
    Require(expectedMaterial.resolved &&
            NearlyEqual(materialResource->baseColor[0], expectedMaterial.material.desc.baseColor[0]) &&
            NearlyEqual(materialResource->baseColor[1], expectedMaterial.material.desc.baseColor[1]) &&
            NearlyEqual(materialResource->baseColor[2], expectedMaterial.material.desc.baseColor[2]),
        "Workspace runtime material resource did not match the resolved NewMaterial base color");
    Require(!submitStats.HasMissingResources(), "Workspace reopened scene reported missing mesh or material resources");
    Require(submitStats.submittedMeshCount == 1U, "Workspace reopened scene did not submit the cube mesh");
    Require(submitStats.submittedDrawCallCount == 1U, "Workspace reopened scene did not emit one cube draw call");

    renderer.EndFrame();
    renderer.Shutdown();
}

void RunRendererSubmitsGltfEmbeddedMaterialInHeadlessNoopTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_runtime_embedded_material";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Embedded material runtime test could not create temp root");

    WriteEmbeddedMaterialTriangleGltf(root);
    WriteTexture(root / "embedded_albedo.kbtex", 160U, 180U, 200U);
    WriteTexture(root / "embedded_normal.kbtex", 128U, 128U, 255U);
    WriteTexture(root / "embedded_mr.kbtex", 64U, 128U, 192U);
    WriteTexture(root / "embedded_ao.kbtex", 192U, 192U, 192U);
    WriteTexture(root / "embedded_emissive.kbtex", 10U, 20U, 30U);

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Embedded material runtime test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderTextureAssetLoader>()), "Embedded material runtime test could not register texture loader");
    Require(manager.Mounts().Mount("Game", root), "Embedded material runtime test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 5U, "Embedded material runtime test did not discover mesh and texture assets");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/embedded_triangle.gltf");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Embedded material runtime test discovered wrong mesh metadata");

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Embedded Material Runtime Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 2U,
        .cachedMaterials = 4U,
        .cachedTextures = 5U,
        .frameReferencedMeshes = 2U,
        .frameReferencedMaterials = 4U,
        .frameReferencedTextures = 5U,
        .scenePassSubmitStats = 2U,
        .renderSceneMeshProxies = 4U,
        .renderSceneDrawGroupKeys = 4U,
        .meshResourceSlots = 2U,
        .materialResourceSlots = 4U,
        .textureResourceSlots = 5U,
        .meshBindings = 2U,
        .materialBindings = 4U,
        .textureBindings = 5U,
        .syncMeshProxies = 4U,
        .syncTransformCacheEntries = 4U,
        .syncTransformResolvingEntries = 4U,
    });
    Require(renderer.Initialize(surface, &config), "Renderer did not initialize in embedded material runtime test");
    Require(renderer.BeginFrame(), "Renderer did not begin embedded material runtime frame");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 4U,
            .maxVisibleInstances = 8U,
        },
    };
    Require(renderer.SubmitScene(scene, desc), "Renderer did not submit embedded material runtime scene");

    const SceneRenderSubmitStats submitStats = renderer.LastSceneSubmitStats();
    Require(submitStats.visibleMeshCount == 1U, "Embedded material runtime test did not keep mesh visible");
    Require(submitStats.submittedMeshCount == 1U, "Embedded material runtime test did not submit mesh");
    Require(submitStats.submittedDrawCallCount == 1U, "Embedded material runtime test did not draw one embedded material batch");
    Require(!submitStats.HasMissingResources(), "Embedded material runtime test reported missing resources");
    const std::span<const SceneRenderPassSubmitStats> passStats = renderer.LastScenePassSubmitStats();
    Require(passStats.size() == 2U, "Embedded material runtime test did not report both scene passes");
    Require(passStats[0].pass == MeshPassType::BaseOpaque && passStats[0].stats.submittedMeshCount == 1U, "Embedded material runtime opaque pass stats are wrong");
    Require(passStats[1].pass == MeshPassType::BaseTransparent && passStats[1].stats.submittedMeshCount == 0U, "Embedded material runtime transparent pass stats are wrong");

    const Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
    Require(runtimeStats.cachedMeshCount == 1U, "Embedded material runtime test did not cache one mesh resource");
    Require(runtimeStats.cachedMaterialCount == 1U, "Embedded material runtime test did not cache one embedded material resource");
    Require(runtimeStats.cachedTextureCount == 5U, "Embedded material runtime test did not cache embedded material textures");
    Require(runtimeStats.referencedMeshAssetCount == 1U, "Embedded material runtime test did not reference one mesh asset");
    Require(runtimeStats.referencedMaterialAssetCount == 1U, "Embedded material runtime test did not reference one embedded material asset");
    Require(runtimeStats.referencedTextureAssetCount == 5U, "Embedded material runtime test did not reference embedded material textures");
    Require(runtimeStats.unresolvedMaterialTexturePathCount == 0U, "Embedded material runtime test reported unresolved texture paths for valid embedded textures");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunGraphMaterialReportsGpuMaterialGraphModeTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_graph_material_cpu_fallback_counter";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Graph material CPU fallback counter test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path graphMaterialPath = root / "graph_mat.kbmat";
    const std::filesystem::path pbrMaterialPath = root / "pbr_mat.kbmat";
    WriteTriangleObj(meshPath);
    WriteGraphValidationMaterial(graphMaterialPath, 0.6F, true);
    {
        std::ofstream pbrOut{ pbrMaterialPath, std::ios::trunc };
        pbrOut
            << "version 1\n"
            << "materialType builtin.pbr\n"
            << "materialTypeVersion 1\n"
            << "baseColor 0.3 0.4 0.5 1\n"
            << "roughnessFactor 0.5\n"
            << "alphaMode OPAQUE\n";
    }

    // This test asserts the GPU material-graph render mode, so the graph material must actually have a
    // cooked GPU binary (graphMaterialGpuCount now reflects the true draw outcome, not resolve intent).
    const std::filesystem::path cacheRoot = root / "graph_shaders";
    {
        const std::optional<RenderMaterialAssetData> graphMaterial = RenderMaterialAssetLoader::LoadMaterial(graphMaterialPath);
        Require(graphMaterial.has_value(), "MAT-27 GPU material mode test could not load the graph material to cook");
        const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graphMaterial->graph, RenderMaterialGraphBuildContext{ .assetId = 0x2700U });
        Require(compiled.Succeeded(), "MAT-27 GPU material mode test graph must compile");
        RenderMaterialGraphShaderArtifactRequest request{};
        request.shadercPath = KB_TEST_GRAPH_SHADERC_PATH;
        request.varyingDefPath = KB_TEST_GRAPH_SHADER_VARYING_DEF;
        request.includeDirs = { KB_TEST_GRAPH_SHADER_INCLUDE_DIR, KB_TEST_GRAPH_BGFX_SHADER_INCLUDE_DIR };
        request.cacheRoot = cacheRoot.generic_string();
        request.pass = "BaseOpaque";
        request.shaderPlatform = kb::assets::bake::ShaderBakePlatform::Windows;
        const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };
        Require(CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, request).Succeeded(),
            "MAT-27 GPU material mode test must cook the graph binary");
    }

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Graph CPU fallback counter test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Graph CPU fallback counter test could not register material loader");
    Require(manager.Mounts().Mount("Game", root), "Graph CPU fallback counter test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 3U, "Graph CPU fallback counter test did not discover assets");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* graphMaterialMetadata = manager.Registry().FindByPath("/Game/graph_mat.kbmat");
    const kb::assets::AssetMetadata* pbrMaterialMetadata = manager.Registry().FindByPath("/Game/pbr_mat.kbmat");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Graph CPU fallback counter test discovered wrong mesh");
    Require(graphMaterialMetadata != nullptr && graphMaterialMetadata->type == "RenderMaterial", "Graph CPU fallback counter test discovered wrong graph material");
    Require(pbrMaterialMetadata != nullptr && pbrMaterialMetadata->type == "RenderMaterial", "Graph CPU fallback counter test discovered wrong PBR material");

    const kb::scene::SceneEntity graphEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Graph Material Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(graphEntity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialAssetId = graphMaterialMetadata->id.value,
    });

    const kb::scene::SceneEntity pbrEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "PBR Material Mesh",
        .transform = TransformAt(1.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(pbrEntity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialAssetId = pbrMaterialMetadata->id.value,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 1U,
        .cachedMaterials = 2U,
        .frameReferencedMeshes = 1U,
        .frameReferencedMaterials = 2U,
        .scenePassSubmitStats = 2U,
        .renderSceneMeshProxies = 4U,
        .renderSceneDrawGroupKeys = 4U,
        .meshResourceSlots = 1U,
        .materialResourceSlots = 2U,
        .meshBindings = 1U,
        .materialBindings = 2U,
        .syncMeshProxies = 4U,
        .syncTransformCacheEntries = 4U,
        .syncTransformResolvingEntries = 4U,
    });
    renderer.SetGraphShaderCacheRoot(cacheRoot.generic_string());
    Require(renderer.Initialize(surface, &config), "Graph CPU fallback counter test did not initialize renderer");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 8U,
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
    };

    Require(renderer.BeginFrame(), "Graph GPU material mode test did not begin first frame");
    Require(renderer.SubmitScene(scene, desc), "Graph GPU material mode test did not submit first frame");
    const Renderer::RuntimeSceneResourceStats firstStats = renderer.RuntimeResourceStats();
    Require(firstStats.graphMaterialGpuCount == 1U,
        "MAT-27: first frame did not report exactly one graph material using the GPU material graph path");
    Require(firstStats.graphMaterialCpuFallbackCount == 0U,
        "MAT-27: first frame fell back a valid graph material to CPU PBR flattening instead of the GPU path");
    Require(firstStats.materialLoadedCount == 2U,
        "MAT-27: first frame did not load exactly two materials (graph + builtin PBR)");
    renderer.EndFrame();

    Require(renderer.BeginFrame(), "Graph GPU material mode test did not begin steady frame");
    Require(renderer.SubmitScene(scene, desc), "Graph GPU material mode test did not submit steady frame");
    const Renderer::RuntimeSceneResourceStats steadyStats = renderer.RuntimeResourceStats();
    Require(steadyStats.graphMaterialGpuCount == 1U,
        "MAT-27: steady frame did not retain the GPU material graph render mode from cache");
    Require(steadyStats.graphMaterialCpuFallbackCount == 0U,
        "MAT-27: steady frame fell back the cached graph material to CPU PBR flattening");
    Require(steadyStats.materialLoadedCount == 0U,
        "MAT-27: steady frame reloaded materials unexpectedly");
    renderer.EndFrame();

    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunRendererSubmitsDeferredGBufferAndLightingPassesInHeadlessNoopTest() {
    kb::scene::Scene scene;

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .scenePassSubmitStats = 4U,
    });
    Require(renderer.Initialize(surface, &config), "Deferred runtime test did not initialize renderer");
    Require(renderer.BeginFrame(), "Deferred runtime test did not begin frame");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .lightingConfig = SceneRenderLightingConfig{
            .lightingPath = SceneRenderLightingPath::Deferred,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueAndTransparent,
        .shadowPassEnabled = false,
        .postProcessEnabled = false,
        .selectionMaskEnabled = false,
        .selectionOutlineEnabled = false,
    };

    Require(renderer.SubmitScene(scene, desc), "Deferred runtime test did not submit scene");
    const std::span<const SceneRenderPassSubmitStats> passStats = renderer.LastScenePassSubmitStats();
    Require(passStats.size() == 3U, "Deferred runtime test did not report GBuffer, lighting and transparent passes");
    Require(passStats[0].renderPass == RenderPassKind::GBufferGeometry && passStats[0].pass == MeshPassType::GBuffer,
        "Deferred runtime test did not submit GBuffer geometry as the first scene pass");
    Require(passStats[1].renderPass == RenderPassKind::DeferredLighting,
        "Deferred runtime test did not submit the deferred lighting pass after GBuffer");
    Require(passStats[2].renderPass == RenderPassKind::TransparentScene && passStats[2].pass == MeshPassType::BaseTransparent,
        "Deferred runtime test did not keep transparent rendering in the forward transparent pass");
    for (const SceneRenderPassSubmitStats& pass : passStats) {
        Require(pass.renderPass != RenderPassKind::OpaqueScene,
            "Deferred runtime test must not use the forward opaque pass as deferred proof");
    }
    Require(!renderer.LastSceneDiagnostics().HasErrors(), "Deferred runtime test produced diagnostics for a valid GBuffer path");

    renderer.EndFrame();
    renderer.Shutdown();
}

void RunRendererSubmitsDockedAndDetachedViewportsInSameFrameTest() {
    kb::scene::Scene scene;

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Renderer did not initialize for same-frame multi-viewport test");
    Require(renderer.BeginFrame(), "Renderer did not begin same-frame multi-viewport test");

    const RenderSceneSubmitDesc docked{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .lightingConfig = SceneRenderLightingConfig{.lightingPath = SceneRenderLightingPath::Deferred},
        .meshPassMode = SceneRenderMeshPassMode::OpaqueAndTransparent,
    };
    const RenderSceneSubmitDesc detached{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 2U },
                .extent = RenderExtent{ 96U, 72U },
                .viewportIndex = 1U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .lightingConfig = SceneRenderLightingConfig{.lightingPath = SceneRenderLightingPath::Deferred},
        .meshPassMode = SceneRenderMeshPassMode::OpaqueAndTransparent,
    };
    const std::array<Renderer::SceneFrameSubmission, 2U> submissions{
        Renderer::SceneFrameSubmission{ .scene = &scene, .desc = docked },
        Renderer::SceneFrameSubmission{ .scene = &scene, .desc = detached },
    };

    Require(renderer.SubmitScenes(submissions), "Renderer rejected docked and detached viewport submissions in one frame");
    const std::span<const SceneRenderPassSubmitStats> passStats = renderer.LastScenePassSubmitStats();
    Require(passStats.size() == 6U, "Same-frame multi-viewport test did not report both deferred pipelines");
    for (std::size_t index = 0U; index < 3U; ++index) {
        Require(passStats[index].viewportId == 1U && passStats[index].viewportIndex == 0U,
            "Docked viewport deferred pass metadata is wrong");
        Require(passStats[index + 3U].viewportId == 2U && passStats[index + 3U].viewportIndex == 1U,
            "Detached viewport deferred pass metadata is wrong");
    }
    Require(!renderer.LastSceneSubmitStats().HasMissingResources(), "Same-frame multi-viewport test reported missing resources for an empty scene");

    renderer.EndFrame();
    renderer.Shutdown();
}

void RunSecondaryFrameModesProduceRuntimeTargetsTest() {
    constexpr std::array<kb::scene::AuxFrameMode, 4U> modes{
        kb::scene::AuxFrameMode::Flat,
        kb::scene::AuxFrameMode::Mirror,
        kb::scene::AuxFrameMode::Cube,
        kb::scene::AuxFrameMode::Panoramic,
    };
    for (std::size_t index = 0U; index < modes.size(); ++index) {
        kb::scene::Scene scene;
        const kb::scene::SceneEntity owner = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
            .name = "Secondary Frame Camera",
            .transform = TransformAt(0.0F, 1.0F, -3.0F),
        });
        scene.Components().Cameras().Set(owner, kb::scene::CameraComponent{});
        const std::uint64_t imageTargetId = 0xA160U + index;
        scene.Components().AuxFrames().Set(owner, kb::scene::AuxFrameComponent{
            .mode = modes[index],
            .imageTargetId = imageTargetId,
            .width = 96U,
            .height = 64U,
            .mirrorPlaneNormal = kb::scene::Vec3{0.0F, 1.0F, 0.0F},
            .enabled = true,
        });

        HeadlessSurface surface;
        DisplayConfig config{};
        config.allowHeadlessNoop = true;
        config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
        Renderer renderer;
        Require(renderer.Initialize(surface, &config), "Secondary Frame test renderer did not initialize");
        Require(renderer.BeginFrame(), "Secondary Frame test renderer did not begin frame");
        const RenderSceneSubmitDesc desc{
            .target = RenderSceneTargetBinding{
                .frameBuffer = BGFX_INVALID_HANDLE,
                .colorTexture = BGFX_INVALID_HANDLE,
                .viewport = RenderViewportDesc{.id = RenderViewportId{1U}, .extent = RenderExtent{64U, 64U}, .viewportIndex = 0U},
            },
            .cameraOverride = IdentityCamera(),
        };
        Require(renderer.SubmitScene(scene, desc), "Secondary Frame renderer rejected an enabled runtime target");
        const SceneRenderResourceMap* map = renderer.SceneResourceMap();
        const RenderResourceRegistry* resources = renderer.SceneResources();
        Require(map != nullptr && resources != nullptr, "Secondary Frame renderer did not expose scene resource state");
        const RenderTextureResource* target = resources->FindTexture(map->ResolveTexture(imageTargetId, RenderTextureColorSpace::Linear));
        Require(target != nullptr, "Secondary Frame did not bind its renderer-owned image target");
        const RenderTextureDimension expected = modes[index] == kb::scene::AuxFrameMode::Cube
            ? RenderTextureDimension::TextureCube
            : RenderTextureDimension::Texture2D;
        Require(target->dimension == expected, "Secondary Frame mode produced an image target of the wrong texture dimension");
        renderer.EndFrame();

        kb::scene::AuxFrameComponent* frame = scene.Components().AuxFrames().TryGet(owner);
        Require(frame != nullptr, "Secondary Frame test lost the canonical ECS component");
        frame->enabled = false;
        scene.Components().AuxFrames().MarkModified(owner);
        Require(renderer.BeginFrame(), "Secondary Frame cleanup test renderer did not begin frame");
        Require(renderer.SubmitScene(scene, desc), "Secondary Frame cleanup submit was rejected");
        Require(!map->ResolveTexture(imageTargetId, RenderTextureColorSpace::Linear).IsValid(),
            "Secondary Frame left a dynamic image target bound after the component was disabled");
        renderer.EndFrame();
        renderer.Shutdown();
    }
}

void RunEditorCameraWireframesSubmitInHeadlessNoopTest() {
    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType =
        static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    Require(
        renderer.Initialize(surface, &config),
        "Camera wireframe renderer did not initialize in headless Noop mode");
    Require(
        renderer.BeginFrame(),
        "Camera wireframe renderer did not begin a frame");

    SceneGizmoPass gizmoPass;
    Require(
        gizmoPass.Initialize(),
        "Camera wireframe gizmo pass did not initialize");
    const SceneRenderCamera camera = IdentityCamera();
    const std::array<EditorCameraWireframeDesc, 2U> wireframes{{
        EditorCameraWireframeDesc{
            .projection =
                EditorCameraWireframeProjection::Perspective,
            .position = {0.0F, 0.0F, 0.0F},
            .verticalFovDegrees = 60.5F,
            .nearClip = 0.01F,
            .farClip = 1000.0F,
            .displayFarClip = 10.0F,
            .aspect = 1.0F,
        },
        EditorCameraWireframeDesc{
            .projection =
                EditorCameraWireframeProjection::Orthographic,
            .position = {2.0F, 0.0F, 0.0F},
            .orthographicHeight = 12.0F,
            .nearClip = 0.25F,
            .farClip = 250.0F,
            .displayFarClip = 15.0F,
            .aspect = 4.0F / 3.0F,
        },
    }};
    Require(
        gizmoPass.Submit(SceneGizmoPassDesc{
            .viewId = 0U,
            .frameBuffer = BGFX_INVALID_HANDLE,
            .extent = RenderExtent{64U, 64U},
            .camera = &camera,
            .cameraWireframes = wireframes,
        }),
        "Perspective and orthographic camera wireframes did not reach the real gizmo pass");

    gizmoPass.Shutdown();
    renderer.EndFrame();
    const auto perspectiveLines =
        BuildEditorCameraWireframeLines(wireframes[0]);
    const auto orthographicLines =
        BuildEditorCameraWireframeLines(wireframes[1]);
    EditorCameraWireframeDesc fullAuthoredFrustum = wireframes[0];
    fullAuthoredFrustum.displayFarClip = 0.0F;
    const auto fullAuthoredLines =
        BuildEditorCameraWireframeLines(fullAuthoredFrustum);
    Require(
        perspectiveLines.size() == 12U &&
            orthographicLines.size() == 12U,
        "Camera wireframe geometry must always contain near, far, and connecting edges");
    Require(
        NearlyEqual(perspectiveLines[4].from[2], 10.0F) &&
            NearlyEqual(perspectiveLines[7].to[2], 10.0F) &&
            NearlyEqual(perspectiveLines[8].to[2], 10.0F),
        "Perspective camera far-plane edges and connectors did not use the bounded display Far Clip");
    Require(
        NearlyEqual(orthographicLines[4].from[2], 15.0F) &&
            NearlyEqual(orthographicLines[7].to[2], 15.0F) &&
            NearlyEqual(orthographicLines[8].to[2], 15.0F),
        "Orthographic camera far-plane edges and connectors did not use the bounded display Far Clip");
    Require(
        NearlyEqual(fullAuthoredLines[4].from[2], 1000.0F) &&
            NearlyEqual(fullAuthoredFrustum.farClip, 1000.0F) &&
            NearlyEqual(wireframes[0].farClip, 1000.0F),
        "Editor display depth must not mutate or replace the authored runtime Camera Far Clip");
    renderer.Shutdown();
}

} // namespace

// MAT-72: material frame time accumulates and is exposed as the u_time vec4 (time, delta, frameIndex).
void RunMaterialFrameTimeAdvanceTest() {
    const auto nearly = [](float a, float b) noexcept { return std::fabs(a - b) <= 0.0005F; };

    SceneRenderer sceneRenderer;
    Require(nearly(sceneRenderer.FrameTimeConstants()[0], 0.0F), "MAT-72: Frame time must start at zero seconds");
    sceneRenderer.AdvanceFrameTime(0.5F);
    sceneRenderer.AdvanceFrameTime(0.25F);
    const std::array<float, 4> constants = sceneRenderer.FrameTimeConstants();
    Require(nearly(constants[0], 0.75F), "MAT-72: Frame seconds must accumulate across advances");
    Require(nearly(constants[1], 0.25F), "MAT-72: Frame delta must reflect the most recent advance");
    Require(nearly(constants[2], 2.0F), "MAT-72: Frame index must increment per advance");
    sceneRenderer.SetDynamicParameter({ 0.1F, 0.2F, 0.3F, 0.4F });
    const std::array<float, 4> dynamicParameter = sceneRenderer.DynamicParameterConstants();
    Require(nearly(dynamicParameter[0], 0.1F) && nearly(dynamicParameter[3], 0.4F),
        "MAT-30: SceneRenderer must retain DynamicParameter constants for graph shader submission");

    Renderer renderer;
    renderer.SetFrameDeltaSeconds(1.0F / 30.0F);
    Require(nearly(renderer.FrameDeltaSeconds(), 1.0F / 30.0F), "MAT-72: Renderer must retain the per-frame delta seconds");
}

#if defined(_WIN32)
void PublishCheckerTexture(kb::assets::AssetManager& manager, kb::assets::AssetId textureId,
                           const std::filesystem::path& root) {
    Require(manager.RegisterAsset(kb::assets::AssetMetadata{
                .id = textureId,
                .type = "RenderTexture",
                .name = "Checker",
                .virtualPath = "/Game/Checker",
                .physicalPath = root / "runtime_checker_only",
                .contentHash = 1U,
                .runtimeLoadable = true,
            }),
        "Could not register the in-memory checker texture");
    auto texture = std::make_shared<RenderTextureAssetData>();
    texture->width = 64U;
    texture->height = 64U;
    texture->colorSpace = RenderTextureAssetColorSpace::Srgb;
    texture->semantic = RenderTextureAssetSemantic::BaseColor;
    texture->rgba8.resize(64U * 64U * 4U);
    for (std::size_t y = 0U; y < 64U; ++y) {
        for (std::size_t x = 0U; x < 64U; ++x) {
            const std::size_t offset = (y * 64U + x) * 4U;
            const bool warm = ((x / 8U) + (y / 8U)) % 2U != 0U;
            texture->rgba8[offset] = warm ? 255U : 30U;
            texture->rgba8[offset + 1U] = warm ? 128U : 205U;
            texture->rgba8[offset + 2U] = warm ? 45U : 245U;
            texture->rgba8[offset + 3U] = 255U;
        }
    }
    Require(manager.PublishRuntimeAsset(textureId, std::move(texture)),
        "Could not publish the in-memory checker texture");
}

void PublishCheckerMaterial(kb::assets::AssetManager& manager, kb::assets::AssetId materialId,
                            kb::assets::AssetId textureId, const std::filesystem::path& root) {
    Require(manager.RegisterAsset(kb::assets::AssetMetadata{
                .id = materialId,
                .type = "RenderMaterial",
                .name = "Checker Material",
                .virtualPath = "/Game/CheckerMaterial",
                .physicalPath = root / "runtime_checker_material_only",
                .contentHash = 1U,
                .runtimeLoadable = true,
            }),
        "Could not register the in-memory checker material");
    auto material = std::make_shared<RenderMaterialAssetData>();
    material->desc.albedoTextureAssetId = textureId.value;
    material->desc.roughnessFactor = 0.75F;
    material->desc.doubleSided = true;
    material->graph = MakeDefaultRenderMaterialGraphDocument();
    Require(manager.PublishRuntimeAsset(materialId, std::move(material)),
        "Could not publish the in-memory checker material");
}

void WriteStressCubeObj(const std::filesystem::path& path) {
    std::ofstream cube{path, std::ios::trunc};
    Require(cube.is_open(), "Could not write the stress cube mesh");
    cube << "v -0.5 -0.5 -0.5\n"
         << "v 0.5 -0.5 -0.5\n"
         << "v 0.5 0.5 -0.5\n"
         << "v -0.5 0.5 -0.5\n"
         << "v -0.5 -0.5 0.5\n"
         << "v 0.5 -0.5 0.5\n"
         << "v 0.5 0.5 0.5\n"
         << "v -0.5 0.5 0.5\n"
         << "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
         << "vn 0 0 1\nvn 0 0 -1\nvn -1 0 0\n"
         << "vn 1 0 0\nvn 0 1 0\nvn 0 -1 0\n";
    constexpr std::array<std::array<int, 4>, 6> faces{{
        {{5, 6, 7, 8}}, {{2, 1, 4, 3}}, {{1, 5, 8, 4}},
        {{6, 2, 3, 7}}, {{4, 8, 7, 3}}, {{1, 2, 6, 5}},
    }};
    for (std::size_t face = 0U; face < faces.size(); ++face) {
        const auto& v = faces[face];
        const int n = static_cast<int>(face + 1U);
        cube << "f " << v[0] << "/1/" << n << ' ' << v[1] << "/2/" << n
             << ' ' << v[2] << "/3/" << n << '\n';
        cube << "f " << v[0] << "/1/" << n << ' ' << v[2] << "/3/" << n
             << ' ' << v[3] << "/4/" << n << '\n';
    }
    Require(cube.good(), "Could not finish writing the stress cube mesh");
}

void RunRendererDrawsPublishedRuntimeTexturePixelsTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("21kb_published_texture_pixels_" + std::to_string(GetCurrentProcessId()) + "_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;
    std::filesystem::create_directories(root, error);
    Require(!error, "Published texture pixel test could not create its asset directory");
    {
        std::ofstream mesh{ root / "triangle.obj", std::ios::trunc };
        mesh << "v -0.9 -0.9 0\n"
             << "v 0.9 -0.9 0\n"
             << "v 0 0.9 0\n"
             << "vt 0 0\n"
             << "vt 1 0\n"
             << "vt 0.5 1\n"
             << "vn 0 0 1\n"
             << "f 1/1/1 2/2/1 3/3/1\n";
    }
    const kb::assets::AssetId textureId = kb::assets::MakeAssetId("PublishedTexturePixelProof");
    const kb::assets::AssetId materialId = kb::assets::MakeAssetId("PublishedMaterialPixelProof");

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()) &&
            manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()) &&
            manager.RegisterLoader(std::make_unique<RenderTextureAssetLoader>()),
        "Published texture pixel test could not register render asset loaders");
    Require(manager.Mounts().Mount("Game", root) && manager.DiscoverMountedAssets() >= 1U,
        "Published texture pixel test could not discover its mesh");
    PublishCheckerTexture(manager, textureId, root);
    PublishCheckerMaterial(manager, materialId, textureId, root);
    const ResolvedRuntimeMaterialAsset resolvedMaterial = RuntimeMaterialResolver{}.ResolveAsset(manager, materialId);
    Require(resolvedMaterial.status == RuntimeMaterialResolveStatus::Resolved &&
            resolvedMaterial.material.desc.albedoTextureAssetId == textureId.value,
        "Published checker material did not resolve its texture asset");
    const kb::assets::AssetMetadata* mesh = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* material = manager.Registry().Find(materialId);
    Require(mesh != nullptr && material != nullptr,
        "Published texture pixel test lost mesh or material metadata");
    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Textured Mesh", .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = mesh->id.value, .materialAssetId = material->id.value, .castsShadow = false,
    });

    NativeTestSurface surface;
    Require(surface.IsValid(), "Published texture pixel test could not create a hidden D3D11 surface");
    DisplayConfig config{};
    config.syncMode = DisplaySyncMode::Uncapped;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Direct3D11);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Published texture pixel test could not initialize renderer");
    renderer.SetRuntimeAssetDiscoveryEnabled(false);
    {
        ParticleMeshReadbackTarget target;
        Require(target.Initialize(), "Published texture pixel test could not create readback target");
        const RenderSceneSubmitDesc desc{
            .target = target.Binding(),
            .cameraOverride = IdentityCamera(),
            .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
            .clearRgba = 0x101820FFU,
            .editorSceneOverlaysEnabled = false,
            .shadowPassEnabled = false,
            .postProcessEnabled = false,
            .selectionMaskEnabled = false,
            .selectionOutlineEnabled = false,
        };
        SubmitLifecycleFrame(renderer, scene, desc, "Published texture pixel test did not submit its mesh");
        const SceneRenderSubmitStats stats = renderer.LastSceneSubmitStats();
        std::fprintf(stderr,
            "published_texture_pixels visible=%u submitted=%u draws=%u dropped=%u missing_binding=%u missing_resource=%u dimension_mismatch=%u\n",
            stats.visibleMeshCount, stats.submittedMeshCount, stats.submittedDrawCallCount,
            stats.droppedInstanceCount, stats.missingTextureBindingCount,
            stats.missingTextureResourceCount, stats.textureDimensionMismatchCount);
        Require(stats.visibleMeshCount == 1U && stats.submittedMeshCount == 1U &&
                stats.submittedDrawCallCount == 1U && stats.droppedInstanceCount == 0U &&
                stats.missingTextureBindingCount == 0U &&
                stats.missingTextureResourceCount == 0U && stats.textureDimensionMismatchCount == 0U,
            "Published texture pixel test did not bind one textured mesh draw");
        const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
        const RenderResourceRegistry* resources = renderer.SceneResources();
        const Renderer::RuntimeSceneResourceStats resourceStats = renderer.RuntimeResourceStats();
        const RenderMaterialResource* boundMaterial = resourceMap != nullptr && resources != nullptr
            ? resources->FindMaterial(resourceMap->ResolveMaterial(materialId.value)) : nullptr;
        std::fprintf(stderr,
            "published_material_state loaded=%u fallback=%u errors=%u diagnostics=%u textures=%u albedo_asset=%llu\n",
            resourceStats.materialLoadedCount, resourceStats.materialFallbackCount,
            resourceStats.materialErrorCount, resourceStats.materialResolverDiagnosticCount,
            resourceStats.cachedTextureCount,
            static_cast<unsigned long long>(boundMaterial != nullptr ? boundMaterial->albedoTextureAssetId : 0U));
        Require(resourceStats.materialFallbackCount == 0U && resourceStats.materialErrorCount == 0U &&
                boundMaterial != nullptr && boundMaterial->albedoTextureAssetId == textureId.value,
            "Published checker material fell back or lost its texture asset");
        Require(resourceMap != nullptr && resources != nullptr &&
                resources->FindTexture(resourceMap->ResolveTexture(textureId.value, RenderTextureColorSpace::Srgb)) != nullptr,
            "Published texture pixel test did not create the runtime GPU texture");
        const std::vector<std::uint8_t> pixels = target.ReadPixels();
        std::size_t warmPixels = 0U;
        std::size_t coolPixels = 0U;
        std::size_t changedPixels = 0U;
        const std::array<std::uint8_t, 3U> background{pixels[0], pixels[1], pixels[2]};
        int maxRed = 0;
        int maxBlue = 0;
        for (std::size_t offset = 0U; offset < pixels.size(); offset += 4U) {
            const int red = pixels[offset];
            const int blue = pixels[offset + 2U];
            warmPixels += red > blue * 2 && red > 24;
            coolPixels += blue > red * 2 && blue > 24;
            changedPixels += pixels[offset] != background[0] || pixels[offset + 1U] != background[1] ||
                pixels[offset + 2U] != background[2];
            maxRed = std::max(maxRed, red);
            maxBlue = std::max(maxBlue, blue);
        }
        std::fprintf(stderr, "published_texture_pixels warm=%zu cool=%zu changed=%zu max_red=%d max_blue=%d background=%u,%u,%u center=%u,%u,%u\n",
            warmPixels, coolPixels, changedPixels, maxRed, maxBlue,
            background[0], background[1], background[2],
            pixels[(32U * 64U + 32U) * 4U], pixels[(32U * 64U + 32U) * 4U + 1U],
            pixels[(32U * 64U + 32U) * 4U + 2U]);
        Require(warmPixels >= 8U && coolPixels >= 8U,
            "Published texture pixel test did not show both checker colors on the rendered mesh");
    }
    renderer.Shutdown();
    std::filesystem::remove(root / "triangle.obj", error);
    std::filesystem::remove(root, error);
}
#endif

void RunRendererParticleMeshSnapshotSubmitTest() {
    RunRendererSubmitsParticleMeshSnapshotAsOneDrawTest();
#if defined(_WIN32)
    RunRendererDrawsParticleMeshSnapshotPixelsTest();
    RunRendererDrawsPublishedRuntimeTexturePixelsTest();
#endif
}

void RunRendererResourceGroupEnsureFallbacksTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("21kb_resource_group_ensure_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;
    std::filesystem::create_directories(root, error);
    Require(!error, "Resource group test could not create its asset directory");
    WriteTriangleObj(root / "visible.obj");
    WriteTriangleObj(root / "hidden.obj");
    WriteMaterial(root / "base.kbmat", 0U, 0U, 0U, 0U, 0U);
    WriteMaterial(root / "hidden.kbmat", 0U, 0U, 0U, 0U, 0U);
    WriteMaterial(root / "slot.kbmat", 0U, 0U, 0U, 0U, 0U);

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()) &&
            manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()),
        "Resource group test could not register render asset loaders");
    Require(manager.Mounts().Mount("Game", root) && manager.DiscoverMountedAssets() >= 5U,
        "Resource group test could not discover its assets");
    const auto assetId = [&](const char* path) {
        const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(path);
        Require(metadata != nullptr, "Resource group test lost a discovered asset");
        return metadata->id.value;
    };
    const std::uint64_t visibleMeshId = assetId("/Game/visible.obj");
    const std::uint64_t hiddenMeshId = assetId("/Game/hidden.obj");
    const std::uint64_t baseMaterialId = assetId("/Game/base.kbmat");
    const std::uint64_t hiddenMaterialId = assetId("/Game/hidden.kbmat");
    const std::uint64_t slotMaterialId = assetId("/Game/slot.kbmat");
    const kb::scene::SceneEntity visible = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Visible Mesh", .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(visible, kb::scene::MeshRendererComponent{
        .meshAssetId = visibleMeshId, .materialAssetId = baseMaterialId,
    });
    const kb::scene::SceneEntity hidden = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Hidden Mesh", .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(hidden, kb::scene::MeshRendererComponent{
        .meshAssetId = hiddenMeshId, .materialAssetId = hiddenMaterialId,
    });
    scene.Components().Visibility().Set(hidden, kb::scene::VisibilityComponent{.visible = false});

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Resource group test renderer did not initialize");
    const RenderSceneSubmitDesc desc = LifecycleSubmitDesc(1U);
    SubmitLifecycleFrame(renderer, scene, desc, "Resource group test failed to submit a hidden mesh");
    auto resources = renderer.RuntimeResourceStats();
    Require(renderer.LastSceneSubmitStats().visibleMeshCount == 1U &&
            resources.cachedMeshCount == 2U && resources.cachedMaterialCount == 2U,
        "Hidden mesh and material resources must remain ensured despite draw group filtering");

    scene.Components().Visibility().Set(hidden, kb::scene::VisibilityComponent{.visible = true});
    SubmitLifecycleFrame(renderer, scene, desc, "Resource group test failed to submit both visible meshes");
    resources = renderer.RuntimeResourceStats();
    Require(renderer.LastSceneSubmitStats().visibleMeshCount == 2U &&
            resources.cachedMeshCount == 2U && resources.cachedMaterialCount == 2U,
        "Visible draw groups did not preserve both mesh and material resources");

    scene.Components().MeshRenderers().Set(visible, kb::scene::MeshRendererComponent{
        .meshAssetId = visibleMeshId,
        .materialAssetId = baseMaterialId,
        .materialSlotAssetIds = {slotMaterialId},
        .materialSlotOverrideCount = 1U,
    });
    SubmitLifecycleFrame(renderer, scene, desc, "Resource group test failed to submit a material slot override");
    resources = renderer.RuntimeResourceStats();
    Require(resources.cachedMaterialCount == 3U && renderer.SceneResourceMap() != nullptr &&
            renderer.SceneResourceMap()->ResolveMaterial(slotMaterialId).IsValid(),
        "Per-proxy material slot override did not load its material outside the group fast path");
    renderer.Shutdown();
    for (const char* name : {"visible.obj", "hidden.obj", "base.kbmat", "hidden.kbmat", "slot.kbmat"}) {
        std::filesystem::remove(root / name, error);
    }
    std::filesystem::remove(root, error);
}

void RunRendererResourceGroupEnsureTests() {
    RunRendererResourceGroupEnsureFallbacksTest();
    RunRendererReloadsChangedRuntimeMeshAssetTest();
    RunRendererReloadsChangedRuntimeMaterialAssetTest();
}

void RunRendererSceneSubmitScaleBenchmark(bool spiralLayout) {
#if defined(_WIN32)
    const auto runId = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("21kb_scene_submit_scale_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(runId));
    const std::filesystem::path logPath = std::filesystem::current_path() / "Saved" / "Logs" /
        ("renderer-submit-scale-" + std::string{spiralLayout ? "spiral-" : "dense-"} +
            std::to_string(runId) + ".log");
    std::error_code error;
    std::filesystem::create_directories(root, error);
    Require(!error, "Scene submit scale benchmark could not create its asset directory");
    std::filesystem::create_directories(logPath.parent_path(), error);
    Require(!error, "Scene submit scale benchmark could not create its log directory");
    {
        std::ofstream log{logPath, std::ios::trunc};
        Require(log.is_open(), "Scene submit scale benchmark could not open its log");
    }
    const auto report = [&](const std::string& row) {
        {
            std::ofstream log{logPath, std::ios::app};
            Require(log.is_open(), "Scene submit scale benchmark could not append to its log");
            log << row << '\n';
            log.flush();
            Require(log.good(), "Scene submit scale benchmark could not flush its log");
        }
        std::fprintf(stderr, "%s\n", row.c_str());
        std::fflush(stderr);
    };
    report("scene_submit_scale_log=" + logPath.string());

    WriteTriangleObj(root / "triangle.obj");
    WriteTexture(root / "albedo.kbtex", 200U, 100U, 50U);
    kb::scene::Scene scene;
    kb::scene::SceneLightingAccess::SetBasicLightingEnabled(scene, true);
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()) &&
            manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()) &&
            manager.RegisterLoader(std::make_unique<RenderTextureAssetLoader>()),
        "Scene submit scale benchmark could not register asset loaders");
    Require(manager.Mounts().Mount("Game", root) && manager.DiscoverMountedAssets() >= 2U,
        "Scene submit scale benchmark could not discover mesh and texture");
    const kb::assets::AssetMetadata* texture = manager.Registry().FindByPath("/Game/albedo.kbtex");
    Require(texture != nullptr, "Scene submit scale benchmark lost its texture metadata");
    WriteMaterial(root / "paint.kbmat", texture->id.value, 0U, 0U, 0U, 0U);
    {
        std::ofstream materialFile{root / "paint.kbmat", std::ios::app};
        Require(materialFile.is_open(), "Scene submit scale benchmark could not set a double-sided material");
        materialFile << "doubleSided true\n";
    }
    Require(manager.DiscoverMountedAssets() >= 3U,
        "Scene submit scale benchmark could not discover its material");
    const kb::assets::AssetMetadata* mesh = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* material = manager.Registry().FindByPath("/Game/paint.kbmat");
    Require(mesh != nullptr && material != nullptr,
        "Scene submit scale benchmark lost mesh or material metadata");
    const std::uint64_t meshId = mesh->id.value;
    const std::uint64_t materialId = material->id.value;

    NativeTestSurface surface;
    Require(surface.IsValid(), "Scene submit scale benchmark could not create its hidden D3D11 surface");
    DisplayConfig config{};
    config.syncMode = DisplaySyncMode::Uncapped;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Direct3D11);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Scene submit scale benchmark could not initialize D3D11");
    if (const bgfx::Caps* caps = bgfx::getCaps(); caps != nullptr) {
        std::ostringstream adapter;
        adapter << "renderer=" << bgfx::getRendererName(bgfx::getRendererType())
            << " vendor_id=" << caps->vendorId << " device_id=" << caps->deviceId;
        report(adapter.str());
    }
    {
        ParticleMeshReadbackTarget target;
        Require(target.Initialize(), "Scene submit scale benchmark could not create a render target");
        SceneRenderCamera camera = IdentityCamera();
        if (spiralLayout) {
            // Match the default 3D Scene viewport camera when Play has no primary scene camera.
            constexpr float toRadians = 3.14159265358979323846F / 180.0F;
            const float yaw = -45.0F * toRadians;
            const float pitch = -30.0F * toRadians;
            const bx::Vec3 eye{8.0F, 6.0F, -8.0F};
            const bx::Vec3 forward{
                std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
            const bx::Vec3 at{eye.x + forward.x, eye.y + forward.y, eye.z + forward.z};
            const bx::Vec3 up{
                -std::sin(yaw) * std::sin(pitch), std::cos(pitch),
                -std::cos(yaw) * std::sin(pitch)};
            bx::mtxLookAt(camera.view.data(), eye, at, up);
            SceneDepthPolicy::MakePerspective(camera.projection.data(), 60.0F, 1.0F, 0.01F, 1'000.0F,
                SceneDepthPolicy::HomogeneousDepth());
        }
        RenderSceneSubmitDesc desc{
            .target = target.Binding(),
            .cameraOverride = camera,
            .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
            .editorSceneOverlaysEnabled = false,
            .shadowPassEnabled = false,
            .postProcessEnabled = false,
            .selectionMaskEnabled = false,
            .selectionOutlineEnabled = false,
        };
        report(std::string{"backend=D3D11 target=64x64 geometry=triangle texture=albedo.kbtex lights=1_per_1000 layout="} +
            (spiralLayout ? "visual_stress_spiral camera=editor_fallback_8_6_minus8_fov60" :
                "dense_visible camera=identity") + " frames_per_stage=12 warmup_frames=2");
        std::size_t created = 0U;
        for (const std::size_t targetCount : {10'000U, 30'000U, 100'000U}) {
            report("stage_begin=" + std::to_string(targetCount) + " live=" + std::to_string(scene.Entities().Count()));
            const auto creationBegin = std::chrono::steady_clock::now();
            bool creationTimedOut = false;
            while (created < targetCount) {
                const std::size_t index = created;
                kb::scene::SceneObjectDesc object{ .name = "Scale Mesh" };
                if (spiralLayout) {
                    const float radius = 0.65F * std::sqrt(static_cast<float>(index));
                    const float angle = static_cast<float>(index) * 2.39996323F;
                    object.transform.localPosition = kb::scene::Vec3{
                        std::cos(angle) * radius, 0.0F, std::sin(angle) * radius};
                    object.transform.localScale = kb::scene::Vec3{0.45F, 0.45F, 0.45F};
                } else {
                    object.transform.localPosition = kb::scene::Vec3{
                        static_cast<float>((index * 73U) % 1009U) / 560.0F - 0.9F,
                        static_cast<float>((index * 173U) % 1013U) / 562.0F - 0.9F,
                        0.0F };
                    object.transform.localScale = kb::scene::Vec3{0.01F, 0.01F, 0.01F};
                }
                const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(std::move(object));
                Require(entity.IsValid(), "Scene submit scale benchmark failed to create a real entity");
                scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
                    .meshAssetId = meshId, .materialAssetId = materialId, .castsShadow = false });
                Require(scene.Components().MeshRenderers().Has(entity),
                    "Scene submit scale benchmark failed to attach a mesh renderer");
                if (index % 1'000U == 0U) {
                    kb::scene::LightComponent light{};
                    light.kind = kb::scene::LightKind::Point;
                    light.intensity = 6.0F;
                    light.range = 12.0F;
                    light.castsShadow = false;
                    scene.Components().Lights().Set(entity, light);
                    Require(scene.Components().Lights().Has(entity),
                        "Scene submit scale benchmark failed to attach a point light");
                }
                ++created;
                if (created % 1'000U == 0U && std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - creationBegin).count() > 60.0) {
                    creationTimedOut = true;
                    break;
                }
            }
            const double createMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - creationBegin).count();
            if (creationTimedOut) {
                report("scale_create_timeout target=" + std::to_string(targetCount) +
                    " live=" + std::to_string(scene.Entities().Count()) +
                    " create_ms=" + std::to_string(createMs));
                break;
            }
            Require(scene.Entities().Count() == targetCount,
                "Scene submit scale benchmark live count does not match created entities");
            const auto updateBegin = std::chrono::steady_clock::now();
            static_cast<void>(scene.Runtime().Update(0.0F));
            const double runtimeUpdateMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - updateBegin).count();
            desc.synchronizeScene = true;
            const auto firstBegin = std::chrono::steady_clock::now();
            SubmitLifecycleFrame(renderer, scene, desc, "Scene submit scale benchmark failed its initial full sync");
            const double firstFrameMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - firstBegin).count();
            desc.synchronizeScene = false;

            std::vector<double> submitMs;
            std::vector<double> endMs;
            std::vector<double> frameMs;
            std::vector<double> visibilityBuildMs;
            std::vector<double> visibilitySortMs;
            std::vector<double> gpuMs;
            std::vector<double> renderThreadMs;
            bool timedOut = firstFrameMs > 10'000.0;
            for (std::size_t frame = 0U; frame < 12U && !timedOut; ++frame) {
                const auto begin = std::chrono::steady_clock::now();
                Require(renderer.BeginFrame(), "Scene submit scale benchmark could not begin a frame");
                Require(renderer.SubmitScene(scene, desc), "Scene submit scale benchmark could not submit the scene");
                const auto submitted = std::chrono::steady_clock::now();
                renderer.EndFrame();
                const auto ended = std::chrono::steady_clock::now();
                const double submit = std::chrono::duration<double, std::milli>(submitted - begin).count();
                const double present = std::chrono::duration<double, std::milli>(ended - submitted).count();
                timedOut = submit + present > 10'000.0;
                if (frame < 2U) continue;
                submitMs.push_back(submit);
                endMs.push_back(present);
                frameMs.push_back(submit + present);
                visibilityBuildMs.push_back(renderer.LastSceneVisibilityBuildMilliseconds());
                visibilitySortMs.push_back(renderer.LastSceneVisibilitySortMilliseconds());
                if (const bgfx::Stats* stats = bgfx::getStats(); stats != nullptr) {
                    if (stats->gpuTimerFreq > 0 && stats->gpuTimeEnd > stats->gpuTimeBegin) {
                        gpuMs.push_back(static_cast<double>(stats->gpuTimeEnd - stats->gpuTimeBegin) *
                            1000.0 / static_cast<double>(stats->gpuTimerFreq));
                    }
                    if (stats->cpuTimerFreq > 0 && stats->cpuTimeEnd > stats->cpuTimeBegin) {
                        renderThreadMs.push_back(static_cast<double>(stats->cpuTimeEnd - stats->cpuTimeBegin) *
                            1000.0 / static_cast<double>(stats->cpuTimerFreq));
                    }
                }
            }
            const auto medianP95 = [](std::vector<double>& values) {
                if (values.empty()) return std::pair{0.0, 0.0};
                std::sort(values.begin(), values.end());
                return std::pair{values[values.size() / 2U],
                    values[std::min(values.size() - 1U, (values.size() * 95U + 99U) / 100U - 1U)]};
            };
            const auto [submitMedian, submitP95] = medianP95(submitMs);
            const auto [endMedian, endP95] = medianP95(endMs);
            const auto [frameMedian, frameP95] = medianP95(frameMs);
            const auto [visibilityBuildMedian, visibilityBuildP95] = medianP95(visibilityBuildMs);
            const auto [visibilitySortMedian, visibilitySortP95] = medianP95(visibilitySortMs);
            const auto [gpuMedian, gpuP95] = medianP95(gpuMs);
            const auto [threadMedian, threadP95] = medianP95(renderThreadMs);
            const SceneRenderSubmitStats stats = renderer.LastSceneSubmitStats();
            std::ostringstream row;
            row << "entities=" << targetCount << " live=" << scene.Entities().Count()
                << " create_ms=" << createMs << " runtime_update_ms=" << runtimeUpdateMs
                << " first_frame_ms=" << firstFrameMs
                << " frame_p50_ms=" << frameMedian << " frame_p95_ms=" << frameP95
                << " visibility_build_p50_ms=" << visibilityBuildMedian
                << " visibility_build_p95_ms=" << visibilityBuildP95
                << " visibility_sort_p50_ms=" << visibilitySortMedian
                << " visibility_sort_p95_ms=" << visibilitySortP95
                << " submit_p50_ms=" << submitMedian << " submit_p95_ms=" << submitP95
                << " end_p50_ms=" << endMedian << " end_p95_ms=" << endP95
                << " gpu_p50_ms=" << gpuMedian << " gpu_p95_ms=" << gpuP95
                << " gpu_samples=" << gpuMs.size()
                << " render_thread_p50_ms=" << threadMedian << " render_thread_p95_ms=" << threadP95
                << " draw_calls=" << stats.submittedDrawCallCount
                << " visible=" << stats.visibleMeshCount << " submitted=" << stats.submittedMeshCount
                << " culled=" << stats.culledInstanceCount << " dropped=" << stats.droppedInstanceCount
                << " gpu_driven_state=" << static_cast<int>(stats.gpuDrivenFeatureState)
                << " gpu_driven_fallbacks=" << stats.gpuDrivenFallbackCount
                << " gpu_driven_upload_bytes=" << stats.gpuDrivenUploadBytes
                << " lights=" << stats.sceneLightCount << " forward_lights=" << stats.submittedForwardLightCount
                << " upload_bytes=" << stats.instanceUploadBytes
                << " missing_texture_bindings=" << stats.missingTextureBindingCount
                << " missing_texture_resources=" << stats.missingTextureResourceCount
                << " texture_dimension_mismatches=" << stats.textureDimensionMismatchCount
                << " timeout=" << (timedOut ? 1 : 0);
            report(row.str());
            Require(stats.visibleMeshCount > 0U && stats.submittedMeshCount > 0U &&
                    stats.submittedDrawCallCount > 0U && stats.missingTextureBindingCount == 0U &&
                    stats.missingTextureResourceCount == 0U && stats.textureDimensionMismatchCount == 0U,
                "Scene submit scale benchmark did not render a textured mesh batch");
            if (!timedOut) {
                SceneRenderCamera farCamera = camera;
                farCamera.view[12] += 1'000.0F;
                desc.cameraOverride = farCamera;
                std::vector<double> farFrameMs;
                std::vector<double> farVisibilitySortMs;
                for (std::size_t frame = 0U; frame < 5U; ++frame) {
                    const auto begin = std::chrono::steady_clock::now();
                    SubmitLifecycleFrame(renderer, scene, desc,
                        "Scene submit scale benchmark failed its far-camera control frame");
                    const double elapsed = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - begin).count();
                    if (frame >= 2U) farFrameMs.push_back(elapsed);
                    if (frame >= 2U) farVisibilitySortMs.push_back(renderer.LastSceneVisibilitySortMilliseconds());
                    if (elapsed > 10'000.0) {
                        timedOut = true;
                        break;
                    }
                }
                const auto [farMedian, farP95] = medianP95(farFrameMs);
                const auto [farSortMedian, farSortP95] = medianP95(farVisibilitySortMs);
                const SceneRenderSubmitStats farStats = renderer.LastSceneSubmitStats();
                std::ostringstream farRow;
                farRow << "far_camera_entities=" << targetCount
                    << " frame_p50_ms=" << farMedian << " frame_p95_ms=" << farP95
                    << " visibility_sort_p50_ms=" << farSortMedian
                    << " visibility_sort_p95_ms=" << farSortP95
                    << " visible=" << farStats.visibleMeshCount
                    << " submitted=" << farStats.submittedMeshCount
                    << " culled=" << farStats.culledInstanceCount
                    << " draws=" << farStats.submittedDrawCallCount;
                report(farRow.str());
                Require(farStats.visibleMeshCount == 0U && farStats.submittedMeshCount == 0U,
                    "Scene submit scale benchmark far camera did not cull the mesh batch");
                desc.cameraOverride = camera;
            }
            if (timedOut || frameP95 > 1'000.0 || stats.droppedInstanceCount != 0U) {
                report("scale_stopped_after=" + std::to_string(targetCount));
                break;
            }
        }
    }
    renderer.Shutdown();
    std::filesystem::remove(root / "triangle.obj", error);
    std::filesystem::remove(root / "albedo.kbtex", error);
    std::filesystem::remove(root / "paint.kbmat", error);
    std::filesystem::remove(root, error);
#else
    static_cast<void>(spiralLayout);
#endif
}

void RunRendererPacedSceneSubmitStressBenchmark(bool staticMillionSnapshot,
    bool gpuDrivenDispatchEnabled, unsigned maxForwardLights) {
#if defined(_WIN32)
    using Clock = std::chrono::steady_clock;
    constexpr std::size_t targetEntities = 1'000'000U;
    constexpr std::size_t entitiesPerSecond = 1'000U;
    constexpr std::uint16_t width = 1'280U;
    constexpr std::uint16_t height = 720U;
    const auto runId = Clock::now().time_since_epoch().count();
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("21kb_paced_render_stress_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(runId));
    const std::filesystem::path logPath = std::filesystem::current_path() / "Saved" / "Logs" /
        ((staticMillionSnapshot ? "renderer-million-scene-snapshot-" : "renderer-paced-scene-stress-") +
            std::to_string(runId) + ".log");
    std::error_code error;
    std::filesystem::create_directories(root, error);
    Require(!error, "Paced scene stress could not create its asset directory");
    std::filesystem::create_directories(logPath.parent_path(), error);
    Require(!error, "Paced scene stress could not create its log directory");
    {
        std::ofstream log{logPath, std::ios::trunc};
        Require(log.is_open(), "Paced scene stress could not open its log");
    }
    const auto report = [&](const std::string& row) {
        {
            std::ofstream log{logPath, std::ios::app};
            Require(log.is_open(), "Paced scene stress could not append to its log");
            log << row << '\n';
            log.flush();
            Require(log.good(), "Paced scene stress could not flush its log");
        }
        std::fprintf(stderr, "%s\n", row.c_str());
        std::fflush(stderr);
    };
    report("paced_scene_stress_log=" + logPath.string());

    WriteStressCubeObj(root / "cube.obj");
    kb::scene::Scene scene;
    kb::scene::SceneLightingAccess::SetBasicLightingEnabled(scene, true);
    kb::scene::SceneRuntime runtime = scene.Runtime();
    runtime.SetPlaying(true);
    runtime.SetEcsProfilerEnabled(true);
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()) &&
            manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()) &&
            manager.RegisterLoader(std::make_unique<RenderTextureAssetLoader>()),
        "Paced scene stress could not register asset loaders");
    Require(manager.Mounts().Mount("Game", root) && manager.DiscoverMountedAssets() >= 1U,
        "Paced scene stress could not discover its cube mesh");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/cube.obj");
    Require(meshMetadata != nullptr, "Paced scene stress lost its cube mesh metadata");
    const std::uint64_t meshId = meshMetadata->id.value;
    const kb::assets::AssetId textureId = kb::assets::MakeAssetId("PacedSceneStress:Checker");
    const kb::assets::AssetId materialId = kb::assets::MakeAssetId("PacedSceneStress:Material");
    PublishCheckerTexture(manager, textureId, root);
    PublishCheckerMaterial(manager, materialId, textureId, root);

    NativeTestSurface surface{width, height};
    Require(surface.IsValid(), "Paced scene stress could not create a hidden D3D11 surface");
    DisplayConfig config{};
    config.syncMode = DisplaySyncMode::Uncapped;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Direct3D11);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Paced scene stress could not initialize D3D11");
    renderer.SetRuntimeAssetDiscoveryEnabled(false);
    if (const bgfx::Caps* caps = bgfx::getCaps(); caps != nullptr) {
        std::ostringstream adapter;
        adapter << "adapter=" << bgfx::getRendererName(bgfx::getRendererType())
                << " vendor_id=" << caps->vendorId << " device_id=" << caps->deviceId;
        report(adapter.str());
    }
    ParticleMeshReadbackTarget target;
    Require(target.Initialize(width, height), "Paced scene stress could not create a render target");
    SceneRenderCamera camera = IdentityCamera();
    constexpr float toRadians = 3.14159265358979323846F / 180.0F;
    const float yaw = -45.0F * toRadians;
    const float pitch = -30.0F * toRadians;
    const bx::Vec3 eye{8.0F, 6.0F, -8.0F};
    const bx::Vec3 forward{
        std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    const bx::Vec3 at{eye.x + forward.x, eye.y + forward.y, eye.z + forward.z};
    const bx::Vec3 up{-std::sin(yaw) * std::sin(pitch), std::cos(pitch), -std::cos(yaw) * std::sin(pitch)};
    bx::mtxLookAt(camera.view.data(), eye, at, up);
    SceneDepthPolicy::MakePerspective(camera.projection.data(), 60.0F,
        static_cast<float>(width) / static_cast<float>(height), 0.01F, 1'000.0F,
        SceneDepthPolicy::HomogeneousDepth());
    RenderSceneSubmitDesc desc{
        .target = target.Binding(),
        .cameraOverride = camera,
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .editorSceneOverlaysEnabled = false,
        .screenUIEnabled = true,
        .shadowPassEnabled = false,
        .postProcessEnabled = false,
        .selectionMaskEnabled = false,
        .selectionOutlineEnabled = false,
        .synchronizeScene = true,
        .transformAffineSync = true,
    };
    desc.gpuDrivenRuntimeDispatchEnabled = gpuDrivenDispatchEnabled;
    desc.lightingConfig.maxForwardLights = maxForwardLights;
    report(std::string{"config target=1000000 mode="} +
        (staticMillionSnapshot ? "static_snapshot" : "paced_1000_per_second") +
        " mesh=cube texture=published_checker material=published lights=1_per_1000 "
        "lighting=basic viewport=1280x720 vsync=off camera=editor_fallback "
        "play=true ecs_profiler=true sync=initial_full_then_delta "
        "frame_watchdog_ms=10000 memory=external_supervisor compute=" +
        std::to_string(gpuDrivenDispatchEnabled) + " forward_light_budget=" +
        std::to_string(maxForwardLights) +
        (staticMillionSnapshot ? " frames=1_full_plus_8_steady_plus_5_far" : " hold_seconds=30"));

    const auto createEntity = [&](std::size_t index) {
        const float radius = 0.65F * std::sqrt(static_cast<float>(index));
        const float angle = static_cast<float>(index) * 2.39996323F;
        kb::scene::SceneObjectDesc object{.name = "Stress Cube"};
        object.transform.localPosition = kb::scene::Vec3{
            std::cos(angle) * radius, 0.0F, std::sin(angle) * radius};
        object.transform.localScale = kb::scene::Vec3{0.45F, 0.45F, 0.45F};
        const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(std::move(object));
        Require(entity.IsValid(), "Paced scene stress could not create a real entity");
        scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
            .meshAssetId = meshId, .materialAssetId = materialId.value, .castsShadow = false});
        Require(scene.Components().MeshRenderers().Has(entity),
            "Paced scene stress could not attach a mesh renderer");
        if (index % 1'000U == 0U) {
            kb::scene::LightComponent light{};
            light.kind = kb::scene::LightKind::Point;
            light.color = kb::scene::Vec3{0.65F, 0.8F, 1.0F};
            light.intensity = 6.0F;
            light.range = 12.0F;
            light.castsShadow = false;
            scene.Components().Lights().Set(entity, light);
            Require(scene.Components().Lights().Has(entity),
                "Paced scene stress could not attach a point light");
        }
        return entity;
    };

    static_cast<void>(createEntity(0U));
    static_cast<void>(runtime.Update(0.0F));
    const auto fullSyncBegin = Clock::now();
    Require(renderer.BeginFrame() && renderer.SubmitScene(scene, desc),
        "Paced scene stress failed its first full scene submit");
    const double firstFullSyncMs = renderer.LastSceneSynchronizationMilliseconds();
    renderer.EndFrame();
    const double firstFrameMs = std::chrono::duration<double, std::milli>(Clock::now() - fullSyncBegin).count();
    const SceneRenderSubmitStats firstStats = renderer.LastSceneSubmitStats();
    Require(firstStats.visibleMeshCount == 1U && firstStats.submittedMeshCount == 1U &&
            firstStats.submittedDrawCallCount > 0U && firstStats.droppedInstanceCount == 0U &&
            firstStats.missingTextureBindingCount == 0U && firstStats.missingTextureResourceCount == 0U &&
            firstStats.textureDimensionMismatchCount == 0U,
        "Paced scene stress first frame did not draw one correctly textured mesh");
    const std::vector<std::uint8_t> pixels = target.ReadPixels();
    std::size_t warmPixels = 0U;
    std::size_t coolPixels = 0U;
    for (std::size_t offset = 0U; offset < pixels.size(); offset += 4U) {
        const int red = pixels[offset];
        const int blue = pixels[offset + 2U];
        warmPixels += red >= 10 && red > blue * 3 / 2;
        coolPixels += blue >= 10 && blue > red * 3 / 2;
    }
    {
        std::ostringstream preflight;
        preflight << "preflight live=1 visible=" << firstStats.visibleMeshCount
                  << " draws=" << firstStats.submittedDrawCallCount
                  << " warm_pixels=" << warmPixels << " cool_pixels=" << coolPixels
                  << " first_frame_ms=" << firstFrameMs << " full_sync_ms=" << firstFullSyncMs;
        report(preflight.str());
    }
    Require(warmPixels >= 8U && coolPixels >= 8U,
        "Paced scene stress did not read back both colors of the published checker");

    if (staticMillionSnapshot) {
        const Clock::time_point creationBegin = Clock::now();
        std::size_t created = 1U;
        bool creationTimedOut = false;
        for (; created < targetEntities; ++created) {
            static_cast<void>(createEntity(created));
            if ((created + 1U) % 10'000U == 0U) {
                const double elapsed = std::chrono::duration<double>(Clock::now() - creationBegin).count();
                report("create_progress live=" + std::to_string(scene.Entities().Count()) +
                    " created=" + std::to_string(created + 1U) +
                    " elapsed_s=" + std::to_string(elapsed));
                if (elapsed > 300.0) {
                    creationTimedOut = true;
                    ++created;
                    break;
                }
            }
        }
        if (creationTimedOut || scene.Entities().Count() != targetEntities) {
            report("snapshot_stopped reason=" +
                std::string{creationTimedOut ? "creation_over_300s" : "live_count_mismatch"} +
                " live=" + std::to_string(scene.Entities().Count()) +
                " created=" + std::to_string(created));
            renderer.Shutdown();
            std::filesystem::remove(root / "cube.obj", error);
            std::filesystem::remove(root, error);
            return;
        }
        const Clock::time_point updateBegin = Clock::now();
        static_cast<void>(runtime.Update(0.0F));
        const Clock::time_point updateEnd = Clock::now();
        report("snapshot_ready live=" + std::to_string(scene.Entities().Count()) +
            " created=" + std::to_string(created) +
            " creation_ms=" + std::to_string(
                std::chrono::duration<double, std::milli>(updateBegin - creationBegin).count()) +
            " runtime_update_ms=" + std::to_string(
                std::chrono::duration<double, std::milli>(updateEnd - updateBegin).count()));
        bool snapshotValid = true;
        int completedFrames = 0;
        std::vector<double> steadyFrameMs;
        std::vector<double> steadyGpuMs;
        for (int frame = 0; frame < 9; ++frame) {
            const std::size_t affineBeforeUpdate = runtime.TransformRenderProxyUpdateEntities().size();
            double runtimeUpdateMs = 0.0;
            if (frame != 0) {
                const Clock::time_point frameUpdateBegin = Clock::now();
                static_cast<void>(runtime.Update(0.0F));
                runtimeUpdateMs = std::chrono::duration<double, std::milli>(
                    Clock::now() - frameUpdateBegin).count();
            }
            const std::size_t affineAfterUpdate = runtime.TransformRenderProxyUpdateEntities().size();
            desc.synchronizeScene = frame == 0;
            const Clock::time_point frameBegin = Clock::now();
            Require(renderer.BeginFrame() && renderer.SubmitScene(scene, desc),
                "Million scene snapshot failed to submit its real scene");
            const Clock::time_point submitted = Clock::now();
            const double syncMs = renderer.LastSceneSynchronizationMilliseconds();
            const double visibilityMs = renderer.LastSceneVisibilityBuildMilliseconds();
            const double sortMs = renderer.LastSceneVisibilitySortMilliseconds();
            const SceneRenderSubmitStats stats = renderer.LastSceneSubmitStats();
            renderer.EndFrame();
            const Clock::time_point frameEnd = Clock::now();
            double gpuMs = 0.0;
            bool gpuSampleValid = false;
            if (const bgfx::Stats* gpu = bgfx::getStats(); gpu != nullptr &&
                gpu->gpuTimerFreq > 0 && gpu->gpuTimeEnd > gpu->gpuTimeBegin) {
                gpuMs = static_cast<double>(gpu->gpuTimeEnd - gpu->gpuTimeBegin) *
                    1'000.0 / static_cast<double>(gpu->gpuTimerFreq);
                gpuSampleValid = true;
            }
            const double frameMs = std::chrono::duration<double, std::milli>(frameEnd - frameBegin).count();
            std::ostringstream row;
            row << "snapshot_frame=" << frame
                << " live=" << scene.Entities().Count()
                << " sync_mode=" << (frame == 0 ? "full" : "steady")
                << " runtime_update_ms=" << runtimeUpdateMs
                << " affine_before_update=" << affineBeforeUpdate
                << " affine_after_update=" << affineAfterUpdate
                << " frame_ms=" << frameMs
                << " sync_ms=" << syncMs
                << " visibility_build_ms=" << visibilityMs
                << " visibility_sort_ms=" << sortMs
                << " submit_ms=" << std::chrono::duration<double, std::milli>(submitted - frameBegin).count()
                << " end_ms=" << std::chrono::duration<double, std::milli>(frameEnd - submitted).count()
                << " gpu_ms=" << gpuMs << " gpu_sample_valid=" << gpuSampleValid
                << " draw_calls=" << stats.submittedDrawCallCount
                << " visible=" << stats.visibleMeshCount
                << " submitted=" << stats.submittedMeshCount
                << " culled=" << stats.culledInstanceCount
                << " dropped=" << stats.droppedInstanceCount
                << " lights=" << stats.sceneLightCount
                << " forward_lights=" << stats.submittedForwardLightCount
                << " upload_bytes=" << stats.instanceUploadBytes
                << " gpu_driven_feature_state=" << static_cast<int>(stats.gpuDrivenFeatureState)
                << " gpu_culling_dispatches=" << stats.gpuCullingDispatchCount
                << " gpu_driven_upload_bytes=" << stats.gpuDrivenUploadBytes
                << " missing_texture_bindings=" << stats.missingTextureBindingCount
                << " missing_texture_resources=" << stats.missingTextureResourceCount
                << " texture_dimension_mismatches=" << stats.textureDimensionMismatchCount;
            report(row.str());
            ++completedFrames;
            if (frame != 0) {
                steadyFrameMs.push_back(frameMs);
                if (gpuSampleValid) steadyGpuMs.push_back(gpuMs);
            }
            if (frameMs > 10'000.0 || stats.visibleMeshCount == 0U ||
                stats.submittedMeshCount == 0U || stats.submittedDrawCallCount == 0U ||
                stats.sceneLightCount == 0U ||
                stats.submittedForwardLightCount != maxForwardLights ||
                (!gpuDrivenDispatchEnabled && (stats.gpuCullingDispatchCount != 0U ||
                    stats.gpuDrivenUploadBytes != 0U)) ||
                stats.droppedInstanceCount != 0U ||
                stats.missingTextureBindingCount != 0U || stats.missingTextureResourceCount != 0U ||
                stats.textureDimensionMismatchCount != 0U) {
                report("snapshot_stopped reason=" + std::string{frameMs > 10'000.0 ?
                    "frame_over_10s" : "render_correctness_failure"});
                snapshotValid = false;
                break;
            }
        }
        const auto percentile = [](std::vector<double>& samples, std::size_t numerator) {
            if (samples.empty()) return 0.0;
            std::sort(samples.begin(), samples.end());
            return samples[std::min(samples.size() - 1U,
                (samples.size() * numerator + 99U) / 100U - 1U)];
        };
        report("snapshot_steady_summary frame_samples=" + std::to_string(steadyFrameMs.size()) +
            " frame_p50_ms=" + std::to_string(percentile(steadyFrameMs, 50U)) +
            " frame_p95_ms=" + std::to_string(percentile(steadyFrameMs, 95U)) +
            " gpu_samples=" + std::to_string(steadyGpuMs.size()) +
            " gpu_p50_ms=" + std::to_string(percentile(steadyGpuMs, 50U)) +
            " gpu_p95_ms=" + std::to_string(percentile(steadyGpuMs, 95U)));
        if (snapshotValid && completedFrames == 9) {
            const std::vector<std::uint8_t> referencePixels = target.ReadPixels();
            std::size_t snapshotWarmPixels = 0U;
            std::size_t snapshotCoolPixels = 0U;
            std::uint64_t pixelHash = 14'695'981'039'346'656'037ULL;
            for (const std::uint8_t channel : referencePixels) {
                pixelHash = (pixelHash ^ channel) * 1'099'511'628'211ULL;
            }
            for (std::size_t offset = 0U; offset < referencePixels.size(); offset += 4U) {
                const int red = referencePixels[offset];
                const int blue = referencePixels[offset + 2U];
                snapshotWarmPixels += red >= 10 && red > blue * 3 / 2;
                snapshotCoolPixels += blue >= 10 && blue > red * 3 / 2;
            }
            report("snapshot_checker warm_pixels=" + std::to_string(snapshotWarmPixels) +
                " cool_pixels=" + std::to_string(snapshotCoolPixels) +
                " pixel_hash=" + std::to_string(pixelHash));
            snapshotValid = snapshotWarmPixels >= 8U && snapshotCoolPixels >= 8U;
        }
        if (snapshotValid && completedFrames == 9) {
            SceneRenderCamera farCamera = camera;
            farCamera.view[12] += 1'000.0F;
            desc.cameraOverride = farCamera;
            std::vector<double> farFrameMs;
            std::vector<double> farGpuMs;
            for (int frame = 0; frame < 5; ++frame) {
                const Clock::time_point updateStart = Clock::now();
                static_cast<void>(runtime.Update(0.0F));
                const Clock::time_point frameStart = Clock::now();
                Require(renderer.BeginFrame() && renderer.SubmitScene(scene, desc),
                    "Million scene far-camera control failed to submit");
                const Clock::time_point submitted = Clock::now();
                const double visibilityMs = renderer.LastSceneVisibilityBuildMilliseconds();
                const double sortMs = renderer.LastSceneVisibilitySortMilliseconds();
                const SceneRenderSubmitStats stats = renderer.LastSceneSubmitStats();
                renderer.EndFrame();
                const Clock::time_point frameEnd = Clock::now();
                const double frameMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
                double gpuMs = 0.0;
                bool gpuSampleValid = false;
                if (const bgfx::Stats* gpu = bgfx::getStats(); gpu != nullptr &&
                    gpu->gpuTimerFreq > 0 && gpu->gpuTimeEnd > gpu->gpuTimeBegin) {
                    gpuMs = static_cast<double>(gpu->gpuTimeEnd - gpu->gpuTimeBegin) *
                        1'000.0 / static_cast<double>(gpu->gpuTimerFreq);
                    gpuSampleValid = true;
                }
                std::ostringstream row;
                row << "far_control_frame=" << frame
                    << " live=" << scene.Entities().Count()
                    << " runtime_update_ms=" << std::chrono::duration<double, std::milli>(frameStart - updateStart).count()
                    << " frame_ms=" << frameMs
                    << " submit_ms=" << std::chrono::duration<double, std::milli>(submitted - frameStart).count()
                    << " visibility_build_ms=" << visibilityMs
                    << " visibility_sort_ms=" << sortMs
                    << " gpu_ms=" << gpuMs << " gpu_sample_valid=" << gpuSampleValid
                    << " visible=" << stats.visibleMeshCount
                    << " submitted=" << stats.submittedMeshCount
                    << " dropped=" << stats.droppedInstanceCount
                    << " draws=" << stats.submittedDrawCallCount;
                report(row.str());
                if (frame >= 2) {
                    farFrameMs.push_back(frameMs);
                    if (gpuSampleValid) farGpuMs.push_back(gpuMs);
                }
                if (frameMs > 10'000.0 || stats.visibleMeshCount != 0U ||
                    stats.submittedMeshCount != 0U || stats.droppedInstanceCount != 0U) {
                    snapshotValid = false;
                    report("far_control_stopped reason=timeout_or_visibility_mismatch");
                    break;
                }
            }
            report("far_control_summary frame_samples=" + std::to_string(farFrameMs.size()) +
                " frame_p50_ms=" + std::to_string(percentile(farFrameMs, 50U)) +
                " frame_p95_ms=" + std::to_string(percentile(farFrameMs, 95U)) +
                " gpu_samples=" + std::to_string(farGpuMs.size()) +
                " gpu_p50_ms=" + std::to_string(percentile(farGpuMs, 50U)));
        }
        report("snapshot_finished live=" + std::to_string(scene.Entities().Count()) +
            " frames=" + std::to_string(completedFrames) +
            " valid=" + std::to_string(snapshotValid && completedFrames == 9));
        renderer.Shutdown();
        std::filesystem::remove(root / "cube.obj", error);
        std::filesystem::remove(root, error);
        return;
    }

    desc.synchronizeScene = false;
    std::size_t created = 1U;
    std::size_t lastReportedCount = created;
    double slowDurationSeconds = 0.0;
    bool spawning = true;
    Clock::time_point holdBegin{};
    const Clock::time_point started = Clock::now();
    Clock::time_point lastReported = started;
    Clock::time_point previousFrame = started;
    struct Bucket {
        std::vector<double> frameTimesMs;
        double spawnMs = 0.0;
        double runtimeMs = 0.0;
        double renderSyncMs = 0.0;
        double visibilityBuildMs = 0.0;
        double visibilitySortMs = 0.0;
        double visibilityPublishMs = 0.0;
        double submitMs = 0.0;
        double endMs = 0.0;
        double gpuMs = 0.0;
        std::size_t gpuSamples = 0U;
    } bucket;
    bool timedOut = false;
    while (true) {
        const Clock::time_point frameBegin = Clock::now();
        if (!spawning && frameBegin - holdBegin >= std::chrono::seconds{30}) {
            break;
        }
        const float deltaSeconds = std::clamp(
            std::chrono::duration<float>(frameBegin - previousFrame).count(), 0.0F, 1.0F / 15.0F);
        previousFrame = frameBegin;
        std::vector<std::uint64_t> dirtyEntityIds;
        for (const kb::scene::SceneEntity entity : runtime.RenderProxyUpdateEntities()) {
            dirtyEntityIds.push_back(entity.Id());
        }
        if (spawning) {
            const double elapsed = std::chrono::duration<double>(frameBegin - started).count();
            const std::size_t scheduled = std::min(targetEntities,
                1U + static_cast<std::size_t>(elapsed * static_cast<double>(entitiesPerSecond)));
            const std::size_t toCreate = std::min(scheduled - created, std::size_t{5'000U});
            for (std::size_t index = 0U; index < toCreate; ++index) {
                dirtyEntityIds.push_back(createEntity(created).Id());
                ++created;
            }
        }
        const Clock::time_point spawned = Clock::now();
        static_cast<void>(runtime.Update(deltaSeconds));
        const Clock::time_point updated = Clock::now();
        for (const kb::scene::SceneEntity entity : runtime.RenderProxyUpdateEntities()) {
            dirtyEntityIds.push_back(entity.Id());
        }
        std::sort(dirtyEntityIds.begin(), dirtyEntityIds.end());
        dirtyEntityIds.erase(std::unique(dirtyEntityIds.begin(), dirtyEntityIds.end()), dirtyEntityIds.end());
        desc.dirtySceneEntityIds = std::span<const std::uint64_t>{dirtyEntityIds};
        Require(renderer.BeginFrame() && renderer.SubmitScene(scene, desc),
            "Paced scene stress failed a delta scene submit");
        const Clock::time_point submitted = Clock::now();
        const double renderSyncMs = renderer.LastSceneSynchronizationMilliseconds();
        const double visibilityBuildMs = renderer.LastSceneVisibilityBuildMilliseconds();
        const double visibilitySortMs = renderer.LastSceneVisibilitySortMilliseconds();
        const double visibilityPublishMs = renderer.LastSceneVisibilityPublishMilliseconds();
        const SceneRenderSubmitStats stats = renderer.LastSceneSubmitStats();
        renderer.EndFrame();
        const Clock::time_point frameEnd = Clock::now();
        const double frameMs = std::chrono::duration<double, std::milli>(frameEnd - frameBegin).count();
        bucket.frameTimesMs.push_back(frameMs);
        bucket.spawnMs += std::chrono::duration<double, std::milli>(spawned - frameBegin).count();
        bucket.runtimeMs += std::chrono::duration<double, std::milli>(updated - spawned).count();
        bucket.renderSyncMs += renderSyncMs;
        bucket.visibilityBuildMs += visibilityBuildMs;
        bucket.visibilitySortMs += visibilitySortMs;
        bucket.visibilityPublishMs += visibilityPublishMs;
        bucket.submitMs += std::chrono::duration<double, std::milli>(submitted - updated).count();
        bucket.endMs += std::chrono::duration<double, std::milli>(frameEnd - submitted).count();
        if (const bgfx::Stats* gpu = bgfx::getStats(); gpu != nullptr &&
            gpu->gpuTimerFreq > 0 && gpu->gpuTimeEnd > gpu->gpuTimeBegin) {
            bucket.gpuMs += static_cast<double>(gpu->gpuTimeEnd - gpu->gpuTimeBegin) *
                1'000.0 / static_cast<double>(gpu->gpuTimerFreq);
            ++bucket.gpuSamples;
        }
        if (frameEnd - lastReported >= std::chrono::seconds{1} || frameMs > 10'000.0) {
            std::sort(bucket.frameTimesMs.begin(), bucket.frameTimesMs.end());
            const std::size_t count = bucket.frameTimesMs.size();
            const double p50 = bucket.frameTimesMs[count / 2U];
            const double p95 = bucket.frameTimesMs[std::min(count - 1U, (count * 95U + 99U) / 100U - 1U)];
            const double interval = std::chrono::duration<double>(frameEnd - lastReported).count();
            const double perFrame = 1.0 / static_cast<double>(count);
            std::ostringstream row;
            row << "second=" << std::chrono::duration<double>(frameEnd - started).count()
                << " live=" << scene.Entities().Count() << " created=" << created
                << " spawn_rate=" << static_cast<double>(created - lastReportedCount) / interval
                << " fps=" << static_cast<double>(count) / interval
                << " frame_p50_ms=" << p50 << " frame_p95_ms=" << p95
                << " spawn_ms=" << bucket.spawnMs * perFrame
                << " runtime_update_ms=" << bucket.runtimeMs * perFrame
                << " render_sync_mode=delta render_sync_ms=" << bucket.renderSyncMs * perFrame
                << " visibility_build_ms=" << bucket.visibilityBuildMs * perFrame
                << " visibility_sort_ms=" << bucket.visibilitySortMs * perFrame
                << " visibility_publish_ms=" << bucket.visibilityPublishMs * perFrame
                << " submit_ms=" << bucket.submitMs * perFrame
                << " end_ms=" << bucket.endMs * perFrame
                << " gpu_ms=" << (bucket.gpuSamples != 0U ? bucket.gpuMs / bucket.gpuSamples : 0.0)
                << " gpu_samples=" << bucket.gpuSamples
                << " draw_calls=" << stats.submittedDrawCallCount
                << " visible=" << stats.visibleMeshCount
                << " submitted=" << stats.submittedMeshCount
                << " culled=" << stats.culledInstanceCount
                << " dropped=" << stats.droppedInstanceCount
                << " lights=" << stats.sceneLightCount
                << " forward_lights=" << stats.submittedForwardLightCount
                << " upload_bytes=" << stats.instanceUploadBytes
                << " gpu_driven_upload_bytes=" << stats.gpuDrivenUploadBytes
                << " missing_texture_bindings=" << stats.missingTextureBindingCount
                << " missing_texture_resources=" << stats.missingTextureResourceCount
                << " texture_dimension_mismatches=" << stats.textureDimensionMismatchCount
                << " spawning=" << (spawning ? 1 : 0);
            report(row.str());
            if (spawning && (scene.Entities().Count() != created || stats.droppedInstanceCount != 0U ||
                stats.missingTextureBindingCount != 0U || stats.missingTextureResourceCount != 0U ||
                stats.textureDimensionMismatchCount != 0U)) {
                report("correctness_failure_live_or_render_resources");
                spawning = false;
                holdBegin = frameEnd;
            } else if (spawning && p95 > 16.67) {
                slowDurationSeconds += interval;
                if (slowDurationSeconds >= 5.0) {
                    spawning = false;
                    holdBegin = frameEnd;
                    report("spawn_paused_reason=p95_over_16_67ms_for_5_seconds at_live=" +
                        std::to_string(scene.Entities().Count()));
                }
            } else if (spawning) {
                slowDurationSeconds = 0.0;
            }
            if (spawning && created == targetEntities) {
                spawning = false;
                holdBegin = frameEnd;
                report("target_reached_live=" + std::to_string(scene.Entities().Count()));
            }
            lastReported = frameEnd;
            lastReportedCount = created;
            bucket = {};
        }
        if (frameMs > 10'000.0) {
            report("frame_watchdog_stop_ms=" + std::to_string(frameMs) +
                " live=" + std::to_string(scene.Entities().Count()));
            timedOut = true;
            break;
        }
    }
    report(std::string{"finished_live="} + std::to_string(scene.Entities().Count()) +
        " created=" + std::to_string(created) + " timeout=" + (timedOut ? "1" : "0"));
    renderer.Shutdown();
    std::filesystem::remove(root / "cube.obj", error);
    std::filesystem::remove(root, error);
#endif
}

void RunRendererParticleStripSnapshotSubmitTest() {
    RunRendererSubmitsParticleStripSnapshotsTest();
#if defined(_WIN32)
    RunRendererDrawsParticleStripSnapshotPixelsTest();
#endif
}

void RunRendererParticleVolumetricSnapshotSubmitTest() {
    RunRendererSubmitsParticleStripSnapshotsTest();
#if defined(_WIN32)
    RunRendererDrawsVolumetricParticleSnapshotPixelsTest();
#endif
}

void RunRendererDetachedViewportFinalCompositePixelsTest() {
#if defined(_WIN32)
    RunRendererDrawsDetachedViewportFinalCompositePixelsTest();
#endif
}

void RunEditorUIViewTransformValidationTests() {
    kb::scene::Scene scene;
    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    Renderer renderer;
    renderer.ReleaseScene(scene);
    renderer.ReleaseAllScenes();
    Require(renderer.Initialize(surface,&config), "Editor UI view renderer initialization failed");
    RenderSceneSubmitDesc desc{};
    desc.target.viewport = RenderViewportDesc{.id=RenderViewportId{1U},.extent={64U,64U},.viewportIndex=0U};
    desc.cameraOverride = IdentityCamera();
    desc.editorSceneOverlaysEnabled = true;
    for (const float scale : {0.0F,-1.0F,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        desc.editorUIScale = scale;
        Require(renderer.BeginFrame(), "Invalid UI scale frame did not begin");
        const bool accepted = renderer.SubmitScene(scene,desc);
        renderer.EndFrame();
        Require(!accepted, "Renderer accepted a nonpositive or nonfinite editor UI scale");
    }
    desc.editorUIScale = 2.0F;
    desc.editorUIOffset = {20.0F,-10.0F};
    Require(renderer.BeginFrame(), "Valid UI view frame did not begin");
    Require(renderer.SubmitScene(scene,desc), "Renderer rejected a valid UI view after invalid scales");
    renderer.EndFrame();
    renderer.Shutdown();
    renderer.ReleaseScene(scene);
    renderer.ReleaseAllScenes();
}

void RunRendererVisibilityFeedbackTest() {
    RunRendererPublishesSceneVisibilityFeedbackTest();
}

void RunRendererRuntimeSubmitTests() {
    RunRendererResourceGroupEnsureFallbacksTest();
    RunEditorUIViewTransformValidationTests();
    RunEditorCameraWireframesSubmitInHeadlessNoopTest();
    RunMaterialFrameTimeAdvanceTest();
    RunRuntimeMaterialResolverReturnsTypedFallbacksAndDiagnosticsTest();
    RunRuntimeMaterialResolverEvaluatesMaterialOutputTextureGraphTest();
    RunRuntimeMaterialResolverEvaluatesConstantAndMathGraphTest();
    RunRendererPublishesSceneVisibilityFeedbackTest();
    RunRendererKeepsSceneWhenUIFrameRefusesTest();
    RunRendererKeepsUIWhenTextCannotBePreparedTest();
    RunRendererParticleMeshSnapshotSubmitTest();
    RunRendererParticleStripSnapshotSubmitTest();
    RunRendererParticleVolumetricSnapshotSubmitTest();
    RunRendererDetachedViewportFinalCompositePixelsTest();
    RunRendererReleaseSceneDropsRuntimeResourcesTest();
    RunRendererPrunesUnreferencedResourcesAfterRetentionTest();
    RunRendererReloadsChangedRuntimeMeshAssetTest();
    RunRendererSubmitsRuntimeMeshAssetInHeadlessNoopTest();
    RunRendererUsesResolverDefaultFallbackForMissingMaterialTest();
    RunRuntimeGraphMaterialRenderModeReportingTest();
    RunRuntimeMaterialInstanceDynamicParameterOverrideTest();
    RunRuntimeMaterialResolverResolvesInstanceParameterOverridesTest();
    RunRendererAppliesMaterialInstanceParameterOverrideTest();
    RunPostProcessProfileAssetSaveLoadRoundTripTest();
    RunRendererAppliesScenePostProcessProfileTest();
    RunRuntimeMaterialInstanceStaticBaseOverrideChainTest();
    RunRendererBindsGraphMaterialGpuProgramTest();
#if defined(KB_TEST_GRAPH_SHADERC_PATH)
    RunRendererPublicGraphShaderCacheRootBindsCookedProgramTest();
    RunRendererSceneRendersMultipleCookedGraphMaterialsTest();
    RunRendererPreservesGraphTextureDimensionsThroughProductionPipelineTest();
#endif
    RunRendererReloadsChangedRuntimeMaterialAssetTest();
    RunRendererEnsuresGraphProgramTextureResourcesTest();
    RunRendererRoutesGraphBlendModeToTransparentPassTest();
    RunGraphBackedMaterialArtifactDependencyReloadInvalidatesOnlyTouchedBindingTest();
    RunCookedGraphBackedMaterialRuntimeDoesNotCompileGraphTest();
    RunInvalidGraphMaterialUsesLastGoodThenRefreshesAfterFixTest();
    RunRendererSubmitsMaterialInstanceAssetInHeadlessNoopTest();
    RunRendererMaterialInstanceInheritsGraphBackedParentParametersTest();
    RunRendererReloadsMaterialInstanceWhenParentMaterialChangesTest();
    RunRendererSubmitsWorkspaceSceneCubeMaterialAfterReopenTest();
    RunRendererSubmitsGltfEmbeddedMaterialInHeadlessNoopTest();
    RunGraphMaterialReportsGpuMaterialGraphModeTest();
    RunRendererSubmitsDeferredGBufferAndLightingPassesInHeadlessNoopTest();
    RunRendererSubmitsDockedAndDetachedViewportsInSameFrameTest();
    RunSecondaryFrameModesProduceRuntimeTargetsTest();
}

} // namespace kb::render::tests
