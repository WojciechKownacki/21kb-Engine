#pragma once

#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "engine/audio/AudioMixerAsset.hpp"
#include "scene/audio/EditorAudioMixerRuntime.hpp"
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace kb::editor {
class EditorAudioMixerAuthoring;
enum class AudioMixerPanelAction { None, Capture, Reset, Stop, Previous, Next, Gain, Mute, Preview };
struct AudioMixerPanelControl {
    AudioMixerPanelAction action{};
    DockRect bounds{};
    std::string bus, label;
    bool enabled = true;
};
struct AudioMixerPanelStrip {
    DockRect bounds{};
    std::string name, route;
    float gain = 1.0F;
    bool mute = false;
    kb::audio::AudioMeterReading meter;
};
struct AudioMixerPanelLayout {
    std::vector<AudioMixerPanelControl> controls;
    std::vector<AudioMixerPanelStrip> strips;
    std::size_t first = 0U, visible = 1U, count = 0U;
};
class AudioMixerPanelView {
public:
    [[nodiscard]] static AudioMixerPanelLayout Layout(DockRect content, const kb::audio::AudioMixerAsset& asset,
        const EditorAudioMixerRuntimeState& runtime, std::size_t first);
    [[nodiscard]] static bool PointerDown(kb::scene::Scene& scene, kb::assets::AssetId id,
        EditorAudioMixerAuthoring& authoring, const AudioMixerPanelLayout& layout, int x, int y, std::size_t& first);
#if defined(_WIN32)
    static void Paint(HDC dc, DockRect content, const EditorTheme& theme, const AudioMixerPanelLayout& layout,
        std::string_view diagnostic);
#endif
};
} // namespace kb::editor
