#ifndef KB_LIGHT_PROBES_SH
#define KB_LIGHT_PROBES_SH

// The baked light probe grid (SceneLightProbeTexture): the 27 SH L2 floats of each probe, coefficient by
// coefficient and r, g, b within one, packed into 7 RGBA slots stacked along z of one 3D texture.
SAMPLER3D(s_lightProbes, 14);
// Grid origin (xyz) and inverse spacing (w); probe counts (xyz) and w 1 when the grid replaces the flat ambient.
uniform vec4 u_lightProbeGrid;
uniform vec4 u_lightProbeSize;

vec4 KbLightProbeSlot(vec3 cell, float slot)
{
    // Keep the filter inside the slot's own layers.
    vec3 texel = vec3(cell.xy, clamp(cell.z, 0.5, u_lightProbeSize.z - 0.5) + slot * u_lightProbeSize.z);
    return texture3DLod(s_lightProbes, texel / vec3(u_lightProbeSize.xy, u_lightProbeSize.z * 7.0), 0.0);
}

// Cosine-convolved SH irradiance for a normal, as EvaluateShL2Irradiance (StaticLightmapBaker.cpp): same band
// order and basis, so albedo * result is the lit colour.
vec3 KbLightProbeIrradiance(vec3 worldPos, vec3 normal)
{
    vec3 cell = (worldPos - u_lightProbeGrid.xyz) * u_lightProbeGrid.w + vec3_splat(0.5);
    vec4 t0 = KbLightProbeSlot(cell, 0.0);
    vec4 t1 = KbLightProbeSlot(cell, 1.0);
    vec4 t2 = KbLightProbeSlot(cell, 2.0);
    vec4 t3 = KbLightProbeSlot(cell, 3.0);
    vec4 t4 = KbLightProbeSlot(cell, 4.0);
    vec4 t5 = KbLightProbeSlot(cell, 5.0);
    vec4 t6 = KbLightProbeSlot(cell, 6.0);
    vec3 n = normalize(normal);
    vec3 sum = t0.xyz * 0.282095;
    sum += vec3(t0.w, t1.xy) * (0.66666667 * 0.488603 * n.y);
    sum += vec3(t1.zw, t2.x) * (0.66666667 * 0.488603 * n.z);
    sum += t2.yzw * (0.66666667 * 0.488603 * n.x);
    sum += t3.xyz * (0.25 * 1.092548 * n.x * n.y);
    sum += vec3(t3.w, t4.xy) * (0.25 * 1.092548 * n.y * n.z);
    sum += vec3(t4.zw, t5.x) * (0.25 * 0.315392 * (3.0 * n.z * n.z - 1.0));
    sum += t5.yzw * (0.25 * 1.092548 * n.x * n.z);
    sum += t6.xyz * (0.25 * 0.546274 * (n.x * n.x - n.y * n.y));
    return max(sum, vec3_splat(0.0));
}

#endif
