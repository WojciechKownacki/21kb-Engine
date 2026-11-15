#ifndef KB_LIGHT_PROBES_SH
#define KB_LIGHT_PROBES_SH

// The baked light probe grid (SceneLightProbeTexture): the 27 SH L2 floats of each probe, coefficient by
// coefficient and r, g, b within one, packed into 7 RGBA tiles side by side; the grid's z layers stack down
// the texture. The stage has to be a literal. Graph materials own the stages from 6 up and take the free 2. The
// deferred lighting shader has no stage left: it reads the grid through the sampler of the screen-space
// reflections' lit colour (KB_LIGHT_PROBES_DEFERRED, after ssr.sh), a stage the frame hands over only while
// it has no screen-space reflections (UsesBakedLightProbes).
#if defined(KB_LIGHT_PROBES_DEFERRED)
#define KbLightProbeTexel(uv) texture2DLod(s_ssrLit, uv, 0.0)
#else
#if defined(KB_LIGHT_PROBES_GRAPH)
SAMPLER2D(s_lightProbes, 2);
#else
SAMPLER2D(s_lightProbes, 14);
#endif
#define KbLightProbeTexel(uv) texture2DLod(s_lightProbes, uv, 0.0)
#endif
// Grid origin (xyz) and inverse spacing (w); probe counts (xyz) and w 1 when the grid replaces the flat ambient.
uniform vec4 u_lightProbeGrid;
uniform vec4 u_lightProbeSize;

vec4 KbLightProbeSlot(vec3 cell, float slot)
{
    // Filtering stays inside the slot's tile; the two layers around the position blend by hand.
    vec3 size = u_lightProbeSize.xyz;
    float layer = clamp(cell.z, 0.5, size.z - 0.5) - 0.5;
    float layer0 = floor(layer);
    float layer1 = min(layer0 + 1.0, size.z - 1.0);
    float x = slot * size.x + clamp(cell.x, 0.5, size.x - 0.5);
    float y = clamp(cell.y, 0.5, size.y - 0.5);
    vec2 atlas = vec2(size.x * 7.0, size.y * size.z);
    vec4 lower = KbLightProbeTexel(vec2(x, layer0 * size.y + y) / atlas);
    vec4 upper = KbLightProbeTexel(vec2(x, layer1 * size.y + y) / atlas);
    return mix(lower, upper, layer - layer0);
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

// The diffuse light of the surroundings at a surface: the baked grid where the frame has one, else `flatAmbient`.
vec3 KbDiffuseAmbient(vec3 worldPos, vec3 normal, vec3 flatAmbient)
{
    vec3 ambient = flatAmbient;
    if (u_lightProbeSize.w > 0.5) {
        ambient = KbLightProbeIrradiance(worldPos, normal);
    }
    return ambient;
}

#endif
