$input v_normal, v_color0, v_texcoord0, v_worldPos, v_shadowPos, v_shadowFlags, v_tangent, v_bitangent, v_objectLocalPos, v_objectWorldPos, v_objectOrientation, v_preSkinnedNormal

#include <bgfx_shader.sh>

SAMPLER2D(s_albedo, 0);
SAMPLER2D(s_normal, 1);
SAMPLER2D(s_metallicRoughness, 2);
SAMPLER2D(s_occlusion, 3);
SAMPLER2D(s_emissive, 4);
SAMPLER2D(s_shadowMap, 5);
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
uniform vec4 u_shadowParams;

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
    float energyBias = mix(0.0, 0.5, roughness);
    float energyFactor = mix(1.0, 1.0 / 1.51, roughness);
    float fd90 = energyBias + 2.0 * lDotH * lDotH * roughness;
    float lightScatter = 1.0 + (fd90 - 1.0) * pow(clamp(1.0 - nDotL, 0.0, 1.0), 5.0);
    float viewScatter = 1.0 + (fd90 - 1.0) * pow(clamp(1.0 - nDotV, 0.0, 1.0), 5.0);
    return lightScatter * viewScatter * energyFactor;
}

vec3 SurfaceEmitterSamplePosition(vec3 center, vec3 normal, vec3 right, float kind, vec2 dimensions, vec3 worldPos)
{
    if (kind < 2.5) {
        return center;
    }

    vec3 localRight = normalize(right);
    vec3 localUp = normalize(cross(normal, localRight));
    vec3 fromCenter = worldPos - center;
    if (kind < 3.5) {
        return center + localRight * clamp(dot(fromCenter, localRight), -dimensions.x * 0.5, dimensions.x * 0.5)
            + localUp * clamp(dot(fromCenter, localUp), -dimensions.y * 0.5, dimensions.y * 0.5);
    }
    if (kind < 4.5) {
        vec3 planar = localRight * dot(fromCenter, localRight) + localUp * dot(fromCenter, localUp);
        float planarLength = length(planar);
        return center + planar * min(1.0, dimensions.x * 0.5 / max(planarLength, 0.0001));
    }

    vec3 axisPoint = center + localRight * clamp(dot(fromCenter, localRight), -dimensions.x * 0.5, dimensions.x * 0.5);
    vec3 radial = worldPos - axisPoint;
    float radialLength = length(radial);
    return axisPoint + radial * min(1.0, dimensions.y * 0.5 / max(radialLength, 0.0001));
}

