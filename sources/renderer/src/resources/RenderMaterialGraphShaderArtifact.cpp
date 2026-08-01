#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"
#include "kb/render/SceneGBufferContract.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <system_error>
#include <unordered_set>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace kb::render {
namespace {

[[nodiscard]] constexpr SceneGBufferShadingModelId GBufferShadingModelId(RenderMaterialShadingModel model) noexcept {
    switch (model) {
    case RenderMaterialShadingModel::Unlit: return SceneGBufferShadingModelId::Unlit;
    case RenderMaterialShadingModel::DefaultLit: return SceneGBufferShadingModelId::DefaultLit;
    case RenderMaterialShadingModel::Subsurface: return SceneGBufferShadingModelId::Subsurface;
    case RenderMaterialShadingModel::ClearCoat: return SceneGBufferShadingModelId::ClearCoat;
    case RenderMaterialShadingModel::Cloth: return SceneGBufferShadingModelId::Cloth;
    case RenderMaterialShadingModel::Hair: return SceneGBufferShadingModelId::Hair;
    case RenderMaterialShadingModel::Eye: return SceneGBufferShadingModelId::Eye;
    case RenderMaterialShadingModel::SingleLayerWater: return SceneGBufferShadingModelId::SingleLayerWater;
    case RenderMaterialShadingModel::ThinTranslucent: return SceneGBufferShadingModelId::ThinTranslucent;
    }
    return SceneGBufferShadingModelId::DefaultLit;
}

constexpr std::uint64_t kArtifactHashPrime = 1099511628211ULL;

void HashByte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kArtifactHashPrime;
}

void HashU64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
        HashByte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void HashString64(std::uint64_t& hash, std::string_view value) noexcept {
    HashU64(hash, static_cast<std::uint64_t>(value.size()));
    for (const char ch : value) {
        HashByte(hash, static_cast<std::uint8_t>(static_cast<unsigned char>(ch)));
    }
}

void AddArtifactDiagnostic(
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics,
    RenderMaterialGraphDiagnosticSeverity severity,
    std::string message,
    std::string pass = {},
    std::string backend = {});

