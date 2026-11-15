#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace kb::scene {

class Scene;

class SceneTagCatalogQueries {
public:
    explicit SceneTagCatalogQueries(const Scene& scene) noexcept;

    [[nodiscard]] std::span<const std::string> Names() const noexcept;
    [[nodiscard]] bool Contains(std::string_view name) const noexcept;
    [[nodiscard]] bool IsAssigned(SceneEntity entity, std::string_view name) const noexcept;

private:
    const Scene& scene_;
};

// Scene-owned tag definitions and entity assignment operations. The catalog is
// the sole authority for author-facing tags; editor UI and script APIs both use
// this surface rather than inferring a list from component text.
class SceneTagCatalog {
public:
    static constexpr std::size_t MaxDefinitions = 256U;
    static constexpr std::array<std::string_view, 6U> DefaultNames{
        "Player", "Enemy", "Monster", "AI", "NPC", "Collision"
    };

    [[nodiscard]] static constexpr bool IsBuiltIn(std::string_view name) noexcept {
        for (const std::string_view builtIn : DefaultNames) {
            if (name == builtIn) {
                return true;
            }
        }
        return false;
    }

    // The name as the catalogue keeps it (trimmed), or an empty string when it is not a valid tag name.
    [[nodiscard]] static std::string CanonicalName(std::string_view name);

    explicit SceneTagCatalog(Scene& scene) noexcept;

    [[nodiscard]] std::span<const std::string> Names() const noexcept;
    [[nodiscard]] bool Contains(std::string_view name) const noexcept;
    [[nodiscard]] bool Define(std::string_view name);
    // Removes a custom definition and every matching assignment in this scene.
    // Built-in definitions are immutable and cannot be removed.
    [[nodiscard]] bool Undefine(std::string_view name);
    // Removes the definitions only this scene has (not built in, not the project's) that no object carries, such as a
    // tag the project dropped while the scene was closed. Returns how many it removed.
    std::size_t UndefineUnused();
    // Replaces the scene's definitions with the built-in ones, the project's (see SetProjectDefinitions) and
    // then `names`. Built-in and project names in `names` are not repeated.
    [[nodiscard]] bool ReplaceDefinitions(std::span<const std::string> names);
    // The tags the project defines, which the host passes on to every scene it builds. They are defined in this
    // scene at once, ahead of any tag only the scene has, and ReplaceDefinitions keeps them. A name that is not
    // a valid tag, or a list the catalogue cannot hold, changes nothing and returns false; a built-in or repeated
    // name is skipped.
    [[nodiscard]] bool SetProjectDefinitions(std::span<const std::string> names);
    [[nodiscard]] std::span<const std::string> ProjectDefinitions() const noexcept;
    void ResetToDefaults();

    // An entity has at most one author-facing tag. Assigning a new one replaces
    // its previous classification.
    [[nodiscard]] bool SetAssigned(SceneEntity entity, std::string_view name, bool assigned);
    [[nodiscard]] bool ClearAssignments(SceneEntity entity);
    // Imports legacy/raw component data into the runtime catalogue. Component
    // loading calls this boundary; editor code never infers tag definitions.
    void RegisterAssignedTags(SceneEntity entity);
    [[nodiscard]] bool IsAssigned(SceneEntity entity, std::string_view name) const noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
