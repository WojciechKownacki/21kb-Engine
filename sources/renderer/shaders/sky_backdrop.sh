#ifndef KB_SKY_BACKDROP_SH
#define KB_SKY_BACKDROP_SH

// The world backdrop behind the scene: what the deferred lighting draws where nothing was drawn,
// and what the sky light captures for reflections (fs_sky_capture.sc). One definition, so a
// reflection shows the sky the camera sees. The includer declares s_deferredBackdropEnvironment
// (the equirectangular environment map) before including this file.
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

// Radiance of the backdrop seen along a direction.
vec3 KbBackdropRadiance(vec3 direction)
{
    // Horizon to zenith by the view's elevation, as a gradient skybox: the horizon stays
    // where the camera sees it, whatever the camera's pitch.
    float vertical = clamp(direction.y - u_deferredBackdropParams.y, 0.0, 1.0);
    float blend = pow(vertical, max(u_deferredBackdropParams.z, 0.0001));
    vec3 radiance = mix(u_deferredBackdropHorizon.rgb, u_deferredBackdropZenith.rgb, blend);
    if (u_deferredBackdropParams.x > 2.5) {
        radiance = KbProceduralSky(direction, normalize(u_deferredBackdropSun.xyz), u_deferredBackdropSunColor.rgb,
            u_deferredBackdropSun.w);
    } else if (u_deferredBackdropParams.x > 1.5) {
        // bgfx's HLSL profile does not expose the GLSL atan(y, x) overload.
        // Reconstruct its quadrant explicitly so this equirectangular mapping
        // remains backend-independent.
        float longitude = atan(direction.z / max(abs(direction.x), 0.0001));
        if (direction.x < 0.0) {
            longitude += direction.z >= 0.0 ? 3.14159265 : -3.14159265;
        }
        vec2 environmentUv = vec2(longitude * 0.15915494 + 0.5, acos(clamp(direction.y, -1.0, 1.0)) * 0.31830989);
        radiance = texture2D(s_deferredBackdropEnvironment, environmentUv).rgb;
    }
    return radiance;
}

#endif // KB_SKY_BACKDROP_SH
