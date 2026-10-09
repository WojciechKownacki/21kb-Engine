#include "engine/world/WorldEditSession.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTagCatalog.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"
#include "scene/prefab/ScenePrefabCaptureService.hpp"
#include "scene/prefab/ScenePrefabCaptureValidator.hpp"
#include "world/WorldObjectSplit.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <unordered_map>

namespace kb::world {
namespace {

using Guid = std::string;

struct ObjectRecord {
    WorldObjectHeader header;
    bool onDisk = false;
    // Unsaved state of an object that was unloaded after being edited.
    std::vector<std::uint8_t> pending;
    bool deleted = false;
    bool loaded = false;
    scene::SceneEntity root{};
};

struct CapturedObject {
    Guid guid;
    scene::SceneEntity root{};
    std::vector<std::uint8_t> bytes;
    WorldObjectHeader header;
};

[[nodiscard]] bool InRect(const WorldCellCoord& cell, const WorldCellCoord& min, const WorldCellCoord& max) noexcept {
    return cell.x >= min.x && cell.x <= max.x && cell.z >= min.z && cell.z <= max.z;
}

// The world position of an object's root in double precision (docs/large_worlds.md): it decides the object's cell.
[[nodiscard]] WorldPoint ToPoint(const kb::scene::Scene& scene, kb::scene::SceneEntity root) {
    const kb::math::DVec3 position = scene.Transforms().WorldTranslation(root);
    return { position.x, position.y, position.z };
}

} // namespace

struct WorldEditSession::Impl {
    scene::Scene* scene = nullptr;
    std::filesystem::path descriptorPath;
    WorldDescriptor descriptor;
    std::filesystem::path objectsDirectory;
    std::map<Guid, ObjectRecord> objects;
    std::unordered_map<std::uint64_t, Guid> byEntity;
    std::set<WorldCellCoord> loadedCells;
    WorldSaveStats lastSave;
    bool descriptorDirty = false;

    [[nodiscard]] WorldPartitionGrid Grid() const { return WorldPartitionGrid{ descriptor.cellSize }; }
    [[nodiscard]] std::filesystem::path FileOf(const Guid& guid) const {
        return objectsDirectory / (guid + std::string{ WorldObjectFile::Extension });
    }

    [[nodiscard]] std::vector<std::uint8_t> DiskBytes(const Guid& guid) const {
        return kb::scene::SceneAssetBinaryIO::ReadAllBytes(FileOf(guid));
    }

    // Links between objects, in both directions, from the stored headers.
    [[nodiscard]] std::map<Guid, std::set<Guid>> HeaderLinks() const {
        std::map<Guid, std::set<Guid>> links;
        for (const auto& [guid, record] : objects) {
            if (record.deleted) continue;
            for (const Guid& target : record.header.references) {
                if (objects.contains(target)) {
                    links[guid].insert(target);
                    links[target].insert(guid);
                }
            }
        }
        return links;
    }

    [[nodiscard]] static std::set<Guid> Expand(std::set<Guid> seeds, const std::map<Guid, std::set<Guid>>& links) {
        std::deque<Guid> queue{ seeds.begin(), seeds.end() };
        while (!queue.empty()) {
            const Guid guid = queue.front();
            queue.pop_front();
            const auto found = links.find(guid);
            if (found == links.end()) continue;
            for (const Guid& next : found->second) {
                if (seeds.insert(next).second) {
                    queue.push_back(next);
                }
            }
        }
        return seeds;
    }

