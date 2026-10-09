#include "engine/navigation/NavMeshAsset.hpp"

#include "assets/bake/AssetPackCompression.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>

namespace kb::navigation {
namespace {

constexpr std::array<std::uint8_t, 8U> kMagic{ '2', '1', 'K', 'B', 'N', 'A', 'V', 'M' };
constexpr int kCompressionLevel = 9;
// Neighbour connection bits of a layer column: 0 = -x, 1 = +z, 2 = +x, 3 = -z.
constexpr std::uint8_t kConnectsMinusX = 1U << 0U;
constexpr std::uint8_t kConnectsPlusZ = 1U << 1U;
constexpr std::uint8_t kConnectsPlusX = 1U << 2U;
constexpr std::uint8_t kConnectsMinusZ = 1U << 3U;
// Area codes a layer may hold: not walkable, or a navigation area id plus one.
constexpr std::uint8_t kMaxLayerArea = 32U;

class Writer {
public:
    void U8(std::uint8_t value) { bytes_.push_back(value); }
    void U16(std::uint16_t value) {
        for (unsigned shift = 0U; shift < 16U; shift += 8U) bytes_.push_back(static_cast<std::uint8_t>(value >> shift));
    }
    void U32(std::uint32_t value) {
        for (unsigned shift = 0U; shift < 32U; shift += 8U) bytes_.push_back(static_cast<std::uint8_t>(value >> shift));
    }
    void U64(std::uint64_t value) {
        for (unsigned shift = 0U; shift < 64U; shift += 8U) bytes_.push_back(static_cast<std::uint8_t>(value >> shift));
    }
    void F32(float value) { U32(std::bit_cast<std::uint32_t>(value)); }
    void Raw(std::span<const std::uint8_t> value) { bytes_.insert(bytes_.end(), value.begin(), value.end()); }
    void String(const std::string& value) {
        U32(static_cast<std::uint32_t>(value.size()));
        Raw({ reinterpret_cast<const std::uint8_t*>(value.data()), value.size() });
    }
    [[nodiscard]] std::vector<std::uint8_t> Take() { return std::move(bytes_); }

private:
    std::vector<std::uint8_t> bytes_;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) noexcept : bytes_(bytes) {}
    [[nodiscard]] std::size_t Remaining() const noexcept { return bytes_.size() - offset_; }
    [[nodiscard]] bool U8(std::uint8_t& value) noexcept {
        if (Remaining() < 1U) return false;
        value = bytes_[offset_++];
        return true;
    }
    [[nodiscard]] bool U16(std::uint16_t& value) noexcept {
        if (Remaining() < 2U) return false;
        value = static_cast<std::uint16_t>(bytes_[offset_] | (bytes_[offset_ + 1U] << 8U));
        offset_ += 2U;
        return true;
    }
    [[nodiscard]] bool U32(std::uint32_t& value) noexcept {
        if (Remaining() < 4U) return false;
        value = 0U;
        for (unsigned index = 0U; index < 4U; ++index) value |= static_cast<std::uint32_t>(bytes_[offset_ + index]) << (index * 8U);
        offset_ += 4U;
        return true;
    }
    [[nodiscard]] bool U64(std::uint64_t& value) noexcept {
        if (Remaining() < 8U) return false;
        value = 0U;
        for (unsigned index = 0U; index < 8U; ++index) value |= static_cast<std::uint64_t>(bytes_[offset_ + index]) << (index * 8U);
        offset_ += 8U;
        return true;
    }
    [[nodiscard]] bool F32(float& value) noexcept {
        std::uint32_t bits = 0U;
        if (!U32(bits)) return false;
        value = std::bit_cast<float>(bits);
        return std::isfinite(value);
    }
    [[nodiscard]] bool Bool(bool& value) noexcept {
        std::uint8_t byte = 0U;
        if (!U8(byte) || byte > 1U) return false;
        value = byte != 0U;
        return true;
    }
    [[nodiscard]] bool String(std::string& value, std::size_t maxBytes) {
        std::uint32_t size = 0U;
        if (!U32(size) || size > maxBytes || size > Remaining()) return false;
        value.assign(reinterpret_cast<const char*>(bytes_.data() + offset_), size);
        offset_ += size;
        return true;
    }
    [[nodiscard]] bool Span(std::size_t size, std::span<const std::uint8_t>& value) noexcept {
        if (size > Remaining()) return false;
        value = bytes_.subspan(offset_, size);
        offset_ += size;
        return true;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0U;
};

[[nodiscard]] NavMeshAssetReadResult Fail(std::string error) {
    return { .succeeded = false, .asset = {}, .error = std::move(error) };
}

[[nodiscard]] bool TileOrder(const NavTile& left, const NavTile& right) noexcept {
    if (left.profile != right.profile) return left.profile < right.profile;
    return left.coord < right.coord;
}

[[nodiscard]] std::string ValidateLayer(const NavTileLayer& layer, std::uint32_t tileCells) {
    const std::size_t cells = static_cast<std::size_t>(tileCells) * tileCells;
    if (layer.heights.size() != cells || layer.areas.size() != cells || layer.connections.size() != cells) {
        return "a navigation layer's grids do not match its tile size";
    }
    if (!std::isfinite(layer.minY) || !std::isfinite(layer.maxY) || layer.minY > layer.maxY) {
        return "a navigation layer has an invalid height range";
    }
    if (layer.heightMin > layer.heightMax || layer.minX > layer.maxX || layer.minZ > layer.maxZ || layer.maxX >= tileCells ||
        layer.maxZ >= tileCells) {
        return "a navigation layer has an invalid usable region";
    }
    for (std::uint32_t z = 0U; z < tileCells; ++z) {
        for (std::uint32_t x = 0U; x < tileCells; ++x) {
            const std::size_t index = static_cast<std::size_t>(z) * tileCells + x;
            if (layer.areas[index] > kMaxLayerArea) {
                return "a navigation layer holds an unknown area";
            }
            // Every connection must lead to a column of the same grid: the polygon builder walks
            // them without bounds checks.
            const std::uint8_t connections = layer.connections[index];
            if (((connections & kConnectsMinusX) != 0U && x == 0U) || ((connections & kConnectsPlusX) != 0U && x + 1U == tileCells) ||
                ((connections & kConnectsMinusZ) != 0U && z == 0U) || ((connections & kConnectsPlusZ) != 0U && z + 1U == tileCells)) {
                return "a navigation layer connects a column to one outside its tile";
            }
        }
    }
    return {};
}

} // namespace

std::string NavMeshAssetIO::Validate(const NavMeshAsset& asset) {
    if (std::string invalid = ValidateNavMeshBuildSettings(asset.settings); !invalid.empty()) {
        return invalid;
    }
    if (asset.tiles.size() > NavMeshAsset::MaxTiles) {
        return "a navigation mesh holds more than 1048576 tiles";
    }
    for (std::size_t index = 0U; index < asset.tiles.size(); ++index) {
        const NavTile& tile = asset.tiles[index];
        if (tile.profile >= asset.settings.profiles.size()) {
            return "a navigation tile names an agent profile the mesh does not have";
        }
        if (index != 0U && !TileOrder(asset.tiles[index - 1U], tile)) {
            return "navigation tiles must be sorted by profile and coordinate, each once";
        }
        if (tile.layers.empty() || tile.layers.size() > NavMeshAsset::MaxLayersPerTile) {
            return "a navigation tile must hold 1 to 32 layers";
        }
        constexpr std::int64_t kLimit = std::int64_t{ 1 } << 52U;
        if (tile.coord.x <= -kLimit || tile.coord.x >= kLimit || tile.coord.z <= -kLimit || tile.coord.z >= kLimit) {
            return "a navigation tile lies outside the addressable world";
        }
        for (const NavTileLayer& layer : tile.layers) {
            if (std::string invalid = ValidateLayer(layer, asset.settings.tileCells); !invalid.empty()) {
                return invalid;
            }
        }
    }
    return {};
}

std::vector<std::uint8_t> NavMeshAssetIO::Serialize(const NavMeshAsset& asset) {
    if (!Validate(asset).empty()) {
        return {};
    }
    Writer out;
    out.Raw(kMagic);
    out.U32(NavMeshAsset::CurrentVersion);
    const NavMeshBuildSettings& settings = asset.settings;
    out.F32(settings.cellSize);
    out.F32(settings.cellHeight);
    out.U32(settings.tileCells);
    out.F32(settings.edgeMaxError);
    out.U8(settings.renderMeshes ? 1U : 0U);
    out.U8(settings.colliders ? 1U : 0U);
    out.U32(static_cast<std::uint32_t>(settings.profiles.size()));
    for (const NavAgentProfile& profile : settings.profiles) {
        out.String(profile.name);
        out.F32(profile.radius);
        out.F32(profile.height);
        out.F32(profile.maxClimb);
        out.F32(profile.maxSlopeDegrees);
    }
    out.U32(static_cast<std::uint32_t>(asset.tiles.size()));
    std::vector<std::uint8_t> grids;
    std::vector<std::uint8_t> compressed;
    for (const NavTile& tile : asset.tiles) {
        out.U32(tile.profile);
        out.U64(static_cast<std::uint64_t>(tile.coord.x));
        out.U64(static_cast<std::uint64_t>(tile.coord.z));
        out.U32(static_cast<std::uint32_t>(tile.layers.size()));
        for (const NavTileLayer& layer : tile.layers) {
            out.F32(layer.minY);
            out.F32(layer.maxY);
            out.U16(layer.heightMin);
            out.U16(layer.heightMax);
            out.U8(layer.minX);
            out.U8(layer.maxX);
            out.U8(layer.minZ);
            out.U8(layer.maxZ);
            grids.clear();
            grids.insert(grids.end(), layer.heights.begin(), layer.heights.end());
            grids.insert(grids.end(), layer.areas.begin(), layer.areas.end());
            grids.insert(grids.end(), layer.connections.begin(), layer.connections.end());
            if (!kb::assets::bake::CompressAssetPackBlock(grids, kCompressionLevel, compressed)) {
                return {};
            }
            out.U32(static_cast<std::uint32_t>(compressed.size()));
            out.Raw(compressed);
        }
    }
    return out.Take();
}

NavMeshAssetReadResult NavMeshAssetIO::Parse(std::span<const std::uint8_t> bytes) {
    Reader in{ bytes };
    std::span<const std::uint8_t> magic;
    if (!in.Span(kMagic.size(), magic) || !std::equal(magic.begin(), magic.end(), kMagic.begin())) {
        return Fail("not a navigation mesh file");
    }
    std::uint32_t version = 0U;
    if (!in.U32(version) || version != NavMeshAsset::CurrentVersion) {
        return Fail("unsupported navigation mesh version");
    }
    NavMeshAsset asset;
    NavMeshBuildSettings& settings = asset.settings;
    std::uint32_t profileCount = 0U;
    if (!in.F32(settings.cellSize) || !in.F32(settings.cellHeight) || !in.U32(settings.tileCells) || !in.F32(settings.edgeMaxError) ||
        !in.Bool(settings.renderMeshes) || !in.Bool(settings.colliders) || !in.U32(profileCount) || profileCount == 0U ||
        profileCount > NavMeshBuildSettings::MaxProfiles) {
        return Fail("navigation mesh settings are truncated or invalid");
    }
    settings.profiles.assign(profileCount, NavAgentProfile{});
    for (NavAgentProfile& profile : settings.profiles) {
        if (!in.String(profile.name, NavAgentProfile::MaxNameBytes) || !in.F32(profile.radius) || !in.F32(profile.height) ||
            !in.F32(profile.maxClimb) || !in.F32(profile.maxSlopeDegrees)) {
            return Fail("navigation agent profile is truncated or invalid");
        }
    }
    if (std::string invalid = ValidateNavMeshBuildSettings(settings); !invalid.empty()) {
        return Fail(std::move(invalid));
    }
    std::uint32_t tileCount = 0U;
    if (!in.U32(tileCount) || tileCount > NavMeshAsset::MaxTiles) {
        return Fail("navigation tile count is truncated or too large");
    }
    // Every tile needs at least its own fixed fields: refuse counts the file cannot hold before
    // reserving anything for them.
    constexpr std::size_t kTileBytes = 4U + 8U + 8U + 4U;
    constexpr std::size_t kLayerBytes = 4U + 4U + 2U + 2U + 4U + 4U;
    if (static_cast<std::size_t>(tileCount) * kTileBytes > in.Remaining()) {
        return Fail("navigation tile count exceeds the file");
    }
    const std::size_t cells = static_cast<std::size_t>(settings.tileCells) * settings.tileCells;
    asset.tiles.reserve(tileCount);
    std::vector<std::uint8_t> grids;
    for (std::uint32_t index = 0U; index < tileCount; ++index) {
        NavTile tile;
        std::uint64_t x = 0U;
        std::uint64_t z = 0U;
        std::uint32_t layerCount = 0U;
        if (!in.U32(tile.profile) || !in.U64(x) || !in.U64(z) || !in.U32(layerCount) || layerCount == 0U ||
            layerCount > NavMeshAsset::MaxLayersPerTile || static_cast<std::size_t>(layerCount) * kLayerBytes > in.Remaining()) {
            return Fail("navigation tile " + std::to_string(index) + " is truncated or invalid");
        }
        tile.coord = { static_cast<std::int64_t>(x), static_cast<std::int64_t>(z) };
        tile.layers.resize(layerCount);
        for (NavTileLayer& layer : tile.layers) {
            std::uint32_t storedBytes = 0U;
            std::span<const std::uint8_t> stored;
            if (!in.F32(layer.minY) || !in.F32(layer.maxY) || !in.U16(layer.heightMin) || !in.U16(layer.heightMax) || !in.U8(layer.minX) ||
                !in.U8(layer.maxX) || !in.U8(layer.minZ) || !in.U8(layer.maxZ) || !in.U32(storedBytes) || !in.Span(storedBytes, stored)) {
                return Fail("navigation layer of tile " + std::to_string(index) + " is truncated");
            }
            if (!kb::assets::bake::DecompressAssetPackBlock(stored, cells * 3U, grids)) {
                return Fail("navigation layer of tile " + std::to_string(index) + " does not decode to its grids");
            }
            layer.heights.assign(grids.begin(), grids.begin() + static_cast<std::ptrdiff_t>(cells));
            layer.areas.assign(grids.begin() + static_cast<std::ptrdiff_t>(cells), grids.begin() + static_cast<std::ptrdiff_t>(cells * 2U));
            layer.connections.assign(grids.begin() + static_cast<std::ptrdiff_t>(cells * 2U), grids.end());
        }
        asset.tiles.push_back(std::move(tile));
    }
    if (in.Remaining() != 0U) {
        return Fail("navigation mesh file has trailing bytes");
    }
    if (std::string invalid = Validate(asset); !invalid.empty()) {
        return Fail(std::move(invalid));
    }
    return { .succeeded = true, .asset = std::move(asset), .error = {} };
}

NavMeshAssetReadResult NavMeshAssetIO::Read(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    if (!input) {
        return Fail("could not open " + path.generic_string());
    }
    const std::vector<std::uint8_t> bytes{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    if (input.bad()) {
        return Fail("could not read " + path.generic_string());
    }
    NavMeshAssetReadResult result = Parse(bytes);
    if (!result.succeeded) {
        result.error = path.generic_string() + ": " + result.error;
    }
    return result;
}

bool NavMeshAssetIO::Write(const std::filesystem::path& path, const NavMeshAsset& asset, std::string& error) {
    if (path.extension() != NavMeshAsset::Extension) {
        error = "navigation meshes use the " + std::string{ NavMeshAsset::Extension } + " extension";
        return false;
    }
    if (std::string invalid = Validate(asset); !invalid.empty()) {
        error = std::move(invalid);
        return false;
    }
    const std::vector<std::uint8_t> bytes = Serialize(asset);
    if (bytes.empty()) {
        error = "could not encode the navigation mesh";
        return false;
    }
    std::error_code code;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), code);
    }
    if (code || !kb::scene::SceneAssetBinaryIO::WriteBytesAtomically(path, bytes)) {
        error = "could not write " + path.generic_string();
        return false;
    }
    return true;
}

std::string_view NavMeshAssetLoader::Type() const noexcept {
    return NavMeshAsset::AssetType;
}

std::type_index NavMeshAssetLoader::PayloadType() const noexcept {
    return typeid(NavMeshAsset);
}

std::vector<std::string> NavMeshAssetLoader::Extensions() const {
    return { std::string{ NavMeshAsset::Extension } };
}

kb::assets::AssetLoadResult NavMeshAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!request.ReadSourceBytes(bytes, error)) {
        return { .asset = {}, .error = std::move(error) };
    }
    NavMeshAssetReadResult parsed = NavMeshAssetIO::Parse(bytes);
    if (!parsed.succeeded) {
        return { .asset = {}, .error = std::move(parsed.error) };
    }
    return { .asset = std::make_shared<NavMeshAsset>(std::move(parsed.asset)), .error = {} };
}

} // namespace kb::navigation
