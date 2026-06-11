#include "scene/EditorPluginCatalog.hpp"

#include <array>

namespace kb::editor {
namespace {

#if !defined(KB_PHYSICS_JOLT_PLUGIN_PATH)
#if defined(_WIN32)
#define KB_PHYSICS_JOLT_PLUGIN_PATH "kb_physics_jolt_plugin.dll"
#else
#define KB_PHYSICS_JOLT_PLUGIN_PATH "libkb_physics_jolt_plugin.so"
#endif
#endif

#if !defined(KB_AUDIO_MINIAUDIO_PLUGIN_PATH)
#if defined(_WIN32)
#define KB_AUDIO_MINIAUDIO_PLUGIN_PATH "kb_audio_miniaudio_plugin.dll"
#else
#define KB_AUDIO_MINIAUDIO_PLUGIN_PATH "libkb_audio_miniaudio_plugin.so"
#endif
#endif

constexpr std::array<EditorPluginDescriptor, 2> kPlugins{{
    EditorPluginDescriptor{
        .id = "Physics.Jolt",
        .displayName = "Jolt Physics",
        .category = "Physics",
        .provider = "Jolt",
        .description = "3D rigidbody physics provider loaded as an engine plugin.",
        .binaryPath = KB_PHYSICS_JOLT_PLUGIN_PATH,
    },
    EditorPluginDescriptor{
        .id = "Audio.Miniaudio",
        .displayName = "Miniaudio",
        .category = "Audio",
        .provider = "miniaudio",
        .description = "Audio playback provider loaded as an engine plugin.",
        .binaryPath = KB_AUDIO_MINIAUDIO_PLUGIN_PATH,
    },
}};

} // namespace

std::size_t EditorPluginCatalog::Count() noexcept {
    return kPlugins.size();
}

const EditorPluginDescriptor* EditorPluginCatalog::At(std::size_t index) noexcept {
    return index < kPlugins.size() ? &kPlugins[index] : nullptr;
}

} // namespace kb::editor