    // The scene's capturable root objects in hierarchy order. New roots get a
    // record and a fresh guid; records whose root is gone are marked deleted.
    [[nodiscard]] std::vector<scene::SceneObject> TrackRoots() {
        std::vector<scene::SceneObject> roots;
        for (const scene::SceneObject root : scene->Hierarchy().RootObjects()) {
            if (kb::scene::ScenePrefabCaptureValidator::CanCapture(*scene, root)) {
                roots.push_back(root);
            }
        }
        std::set<std::uint64_t> live;
        for (const scene::SceneObject root : roots) {
            live.insert(root.Entity().Id());
            if (!byEntity.contains(root.Entity().Id())) {
                Guid guid = MakeWorldObjectGuid();
                while (objects.contains(guid)) guid = MakeWorldObjectGuid();
                ObjectRecord record;
                record.header.guid = guid;
                record.loaded = true;
                record.root = root.Entity();
                objects.emplace(guid, std::move(record));
                byEntity.emplace(root.Entity().Id(), guid);
            }
        }
        for (auto it = byEntity.begin(); it != byEntity.end();) {
            if (!live.contains(it->first)) {
                ObjectRecord& record = objects.at(it->second);
                record.deleted = true;
                record.loaded = false;
                record.root = {};
                record.pending.clear();
                it = byEntity.erase(it);
            } else {
                ++it;
            }
        }
        return roots;
    }

    // Captures every root object of the scene as world objects.
    [[nodiscard]] bool Capture(std::vector<CapturedObject>& captured, std::string& error) {
        captured.clear();
        // Positions are read through the transform storage; publish pending local edits first.
        scene->Runtime().SynchronizeTransforms();
        const std::vector<scene::SceneObject> roots = TrackRoots();
        if (roots.empty()) {
            return true;
        }
        const kb::scene::ScenePrefab prefab = kb::scene::ScenePrefabCaptureService::CaptureRoots(*scene, roots, {});
        std::vector<WorldSplitObject> split;
        const bool splitSucceeded = SplitIntoWorldObjects(prefab, [&](std::size_t ordinal, const kb::scene::ScenePrefabNodeDesc&) {
            return ordinal < roots.size() ? byEntity.at(roots[ordinal].Entity().Id()) : Guid{};
        }, split, error);
        if (!splitSucceeded) {
            return false;
        }
        if (split.size() != roots.size()) {
            error = "the scene could not be captured object by object";
            return false;
        }
        captured.reserve(split.size());
        for (std::size_t index = 0U; index < split.size(); ++index) {
            ObjectRecord& record = objects.at(split[index].guid);
            WorldObjectFile file;
            file.header.guid = split[index].guid;
            file.header.name = split[index].prefab.Nodes().front().name;
            file.header.dataLayer = record.header.dataLayer;
            file.header.alwaysLoaded = record.header.alwaysLoaded;
            file.header.position = ToPoint(*scene, roots[index].Entity());
            file.header.references = split[index].references;
            file.header.nodeCount = static_cast<std::uint32_t>(split[index].prefab.NodeCount());
            file.prefab = std::move(split[index].prefab);
            std::vector<std::uint8_t> bytes = WorldObjectFileIO::Serialize(file, error);
            if (bytes.empty()) {
                error = "object \"" + file.header.name + "\": " + error;
                return false;
            }
            captured.push_back({ .guid = file.header.guid, .root = roots[index].Entity(), .bytes = std::move(bytes), .header = std::move(file.header) });
        }
        return true;
    }

    [[nodiscard]] std::size_t Load(const std::set<Guid>& requested, std::string& error) {
        std::vector<WorldObjectFile> files;
        std::vector<Guid> guids;
        for (const Guid& guid : requested) {
            ObjectRecord& record = objects.at(guid);
            if (record.loaded || record.deleted) continue;
            WorldObjectReadResult read = record.pending.empty()
                ? WorldObjectFileIO::Read(FileOf(guid))
                : WorldObjectFileIO::Parse(record.pending);
            if (!read.succeeded) {
                error = read.error;
                return 0U;
            }
            guids.push_back(guid);
            files.push_back(std::move(read.object));
        }
        if (files.empty()) {
            return 0U;
        }
        // One instantiation per batch so links between the loaded objects resolve.
        kb::scene::ScenePrefab batch;
        std::vector<std::uint32_t> rootNodes;
        for (const WorldObjectFile& file : files) {
            const std::uint32_t base = static_cast<std::uint32_t>(batch.NodeCount());
            rootNodes.push_back(base);
            for (kb::scene::ScenePrefabNodeDesc node : file.prefab.Nodes()) {
                if (node.parentNode != kb::scene::ScenePrefabNodeDesc::NoParent) node.parentNode += base;
                static_cast<void>(batch.AddNode(std::move(node)));
            }
        }
        const kb::scene::ScenePrefabInstance instance = scene->Prefabs().Instantiate(batch,
            kb::scene::ScenePrefabInstantiationSettings{ .parent = {}, .namePrefix = {}, .assignNames = true, .syncWorldHierarchy = false, .linkPrefabInstances = true });
        if (instance.ObjectCount() != batch.NodeCount()) {
            error = "world objects could not be created in the scene";
            return 0U;
        }
        for (std::uint32_t node = 0U; node < static_cast<std::uint32_t>(batch.NodeCount()); ++node) {
            scene->Tags().RegisterAssignedTags(instance.ObjectAt(node).Entity());
        }
        for (std::size_t index = 0U; index < files.size(); ++index) {
            ObjectRecord& record = objects.at(guids[index]);
            record.header = files[index].header;
            record.loaded = true;
            record.root = instance.ObjectAt(rootNodes[index]).Entity();
            byEntity[record.root.Id()] = guids[index];
        }
        scene->Runtime().SynchronizeTransforms();
        return files.size();
    }

