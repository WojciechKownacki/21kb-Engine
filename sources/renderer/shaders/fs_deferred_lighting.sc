$input v_texcoord0

#include <bgfx_shader.sh>
#define KB_LIGHT_GRID_DEFERRED 1
#include "light_grid.sh"
#include "shadow_cascades.sh"
#include "point_shadow.sh"
#include "gbuffer_contract.sh"
#include "shading_models.sh"
#include "gbuffer_position.sh"

SAMPLER2D(s_gbufferAlbedo, 0);
SAMPLER2D(s_gbufferNormal, 1);
SAMPLER2D(s_gbufferMaterial, 2);
SAMPLER2D(s_gbufferSurface, 3);
SAMPLER2D(s_gbufferDepth, 4);
SAMPLER2D(s_deferredShadowMap, 5);
SAMPLER2D(s_deferredBackdropEnvironment, 6);
SAMPLERCUBE(s_skyReflection, 12);
// The G-buffer custom data (Subsurface Color / Fuzz Color, alpha 1 where given); read only when
// u_deferredCustomData.x is 1 (the frame drew a material that writes it).
SAMPLER2D(s_gbufferCustom, 13);
uniform vec4 u_deferredCustomData;
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

#include "sky_backdrop.sh"
#include "sky_reflection.sh"

#include "ssgi.sh"
#include "ssr.sh"

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
    // Disney diffuse shape without energy renormalisation, so a surface lit straight on
    // returns its albedo as Lambert diffuse does (the factor darkened
    // every light by about a quarter at common roughness).
    float energyBias = mix(0.0, 0.5, roughness);
    float fd90 = energyBias + 2.0 * lDotH * lDotH * roughness;
    float lightScatter = 1.0 + (fd90 - 1.0) * pow(clamp(1.0 - nDotL, 0.0, 1.0), 5.0);
    float viewScatter = 1.0 + (fd90 - 1.0) * pow(clamp(1.0 - nDotV, 0.0, 1.0), 5.0);
    return lightScatter * viewScatter;
}

vec3 EnvironmentColor(vec3 direction)
{
    float hemisphere = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 constantColor = u_deferredAmbientColor.rgb;
    vec3 hemisphereColor = mix(u_deferredEnvironmentGround.rgb, u_deferredEnvironmentZenith.rgb, hemisphere);
    return u_deferredEnvironmentParams.x < 1.5 ? constantColor : hemisphereColor;
}

// `reflection` is the screen-space reflection (rgb radiance, a confidence); it replaces the environment
// specular where a ray hit something.
vec3 EvaluateEnvironment(vec3 normal, vec3 viewDir, vec3 albedo, float metallic, float roughness, float specular, float occlusion, vec4 reflection)
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
    vec3 specularEnv = KbSkyReflection(reflectionDir, roughness, EnvironmentColor(reflectionDir)) * fresnel * specularEnergy * u_deferredEnvironmentParams.z;
    vec3 specularSsr = reflection.rgb * fresnel * specularEnergy * u_ssrParams.x;
    return diffuseEnv + mix(specularEnv, specularSsr, reflection.a);
}

void ReadSceneLight(int lightIndex, out vec4 dirKind, out vec4 positionRange, out vec4 colorIntensity, out vec4 spot, out vec4 areaRight)
{
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
}

// Screen-space contact shadow (Contact Shadow Length): 12 steps
// toward the light over `contactLength` metres against the depth buffer; a surface just in front of
// the step hides the light. Catches the small shadows a shadow map is too coarse for.
float ContactShadow(vec3 worldPos, vec3 lightVector, float contactLength)
{
    if (contactLength <= 0.0) {
        return 1.0;
    }
    vec3 camera = u_deferredCameraPosition.xyz;
    float jitter = fract(52.9829189 * fract(dot(worldPos, vec3(0.06711056, 0.00583715, 0.0372))));
    for (int i = 0; i < 12; ++i) {
        vec3 p = worldPos + lightVector * (contactLength * (float(i) + jitter) / 12.0);
        vec4 clip = mul(u_giViewProj, vec4(p, 1.0));
        if (clip.w <= 0.0001) {
            break;
        }
        vec2 uv = KbGiUv(clip);
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
            break;
        }
        float behind = length(p - camera) - length(ReconstructWorldPosition(uv, texture2DLod(s_gbufferDepth, uv, 0.0).x) - camera);
        if (behind > 0.02 && behind < 0.25 * contactLength + 0.1) {
            return 0.0;
        }
    }
    return 1.0;
}

