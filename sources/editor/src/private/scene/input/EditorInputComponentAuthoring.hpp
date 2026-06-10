#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "scene/input/EditorInputAssetCatalog.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace kb::scene {

class Scene;
struct InputComponent;

} // namespace kb::scene

namespace kb::editor {

class EditorConsoleState;

// Adds, removes and edits the InputComponent on scene entities. Mutations run
// through an injected command runner (so they participate in the editor's
// undo/redo) and a selection callback, keeping this class decoupled from the
// owning scene context. Single responsibility: InputComponent authoring.
class EditorInputComponentAuthoring {
public:
    using CommandRunner = std::function<bool(std::string, std::function<bool()>)>;
    using SelectEntityFn = std::function<void(kb::scene::SceneEntity)>;

    EditorInputComponentAuthoring(kb::scene::Scene& scene, EditorConsoleState& console, CommandRunner runCommand, SelectEntityFn selectEntity) noexcept;

    [[nodiscard]] bool Add(kb::scene::SceneEntity entity);
    [[nodiscard]] bool Remove(kb::scene::SceneEntity entity);
    [[nodiscard]] bool ToggleEnabled(kb::scene::SceneEntity entity);
    [[nodiscard]] bool SetPriority(kb::scene::SceneEntity entity, std::int32_t priority);
    [[nodiscard]] bool CycleMappingContext(kb::scene::SceneEntity entity);

private:
    [[nodiscard]] bool ApplyEdit(kb::scene::SceneEntity entity, std::string label, const std::function<void(kb::scene::InputComponent&)>& edit);

    kb::scene::Scene& scene_;
    EditorConsoleState& console_;
    EditorInputAssetCatalog catalog_;
    CommandRunner runCommand_;
    SelectEntityFn selectEntity_;
};

} // namespace kb::editor
