#pragma once

#include "engine/core/ProfilerRegistry.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kb::config {
class IniDocument;
}

namespace kb::editor {

enum class ProfilerCorner : std::uint8_t { TopLeft, TopRight, BottomLeft, BottomRight };
enum class ProfilerTextSize : std::uint8_t { Small, Medium, Large };
enum class ProfilerPreset : std::uint8_t { Minimal, Rendering, Memory, Everything };

// Which lines the live graph on the scene draws (the thresholds are lines of their own).
struct ProfilerGraphLines {
    bool frame = true;
    bool renderThread = false;
    bool gpu = true;
    bool budget = true;
    bool alarm = true;

    [[nodiscard]] bool operator==(const ProfilerGraphLines&) const noexcept = default;
};

// How the counters look on the scene.
struct ProfilerOverlayOptions {
    bool listVisible = false;
    ProfilerCorner listCorner = ProfilerCorner::TopRight;
    bool graphVisible = false;
    ProfilerCorner graphCorner = ProfilerCorner::BottomRight;
    ProfilerTextSize textSize = ProfilerTextSize::Small;
    // How often the numbers on the scene change; 0 follows every frame.
    std::uint8_t refreshPerSecond = 4U;
    std::uint8_t opacityPercent = 70U;
    ProfilerGraphLines lines;

    [[nodiscard]] static constexpr std::array<std::uint8_t, 5> RefreshChoices() noexcept { return { 1U, 2U, 4U, 10U, 0U }; }
    [[nodiscard]] static constexpr std::array<std::uint8_t, 4> OpacityChoices() noexcept { return { 50U, 70U, 85U, 100U }; }

    [[nodiscard]] bool operator==(const ProfilerOverlayOptions&) const noexcept = default;
};

// The budget and alarm the user set for a counter, in place of the ones it was built with.
struct ProfilerThreshold {
    std::string counter;
    double budget = 0.0;
    double alarm = 0.0;

    [[nodiscard]] bool operator==(const ProfilerThreshold&) const noexcept = default;
};

// What the user asked to see: the areas to measure, the counters to show in the panel and the
// ones to show on the scene (two lists, by counter id), and how the scene shows them. A counter
// is shown only while its area is measured.
class ProfilerSelection final {
public:
    std::vector<std::string> areas;
    std::vector<std::string> panelCounters;
    std::vector<std::string> sceneCounters;
    std::vector<ProfilerThreshold> thresholds;
    ProfilerOverlayOptions overlay;

    ProfilerSelection();

    [[nodiscard]] bool AreaMeasured(kb::core::ProfilerArea area) const noexcept;
    [[nodiscard]] bool InPanel(std::string_view counterId) const noexcept;
    [[nodiscard]] bool OnScene(std::string_view counterId) const noexcept;
    void SetAreaMeasured(kb::core::ProfilerArea area, bool measured);
    void SetInPanel(std::string_view counterId, bool shown);
    void SetOnScene(std::string_view counterId, bool shown);
    // Zero for both puts the counter's own thresholds back.
    void SetThreshold(std::string_view counterId, double budget, double alarm);

    // Replaces the areas and both counter lists, and turns the scene's list and graph on.
    void ApplyPreset(ProfilerPreset preset, const kb::core::ProfilerRegistry& registry);

    [[nodiscard]] static std::string_view AreaId(kb::core::ProfilerArea area) noexcept;
    [[nodiscard]] static ProfilerSelection Read(const kb::config::IniDocument& document);
    void Write(kb::config::IniDocument& document) const;

    [[nodiscard]] bool operator==(const ProfilerSelection&) const noexcept = default;
};

} // namespace kb::editor