[[nodiscard]] bool ReadTextFileStrict(const std::filesystem::path& path, std::string& text) {
    std::ifstream input{ path, std::ios::binary };
    if (!input) {
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (input.bad()) {
        return false;
    }
    text = buffer.str();
    return true;
}

[[nodiscard]] std::string ReadTextFile(const std::filesystem::path& path) {
    std::string text;
    static_cast<void>(ReadTextFileStrict(path, text));
    return text;
}

[[nodiscard]] std::vector<std::string> ShaderIncludeNames(std::string_view source) {
    std::vector<std::string> names;
    std::size_t lineBegin = 0U;
    while (lineBegin < source.size()) {
        const std::size_t lineEnd = source.find('\n', lineBegin);
        std::string_view line = source.substr(
            lineBegin,
            lineEnd == std::string_view::npos ? source.size() - lineBegin : lineEnd - lineBegin);
        const std::size_t first = line.find_first_not_of(" \t\r");
        if (first != std::string_view::npos) {
            line.remove_prefix(first);
            if (line.front() == '#') {
                line.remove_prefix(1U);
                const std::size_t directiveBegin = line.find_first_not_of(" \t");
                if (directiveBegin != std::string_view::npos) {
                    line.remove_prefix(directiveBegin);
                }
                constexpr std::string_view directive = "include";
                if (line.starts_with(directive) &&
                    (line.size() == directive.size() || line[directive.size()] == ' ' || line[directive.size()] == '\t' ||
                     line[directive.size()] == '"' || line[directive.size()] == '<')) {
                    line.remove_prefix(directive.size());
                    const std::size_t delimiter = line.find_first_of("\"<");
                    if (delimiter != std::string_view::npos) {
                        const char close = line[delimiter] == '"' ? '"' : '>';
                        const std::size_t closeAt = line.find(close, delimiter + 1U);
                        if (closeAt != std::string_view::npos && closeAt > delimiter + 1U) {
                            names.emplace_back(line.substr(delimiter + 1U, closeAt - delimiter - 1U));
                        }
                    }
                }
            }
        }
        if (lineEnd == std::string_view::npos) {
            break;
        }
        lineBegin = lineEnd + 1U;
    }
    return names;
}

struct ResolvedShaderDependency {
    std::filesystem::path path;
    std::string name;
    std::string content;
};

[[nodiscard]] std::optional<std::filesystem::path> ResolveShaderInclude(
    std::string_view name,
    const std::filesystem::path& includingDirectory,
    std::span<const std::string> includeDirs) {
    std::error_code error;
    if (!includingDirectory.empty()) {
        std::filesystem::path candidate = (includingDirectory / std::filesystem::path{ name }).lexically_normal();
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
        error.clear();
    }
    for (const std::string& includeDir : includeDirs) {
        std::filesystem::path candidate = (std::filesystem::path{ includeDir } / std::filesystem::path{ name }).lexically_normal();
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
        error.clear();
    }
    return std::nullopt;
}

[[nodiscard]] bool CollectShaderDependencies(
    std::string_view wrapperSource,
    const RenderMaterialGraphShaderArtifactRequest& request,
    std::vector<ResolvedShaderDependency>& dependencies,
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics) {
    std::unordered_set<std::string> visitedPaths;
    const auto addDependency = [&](const std::filesystem::path& inputPath,
                                   std::string logicalName,
                                   const auto& self) -> bool {
        std::error_code canonicalError;
        std::filesystem::path path = std::filesystem::weakly_canonical(inputPath, canonicalError);
        if (canonicalError) {
            path = inputPath.lexically_normal();
        }
        std::string pathKey = path.generic_string();
#if defined(_WIN32)
        std::ranges::transform(pathKey, pathKey.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
#endif
        if (!visitedPaths.insert(pathKey).second) {
            return true;
        }

        std::string content;
        if (!ReadTextFileStrict(path, content)) {
            AddArtifactDiagnostic(
                diagnostics,
                RenderMaterialGraphDiagnosticSeverity::Error,
                "Material graph shader dependency could not be read: " + path.generic_string() + ".");
            return false;
        }

        dependencies.push_back(ResolvedShaderDependency{
            .path = path,
            .name = std::move(logicalName),
            .content = std::move(content),
        });
        const std::string& dependencySource = dependencies.back().content;
        for (const std::string& includeName : ShaderIncludeNames(dependencySource)) {
            const std::optional<std::filesystem::path> included = ResolveShaderInclude(includeName, path.parent_path(), request.includeDirs);
            if (!included.has_value()) {
                AddArtifactDiagnostic(
                    diagnostics,
                    RenderMaterialGraphDiagnosticSeverity::Error,
                    "Material graph shader include dependency '" + includeName + "' could not be resolved.");
                return false;
            }
            if (!self(*included, includeName, self)) {
                return false;
            }
        }
        return true;
    };

    for (const std::string& dependencyFile : request.dependencyFiles) {
        const std::filesystem::path path{ dependencyFile };
        if (!addDependency(path, path.filename().generic_string(), addDependency)) {
            return false;
        }
    }
    if (!request.varyingDefPath.empty()) {
        const std::filesystem::path varyingPath{ request.varyingDefPath };
        if (!addDependency(varyingPath, varyingPath.filename().generic_string(), addDependency)) {
            return false;
        }
    }
    for (const std::string& includeName : ShaderIncludeNames(wrapperSource)) {
        const std::optional<std::filesystem::path> included = ResolveShaderInclude(includeName, {}, request.includeDirs);
        if (!included.has_value()) {
            AddArtifactDiagnostic(
                diagnostics,
                RenderMaterialGraphDiagnosticSeverity::Error,
                "Material graph shader include dependency '" + includeName + "' could not be resolved.");
            return false;
        }
        if (!addDependency(*included, includeName, addDependency)) {
            return false;
        }
    }

    std::ranges::sort(dependencies, [](const ResolvedShaderDependency& lhs, const ResolvedShaderDependency& rhs) {
        if (lhs.name != rhs.name) {
            return lhs.name < rhs.name;
        }
        return lhs.path.generic_string() < rhs.path.generic_string();
    });
    return true;
}

[[nodiscard]] std::string QuotePath(const std::string& value) {
    return "\"" + value + "\"";
}

#if defined(_WIN32)
[[nodiscard]] std::wstring ShaderCompilerWideCommand(std::string_view command) {
    if (command.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8,
        0,
        command.data(),
        static_cast<int>(command.size()),
        nullptr,
        0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            0,
            command.data(),
            static_cast<int>(command.size()),
            result.data(),
            size) != size) {
        return {};
    }
    return result;
}
#endif

[[nodiscard]] int RunShaderCompilerHidden(
    const std::string& command,
    const std::filesystem::path& diagnosticPath) {
#if defined(_WIN32)
    SECURITY_ATTRIBUTES security{
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = nullptr,
        .bInheritHandle = TRUE,
    };
    const HANDLE diagnostic = CreateFileW(
        diagnosticPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY,
        nullptr);
    if (diagnostic == INVALID_HANDLE_VALUE) {
        return -1;
    }
    const HANDLE nullInput = CreateFileW(
        L"NUL",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = nullInput == INVALID_HANDLE_VALUE ? nullptr : nullInput;
    startup.hStdOutput = diagnostic;
    startup.hStdError = diagnostic;
    PROCESS_INFORMATION process{};
    std::wstring commandLine = ShaderCompilerWideCommand(command);
    const BOOL created = !commandLine.empty() && CreateProcessW(
        nullptr,
        commandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup,
        &process);
    CloseHandle(diagnostic);
    if (nullInput != INVALID_HANDLE_VALUE) {
        CloseHandle(nullInput);
    }
    if (created == FALSE) {
        return -1;
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = static_cast<DWORD>(-1);
    static_cast<void>(GetExitCodeProcess(process.hProcess, &exitCode));
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
#else
    const std::string shellCommand = command + " > " + QuotePath(diagnosticPath.generic_string()) + " 2>&1";
    return std::system(shellCommand.c_str());
#endif
}

[[nodiscard]] std::string TrimDiagnosticText(std::string text) {
    const auto isTrimmed = [](char ch) {
        return ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t';
    };
    while (!text.empty() && isTrimmed(text.back())) {
        text.pop_back();
    }
    std::size_t begin = 0U;
    while (begin < text.size() && isTrimmed(text[begin])) {
        ++begin;
    }
    return text.substr(begin);
}

void AddArtifactDiagnostic(
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics,
    RenderMaterialGraphDiagnosticSeverity severity,
    std::string message,
    std::string pass,
    std::string backend) {
    diagnostics.push_back(RenderMaterialGraphDiagnostic{
        .severity = severity,
        .kind = RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
        .pass = std::move(pass),
        .backend = std::move(backend),
        .message = std::move(message),
    });
}

void AppendMaterialGraphTangentBasis(std::string& wrapper, const RenderMaterialGraphShaderSource& shader) {
    if (shader.reflection.hasWorldPositionOffset || shader.reflection.hasDisplacement) {
        wrapper += "    vec3 basisNormal = normalize(cross(dFdx(v_worldPos), dFdy(v_worldPos)));\n";
        wrapper += "    basisNormal = dot(basisNormal, basisNormal) > 0.0001 ? basisNormal : normalize(v_normal);\n";
        wrapper += "    basisNormal = dot(basisNormal, v_normal) < 0.0 ? -basisNormal : basisNormal;\n";
    } else {
        wrapper += "    vec3 basisNormal = normalize(v_normal);\n";
    }
    wrapper += "    vec3 vertexTangent = normalize(v_tangent - basisNormal * dot(basisNormal, v_tangent));\n";
    wrapper += "    vec3 fallbackAxis = abs(basisNormal.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);\n";
    wrapper += "    vec3 fallbackTangent = normalize(cross(fallbackAxis, basisNormal));\n";
    wrapper += "    vertexTangent = dot(vertexTangent, vertexTangent) > 0.0001 ? vertexTangent : fallbackTangent;\n";
    wrapper += "    float vertexHandedness = dot(cross(basisNormal, vertexTangent), v_bitangent) < 0.0 ? -1.0 : 1.0;\n";
    wrapper += "    vec3 vertexBitangent = normalize(cross(basisNormal, vertexTangent) * vertexHandedness);\n";
    wrapper += "    vec3 basisTangent = vertexTangent;\n";
    wrapper += "    vec3 basisBitangent = vertexBitangent;\n";
}

} // namespace

std::string_view RenderMaterialGraphShaderBackendName(RenderMaterialGraphShaderBackend backend) noexcept {
    switch (backend) {
    case RenderMaterialGraphShaderBackend::Dxbc: return "dxbc";
    case RenderMaterialGraphShaderBackend::Dxil: return "dxil";
    case RenderMaterialGraphShaderBackend::Spirv: return "spirv";
    case RenderMaterialGraphShaderBackend::Metal: return "metal";
    case RenderMaterialGraphShaderBackend::Essl: return "essl";
    case RenderMaterialGraphShaderBackend::Glsl: return "glsl";
    }
    return "spirv";
}

std::string_view RenderMaterialGraphShaderBackendProfile(RenderMaterialGraphShaderBackend backend) noexcept {
    switch (backend) {
    case RenderMaterialGraphShaderBackend::Dxbc: return "s_5_0";
    case RenderMaterialGraphShaderBackend::Dxil: return "s_6_0";
    case RenderMaterialGraphShaderBackend::Spirv: return "spirv";
    case RenderMaterialGraphShaderBackend::Metal: return "metal";
    case RenderMaterialGraphShaderBackend::Essl: return "300_es";
    case RenderMaterialGraphShaderBackend::Glsl: return "440";
    }
    return "spirv";
}

std::string_view RenderMaterialGraphShaderBackendPlatform(RenderMaterialGraphShaderBackend backend) noexcept {
    switch (backend) {
    case RenderMaterialGraphShaderBackend::Dxbc:
    case RenderMaterialGraphShaderBackend::Dxil:
        return "windows";
    case RenderMaterialGraphShaderBackend::Metal:
        return "osx";
    case RenderMaterialGraphShaderBackend::Essl:
        return "android";
    case RenderMaterialGraphShaderBackend::Spirv:
    case RenderMaterialGraphShaderBackend::Glsl:
        return "linux";
    }
    return "linux";
}

std::string_view RenderMaterialGraphShaderBackendDirectory(RenderMaterialGraphShaderBackend backend) noexcept {
    return RenderMaterialGraphShaderBackendName(backend);
}

std::optional<RenderMaterialGraphShaderBackend> ParseRenderMaterialGraphShaderBackend(std::string_view text) noexcept {
    if (text == "dxbc") return RenderMaterialGraphShaderBackend::Dxbc;
    if (text == "dxil") return RenderMaterialGraphShaderBackend::Dxil;
    if (text == "spirv") return RenderMaterialGraphShaderBackend::Spirv;
    if (text == "metal") return RenderMaterialGraphShaderBackend::Metal;
    if (text == "essl") return RenderMaterialGraphShaderBackend::Essl;
    if (text == "glsl") return RenderMaterialGraphShaderBackend::Glsl;
    return std::nullopt;
}

const RenderMaterialGraphShaderBinary* RenderMaterialGraphShaderArtifact::FindBinary(RenderMaterialGraphShaderBackend backend) const noexcept {
    for (const RenderMaterialGraphShaderBinary& binary : binaries) {
        if (binary.backend == backend) {
            return &binary;
        }
    }
    return nullptr;
}

const RenderMaterialGraphShaderBinary* RenderMaterialGraphShaderArtifact::FindVertexBinary(RenderMaterialGraphShaderBackend backend) const noexcept {
    for (const RenderMaterialGraphShaderBinary& binary : vertexBinaries) {
        if (binary.backend == backend) {
            return &binary;
        }
    }
    return nullptr;
}

bool RenderMaterialGraphShaderArtifactResult::Succeeded() const noexcept {
    if (!artifact.has_value()) {
        return false;
    }
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const RenderMaterialGraphDiagnostic& diagnostic) {
        return diagnostic.severity == RenderMaterialGraphDiagnosticSeverity::Error;
    });
}

