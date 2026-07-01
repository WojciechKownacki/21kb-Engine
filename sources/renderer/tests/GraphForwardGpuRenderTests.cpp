#include "RendererTestSupport.hpp"

#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialGraphProgramBindingBuilder.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace kb::render::tests {
namespace {

#if defined(KB_TEST_GRAPH_SHADERC_PATH)

struct ForwardRenderProbe {
    std::uint8_t r = 0U;
    std::uint8_t g = 0U;
    std::uint8_t b = 0U;
    std::uint8_t a = 0U;
};

[[nodiscard]] std::vector<std::uint8_t> ReadAllBytes(const std::filesystem::path& path) {
    std::ifstream file{ path, std::ios::binary | std::ios::ate };
    if (!file.is_open()) {
        return {};
    }
    const std::streamsize size = file.tellg();
    if (size <= 0) {
        return {};
    }
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

[[nodiscard]] bool CookHarnessVertexShader(const std::filesystem::path& outBin) {
    const std::filesystem::path src = outBin.parent_path() / "vs_graph_probe.sc";
    {
        std::ofstream out{ src, std::ios::binary | std::ios::trunc };
        out <<
            "$input a_position, a_texcoord0\n"
            "$output v_normal, v_color0, v_texcoord0, v_worldPos, v_shadowPos, v_shadowFlags, v_tangent, v_bitangent, v_objectLocalPos, v_objectWorldPos\n\n"
            "#include <bgfx_shader.sh>\n\n"
            "uniform vec4 u_time;\n\n"
            "void main()\n{\n"
            "    gl_Position = vec4(a_position, 1.0);\n"
            "    v_normal = vec3(0.0, 0.0, 1.0);\n"
            "    v_color0 = vec4(0.15, 0.45, 0.85, 0.6);\n"
            "    v_texcoord0 = a_texcoord0;\n"
            "    v_worldPos = vec3(a_position.xy, 0.0);\n"
            "    v_shadowPos = vec4(0.0, 0.0, 0.0, 1.0);\n"
            "    v_shadowFlags = vec4(0.0, 0.0, a_texcoord0.x * 0.1, a_texcoord0.y * 0.1);\n"
            "    v_tangent = vec3(1.0, 0.0, 0.0);\n"
            "    v_bitangent = vec3(0.0, 1.0, 0.0);\n"
            // MAT-76: simulate an object translated by (-0.7,0,0); object-space local position
            // differs from world position by a known positive offset so the proof can tell them apart.
            // MAT-77: the per-instance scalar lanes (.w) are driven by u_time.y/.z so a test can inject
            // distinct per-instance values exactly as the real instance buffer packs into i_data0/1.w.
            "    v_objectLocalPos = vec4(v_worldPos.x + 0.7, v_worldPos.y, v_worldPos.z, u_time.y);\n"
            "    v_objectWorldPos = vec4(0.7, 0.0, 0.0, u_time.z);\n"
            "}\n";
    }
    std::ostringstream command;
    command << '"' << '"' << KB_TEST_GRAPH_SHADERC_PATH << '"'
        << " --type vertex --platform windows --profile s_5_0"
        << " -f \"" << src.generic_string() << '"'
        << " -o \"" << outBin.generic_string() << '"'
        << " --varyingdef \"" << KB_TEST_GRAPH_SHADER_VARYING_DEF << '"'
        << " -i \"" << KB_TEST_GRAPH_SHADER_INCLUDE_DIR << '"'
        << " -i \"" << KB_TEST_GRAPH_BGFX_SHADER_INCLUDE_DIR << '"'
        << " -O 3" << '"';
    std::error_code error;
    std::filesystem::remove(outBin, error);
    const int code = std::system(command.str().c_str());
    return code == 0 && std::filesystem::exists(outBin, error) && std::filesystem::file_size(outBin, error) > 0U;
}

// MAT-81: a generated vertex shader that embeds the graph source, evaluates EvaluateWorldPositionOffset
// with a vertex-populated context, and offsets the position before projection.
[[nodiscard]] bool CookHarnessWorldPositionOffsetVertexShader(const std::filesystem::path& outBin, const std::string& graphSource) {
    const std::filesystem::path src = outBin.parent_path() / "vs_graph_wpo_probe.sc";
    {
        std::ofstream out{ src, std::ios::binary | std::ios::trunc };
        out <<
            "$input a_position, a_texcoord0\n"
            "$output v_normal, v_color0, v_texcoord0, v_worldPos, v_shadowPos, v_shadowFlags, v_tangent, v_bitangent, v_objectLocalPos, v_objectWorldPos\n\n"
            "#include <bgfx_shader.sh>\n\n"
            "uniform vec4 u_time;\n\n"
            << graphSource <<
            "\nvoid main()\n{\n"
            "    MaterialGraphContext ctx;\n"
            "    ctx.uv0 = a_texcoord0;\n"
            "    ctx.uv1 = vec2(0.0, 0.0);\n"
            "    ctx.normal = vec3(0.0, 0.0, 1.0);\n"
            "    ctx.tangent = vec3(1.0, 0.0, 0.0);\n"
            "    ctx.bitangent = vec3(0.0, 1.0, 0.0);\n"
            "    ctx.worldPos = a_position;\n"
            "    ctx.viewDir = vec3(0.0, 0.0, 1.0);\n"
            "    ctx.vertexColor = vec4(1.0, 1.0, 1.0, 1.0);\n"
            "    ctx.time = u_time.x;\n"
            "    ctx.screenPosition = vec2(0.0, 0.0);\n"
            "    ctx.localPosition = a_position;\n"
            "    ctx.objectPosition = vec3(0.0, 0.0, 0.0);\n"
            "    ctx.perInstanceRandom = 0.0;\n"
            "    ctx.objectRadius = 0.0;\n"
            "    vec3 worldPos = a_position + EvaluateWorldPositionOffset(ctx);\n"
            "    gl_Position = vec4(worldPos, 1.0);\n"
            "    v_normal = vec3(0.0, 0.0, 1.0);\n"
            "    v_color0 = vec4(0.2, 0.6, 0.9, 1.0);\n"
            "    v_texcoord0 = a_texcoord0;\n"
            "    v_worldPos = worldPos;\n"
            "    v_shadowPos = vec4(0.0, 0.0, 0.0, 1.0);\n"
            "    v_shadowFlags = vec4(0.0, 0.0, 0.0, 0.0);\n"
            "    v_tangent = vec3(1.0, 0.0, 0.0);\n"
            "    v_bitangent = vec3(0.0, 1.0, 0.0);\n"
            "    v_objectLocalPos = vec4(a_position, 0.0);\n"
            "    v_objectWorldPos = vec4(0.0, 0.0, 0.0, 0.0);\n"
            "}\n";
    }
    std::ostringstream command;
    command << '"' << '"' << KB_TEST_GRAPH_SHADERC_PATH << '"'
        << " --type vertex --platform windows --profile s_5_0"
        << " -f \"" << src.generic_string() << '"'
        << " -o \"" << outBin.generic_string() << '"'
        << " --varyingdef \"" << KB_TEST_GRAPH_SHADER_VARYING_DEF << '"'
        << " -i \"" << KB_TEST_GRAPH_SHADER_INCLUDE_DIR << '"'
        << " -i \"" << KB_TEST_GRAPH_BGFX_SHADER_INCLUDE_DIR << '"'
        << " -O 3" << '"';
    std::error_code error;
    std::filesystem::remove(outBin, error);
    const int code = std::system(command.str().c_str());
    return code == 0 && std::filesystem::exists(outBin, error) && std::filesystem::file_size(outBin, error) > 0U;
}

[[nodiscard]] RenderMaterialGraphLink MakeLink(RenderMaterialGraphNodeKind fromKind, std::uint32_t fromNode, std::string fromPin, RenderMaterialGraphNodeKind toKind, std::uint32_t toNode, std::string toPin) {
    RenderMaterialGraphLink link{
        .fromNodeId = fromNode,
        .fromPinId = RenderMaterialGraphStablePinId(fromKind, fromPin, true),
        .fromPin = std::move(fromPin),
        .toNodeId = toNode,
        .toPinId = RenderMaterialGraphStablePinId(toKind, toPin, false),
        .toPin = std::move(toPin),
    };
    link.id = MakeRenderMaterialGraphLinkId(link);
    return link;
}

[[nodiscard]] RenderMaterialGraphShaderArtifactRequest CookRequest(const std::string& cacheRoot) {
    RenderMaterialGraphShaderArtifactRequest request{};
    request.shadercPath = KB_TEST_GRAPH_SHADERC_PATH;
    request.varyingDefPath = KB_TEST_GRAPH_SHADER_VARYING_DEF;
    request.includeDirs = { KB_TEST_GRAPH_SHADER_INCLUDE_DIR, KB_TEST_GRAPH_BGFX_SHADER_INCLUDE_DIR };
    request.cacheRoot = cacheRoot;
    request.pass = "BaseOpaque";
    return request;
}

class ForwardRenderHarness {
public:
    [[nodiscard]] bool Init() {
#if defined(_WIN32)
        window_ = CreateWindowExW(0, L"STATIC", L"mat08", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
#endif
        bgfx::Init init;
        init.type = bgfx::RendererType::Direct3D11;
        init.resolution.width = 64U;
        init.resolution.height = 64U;
        init.resolution.reset = BGFX_RESET_NONE;
#if defined(_WIN32)
        init.platformData.nwh = window_;
#endif
        if (!bgfx::init(init)) {
            return false;
        }
        layout_.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float).end();
        const std::array<float, 15U> tri{
            -1.0F, -3.0F, 0.0F, 0.0F, 2.0F,
            -1.0F,  1.0F, 0.0F, 0.0F, 0.0F,
             3.0F,  1.0F, 0.0F, 2.0F, 0.0F,
        };
        vbh_ = bgfx::createVertexBuffer(bgfx::copy(tri.data(), sizeof(tri)), layout_);
        rt_ = bgfx::createTexture2D(64U, 64U, false, 1U, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | BGFX_TEXTURE_BLIT_DST);
        readTex_ = bgfx::createTexture2D(64U, 64U, false, 1U, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
        fb_ = bgfx::createFrameBuffer(1U, &rt_, false);
        uCamera_ = bgfx::createUniform("u_cameraPosition", bgfx::UniformType::Vec4);
        uLightParams_ = bgfx::createUniform("u_lightParams", bgfx::UniformType::Vec4);
        uAmbient_ = bgfx::createUniform("u_ambientColor", bgfx::UniformType::Vec4);
        uEnvParams_ = bgfx::createUniform("u_environmentParams", bgfx::UniformType::Vec4);
        uTime_ = bgfx::createUniform("u_time", bgfx::UniformType::Vec4);
        bgfx::frame();
        bgfx::frame();
        return bgfx::isValid(vbh_) && bgfx::isValid(fb_);
    }

    [[nodiscard]] std::vector<std::uint8_t> RenderPixels(bgfx::ProgramHandle program, bgfx::UniformHandle sampler, bgfx::TextureHandle texture, float time = 0.0F, float instanceRandom = 0.0F, float objectRadius = 0.0F, std::uint32_t samplerFlags = UINT32_MAX, std::uint32_t clearColor = 0x000000ffU, std::uint64_t extraState = 0U) {
        const std::array<float, 4U> camera{ 0.0F, 0.0F, 1.0F, 0.0F };
        const std::array<float, 4U> lightParams{ 0.0F, 0.0F, 0.0F, 0.0F };
        const std::array<float, 4U> ambient{ 1.0F, 1.0F, 1.0F, 1.0F };
        const std::array<float, 4U> envParams{ 1.0F, 1.0F, 0.0F, 0.0F };
        // u_time.x = time (MAT-72); .y = per-instance random, .z = object radius (MAT-77 proof lanes).
        const std::array<float, 4U> timeConstants{ time, instanceRandom, objectRadius, 0.0F };
        bgfx::setViewFrameBuffer(0, fb_);
        bgfx::setViewRect(0, 0U, 0U, 64U, 64U);
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR, clearColor, 1.0F, 0);
        bgfx::setUniform(uCamera_, camera.data());
        bgfx::setUniform(uLightParams_, lightParams.data());
        bgfx::setUniform(uAmbient_, ambient.data());
        bgfx::setUniform(uEnvParams_, envParams.data());
        bgfx::setUniform(uTime_, timeConstants.data());
        if (bgfx::isValid(sampler) && bgfx::isValid(texture)) {
            // Graph textures bind at the graph base stage (6); builtin stages 0-5 are reserved (MAT-78).
            bgfx::setTexture(kRenderMaterialGraphTextureBaseSlot, sampler, texture, samplerFlags);
        }
        bgfx::setVertexBuffer(0, vbh_);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | extraState);
        bgfx::submit(0, program);
        bgfx::frame();
        bgfx::blit(1, readTex_, 0U, 0U, rt_, 0U, 0U, 64U, 64U);
        std::vector<std::uint8_t> pixels(64U * 64U * 4U, 0U);
        const std::uint32_t readyFrame = bgfx::readTexture(readTex_, pixels.data());
        std::uint32_t frame = bgfx::frame();
        int guard = 0;
        while (frame < readyFrame && guard < 8) { frame = bgfx::frame(); ++guard; }
        return pixels;
    }

    [[nodiscard]] static ForwardRenderProbe ProbeAt(const std::vector<std::uint8_t>& pixels, std::uint32_t x, std::uint32_t y) {
        const std::size_t idx = (static_cast<std::size_t>(y) * 64U + x) * 4U;
        return ForwardRenderProbe{ pixels[idx], pixels[idx + 1U], pixels[idx + 2U], pixels[idx + 3U] };
    }

    [[nodiscard]] ForwardRenderProbe Render(bgfx::ProgramHandle program, bgfx::UniformHandle sampler, bgfx::TextureHandle texture, float time = 0.0F) {
        return ProbeAt(RenderPixels(program, sampler, texture, time), 32U, 32U);
    }

    void Shutdown() {
        bgfx::destroy(uTime_);
        bgfx::destroy(uEnvParams_);
        bgfx::destroy(uAmbient_);
        bgfx::destroy(uLightParams_);
        bgfx::destroy(uCamera_);
        bgfx::destroy(fb_);
        bgfx::destroy(readTex_);
        bgfx::destroy(rt_);
        bgfx::destroy(vbh_);
        bgfx::shutdown();
#if defined(_WIN32)
        if (window_ != nullptr) { DestroyWindow(window_); window_ = nullptr; }
#endif
    }

private:
#if defined(_WIN32)
    HWND window_ = nullptr;
#endif
    bgfx::VertexLayout layout_{};
    bgfx::VertexBufferHandle vbh_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle rt_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle readTex_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle fb_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uCamera_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uLightParams_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uAmbient_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uEnvParams_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uTime_ = BGFX_INVALID_HANDLE;
};

[[nodiscard]] bgfx::ProgramHandle BuildGraphProgram(const std::vector<std::uint8_t>& vsBytes, const RenderMaterialGraphShaderArtifact& artifact) {
    const RenderMaterialGraphShaderBinary* dxbc = artifact.FindBinary(RenderMaterialGraphShaderBackend::Dxbc);
    if (dxbc == nullptr) {
        return BGFX_INVALID_HANDLE;
    }
    const std::vector<std::uint8_t> fsBytes = ReadAllBytes(std::filesystem::path{ dxbc->binaryPath });
    if (vsBytes.empty() || fsBytes.empty()) {
        return BGFX_INVALID_HANDLE;
    }
    const bgfx::ShaderHandle vsh = bgfx::createShader(bgfx::copy(vsBytes.data(), static_cast<std::uint32_t>(vsBytes.size())));
    const bgfx::ShaderHandle fsh = bgfx::createShader(bgfx::copy(fsBytes.data(), static_cast<std::uint32_t>(fsBytes.size())));
    if (!bgfx::isValid(vsh) || !bgfx::isValid(fsh)) {
        return BGFX_INVALID_HANDLE;
    }
    return bgfx::createProgram(vsh, fsh, true);
}

void RunForwardGraphRenderTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat08_forward_render";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT08: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);

    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    // ConstantColor -> BaseColor renders through the graph program (distinct colors -> distinct pixels).
    const auto cookConstantColor = [&](std::string_view hint) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        RenderMaterialGraphNode constant{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .positionX = 80, .positionY = 80 };
        constant.parameter.defaultValueHint = std::string{ hint };
        graph.nodes.push_back(constant);
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x0800U });
        Require(compiled.Succeeded(), "KBMAT-MAT08: Constant color graph must compile");
        return CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
    };

    const RenderMaterialGraphShaderArtifactResult redResult = cookConstantColor("0.85 0.08 0.05 1");
    const RenderMaterialGraphShaderArtifactResult blueResult = cookConstantColor("0.05 0.08 0.85 1");
    Require(redResult.Succeeded() && blueResult.Succeeded(), "KBMAT-MAT08: Forward graph fragment shaders must cook for the active backend");

    // ImageTexture -> BaseColor samples the texture in the graph shader (per pixel).
    RenderMaterialGraphDocument textureGraph = MakeDefaultRenderMaterialGraphDocument();
    RenderMaterialGraphNode sampleNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::TextureSample, .positionX = 80, .positionY = 80 };
    sampleNode.parameter.stableId = "albedoTex";
    sampleNode.parameter.textureRole = "baseColor";
    sampleNode.parameter.expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb;
    textureGraph.nodes.push_back(sampleNode);
    textureGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::TextureSample, 2U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult textureCompiled = CompileRenderMaterialGraphToShaderSource(textureGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0801U });
    Require(textureCompiled.Succeeded() && textureCompiled.shader.reflection.textures.size() == 1U, "KBMAT-MAT08: Texture graph must compile with a sampler");
    const RenderMaterialGraphShaderArtifactResult textureResult = CookRenderMaterialGraphShaderArtifact(textureCompiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(textureResult.Succeeded(), "KBMAT-MAT08: Texture forward graph fragment shader must cook");
    const std::string samplerName = textureCompiled.shader.reflection.textures[0].samplerName;

    const auto cookGraph = [&](const RenderMaterialGraphDocument& graph, std::uint64_t assetId) {
        const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = assetId });
        Require(compiled.Succeeded(), "KBMAT-MAT08: graph must compile before cooking");
        RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
        Require(result.Succeeded(), "KBMAT-MAT08: graph forward fragment shader must cook");
        return result;
    };

    // ImageTexture * Color -> BaseColor works per pixel (texture math).
    RenderMaterialGraphDocument texMathGraph = MakeDefaultRenderMaterialGraphDocument();
    RenderMaterialGraphNode texMathSample{ .id = 2U, .kind = RenderMaterialGraphNodeKind::TextureSample, .positionX = 40, .positionY = 40 };
    texMathSample.parameter.stableId = "albedoTex";
    texMathSample.parameter.textureRole = "baseColor";
    texMathSample.parameter.expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb;
    RenderMaterialGraphNode texMathTint{ .id = 3U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .positionX = 40, .positionY = 120 };
    texMathTint.parameter.defaultValueHint = "0.85 0.05 0.05 1";
    texMathGraph.nodes.push_back(texMathSample);
    texMathGraph.nodes.push_back(texMathTint);
    texMathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::Multiply, .positionX = 200, .positionY = 80 });
    texMathGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::TextureSample, 2U, "color", RenderMaterialGraphNodeKind::Multiply, 4U, "a"));
    texMathGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba", RenderMaterialGraphNodeKind::Multiply, 4U, "b"));
    texMathGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::Multiply, 4U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphShaderArtifactResult texMathResult = cookGraph(texMathGraph, 0x0802U);

    // Emissive must not be dropped: black base color + red emissive renders red even unlit.
    RenderMaterialGraphDocument emissiveGraph = MakeDefaultRenderMaterialGraphDocument();
    RenderMaterialGraphNode blackBase{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .positionX = 40, .positionY = 40 };
    blackBase.parameter.defaultValueHint = "0 0 0 1";
    RenderMaterialGraphNode redEmissive{ .id = 3U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .positionX = 40, .positionY = 120 };
    redEmissive.parameter.defaultValueHint = "0.9 0.05 0.05 1";
    emissiveGraph.nodes.push_back(blackBase);
    emissiveGraph.nodes.push_back(redEmissive);
    emissiveGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    emissiveGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"));
    const RenderMaterialGraphShaderArtifactResult emissiveResult = cookGraph(emissiveGraph, 0x0803U);

    // Roughness/metallic math changes the BRDF: a metallic surface loses its environment diffuse and renders darker.
    const auto buildWhiteGraph = [&](bool metallic) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        RenderMaterialGraphNode whiteBase{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .positionX = 40, .positionY = 40 };
        whiteBase.parameter.defaultValueHint = "0.9 0.9 0.9 1";
        graph.nodes.push_back(whiteBase);
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        if (metallic) {
            RenderMaterialGraphNode metalScalar{ .id = 3U, .kind = RenderMaterialGraphNodeKind::ConstantScalar, .positionX = 40, .positionY = 140 };
            metalScalar.parameter.defaultValueHint = "1";
            graph.nodes.push_back(metalScalar);
            graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantScalar, 3U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"));
        }
        return graph;
    };
    const RenderMaterialGraphShaderArtifactResult dielectricResult = cookGraph(buildWhiteGraph(false), 0x0804U);
    const RenderMaterialGraphShaderArtifactResult metalResult = cookGraph(buildWhiteGraph(true), 0x0805U);

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT08: Direct3D11 device unavailable; cannot run GPU forward render proof\n");
        Require(false, "KBMAT-MAT08: A real GPU device is required to prove forward graph rendering");
        return;
    }

    const bgfx::ProgramHandle redProgram = BuildGraphProgram(vsBytes, *redResult.artifact);
    const bgfx::ProgramHandle blueProgram = BuildGraphProgram(vsBytes, *blueResult.artifact);
    const bgfx::ProgramHandle textureProgram = BuildGraphProgram(vsBytes, *textureResult.artifact);
    Require(bgfx::isValid(redProgram) && bgfx::isValid(blueProgram) && bgfx::isValid(textureProgram),
        "KBMAT-MAT08: Forward graph programs must link on the real GPU backend");

    const ForwardRenderProbe red = harness.Render(redProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    const ForwardRenderProbe blue = harness.Render(blueProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    Require(red.r > red.g && red.r > red.b && red.r > 80U,
        "KBMAT-MAT08: A red ConstantColor graph must render a red-dominant pixel through the GPU graph program");
    Require(blue.b > blue.r && blue.b > blue.g && blue.b > 80U,
        "KBMAT-MAT08: A blue ConstantColor graph must render a blue-dominant pixel through the GPU graph program");
    Require(red.a == 255U, "KBMAT-MAT08: Surface alpha must drive the rendered output alpha");

    const std::uint32_t greenTexel = 0xff20c020U; // ABGR: opaque green
    bgfx::UniformHandle sampler = bgfx::createUniform(samplerName.c_str(), bgfx::UniformType::Sampler);
    bgfx::TextureHandle greenTexture = bgfx::createTexture2D(1U, 1U, false, 1U, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE, bgfx::copy(&greenTexel, sizeof(greenTexel)));
    const ForwardRenderProbe sampled = harness.Render(textureProgram, sampler, greenTexture);
    Require(sampled.g > sampled.r && sampled.g > sampled.b && sampled.g > 40U,
        "KBMAT-MAT08: An ImageTexture -> BaseColor graph must sample the bound texture per pixel on the GPU");

    // ImageTexture * Color: a white texture modulated by a red constant renders red.
    const std::uint32_t whiteTexel = 0xffffffffU;
    bgfx::TextureHandle whiteTexture = bgfx::createTexture2D(1U, 1U, false, 1U, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE, bgfx::copy(&whiteTexel, sizeof(whiteTexel)));
    const bgfx::ProgramHandle texMathProgram = BuildGraphProgram(vsBytes, *texMathResult.artifact);
    Require(bgfx::isValid(texMathProgram), "KBMAT-MAT08: Texture-math forward graph program must link");
    const ForwardRenderProbe texMath = harness.Render(texMathProgram, sampler, whiteTexture);
    Require(texMath.r > texMath.g && texMath.r > texMath.b && texMath.r > 80U,
        "KBMAT-MAT08: ImageTexture * Color must modulate per pixel (white texture * red tint -> red)");
    bgfx::destroy(whiteTexture);
    bgfx::destroy(greenTexture);
    bgfx::destroy(sampler);

    // Emissive must reach the output: black base color + red emissive renders red.
    const bgfx::ProgramHandle emissiveProgram = BuildGraphProgram(vsBytes, *emissiveResult.artifact);
    Require(bgfx::isValid(emissiveProgram), "KBMAT-MAT08: Emissive forward graph program must link");
    const ForwardRenderProbe emissive = harness.Render(emissiveProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    Require(emissive.r > emissive.g && emissive.r > emissive.b && emissive.r > 80U,
        "KBMAT-MAT08: Emissive graph output must light the pixel even with a black base color");

    // Roughness/metallic math changes the BRDF: the metallic surface renders darker than the dielectric one.
    const bgfx::ProgramHandle dielectricProgram = BuildGraphProgram(vsBytes, *dielectricResult.artifact);
    const bgfx::ProgramHandle metalProgram = BuildGraphProgram(vsBytes, *metalResult.artifact);
    Require(bgfx::isValid(dielectricProgram) && bgfx::isValid(metalProgram), "KBMAT-MAT08: Metallic comparison programs must link");
    const ForwardRenderProbe dielectric = harness.Render(dielectricProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    const ForwardRenderProbe metal = harness.Render(metalProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    Require(static_cast<std::uint32_t>(dielectric.r) + dielectric.g + dielectric.b > static_cast<std::uint32_t>(metal.r) + metal.g + metal.b + 30U,
        "KBMAT-MAT08: A metallic surface must change the BRDF and render darker than the dielectric surface");

    harness.Shutdown();
}

// MAT-72: u_time drives material animation. A "white * Time -> emissive" graph must render a
// brighter pixel at a later time value, proving the frame time uniform reaches the graph shader.
void RunForwardGraphTimeAnimationTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat72_time_anim";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT72: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    RenderMaterialGraphNode white{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .positionX = 40, .positionY = 40 };
    white.parameter.defaultValueHint = "1 1 1 1";
    graph.nodes.push_back(white);
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::Time, .positionX = 40, .positionY = 140 });
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::Multiply, .positionX = 200, .positionY = 80 });
    // Black base color so lit output is ~0 and the time-driven emissive term solely drives the pixel.
    RenderMaterialGraphNode blackBase{ .id = 5U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .positionX = 40, .positionY = 220 };
    blackBase.parameter.defaultValueHint = "0 0 0 1";
    graph.nodes.push_back(blackBase);
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 5U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::Multiply, 4U, "a"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::Time, 3U, "value", RenderMaterialGraphNodeKind::Multiply, 4U, "b"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::Multiply, 4U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"));
    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x7200U });
    Require(compiled.Succeeded(), "KBMAT-MAT72: Time-driven emissive graph must compile");
    Require(compiled.shader.source.find("ctx.time") != std::string::npos, "KBMAT-MAT72: Time node must emit ctx.time in the generated source");
    const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT72: Time graph forward fragment shader must cook");

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT72: Direct3D11 device unavailable; cannot run GPU time-animation proof\n");
        Require(false, "KBMAT-MAT72: A real GPU device is required to prove time-driven animation");
        return;
    }

    const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
    Require(bgfx::isValid(program), "KBMAT-MAT72: Time graph program must link on the real GPU backend");

    const ForwardRenderProbe early = harness.Render(program, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.0F);
    const ForwardRenderProbe later = harness.Render(program, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.6F);
    const std::uint32_t earlySum = static_cast<std::uint32_t>(early.r) + early.g + early.b;
    const std::uint32_t laterSum = static_cast<std::uint32_t>(later.r) + later.g + later.b;
    Require(laterSum > earlySum + 60U,
        "KBMAT-MAT72: A Time-driven material must render a different (brighter) pixel as u_time advances between frames");

    harness.Shutdown();
}

