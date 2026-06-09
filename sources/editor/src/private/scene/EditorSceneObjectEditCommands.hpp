#pragma once

#include "commands/IEditorCommand.hpp"
#include "scene/EditorSceneObjectEditTypes.hpp"

#include <span>
#include <string>
#include <vector>

namespace kb::editor {

class EditorSceneContext;

class EditorSceneObjectPayloadBuilder {
public:
    EditorSceneObjectPayloadBuilder() = delete;

    [[nodiscard]] static std::vector<EditorSceneObjectPrefabPayload> Capture(
        EditorSceneContext& context,
        std::span<const kb::scene::SceneEntity> entities);
};

class EditorSceneTransformDeltaCommand final : public IEditorCommand {
public:
    EditorSceneTransformDeltaCommand(
        EditorSceneContext& context,
        std::string label,
        std::vector<EditorSceneObjectTransformChange> changes);

    [[nodiscard]] std::string_view Label() const noexcept override;
    [[nodiscard]] bool Execute() override;
    [[nodiscard]] bool Undo() override;
    [[nodiscard]] bool Redo() override;

private:
    [[nodiscard]] bool Apply(bool after);

    EditorSceneContext& context_;
    std::string label_;
    std::vector<EditorSceneObjectTransformChange> changes_;
};

class EditorScenePrefabSpawnCommand final : public IEditorCommand {
public:
    EditorScenePrefabSpawnCommand(
        EditorSceneContext& context,
        std::string label,
        std::vector<EditorSceneObjectPrefabPayload> payloads);
    EditorScenePrefabSpawnCommand(
        EditorSceneContext& context,
        std::string label,
        std::vector<EditorSceneObjectPrefabPayload> payloads,
        std::vector<kb::scene::SceneEntity> materializedRoots);

    [[nodiscard]] std::string_view Label() const noexcept override;
    [[nodiscard]] bool Execute() override;
    [[nodiscard]] bool Undo() override;
    [[nodiscard]] bool Redo() override;

    [[nodiscard]] const std::vector<kb::scene::SceneEntity>& CreatedEntities() const noexcept;

private:
    [[nodiscard]] bool InstantiatePayloads();
    [[nodiscard]] bool DestroyCreated();
    void NotifyChanged();
    void SelectCreatedOrClear();

    EditorSceneContext& context_;
    std::string label_;
    std::vector<EditorSceneObjectPrefabPayload> payloads_;
    std::vector<kb::scene::SceneEntity> createdEntities_;
    bool materializedOnConstruction_ = false;
};

class EditorScenePrefabRemoveCommand final : public IEditorCommand {
public:
    EditorScenePrefabRemoveCommand(
        EditorSceneContext& context,
        std::string label,
        std::vector<kb::scene::SceneEntity> entities,
        std::vector<EditorSceneObjectPrefabPayload> payloads);

    [[nodiscard]] std::string_view Label() const noexcept override;
    [[nodiscard]] bool Execute() override;
    [[nodiscard]] bool Undo() override;
    [[nodiscard]] bool Redo() override;

private:
    [[nodiscard]] bool DestroyCurrent();
    [[nodiscard]] bool RestorePayloads();
    void NotifyChanged();

    EditorSceneContext& context_;
    std::string label_;
    std::vector<kb::scene::SceneEntity> currentEntities_;
    std::vector<EditorSceneObjectPrefabPayload> payloads_;
};

} // namespace kb::editor
