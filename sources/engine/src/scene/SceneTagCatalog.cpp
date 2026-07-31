#include "engine/scene/SceneTagCatalog.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/TagsComponent.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace kb::scene {
namespace {

[[nodiscard]] std::string Normalize(std::string_view value) {
    std::size_t first = 0U;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1U])) != 0) {
        --last;
    }
    return std::string{ value.substr(first, last - first) };
}

[[nodiscard]] bool IsValidName(std::string_view name) noexcept {
    return !name.empty() && name.find_first_of(",;") == std::string_view::npos && TagsTextIsValid(name);
}

[[nodiscard]] std::vector<std::string> Parse(std::string_view text) {
    std::vector<std::string> tags;
    std::size_t begin = 0U;
    while (begin <= text.size()) {
        const std::size_t end = text.find_first_of(",;", begin);
        const std::string tag = Normalize(text.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin));
        if (!tag.empty() && std::find(tags.begin(), tags.end(), tag) == tags.end()) {
            tags.push_back(tag);
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
    }
    return tags;
}

[[nodiscard]] std::string Join(const std::vector<std::string>& tags) {
    std::string joined;
    for (const std::string& tag : tags) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += tag;
    }
    return joined;
}

[[nodiscard]] std::vector<std::string>& Definitions(Scene& scene) noexcept {
    return SceneAccess::State(scene).tagDefinitions;
}

[[nodiscard]] const std::vector<std::string>& Definitions(const Scene& scene) noexcept {
    return SceneAccess::State(scene).tagDefinitions;
}

void RemoveAssignment(Scene& scene, SceneEntity entity, std::string_view name) {
    SceneTagsComponents components = scene.Components().Tags();
    const TagsComponent* current = components.TryGet(entity);
    if (current == nullptr) {
        return;
    }
    std::vector<std::string> tags = Parse(TagsText(*current));
    const auto found = std::find(tags.begin(), tags.end(), name);
    if (found == tags.end()) {
        return;
    }
    tags.erase(found);
    if (tags.empty()) {
        components.Remove(entity);
        return;
    }
    TagsComponent updated;
    SetTagsText(updated, Join(tags));
    components.Set(entity, updated);
}

struct RemoveAssignmentContext {
    Scene* scene = nullptr;
    std::string_view name;
};

void RemoveAssignmentVisitor(SceneEntity entity, const TransformComponent&, void* raw) {
    auto* context = static_cast<RemoveAssignmentContext*>(raw);
    if (context != nullptr && context->scene != nullptr) {
        RemoveAssignment(*context->scene, entity, context->name);
    }
}

} // namespace

SceneTagCatalogQueries::SceneTagCatalogQueries(const Scene& scene) noexcept
    : scene_(scene) {}

std::span<const std::string> SceneTagCatalogQueries::Names() const noexcept {
    return Definitions(scene_);
}

bool SceneTagCatalogQueries::Contains(std::string_view name) const noexcept {
    const std::string normalized = Normalize(name);
    const std::vector<std::string>& definitions = Definitions(scene_);
    return std::find(definitions.begin(), definitions.end(), normalized) != definitions.end();
}

bool SceneTagCatalogQueries::IsAssigned(SceneEntity entity, std::string_view name) const noexcept {
    const std::string normalized = Normalize(name);
    const TagsComponent* tags = scene_.Components().Tags().TryGet(entity);
    if (tags == nullptr) {
        return false;
    }
    const std::vector<std::string> assigned = Parse(TagsText(*tags));
    return std::find(assigned.begin(), assigned.end(), normalized) != assigned.end();
}

SceneTagCatalog::SceneTagCatalog(Scene& scene) noexcept
    : scene_(scene) {}

std::span<const std::string> SceneTagCatalog::Names() const noexcept {
    return Definitions(scene_);
}

bool SceneTagCatalog::Contains(std::string_view name) const noexcept {
    return SceneTagCatalogQueries{ scene_ }.Contains(name);
}

bool SceneTagCatalog::Define(std::string_view name) {
    const std::string normalized = Normalize(name);
    std::vector<std::string>& definitions = Definitions(scene_);
    if (!IsValidName(normalized)) {
        return false;
    }
    if (std::find(definitions.begin(), definitions.end(), normalized) != definitions.end()) {
        return true;
    }
    if (definitions.size() >= MaxDefinitions) {
        return false;
    }
    definitions.push_back(normalized);
    return true;
}

