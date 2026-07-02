#include "RendererTestSupport.hpp"

#include "kb/render/MaterialProgramRegistry.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialGraphProgramBindingBuilder.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
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

[[nodiscard]] bool HasGraphDiagnostic(
    const std::vector<RenderMaterialGraphDiagnostic>& diagnostics,
    RenderMaterialGraphDiagnosticKind kind) {
    for (const RenderMaterialGraphDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.kind == kind) {
            return true;
        }
    }
    return false;
}

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
            "$output v_normal, v_color0, v_texcoord0, v_worldPos, v_shadowPos, v_shadowFlags, v_tangent, v_bitangent, v_objectLocalPos, v_objectWorldPos, v_objectOrientation, v_preSkinnedNormal\n\n"
            "#include <bgfx_shader.sh>\n\n"
            "uniform vec4 u_time;\n\n"
            "void main()\n{\n"
            "    gl_Position = vec4(a_position, 1.0);\n"
            "    v_normal = vec3(0.0, 0.0, 1.0);\n"
            "    v_color0 = vec4(0.15, 0.45, 0.85, 0.6);\n"
            "    v_texcoord0 = a_texcoord0;\n"
            "    v_worldPos = vec3(a_position.xy, 0.0);\n"
            "    v_shadowPos = vec4(0.0, 0.0, 0.0, 1.0);\n"
            "    v_shadowFlags = vec4(0.0, u_time.z, a_texcoord0.x * 0.1, a_texcoord0.y * 0.1);\n"
            "    v_tangent = vec3(1.0, 0.0, 0.0);\n"
            "    v_bitangent = vec3(0.0, 1.0, 0.0);\n"
            // MAT-76: simulate an object translated by (-0.7,0,0); object-space local position
            // differs from world position by a known positive offset so the proof can tell them apart.
            // MAT-77: the per-instance scalar lanes (.w) are driven by u_time.y/.z so a test can inject
            // distinct per-instance values exactly as the real instance buffer packs into i_data0/1.w.
            "    v_objectLocalPos = vec4(v_worldPos.x + 0.7, v_worldPos.y, v_worldPos.z, u_time.y);\n"
            "    v_objectWorldPos = vec4(0.7, 0.0, 0.0, u_time.z);\n"
            "    v_objectOrientation = vec4(normalize(vec3(0.2, 0.7, 0.1)), u_time.z);\n"
            "    v_preSkinnedNormal = normalize(vec3(0.1, 0.8, 0.2));\n"
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
            "$output v_normal, v_color0, v_texcoord0, v_worldPos, v_shadowPos, v_shadowFlags, v_tangent, v_bitangent, v_objectLocalPos, v_objectWorldPos, v_objectOrientation, v_preSkinnedNormal\n\n"
            "#include <bgfx_shader.sh>\n\n"
            "uniform vec4 u_time;\n"
            "uniform vec4 u_dynamicParameter;\n\n"
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
            "    ctx.deltaTime = u_time.y;\n"
            "    ctx.dynamicParameter = u_dynamicParameter;\n"
            "    ctx.screenPosition = vec2(0.0, 0.0);\n"
            "    ctx.localPosition = a_position;\n"
            "    ctx.objectPosition = vec3(0.0, 0.0, 0.0);\n"
            "    ctx.perInstanceRandom = 0.0;\n"
            "    ctx.perInstanceFadeAmount = 1.0;\n"
            "    ctx.perInstanceCustomData = 0.0;\n"
            "    ctx.objectRadius = 0.0;\n"
            "    ctx.objectBounds = vec4(0.0, 0.0, 0.0, 0.0);\n"
            "    ctx.objectOrientation = vec3(0.0, 0.0, 1.0);\n"
            "    ctx.preSkinnedPosition = a_position;\n"
            "    ctx.preSkinnedNormal = vec3(0.0, 0.0, 1.0);\n"
            "    ctx.twoSidedSign = 1.0;\n"
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
            "    v_objectOrientation = vec4(0.0, 0.0, 1.0, 0.0);\n"
            "    v_preSkinnedNormal = vec3(0.0, 0.0, 1.0);\n"
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

[[nodiscard]] bool CookHarnessVertexDomainOutputShader(
    const std::filesystem::path& outBin,
    const std::string& graphSource,
    std::string_view positionExpression,
    std::string_view uvExpression) {
    const std::filesystem::path src = outBin.parent_path() / "vs_graph_vertex_domain_probe.sc";
    {
        std::ofstream out{ src, std::ios::binary | std::ios::trunc };
        out <<
            "$input a_position, a_texcoord0\n"
            "$output v_normal, v_color0, v_texcoord0, v_worldPos, v_shadowPos, v_shadowFlags, v_tangent, v_bitangent, v_objectLocalPos, v_objectWorldPos, v_objectOrientation, v_preSkinnedNormal\n\n"
            "#include <bgfx_shader.sh>\n\n"
            "uniform vec4 u_time;\n"
            "uniform vec4 u_dynamicParameter;\n\n"
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
            "    ctx.deltaTime = u_time.y;\n"
            "    ctx.dynamicParameter = u_dynamicParameter;\n"
            "    ctx.screenPosition = vec2(0.0, 0.0);\n"
            "    ctx.localPosition = a_position;\n"
            "    ctx.objectPosition = vec3(0.0, 0.0, 0.0);\n"
            "    ctx.perInstanceRandom = 0.0;\n"
            "    ctx.perInstanceFadeAmount = 1.0;\n"
            "    ctx.perInstanceCustomData = 0.0;\n"
            "    ctx.objectRadius = 0.0;\n"
            "    ctx.objectBounds = vec4(0.0, 0.0, 0.0, 0.0);\n"
            "    ctx.objectOrientation = vec3(0.0, 0.0, 1.0);\n"
            "    ctx.preSkinnedPosition = a_position;\n"
            "    ctx.preSkinnedNormal = vec3(0.0, 0.0, 1.0);\n"
            "    ctx.twoSidedSign = 1.0;\n"
            "    vec3 worldPos = " << positionExpression << ";\n"
            "    gl_Position = vec4(worldPos, 1.0);\n"
            "    v_normal = vec3(0.0, 0.0, 1.0);\n"
            "    v_color0 = vec4(0.2, 0.6, 0.9, 1.0);\n"
            "    v_texcoord0 = " << uvExpression << ";\n"
            "    v_worldPos = worldPos;\n"
            "    v_shadowPos = vec4(0.0, 0.0, 0.0, 1.0);\n"
            "    v_shadowFlags = vec4(0.0, 0.0, 0.0, 0.0);\n"
            "    v_tangent = vec3(1.0, 0.0, 0.0);\n"
            "    v_bitangent = vec3(0.0, 1.0, 0.0);\n"
            "    v_objectLocalPos = vec4(a_position, 0.0);\n"
            "    v_objectWorldPos = vec4(0.0, 0.0, 0.0, 0.0);\n"
            "    v_objectOrientation = vec4(0.0, 0.0, 1.0, 0.0);\n"
            "    v_preSkinnedNormal = vec3(0.0, 0.0, 1.0);\n"
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

[[nodiscard]] RenderMaterialGraphLink MakeLink(const RenderMaterialGraphNode& fromNode, std::string fromPin, const RenderMaterialGraphNode& toNode, std::string toPin) {
    RenderMaterialGraphLink link{
        .fromNodeId = fromNode.id,
        .fromPinId = RenderMaterialGraphStablePinId(fromNode, fromPin, true),
        .fromPin = std::move(fromPin),
        .toNodeId = toNode.id,
        .toPinId = RenderMaterialGraphStablePinId(toNode, toPin, false),
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
        uDynamicParameter_ = bgfx::createUniform("u_dynamicParameter", bgfx::UniformType::Vec4);
        uSceneColor_ = bgfx::createUniform("s_kbSceneColor", bgfx::UniformType::Sampler);
        uSceneDepth_ = bgfx::createUniform("s_kbSceneDepth", bgfx::UniformType::Sampler);
        bgfx::frame();
        bgfx::frame();
        return bgfx::isValid(vbh_) && bgfx::isValid(fb_);
    }

    [[nodiscard]] std::vector<std::uint8_t> RenderPixels(bgfx::ProgramHandle program, bgfx::UniformHandle sampler, bgfx::TextureHandle texture, float time = 0.0F, float instanceRandom = 0.0F, float objectRadius = 0.0F, std::uint32_t samplerFlags = UINT32_MAX, std::uint32_t clearColor = 0x000000ffU, std::uint64_t extraState = 0U, bgfx::TextureHandle sceneDepthTexture = BGFX_INVALID_HANDLE, float dynamicR = 0.0F, float dynamicG = 0.0F, float dynamicB = 0.0F, float dynamicA = 0.0F, bgfx::TextureHandle sceneColorTexture = BGFX_INVALID_HANDLE, bgfx::UniformHandle graphUniform = BGFX_INVALID_HANDLE, const std::array<float, 4U>* graphUniformValue = nullptr) {
        const std::array<float, 4U> camera{ 0.0F, 0.0F, 1.0F, 0.0F };
        const std::array<float, 4U> lightParams{ 0.0F, 0.0F, 0.0F, 0.0F };
        const std::array<float, 4U> ambient{ 1.0F, 1.0F, 1.0F, 1.0F };
        const std::array<float, 4U> envParams{ 1.0F, 1.0F, 0.0F, 0.0F };
        // u_time.x = time (MAT-72); .y = per-instance random, .z = object radius (MAT-77 proof lanes).
        const std::array<float, 4U> timeConstants{ time, instanceRandom, objectRadius, 0.0F };
        const std::array<float, 4U> dynamicParameter{ dynamicR, dynamicG, dynamicB, dynamicA };
        bgfx::setViewFrameBuffer(0, fb_);
        bgfx::setViewRect(0, 0U, 0U, 64U, 64U);
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR, clearColor, 1.0F, 0);
        bgfx::setUniform(uCamera_, camera.data());
        bgfx::setUniform(uLightParams_, lightParams.data());
        bgfx::setUniform(uAmbient_, ambient.data());
        bgfx::setUniform(uEnvParams_, envParams.data());
        bgfx::setUniform(uTime_, timeConstants.data());
        bgfx::setUniform(uDynamicParameter_, dynamicParameter.data());
        if (bgfx::isValid(graphUniform) && graphUniformValue != nullptr) {
            bgfx::setUniform(graphUniform, graphUniformValue->data());
        }
        if (bgfx::isValid(sampler) && bgfx::isValid(texture)) {
            // Graph textures bind at the graph base stage (6); builtin stages 0-5 are reserved (MAT-78).
            bgfx::setTexture(kRenderMaterialGraphTextureBaseSlot, sampler, texture, samplerFlags);
        }
        if (bgfx::isValid(sceneDepthTexture)) {
            bgfx::setTexture(5U, uSceneDepth_, sceneDepthTexture);
        }
        if (bgfx::isValid(sceneColorTexture)) {
            bgfx::setTexture(4U, uSceneColor_, sceneColorTexture);
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

    [[nodiscard]] std::vector<std::uint8_t> RenderSplitPixels(
        bgfx::ProgramHandle leftProgram,
        bgfx::ProgramHandle rightProgram,
        std::uint32_t clearColor = 0x000000ffU) {
        const std::array<float, 4U> camera{ 0.0F, 0.0F, 1.0F, 0.0F };
        const std::array<float, 4U> lightParams{ 0.0F, 0.0F, 0.0F, 0.0F };
        const std::array<float, 4U> ambient{ 1.0F, 1.0F, 1.0F, 1.0F };
        const std::array<float, 4U> envParams{ 1.0F, 1.0F, 0.0F, 0.0F };
        const std::array<float, 4U> timeConstants{ 0.0F, 0.0F, 0.0F, 0.0F };
        const std::array<float, 4U> dynamicParameter{ 0.0F, 0.0F, 0.0F, 0.0F };

        bgfx::setViewFrameBuffer(0, fb_);
        bgfx::setViewRect(0, 0U, 0U, 64U, 64U);
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR, clearColor, 1.0F, 0);
        bgfx::setUniform(uCamera_, camera.data());
        bgfx::setUniform(uLightParams_, lightParams.data());
        bgfx::setUniform(uAmbient_, ambient.data());
        bgfx::setUniform(uEnvParams_, envParams.data());
        bgfx::setUniform(uTime_, timeConstants.data());
        bgfx::setUniform(uDynamicParameter_, dynamicParameter.data());

        struct SplitVertex {
            float x;
            float y;
            float z;
            float u;
            float v;
        };
        const auto submitHalf = [this](bgfx::ProgramHandle program, bool left) {
            const float x0 = left ? -1.0F : 0.0F;
            const float x1 = left ? 0.0F : 1.0F;
            const std::array<SplitVertex, 6U> vertices{ {
                { x0, -1.0F, 0.0F, 0.0F, 1.0F },
                { x0,  1.0F, 0.0F, 0.0F, 0.0F },
                { x1,  1.0F, 0.0F, 1.0F, 0.0F },
                { x0, -1.0F, 0.0F, 0.0F, 1.0F },
                { x1,  1.0F, 0.0F, 1.0F, 0.0F },
                { x1, -1.0F, 0.0F, 1.0F, 1.0F },
            } };
            bgfx::TransientVertexBuffer tvb{};
            Require(bgfx::getAvailTransientVertexBuffer(static_cast<std::uint32_t>(vertices.size()), layout_) >= vertices.size(),
                "KBMAT-MAT68: split-frame acceptance draw could not allocate transient vertices");
            bgfx::allocTransientVertexBuffer(&tvb, static_cast<std::uint32_t>(vertices.size()), layout_);
            std::memcpy(tvb.data, vertices.data(), sizeof(vertices));
            bgfx::setVertexBuffer(0, &tvb);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
            bgfx::submit(0, program);
        };
        submitHalf(leftProgram, true);
        submitHalf(rightProgram, false);

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
        bgfx::destroy(uSceneDepth_);
        bgfx::destroy(uSceneColor_);
        bgfx::destroy(uDynamicParameter_);
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
    bgfx::UniformHandle uDynamicParameter_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uSceneColor_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uSceneDepth_ = BGFX_INVALID_HANDLE;
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

struct AcceptanceCookedMaterial {
    RenderMaterialGraphDocument graph;
    RenderMaterialGraphShaderSource shader;
    RenderMaterialGraphShaderArtifact artifact;
    MaterialProgramKey key;
};

[[nodiscard]] RenderMaterialGraphDocument MakeAcceptanceColorGraph(
    std::string_view colorHint,
    std::string_view shadingModel,
    std::string_view blendMode = "opaque") {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.shadingModel = std::string{ shadingModel };
    graph.blendMode = std::string{ blendMode };
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 80,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = std::string{ colorHint } },
    });
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    return graph;
}

[[nodiscard]] RenderMaterialAssetData RoundTripAcceptanceMaterialAsset(const RenderMaterialGraphDocument& graph) {
    RenderMaterialAssetData material{};
    material.graph = graph;
    std::ostringstream output;
    RenderMaterialAssetWriter::Write(output, material);
    std::istringstream input{ output.str() };
    RenderMaterialAssetParseResult parsed = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(parsed.Succeeded() && parsed.asset.has_value(),
        "KBMAT-MAT68: editor-authored graph material asset must round-trip before acceptance cook");
    return *parsed.asset;
}

[[nodiscard]] MaterialProgramKey AcceptanceProgramKey(
    const RenderMaterialGraphShaderSource& shader,
    std::string pass,
    std::uint64_t materialTypeId,
    std::uint32_t materialTypeVersion) {
    return MaterialProgramKey{
        .materialTypeId = materialTypeId,
        .materialTypeVersion = materialTypeVersion,
        .graphSourceHash = shader.sourceHash,
        .variantKey = RenderMaterialGraphVariantKey(shader),
        .pass = std::move(pass),
        .backend = static_cast<std::uint32_t>(RenderMaterialGraphShaderBackend::Dxbc),
        .pipelineStateKey = RenderMaterialGraphPipelineStateKey(shader),
        .graphProgram = true,
    };
}

[[nodiscard]] AcceptanceCookedMaterial CookAcceptanceMaterial(
    const RenderMaterialGraphDocument& sourceGraph,
    std::uint64_t assetId,
    const std::filesystem::path& cacheDir) {
    const RenderMaterialAssetData material = RoundTripAcceptanceMaterialAsset(sourceGraph);
    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(
        material.graph,
        RenderMaterialGraphBuildContext{ .assetId = assetId, .sourcePath = "/Game/Materials/Acceptance.kbmat" });
    Require(compiled.Succeeded(), "KBMAT-MAT68: acceptance graph must compile after material asset round-trip");
    const RenderMaterialGraphBlendMode blendMode = compiled.shader.reflection.blendMode;
    const std::string pass = IsRenderMaterialGraphBlendModeTransparent(blendMode) ? "BaseTransparent" : "BaseOpaque";
    RenderMaterialGraphShaderArtifactRequest request = CookRequest(cacheDir.generic_string());
    request.pass = pass;
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };
    const RenderMaterialGraphShaderArtifactResult cooked = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, request);
    Require(cooked.Succeeded() && cooked.artifact.has_value(), "KBMAT-MAT68: acceptance graph must cook to a real binary");
    const RenderMaterialGraphShaderBinary* dxbc = cooked.artifact->FindBinary(RenderMaterialGraphShaderBackend::Dxbc);
    Require(dxbc != nullptr && dxbc->byteSize > 0U && std::filesystem::exists(std::filesystem::path{ dxbc->binaryPath }),
        "KBMAT-MAT68: acceptance cook must leave a non-empty fragment binary on disk");
    return AcceptanceCookedMaterial{
        .graph = material.graph,
        .shader = compiled.shader,
        .artifact = *cooked.artifact,
        .key = AcceptanceProgramKey(compiled.shader, pass, assetId, 1U),
    };
}

