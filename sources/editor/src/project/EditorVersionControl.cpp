#include "project/EditorVersionControl.hpp"

#if defined(_WIN32)
#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace kb::editor {
namespace {

// How often the changes are read again while nothing asks for it sooner.
constexpr std::chrono::seconds kRefreshInterval{ 10 };

[[nodiscard]] std::string Key(const std::filesystem::path& path) {
    const std::u8string text = path.lexically_normal().generic_u8string();
    std::string key{ reinterpret_cast<const char*>(text.data()), text.size() };
    while (key.size() > 1U && key.back() == '/') {
        key.pop_back();
    }
    std::ranges::transform(key, key.begin(), [](char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return key;
}

template <typename Result>
[[nodiscard]] bool Finished(std::future<Result>& job) {
    return job.valid() && job.wait_for(std::chrono::seconds{ 0 }) == std::future_status::ready;
}

} // namespace

EditorVersionControl& EditorVersionControl::Instance() {
    // Never destroyed: a background reading may still be running when the process ends.
    static EditorVersionControl& instance = *new EditorVersionControl();
    return instance;
}

bool EditorVersionControl::Pump(const std::filesystem::path& projectRoot, std::chrono::steady_clock::time_point now) {
    bool changed = false;
    if (projectRoot != root_) {
        // Another project: nothing read so far belongs to it.
        root_ = projectRoot;
        Adopt({});
        known_ = false;
        refreshRequested_ = true;
        ClearComparison();
        changed = true;
    }
    if (Finished(reading_)) {
        EditorChangeList fresh = reading_.get();
        const auto sameChange = [](const EditorFileChange& left, const EditorFileChange& right) {
            return left.path == right.path && left.state == right.state;
        };
        const bool same = known_ && fresh.available == changes_.available && fresh.branch == changes_.branch &&
            std::ranges::equal(fresh.changes, changes_.changes, sameChange);
        if (!same) {
            Adopt(std::move(fresh));
            changed = true;
        }
        known_ = true;
    }
    if (Finished(comparison_)) {
        const SavedScene pair = comparison_.get();
        result_ = pair.saved.succeeded ? EditorSceneComparer::Compare(pair.saved.document, pair.current)
                                       : EditorSceneComparison{ .error = pair.saved.error };
        hasComparison_ = true;
        selectedObject_ = kNone;
        sceneScroll = 0;
        changed = true;
    }
    if (!reading_.valid() && !root_.empty() && (refreshRequested_ || now - lastRefresh_ >= kRefreshInterval)) {
        refreshRequested_ = false;
        lastRefresh_ = now;
        reading_ = std::async(std::launch::async, [root = root_]() { return EditorProjectRepository::Changes(root); });
    }
    return changed;
}

void EditorVersionControl::Adopt(EditorChangeList list) {
    // The user's choice stays on the same file when the list is read again.
    const std::string chosen = selected_ < changes_.changes.size() ? changes_.changes[selected_].path : std::string{};
    changes_ = std::move(list);
    selected_ = kNone;
    byPath_.clear();
    folders_.clear();
    for (std::size_t index = 0U; index < changes_.changes.size(); ++index) {
        const EditorFileChange& change = changes_.changes[index];
        const std::filesystem::path full = changes_.root / std::filesystem::path{ ScriptEditorTextEncoding::Widen(change.path) };
        byPath_[Key(full)] = change.state;
        for (std::filesystem::path folder = full.parent_path(); folder.has_parent_path() && folder != folder.parent_path(); folder = folder.parent_path()) {
            if (!folders_.insert(Key(folder)).second) {
                break;
            }
        }
        if (!chosen.empty() && change.path == chosen) {
            selected_ = index;
        }
    }
}

const EditorChangeState* EditorVersionControl::StateOf(const std::filesystem::path& path) const {
    const auto found = byPath_.find(Key(path));
    return found == byPath_.end() ? nullptr : &found->second;
}

bool EditorVersionControl::FolderHasChanges(const std::filesystem::path& path) const {
    return folders_.contains(Key(path));
}

void EditorVersionControl::CompareScene(const std::filesystem::path& projectRoot, std::string repositoryPath, kb::scene::SceneDocument current) {
    if (comparison_.valid()) {
        return;
    }
    hasComparison_ = false;
    result_ = {};
    selectedObject_ = kNone;
    comparison_ = std::async(std::launch::async,
        [projectRoot, repositoryPath = std::move(repositoryPath), current = std::move(current)]() mutable {
            return SavedScene{ EditorSceneComparer::LoadSaved(projectRoot, repositoryPath), std::move(current) };
        });
}

void EditorVersionControl::ClearComparison() noexcept {
    hasComparison_ = false;
    result_ = {};
    selectedObject_ = kNone;
    sceneScroll = 0;
}

} // namespace kb::editor

#endif
