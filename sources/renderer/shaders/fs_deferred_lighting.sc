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
// x: 1 vertical gradient, 2 equirectangular environment map, 3 procedural sky.
// Gradient: horizon/zenith colours, y = horizon offset (sine of the elevation), z = exponent.
// Procedural sky: horizon = ground colour + sun disc (0 none, 1 simple, 2 high quality),
// zenith = sky tint + exposure, y = sun size, z = sun size convergence, w = atmosphere thickness.
uniform vec4 u_deferredBackdropHorizon;
uniform vec4 u_deferredBackdropZenith;
uniform vec4 u_deferredBackdropParams;
// Procedural sky sun: xyz toward the sun (w = 1 when the scene has one), linear colour * intensity.
uniform vec4 u_deferredBackdropSun;
uniform vec4 u_deferredBackdropSunColor;

// Procedural sky after Unity's Skybox/Procedural (O'Neil, GPU Gems 2, ch. 16).
float KbSkyScale(float inCos)
{
    float x = 1.0 - inCos;
    return 0.25 * exp(-0.00287 + x * (0.459 + x * (3.83 + x * (-6.80 + x * 5.25))));
}

vec3 KbProceduralSky(vec3 eyeRay, vec3 sunDirection, vec3 sunLight, float hasSun)
{
    const float kOuterRadius = 1.025;
    const float kOuterRadius2 = 1.050625;
    const float kInnerRadius = 1.0;
    const float kInnerRadius2 = 1.0;
    const float kCameraHeight = 0.0001;
    const float kMie = 0.0010;
    const float kSunBrightness = 20.0;
    const float kMaxScatter = 50.0;
    const float kScale = 40.0;               // 1 / (kOuterRadius - 1)
    const float kScaleDepth = 0.25;
    const float kScaleOverScaleDepth = 160.0; // kScale / kScaleDepth
    const float kMieG = -0.990;
    const float kMieG2 = 0.9801;
    const float kPi = 3.14159265;
    vec3 groundColor = u_deferredBackdropHorizon.rgb;
    float sunDisk = u_deferredBackdropHorizon.w;
    vec3 skyTint = u_deferredBackdropZenith.rgb;
    float exposure = u_deferredBackdropZenith.w;
    float sunSize = u_deferredBackdropParams.y;
    float sunSizeConvergence = u_deferredBackdropParams.z;
    float kRayleigh = mix(0.0, 0.0025, pow(max(u_deferredBackdropParams.w, 0.0), 2.5));

    // The tint steers the scattering wavelengths in gamma space, so 0.5 keeps the default sky.
    vec3 tintInGamma = pow(max(skyTint, vec3_splat(0.0)), vec3_splat(1.0 / 2.2));
    vec3 wavelength = mix(vec3(0.65, 0.57, 0.475) - vec3_splat(0.15), vec3(0.65, 0.57, 0.475) + vec3_splat(0.15),
        vec3_splat(1.0) - tintInGamma);
    vec3 invWavelength = vec3_splat(1.0) / pow(max(wavelength, vec3_splat(0.001)), vec3_splat(4.0));
    float krESun = kRayleigh * kSunBrightness;
    float kr4Pi = kRayleigh * 4.0 * kPi;
    float kmESun = kMie * kSunBrightness;
    float km4Pi = kMie * 4.0 * kPi;
    vec3 cameraPos = vec3(0.0, kInnerRadius + kCameraHeight, 0.0);
    vec3 cIn;
    vec3 cOut;
    if (eyeRay.y >= 0.0) {
        float far = sqrt(kOuterRadius2 + kInnerRadius2 * eyeRay.y * eyeRay.y - kInnerRadius2) - kInnerRadius * eyeRay.y;
        float height = kInnerRadius + kCameraHeight;
        float depth = exp(kScaleOverScaleDepth * (-kCameraHeight));
        float startAngle = dot(eyeRay, cameraPos) / height;
        float startOffset = depth * KbSkyScale(startAngle);
        float sampleLength = far / 2.0;
        float scaledLength = sampleLength * kScale;
        vec3 sampleRay = eyeRay * sampleLength;
        vec3 samplePoint = cameraPos + sampleRay * 0.5;
        vec3 frontColor = vec3(0.0, 0.0, 0.0);
        for (int i = 0; i < 2; i++) {
            float sampleHeight = length(samplePoint);
            float sampleDepth = exp(kScaleOverScaleDepth * (kInnerRadius - sampleHeight));
            float lightAngle = dot(sunDirection, samplePoint) / sampleHeight;
            float cameraAngle = dot(eyeRay, samplePoint) / sampleHeight;
            float scatter = startOffset + sampleDepth * (KbSkyScale(lightAngle) - KbSkyScale(cameraAngle));
            vec3 attenuate = exp(-clamp(scatter, 0.0, kMaxScatter) * (invWavelength * kr4Pi + km4Pi));
            frontColor += attenuate * (sampleDepth * scaledLength);
            samplePoint += sampleRay;
        }
        cIn = frontColor * (invWavelength * krESun);
        cOut = frontColor * kmESun;
    } else {
        float far = (-kCameraHeight) / min(-0.001, eyeRay.y);
        vec3 pos = cameraPos + far * eyeRay;
        float depth = exp((-kCameraHeight) * (1.0 / kScaleDepth));
        float cameraScale = KbSkyScale(dot(-eyeRay, pos));
        float lightScale = KbSkyScale(dot(sunDirection, pos));
        float cameraOffset = depth * cameraScale;
        float temp = lightScale + cameraScale;
        float scaledLength = far * kScale;
        vec3 samplePoint = cameraPos + eyeRay * far * 0.5;
        float sampleDepth = exp(kScaleOverScaleDepth * (kInnerRadius - length(samplePoint)));
        float scatter = sampleDepth * temp - cameraOffset;
        vec3 attenuate = exp(-clamp(scatter, 0.0, kMaxScatter) * (invWavelength * kr4Pi + km4Pi));
        cIn = attenuate * (sampleDepth * scaledLength) * (invWavelength * krESun + kmESun);
        cOut = clamp(attenuate, 0.0, 1.0);
    }
    float eyeCos = dot(sunDirection, eyeRay);
    vec3 sky = exposure * (cIn * (0.75 + 0.75 * eyeCos * eyeCos));
    vec3 ground = exposure * (cIn + groundColor * cOut);
    vec3 color = mix(sky, ground, clamp(-eyeRay.y / 0.02, 0.0, 1.0));
    if (eyeRay.y > 0.0 && hasSun > 0.5 && sunDisk > 0.5) {
        // Bright even under a dim light, matching a specular highlight of the same sun.
        float lightIntensity = clamp(length(sunLight), 0.25, 1.0);
        if (sunDisk < 1.5) {
            vec3 sunColor = 27.0 * clamp(cOut * 8000.0, 0.0, 1.0) * sunLight / lightIntensity;
            float spot = 1.0 - smoothstep(0.0, sunSize, length(sunDirection - eyeRay));
            color += sunColor * (spot * spot);
        } else {
            vec3 sunColor = 15.0 * clamp(cOut, 0.0, 1.0) * sunLight / lightIntensity;
            float focused = pow(clamp(eyeCos, 0.0, 1.0), sunSizeConvergence);
            float mie = max(1.0 + kMieG2 - 2.0 * kMieG * (-focused), 0.0001);
            mie = max(pow(mie, pow(max(sunSize, 0.0), 0.65) * 10.0), 0.0001);
            color += sunColor * (1.5 * ((1.0 - kMieG2) / (2.0 + kMieG2)) * (1.0 + focused * focused) / mie);
        }
    }
    return color;
}

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
    vec3 specularEnv = EnvironmentColor(reflectionDir) * fresnel * specularEnergy * u_deferredEnvironmentParams.z;
    vec3 specularSsr = reflection.rgb * fresnel * specularEnergy * u_ssrParams.x;
    return diffuseEnv + mix(specularEnv, specularSsr, reflection.a);
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
        vec3 backdropDirection = normalize(ReconstructWorldPosition(v_texcoord0, depth) - u_deferredCameraPosition.xyz);
        if (u_deferredBackdropParams.x > 2.5) {
            gl_FragColor = vec4(KbProceduralSky(backdropDirection, normalize(u_deferredBackdropSun.xyz),
                u_deferredBackdropSunColor.rgb, u_deferredBackdropSun.w), 1.0);
            return;
        }
        if (u_deferredBackdropParams.x > 1.5) {
            vec3 direction = backdropDirection;
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
            // Horizon to zenith by the view's elevation, as a gradient skybox: the horizon stays
            // where the camera sees it, whatever the camera's pitch.
            float vertical = clamp(backdropDirection.y - u_deferredBackdropParams.y, 0.0, 1.0);
            float blend = pow(vertical, max(u_deferredBackdropParams.z, 0.0001));
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
        vec4 coarserCoord;
        vec4 shadowCoord = KbResolveShadowCascade(worldPos, coarserCoord);
        if (shadowCoord.w > 0.5) {
            shadowVisible = SampleShadowVisibility(shadowCoord.xyz);
            if (coarserCoord.w > 0.0) {
                shadowVisible = mix(shadowVisible, SampleShadowVisibility(coarserCoord.xyz), coarserCoord.w);
            }
        }
    }

    // Screen-space results of the previous frame: bounce light, ambient visibility, reflections.
    vec3 giAccumulated;
    float aoAccumulated;
    KbScreenSpaceAccumulated(worldPos, giAccumulated, aoAccumulated);
    aoAccumulated = mix(1.0, aoAccumulated, step(0.0001, u_aoParams.x));
    vec4 reflection = KbScreenSpaceReflection(worldPos, normal, viewDir, roughness, gl_FragCoord.xy);
    vec3 lighting = EvaluateEnvironment(normal, viewDir, albedo.rgb, metallic, roughness, specular, occlusion * aoAccumulated, reflection);

    vec2 lightList = KbLightGridList(worldPos, u_deferredLightParams.x);
    for (int entry = 0; entry < int(lightList.y); ++entry) {
        {
            int lightIndex = KbLightGridIndex(lightList, entry);
            vec3 directLight = EvaluateSceneLight(lightIndex, normal, viewDir, worldPos, albedo.rgb, metallic, roughness, specular, occlusion);
            lighting += directLight * (lightIndex == 0 ? shadowVisible : 1.0) * KbPointShadowFactor(lightIndex, worldPos);
        }
    }

    if (u_giParams.x > 0.0) {
        lighting += giAccumulated * albedo.rgb * ((1.0 - metallic) * occlusion * aoAccumulated * u_giParams.x);
    }

    gl_FragColor = vec4(lighting + surface.rgb, 1.0);
}