[[nodiscard]] std::uint64_t AcceptanceBlendState(RenderMaterialGraphBlendMode mode) noexcept {
    switch (mode) {
    case RenderMaterialGraphBlendMode::Additive:
        return BGFX_STATE_BLEND_ADD;
    case RenderMaterialGraphBlendMode::Modulate:
        return BGFX_STATE_BLEND_MULTIPLY;
    case RenderMaterialGraphBlendMode::AlphaComposite:
        return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    case RenderMaterialGraphBlendMode::AlphaHoldout:
        return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    case RenderMaterialGraphBlendMode::Translucent:
        return BGFX_STATE_BLEND_ALPHA;
    case RenderMaterialGraphBlendMode::Opaque:
    case RenderMaterialGraphBlendMode::Masked:
        return 0U;
    }
    return 0U;
}

[[nodiscard]] bool SimilarProbe(const ForwardRenderProbe& lhs, const ForwardRenderProbe& rhs, std::uint8_t tolerance) noexcept {
    const auto delta = [](std::uint8_t a, std::uint8_t b) {
        return a > b ? static_cast<std::uint8_t>(a - b) : static_cast<std::uint8_t>(b - a);
    };
    return delta(lhs.r, rhs.r) <= tolerance &&
        delta(lhs.g, rhs.g) <= tolerance &&
        delta(lhs.b, rhs.b) <= tolerance &&
        delta(lhs.a, rhs.a) <= tolerance;
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

    // ConstantBool -> BaseColor proves bool schema, codegen, cook, link, and GPU execution with readback.
    const auto buildBoolGraph = [](std::string_view hint) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.shadingModel = "unlit";
        RenderMaterialGraphNode boolNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantBool, .positionX = 40, .positionY = 40 };
        boolNode.parameter.defaultValueHint = std::string{ hint };
        graph.nodes.push_back(boolNode);
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantBool, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        return graph;
    };
    const RenderMaterialGraphShaderArtifactResult boolTrueResult = cookGraph(buildBoolGraph("true"), 0x0806U);
    const RenderMaterialGraphShaderArtifactResult boolFalseResult = cookGraph(buildBoolGraph("false"), 0x0807U);

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
    const bgfx::ProgramHandle boolTrueProgram = BuildGraphProgram(vsBytes, *boolTrueResult.artifact);
    const bgfx::ProgramHandle boolFalseProgram = BuildGraphProgram(vsBytes, *boolFalseResult.artifact);
    Require(bgfx::isValid(redProgram) && bgfx::isValid(blueProgram) && bgfx::isValid(textureProgram) &&
            bgfx::isValid(boolTrueProgram) && bgfx::isValid(boolFalseProgram),
        "KBMAT-MAT08: Forward graph programs must link on the real GPU backend");

    const ForwardRenderProbe red = harness.Render(redProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    const ForwardRenderProbe blue = harness.Render(blueProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    const ForwardRenderProbe boolTrue = harness.Render(boolTrueProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    const ForwardRenderProbe boolFalse = harness.Render(boolFalseProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    Require(red.r > red.g && red.r > red.b && red.r > 80U,
        "KBMAT-MAT08: A red ConstantColor graph must render a red-dominant pixel through the GPU graph program");
    Require(blue.b > blue.r && blue.b > blue.g && blue.b > 80U,
        "KBMAT-MAT08: A blue ConstantColor graph must render a blue-dominant pixel through the GPU graph program");
    Require(red.a == 255U, "KBMAT-MAT08: Surface alpha must drive the rendered output alpha");
    Require(boolTrue.r > boolFalse.r + 64U && boolTrue.g > boolFalse.g + 64U && boolTrue.b > boolFalse.b + 64U,
        "KBMAT-MAT08: ConstantBool true/false must produce distinct GPU pixels through bool-to-color codegen");

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

void RunForwardGraphAcceptanceSuiteTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat68_acceptance";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT68: acceptance harness vertex shader must cook");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);

    std::vector<AcceptanceCookedMaterial> cookedMaterials;
    cookedMaterials.reserve(32U);
    const auto cookAndTrack = [&cookedMaterials, &cacheDir](const RenderMaterialGraphDocument& graph, std::uint64_t assetId) -> const AcceptanceCookedMaterial& {
        cookedMaterials.push_back(CookAcceptanceMaterial(graph, assetId, cacheDir));
        return cookedMaterials.back();
    };

    const AcceptanceCookedMaterial& red = cookAndTrack(MakeAcceptanceColorGraph("0.85 0.05 0.03 1", "unlit"), 0x6800U);
    const AcceptanceCookedMaterial& green = cookAndTrack(MakeAcceptanceColorGraph("0.04 0.85 0.08 1", "unlit"), 0x6801U);
    const AcceptanceCookedMaterial& litBlue = cookAndTrack(MakeAcceptanceColorGraph("0.05 0.12 0.85 1", "defaultLit"), 0x6802U);

    MaterialProgramRegistry registry;
    registry.Configure(
        [&cookedMaterials, &vsBytes](const MaterialProgramKey& key) -> bgfx::ProgramHandle {
            for (const AcceptanceCookedMaterial& material : cookedMaterials) {
                if (material.key == key) {
                    return BuildGraphProgram(vsBytes, material.artifact);
                }
            }
            return BGFX_INVALID_HANDLE;
        },
        [](bgfx::ProgramHandle handle) {
            if (bgfx::isValid(handle)) {
                bgfx::destroy(handle);
            }
        },
        0U);

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT68: Direct3D11 device unavailable; cannot run GPU acceptance suite\n");
        Require(false, "KBMAT-MAT68: A real GPU device is required for the acceptance suite");
        return;
    }

    const bgfx::ProgramHandle previewRedProgram = BuildGraphProgram(vsBytes, red.artifact);
    const bgfx::ProgramHandle sceneRedProgram = registry.Acquire(red.key);
    const bgfx::ProgramHandle sceneGreenProgram = registry.Acquire(green.key);
    const bgfx::ProgramHandle sceneLitBlueProgram = registry.Acquire(litBlue.key);
    Require(bgfx::isValid(previewRedProgram) &&
            bgfx::isValid(sceneRedProgram) &&
            bgfx::isValid(sceneGreenProgram) &&
            bgfx::isValid(sceneLitBlueProgram),
        "KBMAT-MAT68: preview and registry scene programs must all link");

    const ForwardRenderProbe previewRed = harness.Render(previewRedProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    const ForwardRenderProbe sceneRed = harness.Render(sceneRedProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    Require(SimilarProbe(previewRed, sceneRed, 2U),
        "KBMAT-MAT68: preview and registry/scene graph programs must render the same pixel");
    Require(sceneRed.r > sceneRed.g + 40U && sceneRed.r > sceneRed.b + 40U && sceneRed.a == 255U,
        "KBMAT-MAT68: red acceptance material must verify the R and A readback channels");

    const ForwardRenderProbe sceneGreen = harness.Render(sceneGreenProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    const ForwardRenderProbe sceneBlue = harness.Render(sceneLitBlueProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    Require(sceneGreen.g > sceneGreen.r + 40U && sceneGreen.g > sceneGreen.b + 40U,
        "KBMAT-MAT68: green acceptance material must verify the G readback channel");
    Require(sceneBlue.b > sceneBlue.r + 20U && sceneBlue.b > sceneBlue.g + 20U,
        "KBMAT-MAT68: default-lit acceptance material must render a blue-dominant pixel");

    const std::vector<std::uint8_t> splitPixels = harness.RenderSplitPixels(sceneRedProgram, sceneGreenProgram);
    const ForwardRenderProbe leftPixel = ForwardRenderHarness::ProbeAt(splitPixels, 16U, 32U);
    const ForwardRenderProbe rightPixel = ForwardRenderHarness::ProbeAt(splitPixels, 48U, 32U);
    Require(leftPixel.r > rightPixel.r + 40U && rightPixel.g > leftPixel.g + 40U,
        "KBMAT-MAT68: two different graph materials must produce two different pixels in one frame");

    const std::array<std::string_view, 7U> blendModes{
        "opaque", "masked", "translucent", "additive", "modulate", "alphaComposite", "alphaHoldout"
    };
    for (std::size_t index = 0U; index < blendModes.size(); ++index) {
        const AcceptanceCookedMaterial& blended = cookAndTrack(
            MakeAcceptanceColorGraph("0.25 0.50 0.90 0.55", "unlit", blendModes[index]),
            0x6810U + index);
        const RenderMaterialGraphProgramBindingResult binding =
            BuildRenderMaterialGraphProgramBinding(0x6810U + index, 1U, blended.shader, std::span<const RenderMaterialGraphParameterValue>{});
        Require(binding.binding.pipelineStateKey == blended.key.pipelineStateKey,
            "KBMAT-MAT68: blend-mode acceptance material must carry the cooked pipeline-state key");
        const bgfx::ProgramHandle program = registry.Acquire(blended.key);
        Require(bgfx::isValid(program), "KBMAT-MAT68: every blend-mode acceptance material must load through the registry");
        const RenderMaterialGraphBlendMode mode = blended.shader.reflection.blendMode;
        const std::uint32_t background =
            mode == RenderMaterialGraphBlendMode::Modulate || mode == RenderMaterialGraphBlendMode::AlphaHoldout
                ? 0xffffffffU
                : 0x000000ffU;
        const ForwardRenderProbe pixel = ForwardRenderHarness::ProbeAt(
            harness.RenderPixels(program, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.0F, 0.0F, 0.0F, UINT32_MAX, background, AcceptanceBlendState(mode)),
            32U,
            32U);
        Require(static_cast<std::uint32_t>(pixel.r) + pixel.g + pixel.b > 30U,
            "KBMAT-MAT68: every declared graph blend mode must produce a readable GPU pixel");
        if (mode == RenderMaterialGraphBlendMode::AlphaHoldout) {
            Require(pixel.r < 240U,
                "KBMAT-MAT68: alpha-holdout acceptance draw must affect the destination color");
        }
    }

    RenderMaterialGraphDocument overrideGraph = MakeDefaultRenderMaterialGraphDocument();
    overrideGraph.shadingModel = "unlit";
    overrideGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ParameterColor,
        .positionX = 80,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "tint", .displayName = "Tint", .defaultValueHint = "1 1 1 1" },
    });
    overrideGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ParameterColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const AcceptanceCookedMaterial& overrideMaterial = cookAndTrack(overrideGraph, 0x6820U);
    Require(overrideMaterial.shader.reflection.uniforms.size() == 1U,
        "KBMAT-MAT68: instance-override acceptance graph must expose one uniform");
    const std::array<RenderMaterialGraphParameterValue, 1U> redOverride{
        RenderMaterialGraphParameterValue{ .stableId = "tint", .type = RenderMaterialParameterType::Color, .numbers = { 0.9F, 0.05F, 0.05F, 1.0F } },
    };
    const std::array<RenderMaterialGraphParameterValue, 1U> blueOverride{
        RenderMaterialGraphParameterValue{ .stableId = "tint", .type = RenderMaterialParameterType::Color, .numbers = { 0.05F, 0.05F, 0.9F, 1.0F } },
    };
    const RenderMaterialGraphProgramBindingResult redBinding =
        BuildRenderMaterialGraphProgramBinding(0x6820U, 1U, overrideMaterial.shader, redOverride);
    const RenderMaterialGraphProgramBindingResult blueBinding =
        BuildRenderMaterialGraphProgramBinding(0x6820U, 1U, overrideMaterial.shader, blueOverride);
    Require(redBinding.binding.graphSourceHash == blueBinding.binding.graphSourceHash &&
            redBinding.binding.variantKey == blueBinding.binding.variantKey &&
            redBinding.binding.uniforms.size() == 1U &&
            blueBinding.binding.uniforms.size() == 1U,
        "KBMAT-MAT68: dynamic instance override must update uniform values without changing the graph program");
    const bgfx::ProgramHandle overrideProgram = registry.Acquire(overrideMaterial.key);
    bgfx::UniformHandle tintUniform = bgfx::createUniform(
        overrideMaterial.shader.reflection.uniforms[0].name.c_str(),
        bgfx::UniformType::Vec4);
    const std::array<float, 4U> redUniformValue{
        redBinding.binding.uniforms[0].value[0],
        redBinding.binding.uniforms[0].value[1],
        redBinding.binding.uniforms[0].value[2],
        redBinding.binding.uniforms[0].value[3],
    };
    const std::array<float, 4U> blueUniformValue{
        blueBinding.binding.uniforms[0].value[0],
        blueBinding.binding.uniforms[0].value[1],
        blueBinding.binding.uniforms[0].value[2],
        blueBinding.binding.uniforms[0].value[3],
    };
    const ForwardRenderProbe overrideRed = ForwardRenderHarness::ProbeAt(
        harness.RenderPixels(overrideProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.0F, 0.0F, 0.0F, UINT32_MAX, 0x000000ffU, 0U, BGFX_INVALID_HANDLE, 0.0F, 0.0F, 0.0F, 0.0F, BGFX_INVALID_HANDLE, tintUniform, &redUniformValue),
        32U,
        32U);
    const ForwardRenderProbe overrideBlue = ForwardRenderHarness::ProbeAt(
        harness.RenderPixels(overrideProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.0F, 0.0F, 0.0F, UINT32_MAX, 0x000000ffU, 0U, BGFX_INVALID_HANDLE, 0.0F, 0.0F, 0.0F, 0.0F, BGFX_INVALID_HANDLE, tintUniform, &blueUniformValue),
        32U,
        32U);
    Require(overrideRed.r > overrideBlue.r + 40U && overrideBlue.b > overrideRed.b + 40U,
        "KBMAT-MAT68: instance override uniform values must change the rendered pixel without a new shader");
    bgfx::destroy(tintUniform);

    const auto makeStaticVariantGraph = [](const char* selector) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.shadingModel = "unlit";
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::StaticBoolParameter, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = selector } });
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.95 0.05 0.05 1" } });
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.05 0.05 0.95 1" } });
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 5U, .kind = RenderMaterialGraphNodeKind::StaticSwitch });
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::StaticBoolParameter, 2U, "value", RenderMaterialGraphNodeKind::StaticSwitch, 5U, "value"));
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba", RenderMaterialGraphNodeKind::StaticSwitch, 5U, "true"));
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 4U, "rgba", RenderMaterialGraphNodeKind::StaticSwitch, 5U, "false"));
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::StaticSwitch, 5U, "result", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        return graph;
    };
    const AcceptanceCookedMaterial& staticTrue = cookAndTrack(makeStaticVariantGraph("true"), 0x6830U);
    const AcceptanceCookedMaterial& staticFalse = cookAndTrack(makeStaticVariantGraph("false"), 0x6831U);
    Require(staticTrue.key.variantKey != staticFalse.key.variantKey,
        "KBMAT-MAT68: static variant acceptance materials must have distinct variant keys");
    const ForwardRenderProbe staticTruePixel = harness.Render(registry.Acquire(staticTrue.key), BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    const ForwardRenderProbe staticFalsePixel = harness.Render(registry.Acquire(staticFalse.key), BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    Require(staticTruePixel.r > staticFalsePixel.r + 40U && staticFalsePixel.b > staticTruePixel.b + 40U,
        "KBMAT-MAT68: static variant selection must change the cooked and rendered material");

    RenderMaterialGraphDocument brokenGraph{};
    brokenGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor });
    const RenderMaterialGraphCompileResult failedCompile =
        CompileRenderMaterialGraphToShaderSource(brokenGraph, RenderMaterialGraphBuildContext{ .assetId = 0x68F0U });
    Require(!failedCompile.Succeeded() && !failedCompile.diagnostics.empty(),
        "KBMAT-MAT68: compile failure must surface diagnostics instead of falling back silently");

    RenderMaterialGraphDocument missingTextureGraph = MakeDefaultRenderMaterialGraphDocument();
    missingTextureGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::TextureSample,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "missingTex", .textureRole = "baseColor", .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb },
    });
    missingTextureGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::TextureSample, 2U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult missingCompiled =
        CompileRenderMaterialGraphToShaderSource(missingTextureGraph, RenderMaterialGraphBuildContext{ .assetId = 0x68F1U });
    Require(missingCompiled.Succeeded(), "KBMAT-MAT68: missing-texture acceptance graph must compile before binding");
    const RenderMaterialGraphProgramBindingResult missingBinding =
        BuildRenderMaterialGraphProgramBinding(0x68F1U, 1U, missingCompiled.shader, std::span<const RenderMaterialGraphParameterValue>{});
    Require(HasGraphDiagnostic(missingBinding.diagnostics, RenderMaterialGraphDiagnosticKind::MissingTexture) &&
            !missingBinding.binding.textures.empty() &&
            !missingBinding.binding.textures[0].resolved,
        "KBMAT-MAT68: missing texture must be reported as a binding diagnostic and unresolved texture");

    const std::vector<RenderMaterialGraphDiagnostic> unsupportedDiagnostics =
        ValidateRenderMaterialGraphDocument(red.graph, RenderMaterialGraphRenderPath::GpuDeferred);
    Require(HasGraphDiagnostic(unsupportedDiagnostics, RenderMaterialGraphDiagnosticKind::UnsupportedRenderPathNode),
        "KBMAT-MAT68: unsupported render path must report an explicit diagnostic");

    const MaterialProgramRegistryStats stats = registry.Stats();
    Require(stats.loads >= 5U && stats.failures == 0U && stats.liveProgramCount >= 5U,
        "KBMAT-MAT68: acceptance suite must load live graph programs through MaterialProgramRegistry without failures");

    bgfx::destroy(previewRedProgram);
    registry.Shutdown();
    harness.Shutdown();
}

void RunForwardGraphOrganizationNodesRenderTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat56_organization_nodes";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT56: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    const auto colorNode = [](std::uint32_t id, std::string_view hint) {
        RenderMaterialGraphNode node{ .id = id, .kind = RenderMaterialGraphNodeKind::ConstantColor, .positionX = 40, .positionY = 80 };
        node.parameter.defaultValueHint = std::string{ hint };
        return node;
    };
    const auto compileAndCook = [&](const RenderMaterialGraphDocument& graph, std::uint64_t assetId, const char* message) {
        const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = assetId });
        Require(compiled.Succeeded(), message);
        const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
        Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT56: Organization graph fragment shader must cook to a binary artifact");
        return result;
    };

    RenderMaterialGraphDocument rerouteGraph = MakeDefaultRenderMaterialGraphDocument();
    const RenderMaterialGraphNode red = colorNode(2U, "0.85 0.04 0.03 1");
    RenderMaterialGraphNode reroute{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::Reroute,
        .positionX = 220,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{ .displayName = "Reroute", .defaultValueHint = "color" },
    };
    rerouteGraph.nodes.push_back(red);
    rerouteGraph.nodes.push_back(reroute);
    rerouteGraph.links.push_back(MakeLink(red, "rgba", reroute, "input"));
    rerouteGraph.links.push_back(MakeLink(reroute, "output", rerouteGraph.nodes.front(), "baseColor"));
    const RenderMaterialGraphShaderArtifactResult rerouteResult =
        compileAndCook(rerouteGraph, 0x5604U, "KBMAT-MAT56: Reroute render graph must compile");

    RenderMaterialGraphDocument namedGraph = MakeDefaultRenderMaterialGraphDocument();
    const RenderMaterialGraphNode green = colorNode(2U, "0.03 0.82 0.04 1");
    RenderMaterialGraphNode declaration{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::NamedRerouteDeclaration,
        .positionX = 220,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "GpuTint", .displayName = "GPU Tint", .defaultValueHint = "color" },
    };
    RenderMaterialGraphNode usage{
        .id = 4U,
        .kind = RenderMaterialGraphNodeKind::NamedRerouteUsage,
        .positionX = 440,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "GpuTint", .displayName = "GPU Tint", .defaultValueHint = "color" },
    };
    namedGraph.nodes.push_back(green);
    namedGraph.nodes.push_back(declaration);
    namedGraph.nodes.push_back(usage);
    namedGraph.links.push_back(MakeLink(green, "rgba", declaration, "input"));
    namedGraph.links.push_back(MakeLink(usage, "output", namedGraph.nodes.front(), "baseColor"));
    const RenderMaterialGraphShaderArtifactResult namedResult =
        compileAndCook(namedGraph, 0x5605U, "KBMAT-MAT56: Named reroute render graph must compile");

    RenderMaterialGraphDocument compositeGraph = MakeDefaultRenderMaterialGraphDocument();
    const RenderMaterialGraphNode blue = colorNode(2U, "0.03 0.05 0.86 1");
    RenderMaterialGraphNode compositeInput{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::CompositeInput,
        .positionX = 220,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{ .displayName = "Composite In", .defaultValueHint = "color" },
    };
    RenderMaterialGraphNode compositeOutput{
        .id = 4U,
        .kind = RenderMaterialGraphNodeKind::CompositeOutput,
        .positionX = 440,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{ .displayName = "Composite Out", .defaultValueHint = "color" },
    };
    compositeGraph.nodes.push_back(blue);
    compositeGraph.nodes.push_back(compositeInput);
    compositeGraph.nodes.push_back(compositeOutput);
    compositeGraph.composites.push_back(RenderMaterialGraphCompositeSubgraph{
        .id = 1U,
        .positionX = 180,
        .positionY = 40,
        .width = 360,
        .height = 180,
        .color = 0x425B4AU,
        .collapsed = true,
        .name = "GPU Composite",
        .nodeIds = { compositeInput.id, compositeOutput.id },
    });
    compositeGraph.links.push_back(MakeLink(blue, "rgba", compositeInput, "input"));
    compositeGraph.links.push_back(MakeLink(compositeInput, "output", compositeOutput, "input"));
    compositeGraph.links.push_back(MakeLink(compositeOutput, "output", compositeGraph.nodes.front(), "baseColor"));
    const RenderMaterialGraphShaderArtifactResult compositeResult =
        compileAndCook(compositeGraph, 0x5606U, "KBMAT-MAT56: Composite render graph must compile");

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT56: Direct3D11 device unavailable; cannot run GPU organization-node proof\n");
        Require(false, "KBMAT-MAT56: A real GPU device is required to prove organization-node rendering");
        return;
    }

    const bgfx::ProgramHandle rerouteProgram = BuildGraphProgram(vsBytes, *rerouteResult.artifact);
    const bgfx::ProgramHandle namedProgram = BuildGraphProgram(vsBytes, *namedResult.artifact);
    const bgfx::ProgramHandle compositeProgram = BuildGraphProgram(vsBytes, *compositeResult.artifact);
    Require(bgfx::isValid(rerouteProgram) && bgfx::isValid(namedProgram) && bgfx::isValid(compositeProgram),
        "KBMAT-MAT56: Organization-node graph programs must link on the real GPU backend");

    const ForwardRenderProbe rerouted = harness.Render(rerouteProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    const ForwardRenderProbe named = harness.Render(namedProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    const ForwardRenderProbe composite = harness.Render(compositeProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    Require(rerouted.r > rerouted.g && rerouted.r > rerouted.b && rerouted.r > 80U,
        "KBMAT-MAT56: Reroute graph must render its routed red color on the GPU");
    Require(named.g > named.r && named.g > named.b && named.g > 70U,
        "KBMAT-MAT56: Named reroute graph must render its declaration color on the GPU");
    Require(composite.b > composite.r && composite.b > composite.g && composite.b > 80U,
        "KBMAT-MAT56: Composite tunnel graph must render its inlined blue color on the GPU");

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

    RenderMaterialGraphDocument deltaGraph = MakeDefaultRenderMaterialGraphDocument();
    RenderMaterialGraphNode deltaBlackBase{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
    deltaBlackBase.parameter.defaultValueHint = "0 0 0 1";
    deltaGraph.nodes.push_back(deltaBlackBase);
    deltaGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::DeltaTime });
    deltaGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::MakeVector });
    deltaGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    deltaGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::DeltaTime, 3U, "value", RenderMaterialGraphNodeKind::MakeVector, 4U, "x"));
    deltaGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::MakeVector, 4U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"));
    const RenderMaterialGraphCompileResult deltaCompiled = CompileRenderMaterialGraphToShaderSource(deltaGraph, RenderMaterialGraphBuildContext{ .assetId = 0x3000U });
    Require(deltaCompiled.Succeeded(), "KBMAT-MAT30: DeltaTime-driven emissive graph must compile");
    Require(deltaCompiled.shader.source.find("ctx.deltaTime") != std::string::npos, "KBMAT-MAT30: DeltaTime node must emit ctx.deltaTime in the generated source");
    const RenderMaterialGraphShaderArtifactResult deltaResult = CookRenderMaterialGraphShaderArtifact(deltaCompiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(deltaResult.Succeeded() && deltaResult.artifact.has_value(), "KBMAT-MAT30: DeltaTime graph forward fragment shader must cook");
    const bgfx::ProgramHandle deltaProgram = BuildGraphProgram(vsBytes, *deltaResult.artifact);
    Require(bgfx::isValid(deltaProgram), "KBMAT-MAT30: DeltaTime graph program must link on the real GPU backend");
    const ForwardRenderProbe smallDelta = ForwardRenderHarness::ProbeAt(harness.RenderPixels(deltaProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.0F, 0.05F), 32U, 32U);
    const ForwardRenderProbe largeDelta = ForwardRenderHarness::ProbeAt(harness.RenderPixels(deltaProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.0F, 0.75F), 32U, 32U);
    Require(largeDelta.r > smallDelta.r + 120U,
        "KBMAT-MAT30: DeltaTime node must read u_time.y and visibly change the rendered pixel between frames");

    RenderMaterialGraphDocument dynamicGraph = MakeDefaultRenderMaterialGraphDocument();
    RenderMaterialGraphNode dynamicBlackBase{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
    dynamicBlackBase.parameter.defaultValueHint = "0 0 0 1";
    dynamicGraph.nodes.push_back(dynamicBlackBase);
    dynamicGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::DynamicParameter });
    dynamicGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    dynamicGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::DynamicParameter, 3U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"));
    const RenderMaterialGraphCompileResult dynamicCompiled = CompileRenderMaterialGraphToShaderSource(dynamicGraph, RenderMaterialGraphBuildContext{ .assetId = 0x3001U });
    Require(dynamicCompiled.Succeeded(), "KBMAT-MAT30: DynamicParameter-driven emissive graph must compile");
    Require(dynamicCompiled.shader.source.find("ctx.dynamicParameter") != std::string::npos, "KBMAT-MAT30: DynamicParameter node must emit ctx.dynamicParameter in the generated source");
    const RenderMaterialGraphShaderArtifactResult dynamicResult = CookRenderMaterialGraphShaderArtifact(dynamicCompiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(dynamicResult.Succeeded() && dynamicResult.artifact.has_value(), "KBMAT-MAT30: DynamicParameter graph forward fragment shader must cook");
    const bgfx::ProgramHandle dynamicProgram = BuildGraphProgram(vsBytes, *dynamicResult.artifact);
    Require(bgfx::isValid(dynamicProgram), "KBMAT-MAT30: DynamicParameter graph program must link on the real GPU backend");
    const ForwardRenderProbe dynamicPixel = ForwardRenderHarness::ProbeAt(
        harness.RenderPixels(dynamicProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.0F, 0.0F, 0.0F, UINT32_MAX, 0x000000ffU, 0U, BGFX_INVALID_HANDLE, 0.12F, 0.56F, 0.88F, 1.0F),
        32U,
        32U);
    Require(dynamicPixel.b > dynamicPixel.g && dynamicPixel.g > dynamicPixel.r + 50U,
        "KBMAT-MAT30: DynamicParameter rgba must reach the real GPU shader through u_dynamicParameter");

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

// MAT-77/MAT-47: per-instance scalars. PerInstanceRandom/ObjectRadius/FadeAmount/CustomData0 ride the
// affine model's .w lanes and are carried through the vertex shader to the graph context.
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
    const bgfx::ProgramHandle fadeProgram = buildScalarProgram(RenderMaterialGraphNodeKind::PerInstanceFadeAmount, "fade", "ctx.perInstanceFadeAmount");
    const bgfx::ProgramHandle customProgram = buildScalarProgram(RenderMaterialGraphNodeKind::PerInstanceCustomData, "custom", "ctx.perInstanceCustomData");

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

    const ForwardRenderProbe lowFade = ForwardRenderHarness::ProbeAt(harness.RenderPixels(fadeProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.0F, 0.0F, 0.25F), 32U, 32U);
    const ForwardRenderProbe highFade = ForwardRenderHarness::ProbeAt(harness.RenderPixels(fadeProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.0F, 0.0F, 0.85F), 32U, 32U);
    Require(highFade.r > lowFade.r + 40U,
        "KBMAT-MAT47: PerInstanceFadeAmount must vary the output with the per-instance fade lane");

    const ForwardRenderProbe lowCustom = ForwardRenderHarness::ProbeAt(harness.RenderPixels(customProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.0F, 0.0F, 0.15F), 32U, 32U);
    const ForwardRenderProbe highCustom = ForwardRenderHarness::ProbeAt(harness.RenderPixels(customProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.0F, 0.0F, 0.75F), 32U, 32U);
    Require(highCustom.r > lowCustom.r + 40U,
        "KBMAT-MAT47: PerInstanceCustomData must vary the output with the per-instance custom data lane");

    bgfx::destroy(customProgram);
    bgfx::destroy(fadeProgram);
    bgfx::destroy(radiusProgram);
    bgfx::destroy(randomProgram);
    harness.Shutdown();
}

