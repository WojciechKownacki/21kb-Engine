$input v_texcoord0

#include <bgfx_shader.sh>
#define KB_LIGHT_GRID_DEFERRED 1
#include "light_grid.sh"
#include "shadow_cascades.sh"
#include "point_shadow.sh"
#include "gbuffer_contract.sh"
#include "gbuffer_position.sh"

SAMPLER2D(s_gbufferAlbedo, 0);
SAMPLER2D(s_gbufferNormal, 1);
SAMPLER2D(s_gbufferMaterial, 2);
SAMPLER2D(s_gbufferSurface, 3);
SAMPLER2D(s_gbufferDepth, 4);
SAMPLER2D(s_deferredShadowMap, 5);
SAMPLER2D(s_deferredBackdropEnvironment, 6);
uniform vec4 u_deferredLightDirKind[32];
uniform vec4 u_deferredLightPositionRange[32];
uniform vec4 u_deferredLightColorIntensity[32];
uniform vec4 u_deferredLightSpot[32];
uniform vec4 u_deferredLightAreaRight[32];
uniform vec4 u_deferredLightParams;
uniform vec4 u_deferredAmbientColor;
uniform vec4 u_deferredEnvironmentZenith;
uniform vec4 u_deferredEnvironmentGround;
uniform vec4 u_deferredEnvironmentParams;
uniform mat4 u_deferredShadowViewProj;
uniform vec4 u_deferredShadowParams;
// x: 1 for gradient/procedural, 2 for an equirectangular environment map;
// y: normalized horizon offset, z: vertical blend exponent, w: procedural variant flag.
uniform vec4 u_deferredBackdropHorizon;
uniform vec4 u_deferredBackdropZenith;
uniform vec4 u_deferredBackdropParams;

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
    vec4 dirKind, positionRange, colorIntensity, spot, areaRight;
    if (u_sceneLightGridDimensions.w > 0.5) {
        float base = float(lightIndex) * 5.0;
        dirKind = KbLightGridTexel(base);
        positionRange = KbLightGridTexel(base + 1.0);
        colorIntensity = KbLightGridTexel(base + 2.0);
        spot = KbLightGridTexel(base + 3.0);
        areaRight = KbLightGridTexel(base + 4.0);
    } else {
        dirKind = u_deferredLightDirKind[lightIndex];
        positionRange = u_deferredLightPositionRange[lightIndex];
        colorIntensity = u_deferredLightColorIntensity[lightIndex];
        spot = u_deferredLightSpot[lightIndex];
        areaRight = u_deferredLightAreaRight[lightIndex];
    }

    vec3 lightVector = vec3(0.0, 1.0, 0.0);
    float attenuation = 1.0;
    if (dirKind.w < 0.5) {
        lightVector = normalize(-dirKind.xyz);
    } else {
        vec3 emitterPosition = KbSurfaceEmitterSamplePosition(positionRange.xyz, dirKind.xyz, areaRight.xyz, dirKind.w, spot.zw, worldPos);
        vec3 toLight = emitterPosition - worldPos;
        float distanceToLight = length(toLight);
        lightVector = distanceToLight > 0.0001 ? toLight / distanceToLight : vec3(0.0, 1.0, 0.0);
        float range = max(positionRange.w, 0.0001);
        float rangeAttenuation = clamp(1.0 - distanceToLight / range, 0.0, 1.0);
        attenuation = rangeAttenuation * rangeAttenuation;
        if (dirKind.w > 1.5 && dirKind.w < 2.5) {
            float coneCos = dot(normalize(dirKind.xyz), normalize(-lightVector));
            float coneWidth = max(spot.x - spot.y, 0.001);
            float coneAttenuation = clamp((coneCos - spot.y) / coneWidth, 0.0, 1.0);
            attenuation *= coneAttenuation * coneAttenuation;
        }
        if (dirKind.w > 2.5) {
            attenuation *= max(dot(normalize(dirKind.xyz), normalize(-lightVector)), 0.0);
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

#include "ssgi.sh"

void main()
{
    vec4 albedo = texture2D(s_gbufferAlbedo, v_texcoord0);
    vec4 encodedNormal = texture2D(s_gbufferNormal, v_texcoord0);
    float depth = texture2D(s_gbufferDepth, v_texcoord0).x;
    // The normal attachment reserves alpha 0 for its clear value, while every
    // opaque G-buffer write stores 1. This is independent from both depth
    // conventions and authored material opacity.
    bool background = encodedNormal.a < 0.5;
    if (background) {
        if (u_deferredBackdropParams.x > 1.5) {
            vec3 farWorld = ReconstructWorldPosition(v_texcoord0, depth);
            vec3 direction = normalize(farWorld - u_deferredCameraPosition.xyz);
            // bgfx's HLSL profile does not expose the GLSL atan(y, x) overload.
            // Reconstruct its quadrant explicitly so this equirectangular mapping
            // remains backend-independent.
            float longitude = atan(direction.z / max(abs(direction.x), 0.0001));
            if (direction.x < 0.0) {
                longitude += direction.z >= 0.0 ? 3.14159265 : -3.14159265;
            }
            vec2 environmentUv = vec2(longitude * 0.15915494 + 0.5, acos(clamp(direction.y, -1.0, 1.0)) * 0.31830989);
            gl_FragColor = vec4(texture2D(s_deferredBackdropEnvironment, environmentUv).rgb, 1.0);
            return;
        }
        if (u_deferredBackdropParams.x > 0.5) {
            float vertical = clamp((1.0 - v_texcoord0.y) + u_deferredBackdropParams.y, 0.0, 1.0);
            float blend = pow(vertical, max(u_deferredBackdropParams.z, 0.0001));
            if (u_deferredBackdropParams.w > 0.5) {
                blend = blend * blend * (3.0 - 2.0 * blend);
            }
            gl_FragColor = vec4(mix(u_deferredBackdropHorizon.rgb, u_deferredBackdropZenith.rgb, blend), 1.0);
            return;
        }
        discard;
    }
    vec3 normal = normalize(encodedNormal.xyz * 2.0 - 1.0);
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

    float shadowVisible = 1.0;
    if (u_deferredShadowParams.w > 0.5) {
        vec4 shadowCoord = KbResolveShadowCascade(worldPos);
        if (shadowCoord.w > 0.5) {
            shadowVisible = SampleShadowVisibility(shadowCoord.xyz);
        }
    }

    vec3 lighting = EvaluateEnvironment(normal, viewDir, albedo.rgb, metallic, roughness, specular, occlusion);

    vec2 lightList = KbLightGridList(worldPos, u_deferredLightParams.x);
    for (int entry = 0; entry < int(lightList.y); ++entry) {
        {
            int lightIndex = KbLightGridIndex(lightList, entry);
            vec3 directLight = EvaluateSceneLight(lightIndex, normal, viewDir, worldPos, albedo.rgb, metallic, roughness, specular, occlusion);
            lighting += directLight * (lightIndex == 0 ? shadowVisible : 1.0) * KbPointShadowFactor(lightIndex, worldPos);
        }
    }

    if (u_giParams.x > 0.0) {
        lighting += KbGiAccumulated(worldPos).rgb * albedo.rgb * ((1.0 - metallic) * occlusion * u_giParams.x);
    }

    gl_FragColor = vec4(lighting + surface.rgb, 1.0);
}
