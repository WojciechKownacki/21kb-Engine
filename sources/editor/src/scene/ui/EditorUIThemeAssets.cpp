#include "scene/ui/EditorUIThemeAssets.hpp"

#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/ui/UIThemeIO.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace kb::editor {
namespace {
std::optional<std::filesystem::path> ThemePath(const kb::scene::Scene& scene, kb::assets::AssetId id) {
    const auto& manager = scene.Assets().Manager();
    const auto* metadata = manager.Registry().Find(id);
    if (!metadata || metadata->type != kb::scene::kUIThemeAssetType) return std::nullopt;
    return !metadata->physicalPath.empty() ? std::optional{metadata->physicalPath} : manager.Mounts().Resolve(metadata->virtualPath);
}
std::vector<kb::scene::UIThemeProperty>* Properties(kb::scene::UITheme& theme, std::string_view name) {
    if (name.empty()) return &theme.properties;
    const auto found = std::ranges::find(theme.variants, name, &kb::scene::UIThemeVariant::name);
    return found == theme.variants.end() ? nullptr : &found->properties;
}
bool Reload(kb::scene::Scene& scene, kb::assets::AssetId id) {
    auto& manager = scene.Assets().Manager();
    static_cast<void>(manager.Unload(id));
    static_cast<void>(scene.Assets().Discover());
    return manager.Load<kb::scene::UITheme>(id).IsLoaded();
}
std::filesystem::path TransactionPath(const std::filesystem::path& target, std::string_view suffix) {
    static std::atomic<std::uint64_t> serial{0U};
    auto path = target;
    path += ".kbtxn." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
        "." + std::to_string(serial.fetch_add(1U, std::memory_order_relaxed)) + "." + std::string{suffix};
    return path;
}
bool ReplaceThemeTransactionFile(const std::filesystem::path& source, const std::filesystem::path& target) {
#if defined(_WIN32)
    return MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error;
    std::filesystem::rename(source, target, error);
    return !error;
#endif
}
} // namespace

std::filesystem::path EditorUIThemeAssets::Create(kb::scene::Scene& scene, EditorAssetBrowserState& browser,
    const std::filesystem::path& virtualFolder) {
    auto& manager = scene.Assets().Manager();
    const auto probe = virtualFolder.empty() ? std::nullopt : manager.Mounts().Resolve(virtualFolder / "probe");
    if (!probe) return {};
    const auto folder = probe->parent_path();
    auto file = folder / "UITheme.kbtheme";
    for (std::uint32_t index = 1U; std::filesystem::exists(file); ++index)
        file = folder / ("UITheme" + std::to_string(index) + ".kbtheme");
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    if (error || !kb::scene::UIThemeIO::Save(file, {})) return {};
    static_cast<void>(scene.Assets().Discover());
    const auto path = manager.Mounts().ToVirtual(file);
    const auto* metadata = path ? manager.Registry().FindByPath(*path) : nullptr;
    if (!metadata || metadata->type != kb::scene::kUIThemeAssetType || !manager.Load<kb::scene::UITheme>(metadata->id).IsLoaded()) {
        std::filesystem::remove(file, error);
        static_cast<void>(scene.Assets().Discover());
        return {};
    }
    static_cast<void>(browser.SelectAsset(metadata->id, manager));
    return *path;
}

