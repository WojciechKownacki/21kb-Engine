#pragma once

#include <cstdint>

// Fixed-width, versioned native plugin boundary. No graphics-library or STL
// objects cross it. Call arguments are borrowed for that call; the factory
// HostApi remains valid until the plugin's onDestroy callback returns.
namespace kb::render::effects {
inline constexpr std::uint32_t ApiVersion = 1U;
using Handle = std::uint64_t;
enum class BufferKind : std::uint32_t { InputUint, StorageUint, InputVec4, StorageVec4 };
enum class Access : std::uint32_t { Read, Write, ReadWrite };
enum class BindingKind : std::uint32_t { Buffer, Image, SampledTexture };
struct Binding {
    Handle resource = 0U;
    Handle sampler = 0U;
    std::uint32_t slot = 0U;
    BindingKind kind = BindingKind::Buffer;
    Access access = Access::Read;
};
struct UniformValue { Handle uniform = 0U; float value[4]{}; };
struct Dispatch {
    Handle program = 0U;
    const Binding* bindings = nullptr;
    std::uint32_t bindingCount = 0U;
    const UniformValue* uniforms = nullptr;
    std::uint32_t uniformCount = 0U;
    std::uint32_t groups[3]{1U,1U,1U};
};
struct Frame {
    std::uint32_t size = sizeof(Frame);
    std::uint32_t width = 0U, height = 0U, completedFrame = 0U;
    float deltaSeconds = 0.0F;
    Handle sceneColor = 0U; // Borrowed sampled input; valid only inside onFrame.
};
struct HostApi {
    std::uint32_t size = sizeof(HostApi), version = ApiVersion;
    void* context = nullptr;
    Handle (*createProgram)(void*, const char*) = nullptr;
    Handle (*createBuffer)(void*, BufferKind, std::uint32_t, const void*, std::uint32_t) = nullptr;
    Handle (*createTexture)(void*, std::uint32_t, std::uint32_t) = nullptr;
    Handle (*createUniform)(void*, const char*, bool sampler) = nullptr;
    bool (*updateBuffer)(void*, Handle, const void*, std::uint32_t) = nullptr;
    bool (*dispatch)(void*, const Dispatch*) = nullptr;
    bool (*present)(void*, Handle) = nullptr;
    Handle (*readBuffer)(void*, Handle) = nullptr;
    // 0 pending, 1 copied, -1 invalid. Host storage outlives plugin unload.
    std::int32_t (*pollReadback)(void*, Handle, void*, std::uint32_t) = nullptr;
    bool (*release)(void*, Handle) = nullptr;
    const char* (*lastError)(void*) = nullptr;
};
struct Plugin {
    std::uint32_t size = sizeof(Plugin), version = ApiVersion;
    void* instance = nullptr;
    bool (*onFrame)(void*, const Frame*) = nullptr;
    void (*onDestroy)(void*) = nullptr;
    std::uint64_t (*status)(void*) = nullptr;
};
using CreatePlugin = bool (*)(const HostApi*, Plugin*);
inline constexpr const char* CreateSymbol = "kb_create_render_effect_v1";
} // namespace kb::render::effects
