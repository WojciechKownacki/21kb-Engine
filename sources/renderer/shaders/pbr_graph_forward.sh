#ifndef KB_PBR_GRAPH_FORWARD_SH
#define KB_PBR_GRAPH_FORWARD_SH

uniform vec4 u_cameraPosition;
uniform vec4 u_lightDirKind[32];
uniform vec4 u_lightPositionRange[32];
uniform vec4 u_lightColorIntensity[32];
uniform vec4 u_lightSpot[32];
uniform vec4 u_lightParams;
uniform vec4 u_ambientColor;
uniform vec4 u_environmentZenith;
uniform vec4 u_environmentGround;
uniform vec4 u_environmentParams;

float KbDistributionGgx(float nDotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * denom * denom, 0.0001);
}

float KbGeometrySchlickGgx(float nDotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.0001);
}

vec3 KbFresnelSchlick(float cosTheta, vec3 f0)
{
    return f0 + (vec3(1.0, 1.0, 1.0) - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 KbFresnelSchlickRoughness(float cosTheta, vec3 f0, float roughness)
{
    vec3 roughF0 = max(vec3_splat(1.0 - roughness), f0);
    return f0 + (roughF0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float KbDiffuseBurley(float nDotV, float nDotL, float lDotH, float roughness)
{
    float energyBias = mix(0.0, 0.5, roughness);
    float energyFactor = mix(1.0, 1.0 / 1.51, roughness);
    float fd90 = energyBias + 2.0 * lDotH * lDotH * roughness;
    float lightScatter = 1.0 + (fd90 - 1.0) * pow(clamp(1.0 - nDotL, 0.0, 1.0), 5.0);
    float viewScatter = 1.0 + (fd90 - 1.0) * pow(clamp(1.0 - nDotV, 0.0, 1.0), 5.0);
    return lightScatter * viewScatter * energyFactor;
}

vec3 KbEvaluateSceneLight(int lightIndex, vec3 normal, vec3 viewDir, vec3 worldPos, vec3 albedo, float metallic, float roughness, float specular, float occlusion)
{
    vec4 dirKind = u_lightDirKind[lightIndex];
    vec4 positionRange = u_lightPositionRange[lightIndex];
    vec4 colorIntensity = u_lightColorIntensity[lightIndex];
    vec4 spot = u_lightSpot[lightIndex];

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
    vec3 fresnel = KbFresnelSchlick(hDotV, f0);
    float distribution = KbDistributionGgx(nDotH, roughness);
    float geometry = KbGeometrySchlickGgx(nDotV, roughness) * KbGeometrySchlickGgx(nDotL, roughness);
    vec3 specularTerm = (distribution * geometry * fresnel) / max(4.0 * nDotV * nDotL, 0.0001);
    vec3 diffuse = (vec3(1.0, 1.0, 1.0) - fresnel) * (1.0 - metallic) * albedo * (0.31830989 * KbDiffuseBurley(nDotV, nDotL, lDotH, roughness)) * occlusion;
    vec3 radiance = colorIntensity.rgb * (colorIntensity.a * attenuation);
    return (diffuse + specularTerm) * radiance * nDotL;
}

vec3 KbEnvironmentColor(vec3 direction)
{
    float hemisphere = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 constantColor = u_ambientColor.rgb;
    vec3 hemisphereColor = mix(u_environmentGround.rgb, u_environmentZenith.rgb, hemisphere);
    return u_environmentParams.x < 1.5 ? constantColor : hemisphereColor;
}

vec3 KbEvaluateEnvironment(vec3 normal, vec3 viewDir, vec3 albedo, float metallic, float roughness, float specular, float occlusion)
{
    if (u_environmentParams.x < 0.5) {
        return vec3(0.0, 0.0, 0.0);
    }

    float nDotV = max(dot(normal, viewDir), 0.0);
    vec3 f0 = mix(vec3_splat(0.08 * specular), albedo, metallic);
    vec3 fresnel = KbFresnelSchlickRoughness(nDotV, f0, roughness);
    vec3 diffuseEnv = KbEnvironmentColor(normal) * albedo * (vec3(1.0, 1.0, 1.0) - fresnel) * (1.0 - metallic) * occlusion * u_environmentParams.y;
    vec3 reflectionDir = reflect(-viewDir, normal);
    float specularEnergy = mix(1.0, 0.18, roughness * roughness);
    vec3 specularEnv = KbEnvironmentColor(reflectionDir) * fresnel * specularEnergy * u_environmentParams.z;
    return diffuseEnv + specularEnv;
}

vec3 KbEvaluateForwardLighting(vec3 worldNormal, vec3 worldPos, vec3 albedo, float metallic, float roughness, float specular, float occlusion)
{
    vec3 viewDir = normalize(u_cameraPosition.xyz - worldPos);
    vec3 lighting = KbEvaluateEnvironment(worldNormal, viewDir, albedo, metallic, roughness, specular, occlusion);
    for (int lightIndex = 0; lightIndex < 32; ++lightIndex) {
        if (float(lightIndex) < u_lightParams.x) {
            lighting += KbEvaluateSceneLight(lightIndex, worldNormal, viewDir, worldPos, albedo, metallic, roughness, specular, occlusion);
        }
    }
    return lighting;
}

#endif // KB_PBR_GRAPH_FORWARD_SH
