#pragma once

#include "scene/EditorAnimationPreviewScene.hpp"
#include "scene/EditorViewportCameraState.hpp"

#include "kb/render/overlay/PhysicsDebugLine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace kb::editor {

// Builds camera-facing vector labels directly into the scene debug-line pass. Keeping these
// labels in the 3D pass makes them visible above the native bgfx child surface and ties their
// scale to screen pixels instead of model units.
class SkeletalMeshEditorSceneLabelBuilder {
public:
    static void Append(
        std::vector<kb::render::PhysicsDebugLine>& output,
        std::span<const AnimationPreviewOverlayLabel> labels,
        const EditorViewportCameraState& camera,
        std::uint32_t viewportHeight) {
        if (viewportHeight == 0U || labels.empty()) return;

        const EditorViewportCameraAxes axes = camera.Axes();
        const float radians = camera.VerticalFovDegrees() * 0.01745329251994329577F;
        const float halfFovTangent = std::tan(radians * 0.5F);
        if (!std::isfinite(halfFovTangent) || halfFovTangent <= 0.0F) return;

        constexpr std::size_t kMaximumSegments = 16384U;
        constexpr std::size_t kMaximumCharactersPerLabel = 48U;
        for (const AnimationPreviewOverlayLabel& label : labels) {
            const kb::scene::Vec3 toLabel = label.position - axes.position;
            const float depth = toLabel.x * axes.forward.x +
                toLabel.y * axes.forward.y + toLabel.z * axes.forward.z;
            if (!std::isfinite(depth) || depth <= camera.NearClip()) continue;

            const float worldPerPixel =
                (2.0F * depth * halfFovTangent) / static_cast<float>(viewportHeight);
            const float cell = worldPerPixel * 1.35F;
            const kb::scene::Vec3 origin = label.position + axes.right * (cell * 1.5F) +
                axes.up * (cell * 4.0F);
            const std::size_t characterCount = std::min(label.text.size(), kMaximumCharactersPerLabel);
            for (std::size_t characterIndex = 0U; characterIndex < characterCount; ++characterIndex) {
                const GlyphRows glyph = Glyph(label.text[characterIndex]);
                for (std::size_t row = 0U; row < glyph.size(); ++row) {
                    std::uint32_t column = 0U;
                    while (column < 5U) {
                        while (column < 5U && !PixelSet(glyph[row], column)) ++column;
                        if (column == 5U) break;
                        const std::uint32_t firstColumn = column;
                        while (column < 5U && PixelSet(glyph[row], column)) ++column;

                        const float characterOffset = static_cast<float>(characterIndex) * 6.0F;
                        const kb::scene::Vec3 from = origin +
                            axes.right * (cell * (characterOffset + static_cast<float>(firstColumn))) -
                            axes.up * (cell * static_cast<float>(row));
                        const kb::scene::Vec3 to = origin +
                            axes.right * (cell * (characterOffset + static_cast<float>(column))) -
                            axes.up * (cell * static_cast<float>(row));
                        output.push_back(kb::render::PhysicsDebugLine{
                            .from = { from.x, from.y, from.z },
                            .to = { to.x, to.y, to.z },
                            .color = LabelColor(label.text),
                            .alpha = 1.0F,
                        });
                        if (output.size() >= kMaximumSegments) return;
                    }
                }
            }
        }
    }

private:
    using GlyphRows = std::array<std::uint8_t, 7U>;

    [[nodiscard]] static constexpr bool PixelSet(std::uint8_t row, std::uint32_t column) noexcept {
        return (row & static_cast<std::uint8_t>(1U << (4U - column))) != 0U;
    }

    [[nodiscard]] static std::array<float, 3U> LabelColor(std::string_view text) noexcept {
        if (text.starts_with("LODs:")) return { 1.0F, 0.86F, 0.22F };
        if (text == "Root motion") return { 0.94F, 0.16F, 0.78F };
        return { 1.0F, 0.58F, 0.24F };
    }

    [[nodiscard]] static constexpr GlyphRows Glyph(char value) noexcept {
        const char c = value >= 'a' && value <= 'z' ? static_cast<char>(value - ('a' - 'A')) : value;
        switch (c) {
        case 'A': return { 14, 17, 17, 31, 17, 17, 17 };
        case 'B': return { 30, 17, 17, 30, 17, 17, 30 };
        case 'C': return { 14, 17, 16, 16, 16, 17, 14 };
        case 'D': return { 30, 17, 17, 17, 17, 17, 30 };
        case 'E': return { 31, 16, 16, 30, 16, 16, 31 };
        case 'F': return { 31, 16, 16, 30, 16, 16, 16 };
        case 'G': return { 14, 17, 16, 23, 17, 17, 14 };
        case 'H': return { 17, 17, 17, 31, 17, 17, 17 };
        case 'I': return { 31, 4, 4, 4, 4, 4, 31 };
        case 'J': return { 7, 2, 2, 2, 18, 18, 12 };
        case 'K': return { 17, 18, 20, 24, 20, 18, 17 };
        case 'L': return { 16, 16, 16, 16, 16, 16, 31 };
        case 'M': return { 17, 27, 21, 21, 17, 17, 17 };
        case 'N': return { 17, 25, 21, 19, 17, 17, 17 };
        case 'O': return { 14, 17, 17, 17, 17, 17, 14 };
        case 'P': return { 30, 17, 17, 30, 16, 16, 16 };
        case 'Q': return { 14, 17, 17, 17, 21, 18, 13 };
        case 'R': return { 30, 17, 17, 30, 20, 18, 17 };
        case 'S': return { 15, 16, 16, 14, 1, 1, 30 };
        case 'T': return { 31, 4, 4, 4, 4, 4, 4 };
        case 'U': return { 17, 17, 17, 17, 17, 17, 14 };
        case 'V': return { 17, 17, 17, 17, 17, 10, 4 };
        case 'W': return { 17, 17, 17, 21, 21, 21, 10 };
        case 'X': return { 17, 17, 10, 4, 10, 17, 17 };
        case 'Y': return { 17, 17, 10, 4, 4, 4, 4 };
        case 'Z': return { 31, 1, 2, 4, 8, 16, 31 };
        case '0': return { 14, 17, 19, 21, 25, 17, 14 };
        case '1': return { 4, 12, 4, 4, 4, 4, 14 };
        case '2': return { 14, 17, 1, 2, 4, 8, 31 };
        case '3': return { 30, 1, 1, 14, 1, 1, 30 };
        case '4': return { 2, 6, 10, 18, 31, 2, 2 };
        case '5': return { 31, 16, 16, 30, 1, 1, 30 };
        case '6': return { 14, 16, 16, 30, 17, 17, 14 };
        case '7': return { 31, 1, 2, 4, 8, 8, 8 };
        case '8': return { 14, 17, 17, 14, 17, 17, 14 };
        case '9': return { 14, 17, 17, 15, 1, 1, 14 };
        case ':': return { 0, 4, 4, 0, 4, 4, 0 };
        case '-': return { 0, 0, 0, 31, 0, 0, 0 };
        case '_': return { 0, 0, 0, 0, 0, 0, 31 };
        case '.': return { 0, 0, 0, 0, 0, 6, 6 };
        case '/': return { 1, 2, 2, 4, 8, 8, 16 };
        case ' ': return {};
        default: return { 14, 17, 1, 2, 4, 0, 4 };
        }
    }
};

} // namespace kb::editor
