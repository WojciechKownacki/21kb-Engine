// Point-light cube shadows shared by the forward and deferred lighting shaders.
// Face f of light slot s lives in the atlas tile at column f, row s. The face table must match
// kFaces in PointShadowPassPlanner.cpp; faces are rendered with a perspective frustum that is a
// little wider than 90 degrees (u_pointShadowAtlas.w = tan(half fov)).
// A spot light (u_pointShadowSpot[s].w > 0) uses a single frustum along its axis in column 0.
// Graph material shaders define KB_POINT_SHADOW_GRAPH: they own the stages from 6 up for their own
// textures. (The stage has to be a literal here; some backends reject a macro as the register.)
#if defined(KB_POINT_SHADOW_GRAPH)
SAMPLER2D(s_pointShadowMap, 1);
#else
SAMPLER2D(s_pointShadowMap, 9);
#endif
uniform vec4 u_pointShadowLight[32]; // xyz = light position, w = packed light slot (< 0 = none)
uniform vec4 u_pointShadowDepth[32]; // x = near plane, y = far plane, z = depth bias (m), w = source radius (m)
uniform vec4 u_pointShadowStrength[8]; // the shadow strength of light s in component s % 4 of entry s / 4
uniform vec4 u_pointShadowSpot[32];  // xyz = spot axis, w = tan(half fov) (0 = cube light)
uniform vec4 u_pointShadowAtlas;    // x,y = 1 / atlas size, z = tile px, w = tan(half fov)
uniform vec4 u_pointShadowInfo;     // x = light count, y = texture v scale (+-0.5), z = strength

// Reverse-z perspective depth back to distance along the face axis.
float KbPointShadowLinearDepth(float stored, float nearPlane, float farPlane)
{
    return nearPlane * farPlane / (nearPlane + stored * (farPlane - nearPlane));
}

float KbPointShadowTap(vec2 tilePx, vec2 tileOrigin, float fragmentDepth, float nearPlane, float farPlane, float bias)
{
    float tile = u_pointShadowAtlas.z;
    vec2 clamped = clamp(tilePx, vec2_splat(0.5), vec2_splat(tile - 0.5));
    vec2 uv = (tileOrigin + clamped) * u_pointShadowAtlas.xy;
    float stored = texture2DLod(s_pointShadowMap, uv, 0.0).x;
    return fragmentDepth - bias > KbPointShadowLinearDepth(stored, nearPlane, farPlane) ? 1.0 : 0.0;
}

