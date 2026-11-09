#include "rendering/AudioMixerPanelView.hpp"
#include "scene/audio/EditorAudioMixerAuthoring.hpp"
#include "localization/EditorLocalization.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#if defined(_WIN32)
#include "rendering/gdi/GdiColor.hpp"
#include "rendering/gdi/GdiRectPainter.hpp"
#include "rendering/gdi/GdiFramePainter.hpp"
#include "platform/win32/Utf8Gdi.hpp"
#endif

namespace kb::editor {
AudioMixerPanelLayout AudioMixerPanelView::Layout(DockRect content, const kb::audio::AudioMixerAsset& asset,
    const EditorAudioMixerRuntimeState& runtime, std::size_t first) {
    AudioMixerPanelLayout result;
    result.count = asset.buses.size() + 1U;
    result.visible = static_cast<std::size_t>(std::max(1, (content.width - 16) / 156));
    result.first = std::min(first, result.count > result.visible ? result.count - result.visible : 0U);
    const auto control = [&](AudioMixerPanelAction action, DockRect bounds, std::string bus, std::string label, bool enabled) {
        result.controls.push_back({action, bounds, std::move(bus), std::move(label), enabled});
    };
    const int top = content.y + 8;
    const int buttonWidth = std::max(1, (content.width - 16) / 5);
    const auto toolbar = [&](int index) { return DockRect{content.x + 8 + index * buttonWidth, top, buttonWidth - 4, 26}; };
    control(AudioMixerPanelAction::Capture, toolbar(0), {}, runtime.meters.captureEnabled ? "REC on" : "REC off", runtime.canCapture);
    control(AudioMixerPanelAction::Reset, toolbar(1), {}, "Reset", runtime.canCapture);
    control(AudioMixerPanelAction::Stop, toolbar(2), {}, std::string{EditorText("common.stop", "Stop")}, runtime.previewPlaying);
    control(AudioMixerPanelAction::Previous, toolbar(3), {}, "<", result.first > 0U);
    control(AudioMixerPanelAction::Next, toolbar(4), {}, ">", result.first + result.visible < result.count);
    if (content.height < 270) return result;
    for (std::size_t i = result.first; i < std::min(result.count, result.first + result.visible); ++i) {
        AudioMixerPanelStrip strip;
        strip.bounds = {content.x + 8 + static_cast<int>(i - result.first) * 156, content.y + 66, 148, content.height - 74};
        strip.name = i == 0U ? "Master" : asset.buses[i - 1U].name;
        strip.route = i == 0U ? "Output" : asset.buses[i - 1U].parentBus;
        if (strip.route.empty()) strip.route = "Master";
        if (i == 0U) strip.meter = runtime.meters.master;
        else {
            const auto& bus = asset.buses[i - 1U];
            strip.gain = bus.volume; strip.mute = bus.mute;
            const auto meter = std::find_if(runtime.meters.buses.begin(), runtime.meters.buses.end(),
                [&](const auto& item) { return item.bus == bus.name; });
            if (meter != runtime.meters.buses.end()) strip.meter = *meter;
            control(AudioMixerPanelAction::Gain, {strip.bounds.x + 94, strip.bounds.y + 60, 40, strip.bounds.height - 132},
                bus.name, {}, true);
            control(AudioMixerPanelAction::Mute, {strip.bounds.x + 8, strip.bounds.y + strip.bounds.height - 62, 132, 24},
                bus.name, (bus.mute ? "[x] " : "[ ] ") + std::string{EditorText("inspector.audioMixer.mute", "Mute")}, true);
        }
        control(AudioMixerPanelAction::Preview, {strip.bounds.x + 8, strip.bounds.y + strip.bounds.height - 32, 132, 24},
            i == 0U ? std::string{} : strip.name, "Preview", runtime.canPreview && !runtime.previewClip.empty());
        result.strips.push_back(std::move(strip));
    }
    return result;
}

bool AudioMixerPanelView::PointerDown(kb::scene::Scene& scene, kb::assets::AssetId id,
    EditorAudioMixerAuthoring& authoring, const AudioMixerPanelLayout& layout, int x, int y, std::size_t& first) {
    for (const auto& control : layout.controls) {
        if (!control.enabled || !control.bounds.Contains(x, y)) continue;
        switch (control.action) {
        case AudioMixerPanelAction::Capture:
            return EditorAudioMixerRuntime::ConfigureCapture(scene, id, !EditorAudioMixerRuntime::Query(scene, id).meters.captureEnabled);
        case AudioMixerPanelAction::Reset: return EditorAudioMixerRuntime::ResetCapture(scene, id);
        case AudioMixerPanelAction::Stop: EditorAudioMixerRuntime::Stop(scene); return true;
        case AudioMixerPanelAction::Previous: first = layout.first > layout.visible ? layout.first - layout.visible : 0U; return true;
        case AudioMixerPanelAction::Next: first = layout.first + layout.visible; return true;
        case AudioMixerPanelAction::Preview: return EditorAudioMixerRuntime::Play(scene, id, control.bus);
        case AudioMixerPanelAction::Gain: {
            // -60 dB to +12 dB. The bottom endpoint is exact silence.
            const float position = std::clamp(1.0F - static_cast<float>(y - control.bounds.y)
                / static_cast<float>(std::max(1, control.bounds.height - 1)), 0.0F, 1.0F);
            return authoring.SetBusVolume(id, control.bus, position == 0.0F ? 0.0F : std::pow(10.0F, (-60.0F + 72.0F * position) / 20.0F));
        }
        case AudioMixerPanelAction::Mute: {
            const auto asset = authoring.Read(id);
            const auto* bus = asset ? asset->FindBus(control.bus) : nullptr;
            return bus != nullptr && authoring.SetBusMute(id, control.bus, !bus->mute);
        }
        case AudioMixerPanelAction::None: return false;
        }
    }
    return false;
}

#if defined(_WIN32)
void AudioMixerPanelView::Paint(HDC dc, DockRect content, const EditorTheme& theme, const AudioMixerPanelLayout& layout,
    std::string_view diagnostic) {
    const auto rect = [](DockRect r) { return RECT{r.x, r.y, r.x + r.width, r.y + r.height}; };
    const auto color = [](EditorColor value) { return GdiColor::ToColorRef(value); };
    const auto text = [&](DockRect r, std::string_view value, COLORREF tint) {
        auto bounds = rect(r); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, tint);
        DrawTextUtf8(dc, value.data(), static_cast<int>(value.size()), &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    };
    GdiRectPainter::Fill(dc, rect(content), color(theme.panel));
    text({content.x + 8, content.y + 38, content.width - 16, 24}, diagnostic.empty()
        ? "Select a mixer in Project Files. Edit routing, effects and preview clip in Inspector." : diagnostic, color(theme.textSecondary));
    for (const auto& strip : layout.strips) {
        GdiFramePainter::DrawSharp(dc, rect(strip.bounds), color(theme.chrome), color(theme.borderPanel));
        text({strip.bounds.x + 4, strip.bounds.y + 4, 140, 22}, strip.name, color(theme.textPrimary));
        text({strip.bounds.x + 4, strip.bounds.y + 28, 140, 20}, strip.route, color(theme.textSecondary));
        const int height = strip.bounds.height - 132;
        for (std::size_t channel = 0U; channel < 2U; ++channel) {
            const DockRect bar{strip.bounds.x + 14 + static_cast<int>(channel) * 32, strip.bounds.y + 60, 24, height};
            GdiRectPainter::Fill(dc, rect(bar), RGB(20, 24, 28));
            if (!strip.meter.available || channel >= strip.meter.channels) continue;
            const auto& reading = strip.meter.channel[channel];
            const auto scale = [](float value) { return value > 0.0F ? std::clamp((20.0F * std::log10(value) + 60.0F) / 60.0F, 0.0F, 1.0F) : 0.0F; };
            const int filled = static_cast<int>(scale(reading.peak) * static_cast<float>(height));
            GdiRectPainter::Fill(dc, rect({bar.x, bar.y + height - filled, bar.width, filled}), RGB(48, 152, 102));
            const int rms = static_cast<int>(scale(reading.rms) * static_cast<float>(height));
            GdiRectPainter::Fill(dc, rect({bar.x + 5, bar.y + height - rms, bar.width - 10, rms}), RGB(100, 210, 150));
            if (reading.clippedSamples > 0U || reading.nonFiniteSamples > 0U || reading.heldPeak >= 1.0F)
                GdiRectPainter::Fill(dc, rect({bar.x, bar.y - 8, bar.width, 5}), RGB(198, 64, 64));
        }
    }
    for (const auto& control : layout.controls) {
        if (control.action == AudioMixerPanelAction::Gain) {
            GdiRectPainter::Fill(dc, rect(control.bounds), RGB(20, 24, 28));
            const auto strip = std::find_if(layout.strips.begin(), layout.strips.end(), [&](const auto& item) { return item.name == control.bus; });
            if (strip == layout.strips.end()) continue;
            const float position = strip->gain > 0.0F ? std::clamp((20.0F * std::log10(strip->gain) + 60.0F) / 72.0F, 0.0F, 1.0F) : 0.0F;
            char gainText[32]{};
            if (strip->gain == 0.0F) std::snprintf(gainText, sizeof(gainText), "-inf");
            else std::snprintf(gainText, sizeof(gainText), "%.1f", static_cast<double>(20.0F * std::log10(strip->gain)));
            text({control.bounds.x - 2, control.bounds.y - 20, control.bounds.width + 4, 18}, gainText, color(theme.textSecondary));
            const int y = control.bounds.y + static_cast<int>((1.0F - position) * static_cast<float>(control.bounds.height - 1));
            GdiRectPainter::Fill(dc, rect({control.bounds.x, y, control.bounds.width, 3}), color(theme.accent));
        } else {
            GdiFramePainter::DrawSharp(dc, rect(control.bounds), color(theme.panel), color(theme.borderPanel));
            text(control.bounds, control.label, color(control.enabled ? theme.textPrimary : theme.textDisabled));
        }
    }
}
#endif
} // namespace kb::editor
