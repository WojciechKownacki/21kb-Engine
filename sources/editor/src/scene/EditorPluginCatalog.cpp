#include "scene/EditorPluginCatalog.hpp"

#include <array>
#include <filesystem>

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

#if !defined(KB_BASIC_LIGHTING_PLUGIN_PATH)
#if defined(_WIN32)
#define KB_BASIC_LIGHTING_PLUGIN_PATH "kb_basic_lighting_plugin.dll"
#else
#define KB_BASIC_LIGHTING_PLUGIN_PATH "libkb_basic_lighting_plugin.so"
#endif
#endif

constexpr std::array<EditorPluginDescriptor, 3> kPlugins{{
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
    EditorPluginDescriptor{
        .id = "Rendering.BasicLighting",
        .displayName = "Basic Lighting",
        .category = "Rendering",
        .provider = "21kb",
        .description = "Directional, point and spot lighting provider for scene rendering.",
        .binaryPath = KB_BASIC_LIGHTING_PLUGIN_PATH,
    },
}};

} // namespace

std::size_t EditorPluginCatalog::Count() noexcept {
    return kPlugins.size();
}

const EditorPluginDescriptor* EditorPluginCatalog::At(std::size_t index) noexcept {
    return index < kPlugins.size() ? &kPlugins[index] : nullptr;
}

const EditorPluginDescriptor* EditorPluginCatalog::FindById(std::string_view id) noexcept {
    for (const EditorPluginDescriptor& descriptor : kPlugins) {
        if (descriptor.id == id) {
            return &descriptor;
        }
    }
    return nullptr;
}

std::string EditorPluginCatalog::PersistentBinaryPath(std::string_view pluginId) {
    const EditorPluginDescriptor* descriptor = FindById(pluginId);
    if (descriptor == nullptr || descriptor->binaryPath.empty()) {
        return {};
    }
    return std::filesystem::path{ descriptor->binaryPath }.filename().string();
}

bool EditorPluginCatalog::NormalizeProjectPluginReference(kb::project::ProjectPluginReference& plugin) {
    const EditorPluginDescriptor* descriptor = FindById(plugin.name);
    if (descriptor == nullptr) {
        return false;
    }

    const std::string normalized = PersistentBinaryPath(plugin.name);
    if (normalized.empty() || plugin.binaryPath == normalized) {
        return false;
    }

    plugin.binaryPath = normalized;
    return true;
}

} // namespace kb::editor
