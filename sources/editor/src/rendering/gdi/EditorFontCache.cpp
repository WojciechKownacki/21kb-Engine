#include "rendering/gdi/EditorFontCache.hpp"

#if defined(_WIN32)
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace kb::editor {
namespace {

[[nodiscard]] std::uint64_t FontKey(int pointSize, int weight) noexcept {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(pointSize)) << 32U) |
        static_cast<std::uint32_t>(weight);
}

[[nodiscard]] HFONT CreateEditorFont(int pointSize, int weight) {
    return CreateFontW(
        -pointSize, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

} // namespace

HFONT EditorFontCache::Acquire(int pointSize, int weight) {
    // Panels are painted from the message loop, but thumbnail and preview rasters are
    // drawn off it, so the table is guarded.
    static std::mutex guard;
    static std::unordered_map<std::uint64_t, HFONT> fonts;

    const std::uint64_t key = FontKey(pointSize, weight);
    const std::lock_guard<std::mutex> lock{ guard };
    const auto existing = fonts.find(key);
    if (existing != fonts.end()) {
        return existing->second;
    }
    HFONT font = CreateEditorFont(pointSize, weight);
    fonts.emplace(key, font);
    return font;
}

} // namespace kb::editor

#endif