const RenderMaterialGraphShaderManifestEntry* RenderMaterialGraphShaderManifest::Find(
    std::uint64_t graphSourceHash,
    std::uint64_t variantKey,
    std::string_view pass,
    RenderMaterialGraphShaderBackend backend) const noexcept {
    for (const RenderMaterialGraphShaderManifestEntry& entry : entries) {
        if (entry.graphSourceHash == graphSourceHash &&
            entry.variantKey == variantKey &&
            entry.pass == pass &&
            entry.backend == backend) {
            return &entry;
        }
    }
    return nullptr;
}

const RenderMaterialGraphShaderManifestEntry* RenderMaterialGraphShaderManifest::Find(
    std::uint64_t graphSourceHash,
    std::string_view pass,
    RenderMaterialGraphShaderBackend backend) const noexcept {
    const RenderMaterialGraphShaderManifestEntry* match = nullptr;
    for (const RenderMaterialGraphShaderManifestEntry& entry : entries) {
        if (entry.graphSourceHash != graphSourceHash || entry.pass != pass || entry.backend != backend) {
            continue;
        }
        if (match != nullptr && match->variantKey != entry.variantKey) {
            return nullptr;
        }
        match = &entry;
    }
    return match;
}

std::string BuildGraphFragmentWrapperSource(
    const RenderMaterialGraphShaderSource& shader,
    std::string_view pass) {
    const bool shadowPass = pass == "ShadowDepth";
    const bool gbufferPass = pass == "GBuffer";

    std::string wrapper;
    wrapper += "$input v_normal, v_color0, v_texcoord0, v_worldPos, v_shadowPos, v_shadowFlags, v_tangent, v_bitangent, v_objectLocalPos, v_objectWorldPos, v_objectOrientation, v_preSkinnedNormal\n\n";
    wrapper += "#include <bgfx_shader.sh>\n";
    if (!shadowPass) {
        wrapper += "#include \"pbr_graph_forward.sh\"\n";
        wrapper += "SAMPLER2D(s_terrainLayerWeights, 15);\n";
        wrapper += "uniform vec4 u_terrainLayerParams;\n";
        wrapper += "float KbTerrainLayerOpacity(vec2 terrainUv)\n{\n";
        wrapper += "    if (u_terrainLayerParams.y < 0.5 || u_terrainLayerParams.x < 0.5) return 1.0;\n";
        wrapper += "    vec4 weights = texture2D(s_terrainLayerWeights, terrainUv);\n";
        wrapper += "    if (u_terrainLayerParams.x < 1.5) return weights.y / max(weights.x + weights.y, 0.0001);\n";
        wrapper += "    if (u_terrainLayerParams.x < 2.5) return weights.z / max(weights.x + weights.y + weights.z, 0.0001);\n";
        wrapper += "    return weights.w / max(weights.x + weights.y + weights.z + weights.w, 0.0001);\n}\n";
    }
    if (gbufferPass) {
        wrapper += "#include \"gbuffer_contract.sh\"\n";
    }
    // MAT-72 frame time constants (x=time seconds, y=deltaTime, z=frameIndex). Bound per frame by
    // SceneMeshPassResources so graph Time/animation nodes read real engine time.
    wrapper += "uniform vec4 u_time;\n";
    wrapper += "uniform vec4 u_dynamicParameter;\n";
    wrapper += "uniform vec4 u_materialParams;\n";
    wrapper += "\n// pass:" + std::string{ pass } + "\n\n";
    wrapper += shader.source;
    wrapper += "\nvoid main()\n{\n";
    wrapper += "    MaterialGraphContext ctx;\n";
    wrapper += "    ctx.uv0 = v_texcoord0;\n";
    wrapper += "    ctx.uv1 = v_shadowFlags.zw;\n";
    wrapper += "    ctx.normal = normalize(v_normal);\n";
    wrapper += "    ctx.tangent = normalize(v_tangent);\n";
    wrapper += "    ctx.bitangent = normalize(v_bitangent);\n";
    wrapper += "    ctx.worldPos = v_worldPos;\n";
    if (shadowPass) {
        wrapper += "    ctx.viewDir = vec3(0.0, 0.0, 1.0);\n";
    } else {
        wrapper += "    ctx.viewDir = normalize(u_cameraPosition.xyz - v_worldPos);\n";
    }
    wrapper += "    ctx.vertexColor = v_color0;\n";
    wrapper += "    ctx.time = u_time.x;\n";
    wrapper += "    ctx.deltaTime = u_time.y;\n";
    wrapper += "    ctx.dynamicParameter = u_dynamicParameter;\n";
    // MAT-75 screen-space coordinate (0..1) from the fragment position and bgfx viewport rect.
    wrapper += "    ctx.screenPosition = gl_FragCoord.xy / max(u_viewRect.zw, vec2(1.0, 1.0));\n";
    // MAT-76 object-space inputs interpolated from the vertex shader.
    wrapper += "    ctx.localPosition = v_objectLocalPos.xyz;\n";
    wrapper += "    ctx.objectPosition = v_objectWorldPos.xyz;\n";
    // MAT-77 per-instance scalars carried in the free .w lanes of the object-space varyings.
    wrapper += "    ctx.perInstanceRandom = v_objectLocalPos.w;\n";
    wrapper += "    ctx.objectRadius = v_objectWorldPos.w;\n";
    wrapper += "    ctx.perInstanceFadeAmount = v_shadowFlags.y;\n";
    wrapper += "    ctx.perInstanceCustomData = v_objectOrientation.w;\n";
    wrapper += "    ctx.objectBounds = vec4(v_objectWorldPos.xyz, max(v_objectWorldPos.w, 0.0));\n";
    wrapper += "    ctx.objectOrientation = dot(v_objectOrientation.xyz, v_objectOrientation.xyz) > 0.0001 ? normalize(v_objectOrientation.xyz) : vec3(0.0, 0.0, 1.0);\n";
    wrapper += "    ctx.preSkinnedPosition = v_objectLocalPos.xyz;\n";
    wrapper += "    ctx.preSkinnedNormal = dot(v_preSkinnedNormal, v_preSkinnedNormal) > 0.0001 ? normalize(v_preSkinnedNormal) : vec3(0.0, 0.0, 1.0);\n";
    // MAT-46: world-space view/light inputs. The shadow pass has no lighting uniforms, so it uses explicit
    // neutral constants; only the forward pass reads the real camera/light/viewport state.
    wrapper += "    ctx.viewSize = u_viewRect.zw;\n";
    wrapper += "    ctx.twoSidedSign = gl_FrontFacing ? 1.0 : -1.0;\n";
    // MAT-80/#18b: this fragment's device depth, so DepthFade can compare against the sampled scene depth.
    wrapper += "    ctx.fragmentDepth = gl_FragCoord.z;\n";
    if (shadowPass) {
        wrapper += "    ctx.cameraPosition = vec3(0.0, 0.0, 0.0);\n";
        wrapper += "    ctx.lightVector = vec3(0.0, 1.0, 0.0);\n";
    } else {
        wrapper += "    ctx.cameraPosition = u_cameraPosition.xyz;\n";
        wrapper += "    ctx.lightVector = (u_lightParams.x > 0.5) ? normalize(-u_lightDirKind[0].xyz) : vec3(0.0, 1.0, 0.0);\n";
    }
    wrapper += "    MaterialSurface surface = EvaluateMaterialGraph(ctx);\n";
    if (shadowPass) {
        wrapper += "    if (surface.alpha < surface.alphaClipThreshold)\n    {\n        discard;\n    }\n";
        wrapper += "    gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);\n";
    } else {
        wrapper += "    float terrainLayerOpacity = KbTerrainLayerOpacity(v_shadowFlags.zw);\n";
        wrapper += "    if (u_terrainLayerParams.y > 0.5 && terrainLayerOpacity <= 0.0001)\n    {\n        discard;\n    }\n";
        wrapper += "    surface.alpha *= terrainLayerOpacity;\n";
        // MAT-38 Masked: clip fragments whose alpha is below the clip threshold so the background shows
        // through (binary opacity). The transparent modes keep every fragment and blend at the ROP stage.
        if (shader.reflection.blendMode == RenderMaterialGraphBlendMode::Masked) {
            wrapper += "    if (surface.alpha < surface.alphaClipThreshold)\n    {\n        discard;\n    }\n";
        }
        if (gbufferPass) {
            const auto shadingModelId = static_cast<std::uint32_t>(GBufferShadingModelId(shader.reflection.shadingModel));
            AppendMaterialGraphTangentBasis(wrapper, shader);
            if (shader.reflection.hasTangentOutput) {
                wrapper += "    vec3 materialTangent = basisTangent * surface.tangentOutput.x + basisBitangent * surface.tangentOutput.y + basisNormal * surface.tangentOutput.z;\n";
                wrapper += "    materialTangent = dot(materialTangent, materialTangent) > 0.0001 ? normalize(materialTangent) : basisTangent;\n";
                wrapper += "    basisTangent = normalize(materialTangent - basisNormal * dot(basisNormal, materialTangent));\n";
                wrapper += "    basisTangent = dot(basisTangent, basisTangent) > 0.0001 ? basisTangent : normalize(v_tangent);\n";
                wrapper += "    basisBitangent = normalize(cross(basisNormal, basisTangent) * vertexHandedness);\n";
            }
            wrapper += "    vec3 graphNormal = surface.normal;\n";
            wrapper += "    graphNormal = (u_materialParams.z > 0.0) ? normalize(vec3(graphNormal.xy * u_materialParams.z, graphNormal.z)) : vec3(0.0, 0.0, 1.0);\n";
            wrapper += "    vec3 worldNormal = normalize(basisTangent * graphNormal.x + basisBitangent * graphNormal.y + basisNormal * graphNormal.z);\n";
            wrapper += "    gl_FragData[0] = vec4(surface.baseColor.rgb, 1.0);\n";
            wrapper += "    gl_FragData[1] = vec4(worldNormal * 0.5 + 0.5, 1.0);\n";
            wrapper += "    gl_FragData[2] = vec4(clamp(surface.metallic, 0.0, 1.0), clamp(surface.roughness, 0.04, 1.0), clamp(surface.occlusion, 0.0, 1.0), KbEncodeGBufferShadingModel(" + std::to_string(shadingModelId) + ".0));\n";
            wrapper += "    gl_FragData[3] = vec4(surface.emissive, clamp(surface.specular, 0.0, 1.0));\n";
        } else if (shader.reflection.shadingModel == RenderMaterialShadingModel::Unlit) {
            // MAT-37 Unlit: the surface emissive plus base color go straight to the framebuffer with no lighting.
            wrapper += "    gl_FragColor = vec4(surface.baseColor.rgb + surface.emissive, surface.alpha);\n";
        } else {
            // MAT-37 DefaultLit: the metallic-roughness forward PBR path.
            AppendMaterialGraphTangentBasis(wrapper, shader);
            if (shader.reflection.hasTangentOutput) {
                wrapper += "    vec3 materialTangent = basisTangent * surface.tangentOutput.x + basisBitangent * surface.tangentOutput.y + basisNormal * surface.tangentOutput.z;\n";
                wrapper += "    materialTangent = dot(materialTangent, materialTangent) > 0.0001 ? normalize(materialTangent) : basisTangent;\n";
                wrapper += "    basisTangent = normalize(materialTangent - basisNormal * dot(basisNormal, materialTangent));\n";
                wrapper += "    basisTangent = dot(basisTangent, basisTangent) > 0.0001 ? basisTangent : normalize(v_tangent);\n";
                wrapper += "    basisBitangent = normalize(cross(basisNormal, basisTangent) * vertexHandedness);\n";
            }
            wrapper += "    vec3 graphNormal = surface.normal;\n";
            wrapper += "    graphNormal = (u_materialParams.z > 0.0) ? normalize(vec3(graphNormal.xy * u_materialParams.z, graphNormal.z)) : vec3(0.0, 0.0, 1.0);\n";
            wrapper += "    vec3 worldNormal = normalize(basisTangent * graphNormal.x + basisBitangent * graphNormal.y + basisNormal * graphNormal.z);\n";
            wrapper += "    float metallic = clamp(surface.metallic, 0.0, 1.0);\n";
            wrapper += "    float roughness = clamp(surface.roughness, 0.04, 1.0);\n";
            wrapper += "    float occlusion = clamp(surface.occlusion, 0.0, 1.0);\n";
            wrapper += "    float specular = clamp(surface.specular, 0.0, 1.0);\n";
            wrapper += "    vec3 lighting = KbEvaluateForwardLighting(worldNormal, v_worldPos, surface.baseColor.rgb, metallic, roughness, specular, occlusion);\n";
            wrapper += "    gl_FragColor = vec4(lighting + surface.emissive, surface.alpha);\n";
        }
    }
    wrapper += "}\n";
    return wrapper;
}

