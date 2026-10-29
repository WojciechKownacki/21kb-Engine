#pragma once

#include "engine/visual/VisualGraphAuthoringDocument.hpp"
#include "engine/visual/VisualGraphRuntimeExecutor.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/Scene.hpp"

#include <memory>

namespace kb::editor {
struct GameplayGraphEditorState final {
    kb::visual::VisualGraphAuthoringDocument document;
    kb::visual::VisualGraphNodeCatalog catalog;
    kb::visual::VisualGraphNativeBindingRegistry nativeBindings;
    kb::assets::AssetId assetId{};
    std::filesystem::path path;
    std::string error;
    std::vector<kb::visual::VisualGraphDiagnostic> diagnostics;
    std::uint32_t selected = 0;
    std::optional<std::size_t> selectedEdge;
    std::vector<std::uint32_t> breakpoints, visited;
    kb::visual::VisualGraphLifecycleEvent event = kb::visual::VisualGraphLifecycleEvent::Tick;
    float panX = 0, panY = 0, zoom = 1;
    bool dragging = false, openRequested = true;
    float dragX = 0, dragY = 0;
    std::optional<std::pair<std::uint32_t, std::string>> wire;
    bool playing = false, paused = false;
    bool manualPreview = false;
    bool keyboardFocus = false;
    std::uint64_t revision = 1;

    [[nodiscard]] bool Load(kb::scene::Scene& source, kb::assets::AssetId id);
    [[nodiscard]] bool Save(kb::scene::Scene& source);
    [[nodiscard]] bool Validate();
    void Edited();
    [[nodiscard]] bool BuildPreview(kb::scene::Scene& source, const kb::project::ProjectDescriptor& project, kb::scene::SceneEntity owner);
    [[nodiscard]] bool Execute(float delta);
    [[nodiscard]] bool Step();
    void Resume();
    [[nodiscard]] bool ToggleBreakpoint(std::uint32_t node);
    [[nodiscard]] std::string PinValue(std::uint32_t node, std::string_view pin, bool output) const;
    [[nodiscard]] const kb::scene::Scene* PreviewScene() const noexcept { return preview_.get(); }
    [[nodiscard]] kb::scene::SceneEntity PreviewOwner() const noexcept { return owner_; }
    [[nodiscard]] const kb::visual::VisualGraphDebugPauseSnapshot& PauseLocation() const noexcept { return debugger_.LastPause(); }
    void StopPreview();
private:
    std::unique_ptr<kb::scene::Scene> preview_;
    std::unique_ptr<kb::script::ScriptRuntimeHost> scripts_;
    kb::scene::SceneEntity owner_{};
    kb::visual::VisualGraphRuntimeArtifact artifact_;
    kb::visual::VisualGraphRuntimeExecutionContext values_;
    kb::visual::VisualGraphDebugSession debugger_;
};
} // namespace kb::editor
