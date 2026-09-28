#include "engine/assets/CollisionMeshAsset.hpp"
#include "engine/assets/TerrainAsset.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <memory>

namespace kb::assets {
namespace {

constexpr std::array<std::uint8_t, 8> Magic{'K', 'B', 'C', 'O', 'L', 'L', 0, 0};
constexpr std::uint64_t HeaderBytes = 20;
constexpr std::uint64_t MaximumBytes = 256ULL * 1024 * 1024;

bool Fail(std::string& error, const char* message) {
    error = message;
    return false;
}

std::uint32_t ReadWord(std::span<const std::uint8_t> bytes, std::size_t& cursor) {
    const std::uint32_t value = std::uint32_t{bytes[cursor]} |
        (std::uint32_t{bytes[cursor + 1]} << 8) |
        (std::uint32_t{bytes[cursor + 2]} << 16) |
        (std::uint32_t{bytes[cursor + 3]} << 24);
    cursor += 4;
    return value;
}

void WriteWord(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

} // namespace

bool ValidateCollisionMesh(const CollisionMeshAsset& mesh, std::string& error) {
    error.clear();
    if (mesh.positions.size() < 3 || mesh.indices.empty() || mesh.indices.size() % 3 != 0 ||
        mesh.positions.size() > std::numeric_limits<std::uint32_t>::max() ||
        mesh.indices.size() > std::numeric_limits<std::uint32_t>::max() ||
        HeaderBytes + std::uint64_t{mesh.positions.size()} * 12 + std::uint64_t{mesh.indices.size()} * 4 > MaximumBytes) {
        return Fail(error, "Collision mesh has invalid geometry counts or exceeds 256 MiB");
    }
    for (const auto& p : mesh.positions) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
            return Fail(error, "Collision mesh contains a non-finite position");
        }
    }
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        const auto a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        if (a >= mesh.positions.size() || b >= mesh.positions.size() || c >= mesh.positions.size()) {
            return Fail(error, "Collision mesh index is outside its vertex buffer");
        }
        const auto& p = mesh.positions[a];
        const auto& q = mesh.positions[b];
        const auto& r = mesh.positions[c];
        const double ux = double(q.x) - p.x, uy = double(q.y) - p.y, uz = double(q.z) - p.z;
        const double vx = double(r.x) - p.x, vy = double(r.y) - p.y, vz = double(r.z) - p.z;
        const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        if (nx * nx + ny * ny + nz * nz == 0.0) {
            return Fail(error, "Collision mesh contains a degenerate triangle");
        }
        if (nx * nx + ny * ny + nz * nz > std::numeric_limits<float>::max()) {
            return Fail(error, "Collision triangle exceeds the supported numeric range");
        }
    }
    return true;
}

std::optional<CollisionMeshAsset> ReadCollisionMesh(std::span<const std::uint8_t> bytes, std::string& error) {
    error.clear();
    if (bytes.size() < HeaderBytes || bytes.size() > MaximumBytes ||
        !std::equal(Magic.begin(), Magic.end(), bytes.begin())) {
        Fail(error, "Collision mesh header or size is invalid");
        return std::nullopt;
    }
    std::size_t cursor = Magic.size();
    const auto version = ReadWord(bytes, cursor);
    const auto vertices = ReadWord(bytes, cursor);
    const auto indices = ReadWord(bytes, cursor);
    if (version != 1 || vertices < 3 || indices == 0 || indices % 3 != 0 ||
        HeaderBytes + std::uint64_t{vertices} * 12 + std::uint64_t{indices} * 4 != bytes.size()) {
        Fail(error, "Collision mesh version or payload size is invalid");
        return std::nullopt;
    }
    CollisionMeshAsset mesh;
    mesh.positions.resize(vertices);
    mesh.indices.resize(indices);
    for (auto& p : mesh.positions) {
        p.x = std::bit_cast<float>(ReadWord(bytes, cursor));
        p.y = std::bit_cast<float>(ReadWord(bytes, cursor));
        p.z = std::bit_cast<float>(ReadWord(bytes, cursor));
    }
    for (auto& index : mesh.indices) index = ReadWord(bytes, cursor);
    if (!ValidateCollisionMesh(mesh, error)) return std::nullopt;
    return mesh;
}

bool WriteCollisionMesh(const std::filesystem::path& path, const CollisionMeshAsset& mesh, std::string& error) {
    if (!ValidateCollisionMesh(mesh, error)) return false;
    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(HeaderBytes + mesh.positions.size() * 12 + mesh.indices.size() * 4));
    bytes.insert(bytes.end(), Magic.begin(), Magic.end());
    WriteWord(bytes, 1);
    WriteWord(bytes, static_cast<std::uint32_t>(mesh.positions.size()));
    WriteWord(bytes, static_cast<std::uint32_t>(mesh.indices.size()));
    for (const auto& p : mesh.positions) {
        WriteWord(bytes, std::bit_cast<std::uint32_t>(p.x));
        WriteWord(bytes, std::bit_cast<std::uint32_t>(p.y));
        WriteWord(bytes, std::bit_cast<std::uint32_t>(p.z));
    }
    for (const auto index : mesh.indices) WriteWord(bytes, index);
    return scene::SceneAssetBinaryIO::WriteBytesAtomically(path, bytes)
        ? true : Fail(error, "Collision mesh file could not be replaced");
}

std::optional<CollisionMeshAsset> BuildTerrainCollisionMesh(const TerrainAsset& terrain, std::string& error) {
    error.clear();
    if (!IsTerrainAssetValid(terrain, &error)) return std::nullopt;
    CollisionMeshAsset mesh;
    mesh.positions.reserve(terrain.heights.size());
    mesh.indices.reserve(terrain.holes.size() * 6);
    for (std::uint32_t z = 0; z < terrain.height; ++z) {
        for (std::uint32_t x = 0; x < terrain.width; ++x) {
            mesh.positions.push_back({
                static_cast<float>(x) / static_cast<float>(terrain.width - 1) * terrain.worldSizeX - terrain.worldSizeX * 0.5F,
                terrain.heights[std::size_t{z} * terrain.width + x],
                static_cast<float>(z) / static_cast<float>(terrain.height - 1) * terrain.worldSizeZ - terrain.worldSizeZ * 0.5F,
            });
            if (x + 1 == terrain.width || z + 1 == terrain.height ||
                terrain.holes[std::size_t{z} * (terrain.width - 1) + x] != 0) continue;
            const auto a = z * terrain.width + x, b = a + 1, c = a + terrain.width, d = c + 1;
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }
    if (!ValidateCollisionMesh(mesh, error)) return std::nullopt;
    return mesh;
}

std::string_view CollisionMeshAssetLoader::Type() const noexcept { return kCollisionMeshAssetType; }
std::type_index CollisionMeshAssetLoader::PayloadType() const noexcept { return typeid(CollisionMeshAsset); }
std::vector<std::string> CollisionMeshAssetLoader::Extensions() const { return {std::string{kCollisionMeshAssetExtension}}; }

AssetLoadResult CollisionMeshAssetLoader::Load(const AssetLoadRequest& request) {
    if (request.SourceExtension() != kCollisionMeshAssetExtension) {
        return {{}, "Collision mesh asset has an unexpected extension"};
    }
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!request.ReadSourceBytes(bytes, error)) return {{}, std::move(error)};
    auto mesh = ReadCollisionMesh(bytes, error);
    return mesh ? AssetLoadResult{std::make_shared<CollisionMeshAsset>(std::move(*mesh)), {}}
                : AssetLoadResult{{}, std::move(error)};
}

} // namespace kb::assets