// Material ambient occlusion darkens only the light from the surroundings; a light
// shining on the surface is not occluded by it.
vec3 EvaluateSceneLight(int lightIndex, vec3 normal, vec3 viewDir, vec3 worldPos, vec3 albedo, float metallic, float roughness, float specular,
    float shadingModel, float customData0, float customData1, vec3 modelColor, vec3 irisNormal, vec2 iris)
{
    vec4 dirKind, positionRange, colorIntensity, spot, areaRight;
    ReadSceneLight(lightIndex, dirKind, positionRange, colorIntensity, spot, areaRight);

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
        // Inverse square, windowed to zero at the light's range (Unity URP, Unreal, Godot 4, Verth).
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

    attenuation *= ContactShadow(worldPos + normal * 0.02, lightVector, fract(dirKind.w) * 25.0);

    float nDotLRaw = dot(normal, lightVector);
    float nDotL = max(nDotLRaw, 0.0);
    // Light from behind lights only a surface that lets it through (subsurface).
    if (nDotL <= 0.0 && !KbIsShadingModel(shadingModel, KB_GBUFFER_SHADING_MODEL_SUBSURFACE)) {
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
    vec3 specularTerm = nDotL > 0.0 ? (distribution * geometry * fresnel) / max(4.0 * nDotV * nDotL, 0.0001) : vec3_splat(0.0);
    vec3 diffuse = (vec3(1.0, 1.0, 1.0) - fresnel) * (1.0 - metallic) * albedo * (0.31830989 * DiffuseBurley(nDotV, nDotL, lDotH, roughness));
    // Light intensity follows the Unity convention: a light of intensity 1 shining straight onto a
    // surface returns its albedo, so the physical BRDF above is taken times pi.
    vec3 radiance = colorIntensity.rgb * (colorIntensity.a * attenuation * 3.14159265);
    return KbShadingModelLightFull(shadingModel, customData0, customData1, normal, viewDir, lightVector, nDotLRaw,
        albedo, metallic, roughness, diffuse, specularTerm, modelColor, irisNormal, iris) * radiance;
}

float SampleShadowVisibility(vec3 shadowCoord, float penumbraScale)
{
    float biasedDepth = shadowCoord.z + u_deferredShadowParams.x;
    float texelSize = max(u_deferredShadowParams.z, 0.000001);
    float selectedShadow = 0.0;
    if (penumbraScale > 0.0 && u_deferredShadowParams.w >= 2.0) {
        // Soft shadows from the light's source angle (PCSS): the mean depth of
        // the occluders around the point sets the penumbra the filter covers.
        float turn = 6.2831853 * fract(52.9829189 * fract(dot(shadowCoord.xy, vec2(4211.0, 2203.0))));
        float search = clamp(penumbraScale * 0.05, texelSize, 12.0 * texelSize);
        float occluderSum = 0.0;
        float occluders = 0.0;
        for (int i = 0; i < 12; ++i) {
            float r = sqrt((float(i) + 0.5) / 12.0);
            float a = float(i) * 2.39996323 + turn;
            float d = texture2DLod(s_deferredShadowMap, shadowCoord.xy + vec2(cos(a), sin(a)) * (r * search), 0.0).x;
            // The shadow map keeps reversed depth: an occluder is nearer the light, its depth larger.
            occluderSum += d > biasedDepth ? d : 0.0;
            occluders += d > biasedDepth ? 1.0 : 0.0;
        }
        float penumbra = clamp(penumbraScale * (occluderSum / max(occluders, 1.0) - biasedDepth), texelSize, 16.0 * texelSize);
        float softShadow = 0.0;
        for (int j = 0; j < 12; ++j) {
            float r = sqrt((float(j) + 0.5) / 12.0);
            float a = float(j) * 2.39996323 + turn + 1.3;
            softShadow += biasedDepth < texture2DLod(s_deferredShadowMap, shadowCoord.xy + vec2(cos(a), sin(a)) * (r * penumbra), 0.0).x ? 1.0 : 0.0;
        }
        selectedShadow = occluders > 0.0 ? softShadow / 12.0 : 0.0;
    } else {
        float hardShadow = biasedDepth < texture2DLod(s_deferredShadowMap, shadowCoord.xy, 0.0).x ? 1.0 : 0.0;
        float shadowSamples = 0.0;
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                shadowSamples += biasedDepth < texture2DLod(s_deferredShadowMap, shadowCoord.xy + vec2(float(x), float(y)) * texelSize, 0.0).x ? 1.0 : 0.0;
            }
        }
        selectedShadow = u_deferredShadowParams.w < 2.0 ? hardShadow : shadowSamples * 0.11111111;
    }
    return mix(1.0, 1.0 - u_deferredShadowParams.y, selectedShadow);
}

