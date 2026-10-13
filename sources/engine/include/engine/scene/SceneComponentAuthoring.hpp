#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <span>
#include <string>
#include <string_view>

namespace kb::project {
struct ProjectDescriptor;
}

namespace kb::scene {

class Scene;
struct ScenePrefabNodeComponents;

// One entry per scene component an author can add by name - the Inspector's Add Component,
// the hierarchy Create menu and kb_cli all go through this table, so a component means the same
// thing (its defaults, its required companions, its plugin) wherever it is added.
struct SceneComponentKind {
    std::string_view id;           // what the Inspector, kb_cli and automation call it
    std::string_view displayName;  // what the author reads
    std::string_view requiredPlugin; // empty when the component needs no project plugin
};

enum class SceneComponentAddStatus {
    Added,
    AlreadyPresent,
    UnknownComponent,
    PluginDisabled,
    Rejected, // the component store refused the default value
};

struct SceneComponentAddResult {
    SceneComponentAddStatus status = SceneComponentAddStatus::UnknownComponent;
    const SceneComponentKind* kind = nullptr;
};

class SceneComponentAuthoring {
public:
    SceneComponentAuthoring() = delete;

    [[nodiscard]] static std::span<const SceneComponentKind> Kinds() noexcept;
    // The current name for an older one ("3D Radiance Emitter" -> "Light", the first Polish
    // names -> "Line Renderer", "Trail Renderer", "Lens Flare"); other names come back as given.
    // Scenes and scripts written with an older name keep working.
    [[nodiscard]] static std::string_view CanonicalName(std::string_view id) noexcept;
    [[nodiscard]] static const SceneComponentKind* Find(std::string_view id) noexcept;
    [[nodiscard]] static bool Has(const Scene& scene, SceneEntity entity, std::string_view id);
    // Whether a saved scene/prefab node carries the component (no live scene needed).
    [[nodiscard]] static bool InPrefabNode(const ScenePrefabNodeComponents& node, std::string_view id);
    [[nodiscard]] static bool PluginEnabled(const kb::project::ProjectDescriptor& project, const SceneComponentKind& kind);
    // Adds the component with its authoring defaults (and any component it cannot live without).
    [[nodiscard]] static SceneComponentAddResult Add(
        Scene& scene, SceneEntity entity, std::string_view id, const kb::project::ProjectDescriptor& project);
    // Removes only the named component; companions added with it stay.
    [[nodiscard]] static bool Remove(Scene& scene, SceneEntity entity, std::string_view id);
};

} // namespace kb::scene
