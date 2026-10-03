#include "kb/render/gi/SceneGiVoxelGrid.hpp"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace kb::render {
namespace {

constexpr std::uint32_t kVoxelCount = SceneGiVoxelGrid::kDimension * SceneGiVoxelGrid::kDimension * SceneGiVoxelGrid::kDimension;

void Mix(std::uint64_t& hash, const void* data, std::size_t bytes) noexcept {
    const auto* values = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0U; index < bytes; ++index) {
        hash = (hash ^ values[index]) * 1099511628211ULL;
    }
}

template <typename T>
void Mix(std::uint64_t& hash, const T& value) noexcept {
    Mix(hash, &value, sizeof(T));
}

struct Surface {
    std::array<float, 3> albedo{ 1.0F, 1.0F, 1.0F };
    std::array<float, 3> emissive{};
};

// Opaque meshes only: a translucent or masked instance would block bounce light it should let through.
[[nodiscard]] bool ResolveSurface(const MeshRenderProxyDesc& desc, const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap, Surface& surface) noexcept {
    const RenderMaterialResource* material = resources.FindMaterial(resourceMap.ResolveMaterial(desc.materialAssetId));
    if (material == nullptr) {
        surface.albedo = { desc.color[0], desc.color[1], desc.color[2] };
        return true;
    }
    if (material->alphaMode != RenderMaterialAlphaMode::Opaque) {
        return false;
    }
    surface.albedo = { material->baseColor[0], material->baseColor[1], material->baseColor[2] };
    const float strength = material->emissiveStrength;
    surface.emissive = { material->emissiveColor[0] * strength, material->emissiveColor[1] * strength,
        material->emissiveColor[2] * strength };
    return true;
}

[[nodiscard]] std::uint8_t ToByte(float value) noexcept {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0F, 1.0F) * 255.0F + 0.5F);
}

} // namespace

SceneGiVoxelGrid::~SceneGiVoxelGrid() {
    Shutdown();
}

