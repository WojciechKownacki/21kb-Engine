#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace kb::particle_editor {

// The particle editor's own text in the editor's language: ParticleText("particle.speed_min",
// "Speed min"). The editor installs its catalogs' lookup at start (SetTextSource); without one -
// in tests and tools - the text is English. scripts/editor_localization.py collects these calls
// like the editor's EditorText, so keys follow the same rules.
using ParticleTextSource = std::string_view (*)(std::string_view key, std::string_view english);

void SetTextSource(ParticleTextSource source) noexcept;

// The view stays valid for the whole run (the editor never unloads a catalog).
[[nodiscard]] std::string_view ParticleText(std::string_view key, std::string_view english);

// `english` with its {name} markers filled in; a translation keeps the same markers.
[[nodiscard]] std::string ParticleFormat(std::string_view key, std::string_view english,
    std::initializer_list<std::pair<std::string_view, std::string>> values);

} // namespace kb::particle_editor