vec3 EvaluateSceneLight(int lightIndex, vec3 normal, vec3 viewDir, vec3 worldPos, vec3 albedo, float metallic, float roughness, float occlusion)
{
    vec4 dirKind = u_lightDirKind[lightIndex];
    vec4 positionRange = u_lightPositionRange[lightIndex];
    vec4 colorIntensity = u_lightColorIntensity[lightIndex];
    vec4 spot = u_lightSpot[lightIndex];
    vec4 areaRight = u_lightAreaRight[lightIndex];

    vec3 lightVector = vec3(0.0, 1.0, 0.0);
    float attenuation = 1.0;
    if (dirKind.w < 0.5) {
        lightVector = normalize(-dirKind.xyz);
    } else {
        vec3 emitterPosition = SurfaceEmitterSamplePosition(positionRange.xyz, dirKind.xyz, areaRight.xyz, dirKind.w, spot.zw, worldPos);
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
    vec3 f0 = mix(vec3(0.04, 0.04, 0.04), albedo, metallic);
    vec3 fresnel = FresnelSchlick(hDotV, f0);
    float distribution = DistributionGgx(nDotH, roughness);
    float geometry = GeometrySchlickGgx(nDotV, roughness) * GeometrySchlickGgx(nDotL, roughness);
    vec3 specular = (distribution * geometry * fresnel) / max(4.0 * nDotV * nDotL, 0.0001);
    vec3 diffuse = (vec3(1.0, 1.0, 1.0) - fresnel) * (1.0 - metallic) * albedo * (0.31830989 * DiffuseBurley(nDotV, nDotL, lDotH, roughness)) * occlusion;
    vec3 radiance = colorIntensity.rgb * (colorIntensity.a * attenuation);
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
    vec3 specularEnv = EnvironmentColor(reflectionDir) * fresnel * specularEnergy * u_environmentParams.z;
    return diffuseEnv + specularEnv;
}

float SampleShadowVisibility(vec3 shadowCoord)
{
    float biasedDepth = shadowCoord.z + u_shadowParams.x;
    float storedDepth = texture2D(s_shadowMap, shadowCoord.xy).x;
    float hardShadow = biasedDepth < storedDepth ? 1.0 : 0.0;
    float texelSize = max(u_shadowParams.z, 0.000001);
    float shadowSamples =
        (biasedDepth < texture2D(s_shadowMap, shadowCoord.xy + vec2(-texelSize, -texelSize)).x ? 1.0 : 0.0) +
        (biasedDepth < texture2D(s_shadowMap, shadowCoord.xy + vec2(0.0, -texelSize)).x ? 1.0 : 0.0) +
        (biasedDepth < texture2D(s_shadowMap, shadowCoord.xy + vec2(texelSize, -texelSize)).x ? 1.0 : 0.0) +
        (biasedDepth < texture2D(s_shadowMap, shadowCoord.xy + vec2(-texelSize, 0.0)).x ? 1.0 : 0.0) +
        (biasedDepth < texture2D(s_shadowMap, shadowCoord.xy).x ? 1.0 : 0.0) +
        (biasedDepth < texture2D(s_shadowMap, shadowCoord.xy + vec2(texelSize, 0.0)).x ? 1.0 : 0.0) +
        (biasedDepth < texture2D(s_shadowMap, shadowCoord.xy + vec2(-texelSize, texelSize)).x ? 1.0 : 0.0) +
        (biasedDepth < texture2D(s_shadowMap, shadowCoord.xy + vec2(0.0, texelSize)).x ? 1.0 : 0.0) +
        (biasedDepth < texture2D(s_shadowMap, shadowCoord.xy + vec2(texelSize, texelSize)).x ? 1.0 : 0.0);
    float inShadow = shadowSamples * 0.11111111;
    float selectedShadow = u_shadowParams.w < 2.0 ? hardShadow : inShadow;
    return mix(1.0, 1.0 - u_shadowParams.y, selectedShadow);
}

void main()
{
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
    vec3 shadowCoord = v_shadowPos.xyz / max(v_shadowPos.w, 0.0001);
    float shadowVisible = 1.0;
    if (u_shadowParams.w > 0.5 && v_shadowFlags.x > 0.5 &&
        shadowCoord.x >= 0.0 && shadowCoord.x <= 1.0 &&
        shadowCoord.y >= 0.0 && shadowCoord.y <= 1.0 &&
        shadowCoord.z >= 0.0 && shadowCoord.z <= 1.0) {
        shadowVisible = SampleShadowVisibility(shadowCoord);
    }
    vec3 lighting = EvaluateEnvironment(normal, viewDir, albedo.rgb, metallic, roughness, occlusion);
    for (int lightIndex = 0; lightIndex < 32; ++lightIndex) {
        if (float(lightIndex) < u_lightParams.x) {
            vec3 directLight = EvaluateSceneLight(lightIndex, normal, viewDir, v_worldPos, albedo.rgb, metallic, roughness, occlusion);
            lighting += lightIndex == 0 ? directLight * shadowVisible : directLight;
        }
    }
    vec3 emissive = texture2D(s_emissive, materialUv).rgb * u_materialEmissive.rgb * u_materialEmissive.a;
    float outputAlpha = u_materialFlags.x < 0.5 ? 1.0 : albedo.a;
    gl_FragColor = vec4(lighting + emissive, outputAlpha);
}
