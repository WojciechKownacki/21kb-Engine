#pragma once

#include "commands/IEditorCommand.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/ui/UITheme.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace kb::scene { class Scene; }
namespace kb::editor {

class EditorAssetBrowserState;

// Shared-theme document operations used by Inspector and headless authoring.
class EditorUIThemeAssets final {
public:
    [[nodiscard]] static std::filesystem::path Create(kb::scene::Scene& scene,
        EditorAssetBrowserState& browser, const std::filesystem::path& virtualFolder);
    [[nodiscard]] static std::optional<kb::scene::UITheme> Read(const kb::scene::Scene& scene, kb::assets::AssetId id);
    [[nodiscard]] static bool Write(kb::scene::Scene& scene, kb::assets::AssetId id, const kb::scene::UITheme& value);
    // Set or remove a catalog-backed visual property in the base style or an existing variant.
    [[nodiscard]] static bool SetProperty(kb::scene::UITheme& theme, std::string_view variant,
        kb::scene::UIThemeProperty property);
    [[nodiscard]] static bool RemoveProperty(kb::scene::UITheme& theme, std::string_view variant,
        const kb::scene::UIThemePropertyKey& key);
    // Record locally authored style keys in the same candidate as the property edit.
    [[nodiscard]] static bool MarkLocalOverrides(kb::scene::UIComponentSet& components,
        std::span<const kb::scene::UIThemePropertyKey> keys);
    [[nodiscard]] static bool AddVariant(kb::scene::UITheme& theme, std::string name);
    [[nodiscard]] static bool RemoveVariant(kb::scene::UITheme& theme, std::string_view name);
};

// Execute/Undo/Redo write the shared asset and invalidate the runtime cache.
class EditorUIThemeEditCommand final : public IEditorCommand {
public:
    EditorUIThemeEditCommand(kb::scene::Scene& scene, kb::assets::AssetId id, std::string label,
        kb::scene::UITheme before, kb::scene::UITheme after);
    [[nodiscard]] std::string_view Label() const noexcept override;
    [[nodiscard]] bool AffectsSceneDocument() const noexcept override;
    [[nodiscard]] bool AffectsHierarchySelection() const noexcept override;
    [[nodiscard]] bool Execute() override;
    [[nodiscard]] bool Undo() override;
    [[nodiscard]] bool Redo() override;
private:
    kb::scene::Scene& scene_;
    kb::assets::AssetId id_;
    std::string label_;
    kb::scene::UITheme before_;
    kb::scene::UITheme after_;
};

} // namespace kb::editor