// Exponential height fog and volumetric fog. u_fogParams: density at the base height,
// falloff per metre above it, base height, 1 = fog on. u_fogColor: colour, w = anisotropy.
// u_fogVolumetric: x 1 = volumetric, y = how far (m) it is marched.
uniform vec4 u_fogParams;
uniform vec4 u_fogColor;
uniform vec4 u_fogVolumetric;
uniform vec4 u_fogSky; // rgb = the colour the scene clears its sky to, w = 1 when fog is on

float FogDensity(float height)
{
    return u_fogParams.x * exp(-u_fogParams.y * max(height - u_fogParams.z, -100.0));
}

// Henyey-Greenstein phase function, normalised over the sphere.
float FogPhase(float cosTheta, float g)
{
    float g2 = g * g;
    return (1.0 - g2) / (12.5663706 * pow(max(1.0 + g2 - 2.0 * g * cosTheta, 0.0001), 1.5));
}

// Light the scene's lights scatter toward the camera at `p` (per unit of fog density): each light
// with Volumetric Scattering (|areaRight.w| - 1), with its falloff, cone and shadows - light shafts.
vec3 LightInFog(vec3 p, vec3 rayDir)
{
    vec3 inScattered = vec3_splat(0.0);
    vec2 lightList = KbLightGridList(p, u_deferredLightParams.x);
    for (int entry = 0; entry < int(lightList.y); ++entry) {
        int lightIndex = KbLightGridIndex(lightList, entry);
        vec4 dirKind, positionRange, colorIntensity, spot, areaRight;
        ReadSceneLight(lightIndex, dirKind, positionRange, colorIntensity, spot, areaRight);
        float scattering = abs(areaRight.w) - 1.0;
        if (scattering <= 0.0) {
            continue;
        }
        vec3 lightVector;
        float attenuation = 1.0;
        if (dirKind.w < 0.5) {
            lightVector = normalize(-dirKind.xyz);
            if (lightIndex == 0 && u_deferredShadowParams.w > 0.5) {
                vec4 coarser;
                vec4 shadowCoord = KbResolveShadowCascade(p, coarser);
                if (shadowCoord.w > 0.5) {
                    attenuation = SampleShadowVisibility(shadowCoord.xyz, 0.0);
                }
            }
        } else {
            vec3 toLight = KbSurfaceEmitterSamplePosition(positionRange.xyz, dirKind.xyz, areaRight.xyz, dirKind.w, spot.zw, p) - p;
            float distanceToLight = length(toLight);
            lightVector = toLight / max(distanceToLight, 0.0001);
            float rangeRatio = distanceToLight / max(positionRange.w, 0.0001);
            float rangeWindow = clamp(1.0 - rangeRatio * rangeRatio * rangeRatio * rangeRatio, 0.0, 1.0);
            attenuation = rangeWindow * rangeWindow / max(distanceToLight * distanceToLight, 0.0001);
            if (dirKind.w > 1.5 && dirKind.w < 2.5) {
                float cone = clamp((dot(normalize(dirKind.xyz), -lightVector) - spot.y) / max(spot.x - spot.y, 0.001), 0.0, 1.0);
                attenuation *= cone * cone;
            }
            if (dirKind.w > 2.5 && dirKind.w < 4.5) {
                attenuation *= max(dot(normalize(dirKind.xyz), -lightVector), 0.0);
            }
            if (attenuation <= 0.0) {
                continue;
            }
            attenuation *= KbPointShadowFactor(lightIndex, p, vec3_splat(0.0));
        }
        // The engine's intensity convention (the surface lighting takes the light times pi).
        inScattered += colorIntensity.rgb * (colorIntensity.a * 3.14159265 * attenuation * scattering *
            FogPhase(dot(rayDir, lightVector), u_fogColor.w));
    }
    return inScattered;
}

