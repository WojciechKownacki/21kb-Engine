#include "engine/world/WorldObjectFile.hpp"

#include "engine/scene/SceneDocument.hpp"
#include "engine/ui/UIEntityReferences.hpp"
#include "engine/world/WorldDescriptor.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"
#include "scene/asset/io/SceneAssetReader.hpp"
#include "scene/asset/io/SceneAssetWriter.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <random>

namespace kb::world {
namespace {

namespace io = kb::scene::SceneAssetBinaryIO;

constexpr std::array<std::uint8_t, 8U> kMagic{ '2', '1', 'K', 'B', 'W', 'O', 'B', 'J' };
constexpr std::uint32_t kMaxReferences = 1U << 20U;
constexpr std::uint8_t kAlwaysLoadedFlag = 1U;
constexpr std::string_view kWorldType = "WorldObject";

[[nodiscard]] std::uint64_t Fnv1a64(std::string_view value, std::uint64_t seed) noexcept {
    std::uint64_t hash = seed;
    for (const char character : value) {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= 0x100000001B3ULL;
    }
    return hash;
}

[[nodiscard]] std::uint64_t Mix(std::uint64_t value) noexcept {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] std::string Hex128(std::uint64_t high, std::uint64_t low) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string out(32U, '0');
    for (std::size_t index = 0U; index < 16U; ++index) {
        out[index] = kDigits[(high >> (60U - index * 4U)) & 0xFU];
        out[16U + index] = kDigits[(low >> (60U - index * 4U)) & 0xFU];
    }
    return out;
}

[[nodiscard]] bool ReadDouble(io::ByteReader& input, double& value) {
    std::uint64_t bits = 0U;
    if (!input.ReadUInt64(bits)) {
        return false;
    }
    value = std::bit_cast<double>(bits);
    return std::isfinite(value);
}

[[nodiscard]] WorldObjectReadResult Fail(std::string error) {
    return { .succeeded = false, .object = {}, .error = std::move(error) };
}

// A link from a node of this object to a node of another object. The scene payload
// must validate on its own, so such links are stored beside it and restored on read.
enum class LinkKind : std::uint8_t {
    Joint = 0U,
    RegionPortal = 1U,
    LensEcho = 2U,
};

struct ExternalLink {
    std::uint32_t node = 0U;
    LinkKind kind = LinkKind::Joint;
    std::uint64_t first = 0U;
    std::uint64_t second = 0U;
    bool enabled = false;
};

constexpr std::uint64_t kUnresolved = UINT64_MAX;

// Moves every link leaving the object out of `prefab` into `links`.
[[nodiscard]] bool ExtractExternalLinks(kb::scene::ScenePrefab& prefab, std::vector<ExternalLink>& links, std::string& error) {
    std::vector<std::uint64_t> internal;
    for (const kb::scene::ScenePrefabNodeDesc& node : prefab.Nodes()) internal.push_back(node.stableId);
    std::ranges::sort(internal);
    const auto external = [&internal](std::uint64_t id) {
        return id != 0U && !std::ranges::binary_search(internal, id);
    };
    for (std::uint32_t index = 0U; index < static_cast<std::uint32_t>(prefab.NodeCount()); ++index) {
        kb::scene::ScenePrefabNodeDesc& node = *prefab.TryGetMutableNode(index);
        kb::scene::ScenePrefabNodeComponents& components = node.components;
        if (components.joint.has_value() && components.joint->connectedNodeStableId != kUnresolved &&
            external(components.joint->connectedNodeStableId)) {
            links.push_back({ .node = index, .kind = LinkKind::Joint, .first = components.joint->connectedNodeStableId, .second = 0U, .enabled = true });
            components.joint->connectedNodeStableId = kb::scene::ScenePrefabJointComponent::InvalidConnectedNodeStableId;
        }
        if (components.regionPortal.has_value() &&
            (external(components.regionPortal->sourceCellNodeStableId) || external(components.regionPortal->targetCellNodeStableId))) {
            kb::scene::ScenePrefabRegionPortalComponent& portal = *components.regionPortal;
            if (portal.sourceCellNodeStableId == kUnresolved || portal.targetCellNodeStableId == kUnresolved) {
                error = "\"" + node.name + "\" has a portal to an object outside the world";
                return false;
            }
            links.push_back({ .node = index, .kind = LinkKind::RegionPortal, .first = portal.sourceCellNodeStableId,
                .second = portal.targetCellNodeStableId, .enabled = portal.enabled });
            portal.sourceCellNodeStableId = kb::scene::ScenePrefabRegionPortalComponent::InvalidCellNodeStableId;
            portal.targetCellNodeStableId = kb::scene::ScenePrefabRegionPortalComponent::InvalidCellNodeStableId;
            portal.enabled = false;
        }
        if (components.lensEcho.has_value() && components.lensEcho->sourceNodeStableId != kUnresolved &&
            external(components.lensEcho->sourceNodeStableId)) {
            links.push_back({ .node = index, .kind = LinkKind::LensEcho, .first = components.lensEcho->sourceNodeStableId, .second = 0U,
                .enabled = components.lensEcho->enabled });
            components.lensEcho->sourceNodeStableId = kb::scene::ScenePrefabLensEchoComponent::InvalidSourceNodeStableId;
            components.lensEcho->enabled = false;
        }
    }
    return true;
}

[[nodiscard]] bool RestoreExternalLinks(kb::scene::ScenePrefab& prefab, const std::vector<ExternalLink>& links) {
    for (const ExternalLink& link : links) {
        kb::scene::ScenePrefabNodeDesc* node = prefab.TryGetMutableNode(link.node);
        if (node == nullptr) return false;
        switch (link.kind) {
        case LinkKind::Joint:
            if (!node->components.joint.has_value()) return false;
            node->components.joint->connectedNodeStableId = link.first;
            break;
        case LinkKind::RegionPortal:
            if (!node->components.regionPortal.has_value()) return false;
            node->components.regionPortal->sourceCellNodeStableId = link.first;
            node->components.regionPortal->targetCellNodeStableId = link.second;
            node->components.regionPortal->enabled = link.enabled;
            break;
        case LinkKind::LensEcho:
            if (!node->components.lensEcho.has_value()) return false;
            node->components.lensEcho->sourceNodeStableId = link.first;
            node->components.lensEcho->enabled = link.enabled;
            break;
        default:
            return false;
        }
    }
    return true;
}

} // namespace