bool SceneTagCatalog::Undefine(std::string_view name) {
    const std::string normalized = Normalize(name);
    if (IsBuiltIn(normalized)) {
        return false;
    }
    std::vector<std::string>& definitions = Definitions(scene_);
    const auto found = std::find(definitions.begin(), definitions.end(), normalized);
    if (found == definitions.end()) {
        return false;
    }
    definitions.erase(found);
    RemoveAssignmentContext context{ .scene = &scene_, .name = normalized };
    scene_.Transforms().ForEach(&RemoveAssignmentVisitor, &context);
    return true;
}

bool SceneTagCatalog::ReplaceDefinitions(std::span<const std::string> names) {
    if (names.size() > MaxDefinitions) {
        return false;
    }
    std::vector<std::string> replacement;
    replacement.reserve(std::min(MaxDefinitions, names.size() + DefaultNames.size()));
    for (const std::string_view builtIn : DefaultNames) {
        replacement.emplace_back(builtIn);
    }
    std::vector<std::string> provided;
    provided.reserve(names.size());
    for (const std::string& name : names) {
        const std::string normalized = Normalize(name);
        if (!IsValidName(normalized) || std::find(provided.begin(), provided.end(), normalized) != provided.end()) {
            return false;
        }
        provided.push_back(normalized);
        if (IsBuiltIn(normalized)) {
            continue;
        }
        if (replacement.size() >= MaxDefinitions) {
            return false;
        }
        replacement.push_back(normalized);
    }
    Definitions(scene_) = std::move(replacement);
    return true;
}

void SceneTagCatalog::ResetToDefaults() {
    std::vector<std::string>& definitions = Definitions(scene_);
    definitions.clear();
    definitions.reserve(DefaultNames.size());
    for (const std::string_view name : DefaultNames) {
        definitions.emplace_back(name);
    }
}

bool SceneTagCatalog::SetAssigned(SceneEntity entity, std::string_view name, bool assigned) {
    if (!entity.IsValid() || !scene_.Entities().IsAlive(entity)) {
        return false;
    }
    const std::string normalized = Normalize(name);
    if (!IsValidName(normalized) || (assigned && !Define(normalized))) {
        return false;
    }
    SceneTagsComponents components = scene_.Components().Tags();
    const TagsComponent* current = components.TryGet(entity);
    if (assigned) {
        // Object classification is deliberately single-select. Assigning a
        // different tag replaces the previous classification atomically.
        if (current != nullptr && TagsText(*current) == normalized) {
            return true;
        }
        TagsComponent updated;
        SetTagsText(updated, normalized);
        components.Set(entity, updated);
        return true;
    }

    if (current == nullptr) {
        return true;
    }
    const std::vector<std::string> currentTags = Parse(TagsText(*current));
    if (std::find(currentTags.begin(), currentTags.end(), normalized) == currentTags.end()) {
        return true;
    }
    components.Remove(entity);
    return true;
}

bool SceneTagCatalog::ClearAssignments(SceneEntity entity) {
    if (!entity.IsValid() || !scene_.Entities().IsAlive(entity)) {
        return false;
    }
    scene_.Components().Tags().Remove(entity);
    return true;
}

void SceneTagCatalog::RegisterAssignedTags(SceneEntity entity) {
    SceneTagsComponents components = scene_.Components().Tags();
    TagsComponent* tags = components.TryGet(entity);
    if (tags == nullptr) {
        return;
    }
    const std::vector<std::string> parsed = Parse(TagsText(*tags));
    if (parsed.empty()) {
        return;
    }
    // Older assets and low-level component writers could contain comma-separated
    // values. Migrate them deterministically to the first classification so the
    // single-select contract also holds after load/prefab instantiation.
    if (!Define(parsed.front())) {
        components.Remove(entity);
        return;
    }
    SetTagsText(*tags, parsed.front());
}

bool SceneTagCatalog::IsAssigned(SceneEntity entity, std::string_view name) const noexcept {
    return SceneTagCatalogQueries{ scene_ }.IsAssigned(entity, name);
}

} // namespace kb::scene
