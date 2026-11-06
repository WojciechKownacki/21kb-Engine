#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/ui/UIComponentCatalog.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace kb::scene {

class Scene;

// A widget authored as a small hierarchy rather than one object. Creating the Dropdown preset builds:
//
//   Dropdown             Border, Selectable, Dropdown (options "Option A", "Option B", "Option C")
//     Label              Text - the caption
//     Arrow              Text - a down triangle
//     Template           Border, Canvas Group (hidden) - cloned as the open list
//       Viewport         Mask, Scroll View (vertical, linked to Scrollbar)
//         Content
//           Item         Selectable (tints Item Background), Toggle (shows Item Checkmark)
//             Item Background   Border
//             Item Checkmark    Text - a check mark
//             Item Label        Text - an option's label
//       Scrollbar        Border, Selectable, Scrollbar
//
// The dropdown points at Template, Label and Item Label. `created` receives every object in creation
// order, root first, so an editor can finish each one (fonts, selection) in the same command.
[[nodiscard]] SceneEntity CreateUIDropdownHierarchy(Scene& scene, SceneObject parent, std::string_view name,
                                                    std::vector<SceneEntity>* created = nullptr);

// The other widgets built as a hierarchy, each wired to the parts it drives:
//
//   Slider               Border (track), Selectable, Slider (fill Fill, handle Handle)
//     Fill               Border - stretched from the start to the value
//     Handle             Border - placed at the value
//   Progress Bar         Border (track), Progress Bar (fill Fill)
//     Fill               Border
//   Scroll View          Border
//     Viewport           Mask, Selectable, Scroll View (vertical, elastic, linked to Scrollbar)
//       Content          where the scrolled widgets go
//     Scrollbar          Border, Selectable, Scrollbar
[[nodiscard]] bool IsUIHierarchyPreset(UIComponentPreset preset) noexcept;
// Builds the preset's hierarchy; an invalid entity for a preset that is a single object.
[[nodiscard]] SceneEntity CreateUIHierarchy(Scene& scene, UIComponentPreset preset, SceneObject parent, std::string_view name,
                                            std::vector<SceneEntity>* created = nullptr);

// A whole screen built from the widgets above, as one hierarchy that stretches over its parent (a Canvas). Its buttons
// carry event names, so a script answers them with `function PlayClicked(self, event)` or OnUIClicked:
//
//   Main Menu   Title, Play (PlayClicked), Settings (SettingsClicked), Quit (QuitClicked)
//   Pause       dimmed backdrop, Panel with Title, Resume (ResumeClicked), Settings (SettingsClicked), Main Menu (MainMenuClicked)
//   HUD         Health (Progress Bar, top left), Score (Text, top right)
//   Settings    dimmed backdrop, Panel with Title, Volume (Slider), Fullscreen (Toggle), Back (BackClicked)
enum class UIScreenTemplate : std::uint8_t { MainMenu, Pause, Hud, Settings };

struct UIScreenTemplateDescriptor {
    UIScreenTemplate id{};
    std::string_view name;
    std::string_view summary;
};

[[nodiscard]] std::span<const UIScreenTemplateDescriptor> UIScreenTemplateCatalog() noexcept;
[[nodiscard]] const UIScreenTemplateDescriptor* FindUIScreenTemplate(std::string_view name) noexcept;
// Builds the screen under `parent` (an invalid object: at the scene root); `created` receives every object, root first.
[[nodiscard]] SceneEntity CreateUIScreen(Scene& scene, UIScreenTemplate screen, SceneObject parent, std::string_view name,
                                         std::vector<SceneEntity>* created = nullptr);

} // namespace kb::scene
