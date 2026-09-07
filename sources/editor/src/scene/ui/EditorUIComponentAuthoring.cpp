#include "scene/ui/EditorUIComponentAuthoring.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneUIComponentSet.hpp"
#include "engine/scene/SceneUIComponents.hpp"
#include "engine/ui/UIComponentPropertyCatalog.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] std::string_view PresetName(std::string_view id) noexcept {
    const std::string_view prefix = EditorUIPresetPrefix();
    return id.starts_with(prefix) ? id.substr(prefix.size()) : std::string_view{};
}

[[nodiscard]] bool AddPreset(kb::scene::SceneUIComponents components,
    kb::scene::SceneEntity entity, const kb::scene::UIComponentPresetDescriptor& preset) {
    kb::scene::UIComponentSet values = kb::scene::BuildUIComponentPreset(preset.preset);
    const kb::scene::UIComponentSet existing = kb::scene::CaptureSceneUIComponents(components, entity);
    kb::scene::UIComponentSet candidate = existing;
    bool changed = false;
    for (const kb::scene::UIComponentType type : preset.components) {
        if (kb::scene::HasUIComponent(existing, type)) {
            static_cast<void>(kb::scene::RemoveUIComponent(values, type));
        } else {
            if (!kb::scene::AddUIComponent(candidate, type)) return false;
            changed = true;
        }
    }
    if (changed) kb::scene::ApplySceneUIComponents(components, entity, values);
    return changed;
}

} // namespace

bool EditorUIComponentAuthoring::Supports(std::string_view id) noexcept {
    return kb::scene::FindUIComponentDescriptor(id) != nullptr ||
        kb::scene::FindUIComponentPreset(PresetName(id)) != nullptr;
}

bool EditorUIComponentAuthoring::Has(const kb::scene::Scene& scene, kb::scene::SceneEntity entity,
    kb::scene::UIComponentType type) noexcept {
    return kb::scene::HasUIComponent(
        kb::scene::CaptureSceneUIComponents(scene.Components().UI(), entity), type);
}

bool EditorUIComponentAuthoring::Add(kb::scene::Scene& scene, kb::scene::SceneEntity entity,
    std::string_view id) {
    kb::scene::SceneUIComponents components = scene.Components().UI();
    if (const kb::scene::UIComponentPresetDescriptor* preset =
            kb::scene::FindUIComponentPreset(PresetName(id));
        preset != nullptr) {
        return AddPreset(components, entity, *preset);
    }
    const kb::scene::UIComponentDescriptor* descriptor = kb::scene::FindUIComponentDescriptor(id);
    if (descriptor == nullptr || Has(scene, entity, descriptor->type)) return false;
    for (const auto& preset : kb::scene::UIComponentPresetCatalog()) {
        if (preset.name == descriptor->displayName) return AddPreset(components, entity, preset);
    }
    kb::scene::UIComponentSet candidate = kb::scene::CaptureSceneUIComponents(components, entity);
    if (!kb::scene::AddUIComponent(candidate, descriptor->type)) return false;
    if (!candidate.rectTransform) candidate.rectTransform.emplace();
    kb::scene::SynchronizeSceneUIComponents(components, entity, candidate);
    return true;
}

bool EditorUIComponentAuthoring::Remove(kb::scene::Scene& scene, kb::scene::SceneEntity entity,
    kb::scene::UIComponentType type) noexcept {
    kb::scene::SceneUIComponents components = scene.Components().UI();
    kb::scene::UIComponentSet values = kb::scene::CaptureSceneUIComponents(components, entity);
    if (type == kb::scene::UIComponentType::RectTransform) {
        for (const auto& descriptor : kb::scene::UIComponentCatalog()) {
            if (descriptor.type != type && kb::scene::HasUIComponent(values, descriptor.type)) return false;
        }
    }
    if (!kb::scene::RemoveUIComponent(values, type)) return false;
    kb::scene::SynchronizeSceneUIComponents(components, entity, values);
    return true;
}

bool EditorUIComponentAuthoring::Complete(kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    auto ui = scene.Components().UI();
    const auto existing = kb::scene::CaptureSceneUIComponents(ui, entity);
    if (existing.Empty() || existing.rectTransform) return false;
    for (const auto& descriptor : kb::scene::UIComponentCatalog()) {
        if (!kb::scene::HasUIComponent(existing, descriptor.type)) continue;
        for (const auto& preset : kb::scene::UIComponentPresetCatalog()) {
            if (preset.name == descriptor.displayName) static_cast<void>(AddPreset(ui, entity, preset));
        }
    }
    if (!ui.Has<kb::scene::UIRectTransform>(entity)) ui.Set(entity, kb::scene::UIRectTransform{});
    return true;
}

} // namespace kb::editor