bool IsValidWorldObjectGuid(std::string_view guid) noexcept {
    return guid.size() == 32U && std::ranges::all_of(guid, [](char character) {
        return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
    });
}

std::string MakeWorldObjectGuid() {
    std::random_device device;
    const std::uint64_t high = (static_cast<std::uint64_t>(device()) << 32U) ^ device();
    const std::uint64_t low = (static_cast<std::uint64_t>(device()) << 32U) ^ device();
    return Hex128(Mix(high ^ static_cast<std::uint64_t>(std::random_device{}())), Mix(low));
}

std::string MakeDeterministicWorldObjectGuid(std::string_view key) {
    return Hex128(Mix(Fnv1a64(key, 0xCBF29CE484222325ULL)), Mix(Fnv1a64(key, 0x84222325CBF29CE4ULL)));
}

std::uint64_t WorldObjectStableId(std::string_view guid, std::uint32_t nodeIndex) noexcept {
    std::uint64_t id = Mix(Fnv1a64(guid, 0xCBF29CE484222325ULL) ^ (static_cast<std::uint64_t>(nodeIndex) * 0xD6E8FEB86659FD93ULL));
    // 0 means "no node" and UINT64_MAX marks an unresolved reference.
    if (id == 0U || id == UINT64_MAX) {
        id = 1U + nodeIndex;
    }
    return id;
}

std::vector<std::uint64_t> CollectNodeReferences(const kb::scene::ScenePrefab& prefab) {
    std::vector<std::uint64_t> references;
    for (const kb::scene::ScenePrefabNodeDesc& node : prefab.Nodes()) {
        const kb::scene::ScenePrefabNodeComponents& components = node.components;
        if (components.joint.has_value()) {
            references.push_back(components.joint->connectedNodeStableId);
        }
        if (components.regionPortal.has_value()) {
            references.push_back(components.regionPortal->sourceCellNodeStableId);
            references.push_back(components.regionPortal->targetCellNodeStableId);
        }
        if (components.lensEcho.has_value()) {
            references.push_back(components.lensEcho->sourceNodeStableId);
        }
        kb::scene::UIComponentSet ui = components.ui;
        kb::scene::ForEachUIEntityReference(ui, [&references](std::uint64_t& reference) {
            references.push_back(reference);
        });
    }
    std::erase_if(references, [](std::uint64_t id) { return id == 0U || id == UINT64_MAX; });
    std::ranges::sort(references);
    references.erase(std::unique(references.begin(), references.end()), references.end());
    return references;
}

