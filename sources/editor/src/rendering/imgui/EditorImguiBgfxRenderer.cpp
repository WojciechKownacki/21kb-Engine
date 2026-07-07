#include "rendering/imgui/EditorImguiBgfxRenderer.hpp"

#include <bgfx/embedded_shader.h>
#include <bx/math.h>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../../../../../third_party/bgfx/examples/common/imgui/vs_ocornut_imgui.bin.h"
#include "../../../../../third_party/bgfx/examples/common/imgui/fs_ocornut_imgui.bin.h"

namespace kb::editor {
namespace {

constexpr std::uint32_t kImguiClearRgba = 0x05070CFFU;

const bgfx::EmbeddedShader kEmbeddedShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER_END()
};

[[nodiscard]] ImTextureID TextureIdFromHandle(bgfx::TextureHandle handle) noexcept {
    if (!bgfx::isValid(handle)) {
        return nullptr;
    }
    return reinterpret_cast<ImTextureID>(static_cast<intptr_t>(handle.idx) + 1);
}

[[nodiscard]] bgfx::TextureHandle TextureHandleFromId(ImTextureID textureId) noexcept {
    const intptr_t raw = reinterpret_cast<intptr_t>(textureId);
    if (raw <= 0 || raw > 0x10000) {
        return BGFX_INVALID_HANDLE;
    }
    return bgfx::TextureHandle{ static_cast<std::uint16_t>(raw - 1) };
}

[[nodiscard]] bool EnsureTransientBuffers(std::uint32_t vertexCount, const bgfx::VertexLayout& layout, std::uint32_t indexCount) noexcept {
    return vertexCount == bgfx::getAvailTransientVertexBuffer(vertexCount, layout) &&
           indexCount == bgfx::getAvailTransientIndexBuffer(indexCount, sizeof(ImDrawIdx) == 4);
}

[[nodiscard]] std::vector<std::uint32_t> ConvertBgraToRgba(std::span<const std::uint32_t> bgraPixels) {
    std::vector<std::uint32_t> rgba;
    rgba.reserve(bgraPixels.size());
    for (const std::uint32_t bgra : bgraPixels) {
        const std::uint32_t b = bgra & 0xFFU;
        const std::uint32_t g = (bgra >> 8U) & 0xFFU;
        const std::uint32_t r = (bgra >> 16U) & 0xFFU;
        const std::uint32_t a = (bgra >> 24U) & 0xFFU;
        rgba.push_back(r | (g << 8U) | (b << 16U) | (a << 24U));
    }
    return rgba;
}

} // namespace

EditorImguiBgfxRenderer::~EditorImguiBgfxRenderer() {
    Shutdown();
}

bool EditorImguiBgfxRenderer::Initialize() {
    if (context_ != nullptr) {
        return true;
    }

    IMGUI_CHECKVERSION();
    context_ = ImGui::CreateContext();
    if (context_ == nullptr) {
        return false;
    }

    ImGui::SetCurrentContext(context_);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.BackendRendererName = "21kb Editor bgfx ImGui";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    ApplyStyle();

    const bool created = CreateDeviceObjects();
    ImGui::SetCurrentContext(nullptr);
    if (!created) {
        Shutdown();
        return false;
    }
    return true;
}

void EditorImguiBgfxRenderer::Shutdown() noexcept {
    if (context_ != nullptr) {
        ImGui::SetCurrentContext(context_);
        DestroyDeviceObjects();
        ImGui::DestroyContext(context_);
        context_ = nullptr;
        ImGui::SetCurrentContext(nullptr);
    }
}

bool EditorImguiBgfxRenderer::BeginFrame(
    std::uint32_t width,
    std::uint32_t height,
    float deltaSeconds,
    const EditorImguiInputState& input) {
    if (!IsInitialized() || width == 0U || height == 0U) {
        return false;
    }

    ImGui::SetCurrentContext(context_);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(1.0F, 1.0F);
    io.DeltaTime = std::max(1.0F / 240.0F, deltaSeconds);
    io.AddMousePosEvent(input.mouseVisible ? input.mouseX : -FLT_MAX, input.mouseVisible ? input.mouseY : -FLT_MAX);
    io.AddMouseButtonEvent(0, input.leftMouseDown);
    io.AddMouseButtonEvent(1, input.rightMouseDown);
    io.AddMouseButtonEvent(2, input.middleMouseDown);
    if (input.mouseWheel != 0.0F) {
        io.AddMouseWheelEvent(0.0F, input.mouseWheel);
    }
    ImGui::NewFrame();
    return true;
}

