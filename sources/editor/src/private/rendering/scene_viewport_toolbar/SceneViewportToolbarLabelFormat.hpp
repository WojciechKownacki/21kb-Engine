#pragma once

#include "engine/library/EngineLibraryTextFormat.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace kb::editor {

// LIB-062: allocation-free formatting of the scene-viewport toolbar's
// per-frame numeric HUD labels (FPS counter, draw-call/mesh chips, ECS
// frame-time chip), built on kb::library::TextFormatBuffer over a
// caller-provided stack buffer. This is deliberately platform-independent
// (no GDI/HDC) so the formatting — the part LIB-062 is about — is directly
// unit-testable; the Win32 GDI renderer (SceneViewportToolbarInfoRenderer)
// simply draws the returned byte range. Numeric conversions go through
// std::to_chars (guaranteed non-allocating, locale-invariant) rather than
// the std::snprintf these labels used before, which invokes locale
// machinery and may allocate internally — exactly the "niekontrolowana
// alokacja" this task rules out. Each function returns a view into `out`,
// so it never allocates; a caller that draws the result must pass the
// returned view's size explicitly (the buffer is not null-terminated past
// the logical length).
struct SceneViewportToolbarLabelFormat {
    [[nodiscard]] static std::string_view Fps(std::span<char> out, int fps) {
        kb::library::TextFormatBuffer buffer{ out };
        if (fps > 0) {
            static_cast<void>(buffer.Append("FPS "));
            static_cast<void>(buffer.AppendInt(fps));
        } else {
            static_cast<void>(buffer.Append("FPS --"));
        }
        return buffer.View();
    }

    [[nodiscard]] static std::string_view DrawCalls(std::span<char> out, std::uint32_t count) {
        kb::library::TextFormatBuffer buffer{ out };
        static_cast<void>(buffer.Append("DC "));
        static_cast<void>(buffer.AppendUInt(count));
        return buffer.View();
    }

    [[nodiscard]] static std::string_view Meshes(std::span<char> out, std::uint32_t count) {
        kb::library::TextFormatBuffer buffer{ out };
        static_cast<void>(buffer.Append("M "));
        static_cast<void>(buffer.AppendUInt(count));
        return buffer.View();
    }

    [[nodiscard]] static std::string_view EcsMilliseconds(std::span<char> out, bool valid, double milliseconds) {
        kb::library::TextFormatBuffer buffer{ out };
        if (valid) {
            static_cast<void>(buffer.Append("ECS "));
            static_cast<void>(buffer.AppendFloat(milliseconds, 2));
            static_cast<void>(buffer.Append(" ms"));
        } else {
            static_cast<void>(buffer.Append("ECS --"));
        }
        return buffer.View();
    }
};

} // namespace kb::editor
