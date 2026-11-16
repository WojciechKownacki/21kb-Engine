#pragma once

#include "scene/EditorHierarchyRow.hpp"
#include "scene/EditorHierarchyRowBuilder.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::editor {

// A virtual list over the scene hierarchy: it counts the visible rows without building them and builds only the
// rows asked for. A row far from where the last one was is found by the subtree sizes the scene keeps, which skip
// the subtrees before it. Search results are listed in full.
// A transient subtree leaves the count when a walk first reaches it, so the count can include one until then.
class EditorHierarchyRowModel {
public:
    static constexpr std::size_t kNotListed = std::numeric_limits<std::size_t>::max();

    // What the walks did since the last call: nodes they stood on, and sibling subtrees stepped over by their size.
    struct Work {
        std::size_t visited = 0U;
        std::size_t skipped = 0U;
    };
    [[nodiscard]] Work TakeWork() noexcept;

    // Forgets what was learned when `dirty` is set, the scene, the query or the collapsed groups changed, or the
    // scene reports that rows moved. A rename keeps the rows where they are, and a root appended at the end adds one.
    void Sync(const kb::scene::Scene& scene, const EditorHierarchyRowBuilder::CollapsedEntitySet& collapsed,
        std::string_view query, bool dirty);
    // Lists a root appended to the scene after the last Sync, keeping the rows before it.
    void AppendRoot(kb::scene::SceneEntity root);

    [[nodiscard]] std::size_t Count() const noexcept;
    // The rows [first, first + count) clamped to the list; valid until the next call.
    [[nodiscard]] const std::vector<EditorHierarchyRow>& Window(std::size_t first, std::size_t count);
    [[nodiscard]] const EditorHierarchyRow* RowAt(std::size_t index);
    // The row of an entity expected near `hint`, or kNotListed.
    [[nodiscard]] std::size_t IndexOfNear(kb::scene::SceneEntity entity, std::size_t hint);
    [[nodiscard]] bool IsListed(kb::scene::SceneEntity entity) const;
    void ForEachEntity(const std::function<void(kb::scene::SceneEntity)>& visit);

private:
    struct Frame {
        kb::scene::SceneEntity entity;
        std::size_t index = 0U;
    };
    using Path = std::vector<Frame>;

    [[nodiscard]] std::size_t SiblingCount(kb::scene::SceneEntity parent) const;
    [[nodiscard]] kb::scene::SceneEntity SiblingAt(kb::scene::SceneEntity parent, std::size_t index) const;
    [[nodiscard]] bool Expanded(kb::scene::SceneEntity entity) const;
    [[nodiscard]] bool IsTransient(kb::scene::SceneEntity entity) const;
    [[nodiscard]] bool Listed(kb::scene::SceneEntity entity);
    [[nodiscard]] std::size_t SubtreeRows(kb::scene::SceneEntity entity) const;
    [[nodiscard]] std::size_t SkipRows(kb::scene::SceneEntity entity) const;
    void Hide(kb::scene::SceneEntity entity, std::size_t rows);
    [[nodiscard]] std::size_t CountRows();
    [[nodiscard]] bool PushFirstChild(Path& path);
    void PushLastDescendants(Path& path);
    [[nodiscard]] bool First(Path& path);
    [[nodiscard]] bool Last(Path& path);
    [[nodiscard]] bool Next(Path& path);
    [[nodiscard]] bool Previous(Path& path);
    [[nodiscard]] bool FindRow(Path& path, std::size_t row);
    [[nodiscard]] bool MoveCursorTo(std::size_t row);
    void Rebuild(std::string_view query);

    const kb::scene::Scene* scene_ = nullptr;
    const EditorHierarchyRowBuilder::CollapsedEntitySet* collapsed_ = nullptr;
    EditorHierarchyRowBuilder::CollapsedEntitySet collapsedSeen_;
    std::string query_;
    std::uint64_t sceneId_ = 0U;
    std::uint64_t epoch_ = 0U;
    std::uint64_t structureEpoch_ = 0U;
    bool synced_ = false;
    std::size_t count_ = 0U;
    std::vector<kb::scene::SceneEntity> roots_;
    std::vector<EditorHierarchyRow> searchRows_;
    std::unordered_set<kb::scene::SceneEntity::IdType> transient_;
    // The rows each entity's subtree gives up to collapsed groups and to the transient subtrees met so far.
    std::unordered_map<kb::scene::SceneEntity::IdType, std::size_t> hidden_;
    bool discovered_ = false;
    Path cursor_;
    std::size_t cursorRow_ = 0U;
    bool cursorValid_ = false;
    std::vector<EditorHierarchyRow> window_;
    Work work_;
};

} // namespace kb::editor