bool EditorImguiBgfxRenderer::SubmitFrame(bgfx::ViewId viewId, bgfx::FrameBufferHandle frameBuffer) {
    if (!IsInitialized()) {
        return false;
    }

    ImGui::SetCurrentContext(context_);
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData == nullptr) {
        return false;
    }

    SubmitDrawData(*drawData, viewId, frameBuffer);
    ImGui::SetCurrentContext(nullptr);
    return true;
}

void EditorImguiBgfxRenderer::ClearCurrentContext() noexcept {
    if (context_ != nullptr) {
        ImGui::SetCurrentContext(nullptr);
    }
}

ImTextureID EditorImguiBgfxRenderer::EnsureTexture(
    std::uint64_t cacheKey,
    std::uint64_t contentHash,
    int width,
    int height,
    std::span<const std::uint32_t> bgraPixels) {
    if (!IsInitialized() ||
        cacheKey == 0U ||
        width <= 0 ||
        height <= 0 ||
        width > 65535 ||
        height > 65535 ||
        bgraPixels.size() != static_cast<std::size_t>(width * height)) {
        return nullptr;
    }

    if (contentHash == 0U) {
        contentHash = cacheKey;
    }
    auto entry = std::ranges::find_if(textureCache_, [cacheKey](const TextureCacheEntry& candidate) {
        return candidate.cacheKey == cacheKey;
    });
    if (entry != textureCache_.end() &&
        entry->width == width &&
        entry->height == height &&
        entry->contentHash == contentHash &&
        bgfx::isValid(entry->texture)) {
        return TextureIdFromHandle(entry->texture);
    }

    std::vector<std::uint32_t> rgbaPixels = ConvertBgraToRgba(bgraPixels);
    const bgfx::Memory* memory = bgfx::copy(rgbaPixels.data(), static_cast<std::uint32_t>(rgbaPixels.size() * sizeof(std::uint32_t)));
    bgfx::TextureHandle texture = bgfx::createTexture2D(
        static_cast<std::uint16_t>(width),
        static_cast<std::uint16_t>(height),
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
        memory);
    if (!bgfx::isValid(texture)) {
        return nullptr;
    }

    if (entry == textureCache_.end()) {
        textureCache_.push_back(TextureCacheEntry{
            .cacheKey = cacheKey,
            .contentHash = contentHash,
            .width = width,
            .height = height,
            .texture = texture,
        });
    } else {
        if (bgfx::isValid(entry->texture)) {
            bgfx::destroy(entry->texture);
        }
        *entry = TextureCacheEntry{
            .cacheKey = cacheKey,
            .contentHash = contentHash,
            .width = width,
            .height = height,
            .texture = texture,
        };
    }
    return TextureIdFromHandle(texture);
}

bool EditorImguiBgfxRenderer::IsInitialized() const noexcept {
    return context_ != nullptr && bgfx::isValid(textureUniform_) && bgfx::isValid(program_) && bgfx::isValid(fontTexture_);
}

bool EditorImguiBgfxRenderer::CreateDeviceObjects() {
    vertexLayout_
        .begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    textureUniform_ = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(textureUniform_)) {
        return false;
    }

    const bgfx::RendererType::Enum rendererType = bgfx::getRendererType();
    bgfx::ShaderHandle vertexShader = bgfx::createEmbeddedShader(kEmbeddedShaders, rendererType, "vs_ocornut_imgui");
    bgfx::ShaderHandle fragmentShader = bgfx::createEmbeddedShader(kEmbeddedShaders, rendererType, "fs_ocornut_imgui");
    if (!bgfx::isValid(vertexShader) || !bgfx::isValid(fragmentShader)) {
        if (bgfx::isValid(vertexShader)) {
            bgfx::destroy(vertexShader);
        }
        if (bgfx::isValid(fragmentShader)) {
            bgfx::destroy(fragmentShader);
        }
        return false;
    }

    program_ = bgfx::createProgram(vertexShader, fragmentShader, true);
    if (!bgfx::isValid(program_)) {
        return false;
    }
    return CreateFontTexture();
}

