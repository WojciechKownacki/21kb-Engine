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

// Entities a subtraction may visit before the visible rows are counted instead.
constexpr std::size_t kHiddenWorkBudget = 65536U;

} // namespace

void EditorHierarchyRowModel::Sync(const kb::scene::Scene& scene,
    const EditorHierarchyRowBuilder::CollapsedEntitySet& collapsed, std::string_view query, bool dirty) {
    scene_ = &scene;
    collapsed_ = &collapsed;
    if (dirty || !synced_ || query != query_ || scene.Hierarchy().RootAppendEpoch() != epoch_) {
        Rebuild(query);
    }
}

void EditorHierarchyRowModel::Rebuild(std::string_view query) {
    query_ = query;
    epoch_ = scene_->Hierarchy().RootAppendEpoch();
    synced_ = true;
    cursorValid_ = false;
    transient_.clear();
    window_.clear();
    searchRows_.clear();
    roots_.clear();
    if (!query_.empty()) {
        searchRows_ = EditorHierarchyRowBuilder::Build(*scene_, *collapsed_, query_);
        count_ = searchRows_.size();
        return;
    }
    roots_ = scene_->Hierarchy().RootEntities();
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
    if (transient_.contains(entity.Id())) {
        return false;
    }
    if (!IsTransient(entity)) {
        return true;
    }
    transient_.insert(entity.Id());
    if (!counting_) {
        count_ -= std::min(count_, SubtreeRows(entity));
        discovered_ = true;
    }
    return false;
}

std::size_t EditorHierarchyRowModel::SubtreeSize(kb::scene::SceneEntity entity, std::size_t& work) const {
    const kb::scene::SceneHierarchyQueries hierarchy = scene_->Hierarchy();
    std::size_t size = 1U;
    Path path{ Frame{ entity, 0U } };
    while (!path.empty()) {
        Frame& frame = path.back();
        if (frame.index >= hierarchy.ChildCount(frame.entity)) {
            path.pop_back();
            continue;
        }
        const kb::scene::SceneEntity child = hierarchy.ChildAt(frame.entity, frame.index++);
        ++size;
        ++work;
        if (hierarchy.ChildCount(child) != 0U) {
            path.push_back(Frame{ child, 0U });
        }
    }
    return size;
}

// The rows the entity and its expanded descendants take, whatever their transient state.
std::size_t EditorHierarchyRowModel::SubtreeRows(kb::scene::SceneEntity entity) const {
    const kb::scene::SceneHierarchyQueries hierarchy = scene_->Hierarchy();
    std::size_t rows = 1U;
    if (!Expanded(entity)) {
        return rows;
    }
    Path path{ Frame{ entity, 0U } };
    while (!path.empty()) {
        Frame& frame = path.back();
        if (frame.index >= hierarchy.ChildCount(frame.entity)) {
            path.pop_back();
            continue;
        }
        const kb::scene::SceneEntity child = hierarchy.ChildAt(frame.entity, frame.index++);
        ++rows;
        if (Expanded(child)) {
            path.push_back(Frame{ child, 0U });
        }
    }
    return rows;
}

// Every entity is a row but those under a collapsed entity, so the rows are the entities less the hidden ones. When
// the collapsed subtrees are large, walking the few visible rows is cheaper.
std::size_t EditorHierarchyRowModel::CountRows() {
    const kb::scene::SceneHierarchyQueries hierarchy = scene_->Hierarchy();
    std::size_t hidden = 0U;
    std::size_t work = 0U;
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
            hidden += SubtreeSize(entity, work) - 1U;
        }
        if (work > kHiddenWorkBudget) {
            break;
        }
    }
    if (work <= kHiddenWorkBudget) {
        const std::size_t total = scene_->Entities().Count();
        return total - std::min(total, hidden);
    }

    counting_ = true;
    std::size_t rows = 0U;
    Path path;
    if (First(path)) {
        rows = 1U;
        while (Next(path)) {
            ++rows;
        }
    }
    counting_ = false;
    return rows;
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

// Puts the cursor on a row, walking from the nearest of the first row, the last row and where it stood.
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
        Path jump;
        if (fromEnd < fromCursor && fromEnd < row && Last(jump)) {
            cursor_ = std::move(jump);
            cursorRow_ = count_ - 1U;
        } else if (row < fromCursor && First(jump)) {
            cursor_ = std::move(jump);
            cursorRow_ = 0U;
        }
        bool reached = true;
        while (reached && cursorRow_ < row) {
            reached = Next(cursor_);
            cursorRow_ += reached ? 1U : 0U;
        }
        while (reached && cursorRow_ > row) {
            reached = Previous(cursor_);
            cursorRow_ -= reached ? 1U : 0U;
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