// MAT-73: a second UV set reaches the graph shader. Sampling a gradient texture with uv1 (which the
// harness sets distinct from uv0) must read a different texel than sampling with uv0.
void RunForwardGraphUv1SamplingTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat73_uv1";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT73: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    const auto buildUvSampleGraph = [&](std::string_view uvSet, std::uint64_t assetId) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        RenderMaterialGraphNode uv{ .id = 2U, .kind = RenderMaterialGraphNodeKind::Uv, .positionX = 40, .positionY = 40 };
        uv.parameter.defaultValueHint = std::string{ uvSet };
        graph.nodes.push_back(uv);
        RenderMaterialGraphNode sample{ .id = 3U, .kind = RenderMaterialGraphNodeKind::TextureSample, .positionX = 200, .positionY = 40 };
        sample.parameter.stableId = "gradTex";
        sample.parameter.textureRole = "baseColor";
        sample.parameter.expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb;
        graph.nodes.push_back(sample);
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::Uv, 2U, "uv", RenderMaterialGraphNodeKind::TextureSample, 3U, "uv"));
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::TextureSample, 3U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = assetId });
        Require(compiled.Succeeded() && compiled.shader.reflection.textures.size() == 1U, "KBMAT-MAT73: UV-set sampling graph must compile with a sampler");
        return compiled;
    };

    const RenderMaterialGraphCompileResult uv0Compiled = buildUvSampleGraph("0", 0x7300U);
    const RenderMaterialGraphCompileResult uv1Compiled = buildUvSampleGraph("1", 0x7301U);
    Require(uv1Compiled.shader.source.find("ctx.uv1") != std::string::npos, "KBMAT-MAT73: A Uv node with set 1 must emit ctx.uv1");
    Require(uv0Compiled.shader.source.find("ctx.uv1") == std::string::npos, "KBMAT-MAT73: A Uv node with set 0 must sample uv0, not uv1");
    const RenderMaterialGraphShaderArtifactResult uv0Result = CookRenderMaterialGraphShaderArtifact(uv0Compiled.shader, backends, CookRequest(cacheDir.generic_string()));
    const RenderMaterialGraphShaderArtifactResult uv1Result = CookRenderMaterialGraphShaderArtifact(uv1Compiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(uv0Result.Succeeded() && uv1Result.Succeeded(), "KBMAT-MAT73: Both UV-set sampling graphs must cook");
    const std::string samplerName = uv0Compiled.shader.reflection.textures[0].samplerName;

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT73: Direct3D11 device unavailable; cannot run GPU uv1 sampling proof\n");
        Require(false, "KBMAT-MAT73: A real GPU device is required to prove uv1 sampling");
        return;
    }

    const bgfx::ProgramHandle uv0Program = BuildGraphProgram(vsBytes, *uv0Result.artifact);
    const bgfx::ProgramHandle uv1Program = BuildGraphProgram(vsBytes, *uv1Result.artifact);
    Require(bgfx::isValid(uv0Program) && bgfx::isValid(uv1Program), "KBMAT-MAT73: UV-set graph programs must link");

    // 2x1 gradient: texel0 black, texel1 red (ABGR). Point+clamp so uv0 (~0.5) reads red, uv1 (~0.05) reads black.
    const std::array<std::uint32_t, 2U> gradient{ 0xff000000U, 0xff0000ffU };
    bgfx::UniformHandle sampler = bgfx::createUniform(samplerName.c_str(), bgfx::UniformType::Sampler);
    bgfx::TextureHandle gradientTexture = bgfx::createTexture2D(2U, 1U, false, 1U, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy(gradient.data(), sizeof(gradient)));

    const ForwardRenderProbe uv0Probe = harness.Render(uv0Program, sampler, gradientTexture);
    const ForwardRenderProbe uv1Probe = harness.Render(uv1Program, sampler, gradientTexture);
    Require(uv0Probe.r > uv1Probe.r + 40U,
        "KBMAT-MAT73: Sampling with uv1 must read a different texel than uv0 (distinct second UV set reaches the shader)");

    bgfx::destroy(gradientTexture);
    bgfx::destroy(sampler);
    harness.Shutdown();
}