// The fogged colour of a surface at `worldPos`: the height fog lights itself to its colour (Fog
// Inscattering Color); volumetric fog adds the light the scene's lights scatter in it, marched
// in 24 steps with their shadows (light shafts).
vec3 ApplyFog(vec3 color, vec3 worldPos, vec2 pixel)
{
    if (u_fogParams.w < 0.5) {
        return color;
    }
    vec3 camera = u_deferredCameraPosition.xyz;
    vec3 ray = worldPos - camera;
    float rayLength = length(ray);
    vec3 rayDir = ray / max(rayLength, 0.0001);
    float falloff = max(u_fogParams.y, 0.0001);
    float rise = falloff * ray.y;
    float alongRay = abs(rise) > 0.001 ? (1.0 - exp(-rise)) / rise : 1.0;
    float transmittance = exp(-FogDensity(camera.y) * alongRay * rayLength);
    if (u_fogVolumetric.x < 0.5) {
        return color * transmittance + u_fogColor.rgb * (1.0 - transmittance);
    }
    float marched = min(rayLength, u_fogVolumetric.y);
    float stepLength = marched / 24.0;
    // Interleaved gradient noise of the pixel: neighbouring pixels start their march at different offsets, so the 24
    // steps read as fine noise instead of rings.
    float jitter = fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
    vec3 inScattered = vec3_splat(0.0);
    float throughput = 1.0;
    for (int i = 0; i < 24; ++i) {
        vec3 p = camera + rayDir * ((float(i) + jitter) * stepLength);
        float density = FogDensity(p.y);
        // White albedo: the lights' own colour scatters.
        inScattered += throughput * density * stepLength * LightInFog(p, rayDir);
        throughput *= exp(-density * stepLength);
    }
    return color * transmittance + u_fogColor.rgb * (1.0 - transmittance) + inScattered;
}