std::vector<std::uint8_t> WorldObjectFileIO::Serialize(const WorldObjectFile& object, std::string& error) {
    const WorldObjectHeader& header = object.header;
    if (!IsValidWorldObjectGuid(header.guid)) {
        error = "object guid must be 32 lowercase hexadecimal characters";
        return {};
    }
    if (!header.dataLayer.empty() && !IsValidDataLayerName(header.dataLayer)) {
        error = "object data layer name is invalid";
        return {};
    }
    if (!std::isfinite(header.position.x) || !std::isfinite(header.position.y) || !std::isfinite(header.position.z)) {
        error = "object position must be finite";
        return {};
    }
    if (header.references.size() > kMaxReferences ||
        !std::ranges::all_of(header.references, [](const std::string& guid) { return IsValidWorldObjectGuid(guid); })) {
        error = "object references must be valid object guids";
        return {};
    }
    const auto nodes = object.prefab.Nodes();
    if (nodes.empty() || nodes.front().parentNode != kb::scene::ScenePrefabNodeDesc::NoParent ||
        std::any_of(nodes.begin() + 1, nodes.end(), [](const kb::scene::ScenePrefabNodeDesc& node) {
            return node.parentNode == kb::scene::ScenePrefabNodeDesc::NoParent;
        })) {
        error = "an object file holds exactly one root and its descendants";
        return {};
    }
    kb::scene::SceneDocument document;
    document.guid = "object:" + header.guid;
    document.name = header.name.empty() ? std::string{ "Object" } : header.name;
    document.worldType = std::string{ kWorldType };
    document.tagDefinitions.clear();
    document.worldPrefab = object.prefab;
    std::vector<ExternalLink> links;
    if (!ExtractExternalLinks(document.worldPrefab, links, error)) {
        return {};
    }
    const std::vector<std::uint8_t> payload = kb::scene::SceneAssetWriter::Encode(document);
    if (payload.empty()) {
        error = "object \"" + document.name + "\" has an invalid hierarchy or component";
        return {};
    }
    std::vector<std::uint8_t> output;
    output.reserve(payload.size() + 256U);
    io::WriteRaw(output, kMagic.data(), kMagic.size());
    io::WriteUInt32(output, WorldObjectFile::CurrentVersion);
    io::WriteString(output, header.guid);
    io::WriteString(output, document.name);
    io::WriteString(output, header.dataLayer);
    io::WriteUInt8(output, header.alwaysLoaded ? kAlwaysLoadedFlag : 0U);
    io::WriteUInt64(output, std::bit_cast<std::uint64_t>(header.position.x));
    io::WriteUInt64(output, std::bit_cast<std::uint64_t>(header.position.y));
    io::WriteUInt64(output, std::bit_cast<std::uint64_t>(header.position.z));
    std::vector<std::string> references = header.references;
    std::ranges::sort(references);
    references.erase(std::unique(references.begin(), references.end()), references.end());
    io::WriteUInt32(output, static_cast<std::uint32_t>(references.size()));
    for (const std::string& reference : references) {
        io::WriteString(output, reference);
    }
    io::WriteUInt32(output, static_cast<std::uint32_t>(links.size()));
    for (const ExternalLink& link : links) {
        io::WriteUInt32(output, link.node);
        io::WriteUInt8(output, static_cast<std::uint8_t>(link.kind));
        io::WriteUInt64(output, link.first);
        io::WriteUInt64(output, link.second);
        io::WriteBool(output, link.enabled);
    }
    io::WriteUInt32(output, static_cast<std::uint32_t>(nodes.size()));
    io::WriteUInt32(output, static_cast<std::uint32_t>(payload.size()));
    io::WriteRaw(output, payload.data(), payload.size());
    return output;
}

