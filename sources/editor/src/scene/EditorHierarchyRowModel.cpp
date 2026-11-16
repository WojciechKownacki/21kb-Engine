#include "scene/EditorHierarchyRowModel.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "scene/EditorHierarchyRowFactory.hpp"

#include <algorithm>
#include <utility>

namespace kb::editor {
namespace {

// Rows the cursor walks to reach a row; farther rows are looked up by the subtree sizes.
constexpr std::size_t kWalkRows = 4096U;

} // namespace

EditorHierarchyRowModel::Work EditorHierarchyRowModel::TakeWork() noexcept {
    return std::exchange(work_, Work{});
}

void EditorHierarchyRowModel::Sync(const kb::scene::Scene& scene,
    const EditorHierarchyRowBuilder::CollapsedEntitySet& collapsed, std::string_view query, bool dirty) {
    const kb::scene::SceneHierarchyQueries hierarchy = scene.Hierarchy();
    const bool sameScene = synced_ && scene.Id() == sceneId_;
    scene_ = &scene;
    collapsed_ = &collapsed;
    // Search rows carry names, so a rename or a root appended at the end changes them too.
    const bool searchChanged = !query_.empty() &&
        (hierarchy.RootAppendEpoch() != epoch_ || hierarchy.RootCount() != roots_.size());
    if (dirty || !sameScene || query != query_ || collapsed != collapsedSeen_ ||
        hierarchy.StructureEpoch() != structureEpoch_ || searchChanged) {
        Rebuild(query);
        return;
    }
    for (std::size_t index = roots_.size(); index < hierarchy.RootCount(); ++index) {
        AppendRoot(hierarchy.RootAt(index));
    }
}

void EditorHierarchyRowModel::Rebuild(std::string_view query) {
    const kb::scene::SceneHierarchyQueries hierarchy = scene_->Hierarchy();
    sceneId_ = scene_->Id();
    query_ = query;
    collapsedSeen_ = *collapsed_;
    epoch_ = hierarchy.RootAppendEpoch();
    structureEpoch_ = hierarchy.StructureEpoch();
    synced_ = true;
    cursorValid_ = false;
    transient_.clear();
    hidden_.clear();
    window_.clear();
    searchRows_.clear();
    roots_ = hierarchy.RootEntities();
    if (!query_.empty()) {
        searchRows_ = EditorHierarchyRowBuilder::Build(*scene_, *collapsed_, query_);
        count_ = searchRows_.size();
        return;
    }
    count_ = CountRows();
}

void EditorHierarchyRowModel::AppendRoot(kb::scene::SceneEntity root) {
    roots_.push_back(root);
    window_.clear();
    if (IsTransient(root)) {
        transient_.insert(root.Id());
        return;
    }
    count_ += SubtreeRows(root);
}

std::size_t EditorHierarchyRowModel::Count() const noexcept {
    return count_;
}

std::size_t EditorHierarchyRowModel::SiblingCount(kb::scene::SceneEntity parent) const {
    return parent.IsValid() ? scene_->Hierarchy().ChildCount(parent) : roots_.size();
}

kb::scene::SceneEntity EditorHierarchyRowModel::SiblingAt(kb::scene::SceneEntity parent, std::size_t index) const {
    return parent.IsValid() ? scene_->Hierarchy().ChildAt(parent, index) : roots_[index];
}

bool EditorHierarchyRowModel::Expanded(kb::scene::SceneEntity entity) const {
    return scene_->Hierarchy().ChildCount(entity) != 0U && !collapsed_->contains(entity.Id());
}

bool EditorHierarchyRowModel::IsTransient(kb::scene::SceneEntity entity) const {
    return EditorHierarchyRowBuilder::IsTransient(*scene_, entity);
}

// A transient subtree is not listed; the first walk that meets it takes its rows out of the count.
bool EditorHierarchyRowModel::Listed(kb::scene::SceneEntity entity) {
    ++work_.visited;
    if (!transient_.empty() && transient_.contains(entity.Id())) {
        return false;
    }
    if (!IsTransient(entity)) {
        return true;
    }
    transient_.insert(entity.Id());
    const std::size_t rows = SubtreeRows(entity);
    count_ -= std::min(count_, rows);
    Hide(entity, rows);
    discovered_ = true;
    return false;
}

// The rows the entity and its expanded descendants take, whatever their transient state: the descendants the scene
// counts, less the rows hidden inside.
std::size_t EditorHierarchyRowModel::SubtreeRows(kb::scene::SceneEntity entity) const {
    if (!Expanded(entity)) {
        return 1U;
    }
    const std::size_t below = scene_->Hierarchy().DescendantCount(entity);
    const auto hidden = hidden_.empty() ? hidden_.end() : hidden_.find(entity.Id());
    return 1U + below - (hidden == hidden_.end() ? 0U : std::min(below, hidden->second));
}

std::size_t EditorHierarchyRowModel::SkipRows(kb::scene::SceneEntity entity) const {
    return !transient_.empty() && transient_.contains(entity.Id()) ? 0U : SubtreeRows(entity);
}

// Takes rows out of the subtree of every entity above `entity`.
void EditorHierarchyRowModel::Hide(kb::scene::SceneEntity entity, std::size_t rows) {
    const kb::scene::SceneHierarchyQueries hierarchy = scene_->Hierarchy();
    for (kb::scene::SceneEntity parent = hierarchy.Parent(entity); parent.IsValid(); parent = hierarchy.Parent(parent)) {
        hidden_[parent.Id()] += rows;
    }
}

// Every entity is a row but those under a collapsed entity: the entities less the descendants of the collapsed ones
// that no other collapsed entity above hides.
std::size_t EditorHierarchyRowModel::CountRows() {
    const kb::scene::SceneHierarchyQueries hierarchy = scene_->Hierarchy();
    std::size_t hidden = 0U;
    for (const kb::scene::SceneEntity::IdType id : *collapsed_) {
        const kb::scene::SceneEntity entity{ id };
        if (!scene_->Entities().IsAlive(entity) || hierarchy.ChildCount(entity) == 0U) {
            continue;
        }
        bool covered = false;
        for (kb::scene::SceneEntity parent = hierarchy.Parent(entity); parent.IsValid() && !covered; parent = hierarchy.Parent(parent)) {
            covered = collapsed_->contains(parent.Id());
        }
        if (!covered) {
            const std::size_t below = hierarchy.DescendantCount(entity);
            hidden += below;
            Hide(entity, below);
        }
    }
    const std::size_t total = scene_->Entities().Count();
    return total - std::min(total, hidden);
}

// Pushes the first listed child of the row the path ends at.
bool EditorHierarchyRowModel::PushFirstChild(Path& path) {
    const kb::scene::SceneEntity parent = path.back().entity;
    const std::size_t count = SiblingCount(parent);
    for (std::size_t index = 0U; index < count; ++index) {
        const kb::scene::SceneEntity child = SiblingAt(parent, index);
        if (Listed(child)) {
            path.push_back(Frame{ child, index });
            return true;
        }
    }
    return false;
}

// Moves the path to the last row of the subtree it ends at.
void EditorHierarchyRowModel::PushLastDescendants(Path& path) {
    while (Expanded(path.back().entity)) {
        const kb::scene::SceneEntity parent = path.back().entity;
        bool pushed = false;
        for (std::size_t index = SiblingCount(parent); index-- > 0U && !pushed;) {
            const kb::scene::SceneEntity child = SiblingAt(parent, index);
            if (Listed(child)) {
                path.push_back(Frame{ child, index });
                pushed = true;
            }
        }
        if (!pushed) {
            return;
        }
    }
}

bool EditorHierarchyRowModel::First(Path& path) {
    path.clear();
    for (std::size_t index = 0U; index < roots_.size(); ++index) {
        if (Listed(roots_[index])) {
            path.push_back(Frame{ roots_[index], index });
            return true;
        }
    }
    return false;
}

bool EditorHierarchyRowModel::Last(Path& path) {
    path.clear();
    for (std::size_t index = roots_.size(); index-- > 0U;) {
        if (Listed(roots_[index])) {
            path.push_back(Frame{ roots_[index], index });
            PushLastDescendants(path);
            return true;
        }
    }
    return false;
}

bool EditorHierarchyRowModel::Next(Path& path) {
    if (Expanded(path.back().entity) && PushFirstChild(path)) {
        return true;
    }
    for (std::size_t depth = path.size(); depth > 0U; --depth) {
        const kb::scene::SceneEntity parent = depth >= 2U ? path[depth - 2U].entity : kb::scene::SceneEntity{};
        const std::size_t count = SiblingCount(parent);
        for (std::size_t index = path[depth - 1U].index + 1U; index < count; ++index) {
            const kb::scene::SceneEntity sibling = SiblingAt(parent, index);
            if (Listed(sibling)) {
                path.resize(depth);
                path.back() = Frame{ sibling, index };
                return true;
            }
        }
    }
    return false;
}

bool EditorHierarchyRowModel::Previous(Path& path) {
    const kb::scene::SceneEntity parent = path.size() >= 2U ? path[path.size() - 2U].entity : kb::scene::SceneEntity{};
    for (std::size_t index = path.back().index; index-- > 0U;) {
        const kb::scene::SceneEntity sibling = SiblingAt(parent, index);
        if (Listed(sibling)) {
            path.back() = Frame{ sibling, index };
            PushLastDescendants(path);
            return true;
        }
    }
    if (path.size() < 2U) {
        return false;
    }
    path.pop_back();
    return true;
}

// Puts the path on a row by stepping over the subtrees before it by their sizes: only the entities on the way down
// are visited. When every entity of a list is a row of its own, the row is the place in the list.
bool EditorHierarchyRowModel::FindRow(Path& path, std::size_t row) {
    path.clear();
    kb::scene::SceneEntity parent{};
    std::size_t listRows = count_;
    for (;;) {
        const std::size_t count = SiblingCount(parent);
        std::size_t index = 0U;
        if (listRows == count && transient_.empty()) {
            index = row;
            row = 0U;
        }
        for (; index < count; ++index) {
            const std::size_t rows = SkipRows(SiblingAt(parent, index));
            if (row < rows) {
                break;
            }
            row -= rows;
            ++work_.skipped;
        }
        if (index >= count) {
            return false;
        }
        const kb::scene::SceneEntity sibling = SiblingAt(parent, index);
        if (!Listed(sibling)) {
            return false;
        }
        path.push_back(Frame{ sibling, index });
        if (row == 0U) {
            return true;
        }
        --row;
        if (!Expanded(sibling)) {
            return false;
        }
        parent = sibling;
        listRows = SubtreeRows(sibling) - 1U;
    }
}

// Puts the cursor on a row: by walking from the nearest of the first row, the last row and where it stood, or, when
// those are far, by looking the row up.
bool EditorHierarchyRowModel::MoveCursorTo(std::size_t row) {
    for (;;) {
        if (row >= count_) {
            return false;
        }
        discovered_ = false;
        if (!cursorValid_) {
            if (!First(cursor_)) {
                count_ = 0U;
                return false;
            }
            cursorRow_ = 0U;
            cursorValid_ = true;
        }
        const std::size_t fromCursor = row > cursorRow_ ? row - cursorRow_ : cursorRow_ - row;
        const std::size_t fromEnd = count_ - 1U - row;
        bool reached = true;
        Path found;
        if (std::min({ row, fromCursor, fromEnd }) > kWalkRows && FindRow(found, row)) {
            cursor_ = std::move(found);
            cursorRow_ = row;
        } else if (!discovered_) {
            Path jump;
            if (fromEnd < fromCursor && fromEnd < row && Last(jump)) {
                cursor_ = std::move(jump);
                cursorRow_ = count_ - 1U;
            } else if (row < fromCursor && First(jump)) {
                cursor_ = std::move(jump);
                cursorRow_ = 0U;
            }
            while (reached && cursorRow_ < row) {
                reached = Next(cursor_);
                cursorRow_ += reached ? 1U : 0U;
            }
            while (reached && cursorRow_ > row) {
                reached = Previous(cursor_);
                cursorRow_ -= reached ? 1U : 0U;
            }
        }
        if (discovered_) {
            cursorValid_ = false;
            continue;
        }
        if (!reached) {
            count_ = cursorRow_ + 1U;
        }
        return reached;
    }
}

const std::vector<EditorHierarchyRow>& EditorHierarchyRowModel::Window(std::size_t first, std::size_t count) {
    window_.clear();
    if (!query_.empty()) {
        const std::size_t begin = std::min(first, searchRows_.size());
        const std::size_t end = std::min(searchRows_.size(), begin + count);
        window_.assign(searchRows_.begin() + static_cast<std::ptrdiff_t>(begin), searchRows_.begin() + static_cast<std::ptrdiff_t>(end));
        return window_;
    }
    if (count == 0U || !MoveCursorTo(first)) {
        return window_;
    }
    Path path = cursor_;
    discovered_ = false;
    for (;;) {
        window_.push_back(EditorHierarchyRowFactory::Make(*scene_, *collapsed_, path.back().entity,
            static_cast<std::uint32_t>(path.size() - 1U)));
        if (window_.size() >= count || !Next(path)) {
            break;
        }
    }
    return discovered_ ? Window(first, count) : window_;
}

const EditorHierarchyRow* EditorHierarchyRowModel::RowAt(std::size_t index) {
    const std::vector<EditorHierarchyRow>& rows = Window(index, 1U);
    return rows.empty() ? nullptr : &rows.front();
}

bool EditorHierarchyRowModel::IsListed(kb::scene::SceneEntity entity) const {
    if (!query_.empty()) {
        return std::ranges::any_of(searchRows_, [entity](const EditorHierarchyRow& row) { return row.entity == entity; });
    }
    if (!scene_->Entities().IsAlive(entity)) {
        return false;
    }
    const kb::scene::SceneHierarchyQueries hierarchy = scene_->Hierarchy();
    for (kb::scene::SceneEntity node = entity; node.IsValid(); node = hierarchy.Parent(node)) {
        if (IsTransient(node) || (node != entity && !Expanded(node))) {
            return false;
        }
    }
    return true;
}

// Looks outward from the hint in both directions, so the cost follows the distance and not the list length.
std::size_t EditorHierarchyRowModel::IndexOfNear(kb::scene::SceneEntity entity, std::size_t hint) {
    if (!query_.empty()) {
        const auto found = std::ranges::find_if(searchRows_, [entity](const EditorHierarchyRow& row) { return row.entity == entity; });
        return found == searchRows_.end() ? kNotListed : static_cast<std::size_t>(found - searchRows_.begin());
    }
    if (count_ == 0U || !IsListed(entity) || !MoveCursorTo(std::min(hint, count_ - 1U))) {
        return kNotListed;
    }
    Path forward = cursor_;
    Path backward = cursor_;
    std::size_t up = cursorRow_;
    std::size_t down = cursorRow_;
    bool forwardLive = true;
    bool backwardLive = true;
    while (forwardLive || backwardLive) {
        if (forwardLive && forward.back().entity == entity) {
            return up;
        }
        if (backwardLive && backward.back().entity == entity) {
            return down;
        }
        forwardLive = forwardLive && Next(forward);
        up += forwardLive ? 1U : 0U;
        backwardLive = backwardLive && Previous(backward);
        down -= backwardLive ? 1U : 0U;
    }
    return kNotListed;
}

void EditorHierarchyRowModel::ForEachEntity(const std::function<void(kb::scene::SceneEntity)>& visit) {
    if (!query_.empty()) {
        for (const EditorHierarchyRow& row : searchRows_) {
            visit(row.entity);
        }
        return;
    }
    Path path;
    if (!First(path)) {
        return;
    }
    do {
        visit(path.back().entity);
    } while (Next(path));
}

} // namespace kb::editor
