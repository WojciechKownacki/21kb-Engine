$input v_normal, v_color0, v_texcoord0, v_worldPos, v_shadowPos, v_shadowFlags, v_tangent, v_bitangent, v_objectLocalPos, v_objectWorldPos, v_objectOrientation, v_preSkinnedNormal

#include <bgfx_shader.sh>
#include "light_grid.sh"
#include "shadow_cascades.sh"
#include "point_shadow.sh"

SAMPLER2D(s_albedo, 0);
SAMPLER2D(s_normal, 1);
SAMPLER2D(s_metallicRoughness, 2);
SAMPLER2D(s_occlusion, 3);
SAMPLER2D(s_emissive, 4);
SAMPLER2D(s_shadowMap, 5);
SAMPLER2D(s_terrainLayerWeights, 15);
SAMPLERCUBE(s_skyReflection, 12);
uniform vec4 u_materialParams;
uniform vec4 u_materialEmissive;
uniform vec4 u_materialFlags;
uniform vec4 u_materialUvTransform;
uniform vec4 u_cameraPosition;
uniform vec4 u_lightDirKind[32];
uniform vec4 u_lightPositionRange[32];
uniform vec4 u_lightColorIntensity[32];
uniform vec4 u_lightSpot[32];
uniform vec4 u_lightAreaRight[32];
uniform vec4 u_lightParams;
uniform vec4 u_ambientColor;
uniform vec4 u_environmentZenith;
uniform vec4 u_environmentGround;
uniform vec4 u_environmentParams;
#include "sky_reflection.sh"
uniform vec4 u_shadowParams;
uniform vec4 u_terrainLayerParams;

float TerrainLayerOpacity(vec2 terrainUv)
{
    if (u_terrainLayerParams.y < 0.5 || u_terrainLayerParams.x < 0.5) {
        return 1.0;
    }
    vec4 weights = texture2D(s_terrainLayerWeights, terrainUv);
    if (u_terrainLayerParams.x < 1.5) {
        return weights.y / max(weights.x + weights.y, 0.0001);
    }
    if (u_terrainLayerParams.x < 2.5) {
        return weights.z / max(weights.x + weights.y + weights.z, 0.0001);
    }
    return weights.w / max(weights.x + weights.y + weights.z + weights.w, 0.0001);
}

float DistributionGgx(float nDotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * denom * denom, 0.0001);
}

float GeometrySchlickGgx(float nDotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.0001);
}

