#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/ui/UIComponentCatalog.hpp"

#include <string_view>

namespace kb::scene {
class Scene;
}

namespace kb::editor {

class EditorUIComponentAuthoring final {
public:
    EditorUIComponentAuthoring() = delete;

    [[nodiscard]] static bool Supports(std::string_view id) noexcept;
    [[nodiscard]] static bool Has(const kb::scene::Scene& scene, kb::scene::SceneEntity entity,
        kb::scene::UIComponentType type) noexcept;
    [[nodiscard]] static bool Add(kb::scene::Scene& scene, kb::scene::SceneEntity entity,
        std::string_view id);
    [[nodiscard]] static bool Remove(kb::scene::Scene& scene, kb::scene::SceneEntity entity,
        kb::scene::UIComponentType type) noexcept;
    [[nodiscard]] static bool Complete(kb::scene::Scene& scene, kb::scene::SceneEntity entity);
};

[[nodiscard]] constexpr std::string_view EditorUIPresetPrefix() noexcept { return "preset:"; }

} // namespace kb::editor
