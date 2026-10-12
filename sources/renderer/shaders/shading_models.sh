#ifndef KB_SHADING_MODELS_SH
#define KB_SHADING_MODELS_SH

// Shading models after Unreal's ShadingModels.ush, shared by deferred lighting and graph forward
// lighting so a surface looks the same on either path. Needs gbuffer_contract.sh for the model ids.
// customData0 is the model's strength (Unreal CustomData0: Clear Coat for the clear coat model),
// customData1 the clear coat roughness (Unreal CustomData1: Clear Coat Roughness).
// The caller's lobes follow the engine's light units: a diffuse lobe of albedo / pi, times the light
// radiance that carries pi (fs_deferred_lighting.sc).

float KbShadingGgx(float nDotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * denom * denom, 0.0000001);
}

float KbShadingSmith(float nDotV, float nDotL, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) * 0.125;
    return (nDotV / max(nDotV * (1.0 - k) + k, 0.0001)) * (nDotL / max(nDotL * (1.0 - k) + k, 0.0001));
}

bool KbIsShadingModel(float model, float id)
{
    return abs(model - id) < 0.5;
}

// One light's response, times N.L, for a surface of the given model. nDotLRaw is the unclamped N.L;
// diffuseLobe and specularLobe are the default-lit lobes the caller evaluated (zero when N.L <= 0).
// The colour a model scatters with when the material gives none (Unreal's Subsurface Color /
// Fuzz Color): the diffuse colour, and the cloth's sheen tint.
vec3 KbDefaultShadingModelColor(float model, vec3 albedo, float metallic)
{
    return KbIsShadingModel(model, KB_GBUFFER_SHADING_MODEL_CLOTH) ? mix(vec3_splat(0.04), albedo, 0.5)
                                                                   : albedo * (1.0 - metallic);
}

vec3 KbShadingModelLightColor(float model, float customData0, float customData1, vec3 N, vec3 V, vec3 L,
    float nDotLRaw, vec3 albedo, float metallic, float roughness, vec3 diffuseLobe, vec3 specularLobe, vec3 modelColor)
{
    float nDotL = max(nDotLRaw, 0.0);
    vec3 defaultLit = (diffuseLobe + specularLobe) * nDotL;
    vec3 diffuseColor = albedo * (1.0 - metallic) * 0.31830989;
    if (KbIsShadingModel(model, KB_GBUFFER_SHADING_MODEL_SUBSURFACE)) {
        // Light wraps past the terminator and scatters through towards the viewer.
        float wrap = 0.5 * customData0;
        float wrapped = clamp((nDotLRaw + wrap) / (1.0 + wrap), 0.0, 1.0) - nDotL;
        float through = pow(clamp(dot(V, -L), 0.0, 1.0), 4.0) * customData0;
        return defaultLit + modelColor * 0.31830989 * (wrapped + through);
    }
    if (nDotL <= 0.0) {
        return vec3(0.0, 0.0, 0.0);
    }
    vec3 H = normalize(V + L);
    float nDotV = max(dot(N, V), 0.0001);
    float nDotH = max(dot(N, H), 0.0);
    if (KbIsShadingModel(model, KB_GBUFFER_SHADING_MODEL_CLEAR_COAT)) {
        // A dielectric coat (F0 0.04) at its own roughness reflects first; the base gets the rest.
        float coatFresnel = (0.04 + 0.96 * pow(1.0 - clamp(dot(V, H), 0.0, 1.0), 5.0)) * clamp(customData0, 0.0, 1.0);
        float coatRoughness = max(customData1, 0.02);
        float coat = KbShadingGgx(nDotH, coatRoughness) * KbShadingSmith(nDotV, nDotL, coatRoughness) * coatFresnel /
            max(4.0 * nDotV * nDotL, 0.0001);
        return defaultLit * (1.0 - coatFresnel) + vec3_splat(coat * nDotL);
    }
    if (KbIsShadingModel(model, KB_GBUFFER_SHADING_MODEL_CLOTH)) {
        // Diffuse with a Charlie sheen (Estevez-Kulla) instead of the GGX highlight.
        float alpha = max(roughness * roughness, 0.07);
        float sin2h = max(1.0 - nDotH * nDotH, 0.0078125);
        float sheenD = (2.0 + 1.0 / alpha) * pow(sin2h, 0.5 / alpha) / (2.0 * 3.14159265);
        float sheenV = 1.0 / (4.0 * (nDotL + nDotV - nDotL * nDotV));
        vec3 sheen = modelColor * customData0 * sheenD * sheenV;
        return (diffuseLobe + sheen) * nDotL;
    }
    if (KbIsShadingModel(model, KB_GBUFFER_SHADING_MODEL_HAIR)) {
        // Kajiya-Kay: the normal carries the strand direction.
        float tDotL = dot(N, L);
        float tDotH = dot(N, H);
        float strandDiffuse = sqrt(max(0.0, 1.0 - tDotL * tDotL)) * 0.5;
        float exponent = mix(8.0, 256.0, 1.0 - roughness);
        float highlight = pow(sqrt(max(0.0, 1.0 - tDotH * tDotH)), exponent) * customData0;
        return diffuseColor * strandDiffuse + mix(vec3_splat(0.04), albedo, 0.5) * 0.31830989 * highlight;
    }
    return defaultLit;
}

vec3 KbShadingModelLight(float model, float customData0, float customData1, vec3 N, vec3 V, vec3 L,
    float nDotLRaw, vec3 albedo, float metallic, float roughness, vec3 diffuseLobe, vec3 specularLobe)
{
    return KbShadingModelLightColor(model, customData0, customData1, N, V, L, nDotLRaw, albedo, metallic, roughness,
        diffuseLobe, specularLobe, KbDefaultShadingModelColor(model, albedo, metallic));
}

// The clear coat's share of the surroundings: the coat reflects them at its own roughness, the base
// light below is what the coat lets through.
vec3 KbClearCoatEnvironment(vec3 baseEnvironment, vec3 coatReflection, float nDotV, float customData0)
{
    float coatFresnel = (0.04 + 0.96 * pow(1.0 - clamp(nDotV, 0.0, 1.0), 5.0)) * clamp(customData0, 0.0, 1.0);
    return baseEnvironment * (1.0 - coatFresnel) + coatReflection * coatFresnel;
}

#endif // KB_SHADING_MODELS_SH
