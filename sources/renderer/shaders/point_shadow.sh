// Point-light cube shadows shared by the forward and deferred lighting shaders.
// Face f of light slot s lives in the atlas tile at column f, row s. The face table must match
// kFaces in PointShadowPassPlanner.cpp; faces are rendered with a perspective frustum that is a
// little wider than 90 degrees (u_pointShadowAtlas.w = tan(half fov)).
// A spot light (u_pointShadowSpot[s].w > 0) uses a single frustum along its axis in column 0.
SAMPLER2D(s_pointShadowMap, 9);
uniform vec4 u_pointShadowLight[4]; // xyz = light position, w = packed light slot (< 0 = none)
uniform vec4 u_pointShadowDepth[4]; // x = near plane, y = far plane, z = depth bias (m)
uniform vec4 u_pointShadowSpot[4];  // xyz = spot axis, w = tan(half fov) (0 = cube light)
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
float KbPointShadowFactor(int lightSlot, vec3 worldPos)
{
    int count = int(u_pointShadowInfo.x);
    for (int s = 0; s < 4; ++s) {
        if (s >= count) {
            break;
        }
        if (int(u_pointShadowLight[s].w + 0.5) != lightSlot) {
            continue;
        }
        vec3 d = worldPos - u_pointShadowLight[s].xyz;
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
        float lit = 0.0;
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                lit += KbPointShadowTap(tilePx + vec2(float(x), float(y)), tileOrigin, z, nearPlane, farPlane, bias);
            }
        }
        return mix(1.0, 1.0 - u_pointShadowInfo.z, lit * 0.11111111);
    }
    return 1.0;
}
