#ifndef KB_HEIGHT_FOG_SH
#define KB_HEIGHT_FOG_SH

// Shared by deferred surfaces/background and forward transparent surfaces.
uniform vec4 u_fogParams; // base density, height falloff, base height, enabled
uniform vec4 u_fogColor;  // RGB inscattering, anisotropy

float FogDensity(float height)
{
    // Keep authored extreme heights/densities finite, including zero density.
    return max(u_fogParams.x, 0.0) * exp(clamp(-u_fogParams.y * (height - u_fogParams.z), -60.0, 60.0));
}

float KbFogTransmittance(vec3 camera, vec3 worldPos)
{
    if (u_fogParams.w < 0.5 || u_fogParams.x <= 0.0) return 1.0;
    vec3 ray = worldPos - camera;
    float rise = max(u_fogParams.y, 0.0) * abs(ray.y);
    // Integrate from the denser endpoint: the exponential only has a negative argument.
    // This avoids infinity times zero on long downward rays from high cameras.
    float alongRay = rise > 0.001 ? (1.0 - exp(-rise)) / rise : 1.0 - rise * 0.5;
    float opticalDepth = FogDensity(min(camera.y, worldPos.y)) * alongRay * length(ray);
    return exp(-min(opticalDepth, 80.0));
}

vec3 KbApplyHeightFog(vec3 color, vec3 camera, vec3 worldPos)
{
    float transmittance = KbFogTransmittance(camera, worldPos);
    return color * transmittance + u_fogColor.rgb * (1.0 - transmittance);
}

#endif