std::string BuildGraphVertexWrapperSource(const RenderMaterialGraphShaderSource& shader) {
    // MAT-67/#54: the instanced mesh vertex shader with graph vertex-domain outputs applied before
    // projection/rasterization. WPO and displacement move real geometry; CustomizedUV0 rewrites the
    // interpolated UV that fragment TextureSample nodes read. Mirrors vs_mesh_instanced.sc.
    std::string vs;
    vs += "$input a_position, a_normal, a_tangent, a_texcoord0, a_texcoord1, a_color0, i_data0, i_data1, i_data2, i_data3, i_data4\n";
    vs += "$output v_normal, v_color0, v_texcoord0, v_worldPos, v_shadowPos, v_shadowFlags, v_tangent, v_bitangent, v_objectLocalPos, v_objectWorldPos, v_objectOrientation, v_preSkinnedNormal\n\n";
    vs += "#include <bgfx_shader.sh>\n";
    vs += "uniform mat4 u_shadowViewProj;\n";
    vs += "uniform vec4 u_time;\n";
    vs += "uniform vec4 u_dynamicParameter;\n\n";
    vs += shader.source;
    vs += "\nvoid main()\n{\n";
    vs += "    float instanceRandom = i_data0.w;\n";
    vs += "    float instanceRadius = i_data1.w;\n";
    vs += "    float instanceFadeAmount = i_data2.w;\n";
    vs += "    float instanceCustomData = i_data3.w;\n";
    vs += "    mat4 model = mtxFromCols(vec4(i_data0.xyz, 0.0), vec4(i_data1.xyz, 0.0), vec4(i_data2.xyz, 0.0), vec4(i_data3.xyz, 1.0));\n";
    vs += "    vec4 worldPos = mul(model, vec4(a_position, 1.0));\n";
    vs += "    vec3 objectWorldPos = mul(model, vec4(0.0, 0.0, 0.0, 1.0)).xyz;\n";
    vs += "    vec3 objectOrientationRaw = mul(model, vec4(0.0, 0.0, 1.0, 0.0)).xyz;\n";
    vs += "    vec3 objectOrientation = dot(objectOrientationRaw, objectOrientationRaw) > 0.0001 ? normalize(objectOrientationRaw) : vec3(0.0, 0.0, 1.0);\n";
    vs += "    vec3 vsNormal = normalize(mul(model, vec4(a_normal, 0.0)).xyz);\n";
    vs += "    vec3 vsTangent = mul(model, vec4(a_tangent.xyz, 0.0)).xyz;\n";
    vs += "    vec3 fallbackAxis = abs(vsNormal.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);\n";
    vs += "    vsTangent = dot(vsTangent, vsTangent) > 0.0001 ? normalize(vsTangent) : normalize(cross(fallbackAxis, vsNormal));\n";
    vs += "    vsTangent = normalize(vsTangent - vsNormal * dot(vsNormal, vsTangent));\n";
    vs += "    float handedness = abs(a_tangent.w) > 0.0001 ? a_tangent.w : 1.0;\n";
    vs += "    vec3 vsBitangent = normalize(cross(vsNormal, vsTangent) * handedness);\n";
    vs += "    vec3 preSkinnedNormal = dot(a_normal, a_normal) > 0.0001 ? normalize(a_normal) : vec3(0.0, 0.0, 1.0);\n";
    vs += "    MaterialGraphContext ctx;\n";
    vs += "    ctx.uv0 = a_texcoord0;\n";
    vs += "    ctx.uv1 = a_texcoord1;\n";
    vs += "    ctx.normal = vsNormal;\n";
    vs += "    ctx.tangent = vsTangent;\n";
    vs += "    ctx.bitangent = vsBitangent;\n";
    vs += "    ctx.worldPos = worldPos.xyz;\n";
    vs += "    ctx.viewDir = vec3(0.0, 0.0, 1.0);\n";
    vs += "    ctx.vertexColor = a_color0 * vec4(i_data4.rgb, abs(i_data4.w));\n";
    vs += "    ctx.time = u_time.x;\n";
    vs += "    ctx.deltaTime = u_time.y;\n";
    vs += "    ctx.dynamicParameter = u_dynamicParameter;\n";
    vs += "    ctx.screenPosition = vec2(0.0, 0.0);\n";
    vs += "    ctx.localPosition = a_position;\n";
    vs += "    ctx.objectPosition = objectWorldPos;\n";
    vs += "    ctx.perInstanceRandom = instanceRandom;\n";
    vs += "    ctx.objectRadius = instanceRadius;\n";
    vs += "    ctx.perInstanceFadeAmount = instanceFadeAmount;\n";
    vs += "    ctx.perInstanceCustomData = instanceCustomData;\n";
    vs += "    ctx.objectBounds = vec4(objectWorldPos, max(instanceRadius, 0.0));\n";
    vs += "    ctx.objectOrientation = objectOrientation;\n";
    vs += "    ctx.preSkinnedPosition = a_position;\n";
    vs += "    ctx.preSkinnedNormal = preSkinnedNormal;\n";
    vs += "    ctx.cameraPosition = vec3(0.0, 0.0, 0.0);\n";
    vs += "    ctx.lightVector = vec3(0.0, 1.0, 0.0);\n";
    vs += "    ctx.viewSize = vec2(0.0, 0.0);\n";
    vs += "    ctx.twoSidedSign = 1.0;\n";
    vs += "    ctx.fragmentDepth = 0.0;\n";
    vs += "    vec2 materialUv0 = a_texcoord0;\n";
    if (shader.reflection.hasCustomizedUv0) {
        vs += "    materialUv0 = EvaluateCustomizedUv0(ctx);\n";
        vs += "    ctx.uv0 = materialUv0;\n";
    }
    if (shader.reflection.hasWorldPositionOffset) {
        vs += "    worldPos.xyz += EvaluateWorldPositionOffset(ctx);\n";
    }
    if (shader.reflection.hasDisplacement) {
        vs += "    worldPos.xyz += EvaluateDisplacement(ctx);\n";
    }
    vs += "    gl_Position = mul(u_viewProj, worldPos);\n";
    vs += "    v_worldPos = worldPos.xyz;\n";
    vs += "    v_objectLocalPos = vec4(a_position, instanceRandom);\n";
    vs += "    v_objectWorldPos = vec4(objectWorldPos, instanceRadius);\n";
    vs += "    v_objectOrientation = vec4(objectOrientation, instanceCustomData);\n";
    vs += "    v_shadowPos = mul(u_shadowViewProj, worldPos);\n";
    vs += "    v_shadowFlags = vec4(i_data4.w >= 0.0 ? 1.0 : 0.0, instanceFadeAmount, a_texcoord1.x, a_texcoord1.y);\n";
    vs += "    v_normal = vsNormal;\n";
    vs += "    v_tangent = vsTangent;\n";
    vs += "    v_bitangent = vsBitangent;\n";
    vs += "    v_texcoord0 = materialUv0;\n";
    vs += "    v_color0 = ctx.vertexColor;\n";
    vs += "    v_preSkinnedNormal = preSkinnedNormal;\n";
    vs += "}\n";
    return vs;
}