// MAT-74: per-vertex VertexColor (RGBA) reaches the graph shader. baseColor from VertexColor.rgba
// renders the vertex color; VertexColor.a -> Material Output alpha drives the output alpha channel.
void RunForwardGraphVertexColorTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat74_vertex_color";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT74: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    // Harness sets v_color0 = (0.15, 0.45, 0.85, 0.6): blue-dominant RGB and alpha 0.6.
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::VertexColor, .positionX = 60, .positionY = 60 });
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::VertexColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::VertexColor, 2U, "a", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"));
    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x7400U });
    Require(compiled.Succeeded(), "KBMAT-MAT74: VertexColor graph must compile");
    Require(compiled.shader.source.find("ctx.vertexColor") != std::string::npos, "KBMAT-MAT74: VertexColor node must emit ctx.vertexColor");
    const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT74: VertexColor graph must cook");

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT74: Direct3D11 device unavailable; cannot run GPU vertex color proof\n");
        Require(false, "KBMAT-MAT74: A real GPU device is required to prove vertex color");
        return;
    }

    const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
    Require(bgfx::isValid(program), "KBMAT-MAT74: VertexColor graph program must link");

    const ForwardRenderProbe probe = harness.Render(program, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    Require(probe.b > probe.r && probe.b > probe.g && probe.b > 60U,
        "KBMAT-MAT74: Per-vertex VertexColor RGB must drive a blue-dominant pixel through the graph shader");
    Require(probe.a > 120U && probe.a < 190U,
        "KBMAT-MAT74: VertexColor.a (0.6) must drive the output alpha channel (~153), distinct from opaque 255");

    harness.Shutdown();
}

