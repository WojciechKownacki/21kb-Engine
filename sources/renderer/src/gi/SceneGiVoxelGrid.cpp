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

// Triangles are sampled on a lattice finer than half a cell, which marks every cell a triangle passes through.
constexpr int kMaskCells = 32;
constexpr int kMaxSamplesPerEdge = 256;

} // namespace

SceneGiVoxelGrid::MeshMask SceneGiVoxelGrid::BuildMask(const RenderMeshProxyGeometry& geometry) {
    MeshMask mask;
    const std::size_t vertexCount = geometry.positions.size() / 3U;
    if (vertexCount == 0U || geometry.indices.size() < 3U) {
        return mask;
    }
    std::array<float, 3> lo{ geometry.positions[0], geometry.positions[1], geometry.positions[2] };
    std::array<float, 3> hi = lo;
    for (std::size_t vertex = 1U; vertex < vertexCount; ++vertex) {
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            lo[axis] = std::min(lo[axis], geometry.positions[vertex * 3U + axis]);
            hi[axis] = std::max(hi[axis], geometry.positions[vertex * 3U + axis]);
        }
    }
    float largest = 0.0F;
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        largest = std::max(largest, hi[axis] - lo[axis]);
    }
    if (!(largest > 0.0F) || !std::isfinite(largest)) {
        return mask;
    }
    mask.cell = largest / static_cast<float>(kMaskCells);
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        mask.minCorner[axis] = lo[axis];
        mask.center[axis] = 0.5F * (lo[axis] + hi[axis]);
        mask.halfExtents[axis] = 0.5F * (hi[axis] - lo[axis]);
        mask.dims[axis] = std::clamp(static_cast<int>(std::ceil((hi[axis] - lo[axis]) / mask.cell)), 1, kMaskCells);
    }
    const std::size_t nx = static_cast<std::size_t>(mask.dims[0]);
    const std::size_t ny = static_cast<std::size_t>(mask.dims[1]);
    const std::size_t nz = static_cast<std::size_t>(mask.dims[2]);
    std::vector<std::uint8_t> cells(nx * ny * nz, 0U);
    const auto mark = [&](float x, float y, float z) {
        const int cx = std::clamp(static_cast<int>((x - lo[0]) / mask.cell), 0, mask.dims[0] - 1);
        const int cy = std::clamp(static_cast<int>((y - lo[1]) / mask.cell), 0, mask.dims[1] - 1);
        const int cz = std::clamp(static_cast<int>((z - lo[2]) / mask.cell), 0, mask.dims[2] - 1);
        cells[(static_cast<std::size_t>(cz) * ny + static_cast<std::size_t>(cy)) * nx + static_cast<std::size_t>(cx)] = 1U;
    };
    const float step = 0.5F * mask.cell;
    for (std::size_t triangle = 0U; triangle + 2U < geometry.indices.size(); triangle += 3U) {
        const float* a = &geometry.positions[static_cast<std::size_t>(geometry.indices[triangle]) * 3U];
        const float* b = &geometry.positions[static_cast<std::size_t>(geometry.indices[triangle + 1U]) * 3U];
        const float* c = &geometry.positions[static_cast<std::size_t>(geometry.indices[triangle + 2U]) * 3U];
        float longest = 0.0F;
        for (const auto& edge : { std::array<const float*, 2>{ a, b }, std::array<const float*, 2>{ b, c }, std::array<const float*, 2>{ c, a } }) {
            const float dx = edge[0][0] - edge[1][0];
            const float dy = edge[0][1] - edge[1][1];
            const float dz = edge[0][2] - edge[1][2];
            longest = std::max(longest, std::sqrt(dx * dx + dy * dy + dz * dz));
        }
        const int n = std::clamp(static_cast<int>(std::ceil(longest / step)), 1, kMaxSamplesPerEdge);
        for (int i = 0; i <= n; ++i) {
            for (int j = 0; j <= n - i; ++j) {
                const float u = static_cast<float>(i) / static_cast<float>(n);
                const float v = static_cast<float>(j) / static_cast<float>(n);
                const float w = 1.0F - u - v;
                mark(a[0] * w + b[0] * u + c[0] * v, a[1] * w + b[1] * u + c[1] * v, a[2] * w + b[2] * u + c[2] * v);
            }
        }
    }
    // Summed-volume table: prefix[(z * (ny + 1) + y) * (nx + 1) + x] = occupied cells with index below (x, y, z).
    mask.prefix.assign((nx + 1U) * (ny + 1U) * (nz + 1U), 0U);
    const auto at = [&](std::size_t x, std::size_t y, std::size_t z) -> std::uint32_t& {
        return mask.prefix[(z * (ny + 1U) + y) * (nx + 1U) + x];
    };
    for (std::size_t z = 1U; z <= nz; ++z) {
        for (std::size_t y = 1U; y <= ny; ++y) {
            for (std::size_t x = 1U; x <= nx; ++x) {
                at(x, y, z) = cells[((z - 1U) * ny + (y - 1U)) * nx + (x - 1U)] + at(x - 1U, y, z) + at(x, y - 1U, z) + at(x, y, z - 1U) -
                    at(x - 1U, y - 1U, z) - at(x - 1U, y, z - 1U) - at(x, y - 1U, z - 1U) + at(x - 1U, y - 1U, z - 1U);
            }
        }
    }
    mask.usable = true;
    return mask;
}