std::uint64_t ComputeRenderMaterialGraphReflectionHash(const RenderMaterialGraphReflection& reflection) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    HashU64(hash, reflection.uniforms.size());
    for (const RenderMaterialGraphReflectionUniform& uniform : reflection.uniforms) {
        HashString64(hash, uniform.name);
        HashString64(hash, uniform.stableId);
        HashU64(hash, static_cast<std::uint64_t>(uniform.kind));
        HashU64(hash, static_cast<std::uint64_t>(uniform.source));
        HashU64(hash, uniform.collectionAssetId);
        HashString64(hash, uniform.collectionParameterStableId);
        // Dynamic parameter defaults are binding data, not shader/program identity. Changing a
        // default must update uploaded uniforms without creating a new GPU permutation.
    }
    HashU64(hash, reflection.textures.size());
    for (const RenderMaterialGraphReflectionTexture& texture : reflection.textures) {
        HashString64(hash, texture.samplerName);
        HashString64(hash, texture.stableId);
        HashString64(hash, texture.role);
        HashU64(hash, texture.slot);
        HashU64(hash, static_cast<std::uint64_t>(texture.colorSpace));
        HashU64(hash, static_cast<std::uint64_t>(texture.samplerState.minFilter));
        HashU64(hash, static_cast<std::uint64_t>(texture.samplerState.magFilter));
        HashU64(hash, static_cast<std::uint64_t>(texture.samplerState.mipFilter));
        HashU64(hash, static_cast<std::uint64_t>(texture.samplerState.wrapU));
        HashU64(hash, static_cast<std::uint64_t>(texture.samplerState.wrapV));
        HashU64(hash, static_cast<std::uint64_t>(texture.dimension));
    }
    HashU64(hash, reflection.requiredVaryings.size());
    for (const std::string& varying : reflection.requiredVaryings) {
        HashString64(hash, varying);
    }
    HashU64(hash, reflection.hasWorldPositionOffset ? 1U : 0U);
    HashU64(hash, reflection.hasCustomizedUv0 ? 1U : 0U);
    HashU64(hash, reflection.hasDisplacement ? 1U : 0U);
    HashU64(hash, reflection.hasTangentOutput ? 1U : 0U);
    // MAT-37: the shading model selects the fragment wrapper lighting branch, so it is part of program identity.
    HashU64(hash, static_cast<std::uint64_t>(reflection.shadingModel));
    // MAT-38: the blend mode changes the wrapper (masked clip) and the cooked pass, so it is part of identity.
    HashU64(hash, static_cast<std::uint64_t>(reflection.blendMode));
    HashU64(hash, reflection.usesSceneDepth ? 1U : 0U);
    HashU64(hash, reflection.usesSceneColor ? 1U : 0U);
    return hash;
}

