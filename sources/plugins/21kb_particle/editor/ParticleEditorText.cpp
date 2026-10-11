#include "ParticleEditorText.hpp"

#include <atomic>

namespace kb::particle_editor {
namespace {

[[nodiscard]] std::atomic<ParticleTextSource>& Source() noexcept {
    static std::atomic<ParticleTextSource> source{ nullptr };
    return source;
}

} // namespace

void SetTextSource(ParticleTextSource source) noexcept {
    Source().store(source, std::memory_order_release);
}

std::string_view ParticleText(std::string_view key, std::string_view english) {
    const ParticleTextSource source = Source().load(std::memory_order_acquire);
    return source != nullptr ? source(key, english) : english;
}

std::string ParticleFormat(std::string_view key, std::string_view english,
    std::initializer_list<std::pair<std::string_view, std::string>> values) {
    std::string text{ ParticleText(key, english) };
    for (const auto& [name, value] : values) {
        const std::string marker = "{" + std::string{ name } + "}";
        for (std::size_t at = text.find(marker); at != std::string::npos; at = text.find(marker, at + value.size())) {
            text.replace(at, marker.size(), value);
        }
    }
    return text;
}

} // namespace kb::particle_editor