void RunForwardGraphPreSkinnedVertexDataTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat47_preskinned";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT47: Harness vertex shader must cook for pre-skinned proof");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT47: Direct3D11 device unavailable; cannot run GPU pre-skinned proof\n");
        Require(false, "KBMAT-MAT47: A real GPU device is required to prove pre-skinned vertex data");
        return;
    }

    const auto buildVec3Program = [&](RenderMaterialGraphNodeKind kind, const std::string& subdir, const char* expectExpr) -> bgfx::ProgramHandle {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.shadingModel = "unlit";
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = kind, .positionX = 40, .positionY = 40 });
        graph.links.push_back(MakeLink(kind, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x4700U });
        Require(compiled.Succeeded(), "KBMAT-MAT47: pre-skinned graph must compile");
        Require(compiled.shader.source.find(expectExpr) != std::string::npos, "KBMAT-MAT47: pre-skinned node must emit its context expression");
        const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest((cacheDir / subdir).generic_string()));
        Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT47: pre-skinned graph must cook");
        const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
        Require(bgfx::isValid(program), "KBMAT-MAT47: pre-skinned graph program must link");
        return program;
    };

    const bgfx::ProgramHandle positionProgram = buildVec3Program(RenderMaterialGraphNodeKind::PreSkinnedPosition, "position", "ctx.preSkinnedPosition");
    const bgfx::ProgramHandle normalProgram = buildVec3Program(RenderMaterialGraphNodeKind::PreSkinnedNormal, "normal", "ctx.preSkinnedNormal");

    const ForwardRenderProbe positionPixel = ForwardRenderHarness::ProbeAt(harness.RenderPixels(positionProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE), 32U, 32U);
    Require(positionPixel.r > 120U,
        "KBMAT-MAT47: PreSkinnedPosition must carry the vertex local position into the fragment graph");

    const ForwardRenderProbe normalPixel = ForwardRenderHarness::ProbeAt(harness.RenderPixels(normalProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE), 32U, 32U);
    Require(normalPixel.g > normalPixel.r + 80U && normalPixel.g > normalPixel.b + 70U,
        "KBMAT-MAT47: PreSkinnedNormal must carry the vertex normal into the fragment graph");

    bgfx::destroy(normalProgram);
    bgfx::destroy(positionProgram);
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

void RunForwardGraphCustomizedUv0Test() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat67_customized_uv0";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.shadingModel = "unlit";
    RenderMaterialGraphNode uv{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantVector2, .positionX = 40, .positionY = 40 };
    uv.parameter.defaultValueHint = "0.1 0.5";
    RenderMaterialGraphNode sample{ .id = 3U, .kind = RenderMaterialGraphNodeKind::TextureSample, .positionX = 240, .positionY = 80 };
    sample.parameter.stableId = "customUvGradient";
    sample.parameter.textureRole = "baseColor";
    sample.parameter.expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb;
    graph.nodes.push_back(uv);
    graph.nodes.push_back(sample);
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantVector2, 2U, "xy", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "customizedUv0"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::TextureSample, 3U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x6700U });
    Require(compiled.Succeeded(), "KBMAT-MAT67: CustomizedUV0 graph must compile");
    Require(compiled.shader.reflection.hasCustomizedUv0, "KBMAT-MAT67: reflection must flag CustomizedUV0");
    Require(compiled.shader.source.find("EvaluateCustomizedUv0") != std::string::npos,
        "KBMAT-MAT67: source must emit the CustomizedUV0 vertex evaluation function");
    const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT67: CustomizedUV0 graph must cook");
    Require(result.artifact->hasVertexShader, "KBMAT-MAT67: CustomizedUV0 must cook a generated vertex shader");
    const RenderMaterialGraphShaderBinary* cookedVs = result.artifact->FindVertexBinary(RenderMaterialGraphShaderBackend::Dxbc);
    Require(cookedVs != nullptr && cookedVs->byteSize > 0U && std::filesystem::exists(std::filesystem::path{ cookedVs->binaryPath }),
        "KBMAT-MAT67: CustomizedUV0 generated vs.bin must exist on disk");

    const std::filesystem::path fixedVsBin = cacheDir / "vs_fixed.bin";
    const std::filesystem::path customUvVsBin = cacheDir / "vs_custom_uv0.bin";
    Require(CookHarnessVertexShader(fixedVsBin), "KBMAT-MAT67: fixed harness vertex shader must cook");
    Require(CookHarnessVertexDomainOutputShader(customUvVsBin, compiled.shader.source, "a_position", "EvaluateCustomizedUv0(ctx)"),
        "KBMAT-MAT67: CustomizedUV0 harness vertex shader must cook");
    const std::vector<std::uint8_t> fixedVsBytes = ReadAllBytes(fixedVsBin);
    const std::vector<std::uint8_t> customUvVsBytes = ReadAllBytes(customUvVsBin);

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT67: Direct3D11 device unavailable; cannot run GPU CustomizedUV0 proof\n");
        Require(false, "KBMAT-MAT67: A real GPU device is required to prove CustomizedUV0");
        return;
    }

    const bgfx::ProgramHandle fixedProgram = BuildGraphProgram(fixedVsBytes, *result.artifact);
    const bgfx::ProgramHandle customUvProgram = BuildGraphProgram(customUvVsBytes, *result.artifact);
    Require(bgfx::isValid(fixedProgram) && bgfx::isValid(customUvProgram), "KBMAT-MAT67: CustomizedUV0 programs must link");

    const std::array<std::uint32_t, 2U> gradient{ 0xff000000U, 0xff0000ffU };
    bgfx::UniformHandle sampler = bgfx::createUniform(compiled.shader.reflection.textures[0].samplerName.c_str(), bgfx::UniformType::Sampler);
    bgfx::TextureHandle texture = bgfx::createTexture2D(2U, 1U, false, 1U, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy(gradient.data(), sizeof(gradient)));

    const ForwardRenderProbe fixedPixel = harness.Render(fixedProgram, sampler, texture);
    const ForwardRenderProbe customUvPixel = harness.Render(customUvProgram, sampler, texture);
    Require(fixedPixel.r > customUvPixel.r + 40U,
        "KBMAT-MAT67: CustomizedUV0 must change per-vertex UV sampling before the fragment texture read");

    bgfx::destroy(texture);
    bgfx::destroy(sampler);
    harness.Shutdown();
}

void RunForwardGraphDisplacementVertexOutputTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat67_displacement";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    RenderMaterialGraphNode displacement{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantVector, .positionX = 40, .positionY = 40 };
    displacement.parameter.defaultValueHint = "0.6 0 0";
    graph.nodes.push_back(displacement);
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantVector, 2U, "xyz", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "displacement"));

    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x6701U });
    Require(compiled.Succeeded(), "KBMAT-MAT67: Displacement graph must compile");
    Require(compiled.shader.reflection.hasDisplacement, "KBMAT-MAT67: reflection must flag Displacement");
    Require(compiled.shader.source.find("EvaluateDisplacement") != std::string::npos,
        "KBMAT-MAT67: source must emit the Displacement vertex evaluation function");
    const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT67: Displacement graph must cook");
    Require(result.artifact->hasVertexShader, "KBMAT-MAT67: Displacement must cook a generated vertex shader");
    const RenderMaterialGraphShaderBinary* cookedVs = result.artifact->FindVertexBinary(RenderMaterialGraphShaderBackend::Dxbc);
    Require(cookedVs != nullptr && cookedVs->byteSize > 0U && std::filesystem::exists(std::filesystem::path{ cookedVs->binaryPath }),
        "KBMAT-MAT67: Displacement generated vs.bin must exist on disk");

    const std::filesystem::path fixedVsBin = cacheDir / "vs_fixed.bin";
    const std::filesystem::path displacedVsBin = cacheDir / "vs_displacement.bin";
    Require(CookHarnessVertexShader(fixedVsBin), "KBMAT-MAT67: fixed harness vertex shader must cook");
    Require(CookHarnessVertexDomainOutputShader(displacedVsBin, compiled.shader.source, "a_position + EvaluateDisplacement(ctx)", "a_texcoord0"),
        "KBMAT-MAT67: Displacement harness vertex shader must cook");
    const std::vector<std::uint8_t> fixedVsBytes = ReadAllBytes(fixedVsBin);
    const std::vector<std::uint8_t> displacedVsBytes = ReadAllBytes(displacedVsBin);

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT67: Direct3D11 device unavailable; cannot run GPU Displacement proof\n");
        Require(false, "KBMAT-MAT67: A real GPU device is required to prove Displacement");
        return;
    }

    const bgfx::ProgramHandle fixedProgram = BuildGraphProgram(fixedVsBytes, *result.artifact);
    const bgfx::ProgramHandle displacedProgram = BuildGraphProgram(displacedVsBytes, *result.artifact);
    Require(bgfx::isValid(fixedProgram) && bgfx::isValid(displacedProgram), "KBMAT-MAT67: Displacement programs must link");

    const ForwardRenderProbe fixedPixel = ForwardRenderHarness::ProbeAt(harness.RenderPixels(fixedProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE), 8U, 32U);
    const ForwardRenderProbe displacedPixel = ForwardRenderHarness::ProbeAt(harness.RenderPixels(displacedProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE), 8U, 32U);
    Require(fixedPixel.r > 100U, "KBMAT-MAT67: without displacement the left-edge pixel must be covered by the material");
    Require(fixedPixel.r > displacedPixel.r + 60U,
        "KBMAT-MAT67: Displacement must move geometry in the vertex stage");

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

void RunForwardGraphSceneDepthRendersTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat80_scenedepth_render";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT18B: scene-depth render harness vertex shader must cook");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    RenderMaterialGraphDocument sceneDepthGraph = MakeDefaultRenderMaterialGraphDocument();
    sceneDepthGraph.shadingModel = "unlit";
    sceneDepthGraph.blendMode = "translucent";
    sceneDepthGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::SceneDepth });
    RenderMaterialGraphNode one{ .id = 3U, .kind = RenderMaterialGraphNodeKind::ConstantScalar };
    one.parameter.defaultValueHint = "1";
    sceneDepthGraph.nodes.push_back(one);
    sceneDepthGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::MakeVector });
    sceneDepthGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::SceneDepth, 2U, "value", RenderMaterialGraphNodeKind::MakeVector, 4U, "x"));
    sceneDepthGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::SceneDepth, 2U, "value", RenderMaterialGraphNodeKind::MakeVector, 4U, "y"));
    sceneDepthGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::SceneDepth, 2U, "value", RenderMaterialGraphNodeKind::MakeVector, 4U, "z"));
    sceneDepthGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantScalar, 3U, "value", RenderMaterialGraphNodeKind::MakeVector, 4U, "w"));
    sceneDepthGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::MakeVector, 4U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    const RenderMaterialGraphCompileResult sceneDepthCompiled = CompileRenderMaterialGraphToShaderSource(sceneDepthGraph, RenderMaterialGraphBuildContext{ .assetId = 0x8B01U });
    Require(sceneDepthCompiled.Succeeded(), "KBMAT-MAT18B: scene-depth render graph must compile");
    Require(sceneDepthCompiled.shader.reflection.usesSceneDepth, "KBMAT-MAT18B: scene-depth render graph must request scene depth binding");
    RenderMaterialGraphShaderArtifactRequest request = CookRequest(cacheDir.generic_string());
    request.pass = "BaseTransparent";
    const RenderMaterialGraphShaderArtifactResult sceneDepthResult = CookRenderMaterialGraphShaderArtifact(sceneDepthCompiled.shader, backends, request);
    Require(sceneDepthResult.Succeeded() && sceneDepthResult.artifact.has_value(), "KBMAT-MAT18B: scene-depth render graph must cook");

    RenderMaterialGraphDocument depthFadeGraph = MakeDefaultRenderMaterialGraphDocument();
    depthFadeGraph.shadingModel = "unlit";
    depthFadeGraph.blendMode = "translucent";
    RenderMaterialGraphNode white{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
    white.parameter.defaultValueHint = "1 1 1 1";
    RenderMaterialGraphNode fadeDistance{ .id = 3U, .kind = RenderMaterialGraphNodeKind::ConstantScalar };
    fadeDistance.parameter.defaultValueHint = "0.5";
    depthFadeGraph.nodes.push_back(white);
    depthFadeGraph.nodes.push_back(fadeDistance);
    depthFadeGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::DepthFade });
    depthFadeGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    depthFadeGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantScalar, 3U, "value", RenderMaterialGraphNodeKind::DepthFade, 4U, "fadeDistance"));
    depthFadeGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::DepthFade, 4U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"));

    const RenderMaterialGraphCompileResult depthFadeCompiled = CompileRenderMaterialGraphToShaderSource(depthFadeGraph, RenderMaterialGraphBuildContext{ .assetId = 0x8B02U });
    Require(depthFadeCompiled.Succeeded(), "KBMAT-MAT18B: depth-fade render graph must compile");
    Require(depthFadeCompiled.shader.reflection.usesSceneDepth, "KBMAT-MAT18B: depth-fade render graph must request scene depth binding");
    const RenderMaterialGraphShaderArtifactResult depthFadeResult = CookRenderMaterialGraphShaderArtifact(depthFadeCompiled.shader, backends, request);
    Require(depthFadeResult.Succeeded() && depthFadeResult.artifact.has_value(), "KBMAT-MAT18B: depth-fade render graph must cook");

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT18B: Direct3D11 device unavailable; cannot run GPU scene-depth proof\n");
        Require(false, "KBMAT-MAT18B: A real GPU device is required to prove scene-depth sampling");
        return;
    }

    const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *sceneDepthResult.artifact);
    Require(bgfx::isValid(program), "KBMAT-MAT18B: scene-depth graph program must link");
    const bgfx::ProgramHandle fadeProgram = BuildGraphProgram(vsBytes, *depthFadeResult.artifact);
    Require(bgfx::isValid(fadeProgram), "KBMAT-MAT18B: depth-fade graph program must link");

    const std::array<std::uint32_t, 2U> depthGradient{ 0xff000000U, 0xff0000ffU };
    bgfx::TextureHandle sceneDepthTexture = bgfx::createTexture2D(2U, 1U, false, 1U, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy(depthGradient.data(), sizeof(depthGradient)));
    const std::vector<std::uint8_t> pixels = harness.RenderPixels(
        program,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        0.0F,
        0.0F,
        0.0F,
        UINT32_MAX,
        0x000000ffU,
        0U,
        sceneDepthTexture);
    const ForwardRenderProbe left = ForwardRenderHarness::ProbeAt(pixels, 12U, 32U);
    const ForwardRenderProbe right = ForwardRenderHarness::ProbeAt(pixels, 52U, 32U);

    Require(right.r > left.r + 40U,
        "KBMAT-MAT18B: SceneDepth must sample the bound scene-depth texture (right gradient texel brighter than left)");

    const std::array<std::uint32_t, 4U> depthFadeRamp{ 0xff000000U, 0xff000055U, 0xff0000aaU, 0xff0000ffU };
    bgfx::TextureHandle depthFadeTexture = bgfx::createTexture2D(4U, 1U, false, 1U, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy(depthFadeRamp.data(), sizeof(depthFadeRamp)));
    const std::vector<std::uint8_t> fadePixels = harness.RenderPixels(
        fadeProgram,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        0.0F,
        0.0F,
        0.0F,
        UINT32_MAX,
        0x000000ffU,
        0U,
        depthFadeTexture);
    const std::array<ForwardRenderProbe, 4U> fadeProbes{
        ForwardRenderHarness::ProbeAt(fadePixels, 8U, 32U),
        ForwardRenderHarness::ProbeAt(fadePixels, 24U, 32U),
        ForwardRenderHarness::ProbeAt(fadePixels, 40U, 32U),
        ForwardRenderHarness::ProbeAt(fadePixels, 56U, 32U),
    };
    std::uint8_t minFadeAlpha = 255U;
    std::uint8_t maxFadeAlpha = 0U;
    for (const ForwardRenderProbe& probe : fadeProbes) {
        if (probe.a < minFadeAlpha) {
            minFadeAlpha = probe.a;
        }
        if (probe.a > maxFadeAlpha) {
            maxFadeAlpha = probe.a;
        }
    }
    Require(minFadeAlpha < 110U && maxFadeAlpha > 180U && maxFadeAlpha > minFadeAlpha + 90U,
        "KBMAT-MAT18B: DepthFade alpha must fade near matching scene depth and stay visible away from the intersection");

    bgfx::destroy(depthFadeTexture);
    bgfx::destroy(fadeProgram);
    bgfx::destroy(sceneDepthTexture);
    bgfx::destroy(program);
    harness.Shutdown();
}

void RunForwardGraphTextureExpansionRendersTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat31_texture_expansion";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT31: texture expansion render harness vertex shader must cook");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    const auto cookGraph = [&](const RenderMaterialGraphDocument& graph, std::uint32_t assetId, std::string_view pass = "BaseOpaque") {
        const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = assetId });
        Require(compiled.Succeeded(), "KBMAT-MAT31: texture expansion graph must compile");
        RenderMaterialGraphShaderArtifactRequest request = CookRequest(cacheDir.generic_string());
        request.pass = std::string{ pass };
        const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, request);
        Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT31: texture expansion graph must cook to a real shader binary");
        return std::pair<RenderMaterialGraphShaderSource, RenderMaterialGraphShaderArtifact>{ compiled.shader, *result.artifact };
    };

    RenderMaterialGraphDocument rgbaGraph = MakeDefaultRenderMaterialGraphDocument();
    rgbaGraph.shadingModel = "unlit";
    rgbaGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::TextureSample, .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "rgbaTex", .textureRole = "baseColor", .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb } });
    rgbaGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::MakeVector });
    rgbaGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::TextureSample, 2U, "r", RenderMaterialGraphNodeKind::MakeVector, 3U, "x"));
    rgbaGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::TextureSample, 2U, "g", RenderMaterialGraphNodeKind::MakeVector, 3U, "y"));
    rgbaGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::TextureSample, 2U, "b", RenderMaterialGraphNodeKind::MakeVector, 3U, "z"));
    rgbaGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::TextureSample, 2U, "a", RenderMaterialGraphNodeKind::MakeVector, 3U, "w"));
    rgbaGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::MakeVector, 3U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const auto [rgbaShader, rgbaArtifact] = cookGraph(rgbaGraph, 0x3100U);

    RenderMaterialGraphDocument objectGraph = MakeDefaultRenderMaterialGraphDocument();
    objectGraph.shadingModel = "unlit";
    objectGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::TextureObject, .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "objectTex", .textureRole = "baseColor", .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb } });
    objectGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::TextureSample });
    objectGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::TextureObject, 2U, "texture", RenderMaterialGraphNodeKind::TextureSample, 3U, "texture"));
    objectGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::TextureSample, 3U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const auto [objectShader, objectArtifact] = cookGraph(objectGraph, 0x3101U);
    Require(objectShader.reflection.textures.size() == 1U && objectShader.reflection.textures[0].stableId == "objectTex",
        "KBMAT-MAT31: TextureObject connected to TextureSample must declare the object sampler in reflection");

    const auto makeSingleSampleGraph = [](RenderMaterialGraphNodeKind kind, std::string stableId) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.shadingModel = "unlit";
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = kind, .parameter = RenderMaterialGraphParameterMetadata{ .stableId = std::move(stableId), .textureRole = "baseColor", .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb } });
        graph.links.push_back(MakeLink(kind, 2U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        return graph;
    };
    const auto [cubeShader, cubeArtifact] = cookGraph(makeSingleSampleGraph(RenderMaterialGraphNodeKind::TextureSampleCube, "cubeTex"), 0x3102U);
    const auto [volumeShader, volumeArtifact] = cookGraph(makeSingleSampleGraph(RenderMaterialGraphNodeKind::TextureSampleVolume, "volumeTex"), 0x3103U);

    RenderMaterialGraphDocument arrayGraph = makeSingleSampleGraph(RenderMaterialGraphNodeKind::TextureSample2DArray, "arrayTex");
    RenderMaterialGraphNode arrayLayer{ .id = 3U, .kind = RenderMaterialGraphNodeKind::ConstantScalar };
    arrayLayer.parameter.defaultValueHint = "1";
    arrayGraph.nodes.push_back(arrayLayer);
    arrayGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantScalar, 3U, "value", RenderMaterialGraphNodeKind::TextureSample2DArray, 2U, "layer"));
    const auto [arrayShader, arrayArtifact] = cookGraph(arrayGraph, 0x3104U);
    Require(cubeShader.reflection.textures[0].dimension == RenderMaterialGraphTextureDimension::TextureCube &&
            volumeShader.reflection.textures[0].dimension == RenderMaterialGraphTextureDimension::Texture3D &&
            arrayShader.reflection.textures[0].dimension == RenderMaterialGraphTextureDimension::Texture2DArray,
        "KBMAT-MAT31: reflection must carry sampler dimensions for cube, volume and 2D array textures");

    RenderMaterialGraphDocument sceneColorGraph = MakeDefaultRenderMaterialGraphDocument();
    sceneColorGraph.shadingModel = "unlit";
    sceneColorGraph.blendMode = "translucent";
    sceneColorGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::SceneColor });
    sceneColorGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::SceneColor, 2U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const auto [sceneColorShader, sceneColorArtifact] = cookGraph(sceneColorGraph, 0x3105U, "BaseTransparent");
    Require(sceneColorShader.reflection.usesSceneColor && sceneColorShader.source.find("SAMPLER2D(s_kbSceneColor, 4)") != std::string::npos,
        "KBMAT-MAT31: SceneColor must request the reserved scene color sampler");

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT31: Direct3D11 device unavailable; cannot run GPU texture expansion proof\n");
        Require(false, "KBMAT-MAT31: A real GPU device is required to prove texture expansion rendering");
        return;
    }

    const bgfx::ProgramHandle rgbaProgram = BuildGraphProgram(vsBytes, rgbaArtifact);
    const bgfx::ProgramHandle objectProgram = BuildGraphProgram(vsBytes, objectArtifact);
    const bgfx::ProgramHandle cubeProgram = BuildGraphProgram(vsBytes, cubeArtifact);
    const bgfx::ProgramHandle volumeProgram = BuildGraphProgram(vsBytes, volumeArtifact);
    const bgfx::ProgramHandle arrayProgram = BuildGraphProgram(vsBytes, arrayArtifact);
    const bgfx::ProgramHandle sceneColorProgram = BuildGraphProgram(vsBytes, sceneColorArtifact);
    Require(bgfx::isValid(rgbaProgram) && bgfx::isValid(objectProgram) && bgfx::isValid(cubeProgram) &&
            bgfx::isValid(volumeProgram) && bgfx::isValid(arrayProgram) && bgfx::isValid(sceneColorProgram),
        "KBMAT-MAT31: texture expansion graph programs must link on the real GPU backend");

    const std::uint32_t rgbaTexel = 0xe0408020U;
    bgfx::UniformHandle rgbaSampler = bgfx::createUniform(rgbaShader.reflection.textures[0].samplerName.c_str(), bgfx::UniformType::Sampler);
    bgfx::TextureHandle rgbaTexture = bgfx::createTexture2D(1U, 1U, false, 1U, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy(&rgbaTexel, sizeof(rgbaTexel)));
    const ForwardRenderProbe rgba = harness.Render(rgbaProgram, rgbaSampler, rgbaTexture);
    Require(rgba.g > rgba.b && rgba.b > rgba.r && rgba.a > rgba.g,
        "KBMAT-MAT31: TextureSample r/g/b/a outputs must render distinguishable GPU channels");

    const std::uint32_t midGray = 0xff808080U;
    bgfx::TextureHandle linearTexture = bgfx::createTexture2D(1U, 1U, false, 1U, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy(&midGray, sizeof(midGray)));
    bgfx::TextureHandle srgbTexture = bgfx::createTexture2D(1U, 1U, false, 1U, bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_SRGB | BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy(&midGray, sizeof(midGray)));
    const ForwardRenderProbe linearSample = harness.Render(rgbaProgram, rgbaSampler, linearTexture);
    const ForwardRenderProbe srgbSample = harness.Render(rgbaProgram, rgbaSampler, srgbTexture);
    Require(linearSample.r > srgbSample.r + 35U,
        "KBMAT-MAT31: sRGB texture binding must be sampled through hardware sRGB->linear conversion");

    const std::uint32_t objectTexel = 0xffe03020U;
    bgfx::UniformHandle objectSampler = bgfx::createUniform(objectShader.reflection.textures[0].samplerName.c_str(), bgfx::UniformType::Sampler);
    bgfx::TextureHandle objectTexture = bgfx::createTexture2D(1U, 1U, false, 1U, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy(&objectTexel, sizeof(objectTexel)));
    const ForwardRenderProbe objectSample = harness.Render(objectProgram, objectSampler, objectTexture);
    Require(objectSample.b > objectSample.r && objectSample.b > objectSample.g,
        "KBMAT-MAT31: TextureObject -> TextureSample must bind and sample the object texture on the GPU");

    const std::array<std::uint32_t, 6U> cubeTexels{ 0xffd0d020U, 0xffd0d020U, 0xffd0d020U, 0xffd0d020U, 0xffd0d020U, 0xffd0d020U };
    bgfx::UniformHandle cubeSampler = bgfx::createUniform(cubeShader.reflection.textures[0].samplerName.c_str(), bgfx::UniformType::Sampler);
    bgfx::TextureHandle cubeTexture = bgfx::createTextureCube(1U, false, 1U, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP, bgfx::copy(cubeTexels.data(), sizeof(cubeTexels)));
    const ForwardRenderProbe cubeSample = harness.Render(cubeProgram, cubeSampler, cubeTexture);
    Require(cubeSample.g > cubeSample.r && cubeSample.b > cubeSample.r,
        "KBMAT-MAT31: TextureSampleCube must render from a real GPU cube texture");

    const bgfx::Caps* caps = bgfx::getCaps();
    Require(caps != nullptr && (caps->formats[bgfx::TextureFormat::RGBA8] & BGFX_CAPS_FORMAT_TEXTURE_3D) != 0U,
        "KBMAT-MAT31: active GPU backend must support RGBA8 3D textures for the volume texture render proof");
    const std::array<std::uint32_t, 8U> volumeTexels{
        0xff30d020U, 0xff30d020U, 0xff30d020U, 0xff30d020U,
        0xff30d020U, 0xff30d020U, 0xff30d020U, 0xff30d020U,
    };
    bgfx::UniformHandle volumeSampler = bgfx::createUniform(volumeShader.reflection.textures[0].samplerName.c_str(), bgfx::UniformType::Sampler);
    bgfx::TextureHandle volumeTexture = bgfx::createTexture3D(2U, 2U, 2U, false, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP, bgfx::copy(volumeTexels.data(), sizeof(volumeTexels)));
    const ForwardRenderProbe volumeSample = harness.Render(volumeProgram, volumeSampler, volumeTexture);
    Require(volumeSample.g > volumeSample.r && volumeSample.g > volumeSample.b,
        "KBMAT-MAT31: TextureSampleVolume must render from a real GPU 3D texture");

    const std::array<std::uint32_t, 2U> arrayTexels{ 0xffd02020U, 0xff2020d0U };
    bgfx::UniformHandle arraySampler = bgfx::createUniform(arrayShader.reflection.textures[0].samplerName.c_str(), bgfx::UniformType::Sampler);
    bgfx::TextureHandle arrayTexture = bgfx::createTexture2D(1U, 1U, false, 2U, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy(arrayTexels.data(), sizeof(arrayTexels)));
    const ForwardRenderProbe arraySample = harness.Render(arrayProgram, arraySampler, arrayTexture);
    Require(arraySample.r > arraySample.g && arraySample.r > arraySample.b,
        "KBMAT-MAT31: TextureSample2DArray must render the selected array layer on the GPU");

    const std::array<std::uint32_t, 2U> sceneColorTexels{ 0xff2020e0U, 0xffe02020U };
    bgfx::TextureHandle sceneColorTexture = bgfx::createTexture2D(2U, 1U, false, 1U, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy(sceneColorTexels.data(), sizeof(sceneColorTexels)));
    const std::vector<std::uint8_t> scenePixels = harness.RenderPixels(
        sceneColorProgram,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        0.0F,
        0.0F,
        0.0F,
        UINT32_MAX,
        0x000000ffU,
        0U,
        BGFX_INVALID_HANDLE,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        sceneColorTexture);
    const ForwardRenderProbe sceneLeft = ForwardRenderHarness::ProbeAt(scenePixels, 12U, 32U);
    const ForwardRenderProbe sceneRight = ForwardRenderHarness::ProbeAt(scenePixels, 52U, 32U);
    Require(sceneLeft.r > sceneLeft.b && sceneRight.b > sceneRight.r,
        "KBMAT-MAT31: SceneColor must sample the bound opaque scene-color texture across the screen");

    bgfx::destroy(sceneColorTexture);
    bgfx::destroy(arrayTexture);
    bgfx::destroy(arraySampler);
    bgfx::destroy(volumeTexture);
    bgfx::destroy(volumeSampler);
    bgfx::destroy(cubeTexture);
    bgfx::destroy(cubeSampler);
    bgfx::destroy(objectTexture);
    bgfx::destroy(objectSampler);
    bgfx::destroy(srgbTexture);
    bgfx::destroy(linearTexture);
    bgfx::destroy(rgbaTexture);
    bgfx::destroy(rgbaSampler);
    bgfx::destroy(sceneColorProgram);
    bgfx::destroy(arrayProgram);
    bgfx::destroy(volumeProgram);
    bgfx::destroy(cubeProgram);
    bgfx::destroy(objectProgram);
    bgfx::destroy(rgbaProgram);
    harness.Shutdown();
}

void RunForwardGraphCustomCodeRendersTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat33_custom_code";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT33: custom-code render harness vertex shader must cook");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.shadingModel = "unlit";
    RenderMaterialGraphNode custom{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::CustomCode,
        .customCode = RenderMaterialGraphCustomCode{
            .body = "Mask = A.g;\nreturn A * B;",
            .outputType = RenderMaterialGraphPinType::Color,
            .inputs = {
                RenderMaterialGraphCustomPin{ .name = "A", .type = RenderMaterialGraphPinType::Color },
                RenderMaterialGraphCustomPin{ .name = "B", .type = RenderMaterialGraphPinType::Color },
            },
            .outputs = {
                RenderMaterialGraphCustomPin{ .name = "Mask", .type = RenderMaterialGraphPinType::Float },
            },
        },
    };
    RenderMaterialGraphNode a{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0 1 0 1", .overrideSupported = false },
    };
    RenderMaterialGraphNode b{
        .id = 4U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0 0.6 0 1", .overrideSupported = false },
    };
    const RenderMaterialGraphNode output = graph.nodes[0];
    graph.nodes.push_back(custom);
    graph.nodes.push_back(a);
    graph.nodes.push_back(b);
    graph.links.push_back(MakeLink(a, "rgba", custom, "A"));
    graph.links.push_back(MakeLink(b, "rgba", custom, "B"));
    graph.links.push_back(MakeLink(custom, "value", output, "baseColor"));
    graph.links.push_back(MakeLink(custom, "Mask", output, "roughness"));

    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x3302U });
    Require(compiled.Succeeded(), "KBMAT-MAT33: valid CustomCode render graph must compile");
    RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT33: valid CustomCode graph must cook to a real shader binary");

    RenderMaterialGraphDocument invalidGraph = graph;
    for (RenderMaterialGraphNode& node : invalidGraph.nodes) {
        if (node.id == custom.id) {
            node.customCode.body = "return MissingInput + A;";
            break;
        }
    }
    const RenderMaterialGraphCompileResult invalidCompiled = CompileRenderMaterialGraphToShaderSource(invalidGraph, RenderMaterialGraphBuildContext{ .assetId = 0x3303U });
    Require(invalidCompiled.Succeeded(), "KBMAT-MAT33: invalid CustomCode source should reach shaderc validation");
    RenderMaterialGraphShaderArtifactResult invalidResult = CookRenderMaterialGraphShaderArtifact(invalidCompiled.shader, backends, CookRequest((cacheDir / "invalid").generic_string()));
    Require(!invalidResult.Succeeded() && !invalidResult.artifact.has_value() && !invalidResult.diagnostics.empty(),
        "KBMAT-MAT33: shaderc must reject invalid CustomCode and report diagnostics");

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT33: Direct3D11 device unavailable; cannot run GPU custom-code proof\n");
        Require(false, "KBMAT-MAT33: A real GPU device is required to prove CustomCode rendering");
        return;
    }

    const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
    Require(bgfx::isValid(program), "KBMAT-MAT33: custom-code graph program must link on the real GPU backend");
    const ForwardRenderProbe pixel = harness.Render(program, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    Require(pixel.g > pixel.r + 45U && pixel.g > pixel.b + 45U,
        "KBMAT-MAT33: CustomCode return A*B must render a green GPU pixel");

    bgfx::destroy(program);
    harness.Shutdown();
}

void RunForwardGraphMaterialFunctionRendersTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat42_material_function";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT42: material-function render harness vertex shader must cook");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    constexpr std::uint64_t kFunctionAssetId = 0x42420002ULL;
    RenderMaterialGraphDocument functionGraph = MakeDefaultRenderMaterialGraphDocument();
    functionGraph.storageModel = "material-function-asset";
    functionGraph.shadingModel = "unlit";
    functionGraph.nodes.clear();
    RenderMaterialGraphNode functionColor{
        .id = 1U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.9 0.12 0.18 1", .overrideSupported = false },
    };
    RenderMaterialGraphNode functionOutput{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::FunctionOutput,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "Color", .displayName = "Color", .defaultValueHint = "color" },
    };
    functionGraph.nodes.push_back(functionColor);
    functionGraph.nodes.push_back(functionOutput);
    functionGraph.links.push_back(MakeLink(functionColor, "rgba", functionOutput, "value"));

    RenderMaterialGraphFunctionLibrary library{};
    library.entries.push_back(RenderMaterialGraphFunctionLibraryEntry{
        .assetId = kFunctionAssetId,
        .contentHash = 0x42020001ULL,
        .name = "/Game/Functions/GpuRed.kbmatfn",
        .graph = functionGraph,
    });

    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.shadingModel = "unlit";
    RenderMaterialGraphNode call{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::MaterialFunctionCall,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = std::to_string(kFunctionAssetId), .displayName = "Gpu Red" },
        .customCode = RenderMaterialGraphCustomCode{
            .body = {},
            .outputType = RenderMaterialGraphPinType::Color,
            .inputs = {},
            .outputs = {
                RenderMaterialGraphCustomPin{ .name = "Color", .type = RenderMaterialGraphPinType::Color },
            },
        },
    };
    const RenderMaterialGraphNode output = graph.nodes.front();
    graph.nodes.push_back(call);
    graph.links.push_back(MakeLink(call, "Color", output, "baseColor"));

    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(
        graph,
        RenderMaterialGraphBuildContext{ .assetId = 0x4207U, .functionLibrary = &library });
    Require(compiled.Succeeded(), "KBMAT-MAT42: MaterialFunctionCall render graph must compile after inlining");
    Require(compiled.shader.source.find("MaterialFunctionCall") == std::string::npos,
        "KBMAT-MAT42: Generated shader source must not contain function-call scaffolding after inlining");

    RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT42: MaterialFunctionCall graph must cook to a real DXBC binary");

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT42: Direct3D11 device unavailable; cannot run GPU material-function proof\n");
        Require(false, "KBMAT-MAT42: A real GPU device is required to prove MaterialFunctionCall rendering");
        return;
    }

    const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
    Require(bgfx::isValid(program), "KBMAT-MAT42: MaterialFunctionCall graph program must link on the real GPU backend");
    const ForwardRenderProbe pixel = harness.Render(program, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    Require(pixel.r > pixel.g + 60U && pixel.r > pixel.b + 60U,
        "KBMAT-MAT42: Inlined material function must render its red color through the GPU graph program");

    bgfx::destroy(program);
    harness.Shutdown();
}

void RunForwardGraphMaterialLayerStackRendersTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat43_layer_stack";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT43: material-layer render harness vertex shader must cook");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    constexpr std::uint64_t kRedLayerId = 0x43000001ULL;
    constexpr std::uint64_t kBlueLayerId = 0x43000002ULL;
    constexpr std::uint64_t kBlendId = 0x43000003ULL;

    const auto makeLayerFunction = []() {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.storageModel = "material-function-asset";
        graph.shadingModel = "unlit";
        graph.nodes.clear();
        RenderMaterialGraphNode tint{
            .id = 1U,
            .kind = RenderMaterialGraphNodeKind::FunctionInput,
            .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "Tint", .displayName = "Tint", .defaultValueHint = "color" },
        };
        RenderMaterialGraphNode attrs{ .id = 2U, .kind = RenderMaterialGraphNodeKind::MakeMaterialAttributes };
        RenderMaterialGraphNode output{
            .id = 3U,
            .kind = RenderMaterialGraphNodeKind::FunctionOutput,
            .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "Attributes", .displayName = "Attributes", .defaultValueHint = "materialAttributes" },
        };
        graph.nodes.push_back(tint);
        graph.nodes.push_back(attrs);
        graph.nodes.push_back(output);
        graph.links.push_back(MakeLink(tint, "value", attrs, "baseColor"));
        graph.links.push_back(MakeLink(attrs, "attributes", output, "value"));
        return graph;
    };

    const auto makeBlendFunction = []() {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.storageModel = "material-function-asset";
        graph.shadingModel = "unlit";
        graph.nodes.clear();
        RenderMaterialGraphNode a{
            .id = 1U,
            .kind = RenderMaterialGraphNodeKind::FunctionInput,
            .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "A", .displayName = "A", .defaultValueHint = "materialAttributes" },
        };
        RenderMaterialGraphNode b{
            .id = 2U,
            .kind = RenderMaterialGraphNodeKind::FunctionInput,
            .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "B", .displayName = "B", .defaultValueHint = "materialAttributes" },
        };
        RenderMaterialGraphNode factor{
            .id = 3U,
            .kind = RenderMaterialGraphNodeKind::FunctionInput,
            .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "Factor", .displayName = "Factor", .defaultValueHint = "float" },
        };
        RenderMaterialGraphNode blend{ .id = 4U, .kind = RenderMaterialGraphNodeKind::BlendMaterialAttributes };
        RenderMaterialGraphNode output{
            .id = 5U,
            .kind = RenderMaterialGraphNodeKind::FunctionOutput,
            .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "Attributes", .displayName = "Attributes", .defaultValueHint = "materialAttributes" },
        };
        graph.nodes.push_back(a);
        graph.nodes.push_back(b);
        graph.nodes.push_back(factor);
        graph.nodes.push_back(blend);
        graph.nodes.push_back(output);
        graph.links.push_back(MakeLink(a, "value", blend, "a"));
        graph.links.push_back(MakeLink(b, "value", blend, "b"));
        graph.links.push_back(MakeLink(factor, "value", blend, "factor"));
        graph.links.push_back(MakeLink(blend, "attributes", output, "value"));
        return graph;
    };

    RenderMaterialGraphFunctionLibrary library{};
    library.entries.push_back(RenderMaterialGraphFunctionLibraryEntry{ .assetId = kRedLayerId, .contentHash = 0x43010001ULL, .name = "/Game/Layers/GpuRed.kbmatfn", .graph = makeLayerFunction() });
    library.entries.push_back(RenderMaterialGraphFunctionLibraryEntry{ .assetId = kBlueLayerId, .contentHash = 0x43010002ULL, .name = "/Game/Layers/GpuBlue.kbmatfn", .graph = makeLayerFunction() });
    library.entries.push_back(RenderMaterialGraphFunctionLibraryEntry{ .assetId = kBlendId, .contentHash = 0x43010003ULL, .name = "/Game/Layers/GpuHalfBlend.kbmatfn", .graph = makeBlendFunction() });

    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.shadingModel = "unlit";
    RenderMaterialGraphNode stack{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::LayerStack,
        .positionX = -240,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "surfaceLayers", .displayName = "Surface Layers" },
        .layerStack = {
            RenderMaterialGraphLayerStackEntry{
                .layerFunctionAssetId = kRedLayerId,
                .enabled = true,
                .layerName = "Red Base",
                .linkState = "base-linked",
                .layerParameters = {
                    RenderMaterialGraphLayerStackParameter{ .pinName = "Tint", .type = RenderMaterialGraphPinType::Color, .valueHint = "1 0 0 1" },
                },
            },
            RenderMaterialGraphLayerStackEntry{
                .layerFunctionAssetId = kBlueLayerId,
                .blendFunctionAssetId = kBlendId,
                .enabled = true,
                .layerName = "Blue Coat",
                .blendName = "Half Blend",
                .linkState = "coat-linked",
                .layerParameters = {
                    RenderMaterialGraphLayerStackParameter{ .pinName = "Tint", .type = RenderMaterialGraphPinType::Color, .valueHint = "0 0 1 1" },
                },
                .blendParameters = {
                    RenderMaterialGraphLayerStackParameter{ .pinName = "Factor", .type = RenderMaterialGraphPinType::Float, .valueHint = "0.5" },
                },
            },
        },
    };
    const RenderMaterialGraphNode output = graph.nodes.front();
    graph.nodes.push_back(stack);
    graph.links.push_back(MakeLink(stack, "attributes", output, "attributes"));

    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(
        graph,
        RenderMaterialGraphBuildContext{ .assetId = 0x4305U, .functionLibrary = &library });
    Require(compiled.Succeeded(), "KBMAT-MAT43: LayerStack render graph must compile after expansion and inlining");
    Require(compiled.shader.source.find("mix(") != std::string::npos &&
            compiled.shader.source.find("LayerStack") == std::string::npos &&
            compiled.shader.source.find("MaterialFunctionCall") == std::string::npos,
        "KBMAT-MAT43: LayerStack shader source must contain the real blend code and no scaffolding");

    RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT43: LayerStack graph must cook to a real DXBC binary");

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT43: Direct3D11 device unavailable; cannot run GPU layer-stack proof\n");
        Require(false, "KBMAT-MAT43: A real GPU device is required to prove LayerStack rendering");
        return;
    }

    const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
    Require(bgfx::isValid(program), "KBMAT-MAT43: LayerStack graph program must link on the real GPU backend");
    const ForwardRenderProbe pixel = harness.Render(program, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    const int redBlueDelta = std::abs(static_cast<int>(pixel.r) - static_cast<int>(pixel.b));
    Require(pixel.r > 60U && pixel.b > 60U && redBlueDelta < 45 && pixel.g + 35U < pixel.r && pixel.g + 35U < pixel.b,
        "KBMAT-MAT43: two material layers with a 0.5 blend function must render a purple GPU pixel");

    bgfx::destroy(program);
    harness.Shutdown();
}