std::uint64_t ComputeRenderMaterialGraphVariantKey(const RenderMaterialGraphShaderSource& shader) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    HashU64(hash, shader.sourceHash);
    HashU64(hash, kRenderMaterialGraphShaderWrapperVersion);
    HashU64(hash, ComputeRenderMaterialGraphReflectionHash(shader.reflection));
    HashString64(hash, shader.entryPoint);
    return hash;
}

RenderMaterialGraphShaderArtifactResult CookRenderMaterialGraphShaderArtifact(
    const RenderMaterialGraphShaderSource& shader,
    std::span<const RenderMaterialGraphShaderBackend> backends,
    const RenderMaterialGraphShaderArtifactRequest& request) {
    RenderMaterialGraphShaderArtifactResult result{};

    if (request.shadercPath.empty() || request.cacheRoot.empty()) {
        AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
            "Material graph shader cook requires a shaderc tool path and a cache root.");
        return result;
    }

    RenderMaterialGraphShaderArtifact artifact{};
    artifact.graphSourceHash = shader.sourceHash;
    artifact.variantKey = ComputeRenderMaterialGraphVariantKey(shader);
    artifact.pass = request.pass;
    artifact.entryPoint = shader.entryPoint;
    artifact.graphGenerated = true;
    artifact.wrapperSource = BuildGraphFragmentWrapperSource(shader, request.pass);
    std::uint64_t wrapperHash = 1469598103934665603ULL;
    HashString64(wrapperHash, artifact.wrapperSource);
    artifact.wrapperHash = wrapperHash;
    artifact.reflectionHash = ComputeRenderMaterialGraphReflectionHash(shader.reflection);
    artifact.materialTypeVersion = request.materialTypeVersion;

    // Dependency graph: the wrapper includes (e.g. the shared PBR library, varying definitions)
    // contribute to the artifact identity so editing them invalidates cooked binaries.
    std::vector<ResolvedShaderDependency> dependencies;
    if (!CollectShaderDependencies(artifact.wrapperSource, request, dependencies, result.diagnostics)) {
        return result;
    }
    std::uint64_t dependencyHash = 1469598103934665603ULL;
    // Resolution order is compiler input: two include roots containing the same logical
    // name may resolve to different files. Keep both the ordered roots and the resolved
    // canonical path in the identity instead of hashing only logical name/content.
    HashU64(dependencyHash, request.includeDirs.size());
    for (std::size_t index = 0U; index < request.includeDirs.size(); ++index) {
        std::error_code canonicalError;
        std::filesystem::path includePath = std::filesystem::weakly_canonical(request.includeDirs[index], canonicalError);
        if (canonicalError) {
            includePath = std::filesystem::path{ request.includeDirs[index] }.lexically_normal();
        }
        HashU64(dependencyHash, index);
        HashString64(dependencyHash, includePath.generic_string());
        artifact.dependencies.push_back(RenderMaterialGraphArtifactDependency{
            .name = "include-dir[" + std::to_string(index) + "]:" + includePath.generic_string(),
            .contentHash = 0U,
        });
    }

    std::error_code shadercCanonicalError;
    std::filesystem::path shadercPath = std::filesystem::weakly_canonical(request.shadercPath, shadercCanonicalError);
    if (shadercCanonicalError) {
        shadercPath = std::filesystem::path{ request.shadercPath }.lexically_normal();
    }
    std::string shadercBytes;
    if (!ReadTextFileStrict(shadercPath, shadercBytes)) {
        AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
            "Material graph shader compiler identity could not be read: " + shadercPath.generic_string() + ".");
        return result;
    }
    std::uint64_t shadercHash = 1469598103934665603ULL;
    HashString64(shadercHash, shadercBytes);
    HashString64(dependencyHash, shadercPath.generic_string());
    HashU64(dependencyHash, shadercHash);
    artifact.dependencies.push_back(RenderMaterialGraphArtifactDependency{
        .name = "shaderc:" + shadercPath.generic_string(),
        .contentHash = shadercHash,
    });

    HashU64(dependencyHash, dependencies.size());
    for (const ResolvedShaderDependency& dependency : dependencies) {
        std::uint64_t contentHash = 1469598103934665603ULL;
        HashString64(contentHash, dependency.content);
        HashString64(dependencyHash, dependency.name);
        HashString64(dependencyHash, dependency.path.generic_string());
        HashU64(dependencyHash, contentHash);
        artifact.dependencies.push_back(RenderMaterialGraphArtifactDependency{
            .name = dependency.name,
            .contentHash = contentHash,
        });
    }
    artifact.dependencyHash = dependencyHash;

    std::uint64_t artifactHash = 1469598103934665603ULL;
    HashU64(artifactHash, artifact.graphSourceHash);
    HashU64(artifactHash, kRenderMaterialGraphShaderWrapperVersion);
    HashU64(artifactHash, artifact.variantKey);
    HashU64(artifactHash, artifact.wrapperHash);
    HashU64(artifactHash, artifact.dependencyHash);
    HashU64(artifactHash, artifact.reflectionHash);
    HashU64(artifactHash, artifact.materialTypeVersion);
    artifact.artifactHash = artifactHash;

    // The per-binary cache key combines the wrapper, its dependencies and the material type version,
    // so any of those changing forces a recompile rather than serving a stale binary.
    std::uint64_t cookKey = 1469598103934665603ULL;
    HashU64(cookKey, kRenderMaterialGraphShaderWrapperVersion);
    HashU64(cookKey, artifact.wrapperHash);
    HashU64(cookKey, artifact.dependencyHash);
    HashU64(cookKey, artifact.materialTypeVersion);

    std::error_code error;
    const std::filesystem::path passRoot = std::filesystem::path{ request.cacheRoot } /
        ("graph_" + std::to_string(shader.sourceHash)) /
        ("variant_" + std::to_string(artifact.variantKey)) /
        request.pass;
    std::filesystem::create_directories(passRoot, error);
    const std::filesystem::path wrapperPath = passRoot / "fs_graph.sc";
    {
        std::ofstream wrapperOut{ wrapperPath, std::ios::binary | std::ios::trunc };
        if (!wrapperOut) {
            AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                "Material graph shader cook could not write the shader wrapper to " + wrapperPath.generic_string() + ".");
            return result;
        }
        wrapperOut << artifact.wrapperSource;
    }

    for (const RenderMaterialGraphShaderBackend backend : backends) {
        const std::filesystem::path backendDir = passRoot / std::string{ RenderMaterialGraphShaderBackendDirectory(backend) };
        std::filesystem::create_directories(backendDir, error);
        const std::filesystem::path binaryPath = backendDir / "fs.bin";
        const std::filesystem::path hashPath = backendDir / "fs.bin.hash";

        const std::string cachedHash = TrimDiagnosticText(ReadTextFile(hashPath));
        const bool cached = !cachedHash.empty() &&
            cachedHash == std::to_string(cookKey) &&
            std::filesystem::exists(binaryPath, error) &&
            std::filesystem::file_size(binaryPath, error) > 0U;
        if (cached) {
            artifact.binaries.push_back(RenderMaterialGraphShaderBinary{
                .backend = backend,
                .binaryPath = binaryPath.generic_string(),
                .byteSize = std::filesystem::file_size(binaryPath, error),
                .cacheHit = true,
            });
            continue;
        }

        const std::filesystem::path errorPath = backendDir / "fs.shaderc.tmp";
        const std::string shadercExe = std::filesystem::path{ request.shadercPath }.make_preferred().string();
        std::string command = QuotePath(shadercExe);
        command += " --type fragment";
        command += " --platform " + std::string{ RenderMaterialGraphShaderBackendPlatform(backend) };
        command += " --profile " + std::string{ RenderMaterialGraphShaderBackendProfile(backend) };
        command += " -f " + QuotePath(wrapperPath.generic_string());
        command += " -o " + QuotePath(binaryPath.generic_string());
        if (!request.varyingDefPath.empty()) {
            command += " --varyingdef " + QuotePath(request.varyingDefPath);
        }
        for (const std::string& includeDir : request.includeDirs) {
            command += " -i " + QuotePath(includeDir);
        }
        command += request.debug ? " -O 0 --debug" : " -O 3";

        std::filesystem::remove(binaryPath, error);
        const int exitCode = RunShaderCompilerHidden(command, errorPath);

        const bool produced = std::filesystem::exists(binaryPath, error) &&
            std::filesystem::file_size(binaryPath, error) > 0U;
        if (exitCode != 0 || !produced) {
            std::string log = TrimDiagnosticText(ReadTextFile(errorPath));
            std::filesystem::remove(errorPath, error);
            error.clear();
            if (log.empty()) {
                log = "shaderc exited with code " + std::to_string(exitCode) + " without producing a binary.";
            }
            AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                "Material graph shader cook failed for backend '" + std::string{ RenderMaterialGraphShaderBackendName(backend) } +
                "' (pass '" + request.pass + "'): " + log,
                request.pass,
                std::string{ RenderMaterialGraphShaderBackendName(backend) });
            continue;
        }
        std::filesystem::remove(errorPath, error);
        error.clear();

        {
            std::ofstream hashOut{ hashPath, std::ios::binary | std::ios::trunc };
            if (hashOut) {
                hashOut << cookKey;
            }
        }
        artifact.binaries.push_back(RenderMaterialGraphShaderBinary{
            .backend = backend,
            .binaryPath = binaryPath.generic_string(),
            .byteSize = std::filesystem::file_size(binaryPath, error),
            .cacheHit = false,
        });
    }

    if (artifact.binaries.empty()) {
        AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
            "Material graph shader cook produced no backend binaries.");
        return result;
    }

    // MAT-67/#54: when the graph drives vertex-domain outputs, cook the generated vertex shader too so the
    // scene program pairs it with the graph fragment shader and moves geometry / rewrites UVs before
    // rasterization. ShadowDepth uses the same generated vertex-domain contract for WPO/displacement.
    const bool hasVertexDomainOutput =
        shader.reflection.hasWorldPositionOffset ||
        shader.reflection.hasCustomizedUv0 ||
        shader.reflection.hasDisplacement;
    if (hasVertexDomainOutput) {
        artifact.hasVertexShader = true;
        artifact.vertexWrapperSource = BuildGraphVertexWrapperSource(shader);
        const std::filesystem::path vsWrapperPath = passRoot / "vs_graph.sc";
        {
            std::ofstream vsWrapperOut{ vsWrapperPath, std::ios::binary | std::ios::trunc };
            if (!vsWrapperOut) {
                AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                    "Material graph shader cook could not write the vertex wrapper to " + vsWrapperPath.generic_string() + ".");
                return result;
            }
            vsWrapperOut << artifact.vertexWrapperSource;
        }

        std::uint64_t vsCookKey = 1469598103934665603ULL;
        HashString64(vsCookKey, artifact.vertexWrapperSource);
        HashU64(vsCookKey, artifact.dependencyHash);
        HashU64(vsCookKey, artifact.materialTypeVersion);

        for (const RenderMaterialGraphShaderBackend backend : backends) {
            const std::filesystem::path backendDir = passRoot / std::string{ RenderMaterialGraphShaderBackendDirectory(backend) };
            std::filesystem::create_directories(backendDir, error);
            const std::filesystem::path vsBinaryPath = backendDir / "vs.bin";
            const std::filesystem::path vsHashPath = backendDir / "vs.bin.hash";

            const std::string cachedVsHash = TrimDiagnosticText(ReadTextFile(vsHashPath));
            const bool cached = !cachedVsHash.empty() &&
                cachedVsHash == std::to_string(vsCookKey) &&
                std::filesystem::exists(vsBinaryPath, error) &&
                std::filesystem::file_size(vsBinaryPath, error) > 0U;
            if (cached) {
                artifact.vertexBinaries.push_back(RenderMaterialGraphShaderBinary{
                    .backend = backend,
                    .binaryPath = vsBinaryPath.generic_string(),
                    .byteSize = std::filesystem::file_size(vsBinaryPath, error),
                    .cacheHit = true,
                });
                continue;
            }

            const std::filesystem::path vsErrorPath = backendDir / "vs.shaderc.tmp";
            const std::string shadercExe = std::filesystem::path{ request.shadercPath }.make_preferred().string();
            std::string command = QuotePath(shadercExe);
            command += " --type vertex";
            command += " --platform " + std::string{ RenderMaterialGraphShaderBackendPlatform(backend) };
            command += " --profile " + std::string{ RenderMaterialGraphShaderBackendProfile(backend) };
            command += " -f " + QuotePath(vsWrapperPath.generic_string());
            command += " -o " + QuotePath(vsBinaryPath.generic_string());
            if (!request.varyingDefPath.empty()) {
                command += " --varyingdef " + QuotePath(request.varyingDefPath);
            }
            for (const std::string& includeDir : request.includeDirs) {
                command += " -i " + QuotePath(includeDir);
            }
            command += request.debug ? " -O 0 --debug" : " -O 3";

            std::filesystem::remove(vsBinaryPath, error);
            const int exitCode = RunShaderCompilerHidden(command, vsErrorPath);

            const bool produced = std::filesystem::exists(vsBinaryPath, error) &&
                std::filesystem::file_size(vsBinaryPath, error) > 0U;
            if (exitCode != 0 || !produced) {
                std::string log = TrimDiagnosticText(ReadTextFile(vsErrorPath));
                std::filesystem::remove(vsErrorPath, error);
                error.clear();
                if (log.empty()) {
                    log = "shaderc exited with code " + std::to_string(exitCode) + " without producing a vertex binary.";
                }
                AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                    "Material graph vertex shader cook failed for backend '" + std::string{ RenderMaterialGraphShaderBackendName(backend) } +
                    "' (pass '" + request.pass + "'): " + log,
                    request.pass,
                    std::string{ RenderMaterialGraphShaderBackendName(backend) });
                return result;
            }
            std::filesystem::remove(vsErrorPath, error);
            error.clear();

            {
                std::ofstream vsHashOut{ vsHashPath, std::ios::binary | std::ios::trunc };
                if (vsHashOut) {
                    vsHashOut << vsCookKey;
                }
            }
            artifact.vertexBinaries.push_back(RenderMaterialGraphShaderBinary{
                .backend = backend,
                .binaryPath = vsBinaryPath.generic_string(),
                .byteSize = std::filesystem::file_size(vsBinaryPath, error),
                .cacheHit = false,
            });
        }

        if (artifact.vertexBinaries.empty()) {
            AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
            "Material graph shader cook produced no vertex binaries for a vertex-domain graph.");
            return result;
        }
    }

    result.artifact = std::move(artifact);
    return result;
}