WorldObjectReadResult WorldObjectFileIO::Parse(std::vector<std::uint8_t> bytes, bool headerOnly) {
    io::ByteReader input{ std::move(bytes) };
    std::array<std::uint8_t, kMagic.size()> magic{};
    std::uint32_t version = 0U;
    if (!input.ReadRaw(magic.data(), magic.size()) || magic != kMagic || !input.ReadUInt32(version)) {
        return Fail("not a world object file");
    }
    if (version == 0U || version > WorldObjectFile::CurrentVersion) {
        return Fail("world object file version " + std::to_string(version) + " is not supported");
    }
    WorldObjectFile object;
    WorldObjectHeader& header = object.header;
    std::uint8_t flags = 0U;
    std::uint32_t referenceCount = 0U;
    if (!input.ReadString(header.guid, 64U) || !IsValidWorldObjectGuid(header.guid) ||
        !input.ReadString(header.name) || !input.ReadString(header.dataLayer, 256U) ||
        (!header.dataLayer.empty() && !IsValidDataLayerName(header.dataLayer)) ||
        !input.ReadUInt8(flags) || (flags & ~kAlwaysLoadedFlag) != 0U ||
        !ReadDouble(input, header.position.x) || !ReadDouble(input, header.position.y) || !ReadDouble(input, header.position.z) ||
        !input.ReadUInt32(referenceCount) || referenceCount > kMaxReferences) {
        return Fail("world object header is invalid");
    }
    header.alwaysLoaded = (flags & kAlwaysLoadedFlag) != 0U;
    header.references.resize(referenceCount);
    for (std::string& reference : header.references) {
        if (!input.ReadString(reference, 64U) || !IsValidWorldObjectGuid(reference)) {
            return Fail("world object reference list is invalid");
        }
    }
    std::uint32_t linkCount = 0U;
    if (!input.ReadUInt32(linkCount) || linkCount > kMaxReferences) {
        return Fail("world object link list is invalid");
    }
    std::vector<ExternalLink> links(linkCount);
    for (ExternalLink& link : links) {
        std::uint8_t kind = 0U;
        if (!input.ReadUInt32(link.node) || !input.ReadUInt8(kind) || kind > static_cast<std::uint8_t>(LinkKind::LensEcho) ||
            !input.ReadUInt64(link.first) || !input.ReadUInt64(link.second) || !input.ReadBool(link.enabled)) {
            return Fail("world object link list is invalid");
        }
        link.kind = static_cast<LinkKind>(kind);
    }
    std::uint32_t payloadSize = 0U;
    if (!input.ReadUInt32(header.nodeCount) || header.nodeCount == 0U || !input.ReadUInt32(payloadSize)) {
        return Fail("world object header is invalid");
    }
    if (headerOnly) {
        return { .succeeded = true, .object = std::move(object), .error = {} };
    }
    std::vector<std::uint8_t> payload(payloadSize);
    if (!input.ReadRaw(payload.data(), payload.size()) || !input.Exhausted()) {
        return Fail("world object payload is truncated or followed by extra data");
    }
    kb::scene::SceneDocumentLoadResult document = kb::scene::SceneAssetReader::Read(std::move(payload));
    if (!document.succeeded) {
        return Fail("world object payload is invalid: " + document.error);
    }
    if (document.document.worldPrefab.NodeCount() != header.nodeCount ||
        document.document.worldPrefab.Nodes().front().parentNode != kb::scene::ScenePrefabNodeDesc::NoParent) {
        return Fail("world object payload does not match its header");
    }
    object.prefab = std::move(document.document.worldPrefab);
    if (!RestoreExternalLinks(object.prefab, links)) {
        return Fail("world object links do not match its payload");
    }
    return { .succeeded = true, .object = std::move(object), .error = {} };
}

WorldObjectReadResult WorldObjectFileIO::Read(const std::filesystem::path& path, bool headerOnly) {
    std::vector<std::uint8_t> bytes = io::ReadAllBytes(path);
    if (bytes.empty()) {
        return Fail("could not read " + path.generic_string());
    }
    WorldObjectReadResult result = Parse(std::move(bytes), headerOnly);
    if (!result.succeeded) {
        result.error = path.generic_string() + ": " + result.error;
    } else if (path.stem().string() != result.object.header.guid) {
        return Fail(path.generic_string() + ": file name does not match the object guid " + result.object.header.guid);
    }
    return result;
}

bool WorldObjectFileIO::WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes, std::string& error) {
    std::error_code directoryError;
    std::filesystem::create_directories(path.parent_path(), directoryError);
    if (directoryError || !io::WriteBytesAtomically(path, bytes)) {
        error = "could not write " + path.generic_string();
        return false;
    }
    return true;
}

std::vector<std::filesystem::path> WorldObjectFileIO::List(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        return files;
    }
    for (std::filesystem::directory_iterator it{ directory, error }, end; !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error) && it->path().extension() == WorldObjectFile::Extension) {
            files.push_back(it->path());
        }
    }
    std::ranges::sort(files, [](const std::filesystem::path& left, const std::filesystem::path& right) {
        return left.filename().generic_string() < right.filename().generic_string();
    });
    return files;
}

} // namespace kb::world
