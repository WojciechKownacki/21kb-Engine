$input v_texcoord0

#include <bgfx_shader.sh>
#include "gbuffer_contract.sh"

SAMPLER2D(s_gbufferAlbedo, 0);
SAMPLER2D(s_gbufferNormal, 1);
SAMPLER2D(s_gbufferMaterial, 2);
SAMPLER2D(s_gbufferSurface, 3);
SAMPLER2D(s_gbufferDepth, 4);
SAMPLER2D(s_deferredShadowMap, 5);
uniform vec4 u_deferredLightDirKind[32];
uniform vec4 u_deferredLightPositionRange[32];
uniform vec4 u_deferredLightColorIntensity[32];
uniform vec4 u_deferredLightSpot[32];
uniform vec4 u_deferredLightParams;
uniform vec4 u_deferredAmbientColor;
uniform vec4 u_deferredEnvironmentZenith;
uniform vec4 u_deferredEnvironmentGround;
uniform vec4 u_deferredEnvironmentParams;
uniform vec4 u_deferredCameraPosition;
uniform mat4 u_deferredInverseViewProjection;
uniform vec4 u_deferredDepthParams;
uniform mat4 u_deferredShadowViewProj;
uniform vec4 u_deferredShadowParams;

vec3 FresnelSchlick(float cosTheta, vec3 f0)
{
    return f0 + (vec3(1.0, 1.0, 1.0) - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 f0, float roughness)
{
    vec3 roughF0 = max(vec3_splat(1.0 - roughness), f0);
    return f0 + (roughF0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGgx(float nDotH, float roughness)
{
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denom = nDotH * nDotH * (alpha2 - 1.0) + 1.0;
    return alpha2 / max(3.14159265 * denom * denom, 0.0001);
}

float GeometrySchlickGgx(float nDotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) * 0.125;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.0001);
}

float DiffuseBurley(float nDotV, float nDotL, float lDotH, float roughness)
{
    float energyBias = mix(0.0, 0.5, roughness);
    float energyFactor = mix(1.0, 1.0 / 1.51, roughness);
    float fd90 = energyBias + 2.0 * lDotH * lDotH * roughness;
    float lightScatter = 1.0 + (fd90 - 1.0) * pow(clamp(1.0 - nDotL, 0.0, 1.0), 5.0);
    float viewScatter = 1.0 + (fd90 - 1.0) * pow(clamp(1.0 - nDotV, 0.0, 1.0), 5.0);
    return lightScatter * viewScatter * energyFactor;
}

vec3 ReconstructWorldPosition(vec2 uv, float depth)
{
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float clipDepth = u_deferredDepthParams.x > 0.5 ? depth * 2.0 - 1.0 : depth;
    vec4 world = mul(u_deferredInverseViewProjection, vec4(ndc, clipDepth, 1.0));
    world.xyz /= max(world.w, 0.000001);
    return world.xyz;
}

vec3 EnvironmentColor(vec3 direction)
{
    float hemisphere = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 constantColor = u_deferredAmbientColor.rgb;
    vec3 hemisphereColor = mix(u_deferredEnvironmentGround.rgb, u_deferredEnvironmentZenith.rgb, hemisphere);
    return u_deferredEnvironmentParams.x < 1.5 ? constantColor : hemisphereColor;
}

vec3 EvaluateEnvironment(vec3 normal, vec3 viewDir, vec3 albedo, float metallic, float roughness, float specular, float occlusion)
{
    if (u_deferredEnvironmentParams.x < 0.5) {
        return vec3(0.0, 0.0, 0.0);
    }

    float nDotV = max(dot(normal, viewDir), 0.0);
    vec3 f0 = mix(vec3_splat(0.08 * specular), albedo, metallic);
    vec3 fresnel = FresnelSchlickRoughness(nDotV, f0, roughness);
    vec3 diffuseEnv = EnvironmentColor(normal) * albedo * (vec3(1.0, 1.0, 1.0) - fresnel) * (1.0 - metallic) * occlusion * u_deferredEnvironmentParams.y;
    vec3 reflectionDir = reflect(-viewDir, normal);
    float specularEnergy = mix(1.0, 0.18, roughness * roughness);
    vec3 specularEnv = EnvironmentColor(reflectionDir) * fresnel * specularEnergy * u_deferredEnvironmentParams.z;
    return diffuseEnv + specularEnv;
}

vec3 EvaluateSceneLight(int lightIndex, vec3 normal, vec3 viewDir, vec3 worldPos, vec3 albedo, float metallic, float roughness, float specular, float occlusion)
{
    vec4 dirKind = u_deferredLightDirKind[lightIndex];
    vec4 positionRange = u_deferredLightPositionRange[lightIndex];
    vec4 colorIntensity = u_deferredLightColorIntensity[lightIndex];
    vec4 spot = u_deferredLightSpot[lightIndex];

    vec3 lightVector = vec3(0.0, 1.0, 0.0);
    float attenuation = 1.0;
    if (dirKind.w < 0.5) {
        lightVector = normalize(-dirKind.xyz);
    } else {
        vec3 toLight = positionRange.xyz - worldPos;
        float distanceToLight = length(toLight);
        lightVector = distanceToLight > 0.0001 ? toLight / distanceToLight : vec3(0.0, 1.0, 0.0);
        float range = max(positionRange.w, 0.0001);
        float rangeAttenuation = clamp(1.0 - distanceToLight / range, 0.0, 1.0);
        attenuation = rangeAttenuation * rangeAttenuation;
        if (dirKind.w > 1.5) {
            float coneCos = dot(normalize(dirKind.xyz), normalize(-lightVector));
            float coneWidth = max(spot.x - spot.y, 0.001);
            float coneAttenuation = clamp((coneCos - spot.y) / coneWidth, 0.0, 1.0);
            attenuation *= coneAttenuation * coneAttenuation;
        }
    }

    float nDotL = max(dot(normal, lightVector), 0.0);
    if (nDotL <= 0.0) {
        return vec3(0.0, 0.0, 0.0);
    }

    vec3 halfVector = normalize(viewDir + lightVector);
    float nDotV = max(dot(normal, viewDir), 0.0001);
    float nDotH = max(dot(normal, halfVector), 0.0);
    float hDotV = max(dot(halfVector, viewDir), 0.0);
    float lDotH = max(dot(lightVector, halfVector), 0.0);
    vec3 f0 = mix(vec3_splat(0.08 * specular), albedo, metallic);
    vec3 fresnel = FresnelSchlick(hDotV, f0);
    float distribution = DistributionGgx(nDotH, roughness);
    float geometry = GeometrySchlickGgx(nDotV, roughness) * GeometrySchlickGgx(nDotL, roughness);
    vec3 specularTerm = (distribution * geometry * fresnel) / max(4.0 * nDotV * nDotL, 0.0001);
    vec3 diffuse = (vec3(1.0, 1.0, 1.0) - fresnel) * (1.0 - metallic) * albedo * (0.31830989 * DiffuseBurley(nDotV, nDotL, lDotH, roughness)) * occlusion;
    vec3 radiance = colorIntensity.rgb * (colorIntensity.a * attenuation);
    return (diffuse + specularTerm) * radiance * nDotL;
}

float SampleShadowVisibility(vec3 shadowCoord)
{
    float biasedDepth = shadowCoord.z + u_deferredShadowParams.x;
    float storedDepth = texture2D(s_deferredShadowMap, shadowCoord.xy).x;
    float hardShadow = biasedDepth < storedDepth ? 1.0 : 0.0;
    float texelSize = max(u_deferredShadowParams.z, 0.000001);
    float shadowSamples =
        (biasedDepth < texture2D(s_deferredShadowMap, shadowCoord.xy + vec2(-texelSize, -texelSize)).x ? 1.0 : 0.0) +
        (biasedDepth < texture2D(s_deferredShadowMap, shadowCoord.xy + vec2(0.0, -texelSize)).x ? 1.0 : 0.0) +
        (biasedDepth < texture2D(s_deferredShadowMap, shadowCoord.xy + vec2(texelSize, -texelSize)).x ? 1.0 : 0.0) +
        (biasedDepth < texture2D(s_deferredShadowMap, shadowCoord.xy + vec2(-texelSize, 0.0)).x ? 1.0 : 0.0) +
        (biasedDepth < texture2D(s_deferredShadowMap, shadowCoord.xy).x ? 1.0 : 0.0) +
        (biasedDepth < texture2D(s_deferredShadowMap, shadowCoord.xy + vec2(texelSize, 0.0)).x ? 1.0 : 0.0) +
        (biasedDepth < texture2D(s_deferredShadowMap, shadowCoord.xy + vec2(-texelSize, texelSize)).x ? 1.0 : 0.0) +
        (biasedDepth < texture2D(s_deferredShadowMap, shadowCoord.xy + vec2(0.0, texelSize)).x ? 1.0 : 0.0) +
        (biasedDepth < texture2D(s_deferredShadowMap, shadowCoord.xy + vec2(texelSize, texelSize)).x ? 1.0 : 0.0);
    float inShadow = shadowSamples * 0.11111111;
    float selectedShadow = u_deferredShadowParams.w < 2.0 ? hardShadow : inShadow;
    return mix(1.0, 1.0 - u_deferredShadowParams.y, selectedShadow);
}

void main()
{
    vec4 albedo = texture2D(s_gbufferAlbedo, v_texcoord0);
    float depth = texture2D(s_gbufferDepth, v_texcoord0).x;
    if (depth <= 0.000001) {
        discard;
    }
    vec3 normal = normalize(texture2D(s_gbufferNormal, v_texcoord0).xyz * 2.0 - 1.0);
    vec4 material = texture2D(s_gbufferMaterial, v_texcoord0);
    vec4 surface = texture2D(s_gbufferSurface, v_texcoord0);
    float metallic = clamp(material.x, 0.0, 1.0);
    float roughness = clamp(material.y, 0.04, 1.0);
    float occlusion = clamp(material.z, 0.0, 1.0);
    float shadingModel = KbDecodeGBufferShadingModel(material.w);
    float specular = clamp(surface.w, 0.0, 1.0);

    if (abs(shadingModel - KB_GBUFFER_SHADING_MODEL_UNLIT) < 0.5) {
        gl_FragColor = vec4(albedo.rgb + surface.rgb, 1.0);
        return;
    }

    vec3 worldPos = ReconstructWorldPosition(v_texcoord0, depth);
    vec3 viewDir = normalize(u_deferredCameraPosition.xyz - worldPos);

    vec4 shadowClip = mul(u_deferredShadowViewProj, vec4(worldPos, 1.0));
    vec3 shadowCoord = shadowClip.xyz / max(shadowClip.w, 0.0001);
    float shadowVisible = 1.0;
    if (u_deferredShadowParams.w > 0.5 &&
        shadowCoord.x >= 0.0 && shadowCoord.x <= 1.0 &&
        shadowCoord.y >= 0.0 && shadowCoord.y <= 1.0 &&
        shadowCoord.z >= 0.0 && shadowCoord.z <= 1.0) {
        shadowVisible = SampleShadowVisibility(shadowCoord);
    }

    vec3 lighting = EvaluateEnvironment(normal, viewDir, albedo.rgb, metallic, roughness, specular, occlusion);

    for (int lightIndex = 0; lightIndex < 32; ++lightIndex) {
        if (float(lightIndex) < u_deferredLightParams.x) {
            vec3 directLight = EvaluateSceneLight(lightIndex, normal, viewDir, worldPos, albedo.rgb, metallic, roughness, specular, occlusion);
            lighting += lightIndex == 0 ? directLight * shadowVisible : directLight;
        }
    }

    gl_FragColor = vec4(lighting + surface.rgb, 1.0);
}