// MAT-50: math utility nodes must each emit their real GLSL intrinsic/helper and cook to a binary.
// A white constant is routed through the node into emissive; we assert the generated source carries
// the expected symbol and that the fragment shader cooks to a real DXBC binary.
void RunForwardGraphExpLogSrgbNodesCookTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat50_explogsrgb";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    struct Case {
        RenderMaterialGraphNodeKind kind;
        const char* intrinsic;
        const char* inputPin;
        std::uint32_t assetId;
    };
    const Case cases[] = {
        { RenderMaterialGraphNodeKind::Exponential, "exp(", "value", 0x5000U },
        { RenderMaterialGraphNodeKind::Exponential2, "exp2(", "value", 0x5001U },
        { RenderMaterialGraphNodeKind::Logarithm, "log(", "value", 0x5002U },
        { RenderMaterialGraphNodeKind::Logarithm2, "log2(", "value", 0x5003U },
        // sRGB<->linear are pow() gamma curves; the decode exponent 2.2 vs encode 0.454545 disambiguates them.
        { RenderMaterialGraphNodeKind::SrgbToLinear, "2.2", "value", 0x5004U },
        { RenderMaterialGraphNodeKind::LinearToSrgb, "0.454545", "value", 0x5005U },
        { RenderMaterialGraphNodeKind::Logarithm10, "0.434294", "value", 0x5006U },
        // HSV<->RGB call the shared prelude helpers emitted only when those nodes are present.
        { RenderMaterialGraphNodeKind::HsvToRgb, "kbHsvToRgb(", "value", 0x5007U },
        { RenderMaterialGraphNodeKind::RgbToHsv, "kbRgbToHsv(", "value", 0x5008U },
        { RenderMaterialGraphNodeKind::DeriveNormalZ, "normalize(", "value", 0x5009U },
        { RenderMaterialGraphNodeKind::Fmod, "mod(", "a", 0x500AU },
        { RenderMaterialGraphNodeKind::InverseLerp, "mix(", "a", 0x500BU },
        { RenderMaterialGraphNodeKind::PartialDerivativeX, "dFdx(", "value", 0x500CU },
        { RenderMaterialGraphNodeKind::PartialDerivativeY, "dFdy(", "value", 0x500DU },
        { RenderMaterialGraphNodeKind::SphereMask, "smoothstep(", "a", 0x500EU },
        { RenderMaterialGraphNodeKind::BlackBody, "kbBlackBody(", "value", 0x500FU },
        { RenderMaterialGraphNodeKind::Noise, "kbValueNoise(", "value", 0x5010U },
        { RenderMaterialGraphNodeKind::VectorNoise, "kbVectorNoise(", "value", 0x5011U },
        // AppendVector concatenates its float3 "a" with scalar "b" -> vec4(a.xyz, b.x); the grey feeds "a".
        { RenderMaterialGraphNodeKind::AppendVector, ".xyz, (", "a", 0x5012U },
        // ColorRamp blends gradient stops with smoothstep; AntialiasedTextureMask uses screen-space derivatives.
        { RenderMaterialGraphNodeKind::ColorRamp, "smoothstep(", "value", 0x5013U },
        { RenderMaterialGraphNodeKind::AntialiasedTextureMask, "dFdx(", "value", 0x5014U },
        // Transform/TransformPosition default to tangent->world, which multiplies by the interpolated TBN.
        { RenderMaterialGraphNodeKind::Transform, "mat3(ctx.tangent", "value", 0x5015U },
        { RenderMaterialGraphNodeKind::TransformPosition, "mat3(ctx.tangent", "value", 0x5016U },
        // UE-style fast inverse trig nodes emit shared polynomial helpers and must still cook to DXBC.
        { RenderMaterialGraphNodeKind::ArcSineFast, "kbAsinFast(", "value", 0x5018U },
        { RenderMaterialGraphNodeKind::ArcCosineFast, "kbAcosFast(", "value", 0x5019U },
        { RenderMaterialGraphNodeKind::ArcTangentFast, "kbAtanFast(", "value", 0x501AU },
        { RenderMaterialGraphNodeKind::ArcTangent2Fast, "kbAtan2Fast(", "y", 0x501BU },
    };

    for (const Case& testCase : cases) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.shadingModel = "unlit";
        RenderMaterialGraphNode grey{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
        grey.parameter.defaultValueHint = "0.5 0.5 0.5 1";
        graph.nodes.push_back(grey);
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = testCase.kind });
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", testCase.kind, 3U, testCase.inputPin));
        graph.links.push_back(MakeLink(testCase.kind, 3U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"));

        const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = testCase.assetId });
        Require(compiled.Succeeded(), "KBMAT-MAT50: math utility graph must compile");
        Require(compiled.shader.source.find(testCase.intrinsic) != std::string::npos, "KBMAT-MAT50: math node must emit its GLSL intrinsic/helper in the generated source");

        const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
        Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT50: math utility graph must cook");
        const RenderMaterialGraphShaderBinary* binary = result.artifact->FindBinary(RenderMaterialGraphShaderBackend::Dxbc);
        Require(binary != nullptr && binary->byteSize > 0U, "KBMAT-MAT50: the math node shader must cook to a real binary");
    }

    // MAT-50/#14: the world->view Transform path multiplies by the bgfx view matrix (u_view), which had not
    // been exercised by any graph node before. Prove it references u_view AND cooks to a real binary.
    {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.shadingModel = "unlit";
        RenderMaterialGraphNode grey{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
        grey.parameter.defaultValueHint = "0.5 0.5 0.5 1";
        graph.nodes.push_back(grey);
        RenderMaterialGraphNode xform{ .id = 3U, .kind = RenderMaterialGraphNodeKind::Transform };
        xform.parameter.defaultValueHint = "world view";
        graph.nodes.push_back(xform);
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::Transform, 3U, "value"));
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::Transform, 3U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"));

        const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x5017U });
        Require(compiled.Succeeded(), "KBMAT-MAT50: world->view Transform graph must compile");
        Require(compiled.shader.source.find("u_view") != std::string::npos, "KBMAT-MAT50: world->view Transform must multiply by the bgfx view matrix");
        const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
        Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT50: world->view Transform must cook (u_view available in the graph FS)");
        const RenderMaterialGraphShaderBinary* binary = result.artifact->FindBinary(RenderMaterialGraphShaderBackend::Dxbc);
        Require(binary != nullptr && binary->byteSize > 0U, "KBMAT-MAT50: the world->view Transform shader must cook to a real binary");
    }
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

// MAT-50/#32: Runtime Switch is a dynamic index-based branch. It must emit the kbSwitch4 helper,
// cook to DXBC, link as a real bgfx program, and render the selected case through GPU readback.
void RunForwardGraphRuntimeSwitchTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat50_runtime_switch";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT50: Harness vertex shader must cook to a DXBC binary for Runtime Switch");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.shadingModel = "unlit";
    RenderMaterialGraphNode index{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantScalar };
    index.parameter.defaultValueHint = "2.0";
    RenderMaterialGraphNode fallbackRed{ .id = 3U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
    fallbackRed.parameter.defaultValueHint = "1 0 0 1";
    RenderMaterialGraphNode greenCase{ .id = 4U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
    greenCase.parameter.defaultValueHint = "0 1 0 1";
    RenderMaterialGraphNode blueCase{ .id = 5U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
    blueCase.parameter.defaultValueHint = "0 0 1 1";
    graph.nodes.push_back(index);
    graph.nodes.push_back(fallbackRed);
    graph.nodes.push_back(greenCase);
    graph.nodes.push_back(blueCase);
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 6U, .kind = RenderMaterialGraphNodeKind::RuntimeSwitch });
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantScalar, 2U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 6U, "index"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba", RenderMaterialGraphNodeKind::RuntimeSwitch, 6U, "default"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 4U, "rgba", RenderMaterialGraphNodeKind::RuntimeSwitch, 6U, "case1"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 5U, "rgba", RenderMaterialGraphNodeKind::RuntimeSwitch, 6U, "case2"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::RuntimeSwitch, 6U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x5032U });
    Require(compiled.Succeeded(), "KBMAT-MAT50: Runtime Switch graph must compile");
    Require(compiled.shader.source.find("kbSwitch4(") != std::string::npos, "KBMAT-MAT50: Runtime Switch graph must emit kbSwitch4");
    const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT50: Runtime Switch graph must cook");
    const RenderMaterialGraphShaderBinary* binary = result.artifact->FindBinary(RenderMaterialGraphShaderBackend::Dxbc);
    Require(binary != nullptr && binary->byteSize > 0U, "KBMAT-MAT50: Runtime Switch fragment shader must cook to a real binary");

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT50: Direct3D11 device unavailable; cannot run GPU runtime switch proof\n");
        Require(false, "KBMAT-MAT50: A real GPU device is required to prove Runtime Switch selection");
        return;
    }

    const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
    Require(bgfx::isValid(program), "KBMAT-MAT50: Runtime Switch graph program must link");
    const ForwardRenderProbe pixel = harness.Render(program, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    Require(pixel.b > pixel.r + 40U && pixel.b > pixel.g + 40U,
        "KBMAT-MAT50: Runtime Switch case2 branch must render the blue constant on the GPU");

    bgfx::destroy(program);
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

// MAT-52: quality/feature/path/stage switches are compile-time variant selectors. Each graph below
// cooks to DXBC, links as a real bgfx program, and renders the selected branch through D3D11 readback.
void RunForwardGraphVariantSwitchesTest() {
    const std::filesystem::path baseDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat52_variant_switch";
    std::error_code error;
    std::filesystem::remove_all(baseDir, error);
    std::filesystem::create_directories(baseDir, error);

    const std::filesystem::path vsBin = baseDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT52: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    const auto buildSwitchGraph = [](
                                      RenderMaterialGraphNodeKind switchKind,
                                      std::string firstPin,
                                      std::string secondPin) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.shadingModel = "unlit";
        RenderMaterialGraphNode red{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
        red.parameter.defaultValueHint = "1 0 0 1";
        RenderMaterialGraphNode blue{ .id = 3U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
        blue.parameter.defaultValueHint = "0 0 1 1";
        graph.nodes.push_back(red);
        graph.nodes.push_back(blue);
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = switchKind });
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", switchKind, 4U, std::move(firstPin)));
        graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba", switchKind, 4U, std::move(secondPin)));
        graph.links.push_back(MakeLink(switchKind, 4U, "result", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        return graph;
    };

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT52: Direct3D11 device unavailable; cannot run GPU variant switch proof\n");
        Require(false, "KBMAT-MAT52: A real GPU device is required to prove variant switch selection");
        return;
    }

    const auto render = [&](const RenderMaterialGraphDocument& graph, RenderMaterialGraphBuildContext context, const std::filesystem::path& cacheDir) {
        std::error_code subError;
        std::filesystem::create_directories(cacheDir, subError);
        const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, context);
        Require(compiled.Succeeded(), "KBMAT-MAT52: variant switch graph must compile");
        const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
        Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT52: variant switch graph must cook");
        const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
        Require(bgfx::isValid(program), "KBMAT-MAT52: variant switch graph program must link");
        return ForwardRenderHarness::ProbeAt(harness.RenderPixels(program, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE), 32U, 32U);
    };

    const RenderMaterialGraphDocument qualityGraph = buildSwitchGraph(RenderMaterialGraphNodeKind::QualitySwitch, "low", "high");
    const ForwardRenderProbe qualityLow = render(
        qualityGraph,
        RenderMaterialGraphBuildContext{ .assetId = 0x5200U, .qualityLevel = RenderMaterialGraphQualityLevel::Low },
        baseDir / "quality_low");
    const ForwardRenderProbe qualityHigh = render(
        qualityGraph,
        RenderMaterialGraphBuildContext{ .assetId = 0x5201U, .qualityLevel = RenderMaterialGraphQualityLevel::High },
        baseDir / "quality_high");
    Require(qualityLow.r > qualityLow.b + 40U, "KBMAT-MAT52: QualitySwitch low branch must render red on the GPU");
    Require(qualityHigh.b > qualityHigh.r + 40U, "KBMAT-MAT52: QualitySwitch high branch must render blue on the GPU");

    const RenderMaterialGraphDocument featureGraph = buildSwitchGraph(RenderMaterialGraphNodeKind::FeatureLevelSwitch, "es3", "sm6");
    const ForwardRenderProbe featureEs3 = render(
        featureGraph,
        RenderMaterialGraphBuildContext{ .assetId = 0x5202U, .featureLevel = RenderMaterialGraphFeatureLevel::Es3 },
        baseDir / "feature_es3");
    const ForwardRenderProbe featureSm6 = render(
        featureGraph,
        RenderMaterialGraphBuildContext{ .assetId = 0x5203U, .featureLevel = RenderMaterialGraphFeatureLevel::Sm6 },
        baseDir / "feature_sm6");
    Require(featureEs3.r > featureEs3.b + 40U, "KBMAT-MAT52: FeatureLevelSwitch ES3 branch must render red on the GPU");
    Require(featureSm6.b > featureSm6.r + 40U, "KBMAT-MAT52: FeatureLevelSwitch SM6 branch must render blue on the GPU");

    const RenderMaterialGraphDocument pathGraph = buildSwitchGraph(RenderMaterialGraphNodeKind::ShadingPathSwitch, "forward", "deferred");
    const ForwardRenderProbe pathForward = render(
        pathGraph,
        RenderMaterialGraphBuildContext{ .assetId = 0x5204U, .shadingPath = RenderMaterialGraphShadingPath::Forward },
        baseDir / "path_forward");
    const ForwardRenderProbe pathDeferred = render(
        pathGraph,
        RenderMaterialGraphBuildContext{ .assetId = 0x5205U, .shadingPath = RenderMaterialGraphShadingPath::Deferred },
        baseDir / "path_deferred");
    Require(pathForward.r > pathForward.b + 40U, "KBMAT-MAT52: ShadingPathSwitch forward branch must render red on the GPU");
    Require(pathDeferred.b > pathDeferred.r + 40U, "KBMAT-MAT52: ShadingPathSwitch deferred branch must render blue on the GPU");

    const RenderMaterialGraphDocument stageGraph = buildSwitchGraph(RenderMaterialGraphNodeKind::ShaderStageSwitch, "vertex", "fragment");
    const ForwardRenderProbe stageFragment = render(
        stageGraph,
        RenderMaterialGraphBuildContext{ .assetId = 0x5206U },
        baseDir / "stage_fragment");
    Require(stageFragment.b > stageFragment.r + 40U, "KBMAT-MAT52: ShaderStageSwitch fragment branch must render blue on the GPU");

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

    RenderMaterialGraphDocument boundsGraph = MakeDefaultRenderMaterialGraphDocument();
    boundsGraph.shadingModel = "unlit";
    boundsGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ObjectBounds });
    boundsGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ObjectBounds, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult boundsCompiled = CompileRenderMaterialGraphToShaderSource(boundsGraph, RenderMaterialGraphBuildContext{ .assetId = 0x4602U });
    Require(boundsCompiled.Succeeded(), "KBMAT-MAT46: ObjectBounds graph must compile");
    const RenderMaterialGraphShaderArtifactResult boundsResult = CookRenderMaterialGraphShaderArtifact(boundsCompiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(boundsResult.Succeeded() && boundsResult.artifact.has_value(), "KBMAT-MAT46: ObjectBounds graph must cook");

    RenderMaterialGraphDocument orientationGraph = MakeDefaultRenderMaterialGraphDocument();
    orientationGraph.shadingModel = "unlit";
    orientationGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ObjectOrientation });
    orientationGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ObjectOrientation, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult orientationCompiled = CompileRenderMaterialGraphToShaderSource(orientationGraph, RenderMaterialGraphBuildContext{ .assetId = 0x4603U });
    Require(orientationCompiled.Succeeded(), "KBMAT-MAT46: ObjectOrientation graph must compile");
    const RenderMaterialGraphShaderArtifactResult orientationResult = CookRenderMaterialGraphShaderArtifact(orientationCompiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(orientationResult.Succeeded() && orientationResult.artifact.has_value(), "KBMAT-MAT46: ObjectOrientation graph must cook");

    RenderMaterialGraphDocument twoSidedSignGraph = MakeDefaultRenderMaterialGraphDocument();
    twoSidedSignGraph.shadingModel = "unlit";
    twoSidedSignGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::TwoSidedSign });
    RenderMaterialGraphNode white{ .id = 3U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
    white.parameter.defaultValueHint = "1 1 1 1";
    twoSidedSignGraph.nodes.push_back(white);
    twoSidedSignGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::Multiply });
    twoSidedSignGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::TwoSidedSign, 2U, "value", RenderMaterialGraphNodeKind::Multiply, 4U, "a"));
    twoSidedSignGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba", RenderMaterialGraphNodeKind::Multiply, 4U, "b"));
    twoSidedSignGraph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::Multiply, 4U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult twoSidedSignCompiled = CompileRenderMaterialGraphToShaderSource(twoSidedSignGraph, RenderMaterialGraphBuildContext{ .assetId = 0x4604U });
    Require(twoSidedSignCompiled.Succeeded() && twoSidedSignCompiled.shader.source.find("ctx.twoSidedSign") != std::string::npos,
        "KBMAT-MAT46: TwoSidedSign graph must compile through ctx.twoSidedSign");
    const RenderMaterialGraphShaderArtifactResult twoSidedSignResult = CookRenderMaterialGraphShaderArtifact(twoSidedSignCompiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(twoSidedSignResult.Succeeded() && twoSidedSignResult.artifact.has_value(), "KBMAT-MAT46: TwoSidedSign graph must cook");

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

    const bgfx::ProgramHandle boundsProgram = BuildGraphProgram(vsBytes, *boundsResult.artifact);
    Require(bgfx::isValid(boundsProgram), "KBMAT-MAT46: ObjectBounds program must link");
    const ForwardRenderProbe boundsPixel = ForwardRenderHarness::ProbeAt(
        harness.RenderPixels(boundsProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.0F, 0.0F, 0.75F),
        32U,
        32U);
    Require(boundsPixel.r > 120U && boundsPixel.a > 150U,
        "KBMAT-MAT46: ObjectBounds must expose object center.xyz and radius.w from the vertex-supplied bounds");

    const bgfx::ProgramHandle orientationProgram = BuildGraphProgram(vsBytes, *orientationResult.artifact);
    Require(bgfx::isValid(orientationProgram), "KBMAT-MAT46: ObjectOrientation program must link");
    const ForwardRenderProbe orientationPixel = ForwardRenderHarness::ProbeAt(harness.RenderPixels(orientationProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE), 32U, 32U);
    Require(orientationPixel.g > orientationPixel.r + 40U && orientationPixel.g > orientationPixel.b + 80U,
        "KBMAT-MAT46: ObjectOrientation must reach the graph shader as the object-space +Z axis in world space");

    const bgfx::ProgramHandle twoSidedSignProgram = BuildGraphProgram(vsBytes, *twoSidedSignResult.artifact);
    Require(bgfx::isValid(twoSidedSignProgram), "KBMAT-MAT46: TwoSidedSign program must link");
    const ForwardRenderProbe twoSidedSignPixel = ForwardRenderHarness::ProbeAt(harness.RenderPixels(twoSidedSignProgram, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE), 32U, 32U);
    Require(twoSidedSignPixel.r > 200U && twoSidedSignPixel.g > 200U && twoSidedSignPixel.b > 200U,
        "KBMAT-MAT46: front-facing TwoSidedSign must render as +1 on the GPU");

    bgfx::destroy(twoSidedSignProgram);
    bgfx::destroy(orientationProgram);
    bgfx::destroy(boundsProgram);
    bgfx::destroy(fresnelProgram);
    bgfx::destroy(cameraProgram);
    harness.Shutdown();
}

