#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"

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

} // namespace kb::scene
