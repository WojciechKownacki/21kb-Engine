#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace kb::render {
namespace {

void HashString64(std::uint64_t& hash, std::string_view value) noexcept {
    for (const char ch : value) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= 1099511628211ULL;
    }
}

void HashU64(std::uint64_t& hash, std::uint64_t value) noexcept {
    HashString64(hash, std::to_string(value));
}

[[nodiscard]] std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    if (!input) {
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

[[nodiscard]] std::string QuotePath(const std::string& value) {
    return "\"" + value + "\"";
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
    std::string pass = {},
    std::string backend = {}) {
    diagnostics.push_back(RenderMaterialGraphDiagnostic{
        .severity = severity,
        .kind = RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
        .pass = std::move(pass),
        .backend = std::move(backend),
        .message = std::move(message),
    });
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
    std::string_view pass,
    RenderMaterialGraphShaderBackend backend) const noexcept {
    for (const RenderMaterialGraphShaderManifestEntry& entry : entries) {
        if (entry.graphSourceHash == graphSourceHash && entry.pass == pass && entry.backend == backend) {
            return &entry;
        }
    }
    return nullptr;
}

std::string BuildGraphFragmentWrapperSource(
    const RenderMaterialGraphShaderSource& shader,
    std::string_view pass) {
    const bool shadowPass = pass == "ShadowDepth";

    std::string wrapper;
    wrapper += "$input v_normal, v_color0, v_texcoord0, v_worldPos, v_shadowPos, v_shadowFlags, v_tangent, v_bitangent, v_objectLocalPos, v_objectWorldPos\n\n";
    wrapper += "#include <bgfx_shader.sh>\n";
    if (!shadowPass) {
        wrapper += "#include \"pbr_graph_forward.sh\"\n";
    }
    // MAT-72 frame time constants (x=time seconds, y=deltaTime, z=frameIndex). Bound per frame by
    // SceneMeshPassResources so graph Time/animation nodes read real engine time.
    wrapper += "uniform vec4 u_time;\n";
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
    // MAT-75 screen-space coordinate (0..1) from the fragment position and bgfx viewport rect.
    wrapper += "    ctx.screenPosition = gl_FragCoord.xy / max(u_viewRect.zw, vec2(1.0, 1.0));\n";
    // MAT-76 object-space inputs interpolated from the vertex shader.
    wrapper += "    ctx.localPosition = v_objectLocalPos.xyz;\n";
    wrapper += "    ctx.objectPosition = v_objectWorldPos.xyz;\n";
    // MAT-77 per-instance scalars carried in the free .w lanes of the object-space varyings.
    wrapper += "    ctx.perInstanceRandom = v_objectLocalPos.w;\n";
    wrapper += "    ctx.objectRadius = v_objectWorldPos.w;\n";
    // MAT-46: world-space view/light inputs. The shadow pass has no lighting uniforms, so it uses safe
    // placeholders; only the forward pass reads the real camera/light/viewport state.
    wrapper += "    ctx.viewSize = u_viewRect.zw;\n";
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
        // MAT-38 Masked: clip fragments whose alpha is below the clip threshold so the background shows
        // through (binary opacity). The transparent modes keep every fragment and blend at the ROP stage.
        if (shader.reflection.blendMode == RenderMaterialGraphBlendMode::Masked) {
            wrapper += "    if (surface.alpha < surface.alphaClipThreshold)\n    {\n        discard;\n    }\n";
        }
        if (shader.reflection.shadingModel == RenderMaterialShadingModel::Unlit) {
            // MAT-37 Unlit: the surface emissive plus base color go straight to the framebuffer with no lighting.
            wrapper += "    gl_FragColor = vec4(surface.baseColor.rgb + surface.emissive, surface.alpha);\n";
        } else {
            // MAT-37 DefaultLit: the metallic-roughness forward PBR path.
            wrapper += "    vec3 worldNormal = normalize(v_tangent * surface.normal.x + v_bitangent * surface.normal.y + v_normal * surface.normal.z);\n";
            wrapper += "    float metallic = clamp(surface.metallic, 0.0, 1.0);\n";
            wrapper += "    float roughness = clamp(surface.roughness, 0.04, 1.0);\n";
            wrapper += "    float occlusion = clamp(surface.occlusion, 0.0, 1.0);\n";
            wrapper += "    vec3 lighting = KbEvaluateForwardLighting(worldNormal, v_worldPos, surface.baseColor.rgb, metallic, roughness, occlusion);\n";
            wrapper += "    gl_FragColor = vec4(lighting + surface.emissive, surface.alpha);\n";
        }
    }
    wrapper += "}\n";
    return wrapper;
}