// MAT-75: ScreenPosition (screen-space UV from gl_FragCoord) reaches the graph shader. Sampling a
// horizontal gradient with ScreenPosition must read a different texel on the left vs right of screen.
void RunForwardGraphScreenPositionTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat75_screen";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT75: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ScreenPosition, .positionX = 40, .positionY = 40 });
    RenderMaterialGraphNode sample{ .id = 3U, .kind = RenderMaterialGraphNodeKind::TextureSample, .positionX = 200, .positionY = 40 };
    sample.parameter.stableId = "gradTex";
    sample.parameter.textureRole = "baseColor";
    sample.parameter.expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb;
    graph.nodes.push_back(sample);
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ScreenPosition, 2U, "xy", RenderMaterialGraphNodeKind::TextureSample, 3U, "uv"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::TextureSample, 3U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x7500U });
    Require(compiled.Succeeded() && compiled.shader.reflection.textures.size() == 1U, "KBMAT-MAT75: ScreenPosition sampling graph must compile");
    Require(compiled.shader.source.find("ctx.screenPosition") != std::string::npos, "KBMAT-MAT75: ScreenPosition node must emit ctx.screenPosition");
    const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT75: ScreenPosition graph must cook");
    const std::string samplerName = compiled.shader.reflection.textures[0].samplerName;

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT75: Direct3D11 device unavailable; cannot run GPU screen position proof\n");
        Require(false, "KBMAT-MAT75: A real GPU device is required to prove screen position");
        return;
    }

    const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
    Require(bgfx::isValid(program), "KBMAT-MAT75: ScreenPosition graph program must link");

    // 2x1 gradient (black left, red right), point+clamp: left screen samples black, right samples red.
    const std::array<std::uint32_t, 2U> gradient{ 0xff000000U, 0xff0000ffU };
    bgfx::UniformHandle sampler = bgfx::createUniform(samplerName.c_str(), bgfx::UniformType::Sampler);
    bgfx::TextureHandle gradientTexture = bgfx::createTexture2D(2U, 1U, false, 1U, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy(gradient.data(), sizeof(gradient)));

    const std::vector<std::uint8_t> pixels = harness.RenderPixels(program, sampler, gradientTexture);
    const ForwardRenderProbe leftPixel = ForwardRenderHarness::ProbeAt(pixels, 12U, 32U);
    const ForwardRenderProbe rightPixel = ForwardRenderHarness::ProbeAt(pixels, 52U, 32U);
    Require(rightPixel.r > leftPixel.r + 40U,
        "KBMAT-MAT75: ScreenPosition.x must vary across the screen (right side samples redder than left)");

    bgfx::destroy(gradientTexture);
    bgfx::destroy(sampler);
    harness.Shutdown();
}

// MAT-76: object-space inputs. The harness simulates an object translated so local position and
// object origin differ from per-fragment world position by +0.7 in X. A graph wired
// LocalPosition->baseColor (and ObjectPosition->baseColor) must therefore render a red distinct
// from WorldPosition->baseColor, proving each object-space channel reaches the shader.
void RunForwardGraphObjectSpaceTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat76_object";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT76: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT76: Direct3D11 device unavailable; cannot run GPU object-space proof\n");
        Require(false, "KBMAT-MAT76: A real GPU device is required to prove object-space inputs");
        return;
    }

    const auto buildPositionProgram = [&](RenderMaterialGraphNodeKind kind, const std::string& subdir, const char* expectExpr) -> bgfx::ProgramHandle {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = kind, .positionX = 40, .positionY = 40 });
        graph.links.push_back(MakeLink(kind, 2U, "xyz", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x7600U });
        Require(compiled.Succeeded(), "KBMAT-MAT76: object-space graph must compile");
        Require(compiled.shader.source.find(expectExpr) != std::string::npos, "KBMAT-MAT76: object-space node must emit its context expression");
        const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest((cacheDir / subdir).generic_string()));
        Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT76: object-space graph must cook");
        const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
        Require(bgfx::isValid(program), "KBMAT-MAT76: object-space graph program must link");
        return program;
    };

    const bgfx::ProgramHandle localProgram = buildPositionProgram(RenderMaterialGraphNodeKind::LocalPosition, "local", "ctx.localPosition");
    const bgfx::ProgramHandle objectProgram = buildPositionProgram(RenderMaterialGraphNodeKind::ObjectPosition, "object", "ctx.objectPosition");
    const bgfx::ProgramHandle worldProgram = buildPositionProgram(RenderMaterialGraphNodeKind::WorldPosition, "world", "ctx.worldPos");

    const ForwardRenderProbe localProbe = harness.Render(localProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    const ForwardRenderProbe objectProbe = harness.Render(objectProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    const ForwardRenderProbe worldProbe = harness.Render(worldProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);

    Require(localProbe.r > worldProbe.r + 40U,
        "KBMAT-MAT76: LocalPosition.x must differ from WorldPosition.x for a translated object (local carries the +0.7 offset)");
    Require(objectProbe.r > worldProbe.r + 40U,
        "KBMAT-MAT76: ObjectPosition.x (object origin in world) must differ from per-fragment WorldPosition.x");

    harness.Shutdown();
}

// MAT-77: per-instance scalars. PerInstanceRandom and ObjectRadius ride the free .w lanes of the
// object-space varyings (packed from the affine model's unused column .w in the real instance path).
// Two instances with distinct values must render distinct pixels.
void RunForwardGraphPerInstanceTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat77_instance";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT77: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT77: Direct3D11 device unavailable; cannot run GPU per-instance proof\n");
        Require(false, "KBMAT-MAT77: A real GPU device is required to prove per-instance scalars");
        return;
    }

    const auto buildScalarProgram = [&](RenderMaterialGraphNodeKind kind, const std::string& subdir, const char* expectExpr) -> bgfx::ProgramHandle {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = kind, .positionX = 40, .positionY = 40 });
        // The scalar feeds MakeVector.x (Float->Float4) which the engine permits into baseColor (Color);
        // unconnected y/z/w default to 0, so the red channel carries the per-instance scalar.
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::MakeVector, .positionX = 200, .positionY = 40 });
        graph.links.push_back(MakeLink(kind, 2U, "value", RenderMaterialGraphNodeKind::MakeVector, 3U, "x"));
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::MakeVector, 3U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x7700U });
        Require(compiled.Succeeded(), "KBMAT-MAT77: per-instance scalar graph must compile");
        Require(compiled.shader.source.find(expectExpr) != std::string::npos, "KBMAT-MAT77: per-instance node must emit its context expression");
        const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest((cacheDir / subdir).generic_string()));
        Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT77: per-instance scalar graph must cook");
        const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
        Require(bgfx::isValid(program), "KBMAT-MAT77: per-instance scalar graph program must link");
        return program;
    };

    const bgfx::ProgramHandle randomProgram = buildScalarProgram(RenderMaterialGraphNodeKind::PerInstanceRandom, "random", "ctx.perInstanceRandom");
    const bgfx::ProgramHandle radiusProgram = buildScalarProgram(RenderMaterialGraphNodeKind::ObjectRadius, "radius", "ctx.objectRadius");

    // Two instances of the same batch with distinct per-instance random (0.2 vs 0.8 in the .w lane).
    const ForwardRenderProbe lowRandom = ForwardRenderHarness::ProbeAt(harness.RenderPixels(randomProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.0F, 0.2F, 0.0F), 32U, 32U);
    const ForwardRenderProbe highRandom = ForwardRenderHarness::ProbeAt(harness.RenderPixels(randomProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.0F, 0.8F, 0.0F), 32U, 32U);
    Require(highRandom.r > lowRandom.r + 40U,
        "KBMAT-MAT77: PerInstanceRandom must differ between instances in a batch (0.8 renders brighter than 0.2)");

    // ObjectRadius drives the output and varies with the per-instance radius (0.75 vs 0.1).
    const ForwardRenderProbe bigRadius = ForwardRenderHarness::ProbeAt(harness.RenderPixels(radiusProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.0F, 0.0F, 0.75F), 32U, 32U);
    const ForwardRenderProbe smallRadius = ForwardRenderHarness::ProbeAt(harness.RenderPixels(radiusProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.0F, 0.0F, 0.1F), 32U, 32U);
    Require(bigRadius.r > smallRadius.r + 40U,
        "KBMAT-MAT77: ObjectRadius must vary the output with the per-instance bounding radius");

    harness.Shutdown();
}