std::optional<kb::scene::UITheme> EditorUIThemeAssets::Read(const kb::scene::Scene& scene, kb::assets::AssetId id) {
    const auto path = ThemePath(scene, id);
    return path ? kb::scene::UIThemeIO::Load(*path) : std::nullopt;
}
bool EditorUIThemeAssets::Write(kb::scene::Scene& scene, kb::assets::AssetId id, const kb::scene::UITheme& value) {
    const auto path = ThemePath(scene, id);
    if (!path || !kb::scene::ValidateUITheme(value)) return false;
    if (!kb::scene::UIThemeIO::Load(*path)) return false;
    const auto staged = TransactionPath(*path, "stage");
    const auto backup = TransactionPath(*path, "backup");
    std::error_code error;
    const auto cleanup = [&] {
        std::filesystem::remove(staged, error);
        std::filesystem::remove(backup, error);
    };
    if (!kb::scene::UIThemeIO::Save(staged, value) || !kb::scene::UIThemeIO::Load(staged) ||
        !std::filesystem::copy_file(*path, backup, std::filesystem::copy_options::none, error) ||
        !ReplaceThemeTransactionFile(staged, *path)) {
        cleanup();
        return false;
    }
    if (Reload(scene, id)) {
        cleanup();
        return true;
    }
    if (ReplaceThemeTransactionFile(backup, *path)) static_cast<void>(Reload(scene, id));
    else {
        // Retain the backup when rollback fails so the previous asset can be recovered.
        std::filesystem::remove(staged, error);
        return false;
    }
    cleanup();
    return false;
}
bool EditorUIThemeAssets::SetProperty(kb::scene::UITheme& theme, std::string_view variant,
    kb::scene::UIThemeProperty property) {
    auto candidate = theme;
    auto* properties = Properties(candidate, variant);
    if (!properties) return false;
    const auto found = std::ranges::find(*properties, property.key, &kb::scene::UIThemeProperty::key);
    if (found == properties->end()) properties->push_back(std::move(property));
    else *found = std::move(property);
    if (!kb::scene::ValidateUITheme(candidate)) return false;
    theme = std::move(candidate);
    return true;
}
bool EditorUIThemeAssets::RemoveProperty(kb::scene::UITheme& theme, std::string_view variant,
    const kb::scene::UIThemePropertyKey& key) {
    auto* properties = Properties(theme, variant);
    if (!properties) return false;
    return std::erase_if(*properties, [&](const auto& property) { return property.key == key; }) != 0U;
}
bool EditorUIThemeAssets::MarkLocalOverrides(kb::scene::UIComponentSet& components,
    std::span<const kb::scene::UIThemePropertyKey> keys) {
    if (!components.rectTransform || components.rectTransform->themeAssetId == 0U) return true;
    auto& rect = *components.rectTransform;
    std::vector<kb::scene::UIThemePropertyKey> overrides;
    if (!kb::scene::ParseUIThemeOverrides(kb::scene::UIRectThemeOverrides(rect), overrides)) return false;
    for (const auto& key : keys) {
        if (!kb::scene::IsUIThemeProperty(key)) continue;
        if (std::ranges::find(overrides, key) == overrides.end()) overrides.push_back(key);
    }
    std::string text;
    for (const auto& key : overrides) {
        const auto* component = kb::scene::FindUIComponentDescriptor(key.component);
        if (!component) return false;
        if (!text.empty()) text += ';';
        text += component->stableId;
        text += ':';
        text += key.property;
    }
    return kb::scene::SetUIRectThemeOverrides(rect, text);
}

bool EditorUIThemeAssets::AddVariant(kb::scene::UITheme& theme, std::string name) {
    auto candidate = theme;
    candidate.variants.push_back({std::move(name), {}});
    if (!kb::scene::ValidateUITheme(candidate)) return false;
    theme = std::move(candidate);
    return true;
}
bool EditorUIThemeAssets::RemoveVariant(kb::scene::UITheme& theme, std::string_view name) {
    return std::erase_if(theme.variants, [&](const auto& variant) { return variant.name == name; }) != 0U;
}

EditorUIThemeEditCommand::EditorUIThemeEditCommand(kb::scene::Scene& scene, kb::assets::AssetId id,
    std::string label, kb::scene::UITheme before, kb::scene::UITheme after)
    : scene_(scene), id_(id), label_(std::move(label)), before_(std::move(before)), after_(std::move(after)) {}
std::string_view EditorUIThemeEditCommand::Label() const noexcept { return label_; }
bool EditorUIThemeEditCommand::AffectsSceneDocument() const noexcept { return false; }
bool EditorUIThemeEditCommand::AffectsHierarchySelection() const noexcept { return false; }
bool EditorUIThemeEditCommand::Execute() { return EditorUIThemeAssets::Write(scene_, id_, after_); }
bool EditorUIThemeEditCommand::Undo() { return EditorUIThemeAssets::Write(scene_, id_, before_); }
bool EditorUIThemeEditCommand::Redo() { return EditorUIThemeAssets::Write(scene_, id_, after_); }

} // namespace kb::editor