std::uint64_t ComputeRenderMaterialGraphReflectionHash(const RenderMaterialGraphReflection& reflection) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const RenderMaterialGraphReflectionUniform& uniform : reflection.uniforms) {
        HashString64(hash, uniform.name);
        HashString64(hash, uniform.stableId);
        HashU64(hash, static_cast<std::uint64_t>(uniform.kind));
    }
    for (const RenderMaterialGraphReflectionTexture& texture : reflection.textures) {
        HashString64(hash, texture.samplerName);
        HashString64(hash, texture.stableId);
        HashU64(hash, texture.slot);
        HashU64(hash, static_cast<std::uint64_t>(texture.colorSpace));
    }
    for (const std::string& varying : reflection.requiredVaryings) {
        HashString64(hash, varying);
    }
    // MAT-37: the shading model selects the fragment wrapper lighting branch, so it is part of program identity.
    HashU64(hash, static_cast<std::uint64_t>(reflection.shadingModel));
    // MAT-38: the blend mode changes the wrapper (masked clip) and the cooked pass, so it is part of identity.
    HashU64(hash, static_cast<std::uint64_t>(reflection.blendMode));
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
    std::uint64_t dependencyHash = 1469598103934665603ULL;
    std::vector<std::string> dependencyFiles = request.dependencyFiles;
    if (!request.varyingDefPath.empty()) {
        dependencyFiles.push_back(request.varyingDefPath);
    }
    std::sort(dependencyFiles.begin(), dependencyFiles.end());
    for (const std::string& dependencyFile : dependencyFiles) {
        std::uint64_t contentHash = 1469598103934665603ULL;
        HashString64(contentHash, ReadTextFile(std::filesystem::path{ dependencyFile }));
        const std::string name = std::filesystem::path{ dependencyFile }.filename().generic_string();
        HashString64(dependencyHash, name);
        HashU64(dependencyHash, contentHash);
        artifact.dependencies.push_back(RenderMaterialGraphArtifactDependency{ .name = name, .contentHash = contentHash });
    }
    artifact.dependencyHash = dependencyHash;

    std::uint64_t artifactHash = 1469598103934665603ULL;
    HashU64(artifactHash, artifact.graphSourceHash);
    HashU64(artifactHash, artifact.wrapperHash);
    HashU64(artifactHash, artifact.dependencyHash);
    HashU64(artifactHash, artifact.reflectionHash);
    HashU64(artifactHash, artifact.materialTypeVersion);
    artifact.artifactHash = artifactHash;

    // The per-binary cache key combines the wrapper, its dependencies and the material type version,
    // so any of those changing forces a recompile rather than serving a stale binary.
    std::uint64_t cookKey = 1469598103934665603ULL;
    HashU64(cookKey, artifact.wrapperHash);
    HashU64(cookKey, artifact.dependencyHash);
    HashU64(cookKey, artifact.materialTypeVersion);

    std::error_code error;
    const std::filesystem::path passRoot = std::filesystem::path{ request.cacheRoot } /
        ("graph_" + std::to_string(shader.sourceHash)) / request.pass;
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

        const std::filesystem::path errorPath = backendDir / "fs.shaderc.log";
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
        command += " > " + QuotePath(errorPath.generic_string()) + " 2>&1";

        std::filesystem::remove(binaryPath, error);
        const std::string shellCommand = "\"" + command + "\"";
        const int exitCode = std::system(shellCommand.c_str());

        const bool produced = std::filesystem::exists(binaryPath, error) &&
            std::filesystem::file_size(binaryPath, error) > 0U;
        if (exitCode != 0 || !produced) {
            std::string log = TrimDiagnosticText(ReadTextFile(errorPath));
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
        if (lhs.pass != rhs.pass) {
            return lhs.pass < rhs.pass;
        }
        return static_cast<std::uint8_t>(lhs.backend) < static_cast<std::uint8_t>(rhs.backend);
    });

    std::uint64_t manifestHash = 1469598103934665603ULL;
    for (const RenderMaterialGraphShaderManifestEntry& entry : manifest.entries) {
        HashU64(manifestHash, entry.graphSourceHash);
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
    output << "graphShaderManifest 1\n";
    output << "manifestHash " << manifest.manifestHash << '\n';
    for (const RenderMaterialGraphShaderManifestEntry& entry : manifest.entries) {
        output << "graphArtifact "
            << entry.graphSourceHash << ' '
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
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream stream{ line };
        std::string token;
        stream >> token;
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
        stream >> entry.graphSourceHash >> backendName >> entry.wrapperHash >> entry.reflectionHash >>
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
