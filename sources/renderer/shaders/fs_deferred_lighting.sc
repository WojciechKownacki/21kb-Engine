$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_gbufferAlbedo, 0);
SAMPLER2D(s_gbufferNormal, 1);
SAMPLER2D(s_gbufferMaterial, 2);
SAMPLER2D(s_gbufferDepth, 3);
uniform vec4 u_deferredLightDirKind[4];
uniform vec4 u_deferredLightPositionRange[4];
uniform vec4 u_deferredLightColorIntensity[4];
uniform vec4 u_deferredLightSpot[4];
uniform vec4 u_deferredLightParams;
uniform vec4 u_deferredAmbientColor;
uniform vec4 u_deferredEnvironmentZenith;
uniform vec4 u_deferredEnvironmentGround;
uniform vec4 u_deferredEnvironmentParams;
uniform vec4 u_deferredCameraPosition;
uniform mat4 u_deferredInverseViewProjection;
uniform vec4 u_deferredDepthParams;

vec3 FresnelSchlick(float cosTheta, vec3 f0)
{
    return f0 + (vec3(1.0, 1.0, 1.0) - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
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

vec3 EvaluateEnvironment(vec3 normal, vec3 viewDir, vec3 albedo, float metallic, float roughness, float occlusion)
{
    if (u_deferredEnvironmentParams.x < 0.5) {
        return vec3(0.0, 0.0, 0.0);
    }

    float nDotV = max(dot(normal, viewDir), 0.0);
    vec3 f0 = mix(vec3(0.04, 0.04, 0.04), albedo, metallic);
    vec3 fresnel = FresnelSchlick(nDotV, f0);
    vec3 diffuseEnv = EnvironmentColor(normal) * albedo * (vec3(1.0, 1.0, 1.0) - fresnel) * (1.0 - metallic) * occlusion * u_deferredEnvironmentParams.y;
    vec3 reflectionDir = reflect(-viewDir, normal);
    float specularEnergy = mix(1.0, 0.18, roughness * roughness);
    vec3 specularEnv = EnvironmentColor(reflectionDir) * fresnel * specularEnergy * u_deferredEnvironmentParams.z;
    return diffuseEnv + specularEnv;
}

vec3 EvaluateSceneLight(int lightIndex, vec3 normal, vec3 viewDir, vec3 worldPos, vec3 albedo, float metallic, float roughness, float occlusion)
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
    vec3 f0 = mix(vec3(0.04, 0.04, 0.04), albedo, metallic);
    vec3 fresnel = FresnelSchlick(hDotV, f0);
    float distribution = DistributionGgx(nDotH, roughness);
    float geometry = GeometrySchlickGgx(nDotV, roughness) * GeometrySchlickGgx(nDotL, roughness);
    vec3 specular = (distribution * geometry * fresnel) / max(4.0 * nDotV * nDotL, 0.0001);
    vec3 diffuse = (vec3(1.0, 1.0, 1.0) - fresnel) * (1.0 - metallic) * albedo * (0.31830989 * DiffuseBurley(nDotV, nDotL, lDotH, roughness)) * occlusion;
    vec3 radiance = colorIntensity.rgb * (colorIntensity.a * attenuation);
    return (diffuse + specular) * radiance * nDotL;
}

void main()
{
    vec4 albedo = texture2D(s_gbufferAlbedo, v_texcoord0);
    float depth = texture2D(s_gbufferDepth, v_texcoord0).x;
    if (depth <= 0.000001 || albedo.a <= 0.000001) {
        discard;
    }
    vec3 normal = normalize(texture2D(s_gbufferNormal, v_texcoord0).xyz * 2.0 - 1.0);
    vec4 material = texture2D(s_gbufferMaterial, v_texcoord0);
    float metallic = clamp(material.x, 0.0, 1.0);
    float roughness = clamp(material.y, 0.04, 1.0);
    float occlusion = clamp(material.z, 0.0, 1.0);

    vec3 worldPos = ReconstructWorldPosition(v_texcoord0, depth);
    vec3 viewDir = normalize(u_deferredCameraPosition.xyz - worldPos);
    vec3 lighting = EvaluateEnvironment(normal, viewDir, albedo.rgb, metallic, roughness, occlusion);

    for (int lightIndex = 0; lightIndex < 4; ++lightIndex) {
        if (float(lightIndex) < u_deferredLightParams.x) {
            lighting += EvaluateSceneLight(lightIndex, normal, viewDir, worldPos, albedo.rgb, metallic, roughness, occlusion);
        }
    }

    gl_FragColor = vec4(lighting, 1.0);
}