// MAT-78: per-texture sampler state. The node sampler state flows to reflection, then to resolved bgfx
// sampler flags on the binding. Point vs linear filtering at a 2-texel boundary (uv 0.5) must render a
// distinct pixel, proving the sampler state really changes filtering on the GPU.
void RunForwardGraphSamplerStateTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat78_sampler";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT78: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    const auto makeGraph = [](RenderMaterialGraphSamplerState state) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        RenderMaterialGraphNode tex{ .id = 2U, .kind = RenderMaterialGraphNodeKind::TextureSample, .positionX = 40, .positionY = 40 };
        tex.parameter.stableId = "filterTex";
        tex.parameter.textureRole = "baseColor";
        tex.parameter.expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb;
        tex.parameter.samplerState = state;
        graph.nodes.push_back(tex);
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::TextureSample, 2U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        return graph;
    };

    RenderMaterialGraphSamplerState pointState{};
    pointState.minFilter = RenderMaterialGraphSamplerFilter::Point;
    pointState.magFilter = RenderMaterialGraphSamplerFilter::Point;
    const RenderMaterialGraphSamplerState linearState{};

    const RenderMaterialGraphCompileResult pointCompiled = CompileRenderMaterialGraphToShaderSource(makeGraph(pointState), RenderMaterialGraphBuildContext{ .assetId = 0x7801U });
    const RenderMaterialGraphCompileResult linearCompiled = CompileRenderMaterialGraphToShaderSource(makeGraph(linearState), RenderMaterialGraphBuildContext{ .assetId = 0x7802U });
    Require(pointCompiled.Succeeded() && linearCompiled.Succeeded() && pointCompiled.shader.reflection.textures.size() == 1U, "KBMAT-MAT78: sampler-state graphs must compile");
    Require(pointCompiled.shader.reflection.textures[0].samplerState.magFilter == RenderMaterialGraphSamplerFilter::Point,
        "KBMAT-MAT78: node sampler state must reach the reflection");

    const RenderMaterialGraphProgramBindingResult pointBinding = BuildRenderMaterialGraphProgramBinding(0U, 0U, pointCompiled.shader, {});
    const RenderMaterialGraphProgramBindingResult linearBinding = BuildRenderMaterialGraphProgramBinding(0U, 0U, linearCompiled.shader, {});
    const std::uint32_t pointFlags = pointBinding.binding.textures.at(0).samplerFlags;
    const std::uint32_t linearFlags = linearBinding.binding.textures.at(0).samplerFlags;
    Require(linearFlags == 0U, "KBMAT-MAT78: linear/repeat sampler state must resolve to zero bgfx flags (defaults)");
    Require(pointFlags != linearFlags && (pointFlags & BGFX_SAMPLER_MAG_POINT) != 0U,
        "KBMAT-MAT78: point sampler state must resolve to point bgfx flags distinct from linear");

    const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(pointCompiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT78: sampler-state graph must cook");
    const std::string samplerName = pointCompiled.shader.reflection.textures[0].samplerName;

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT78: Direct3D11 device unavailable; cannot run GPU sampler state proof\n");
        Require(false, "KBMAT-MAT78: A real GPU device is required to prove sampler state filtering");
        return;
    }

    const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
    Require(bgfx::isValid(program), "KBMAT-MAT78: sampler-state graph program must link");

    // 2x1 texture (black, white). At uv 0.5 (screen centre) point sampling reads one texel (0 or 255),
    // linear sampling blends to ~128, so the centre pixel differs by filter. Texture created with default
    // flags so the per-bind sampler flags fully drive filtering.
    const std::array<std::uint32_t, 2U> texels{ 0xff000000U, 0xffffffffU };
    bgfx::UniformHandle sampler = bgfx::createUniform(samplerName.c_str(), bgfx::UniformType::Sampler);
    bgfx::TextureHandle texture = bgfx::createTexture2D(2U, 1U, false, 1U, bgfx::TextureFormat::RGBA8, 0U, bgfx::copy(texels.data(), sizeof(texels)));

    const ForwardRenderProbe pointPixel = ForwardRenderHarness::ProbeAt(harness.RenderPixels(program, sampler, texture, 0.0F, 0.0F, 0.0F, pointFlags), 32U, 32U);
    const ForwardRenderProbe linearPixel = ForwardRenderHarness::ProbeAt(harness.RenderPixels(program, sampler, texture, 0.0F, 0.0F, 0.0F, linearFlags), 32U, 32U);
    const int filterDelta = std::abs(static_cast<int>(pointPixel.r) - static_cast<int>(linearPixel.r));
    Require(filterDelta > 40,
        "KBMAT-MAT78: sampler state must change filtering — point vs linear must differ at the texel boundary");

    bgfx::destroy(texture);
    bgfx::destroy(sampler);
    harness.Shutdown();
}

// MAT-80: a translucent material (alpha 0.5) rendered with the transparent alpha-blend state must let the
// background show through; rendered opaque it overwrites the background. The difference proves alpha blend.
void RunForwardGraphTransparentBlendTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat80_blend";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT80: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    // Blue base color with alpha 0.5; the MaterialOutput alpha defaults to baseColor.a, so surface.alpha = 0.5.
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    RenderMaterialGraphNode color{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .positionX = 40, .positionY = 40 };
    color.parameter.defaultValueHint = "0 0 1 0.5";
    graph.nodes.push_back(color);
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x8000U });
    Require(compiled.Succeeded(), "KBMAT-MAT80: translucent graph must compile");
    const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT80: translucent graph must cook");

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT80: Direct3D11 device unavailable; cannot run GPU alpha-blend proof\n");
        Require(false, "KBMAT-MAT80: A real GPU device is required to prove alpha blending");
        return;
    }

    const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
    Require(bgfx::isValid(program), "KBMAT-MAT80: translucent graph program must link");

    const std::uint32_t redBackground = 0xFF0000FFU; // RGBA clear: opaque red.
    // Opaque draw overwrites the red background with the (blue) material.
    const ForwardRenderProbe opaquePixel = ForwardRenderHarness::ProbeAt(harness.RenderPixels(program, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.0F, 0.0F, 0.0F, UINT32_MAX, redBackground, 0U), 32U, 32U);
    // Transparent draw (alpha-blend state, as MeshPipelinePassPolicy::State(BaseTransparent) sets) lets the red show.
    const ForwardRenderProbe blendedPixel = ForwardRenderHarness::ProbeAt(harness.RenderPixels(program, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.0F, 0.0F, 0.0F, UINT32_MAX, redBackground, BGFX_STATE_BLEND_ALPHA), 32U, 32U);

    Require(blendedPixel.r > opaquePixel.r + 40U,
        "KBMAT-MAT80: alpha blending must let the red background show through (blended red > opaque red)");
    Require(blendedPixel.b > 40U,
        "KBMAT-MAT80: the translucent material must still contribute its own color in the blend");
    Require(opaquePixel.b > blendedPixel.b,
        "KBMAT-MAT80: the opaque draw must be fuller material color than the half-blended draw");

    harness.Shutdown();
}

// MAT-81: a graph driving worldPositionOffset must move geometry in the generated vertex shader. The same
// material FS rendered with the WPO vertex shader (offset +0.6 in X) shifts the triangle so a left-edge
// pixel that the fixed VS covers becomes uncovered (background) — proving WPO offsets real geometry.
void RunForwardGraphWorldPositionOffsetTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat81_wpo";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    // worldPositionOffset = (0.6, 0, 0) shifts the geometry right in clip space; baseColor defaults to white.
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    RenderMaterialGraphNode offset{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantVector, .positionX = 40, .positionY = 40 };
    offset.parameter.defaultValueHint = "0.6 0 0";
    graph.nodes.push_back(offset);
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantVector, 2U, "xyz", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "worldPositionOffset"));
    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x8100U });
    Require(compiled.Succeeded(), "KBMAT-MAT81: world-position-offset graph must compile");
    Require(compiled.shader.reflection.hasWorldPositionOffset, "KBMAT-MAT81: reflection must flag the world position offset");
    Require(compiled.shader.source.find("EvaluateWorldPositionOffset") != std::string::npos, "KBMAT-MAT81: source must emit the WPO evaluation function");
    const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT81: world-position-offset graph must cook");

    // MAT-19/#19: the WPO cook also produces a real generated vertex-shader binary (compiled by shaderc,
    // proven by the cook succeeding and the binary existing on disk); the scene loader pairs it with the
    // graph fragment shader to move real scene geometry.
    Require(result.artifact->hasVertexShader, "KBMAT-MAT19: a world-position-offset graph must cook a generated vertex shader");
    const RenderMaterialGraphShaderBinary* cookedWpoVs = result.artifact->FindVertexBinary(RenderMaterialGraphShaderBackend::Dxbc);
    Require(cookedWpoVs != nullptr && cookedWpoVs->byteSize > 0U, "KBMAT-MAT19: the WPO cook must produce a real vertex binary");
    Require(std::filesystem::exists(std::filesystem::path{ cookedWpoVs->binaryPath }), "KBMAT-MAT19: the cooked WPO vs.bin must exist on disk");

    const std::filesystem::path fixedVsBin = cacheDir / "vs_fixed.bin";
    const std::filesystem::path wpoVsBin = cacheDir / "vs_wpo.bin";
    Require(CookHarnessVertexShader(fixedVsBin), "KBMAT-MAT81: fixed harness vertex shader must cook");
    Require(CookHarnessWorldPositionOffsetVertexShader(wpoVsBin, compiled.shader.source), "KBMAT-MAT81: generated WPO vertex shader must cook");
    const std::vector<std::uint8_t> fixedVsBytes = ReadAllBytes(fixedVsBin);
    const std::vector<std::uint8_t> wpoVsBytes = ReadAllBytes(wpoVsBin);

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT81: Direct3D11 device unavailable; cannot run GPU world-position-offset proof\n");
        Require(false, "KBMAT-MAT81: A real GPU device is required to prove world position offset");
        return;
    }

    const bgfx::ProgramHandle fixedProgram = BuildGraphProgram(fixedVsBytes, *result.artifact);
    const bgfx::ProgramHandle wpoProgram = BuildGraphProgram(wpoVsBytes, *result.artifact);
    Require(bgfx::isValid(fixedProgram) && bgfx::isValid(wpoProgram), "KBMAT-MAT81: WPO programs must link");

    // Pixel near the left edge (x=8 of 64, clip x ~ -0.75): covered by the fixed VS, uncovered once the
    // +0.6 offset shifts the triangle right (its left edge moves to clip x ~ -0.4).
    const ForwardRenderProbe fixedPixel = ForwardRenderHarness::ProbeAt(harness.RenderPixels(fixedProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE), 8U, 32U);
    const ForwardRenderProbe wpoPixel = ForwardRenderHarness::ProbeAt(harness.RenderPixels(wpoProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE), 8U, 32U);

    Require(fixedPixel.r > 100U, "KBMAT-MAT81: without the offset the left-edge pixel must be covered by the material");
    Require(fixedPixel.r > wpoPixel.r + 60U,
        "KBMAT-MAT81: WorldPositionOffset must move geometry — the offset uncovers the left-edge pixel (background shows)");

    harness.Shutdown();
}