bool EditorImguiBgfxRenderer::CreateFontTexture() {
    if (context_ == nullptr) {
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFontConfig fontConfig{};
    fontConfig.OversampleH = 2;
    fontConfig.OversampleV = 2;
    fontConfig.PixelSnapH = false;
    ImFont* uiFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 15.0F, &fontConfig);
    if (uiFont == nullptr) {
        uiFont = io.Fonts->AddFontDefault();
    }
    io.FontDefault = uiFont;

    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    const bgfx::Memory* memory = bgfx::copy(pixels, static_cast<std::uint32_t>(width * height * 4));
    fontTexture_ = bgfx::createTexture2D(
        static_cast<std::uint16_t>(width),
        static_cast<std::uint16_t>(height),
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
        memory);
    if (!bgfx::isValid(fontTexture_)) {
        return false;
    }
    io.Fonts->SetTexID(TextureIdFromHandle(fontTexture_));
    return true;
}

void EditorImguiBgfxRenderer::DestroyDeviceObjects() noexcept {
    for (TextureCacheEntry& entry : textureCache_) {
        if (bgfx::isValid(entry.texture)) {
            bgfx::destroy(entry.texture);
            entry.texture = BGFX_INVALID_HANDLE;
        }
    }
    textureCache_.clear();
    if (bgfx::isValid(fontTexture_)) {
        bgfx::destroy(fontTexture_);
        fontTexture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(program_)) {
        bgfx::destroy(program_);
        program_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(textureUniform_)) {
        bgfx::destroy(textureUniform_);
        textureUniform_ = BGFX_INVALID_HANDLE;
    }
}

void EditorImguiBgfxRenderer::ApplyStyle() noexcept {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0F;
    style.ChildRounding = 0.0F;
    style.FrameRounding = 3.0F;
    style.PopupRounding = 4.0F;
    style.ScrollbarRounding = 3.0F;
    style.GrabRounding = 3.0F;
    style.WindowBorderSize = 0.0F;
    style.ChildBorderSize = 0.0F;
    style.FrameBorderSize = 1.0F;
    style.WindowPadding = ImVec2(0.0F, 0.0F);
    style.ItemSpacing = ImVec2(8.0F, 6.0F);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.93F, 0.96F, 1.0F, 1.0F);
    colors[ImGuiCol_WindowBg] = ImVec4(0.025F, 0.032F, 0.044F, 1.0F);
    colors[ImGuiCol_ChildBg] = ImVec4(0.025F, 0.032F, 0.044F, 1.0F);
    colors[ImGuiCol_Border] = ImVec4(0.16F, 0.20F, 0.27F, 1.0F);
    colors[ImGuiCol_FrameBg] = ImVec4(0.06F, 0.075F, 0.10F, 1.0F);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.09F, 0.13F, 0.17F, 1.0F);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.12F, 0.18F, 0.23F, 1.0F);
    colors[ImGuiCol_Header] = ImVec4(0.08F, 0.28F, 0.30F, 1.0F);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.10F, 0.38F, 0.40F, 1.0F);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.13F, 0.47F, 0.48F, 1.0F);
}

