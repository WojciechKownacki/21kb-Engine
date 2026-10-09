#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/world/WorldDescriptor.hpp"
#include "engine/world/WorldObjectFile.hpp"
#include "engine/world/WorldPartitionGrid.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {
class Scene;
}

namespace kb::world {

struct WorldEditObjectInfo {
    std::string guid;
    std::string name;
    std::string dataLayer;
    bool alwaysLoaded = false;
    WorldPoint position{};
    std::optional<WorldCellCoord> cell;
    bool loaded = false;
    scene::SceneEntity root{};
};

struct WorldSaveStats {
    std::size_t written = 0U;
    std::size_t deleted = 0U;
    std::size_t unchanged = 0U;
};

// Edits a partitioned world in a scene one region at a time. Opening loads the
// always-loaded objects; regions of cells are then loaded and unloaded on demand.
// Saving writes one file per object and only the files whose content changed,
// deletes the files of objects removed from the scene, and keeps edits made to
// objects that were unloaded before the save.
class WorldEditSession {
public:
    WorldEditSession();
    ~WorldEditSession();
    WorldEditSession(const WorldEditSession&) = delete;
    WorldEditSession& operator=(const WorldEditSession&) = delete;
    WorldEditSession(WorldEditSession&&) noexcept;
    WorldEditSession& operator=(WorldEditSession&&) noexcept;

    // The scene should hold no other root objects: everything at the root of the
    // scene is treated as part of the world when saving.
    [[nodiscard]] bool Open(scene::Scene& scene, const std::filesystem::path& descriptorPath, std::string& error);
    void Close() noexcept;
    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] const WorldDescriptor& Descriptor() const noexcept;
    [[nodiscard]] const std::filesystem::path& DescriptorPath() const noexcept;
    [[nodiscard]] double CellSize() const noexcept;

    // Loads every object whose cell lies in the inclusive rectangle, together with
    // the objects they are linked to. Returns the number of objects created.
    [[nodiscard]] std::size_t LoadRegion(WorldCellCoord min, WorldCellCoord max, std::string& error);
    // Removes the region's objects (and linked objects) from the scene; their
    // unsaved edits are kept for the next Save. Returns the number removed.
    [[nodiscard]] std::size_t UnloadRegion(WorldCellCoord min, WorldCellCoord max, std::string& error);
    [[nodiscard]] std::size_t LoadAll(std::string& error);
    [[nodiscard]] std::size_t UnloadAll(std::string& error);

    [[nodiscard]] bool IsCellLoaded(WorldCellCoord cell) const;
    [[nodiscard]] std::vector<WorldCellCoord> LoadedCells() const;
    // Cells holding at least one spatially loaded object, saved or live.
    [[nodiscard]] std::vector<WorldCellCoord> OccupiedCells() const;
    [[nodiscard]] std::vector<WorldEditObjectInfo> Objects() const;
    [[nodiscard]] std::optional<WorldEditObjectInfo> FindObject(scene::SceneEntity root) const;
    [[nodiscard]] std::size_t LoadedObjectCount() const;

    // Assigns a root object to a data layer ("" = base). The layer must be declared
    // by the descriptor.
    [[nodiscard]] bool SetObjectDataLayer(scene::SceneEntity root, std::string_view layer, std::string& error);
    [[nodiscard]] bool SetObjectAlwaysLoaded(scene::SceneEntity root, bool alwaysLoaded, std::string& error);
    // Adds a data layer to the world; the world file is rewritten on the next Save.
    [[nodiscard]] bool DeclareDataLayer(std::string_view name, bool initiallyActive, std::string& error);

    // Guids of the scene's root objects in hierarchy order; new roots become objects.
    [[nodiscard]] std::vector<std::string> RootObjectGuids();
    // Re-associates the objects with the scene's roots after every root was
    // recreated in the same order (play mode restoring the edited scene).
    [[nodiscard]] bool RebindRootObjects(const std::vector<std::string>& guidsInRootOrder, std::string& error);

    [[nodiscard]] bool Save(std::string& error);
    [[nodiscard]] WorldSaveStats LastSaveStats() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct WorldMigrationResult {
    bool succeeded = false;
    std::size_t objectCount = 0U;
    std::filesystem::path descriptorPath;
    std::string error;
};

// Converts a single-file scene into a partitioned world: every root object of
// the scene becomes one object file. The scene file itself is left untouched.
// `descriptorPath` must not exist yet; its object directory is created next to it.
class WorldMigration {
public:
    WorldMigration() = delete;

    [[nodiscard]] static WorldMigrationResult ConvertScene(
        const std::filesystem::path& scenePath,
        const std::filesystem::path& descriptorPath,
        double cellSize);
};

} // namespace kb::world