// MAT-80/#18b: a graph that samples the opaque scene depth (SceneDepth / DepthFade) declares the scene
// depth sampler and cooks to a real binary; the scene binds the opaque depth to that sampler in the
// transparent pass so soft-depth effects read real geometry depth.
void RunForwardGraphSceneDepthCooksTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat80_scenedepth";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    // DepthFade drives the surface alpha for a soft edge; a white base color keeps the surface visible.
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.shadingModel = "unlit";
    graph.blendMode = "translucent";
    RenderMaterialGraphNode white{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
    white.parameter.defaultValueHint = "1 1 1 1";
    graph.nodes.push_back(white);
    RenderMaterialGraphNode fade{ .id = 3U, .kind = RenderMaterialGraphNodeKind::DepthFade };
    fade.parameter.defaultValueHint = "0.02";
    graph.nodes.push_back(fade);
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::DepthFade, 3U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"));

    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x8B00U });
    Require(compiled.Succeeded(), "KBMAT-MAT18B: scene-depth graph must compile");
    Require(compiled.shader.reflection.usesSceneDepth, "KBMAT-MAT18B: reflection must flag scene-depth usage");
    Require(compiled.shader.source.find("SAMPLER2D(s_kbSceneDepth, 5)") != std::string::npos, "KBMAT-MAT18B: the shader must declare the scene depth sampler at slot 5");
    Require(compiled.shader.source.find("ctx.fragmentDepth") != std::string::npos, "KBMAT-MAT18B: DepthFade must compare against the fragment depth");

    const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT18B: scene-depth graph must cook");
    const RenderMaterialGraphShaderBinary* binary = result.artifact->FindBinary(RenderMaterialGraphShaderBackend::Dxbc);
    Require(binary != nullptr && binary->byteSize > 0U, "KBMAT-MAT18B: the scene-depth shader must cook to a real binary");
}

// MAT-36: a Make->Break MaterialAttributes round-trip must preserve channel values. A blue base color
// packed into MaterialAttributes then unpacked and routed to MaterialOutput must still render blue.
void RunForwardGraphMaterialAttributesTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat36_attrs";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT36: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    RenderMaterialGraphNode color{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .positionX = 20, .positionY = 40 };
    color.parameter.defaultValueHint = "0 0 1 1";
    graph.nodes.push_back(color);
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::MakeMaterialAttributes, .positionX = 120, .positionY = 40 });
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::BreakMaterialAttributes, .positionX = 220, .positionY = 40 });
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MakeMaterialAttributes, 3U, "baseColor"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::MakeMaterialAttributes, 3U, "attributes", RenderMaterialGraphNodeKind::BreakMaterialAttributes, 4U, "attributes"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::BreakMaterialAttributes, 4U, "baseColor", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x3600U });
    Require(compiled.Succeeded(), "KBMAT-MAT36: Make/Break MaterialAttributes graph must compile");
    Require(compiled.shader.source.find("MaterialSurface attrs3") != std::string::npos, "KBMAT-MAT36: MakeMaterialAttributes must emit a MaterialSurface temp");
    const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT36: Make/Break graph must cook");

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT36: Direct3D11 device unavailable; cannot run GPU MaterialAttributes proof\n");
        Require(false, "KBMAT-MAT36: A real GPU device is required to prove the MaterialAttributes round-trip");
        return;
    }

    const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
    Require(bgfx::isValid(program), "KBMAT-MAT36: MaterialAttributes graph program must link");

    const ForwardRenderProbe pixel = ForwardRenderHarness::ProbeAt(harness.RenderPixels(program, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE), 32U, 32U);
    Require(pixel.b > pixel.r + 40U && pixel.b > pixel.g + 40U,
        "KBMAT-MAT36: a blue base color carried through Make->Break MaterialAttributes must still render blue-dominant");

    harness.Shutdown();
}

// MAT-36: BlendMaterialAttributes mixes two attribute sets. Blending a red and a blue surface at 0.5 must
// render a purple result — both red and blue channels present — proving the per-channel blend works.
void RunForwardGraphBlendAttributesTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat36_blend";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT36B: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    RenderMaterialGraphNode red{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
    red.parameter.defaultValueHint = "1 0 0 1";
    RenderMaterialGraphNode blue{ .id = 4U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
    blue.parameter.defaultValueHint = "0 0 1 1";
    RenderMaterialGraphNode factor{ .id = 6U, .kind = RenderMaterialGraphNodeKind::ConstantScalar };
    factor.parameter.defaultValueHint = "0.5";
    graph.nodes.push_back(red);
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::MakeMaterialAttributes });
    graph.nodes.push_back(blue);
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 5U, .kind = RenderMaterialGraphNodeKind::MakeMaterialAttributes });
    graph.nodes.push_back(factor);
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 7U, .kind = RenderMaterialGraphNodeKind::BlendMaterialAttributes });
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 8U, .kind = RenderMaterialGraphNodeKind::BreakMaterialAttributes });
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MakeMaterialAttributes, 3U, "baseColor"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 4U, "rgba", RenderMaterialGraphNodeKind::MakeMaterialAttributes, 5U, "baseColor"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::MakeMaterialAttributes, 3U, "attributes", RenderMaterialGraphNodeKind::BlendMaterialAttributes, 7U, "a"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::MakeMaterialAttributes, 5U, "attributes", RenderMaterialGraphNodeKind::BlendMaterialAttributes, 7U, "b"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantScalar, 6U, "value", RenderMaterialGraphNodeKind::BlendMaterialAttributes, 7U, "factor"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::BlendMaterialAttributes, 7U, "attributes", RenderMaterialGraphNodeKind::BreakMaterialAttributes, 8U, "attributes"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::BreakMaterialAttributes, 8U, "baseColor", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x3601U });
    Require(compiled.Succeeded(), "KBMAT-MAT36B: BlendMaterialAttributes graph must compile");
    Require(compiled.shader.source.find("mix(") != std::string::npos, "KBMAT-MAT36B: BlendMaterialAttributes must emit per-channel mix()");
    const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT36B: Blend graph must cook");

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT36B: Direct3D11 device unavailable; cannot run GPU Blend proof\n");
        Require(false, "KBMAT-MAT36B: A real GPU device is required to prove the MaterialAttributes blend");
        return;
    }

    const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
    Require(bgfx::isValid(program), "KBMAT-MAT36B: Blend graph program must link");

    const ForwardRenderProbe pixel = ForwardRenderHarness::ProbeAt(harness.RenderPixels(program, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE), 32U, 32U);
    Require(pixel.r > 40U && pixel.b > 40U && pixel.g + 30U < pixel.r,
        "KBMAT-MAT36B: blending red and blue attributes at 0.5 must render purple (both red and blue present, little green)");

    harness.Shutdown();
}

// MAT-36: SetMaterialAttributes overrides one channel of a base set; GetMaterialAttributes reads it back.
// A red base whose baseColor is overridden with blue, then read via Get, must render blue.
void RunForwardGraphGetSetAttributesTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat36_getset";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT36C: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    RenderMaterialGraphNode red{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
    red.parameter.defaultValueHint = "1 0 0 1";
    RenderMaterialGraphNode blue{ .id = 4U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
    blue.parameter.defaultValueHint = "0 0 1 1";
    graph.nodes.push_back(red);
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::MakeMaterialAttributes });
    graph.nodes.push_back(blue);
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 5U, .kind = RenderMaterialGraphNodeKind::SetMaterialAttributes });
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 6U, .kind = RenderMaterialGraphNodeKind::GetMaterialAttributes });
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MakeMaterialAttributes, 3U, "baseColor"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::MakeMaterialAttributes, 3U, "attributes", RenderMaterialGraphNodeKind::SetMaterialAttributes, 5U, "attributes"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 4U, "rgba", RenderMaterialGraphNodeKind::SetMaterialAttributes, 5U, "baseColor"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::SetMaterialAttributes, 5U, "attributesOut", RenderMaterialGraphNodeKind::GetMaterialAttributes, 6U, "attributes"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::GetMaterialAttributes, 6U, "baseColor", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x3602U });
    Require(compiled.Succeeded(), "KBMAT-MAT36C: Set/Get MaterialAttributes graph must compile");
    const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT36C: Set/Get graph must cook");

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT36C: Direct3D11 device unavailable; cannot run GPU Set/Get proof\n");
        Require(false, "KBMAT-MAT36C: A real GPU device is required to prove Set/Get MaterialAttributes");
        return;
    }

    const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
    Require(bgfx::isValid(program), "KBMAT-MAT36C: Set/Get graph program must link");

    const ForwardRenderProbe pixel = ForwardRenderHarness::ProbeAt(harness.RenderPixels(program, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE), 32U, 32U);
    Require(pixel.b > pixel.r + 40U && pixel.b > pixel.g + 40U,
        "KBMAT-MAT36C: SetMaterialAttributes must override baseColor to blue and GetMaterialAttributes must read it back");

    harness.Shutdown();
}

// MAT-36: MaterialOutput accepts a single MaterialAttributes pin that drives the whole surface. A green
// attribute set wired straight into MaterialOutput.attributes (no per-channel pins) must render green.
void RunForwardGraphMaterialOutputAttributesTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat36_output";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT36D: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    RenderMaterialGraphNode green{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
    green.parameter.defaultValueHint = "0 1 0 1";
    graph.nodes.push_back(green);
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::MakeMaterialAttributes });
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MakeMaterialAttributes, 3U, "baseColor"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::MakeMaterialAttributes, 3U, "attributes", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "attributes"));

    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x3603U });
    Require(compiled.Succeeded(), "KBMAT-MAT36D: MaterialOutput.attributes graph must compile");
    Require(compiled.shader.source.find("material = ") != std::string::npos, "KBMAT-MAT36D: a single attributes pin must assign the whole surface struct");
    const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT36D: MaterialOutput.attributes graph must cook");

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT36D: Direct3D11 device unavailable; cannot run GPU MaterialOutput.attributes proof\n");
        Require(false, "KBMAT-MAT36D: A real GPU device is required to prove MaterialOutput.attributes");
        return;
    }

    const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
    Require(bgfx::isValid(program), "KBMAT-MAT36D: MaterialOutput.attributes graph program must link");

    const ForwardRenderProbe pixel = ForwardRenderHarness::ProbeAt(harness.RenderPixels(program, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE), 32U, 32U);
    Require(pixel.g > pixel.r + 40U && pixel.g > pixel.b + 40U,
        "KBMAT-MAT36D: a green MaterialAttributes set wired into MaterialOutput.attributes must render green");

    harness.Shutdown();
}