void EditorImguiBgfxRenderer::SubmitDrawData(ImDrawData& drawData, bgfx::ViewId viewId, bgfx::FrameBufferHandle frameBuffer) noexcept {
    const int framebufferWidth = static_cast<int>(drawData.DisplaySize.x * drawData.FramebufferScale.x);
    const int framebufferHeight = static_cast<int>(drawData.DisplaySize.y * drawData.FramebufferScale.y);
    if (framebufferWidth <= 0 || framebufferHeight <= 0) {
        return;
    }

    const bgfx::Caps* caps = bgfx::getCaps();
    float ortho[16]{};
    bx::mtxOrtho(
        ortho,
        drawData.DisplayPos.x,
        drawData.DisplayPos.x + drawData.DisplaySize.x,
        drawData.DisplayPos.y + drawData.DisplaySize.y,
        drawData.DisplayPos.y,
        0.0F,
        1000.0F,
        0.0F,
        caps != nullptr && caps->homogeneousDepth);

    bgfx::setViewName(viewId, "KB Material ImGui Graph");
    bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);
    bgfx::setViewFrameBuffer(viewId, frameBuffer);
    bgfx::setViewTransform(viewId, nullptr, ortho);
    bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR, kImguiClearRgba);
    bgfx::setViewRect(viewId, 0, 0, static_cast<std::uint16_t>(framebufferWidth), static_cast<std::uint16_t>(framebufferHeight));
    bgfx::touch(viewId);

    const ImVec2 clipPos = drawData.DisplayPos;
    const ImVec2 clipScale = drawData.FramebufferScale;
    for (int listIndex = 0; listIndex < drawData.CmdListsCount; ++listIndex) {
        const ImDrawList* drawList = drawData.CmdLists[listIndex];
        if (drawList == nullptr) {
            continue;
        }

        const std::uint32_t vertexCount = static_cast<std::uint32_t>(drawList->VtxBuffer.Size);
        const std::uint32_t indexCount = static_cast<std::uint32_t>(drawList->IdxBuffer.Size);
        if (vertexCount == 0U || indexCount == 0U || !EnsureTransientBuffers(vertexCount, vertexLayout_, indexCount)) {
            continue;
        }

        bgfx::TransientVertexBuffer vertexBuffer{};
        bgfx::TransientIndexBuffer indexBuffer{};
        bgfx::allocTransientVertexBuffer(&vertexBuffer, vertexCount, vertexLayout_);
        bgfx::allocTransientIndexBuffer(&indexBuffer, indexCount, sizeof(ImDrawIdx) == 4);
        std::memcpy(vertexBuffer.data, drawList->VtxBuffer.Data, vertexCount * sizeof(ImDrawVert));
        std::memcpy(indexBuffer.data, drawList->IdxBuffer.Data, indexCount * sizeof(ImDrawIdx));

        bgfx::Encoder* encoder = bgfx::begin();
        if (encoder == nullptr) {
            continue;
        }

        for (const ImDrawCmd& command : drawList->CmdBuffer) {
            if (command.UserCallback != nullptr) {
                command.UserCallback(drawList, &command);
                continue;
            }
            if (command.ElemCount == 0U) {
                continue;
            }

            ImVec4 clipRect;
            clipRect.x = (command.ClipRect.x - clipPos.x) * clipScale.x;
            clipRect.y = (command.ClipRect.y - clipPos.y) * clipScale.y;
            clipRect.z = (command.ClipRect.z - clipPos.x) * clipScale.x;
            clipRect.w = (command.ClipRect.w - clipPos.y) * clipScale.y;
            if (clipRect.x >= static_cast<float>(framebufferWidth) || clipRect.y >= static_cast<float>(framebufferHeight) || clipRect.z < 0.0F || clipRect.w < 0.0F) {
                continue;
            }

            const std::uint16_t scissorX = static_cast<std::uint16_t>(std::max(clipRect.x, 0.0F));
            const std::uint16_t scissorY = static_cast<std::uint16_t>(std::max(clipRect.y, 0.0F));
            const std::uint16_t scissorW = static_cast<std::uint16_t>(std::min(clipRect.z, 65535.0F) - static_cast<float>(scissorX));
            const std::uint16_t scissorH = static_cast<std::uint16_t>(std::min(clipRect.w, 65535.0F) - static_cast<float>(scissorY));
            if (scissorW == 0U || scissorH == 0U) {
                continue;
            }

            bgfx::TextureHandle texture = TextureHandleFromId(command.GetTexID());
            if (!bgfx::isValid(texture)) {
                texture = fontTexture_;
            }

            encoder->setScissor(scissorX, scissorY, scissorW, scissorH);
            encoder->setState(
                BGFX_STATE_WRITE_RGB |
                BGFX_STATE_WRITE_A |
                BGFX_STATE_MSAA |
                BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
            encoder->setTexture(0, textureUniform_, texture);
            encoder->setVertexBuffer(0, &vertexBuffer, command.VtxOffset, vertexCount);
            encoder->setIndexBuffer(&indexBuffer, command.IdxOffset, command.ElemCount);
            encoder->submit(viewId, program_);
        }

        bgfx::end(encoder);
    }
}

} // namespace kb::editor