// The lit colour of the G-buffer surface at this pixel; an unlit surface comes back without fog.
vec3 ShadeSurface(vec2 uv, vec2 pixel, vec4 albedo, vec4 encodedNormal, float depth, out vec3 worldPos, out bool fogged)
{
    worldPos = vec3_splat(0.0);
    fogged = false;
    vec3 normal = normalize(encodedNormal.xyz * 2.0 - 1.0);
    vec4 material = texture2D(s_gbufferMaterial, uv);
    vec4 surface = texture2D(s_gbufferSurface, uv);
    float metallic = clamp(material.x, 0.0, 1.0);
    float roughness = clamp(material.y, 0.04, 1.0);
    float occlusion = clamp(material.z, 0.0, 1.0);
    float shadingModel = KbDecodeGBufferShadingModel(material.w);
    float specular = clamp(surface.w, 0.0, 1.0);
    float customData0 = clamp(albedo.a, 0.0, 1.0);
    float customData1 = KbDecodeGBufferCustomData1(encodedNormal.a);
    vec3 modelColor = KbDefaultShadingModelColor(shadingModel, albedo.rgb, metallic);
    vec3 irisNormal = normal;
    vec2 iris = vec2_splat(0.0);
    if (u_deferredCustomData.x > 0.5) {
        vec4 customData = texture2D(s_gbufferCustom, uv);
        if (abs(shadingModel - KB_GBUFFER_SHADING_MODEL_EYE) < 0.5) {
            // Iris normal (octahedral), mask, distance; zero = an eye without iris data.
            if (dot(customData, vec4_splat(1.0)) > 0.0) {
                irisNormal = KbOctDecode(customData.xy * 2.0 - 1.0);
                iris = customData.zw;
            }
        } else {
            modelColor = customData.a > 0.5 ? customData.rgb : modelColor;
        }
    }

    if (abs(shadingModel - KB_GBUFFER_SHADING_MODEL_UNLIT) < 0.5) {
        return albedo.rgb + surface.rgb;
    }

    fogged = true;
    worldPos = ReconstructWorldPosition(uv, depth);
    vec3 viewDir = normalize(u_deferredCameraPosition.xyz - worldPos);

    float shadowVisible = 1.0;
    if (u_deferredShadowParams.w > 0.5) {
        vec4 coarserCoord;
        vec4 shadowCoord = KbResolveShadowCascade(worldPos, coarserCoord);
        if (shadowCoord.w > 0.5) {
            shadowVisible = SampleShadowVisibility(shadowCoord.xyz, shadowCoord.w - 1.0);
            if (coarserCoord.w > 0.0) {
                shadowVisible = mix(shadowVisible, SampleShadowVisibility(coarserCoord.xyz, shadowCoord.w - 1.0), coarserCoord.w);
            }
        }
    }

    // Screen-space results of the previous frame: bounce light, ambient visibility, reflections.
    vec3 giAccumulated;
    float aoAccumulated;
    float skyAccumulated;
    KbScreenSpaceAccumulatedSmooth(worldPos, giAccumulated, aoAccumulated, skyAccumulated);
    aoAccumulated = mix(1.0, aoAccumulated, step(0.0001, u_aoParams.x));
    // The sky light reaches only as much of the sky as the voxel GI sees (occluded sky light).
    skyAccumulated = mix(1.0, skyAccumulated, step(0.0001, u_giParams.x));
    vec4 reflection = KbScreenSpaceReflection(worldPos, normal, viewDir, roughness, pixel);
    vec3 lighting = EvaluateEnvironment(normal, viewDir, albedo.rgb, metallic, roughness, specular, occlusion * aoAccumulated * skyAccumulated, reflection);
    if (KbIsShadingModel(shadingModel, KB_GBUFFER_SHADING_MODEL_CLEAR_COAT) && u_deferredEnvironmentParams.x > 0.5) {
        vec3 coatDirection = reflect(-viewDir, normal);
        vec3 coatReflection = mix(KbSkyReflection(coatDirection, customData1, EnvironmentColor(coatDirection)) * u_deferredEnvironmentParams.z,
            reflection.rgb * u_ssrParams.x, reflection.a);
        lighting = KbClearCoatEnvironment(lighting, coatReflection, dot(normal, viewDir), customData0);
    }

    vec2 lightList = KbLightGridList(worldPos, u_deferredLightParams.x);
    for (int entry = 0; entry < int(lightList.y); ++entry) {
        {
            int lightIndex = KbLightGridIndex(lightList, entry);
            vec3 directLight = EvaluateSceneLight(lightIndex, normal, viewDir, worldPos, albedo.rgb, metallic, roughness, specular,
                shadingModel, customData0, customData1, modelColor, irisNormal, iris);
            lighting += directLight * (lightIndex == 0 ? shadowVisible : 1.0) * KbPointShadowFactor(lightIndex, worldPos, normal);
        }
    }

    if (u_giParams.x > 0.0) {
        lighting += giAccumulated * albedo.rgb * ((1.0 - metallic) * occlusion * aoAccumulated * u_giParams.x);
    }
    return lighting + surface.rgb;
}

void main()
{
    vec4 albedo = texture2D(s_gbufferAlbedo, v_texcoord0);
    vec4 encodedNormal = texture2D(s_gbufferNormal, v_texcoord0);
    float depth = texture2D(s_gbufferDepth, v_texcoord0).x;
    // The normal attachment reserves alpha 0 for its clear value, while every
    // opaque G-buffer write stores 1. This is independent from both depth
    // conventions and authored material opacity.
    bool background = encodedNormal.a < 0.5;
    vec3 color;
    vec3 fogEnd;
    bool fogged = true;
    if (background) {
        if (u_deferredBackdropParams.x <= 0.5 && u_fogSky.w <= 0.5) {
            discard;
        }
        vec3 backdropDirection = normalize(ReconstructWorldPosition(v_texcoord0, depth) - u_deferredCameraPosition.xyz);
        // The sky is as far away as the fog is marched: it takes the fog (and its light shafts) in front of it.
        fogEnd = u_deferredCameraPosition.xyz + backdropDirection * u_fogVolumetric.y;
        if (u_deferredBackdropParams.x > 0.5) {
            color = KbBackdropRadiance(backdropDirection);
        } else {
            color = u_fogSky.rgb;
        }
    } else {
        color = ShadeSurface(v_texcoord0, gl_FragCoord.xy, albedo, encodedNormal, depth, fogEnd, fogged);
    }
    // One call: the volumetric fog march is the bulk of this shader, and each call is inlined.
    if (fogged) {
        color = ApplyFog(color, fogEnd, gl_FragCoord.xy);
    }
    gl_FragColor = vec4(color, 1.0);
}