// Returns 1.0 for lit and (1 - strength) for fully shadowed. lightSlot is the packed light index.
float KbPointShadowFactor(int lightSlot, vec3 worldPos, vec3 normal)
{
    int count = int(u_pointShadowInfo.x);
    for (int s = 0; s < 32; ++s) {
        if (s >= count) {
            break;
        }
        if (int(u_pointShadowLight[s].w + 0.5) != lightSlot) {
            continue;
        }
        vec3 d = worldPos - u_pointShadowLight[s].xyz;
        // Normal offset of 1.5 shadow texels at this distance (normal bias): no acne at grazing angles.
        d += normal * (length(d) * 3.0 * u_pointShadowAtlas.w / max(u_pointShadowAtlas.z, 1.0));
        vec3 a = abs(d);
        vec3 dir;
        vec3 up;
        float face;
        float tanHalf = u_pointShadowAtlas.w;
        if (u_pointShadowSpot[s].w > 0.0) {
            dir = u_pointShadowSpot[s].xyz;
            // The helper axis must match PointShadowPassPlanner.cpp.
            vec3 helper = abs(dir.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
            up = cross(dir, normalize(cross(helper, dir)));
            face = 0.0;
            tanHalf = u_pointShadowSpot[s].w;
        } else if (a.x >= a.y && a.x >= a.z) {
            float sgn = d.x >= 0.0 ? 1.0 : -1.0;
            dir = vec3(sgn, 0.0, 0.0);
            up = vec3(0.0, 1.0, 0.0);
            face = d.x >= 0.0 ? 0.0 : 1.0;
        } else if (a.y >= a.z) {
            float sgn = d.y >= 0.0 ? 1.0 : -1.0;
            dir = vec3(0.0, sgn, 0.0);
            up = vec3(0.0, 0.0, -sgn);
            face = d.y >= 0.0 ? 2.0 : 3.0;
        } else {
            float sgn = d.z >= 0.0 ? 1.0 : -1.0;
            dir = vec3(0.0, 0.0, sgn);
            up = vec3(0.0, 1.0, 0.0);
            face = d.z >= 0.0 ? 4.0 : 5.0;
        }
        vec3 right = cross(up, dir);
        float z = dot(d, dir);
        float nearPlane = u_pointShadowDepth[s].x;
        float farPlane = u_pointShadowDepth[s].y;
        if (z <= nearPlane || z >= farPlane) {
            return 1.0;
        }
        vec2 ndc = vec2(dot(d, right), dot(d, up)) / (z * tanHalf);
        float tile = u_pointShadowAtlas.z;
        vec2 tilePx = vec2(0.5 + 0.5 * ndc.x, 0.5 + u_pointShadowInfo.y * ndc.y) * tile;
        vec2 tileOrigin = vec2(face * tile, float(s) * tile);
        float bias = u_pointShadowDepth[s].z + z * 0.012;
        int entry = int(float(s) * 0.25);
        vec4 strengths = u_pointShadowStrength[entry];
        int lane = s - entry * 4;
        float strength = u_pointShadowInfo.z * (lane == 0 ? strengths.x : (lane == 1 ? strengths.y : (lane == 2 ? strengths.z : strengths.w)));
        float lit = 0.0;
        float sourceRadius = u_pointShadowDepth[s].w;
        // Pixels of this tile one metre across the light's view spans at the receiver.
        float pxPerMetre = 0.5 * tile / (z * tanHalf);
        if (sourceRadius > 0.0) {
            // Soft shadows from the light's size (PCSS): the occluders' mean distance sets the penumbra.
            float turn = 6.2831853 * fract(52.9829189 * fract(dot(worldPos, vec3(0.0671, 0.0058, 0.0372))));
            float search = clamp(sourceRadius * pxPerMetre, 1.0, 12.0);
            float occluderSum = 0.0;
            float occluders = 0.0;
            for (int i = 0; i < 12; ++i) {
                float r = sqrt((float(i) + 0.5) / 12.0);
                float a = float(i) * 2.39996323 + turn;
                vec2 px = clamp(tilePx + vec2(cos(a), sin(a)) * (r * search), vec2_splat(0.5), vec2_splat(tile - 0.5));
                float stored = KbPointShadowLinearDepth(texture2DLod(s_pointShadowMap, (tileOrigin + px) * u_pointShadowAtlas.xy, 0.0).x, nearPlane, farPlane);
                occluderSum += stored < z - bias ? stored : 0.0;
                occluders += stored < z - bias ? 1.0 : 0.0;
            }
            float occluder = max(occluderSum / max(occluders, 1.0), nearPlane);
            float penumbra = clamp(sourceRadius * (z - occluder) / occluder * pxPerMetre, 1.0, 16.0);
            for (int j = 0; j < 12; ++j) {
                float r = sqrt((float(j) + 0.5) / 12.0);
                float a = float(j) * 2.39996323 + turn + 1.3;
                lit += KbPointShadowTap(tilePx + vec2(cos(a), sin(a)) * (r * penumbra), tileOrigin, z, nearPlane, farPlane, bias);
            }
            lit = occluders > 0.0 ? lit * (9.0 / 12.0) : 0.0;
        } else {
            for (int y = -1; y <= 1; ++y) {
                for (int x = -1; x <= 1; ++x) {
                    lit += KbPointShadowTap(tilePx + vec2(float(x), float(y)), tileOrigin, z, nearPlane, farPlane, bias);
                }
            }
        }
        return mix(1.0, 1.0 - strength, lit * 0.11111111);
    }
    return 1.0;
}
