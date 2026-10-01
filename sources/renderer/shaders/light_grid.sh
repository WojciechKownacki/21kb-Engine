#ifndef KB_LIGHT_GRID_SH
#define KB_LIGHT_GRID_SH

#if defined(KB_LIGHT_GRID_GRAPH)
SAMPLER2D(s_sceneLightGrid, 0);
#elif defined(KB_LIGHT_GRID_DEFERRED)
SAMPLER2D(s_sceneLightGrid, 7);
#else
SAMPLER2D(s_sceneLightGrid, 6);
#endif
uniform vec4 u_sceneLightGridMinimum;
uniform vec4 u_sceneLightGridInverseCellSize;
uniform vec4 u_sceneLightGridDimensions;
// Header base, index base, inverse atlas width, inverse atlas height.
uniform vec4 u_sceneLightGridLayout;

vec4 KbLightGridTexel(float address)
{
    vec2 uv = vec2(fract((address + 0.5) * u_sceneLightGridLayout.z),
        (floor(address * u_sceneLightGridLayout.z) + 0.5) * u_sceneLightGridLayout.w);
    return texture2DLod(s_sceneLightGrid, uv, 0.0);
}

vec2 KbLightGridList(vec3 worldPos, float uniformCount)
{
    if (u_sceneLightGridDimensions.w < 0.5) return vec2(0.0, uniformCount);
    vec3 cell = floor((worldPos - u_sceneLightGridMinimum.xyz) * u_sceneLightGridInverseCellSize.xyz);
    float address = 0.0;
    if (all(greaterThanEqual(cell, vec3_splat(0.0))) && all(lessThan(cell, u_sceneLightGridDimensions.xyz)))
        address = 1.0 + cell.x + u_sceneLightGridDimensions.x * (cell.y + u_sceneLightGridDimensions.y * cell.z);
    return KbLightGridTexel(u_sceneLightGridLayout.x + address).xy;
}

int KbLightGridIndex(vec2 list, int entry)
{
    if (u_sceneLightGridDimensions.w < 0.5) return entry;
    float address = list.x + float(entry);
    vec4 indices = KbLightGridTexel(u_sceneLightGridLayout.y + floor(address * 0.25));
    float lane = mod(address, 4.0);
    return int(lane < 0.5 ? indices.x : (lane < 1.5 ? indices.y : (lane < 2.5 ? indices.z : indices.w)));
}

vec3 KbSurfaceEmitterSamplePosition(vec3 center, vec3 normal, vec3 right, float kind, vec2 dimensions, vec3 worldPos)
{
    if (kind < 2.5) return center;
    vec3 localRight = normalize(right);
    vec3 localUp = normalize(cross(normal, localRight));
    vec3 fromCenter = worldPos - center;
    if (kind < 3.5)
        return center + localRight * clamp(dot(fromCenter, localRight), -dimensions.x * 0.5, dimensions.x * 0.5)
            + localUp * clamp(dot(fromCenter, localUp), -dimensions.y * 0.5, dimensions.y * 0.5);
    if (kind < 4.5) {
        vec3 planar = localRight * dot(fromCenter, localRight) + localUp * dot(fromCenter, localUp);
        return center + planar * min(1.0, dimensions.x * 0.5 / max(length(planar), 0.0001));
    }
    vec3 axisPoint = center + localRight * clamp(dot(fromCenter, localRight), -dimensions.x * 0.5, dimensions.x * 0.5);
    vec3 radial = worldPos - axisPoint;
    return axisPoint + radial * min(1.0, dimensions.y * 0.5 / max(length(radial), 0.0001));
}

#endif