RenderMaterialGraphShaderManifest BuildRenderMaterialGraphShaderManifest(
    std::span<const RenderMaterialGraphShaderArtifact> artifacts) {
    RenderMaterialGraphShaderManifest manifest{};
    for (const RenderMaterialGraphShaderArtifact& artifact : artifacts) {
        for (const RenderMaterialGraphShaderBinary& binary : artifact.binaries) {
            manifest.entries.push_back(RenderMaterialGraphShaderManifestEntry{
                .graphSourceHash = artifact.graphSourceHash,
                .variantKey = artifact.variantKey,
                .wrapperHash = artifact.wrapperHash,
                .reflectionHash = artifact.reflectionHash,
                .dependencyHash = artifact.dependencyHash,
                .artifactHash = artifact.artifactHash,
                .materialTypeVersion = artifact.materialTypeVersion,
                .pass = artifact.pass,
                .backend = binary.backend,
                .binaryPath = binary.binaryPath,
                .graphGenerated = artifact.graphGenerated,
            });
        }
    }
    std::sort(manifest.entries.begin(), manifest.entries.end(), [](const RenderMaterialGraphShaderManifestEntry& lhs, const RenderMaterialGraphShaderManifestEntry& rhs) {
        if (lhs.graphSourceHash != rhs.graphSourceHash) {
            return lhs.graphSourceHash < rhs.graphSourceHash;
        }
        if (lhs.variantKey != rhs.variantKey) {
            return lhs.variantKey < rhs.variantKey;
        }
        if (lhs.pass != rhs.pass) {
            return lhs.pass < rhs.pass;
        }
        return static_cast<std::uint8_t>(lhs.backend) < static_cast<std::uint8_t>(rhs.backend);
    });

    std::uint64_t manifestHash = 1469598103934665603ULL;
    HashU64(manifestHash, manifest.entries.size());
    for (const RenderMaterialGraphShaderManifestEntry& entry : manifest.entries) {
        HashU64(manifestHash, entry.graphSourceHash);
        HashU64(manifestHash, entry.variantKey);
        HashU64(manifestHash, entry.wrapperHash);
        HashU64(manifestHash, entry.reflectionHash);
        HashU64(manifestHash, entry.dependencyHash);
        HashU64(manifestHash, entry.artifactHash);
        HashU64(manifestHash, entry.materialTypeVersion);
        HashString64(manifestHash, entry.pass);
        HashU64(manifestHash, static_cast<std::uint64_t>(entry.backend));
        HashString64(manifestHash, entry.binaryPath);
        HashU64(manifestHash, entry.graphGenerated ? 1U : 0U);
    }
    manifest.manifestHash = manifestHash;
    return manifest;
}