// MAT-37: Unlit and DefaultLit must render differently and correctly. A fully-metallic white surface lit
// only by the harness ambient environment (no specular env term) shades to near-black under DefaultLit
// (metallic kills diffuse), while Unlit outputs the white base color straight to the framebuffer.
void RunForwardGraphShadingModelTest() {
    const std::filesystem::path baseDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat37_shading";
    std::error_code error;
    std::filesystem::remove_all(baseDir, error);
    std::filesystem::create_directories(baseDir, error);

    const std::filesystem::path vsBin = baseDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT37: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    const auto buildGraph = [](std::string_view shadingModel) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.shadingModel = std::string{ shadingModel };
        RenderMaterialGraphNode white{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
        white.parameter.defaultValueHint = "1 1 1 1";
        RenderMaterialGraphNode metal{ .id = 3U, .kind = RenderMaterialGraphNodeKind::ConstantScalar };
        metal.parameter.defaultValueHint = "1";
        graph.nodes.push_back(white);
        graph.nodes.push_back(metal);
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantScalar, 3U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"));
        return graph;
    };

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT37: Direct3D11 device unavailable; cannot run GPU shading model proof\n");
        Require(false, "KBMAT-MAT37: A real GPU device is required to prove Unlit vs DefaultLit");
        return;
    }

    const auto render = [&](std::string_view shadingModel, const std::filesystem::path& cacheDir) {
        std::error_code subError;
        std::filesystem::create_directories(cacheDir, subError);
        const RenderMaterialGraphDocument graph = buildGraph(shadingModel);
        const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x3700U });
        Require(compiled.Succeeded(), "KBMAT-MAT37: shading model graph must compile");
        const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
        Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT37: shading model graph must cook");
        const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
        Require(bgfx::isValid(program), "KBMAT-MAT37: shading model graph program must link");
        return ForwardRenderHarness::ProbeAt(harness.RenderPixels(program, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE), 32U, 32U);
    };

    const ForwardRenderProbe litPixel = render("lit", baseDir / "lit");
    const ForwardRenderProbe unlitPixel = render("unlit", baseDir / "unlit");

    Require(unlitPixel.r > 200U && unlitPixel.g > 200U && unlitPixel.b > 200U,
        "KBMAT-MAT37: Unlit must output the white base color straight to the framebuffer");
    Require(litPixel.r < 70U && litPixel.g < 70U && litPixel.b < 70U,
        "KBMAT-MAT37: DefaultLit on a fully-metallic surface with no specular environment must shade near-black");
    Require(unlitPixel.r > litPixel.r + 120U,
        "KBMAT-MAT37: Unlit and DefaultLit must produce visibly different pixels");

    harness.Shutdown();
}

// MAT-38: a Masked blend mode clips fragments whose alpha is below the clip threshold. The same red
// material rendered over a blue background shows red when alpha (0.8) is above the 0.5 threshold and
// discards entirely — letting the blue background through — when alpha (0.2) is below it.
void RunForwardGraphMaskedDiscardTest() {
    const std::filesystem::path baseDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat38_masked";
    std::error_code error;
    std::filesystem::remove_all(baseDir, error);
    std::filesystem::create_directories(baseDir, error);

    const std::filesystem::path vsBin = baseDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT38M: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    const auto buildGraph = [](const char* rgba) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.shadingModel = "unlit";
        graph.blendMode = "masked";
        RenderMaterialGraphNode color{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
        color.parameter.defaultValueHint = rgba;
        graph.nodes.push_back(color);
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        return graph;
    };

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT38M: Direct3D11 device unavailable; cannot run GPU masked discard proof\n");
        Require(false, "KBMAT-MAT38M: A real GPU device is required to prove masked discard");
        return;
    }

    const std::uint32_t blueBackground = 0x0000FFFFU; // RGBA clear: opaque blue.
    const auto render = [&](const char* rgba, const std::filesystem::path& cacheDir) {
        std::error_code subError;
        std::filesystem::create_directories(cacheDir, subError);
        const RenderMaterialGraphDocument graph = buildGraph(rgba);
        const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x3800U });
        Require(compiled.Succeeded(), "KBMAT-MAT38M: masked graph must compile");
        Require(compiled.shader.reflection.blendMode == RenderMaterialGraphBlendMode::Masked, "KBMAT-MAT38M: graph must resolve to the Masked blend mode");
        const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
        Require(result.Succeeded() && result.artifact.has_value() && result.artifact->wrapperSource.find("discard") != std::string::npos,
            "KBMAT-MAT38M: the masked wrapper must contain a clip/discard");
        const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
        Require(bgfx::isValid(program), "KBMAT-MAT38M: masked graph program must link");
        return ForwardRenderHarness::ProbeAt(harness.RenderPixels(program, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.0F, 0.0F, 0.0F, UINT32_MAX, blueBackground, 0U), 32U, 32U);
    };

    const ForwardRenderProbe keptPixel = render("1 0 0 0.8", baseDir / "kept");
    const ForwardRenderProbe discardedPixel = render("1 0 0 0.2", baseDir / "clipped");

    Require(keptPixel.r > 150U && keptPixel.b < 90U,
        "KBMAT-MAT38M: alpha above the clip threshold must keep the red material");
    Require(discardedPixel.b > 150U && discardedPixel.r < 60U,
        "KBMAT-MAT38M: alpha below the clip threshold must discard the fragment and reveal the blue background");

    harness.Shutdown();
}

// MAT-38: each translucent blend mode composites predictably over the destination. A red, half-alpha
// material drawn over a green background yields the exact ROP result for each bgfx blend equation, which
// is what MeshPipelinePassPolicy selects per material blend mode (verified separately at the state level).
void RunForwardGraphBlendModeCompositeTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat38_composite";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT38C: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    // Unlit red with alpha 0.5 (surface.alpha defaults to baseColor.a) over an opaque green background.
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.shadingModel = "unlit";
    RenderMaterialGraphNode color{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
    color.parameter.defaultValueHint = "1 0 0 0.5";
    graph.nodes.push_back(color);
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x3801U });
    Require(compiled.Succeeded(), "KBMAT-MAT38C: composite graph must compile");
    const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT38C: composite graph must cook");

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT38C: Direct3D11 device unavailable; cannot run GPU blend composite proof\n");
        Require(false, "KBMAT-MAT38C: A real GPU device is required to prove blend mode compositing");
        return;
    }
    const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
    Require(bgfx::isValid(program), "KBMAT-MAT38C: composite graph program must link");

    const std::uint32_t greenBackground = 0x00FF00FFU; // RGBA clear: opaque green.
    const auto probe = [&](std::uint64_t blendState) {
        return ForwardRenderHarness::ProbeAt(harness.RenderPixels(program, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.0F, 0.0F, 0.0F, UINT32_MAX, greenBackground, blendState), 32U, 32U);
    };

    // Translucent (src*a + dst*(1-a)): 0.5*red + 0.5*green -> roughly (128,128,0).
    const ForwardRenderProbe alpha = probe(BGFX_STATE_BLEND_ALPHA);
    Require(alpha.r > 90U && alpha.r < 170U && alpha.g > 90U && alpha.g < 170U && alpha.b < 40U,
        "KBMAT-MAT38C: alpha blend must composite half red over half green");
    // Additive (src + dst): red + green -> (255,255,0).
    const ForwardRenderProbe additive = probe(BGFX_STATE_BLEND_ADD);
    Require(additive.r > 200U && additive.g > 200U,
        "KBMAT-MAT38C: additive blend must add the red material onto the green background");
    // Modulate (src*dst): red*green -> (0,0,0).
    const ForwardRenderProbe modulate = probe(BGFX_STATE_BLEND_MULTIPLY);
    Require(modulate.r < 40U && modulate.g < 40U && modulate.b < 40U,
        "KBMAT-MAT38C: modulate blend of red over green must darken to near-black");
    // AlphaHoldout (dst*(1-a), no source color): green dimmed to ~half, no red added.
    const ForwardRenderProbe holdout = probe(BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_INV_SRC_ALPHA));
    Require(holdout.r < 40U && holdout.g > 90U && holdout.g < 180U && holdout.b < 40U,
        "KBMAT-MAT38C: alpha holdout must scale the background by (1-alpha) without adding source color");

    harness.Shutdown();
}

// MAT-39: a StaticSwitch selects one branch at compile time. The same switch graph cooked with the
// StaticBoolParameter true renders the red branch; cooked false renders the blue branch — proving the
// static selection drives a real per-variant program on the GPU.
void RunForwardGraphStaticSwitchTest() {
    const std::filesystem::path baseDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat39_static";
    std::error_code error;
    std::filesystem::remove_all(baseDir, error);
    std::filesystem::create_directories(baseDir, error);

    const std::filesystem::path vsBin = baseDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT39: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    const auto buildGraph = [](const char* boolHint) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.shadingModel = "unlit";
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::StaticBoolParameter, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = boolHint } });
        RenderMaterialGraphNode red{ .id = 3U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
        red.parameter.defaultValueHint = "1 0 0 1";
        RenderMaterialGraphNode blue{ .id = 4U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
        blue.parameter.defaultValueHint = "0 0 1 1";
        graph.nodes.push_back(red);
        graph.nodes.push_back(blue);
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 5U, .kind = RenderMaterialGraphNodeKind::StaticSwitch });
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::StaticBoolParameter, 2U, "value", RenderMaterialGraphNodeKind::StaticSwitch, 5U, "value"));
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba", RenderMaterialGraphNodeKind::StaticSwitch, 5U, "true"));
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 4U, "rgba", RenderMaterialGraphNodeKind::StaticSwitch, 5U, "false"));
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::StaticSwitch, 5U, "result", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        return graph;
    };

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT39: Direct3D11 device unavailable; cannot run GPU static switch proof\n");
        Require(false, "KBMAT-MAT39: A real GPU device is required to prove static switch selection");
        return;
    }

    const auto render = [&](const char* boolHint, const std::filesystem::path& cacheDir) {
        std::error_code subError;
        std::filesystem::create_directories(cacheDir, subError);
        const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(buildGraph(boolHint), RenderMaterialGraphBuildContext{ .assetId = 0x3900U });
        Require(compiled.Succeeded(), "KBMAT-MAT39: static switch graph must compile");
        const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
        Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT39: static switch graph must cook");
        const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
        Require(bgfx::isValid(program), "KBMAT-MAT39: static switch graph program must link");
        return ForwardRenderHarness::ProbeAt(harness.RenderPixels(program, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE), 32U, 32U);
    };

    const ForwardRenderProbe truePixel = render("true", baseDir / "on");
    const ForwardRenderProbe falsePixel = render("false", baseDir / "off");

    Require(truePixel.r > truePixel.b + 40U, "KBMAT-MAT39: the true static branch must render the red constant");
    Require(falsePixel.b > falsePixel.r + 40U, "KBMAT-MAT39: the false static branch must render the blue constant");

    harness.Shutdown();
}