    // Removes objects from the scene, keeping unsaved edits as pending bytes.
    [[nodiscard]] std::size_t Unload(const std::function<bool(const ObjectRecord&, const WorldPoint&)>& select, std::string& error) {
        std::vector<CapturedObject> captured;
        if (!Capture(captured, error)) {
            return 0U;
        }
        std::map<Guid, std::set<Guid>> links;
        std::set<Guid> seeds;
        for (const CapturedObject& object : captured) {
            for (const Guid& target : object.header.references) {
                links[object.guid].insert(target);
                links[target].insert(object.guid);
            }
            if (select(objects.at(object.guid), object.header.position)) {
                seeds.insert(object.guid);
            }
        }
        std::set<Guid> group = Expand(seeds, links);
        // A group holding an always-loaded object stays: it belongs to the persistent part.
        for (const Guid& guid : std::set<Guid>{ group }) {
            if (objects.at(guid).header.alwaysLoaded) {
                const std::set<Guid> keep = Expand({ guid }, links);
                for (const Guid& kept : keep) group.erase(kept);
            }
        }
        std::size_t removed = 0U;
        for (CapturedObject& object : captured) {
            if (!group.contains(object.guid)) continue;
            ObjectRecord& record = objects.at(object.guid);
            const bool unchanged = record.onDisk && DiskBytes(object.guid) == object.bytes;
            record.pending = unchanged ? std::vector<std::uint8_t>{} : std::move(object.bytes);
            record.header = std::move(object.header);
            if (scene->Entities().IsAlive(record.root)) {
                scene->Entities().Destroy(record.root);
            }
            byEntity.erase(record.root.Id());
            record.root = {};
            record.loaded = false;
            ++removed;
        }
        return removed;
    }
};

WorldEditSession::WorldEditSession() = default;
WorldEditSession::~WorldEditSession() = default;
WorldEditSession::WorldEditSession(WorldEditSession&&) noexcept = default;
WorldEditSession& WorldEditSession::operator=(WorldEditSession&&) noexcept = default;

bool WorldEditSession::Open(scene::Scene& scene, const std::filesystem::path& descriptorPath, std::string& error) {
    // Callers test the error text after a call, so a stale one is never left behind.
    error.clear();
    WorldDescriptorReadResult descriptor = WorldDescriptorIO::Read(descriptorPath);
    if (!descriptor.succeeded) {
        error = descriptor.error;
        return false;
    }
    auto impl = std::make_unique<Impl>();
    impl->scene = &scene;
    impl->descriptorPath = descriptorPath;
    impl->descriptor = std::move(descriptor.descriptor);
    impl->objectsDirectory = WorldPaths::ObjectsDirectory(descriptorPath, impl->descriptor);
    for (const std::filesystem::path& file : WorldObjectFileIO::List(impl->objectsDirectory)) {
        WorldObjectReadResult read = WorldObjectFileIO::Read(file, true);
        if (!read.succeeded) {
            error = read.error;
            return false;
        }
        ObjectRecord record;
        record.header = std::move(read.object.header);
        record.onDisk = true;
        impl->objects.emplace(record.header.guid, std::move(record));
    }
    if (!scene.Tags().ReplaceDefinitions(impl->descriptor.tagDefinitions)) {
        error = "the world's tag definitions are invalid";
        return false;
    }
    impl_ = std::move(impl);
    std::set<Guid> persistent;
    for (const auto& [guid, record] : impl_->objects) {
        if (record.header.alwaysLoaded) persistent.insert(guid);
    }
    static_cast<void>(impl_->Load(Impl::Expand(persistent, impl_->HeaderLinks()), error));
    if (!error.empty()) {
        impl_.reset();
        return false;
    }
    return true;
}

void WorldEditSession::Close() noexcept {
    impl_.reset();
}

bool WorldEditSession::IsOpen() const noexcept {
    return impl_ != nullptr;
}

const WorldDescriptor& WorldEditSession::Descriptor() const noexcept {
    static const WorldDescriptor empty{};
    return impl_ ? impl_->descriptor : empty;
}

const std::filesystem::path& WorldEditSession::DescriptorPath() const noexcept {
    static const std::filesystem::path empty{};
    return impl_ ? impl_->descriptorPath : empty;
}

double WorldEditSession::CellSize() const noexcept {
    return impl_ ? impl_->descriptor.cellSize : 0.0;
}

std::size_t WorldEditSession::LoadRegion(WorldCellCoord min, WorldCellCoord max, std::string& error) {
    error.clear();
    if (!impl_) {
        error = "no world is open";
        return 0U;
    }
    const WorldPartitionGrid grid = impl_->Grid();
    std::set<Guid> seeds;
    for (const auto& [guid, record] : impl_->objects) {
        if (record.deleted || record.loaded) continue;
        const std::optional<WorldCellCoord> cell = grid.CellOf(record.header.position.x, record.header.position.z);
        if (cell.has_value() && InRect(*cell, min, max)) {
            seeds.insert(guid);
            impl_->loadedCells.insert(*cell);
        }
    }
    for (const WorldCellCoord& cell : OccupiedCells()) {
        if (InRect(cell, min, max)) impl_->loadedCells.insert(cell);
    }
    return impl_->Load(Impl::Expand(seeds, impl_->HeaderLinks()), error);
}

std::size_t WorldEditSession::UnloadRegion(WorldCellCoord min, WorldCellCoord max, std::string& error) {
    error.clear();
    if (!impl_) {
        error = "no world is open";
        return 0U;
    }
    const WorldPartitionGrid grid = impl_->Grid();
    const std::size_t removed = impl_->Unload([&](const ObjectRecord&, const WorldPoint& position) {
        const std::optional<WorldCellCoord> cell = grid.CellOf(position.x, position.z);
        return cell.has_value() && InRect(*cell, min, max);
    }, error);
    std::erase_if(impl_->loadedCells, [&](const WorldCellCoord& cell) { return InRect(cell, min, max); });
    return removed;
}

std::size_t WorldEditSession::LoadAll(std::string& error) {
    error.clear();
    if (!impl_) {
        error = "no world is open";
        return 0U;
    }
    std::set<Guid> all;
    for (const auto& [guid, record] : impl_->objects) {
        if (!record.deleted) all.insert(guid);
    }
    const std::size_t loaded = impl_->Load(all, error);
    for (const WorldCellCoord& cell : OccupiedCells()) impl_->loadedCells.insert(cell);
    return loaded;
}

std::size_t WorldEditSession::UnloadAll(std::string& error) {
    error.clear();
    if (!impl_) {
        error = "no world is open";
        return 0U;
    }
    const std::size_t removed = impl_->Unload([](const ObjectRecord&, const WorldPoint&) { return true; }, error);
    impl_->loadedCells.clear();
    return removed;
}

bool WorldEditSession::IsCellLoaded(WorldCellCoord cell) const {
    return impl_ && impl_->loadedCells.contains(cell);
}

std::vector<WorldCellCoord> WorldEditSession::LoadedCells() const {
    return impl_ ? std::vector<WorldCellCoord>{ impl_->loadedCells.begin(), impl_->loadedCells.end() } : std::vector<WorldCellCoord>{};
}

std::vector<WorldCellCoord> WorldEditSession::OccupiedCells() const {
    std::set<WorldCellCoord> cells;
    if (!impl_) return {};
    for (const WorldEditObjectInfo& object : Objects()) {
        if (!object.alwaysLoaded && object.cell.has_value()) cells.insert(*object.cell);
    }
    return { cells.begin(), cells.end() };
}

std::vector<WorldEditObjectInfo> WorldEditSession::Objects() const {
    std::vector<WorldEditObjectInfo> output;
    if (!impl_) return output;
    const WorldPartitionGrid grid = impl_->Grid();
    for (const auto& [guid, record] : impl_->objects) {
        if (record.deleted) continue;
        WorldEditObjectInfo info;
        info.guid = guid;
        info.name = record.header.name;
        info.dataLayer = record.header.dataLayer;
        info.alwaysLoaded = record.header.alwaysLoaded;
        info.position = record.header.position;
        info.loaded = record.loaded;
        info.root = record.root;
        if (record.loaded && impl_->scene->Entities().IsAlive(record.root)) {
            info.position = ToPoint(*impl_->scene, record.root);
            info.name = impl_->scene->Entities().Name(record.root);
        }
        info.cell = grid.CellOf(info.position.x, info.position.z);
        output.push_back(std::move(info));
    }
    return output;
}

std::optional<WorldEditObjectInfo> WorldEditSession::FindObject(scene::SceneEntity root) const {
    if (!impl_) return std::nullopt;
    const auto found = impl_->byEntity.find(root.Id());
    if (found == impl_->byEntity.end()) return std::nullopt;
    for (WorldEditObjectInfo& info : Objects()) {
        if (info.guid == found->second) return std::move(info);
    }
    return std::nullopt;
}

std::size_t WorldEditSession::LoadedObjectCount() const {
    return impl_ ? impl_->byEntity.size() : 0U;
}

bool WorldEditSession::SetObjectDataLayer(scene::SceneEntity root, std::string_view layer, std::string& error) {
    error.clear();
    if (!impl_) {
        error = "no world is open";
        return false;
    }
    if (!layer.empty() && impl_->descriptor.FindDataLayer(layer) == nullptr) {
        error = "the world declares no data layer \"" + std::string{ layer } + "\"";
        return false;
    }
    const auto found = impl_->byEntity.find(root.Id());
    if (found == impl_->byEntity.end()) {
        // A root created since the last capture becomes an object now.
        std::vector<CapturedObject> captured;
        if (!impl_->Capture(captured, error)) return false;
        if (!impl_->byEntity.contains(root.Id())) {
            error = "the entity is not a root object of the world";
            return false;
        }
    }
    impl_->objects.at(impl_->byEntity.at(root.Id())).header.dataLayer = std::string{ layer };
    return true;
}

bool WorldEditSession::SetObjectAlwaysLoaded(scene::SceneEntity root, bool alwaysLoaded, std::string& error) {
    error.clear();
    if (!impl_) {
        error = "no world is open";
        return false;
    }
    if (!impl_->byEntity.contains(root.Id())) {
        std::vector<CapturedObject> captured;
        if (!impl_->Capture(captured, error)) return false;
        if (!impl_->byEntity.contains(root.Id())) {
            error = "the entity is not a root object of the world";
            return false;
        }
    }
    impl_->objects.at(impl_->byEntity.at(root.Id())).header.alwaysLoaded = alwaysLoaded;
    return true;
}

bool WorldEditSession::Save(std::string& error) {
    error.clear();
    if (!impl_) {
        error = "no world is open";
        return false;
    }
    std::vector<CapturedObject> captured;
    if (!impl_->Capture(captured, error)) {
        return false;
    }
    WorldSaveStats stats;
    for (CapturedObject& object : captured) {
        ObjectRecord& record = impl_->objects.at(object.guid);
        if (record.onDisk && impl_->DiskBytes(object.guid) == object.bytes) {
            ++stats.unchanged;
        } else {
            if (!WorldObjectFileIO::WriteBytes(impl_->FileOf(object.guid), object.bytes, error)) return false;
            ++stats.written;
        }
        record.onDisk = true;
        record.header = std::move(object.header);
    }
    for (auto it = impl_->objects.begin(); it != impl_->objects.end();) {
        ObjectRecord& record = it->second;
        if (record.deleted) {
            std::error_code code;
            if (record.onDisk && !std::filesystem::remove(impl_->FileOf(it->first), code) && code) {
                error = "could not delete " + impl_->FileOf(it->first).generic_string() + ": " + code.message();
                return false;
            }
            stats.deleted += record.onDisk ? 1U : 0U;
            it = impl_->objects.erase(it);
            continue;
        }
        if (!record.loaded && !record.pending.empty()) {
            if (!WorldObjectFileIO::WriteBytes(impl_->FileOf(it->first), record.pending, error)) return false;
            record.pending.clear();
            record.onDisk = true;
            ++stats.written;
        }
        ++it;
    }
    const auto names = impl_->scene->Tags().Names();
    std::vector<std::string> tags{ names.begin(), names.end() };
    if (tags != impl_->descriptor.tagDefinitions || impl_->descriptorDirty) {
        WorldDescriptor updated = impl_->descriptor;
        updated.tagDefinitions = std::move(tags);
        if (!WorldDescriptorIO::Write(impl_->descriptorPath, updated, error)) return false;
        impl_->descriptor = std::move(updated);
        impl_->descriptorDirty = false;
    }
    impl_->lastSave = stats;
    return true;
}

bool WorldEditSession::DeclareDataLayer(std::string_view name, bool initiallyActive, std::string& error) {
    error.clear();
    if (!impl_) {
        error = "no world is open";
        return false;
    }
    WorldDescriptor updated = impl_->descriptor;
    updated.dataLayers.push_back({ .name = std::string{ name }, .initiallyActive = initiallyActive });
    if (std::string invalid = WorldDescriptorIO::Validate(updated); !invalid.empty()) {
        error = std::move(invalid);
        return false;
    }
    impl_->descriptor = std::move(updated);
    impl_->descriptorDirty = true;
    return true;
}

std::vector<std::string> WorldEditSession::RootObjectGuids() {
    std::vector<std::string> guids;
    if (!impl_) return guids;
    for (const scene::SceneObject root : impl_->TrackRoots()) {
        guids.push_back(impl_->byEntity.at(root.Entity().Id()));
    }
    return guids;
}

bool WorldEditSession::RebindRootObjects(const std::vector<std::string>& guidsInRootOrder, std::string& error) {
    error.clear();
    if (!impl_) {
        error = "no world is open";
        return false;
    }
    std::vector<scene::SceneObject> roots;
    for (const scene::SceneObject root : impl_->scene->Hierarchy().RootObjects()) {
        if (kb::scene::ScenePrefabCaptureValidator::CanCapture(*impl_->scene, root)) roots.push_back(root);
    }
    if (roots.size() != guidsInRootOrder.size() ||
        !std::ranges::all_of(guidsInRootOrder, [this](const std::string& guid) { return impl_->objects.contains(guid); })) {
        error = "the scene's root objects no longer match the world's objects";
        return false;
    }
    impl_->byEntity.clear();
    for (std::size_t index = 0U; index < roots.size(); ++index) {
        ObjectRecord& record = impl_->objects.at(guidsInRootOrder[index]);
        record.root = roots[index].Entity();
        record.loaded = true;
        impl_->byEntity.emplace(record.root.Id(), guidsInRootOrder[index]);
    }
    return true;
}

WorldSaveStats WorldEditSession::LastSaveStats() const noexcept {
    return impl_ ? impl_->lastSave : WorldSaveStats{};
}

WorldMigrationResult WorldMigration::ConvertScene(const std::filesystem::path& scenePath, const std::filesystem::path& descriptorPath, double cellSize) {
    WorldMigrationResult result;
    result.descriptorPath = descriptorPath;
    std::error_code code;
    if (descriptorPath.extension() != WorldDescriptor::Extension) {
        result.error = "the world file must use the " + std::string{ WorldDescriptor::Extension } + " extension";
        return result;
    }
    if (std::filesystem::exists(descriptorPath, code)) {
        result.error = descriptorPath.generic_string() + " already exists";
        return result;
    }
    if (!IsValidCellSize(cellSize)) {
        result.error = "cell size must be between 1 and 1000000 metres";
        return result;
    }
    const kb::scene::SceneDocumentLoadResult scene = kb::scene::SceneDocumentService::Load(scenePath);
    if (!scene.succeeded) {
        result.error = scenePath.generic_string() + ": " + scene.error;
        return result;
    }
    WorldDescriptor descriptor;
    descriptor.guid = MakeDeterministicWorldObjectGuid("world:" + scene.document.guid);
    descriptor.name = descriptorPath.stem().string();
    descriptor.cellSize = cellSize;
    descriptor.objectsDirectory = descriptorPath.stem().string() + ".objects";
    // The tag catalogue always starts with the built-in tags; store it the way the
    // editor will save it, so the first save does not rewrite the world file.
    descriptor.tagDefinitions.assign(kb::scene::SceneTagCatalog::DefaultNames.begin(), kb::scene::SceneTagCatalog::DefaultNames.end());
    for (const std::string& tag : scene.document.tagDefinitions) {
        if (!kb::scene::SceneTagCatalog::IsBuiltIn(tag) && std::ranges::find(descriptor.tagDefinitions, tag) == descriptor.tagDefinitions.end()) {
            descriptor.tagDefinitions.push_back(tag);
        }
    }
    if (std::string invalid = WorldDescriptorIO::Validate(descriptor); !invalid.empty()) {
        result.error = invalid;
        return result;
    }
    const std::filesystem::path objectsDirectory = WorldPaths::ObjectsDirectory(descriptorPath, descriptor);
    if (std::filesystem::exists(objectsDirectory, code) && !WorldObjectFileIO::List(objectsDirectory).empty()) {
        result.error = objectsDirectory.generic_string() + " already holds world objects";
        return result;
    }
    std::vector<WorldSplitObject> split;
    std::string error;
    const std::string sceneKey = scene.document.guid.empty() ? scenePath.filename().generic_string() : scene.document.guid;
    if (!SplitIntoWorldObjects(scene.document.worldPrefab, [&](std::size_t, const kb::scene::ScenePrefabNodeDesc& root) {
            return MakeDeterministicWorldObjectGuid(sceneKey + ":" + std::to_string(root.stableId));
        }, split, error)) {
        result.error = error;
        return result;
    }
    for (WorldSplitObject& object : split) {
        WorldObjectFile file;
        const kb::scene::ScenePrefabNodeDesc& root = object.prefab.Nodes().front();
        file.header.guid = object.guid;
        file.header.name = root.name;
        const kb::math::DVec3 rootPosition = root.LocalTranslation();
        file.header.position = { rootPosition.x, rootPosition.y, rootPosition.z };
        file.header.references = object.references;
        file.header.nodeCount = static_cast<std::uint32_t>(object.prefab.NodeCount());
        // Scene-wide lighting and backdrops have no place of their own: keep them loaded.
        file.header.alwaysLoaded = std::ranges::any_of(object.prefab.Nodes(), [](const kb::scene::ScenePrefabNodeDesc& node) {
            return node.components.worldBackdrop.has_value() || node.components.ambientRadiance.has_value() ||
                (node.components.light.has_value() && node.components.light->kind == kb::scene::LightKind::Directional);
        });
        file.prefab = std::move(object.prefab);
        const std::vector<std::uint8_t> bytes = WorldObjectFileIO::Serialize(file, error);
        if (bytes.empty() || !WorldObjectFileIO::WriteBytes(objectsDirectory / (file.header.guid + std::string{ WorldObjectFile::Extension }), bytes, error)) {
            result.error = "object \"" + file.header.name + "\": " + error;
            return result;
        }
    }
    if (!WorldDescriptorIO::Write(descriptorPath, descriptor, error)) {
        result.error = error;
        return result;
    }
    result.succeeded = true;
    result.objectCount = split.size();
    return result;
}

} // namespace kb::world