bool SceneGiVoxelGrid::Initialize() {
    if (IsValid()) {
        return true;
    }
    constexpr std::uint16_t dimension = kDimension;
    albedo_ = bgfx::createTexture3D(dimension, dimension, dimension, false, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
    emissive_ = bgfx::createTexture3D(dimension, dimension, dimension, false, bgfx::TextureFormat::RGBA16F,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
    if (!IsValid()) {
        Shutdown();
        return false;
    }
    return true;
}

void SceneGiVoxelGrid::Shutdown() noexcept {
    for (bgfx::TextureHandle* handle : { &albedo_, &emissive_ }) {
        if (bgfx::isValid(*handle)) {
            bgfx::destroy(*handle);
        }
        *handle = BGFX_INVALID_HANDLE;
    }
    signature_ = 0U;
    albedoVoxels_.clear();
    emissiveVoxels_.clear();
}

void SceneGiVoxelGrid::Update(const RenderScene& renderScene, const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap, const std::array<float, 3>& focus, float voxelSize) {
    if (!IsValid()) {
        return;
    }
    voxelSize = std::max(voxelSize, 0.05F);
    const float half = static_cast<float>(kDimension / 2U);
    const std::array<float, 3> origin{
        std::floor(focus[0] / voxelSize - half) * voxelSize,
        std::floor(focus[1] / voxelSize - half) * voxelSize,
        std::floor(focus[2] / voxelSize - half) * voxelSize };

    std::uint64_t signature = 14695981039346656037ULL;
    Mix(signature, voxelSize);
    Mix(signature, origin);
    for (const auto& [entityId, proxy] : renderScene.MeshProxies()) {
        static_cast<void>(entityId);
        const MeshRenderProxyDesc& desc = proxy.desc;
        Surface surface;
        if (!desc.visible || !ResolveSurface(desc, resources, resourceMap, surface)) {
            continue;
        }
        Mix(signature, desc.model);
        Mix(signature, desc.meshAssetId);
        Mix(signature, surface.albedo);
        Mix(signature, surface.emissive);
    }
    if (signature == signature_) {
        return;
    }
    signature_ = signature;
    origin_ = { origin[0], origin[1], origin[2], voxelSize };

    albedoVoxels_.assign(static_cast<std::size_t>(kVoxelCount) * 4U, 0U);
    emissiveVoxels_.assign(static_cast<std::size_t>(kVoxelCount) * 4U, 0U);
    const auto dimension = static_cast<int>(kDimension);
    for (const auto& [entityId, proxy] : renderScene.MeshProxies()) {
        static_cast<void>(entityId);
        const MeshRenderProxyDesc& desc = proxy.desc;
        Surface surface;
        const RenderMeshResource* mesh = resources.FindMesh(resourceMap.ResolveMesh(desc.meshAssetId));
        if (!desc.visible || mesh == nullptr || !mesh->boundsBox.IsValid() || !ResolveSurface(desc, resources, resourceMap, surface)) {
            continue;
        }
        const std::array<float, 16>& model = desc.model;
        std::array<float, 16> inverse{};
        bx::mtxInverse(inverse.data(), model.data());
        const RenderBoundsBox& box = mesh->boundsBox;
        // World-space extent of the box, to bound the voxels to visit.
        std::array<float, 3> worldCenter{};
        std::array<float, 3> worldHalf{};
        for (int axis = 0; axis < 3; ++axis) {
            worldCenter[axis] = model[12 + axis] + model[axis] * box.center[0] + model[4 + axis] * box.center[1] + model[8 + axis] * box.center[2];
            worldHalf[axis] = std::abs(model[axis]) * box.halfExtents[0] + std::abs(model[4 + axis]) * box.halfExtents[1] +
                std::abs(model[8 + axis]) * box.halfExtents[2];
        }
        // Local-space slack for half a voxel diagonal along each local axis.
        std::array<float, 3> slack{};
        for (int axis = 0; axis < 3; ++axis) {
            const float rowLength = std::sqrt(inverse[axis] * inverse[axis] + inverse[4 + axis] * inverse[4 + axis] +
                inverse[8 + axis] * inverse[8 + axis]);
            slack[axis] = 0.5F * 0.87F * voxelSize * rowLength;
        }
        std::array<int, 3> lo{};
        std::array<int, 3> hi{};
        for (int axis = 0; axis < 3; ++axis) {
            lo[axis] = std::max(0, static_cast<int>(std::floor((worldCenter[axis] - worldHalf[axis] - origin[axis]) / voxelSize)));
            hi[axis] = std::min(dimension - 1, static_cast<int>(std::floor((worldCenter[axis] + worldHalf[axis] - origin[axis]) / voxelSize)));
        }
        for (int z = lo[2]; z <= hi[2]; ++z) {
            for (int y = lo[1]; y <= hi[1]; ++y) {
                for (int x = lo[0]; x <= hi[0]; ++x) {
                    const std::array<float, 3> center{
                        origin[0] + (static_cast<float>(x) + 0.5F) * voxelSize,
                        origin[1] + (static_cast<float>(y) + 0.5F) * voxelSize,
                        origin[2] + (static_cast<float>(z) + 0.5F) * voxelSize };
                    bool inside = true;
                    for (int axis = 0; axis < 3 && inside; ++axis) {
                        // Column-major inverse: row `axis` is elements axis, 4 + axis, 8 + axis.
                        const float local = inverse[axis] * center[0] + inverse[4 + axis] * center[1] +
                            inverse[8 + axis] * center[2] + inverse[12 + axis];
                        inside = std::abs(local - box.center[axis]) <= box.halfExtents[axis] + slack[axis];
                    }
                    if (!inside) {
                        continue;
                    }
                    const std::size_t index = (static_cast<std::size_t>(z) * kDimension + static_cast<std::size_t>(y)) * kDimension +
                        static_cast<std::size_t>(x);
                    albedoVoxels_[index * 4U + 0U] = ToByte(surface.albedo[0]);
                    albedoVoxels_[index * 4U + 1U] = ToByte(surface.albedo[1]);
                    albedoVoxels_[index * 4U + 2U] = ToByte(surface.albedo[2]);
                    albedoVoxels_[index * 4U + 3U] = 255U;
                    for (int channel = 0; channel < 3; ++channel) {
                        emissiveVoxels_[index * 4U + static_cast<std::size_t>(channel)] = bx::halfFromFloat(surface.emissive[channel]);
                    }
                }
            }
        }
    }
    const auto upload = [](bgfx::TextureHandle texture, const void* data, std::uint32_t bytes) {
        const bgfx::Memory* memory = bgfx::alloc(bytes);
        if (memory == nullptr || memory->data == nullptr) {
            return;
        }
        std::memcpy(memory->data, data, bytes);
        bgfx::updateTexture3D(texture, 0U, 0U, 0U, 0U, kDimension, kDimension, kDimension, memory);
    };
    upload(albedo_, albedoVoxels_.data(), static_cast<std::uint32_t>(albedoVoxels_.size()));
    upload(emissive_, emissiveVoxels_.data(), static_cast<std::uint32_t>(emissiveVoxels_.size() * sizeof(std::uint16_t)));
}

} // namespace kb::render