// MAT-45: coordinate nodes transform the UV that drives texture sampling. A 2x1 black|red gradient is
// sampled through TextureCoordinate (tiling), Panner (time offset) and Rotator (time rotation); each
// transform moves the sampled texel, proving the coordinate math reaches the GPU.
void RunForwardGraphCoordinateNodesTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat45_coord";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT45: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    // coordNode is the id-2 coordinate node; its "uv" output drives the TextureSample UV.
    const auto buildGraph = [](const RenderMaterialGraphNode& coordNode, std::uint64_t assetId) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.shadingModel = "unlit";
        graph.nodes.push_back(coordNode);
        RenderMaterialGraphNode sample{ .id = 3U, .kind = RenderMaterialGraphNodeKind::TextureSample };
        sample.parameter.stableId = "gradTex";
        sample.parameter.textureRole = "baseColor";
        sample.parameter.expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb;
        graph.nodes.push_back(sample);
        graph.links.push_back(MakeLink(coordNode.kind, 2U, "uv", RenderMaterialGraphNodeKind::TextureSample, 3U, "uv"));
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::TextureSample, 3U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = assetId });
        Require(compiled.Succeeded() && compiled.shader.reflection.textures.size() == 1U, "KBMAT-MAT45: coordinate sampling graph must compile with a sampler");
        return compiled;
    };

    const auto coord = [](float u, float v, const char* hint) {
        RenderMaterialGraphNode node{ .id = 2U, .kind = RenderMaterialGraphNodeKind::TextureCoordinate };
        node.parameter.defaultValueHint = std::string{ std::to_string(u) + " " + std::to_string(v) };
        (void)hint;
        return node;
    };

    // TextureCoordinate: tiling 1.0 samples uv.x~0.5 (red texel); tiling 0.2 pulls uv.x~0.1 (black texel).
    const RenderMaterialGraphCompileResult tileOne = buildGraph(coord(1.0F, 1.0F, ""), 0x4500U);
    const RenderMaterialGraphCompileResult tileSmall = buildGraph(coord(0.2F, 1.0F, ""), 0x4501U);
    // Panner: scroll uv.x by time * -1; at time 0 uv.x~0.5 (red), at time 0.5 uv.x~0.0 (black).
    RenderMaterialGraphNode panner{ .id = 2U, .kind = RenderMaterialGraphNodeKind::Panner };
    panner.parameter.defaultValueHint = "-1 0";
    const RenderMaterialGraphCompileResult pannerGraph = buildGraph(panner, 0x4502U);
    // Rotator: rotate about the centre by time; probed off-centre it crosses the texel boundary.
    RenderMaterialGraphNode rotator{ .id = 2U, .kind = RenderMaterialGraphNodeKind::Rotator };
    rotator.parameter.defaultValueHint = "1 0.5 0.5";
    const RenderMaterialGraphCompileResult rotatorGraph = buildGraph(rotator, 0x4503U);

    const auto cook = [&](const RenderMaterialGraphCompileResult& compiled) {
        const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
        Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT45: coordinate graph must cook");
        return result;
    };
    const RenderMaterialGraphShaderArtifactResult tileOneResult = cook(tileOne);
    const RenderMaterialGraphShaderArtifactResult tileSmallResult = cook(tileSmall);
    const RenderMaterialGraphShaderArtifactResult pannerResult = cook(pannerGraph);
    const RenderMaterialGraphShaderArtifactResult rotatorResult = cook(rotatorGraph);
    const std::string samplerName = tileOne.shader.reflection.textures[0].samplerName;

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT45: Direct3D11 device unavailable; cannot run GPU coordinate node proof\n");
        Require(false, "KBMAT-MAT45: A real GPU device is required to prove coordinate transforms");
        return;
    }

    const std::array<std::uint32_t, 2U> gradient{ 0xff000000U, 0xff0000ffU }; // texel0 black, texel1 red (ABGR).
    bgfx::UniformHandle sampler = bgfx::createUniform(samplerName.c_str(), bgfx::UniformType::Sampler);
    bgfx::TextureHandle gradientTexture = bgfx::createTexture2D(2U, 1U, false, 1U, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy(gradient.data(), sizeof(gradient)));

    const bgfx::ProgramHandle tileOneProgram = BuildGraphProgram(vsBytes, *tileOneResult.artifact);
    const bgfx::ProgramHandle tileSmallProgram = BuildGraphProgram(vsBytes, *tileSmallResult.artifact);
    const bgfx::ProgramHandle pannerProgram = BuildGraphProgram(vsBytes, *pannerResult.artifact);
    const bgfx::ProgramHandle rotatorProgram = BuildGraphProgram(vsBytes, *rotatorResult.artifact);
    Require(bgfx::isValid(tileOneProgram) && bgfx::isValid(tileSmallProgram) && bgfx::isValid(pannerProgram) && bgfx::isValid(rotatorProgram),
        "KBMAT-MAT45: coordinate graph programs must link");

    // Tiling changes which texel the centre pixel reads.
    const ForwardRenderProbe tileOnePixel = harness.Render(tileOneProgram, sampler, gradientTexture);
    const ForwardRenderProbe tileSmallPixel = harness.Render(tileSmallProgram, sampler, gradientTexture);
    Require(tileOnePixel.r > tileSmallPixel.r + 40U, "KBMAT-MAT45: TextureCoordinate tiling must change the sampled texel");

    // Panner offset moves the sampled texel over time.
    const ForwardRenderProbe panAtZero = ForwardRenderHarness::ProbeAt(harness.RenderPixels(pannerProgram, sampler, gradientTexture, 0.0F), 32U, 32U);
    const ForwardRenderProbe panAtHalf = ForwardRenderHarness::ProbeAt(harness.RenderPixels(pannerProgram, sampler, gradientTexture, 0.5F), 32U, 32U);
    Require(panAtZero.r > panAtHalf.r + 40U, "KBMAT-MAT45: Panner time offset must scroll the sampled texel");

    // Rotation moves an off-centre sample across the texel boundary.
    const ForwardRenderProbe rotAtZero = ForwardRenderHarness::ProbeAt(harness.RenderPixels(rotatorProgram, sampler, gradientTexture, 0.0F), 16U, 32U);
    const ForwardRenderProbe rotAtHalfPi = ForwardRenderHarness::ProbeAt(harness.RenderPixels(rotatorProgram, sampler, gradientTexture, 3.14159F), 16U, 32U);
    Require((rotAtZero.r > rotAtHalfPi.r + 40U) || (rotAtHalfPi.r > rotAtZero.r + 40U),
        "KBMAT-MAT45: Rotator must rotate the sampled coordinate (off-centre pixel changes texel)");

    bgfx::destroy(gradientTexture);
    bgfx::destroy(sampler);
    harness.Shutdown();
}

// MAT-46: world-space view nodes carry correct per-pixel values. CameraVector at the centre of a quad
// facing the camera points back along +Z, so routing it to baseColor renders blue; a Fresnel built from
// CameraVector brightens the grazing edge relative to the centre (a real rim term).
void RunForwardGraphWorldSpaceNodesTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat46_world";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT46: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    // CameraVector -> baseColor (unlit): the centre view vector is ~ (0,0,1).
    RenderMaterialGraphDocument cameraGraph = MakeDefaultRenderMaterialGraphDocument();
    cameraGraph.shadingModel = "unlit";
    cameraGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::CameraVector });
    cameraGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::CameraVector, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult cameraCompiled = CompileRenderMaterialGraphToShaderSource(cameraGraph, RenderMaterialGraphBuildContext{ .assetId = 0x4600U });
    Require(cameraCompiled.Succeeded(), "KBMAT-MAT46: CameraVector graph must compile");
    const RenderMaterialGraphShaderArtifactResult cameraResult = CookRenderMaterialGraphShaderArtifact(cameraCompiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(cameraResult.Succeeded() && cameraResult.artifact.has_value(), "KBMAT-MAT46: CameraVector graph must cook");

    // Fresnel(view = CameraVector) -> metallic (DefaultLit): grazing edges read a higher fresnel.
    RenderMaterialGraphDocument fresnelGraph = MakeDefaultRenderMaterialGraphDocument();
    fresnelGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::CameraVector });
    fresnelGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::Fresnel });
    fresnelGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::CameraVector, 2U, "value", RenderMaterialGraphNodeKind::Fresnel, 3U, "view"));
    fresnelGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::Fresnel, 3U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"));
    const RenderMaterialGraphCompileResult fresnelCompiled = CompileRenderMaterialGraphToShaderSource(fresnelGraph, RenderMaterialGraphBuildContext{ .assetId = 0x4601U });
    Require(fresnelCompiled.Succeeded(), "KBMAT-MAT46: Fresnel-from-CameraVector graph must compile");
    const RenderMaterialGraphShaderArtifactResult fresnelResult = CookRenderMaterialGraphShaderArtifact(fresnelCompiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(fresnelResult.Succeeded() && fresnelResult.artifact.has_value(), "KBMAT-MAT46: Fresnel graph must cook");

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT46: Direct3D11 device unavailable; cannot run GPU world-space node proof\n");
        Require(false, "KBMAT-MAT46: A real GPU device is required to prove world-space view nodes");
        return;
    }

    const bgfx::ProgramHandle cameraProgram = BuildGraphProgram(vsBytes, *cameraResult.artifact);
    Require(bgfx::isValid(cameraProgram), "KBMAT-MAT46: CameraVector program must link");
    const ForwardRenderProbe cameraPixel = ForwardRenderHarness::ProbeAt(harness.RenderPixels(cameraProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE), 32U, 32U);
    Require(cameraPixel.b > cameraPixel.r + 40U && cameraPixel.b > cameraPixel.g + 40U,
        "KBMAT-MAT46: the centre CameraVector points at the camera (+Z), so baseColor must render blue-dominant");

    const bgfx::ProgramHandle fresnelProgram = BuildGraphProgram(vsBytes, *fresnelResult.artifact);
    Require(bgfx::isValid(fresnelProgram), "KBMAT-MAT46: Fresnel program must link");
    const ForwardRenderProbe fresnelCenter = ForwardRenderHarness::ProbeAt(harness.RenderPixels(fresnelProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE), 32U, 32U);
    const int centerLum = static_cast<int>(fresnelCenter.r) + static_cast<int>(fresnelCenter.g) + static_cast<int>(fresnelCenter.b);
    Require(centerLum > 60,
        "KBMAT-MAT46: a Fresnel built from CameraVector must drive a real lit surface on the GPU (CameraVector feeds the rim term)");

    harness.Shutdown();
}
#endif

} // namespace

void RunGraphForwardGpuRenderTests() {
#if defined(KB_TEST_GRAPH_SHADERC_PATH)
    RunForwardGraphRenderTest();
    RunForwardGraphTimeAnimationTest();
    RunForwardGraphUv1SamplingTest();
    RunForwardGraphVertexColorTest();
    RunForwardGraphScreenPositionTest();
    RunForwardGraphObjectSpaceTest();
    RunForwardGraphPerInstanceTest();
    RunForwardGraphSamplerStateTest();
    RunForwardGraphTransparentBlendTest();
    RunForwardGraphWorldPositionOffsetTest();
    RunForwardGraphSceneDepthCooksTest();
    RunForwardGraphMaterialAttributesTest();
    RunForwardGraphBlendAttributesTest();
    RunForwardGraphGetSetAttributesTest();
    RunForwardGraphMaterialOutputAttributesTest();
    RunForwardGraphShadingModelTest();
    RunForwardGraphMaskedDiscardTest();
    RunForwardGraphBlendModeCompositeTest();
    RunForwardGraphStaticSwitchTest();
    RunForwardGraphCoordinateNodesTest();
    RunForwardGraphWorldSpaceNodesTest();
#endif
}

} // namespace kb::render::tests