std::vector<RenderMaterialGraphDiagnostic> ValidateRenderMaterialGraphShaderManifest(
    const RenderMaterialGraphShaderManifest& manifest) {
    std::vector<RenderMaterialGraphDiagnostic> diagnostics;
    std::error_code error;
    for (const RenderMaterialGraphShaderManifestEntry& entry : manifest.entries) {
        if (!entry.graphGenerated) {
            continue;
        }
        const std::string backendName{ RenderMaterialGraphShaderBackendName(entry.backend) };
        if (entry.binaryPath.empty()) {
            AddArtifactDiagnostic(diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                "Graph shader manifest entry (pass '" + entry.pass + "', backend '" + backendName + "') has no binary path.",
                entry.pass, backendName);
            continue;
        }
        const std::filesystem::path binaryPath{ entry.binaryPath };
        if (!std::filesystem::exists(binaryPath, error) || std::filesystem::file_size(binaryPath, error) == 0U) {
            AddArtifactDiagnostic(diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                "Graph shader manifest references a missing or empty binary at " + entry.binaryPath + ".",
                entry.pass, backendName);
        }
    }
    return diagnostics;
}

void WriteRenderMaterialGraphShaderManifest(std::ostream& output, const RenderMaterialGraphShaderManifest& manifest) {
    output << "graphShaderManifest 2\n";
    output << "manifestHash " << manifest.manifestHash << '\n';
    for (const RenderMaterialGraphShaderManifestEntry& entry : manifest.entries) {
        output << "graphArtifact "
            << entry.graphSourceHash << ' '
            << entry.variantKey << ' '
            << RenderMaterialGraphShaderBackendName(entry.backend) << ' '
            << entry.wrapperHash << ' '
            << entry.reflectionHash << ' '
            << entry.dependencyHash << ' '
            << entry.artifactHash << ' '
            << entry.materialTypeVersion << ' '
            << (entry.graphGenerated ? 1U : 0U) << ' '
            << (entry.pass.empty() ? "_" : entry.pass) << ' '
            << entry.binaryPath << '\n';
    }
}

RenderMaterialGraphShaderManifest ParseRenderMaterialGraphShaderManifest(std::istream& input) {
    RenderMaterialGraphShaderManifest manifest{};
    std::uint32_t version = 1U;
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream stream{ line };
        std::string token;
        stream >> token;
        if (token == "graphShaderManifest") {
            stream >> version;
            continue;
        }
        if (token == "manifestHash") {
            stream >> manifest.manifestHash;
            continue;
        }
        if (token != "graphArtifact") {
            continue;
        }
        RenderMaterialGraphShaderManifestEntry entry{};
        std::string backendName;
        std::string pass;
        std::uint32_t graphGenerated = 1U;
        stream >> entry.graphSourceHash;
        if (version >= 2U) {
            stream >> entry.variantKey;
        }
        stream >> backendName >> entry.wrapperHash >> entry.reflectionHash >>
            entry.dependencyHash >> entry.artifactHash >> entry.materialTypeVersion >> graphGenerated >> pass;
        entry.backend = ParseRenderMaterialGraphShaderBackend(backendName).value_or(RenderMaterialGraphShaderBackend::Spirv);
        entry.graphGenerated = graphGenerated != 0U;
        entry.pass = pass == "_" ? std::string{} : pass;
        std::string binaryPath;
        std::getline(stream, binaryPath);
        if (!binaryPath.empty() && binaryPath.front() == ' ') {
            binaryPath.erase(binaryPath.begin());
        }
        entry.binaryPath = binaryPath;
        manifest.entries.push_back(std::move(entry));
    }
    return manifest;
}

} // namespace kb::render