bool SceneGiVoxelGrid::MaskTouches(const MeshMask& mask, const std::array<float, 3>& lo, const std::array<float, 3>& hi) noexcept {
    std::array<int, 3> first{};
    std::array<int, 3> last{};
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        const int low = static_cast<int>(std::floor((lo[axis] - mask.minCorner[axis]) / mask.cell));
        const int high = static_cast<int>(std::floor((hi[axis] - mask.minCorner[axis]) / mask.cell));
        if (high < 0 || low >= mask.dims[axis]) {
            return false;
        }
        first[axis] = std::max(low, 0);
        last[axis] = std::min(high, mask.dims[axis] - 1) + 1; // exclusive
    }
    const std::size_t nx = static_cast<std::size_t>(mask.dims[0]) + 1U;
    const std::size_t ny = static_cast<std::size_t>(mask.dims[1]) + 1U;
    const auto at = [&](int x, int y, int z) {
        return static_cast<std::int64_t>(mask.prefix[(static_cast<std::size_t>(z) * ny + static_cast<std::size_t>(y)) * nx + static_cast<std::size_t>(x)]);
    };
    const std::int64_t sum = at(last[0], last[1], last[2]) - at(first[0], last[1], last[2]) - at(last[0], first[1], last[2]) -
        at(last[0], last[1], first[2]) + at(first[0], first[1], last[2]) + at(first[0], last[1], first[2]) +
        at(last[0], first[1], first[2]) - at(first[0], first[1], first[2]);
    return sum > 0;
}

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
    masks_.clear();
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
        const RenderMeshResource* signatureMesh = resources.FindMesh(resourceMap.ResolveMesh(desc.meshAssetId));
        Mix(signature, signatureMesh != nullptr ? signatureMesh->version : std::uint64_t{ 0U });
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
    occupiedVoxels_ = 0U;
    const auto dimension = static_cast<int>(kDimension);
    for (const auto& [entityId, proxy] : renderScene.MeshProxies()) {
        static_cast<void>(entityId);
        const MeshRenderProxyDesc& desc = proxy.desc;
        Surface surface;
        const RenderMeshResource* mesh = resources.FindMesh(resourceMap.ResolveMesh(desc.meshAssetId));
        const MeshMask* mask = nullptr;
        if (desc.visible && mesh != nullptr && mesh->proxyGeometry != nullptr) {
            MeshMask& cached = masks_[desc.meshAssetId];
            if (cached.version != mesh->version) {
                cached = BuildMask(*mesh->proxyGeometry);
                cached.version = mesh->version;
            }
            mask = cached.usable ? &cached : nullptr;
        }
        if (!desc.visible || mesh == nullptr || (mask == nullptr && !mesh->boundsBox.IsValid()) ||
            !ResolveSurface(desc, resources, resourceMap, surface)) {
            continue;
        }
        const std::array<float, 16>& model = desc.model;
        std::array<float, 16> inverse{};
        bx::mtxInverse(inverse.data(), model.data());
        const RenderBoundsBox box = mask != nullptr ? RenderBoundsBox{ mask->center, mask->halfExtents } : mesh->boundsBox;
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
                    std::array<float, 3> localLow{};
                    std::array<float, 3> localHigh{};
                    for (int axis = 0; axis < 3 && inside; ++axis) {
                        // Column-major inverse: row `axis` is elements axis, 4 + axis, 8 + axis.
                        const float local = inverse[axis] * center[0] + inverse[4 + axis] * center[1] +
                            inverse[8 + axis] * center[2] + inverse[12 + axis];
                        inside = std::abs(local - box.center[axis]) <= box.halfExtents[axis] + slack[axis];
                        localLow[axis] = local - slack[axis];
                        localHigh[axis] = local + slack[axis];
                    }
                    // A voxel of a meshed object is occupied only when some triangle passes through it.
                    if (!inside || (mask != nullptr && !MaskTouches(*mask, localLow, localHigh))) {
                        continue;
                    }
                    const std::size_t index = (static_cast<std::size_t>(z) * kDimension + static_cast<std::size_t>(y)) * kDimension +
                        static_cast<std::size_t>(x);
                    occupiedVoxels_ += albedoVoxels_[index * 4U + 3U] == 0U ? 1U : 0U;
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