vec3 FresnelSchlick(float cosTheta, vec3 f0)
{
    return f0 + (vec3(1.0, 1.0, 1.0) - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 f0, float roughness)
{
    vec3 roughF0 = max(vec3_splat(1.0 - roughness), f0);
    return f0 + (roughF0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DiffuseBurley(float nDotV, float nDotL, float lDotH, float roughness)
{
    // Disney diffuse shape without energy renormalisation, so a surface lit straight on
    // returns its albedo as Lambert diffuse does (the factor darkened
    // every light by about a quarter at common roughness).
    float energyBias = mix(0.0, 0.5, roughness);
    float fd90 = energyBias + 2.0 * lDotH * lDotH * roughness;
    float lightScatter = 1.0 + (fd90 - 1.0) * pow(clamp(1.0 - nDotL, 0.0, 1.0), 5.0);
    float viewScatter = 1.0 + (fd90 - 1.0) * pow(clamp(1.0 - nDotV, 0.0, 1.0), 5.0);
    return lightScatter * viewScatter;
}

// Material ambient occlusion darkens only the light from the surroundings (fs_deferred_lighting.sc).
vec3 EvaluateSceneLight(int lightIndex, vec3 normal, vec3 viewDir, vec3 worldPos, vec3 albedo, float metallic, float roughness)
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
        dirKind = u_lightDirKind[lightIndex];
        positionRange = u_lightPositionRange[lightIndex];
        colorIntensity = u_lightColorIntensity[lightIndex];
        spot = u_lightSpot[lightIndex];
        areaRight = u_lightAreaRight[lightIndex];
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
        // Inverse square, windowed to zero at the light's range.
        float rangeRatio = distanceToLight / range;
        float rangeWindow = clamp(1.0 - rangeRatio * rangeRatio * rangeRatio * rangeRatio, 0.0, 1.0);
        attenuation = rangeWindow * rangeWindow / max(distanceToLight * distanceToLight, 0.0001);
        if (dirKind.w > 1.5 && dirKind.w < 2.5) {
            float coneCos = dot(normalize(dirKind.xyz), normalize(-lightVector));
            float coneWidth = max(spot.x - spot.y, 0.001);
            float coneAttenuation = clamp((coneCos - spot.y) / coneWidth, 0.0, 1.0);
            attenuation *= coneAttenuation * coneAttenuation;
        }
        // Rectangle and disc emit from their front face only; a tube shines all around.
        if (dirKind.w > 2.5 && dirKind.w < 4.5) {
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
    vec3 f0 = mix(vec3(0.04, 0.04, 0.04), albedo, metallic);
    vec3 fresnel = FresnelSchlick(hDotV, f0);
    float distribution = DistributionGgx(nDotH, roughness);
    float geometry = GeometrySchlickGgx(nDotV, roughness) * GeometrySchlickGgx(nDotL, roughness);
    vec3 specular = (distribution * geometry * fresnel) / max(4.0 * nDotV * nDotL, 0.0001);
    vec3 diffuse = (vec3(1.0, 1.0, 1.0) - fresnel) * (1.0 - metallic) * albedo * (0.31830989 * DiffuseBurley(nDotV, nDotL, lDotH, roughness));
    // Light intensity follows the Unity convention: a light of intensity 1 shining straight onto a
    // surface returns its albedo, so the physical BRDF above is taken times pi.
    vec3 radiance = colorIntensity.rgb * (colorIntensity.a * attenuation * 3.14159265);
    return (diffuse + specular) * radiance * nDotL;
}

vec3 EnvironmentColor(vec3 direction)
{
    float hemisphere = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 constantColor = u_ambientColor.rgb;
    vec3 hemisphereColor = mix(u_environmentGround.rgb, u_environmentZenith.rgb, hemisphere);
    return u_environmentParams.x < 1.5 ? constantColor : hemisphereColor;
}

vec3 EvaluateEnvironment(vec3 normal, vec3 viewDir, vec3 albedo, float metallic, float roughness, float occlusion)
{
    if (u_environmentParams.x < 0.5) {
        return vec3(0.0, 0.0, 0.0);
    }

    float nDotV = max(dot(normal, viewDir), 0.0);
    vec3 f0 = mix(vec3(0.04, 0.04, 0.04), albedo, metallic);
    vec3 fresnel = FresnelSchlickRoughness(nDotV, f0, roughness);
    vec3 diffuseEnv = EnvironmentColor(normal) * albedo * (vec3(1.0, 1.0, 1.0) - fresnel) * (1.0 - metallic) * occlusion * u_environmentParams.y;
    vec3 reflectionDir = reflect(-viewDir, normal);
    float specularEnergy = mix(1.0, 0.18, roughness * roughness);
    vec3 specularEnv = KbSkyReflection(reflectionDir, roughness, EnvironmentColor(reflectionDir)) * fresnel * specularEnergy * u_environmentParams.z;
    return diffuseEnv + specularEnv;
}

float SampleShadowVisibility(vec3 shadowCoord, float penumbraScale)
{
    float biasedDepth = shadowCoord.z + u_shadowParams.x;
    float texelSize = max(u_shadowParams.z, 0.000001);
    float selectedShadow = 0.0;
    if (penumbraScale > 0.0 && u_shadowParams.w >= 2.0) {
        // Soft shadows from the light's source angle (PCSS): the mean depth of
        // the occluders around the point sets the penumbra the filter covers.
        float turn = 6.2831853 * fract(52.9829189 * fract(dot(shadowCoord.xy, vec2(4211.0, 2203.0))));
        float search = clamp(penumbraScale * 0.05, texelSize, 12.0 * texelSize);
        float occluderSum = 0.0;
        float occluders = 0.0;
        for (int i = 0; i < 12; ++i) {
            float r = sqrt((float(i) + 0.5) / 12.0);
            float a = float(i) * 2.39996323 + turn;
            float d = texture2DLod(s_shadowMap, shadowCoord.xy + vec2(cos(a), sin(a)) * (r * search), 0.0).x;
            // The shadow map keeps reversed depth: an occluder is nearer the light, its depth larger.
            occluderSum += d > biasedDepth ? d : 0.0;
            occluders += d > biasedDepth ? 1.0 : 0.0;
        }
        float penumbra = clamp(penumbraScale * (occluderSum / max(occluders, 1.0) - biasedDepth), texelSize, 16.0 * texelSize);
        float softShadow = 0.0;
        for (int j = 0; j < 12; ++j) {
            float r = sqrt((float(j) + 0.5) / 12.0);
            float a = float(j) * 2.39996323 + turn + 1.3;
            softShadow += biasedDepth < texture2DLod(s_shadowMap, shadowCoord.xy + vec2(cos(a), sin(a)) * (r * penumbra), 0.0).x ? 1.0 : 0.0;
        }
        selectedShadow = occluders > 0.0 ? softShadow / 12.0 : 0.0;
    } else {
        float hardShadow = biasedDepth < texture2DLod(s_shadowMap, shadowCoord.xy, 0.0).x ? 1.0 : 0.0;
        float shadowSamples = 0.0;
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                shadowSamples += biasedDepth < texture2DLod(s_shadowMap, shadowCoord.xy + vec2(float(x), float(y)) * texelSize, 0.0).x ? 1.0 : 0.0;
            }
        }
        selectedShadow = u_shadowParams.w < 2.0 ? hardShadow : shadowSamples * 0.11111111;
    }
    return mix(1.0, 1.0 - u_shadowParams.y, selectedShadow);
}

void main()
{
    float terrainLayerOpacity = TerrainLayerOpacity(v_shadowFlags.zw);
    if (terrainLayerOpacity <= 0.0001) {
        discard;
    }
    vec2 materialUv = v_texcoord0 * u_materialUvTransform.xy + u_materialUvTransform.zw;
    vec4 albedo = texture2D(s_albedo, materialUv) * v_color0;
    if (u_materialFlags.x > 0.5 && u_materialFlags.x < 1.5 && albedo.a < u_materialParams.w) {
        discard;
    }

    vec3 normal = normalize(v_normal);
    if (u_materialParams.z > 0.0) {
        vec3 normalSample = texture2D(s_normal, materialUv).xyz * 2.0 - 1.0;
        normalSample.xy *= u_materialParams.z;
        normal = normalize(v_tangent * normalSample.x + v_bitangent * normalSample.y + normal * normalSample.z);
    }
    vec4 metallicRoughness = texture2D(s_metallicRoughness, materialUv);
    float metallic = clamp(u_materialParams.x * metallicRoughness.b, 0.0, 1.0);
    float roughness = clamp(u_materialParams.y * metallicRoughness.g, 0.04, 1.0);
    float occlusionSample = texture2D(s_occlusion, materialUv).r;
    float occlusion = mix(1.0, occlusionSample, clamp(u_materialFlags.y, 0.0, 1.0));
    vec3 viewDir = normalize(u_cameraPosition.xyz - v_worldPos);
    float shadowVisible = 1.0;
    if (u_shadowParams.w > 0.5 && v_shadowFlags.x > 0.5) {
        vec4 coarserCoord;
        vec4 shadowCoord = KbResolveShadowCascade(v_worldPos, coarserCoord);
        if (shadowCoord.w > 0.5) {
            shadowVisible = SampleShadowVisibility(shadowCoord.xyz, shadowCoord.w - 1.0);
            if (coarserCoord.w > 0.0) {
                shadowVisible = mix(shadowVisible, SampleShadowVisibility(coarserCoord.xyz, shadowCoord.w - 1.0), coarserCoord.w);
            }
        }
    }
    vec3 lighting = EvaluateEnvironment(normal, viewDir, albedo.rgb, metallic, roughness, occlusion);
    vec2 lightList = KbLightGridList(v_worldPos, u_lightParams.x);
    for (int entry = 0; entry < int(lightList.y); ++entry) {
        {
            int lightIndex = KbLightGridIndex(lightList, entry);
            vec3 directLight = EvaluateSceneLight(lightIndex, normal, viewDir, v_worldPos, albedo.rgb, metallic, roughness);
            lighting += directLight * (lightIndex == 0 ? shadowVisible : 1.0) * KbPointShadowFactor(lightIndex, v_worldPos, normal);
        }
    }
    vec3 emissive = texture2D(s_emissive, materialUv).rgb * u_materialEmissive.rgb * u_materialEmissive.a;
    // Opaque and masked surfaces cover what is behind them (the scene colour alpha is 1, SceneColorAlphaPolicy);
    // only a blended material has an opacity. A terrain layer blends by its weight.
    float outputAlpha = (u_materialFlags.x < 1.5 ? 1.0 : albedo.a) * terrainLayerOpacity;
    gl_FragColor = vec4(lighting + emissive, outputAlpha);
}