// MAT-50: the Noise node must produce real spatial variation on the GPU. World position (scaled up so the
// harness quad spans several noise cells) feeds Noise -> baseColor (unlit); pixels sampled across the surface
// must span a meaningful brightness range, proving the value-noise field actually varies per fragment.
void RunForwardGraphNoiseRendersTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat50_noise";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT50: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.shadingModel = "unlit";
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::WorldPosition });
    RenderMaterialGraphNode scale{ .id = 3U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
    scale.parameter.defaultValueHint = "24 24 24 1";
    graph.nodes.push_back(scale);
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::Multiply });
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 5U, .kind = RenderMaterialGraphNodeKind::Noise });
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::WorldPosition, 2U, "value", RenderMaterialGraphNodeKind::Multiply, 4U, "a"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba", RenderMaterialGraphNodeKind::Multiply, 4U, "b"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::Multiply, 4U, "value", RenderMaterialGraphNodeKind::Noise, 5U, "value"));
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::Noise, 5U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x5100U });
    Require(compiled.Succeeded(), "KBMAT-MAT50: noise graph must compile");
    Require(compiled.shader.source.find("kbValueNoise(") != std::string::npos, "KBMAT-MAT50: Noise node must call the value-noise helper");
    const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(result.Succeeded() && result.artifact.has_value(), "KBMAT-MAT50: noise graph must cook");

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT50: Direct3D11 device unavailable; cannot run GPU noise proof\n");
        Require(false, "KBMAT-MAT50: A real GPU device is required to prove the noise field varies");
        return;
    }

    const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
    Require(bgfx::isValid(program), "KBMAT-MAT50: noise program must link on the real GPU backend");

    const std::vector<std::uint8_t> pixels = harness.RenderPixels(program, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    const std::uint32_t samples[][2] = { { 8U, 8U }, { 24U, 16U }, { 40U, 24U }, { 56U, 40U }, { 16U, 48U }, { 48U, 56U } };
    int minLum = 1000;
    int maxLum = -1;
    for (const auto& s : samples) {
        const ForwardRenderProbe p = ForwardRenderHarness::ProbeAt(pixels, s[0], s[1]);
        const int lum = static_cast<int>(p.r) + static_cast<int>(p.g) + static_cast<int>(p.b);
        minLum = lum < minLum ? lum : minLum;
        maxLum = lum > maxLum ? lum : maxLum;
    }
    Require(maxLum - minLum > 30,
        "KBMAT-MAT50: the Noise field must produce visibly different brightness across the surface (spatial variation)");

    harness.Shutdown();
}

void RunForwardGraphMaterialParameterCollectionRendersTest() {
    const std::filesystem::path cacheDir = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat50_material_parameter_collection";
    std::error_code error;
    std::filesystem::remove_all(cacheDir, error);
    std::filesystem::create_directories(cacheDir, error);

    const std::filesystem::path vsBin = cacheDir / "vs_graph_probe.bin";
    Require(CookHarnessVertexShader(vsBin), "KBMAT-MAT50-MPC: Harness vertex shader must cook to a DXBC binary");
    const std::vector<std::uint8_t> vsBytes = ReadAllBytes(vsBin);
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };

    constexpr std::uint64_t collectionAssetId = 0x50C011EC7100U;
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.shadingModel = "unlit";
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::CollectionParameter,
        .positionX = 40,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "GpuTint",
            .displayName = "GPU Tint",
            .defaultValueHint = std::to_string(collectionAssetId),
            .overrideSupported = false,
        },
    });
    graph.links.push_back(MakeLink(RenderMaterialGraphNodeKind::CollectionParameter, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    const RenderMaterialGraphCompileResult compiled =
        CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x50C05050U });
    Require(!HasGraphDiagnostic(compiled.diagnostics, RenderMaterialGraphDiagnosticKind::UnsupportedNode),
        "KBMAT-MAT50-MPC: CollectionParameter must be supported by graph validation");
    Require(!HasGraphDiagnostic(compiled.diagnostics, RenderMaterialGraphDiagnosticKind::DisconnectedRequiredOutput),
        "KBMAT-MAT50-MPC: CollectionParameter output must not be treated as a disconnected input node");
    Require(!HasGraphDiagnostic(compiled.diagnostics, RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed),
        "KBMAT-MAT50-MPC: CollectionParameter graph must not produce shader-generation diagnostics");
    Require(!HasGraphDiagnostic(compiled.diagnostics, RenderMaterialGraphDiagnosticKind::TypeMismatch),
        "KBMAT-MAT50-MPC: CollectionParameter output must type-check against BaseColor");
    Require(!HasGraphDiagnostic(compiled.diagnostics, RenderMaterialGraphDiagnosticKind::Cycle),
        "KBMAT-MAT50-MPC: CollectionParameter graph must not create a dependency cycle");
    Require(!HasGraphDiagnostic(compiled.diagnostics, RenderMaterialGraphDiagnosticKind::MissingTexture),
        "KBMAT-MAT50-MPC: CollectionParameter graph must not require a texture binding");
    Require(!HasGraphDiagnostic(compiled.diagnostics, RenderMaterialGraphDiagnosticKind::InvalidColorSpaceRole),
        "KBMAT-MAT50-MPC: CollectionParameter graph must not produce texture color-space diagnostics");
    Require(!HasGraphDiagnostic(compiled.diagnostics, RenderMaterialGraphDiagnosticKind::UnsupportedBlendMode),
        "KBMAT-MAT50-MPC: CollectionParameter graph blend mode must be supported");
    Require(!HasGraphDiagnostic(compiled.diagnostics, RenderMaterialGraphDiagnosticKind::DuplicateParameterStableId),
        "KBMAT-MAT50-MPC: CollectionParameter graph must not duplicate stable ids");
    Require(!HasGraphDiagnostic(compiled.diagnostics, RenderMaterialGraphDiagnosticKind::UnsupportedRenderPathNode),
        "KBMAT-MAT50-MPC: CollectionParameter must be supported on the GPU forward render path");
    Require(!HasGraphDiagnostic(compiled.diagnostics, RenderMaterialGraphDiagnosticKind::TextureSamplerLimitExceeded),
        "KBMAT-MAT50-MPC: CollectionParameter graph must not consume texture samplers");
    Require(!HasGraphDiagnostic(compiled.diagnostics, RenderMaterialGraphDiagnosticKind::UnsupportedMaterialDomain),
        "KBMAT-MAT50-MPC: CollectionParameter graph material domain must be supported");
    Require(!HasGraphDiagnostic(compiled.diagnostics, RenderMaterialGraphDiagnosticKind::UnsupportedShadingModel),
        "KBMAT-MAT50-MPC: CollectionParameter graph shading model must be supported");
    Require(!HasGraphDiagnostic(compiled.diagnostics, RenderMaterialGraphDiagnosticKind::StaticPermutationExplosion),
        "KBMAT-MAT50-MPC: CollectionParameter graph must not expand static permutations");
    Require(!HasGraphDiagnostic(compiled.diagnostics, RenderMaterialGraphDiagnosticKind::MissingMaterialFunction),
        "KBMAT-MAT50-MPC: CollectionParameter graph must not require material functions");
    Require(!HasGraphDiagnostic(compiled.diagnostics, RenderMaterialGraphDiagnosticKind::MaterialFunctionCycle),
        "KBMAT-MAT50-MPC: CollectionParameter graph must not create material function cycles");
    Require(!HasGraphDiagnostic(compiled.diagnostics, RenderMaterialGraphDiagnosticKind::MaterialFunctionSignatureMismatch),
        "KBMAT-MAT50-MPC: CollectionParameter graph must not depend on material function signatures");
    Require(compiled.Succeeded(), "KBMAT-MAT50-MPC: CollectionParameter graph must compile");
    Require(compiled.shader.sourceHash != 0U, "KBMAT-MAT50-MPC: CollectionParameter shader source hash must be non-zero");
    Require(compiled.shader.reflection.uniforms.size() == 1U,
        "KBMAT-MAT50-MPC: CollectionParameter graph reflection must expose one uniform");
    Require(compiled.shader.reflection.uniforms[0].source == RenderMaterialGraphReflectionUniformSource::ParameterCollection,
        "KBMAT-MAT50-MPC: CollectionParameter reflected uniform must be marked as MPC-sourced");
    const RenderMaterialGraphReflectionUniform& uniform = compiled.shader.reflection.uniforms[0];
    Require(compiled.shader.source.find("uniform vec4 " + uniform.name + ";") != std::string::npos,
        "KBMAT-MAT50-MPC: CollectionParameter shader source must declare a GPU uniform");

    const RenderMaterialGraphShaderArtifactResult result =
        CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, CookRequest(cacheDir.generic_string()));
    Require(result.Succeeded() && result.artifact.has_value(),
        "KBMAT-MAT50-MPC: CollectionParameter forward graph fragment shader must cook to a DXBC binary");

    ForwardRenderHarness harness;
    if (!harness.Init()) {
        std::fprintf(stderr, "KBMAT-MAT50-MPC: Direct3D11 device unavailable; cannot run GPU MPC proof\n");
        Require(false, "KBMAT-MAT50-MPC: A real GPU device is required to prove Material Parameter Collection rendering");
        return;
    }

    const bgfx::ProgramHandle program = BuildGraphProgram(vsBytes, *result.artifact);
    Require(bgfx::isValid(program), "KBMAT-MAT50-MPC: CollectionParameter program must link on the real GPU backend");
    const bgfx::UniformHandle tintUniform = bgfx::createUniform(uniform.name.c_str(), bgfx::UniformType::Vec4);
    Require(bgfx::isValid(tintUniform), "KBMAT-MAT50-MPC: MPC reflection uniform must create a real bgfx uniform handle");

    const std::array<float, 4U> redTint{ 0.90F, 0.05F, 0.05F, 1.0F };
    const std::array<float, 4U> greenTint{ 0.05F, 0.85F, 0.05F, 1.0F };
    const ForwardRenderProbe red = ForwardRenderHarness::ProbeAt(
        harness.RenderPixels(program, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.0F, 0.0F, 0.0F, UINT32_MAX, 0x000000ffU, 0U, BGFX_INVALID_HANDLE, 0.0F, 0.0F, 0.0F, 0.0F, BGFX_INVALID_HANDLE, tintUniform, &redTint),
        32U,
        32U);
    const ForwardRenderProbe green = ForwardRenderHarness::ProbeAt(
        harness.RenderPixels(program, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 0.0F, 0.0F, 0.0F, UINT32_MAX, 0x000000ffU, 0U, BGFX_INVALID_HANDLE, 0.0F, 0.0F, 0.0F, 0.0F, BGFX_INVALID_HANDLE, tintUniform, &greenTint),
        32U,
        32U);
    Require(red.r > red.g && red.r > red.b && red.r > 80U,
        "KBMAT-MAT50-MPC: Red MPC value must render a red-dominant pixel through the GPU graph program");
    Require(green.g > green.r && green.g > green.b && green.g > 70U,
        "KBMAT-MAT50-MPC: Live MPC uniform update must render a different green-dominant pixel without a new shader");

    bgfx::destroy(tintUniform);
    harness.Shutdown();
}
#endif

} // namespace

void RunGraphForwardGpuRenderTests() {
#if defined(KB_TEST_GRAPH_SHADERC_PATH)
    RunForwardGraphRenderTest();
    RunForwardGraphAcceptanceSuiteTest();
    RunForwardGraphOrganizationNodesRenderTest();
    RunForwardGraphTimeAnimationTest();
    RunForwardGraphUv1SamplingTest();
    RunForwardGraphVertexColorTest();
    RunForwardGraphScreenPositionTest();
    RunForwardGraphObjectSpaceTest();
    RunForwardGraphPerInstanceTest();
    RunForwardGraphPreSkinnedVertexDataTest();
    RunForwardGraphSamplerStateTest();
    RunForwardGraphTransparentBlendTest();
    RunForwardGraphWorldPositionOffsetTest();
    RunForwardGraphCustomizedUv0Test();
    RunForwardGraphDisplacementVertexOutputTest();
    RunForwardGraphSceneDepthCooksTest();
    RunForwardGraphSceneDepthRendersTest();
    RunForwardGraphTextureExpansionRendersTest();
    RunForwardGraphCustomCodeRendersTest();
    RunForwardGraphMaterialFunctionRendersTest();
    RunForwardGraphMaterialLayerStackRendersTest();
    RunForwardGraphMaterialAttributesTest();
    RunForwardGraphBlendAttributesTest();
    RunForwardGraphGetSetAttributesTest();
    RunForwardGraphMaterialOutputAttributesTest();
    RunForwardGraphShadingModelTest();
    RunForwardGraphMaskedDiscardTest();
    RunForwardGraphBlendModeCompositeTest();
    RunForwardGraphRuntimeSwitchTest();
    RunForwardGraphStaticSwitchTest();
    RunForwardGraphVariantSwitchesTest();
    RunForwardGraphCoordinateNodesTest();
    RunForwardGraphWorldSpaceNodesTest();
    RunForwardGraphExpLogSrgbNodesCookTest();
    RunForwardGraphNoiseRendersTest();
    RunForwardGraphMaterialParameterCollectionRendersTest();
#endif
}

} // namespace kb::render::tests
