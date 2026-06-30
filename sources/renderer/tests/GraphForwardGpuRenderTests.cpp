#include "RendererTestSupport.hpp"

#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
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
            "$output v_normal, v_color0, v_texcoord0, v_worldPos, v_shadowPos, v_shadowFlags, v_tangent, v_bitangent\n\n"
            "#include <bgfx_shader.sh>\n\n"
            "void main()\n{\n"
            "    gl_Position = vec4(a_position, 1.0);\n"
            "    v_normal = vec3(0.0, 0.0, 1.0);\n"
            "    v_color0 = vec4(1.0, 1.0, 1.0, 1.0);\n"
            "    v_texcoord0 = a_texcoord0;\n"
            "    v_worldPos = vec3(a_position.xy, 0.0);\n"
            "    v_shadowPos = vec4(0.0, 0.0, 0.0, 1.0);\n"
            "    v_shadowFlags = vec4(0.0, 0.0, 0.0, 0.0);\n"
            "    v_tangent = vec3(1.0, 0.0, 0.0);\n"
            "    v_bitangent = vec3(0.0, 1.0, 0.0);\n"
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
        bgfx::frame();
        bgfx::frame();
        return bgfx::isValid(vbh_) && bgfx::isValid(fb_);
    }

    [[nodiscard]] ForwardRenderProbe Render(bgfx::ProgramHandle program, bgfx::UniformHandle sampler, bgfx::TextureHandle texture) {
        const std::array<float, 4U> camera{ 0.0F, 0.0F, 1.0F, 0.0F };
        const std::array<float, 4U> lightParams{ 0.0F, 0.0F, 0.0F, 0.0F };
        const std::array<float, 4U> ambient{ 1.0F, 1.0F, 1.0F, 1.0F };
        const std::array<float, 4U> envParams{ 1.0F, 1.0F, 0.0F, 0.0F };
        bgfx::setViewFrameBuffer(0, fb_);
        bgfx::setViewRect(0, 0U, 0U, 64U, 64U);
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR, 0x000000ff, 1.0F, 0);
        bgfx::setUniform(uCamera_, camera.data());
        bgfx::setUniform(uLightParams_, lightParams.data());
        bgfx::setUniform(uAmbient_, ambient.data());
        bgfx::setUniform(uEnvParams_, envParams.data());
        if (bgfx::isValid(sampler) && bgfx::isValid(texture)) {
            bgfx::setTexture(0U, sampler, texture);
        }
        bgfx::setVertexBuffer(0, vbh_);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(0, program);
        bgfx::frame();
        bgfx::blit(1, readTex_, 0U, 0U, rt_, 0U, 0U, 64U, 64U);
        std::vector<std::uint8_t> pixels(64U * 64U * 4U, 0U);
        const std::uint32_t readyFrame = bgfx::readTexture(readTex_, pixels.data());
        std::uint32_t frame = bgfx::frame();
        int guard = 0;
        while (frame < readyFrame && guard < 8) { frame = bgfx::frame(); ++guard; }
        const std::size_t center = (32U * 64U + 32U) * 4U;
        return ForwardRenderProbe{ pixels[center], pixels[center + 1U], pixels[center + 2U], pixels[center + 3U] };
    }

    void Shutdown() {
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
#endif

} // namespace

void RunGraphForwardGpuRenderTests() {
#if defined(KB_TEST_GRAPH_SHADERC_PATH)
    RunForwardGraphRenderTest();
#endif
}

} // namespace kb::render::tests
