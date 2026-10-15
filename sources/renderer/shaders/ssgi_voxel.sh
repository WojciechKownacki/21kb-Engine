// Bounce light traced through the world-space voxel grid of SceneGiVoxelGrid (resolve pass only). A
// ray steps voxel by voxel until it enters an occupied one; the radiance leaving that voxel is its
// emissive colour plus its albedo lit by the scene lights; a light that casts shadows is blocked when a ray
// from the hit towards it meets another occupied voxel. Because the grid is built from the scene, not from
// the screen, the rays also find objects outside the camera's view.
SAMPLER3D(s_voxelAlbedo, 12);
SAMPLER3D(s_voxelEmissive, 13);
uniform vec4 u_voxelGrid; // xyz = world position of the minimum corner, w = voxel size
uniform vec4 u_voxelInfo; // x = 1 when enabled, y = light count (<= 8), z = grid dimension
uniform vec4 u_voxelLightDirKind[8];
uniform vec4 u_voxelLightPositionRange[8];
uniform vec4 u_voxelLightColorIntensity[8];
uniform vec4 u_voxelLightSpot[8];
uniform vec4 u_voxelLightFlags[8]; // w = 1 when the light casts shadows

bool KbVoxelOutside(vec3 cell)
{
    float dimension = u_voxelInfo.z;
    return cell.x < 0.0 || cell.y < 0.0 || cell.z < 0.0 || cell.x >= dimension || cell.y >= dimension || cell.z >= dimension;
}

float KbVoxelOccupied(vec3 cell)
{
    if (KbVoxelOutside(cell)) {
        return 0.0;
    }
    return texture3DLod(s_voxelAlbedo, (cell + vec3_splat(0.5)) / u_voxelInfo.z, 0.0).a;
}

// 1.0 when the grid blocks the way from `start` along `dir` within `maxDistance` (metres), else 0.0.
float KbVoxelBlocked(vec3 start, vec3 dir, float maxDistance)
{
    float voxelSize = u_voxelGrid.w;
    vec3 gridPos = (start - u_voxelGrid.xyz) / voxelSize;
    vec3 cell = floor(gridPos);
    vec3 stepDir = vec3(dir.x >= 0.0 ? 1.0 : -1.0, dir.y >= 0.0 ? 1.0 : -1.0, dir.z >= 0.0 ? 1.0 : -1.0);
    vec3 inverseDir = vec3(1.0, 1.0, 1.0) / max(abs(dir), vec3_splat(0.00001));
    vec3 nextBoundary = cell + max(stepDir, vec3_splat(0.0));
    vec3 tMax = abs(nextBoundary - gridPos) * inverseDir;
    for (int stepIndex = 0; stepIndex < 64; ++stepIndex) {
        if (KbVoxelOutside(cell)) {
            return 0.0;
        }
        if (KbVoxelOccupied(cell) > 0.5) {
            return 1.0;
        }
        float nextCrossing = min(min(tMax.x, tMax.y), tMax.z);
        if (nextCrossing * voxelSize > maxDistance) {
            return 0.0;
        }
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            cell.x += stepDir.x;
            tMax.x += inverseDir.x;
        } else if (tMax.y < tMax.z) {
            cell.y += stepDir.y;
            tMax.y += inverseDir.y;
        } else {
            cell.z += stepDir.z;
            tMax.z += inverseDir.z;
        }
    }
    return 0.0;
}

// Light arriving at a voxel hit, evaluated like the lighting shader's diffuse term.
vec3 KbVoxelDirect(vec3 position, vec3 normal)
{
    vec3 sum = vec3(0.0, 0.0, 0.0);
    for (int i = 0; i < 8; ++i) {
        if (float(i) >= u_voxelInfo.y) {
            break;
        }
        vec4 dirKind = u_voxelLightDirKind[i];
        vec4 positionRange = u_voxelLightPositionRange[i];
        vec4 colorIntensity = u_voxelLightColorIntensity[i];
        vec4 spot = u_voxelLightSpot[i];
        // Rectangle and disc emit from their front face only; a tube shines all around.
        if (dirKind.w > 2.5 && dirKind.w < 4.5) {
            continue; // surface emitters are not sampled here
        }
        vec3 lightVector = normalize(-dirKind.xyz);
        float attenuation = 1.0;
        float lightDistance = u_voxelInfo.z * u_voxelGrid.w * 1.8;
        if (dirKind.w > 0.5) {
            vec3 toLight = positionRange.xyz - position;
            float distanceToLight = length(toLight);
            lightVector = toLight / max(distanceToLight, 0.0001);
            lightDistance = distanceToLight;
            // Inverse square, windowed to zero at the light's range (Unity URP, Unreal, Godot 4, Verth).
            float rangeRatio = distanceToLight / max(positionRange.w, 0.0001);
            float rangeWindow = clamp(1.0 - rangeRatio * rangeRatio * rangeRatio * rangeRatio, 0.0, 1.0);
            attenuation = rangeWindow * rangeWindow / max(distanceToLight * distanceToLight, 0.0001);
            if (dirKind.w > 1.5) {
                float coneCos = dot(normalize(dirKind.xyz), -lightVector);
                float coneAttenuation = clamp((coneCos - spot.y) / max(spot.x - spot.y, 0.001), 0.0, 1.0);
                attenuation *= coneAttenuation * coneAttenuation;
            }
        }
        float facing = max(dot(normal, lightVector), 0.0);
        if (facing > 0.0 && attenuation > 0.0 && u_voxelLightFlags[i].w > 0.5) {
            // The walk stops a voxel and a half short of the light: a light next to a ceiling or a wall
            // sits in that surface's voxel and would block itself.
            // `position` lies half a voxel inside the hit voxel: start past its far side, or the voxel
            // itself blocks every light.
            attenuation *= 1.0 - KbVoxelBlocked(position + normal * (1.1 * u_voxelGrid.w), lightVector,
                max(lightDistance - 1.5 * u_voxelGrid.w, 0.0));
        }
        // Same light intensity convention as direct lighting (as in Unity: intensity 1 = albedo).
        sum += colorIntensity.rgb * (colorIntensity.a * attenuation * facing * 3.14159265);
    }
    return sum * 0.31830989;
}

// Returns incoming diffuse radiance (before multiplying by the surface albedo) and in skyVisibility
// the share of the (cosine-distributed) rays that leave the grid without hitting anything: how much
// of the sky the point sees, which scales the ambient light so the sky light is occluded.
vec3 KbVoxelGi(vec3 worldPos, vec3 normal, vec2 pixel, out float skyVisibility)
{
    skyVisibility = 0.0;
    float voxelSize = u_voxelGrid.w;
    float dimension = u_voxelInfo.z;
    vec3 total = vec3(0.0, 0.0, 0.0);
    // Start one voxel away so the ray does not hit the voxel that holds the surface itself.
    vec3 origin = worldPos + normal * (voxelSize * 0.9);
    float seed = u_giParams.z;
    for (int ray = 0; ray < 6; ++ray) {
        float rayId = float(ray) + seed * 6.0;
        vec3 dir = KbGiDirection(normal, KbGiHash(pixel, rayId), KbGiHash(pixel.yx, rayId + 0.5));
        vec3 gridPos = (origin - u_voxelGrid.xyz) / voxelSize;
        vec3 cell = floor(gridPos);
        vec3 stepDir = vec3(dir.x >= 0.0 ? 1.0 : -1.0, dir.y >= 0.0 ? 1.0 : -1.0, dir.z >= 0.0 ? 1.0 : -1.0);
        vec3 inverseDir = vec3(1.0, 1.0, 1.0) / max(abs(dir), vec3_splat(0.00001));
        vec3 nextBoundary = cell + max(stepDir, vec3_splat(0.0));
        vec3 tMax = abs(nextBoundary - gridPos) * inverseDir;
        bool hit = false;
        for (int stepIndex = 0; stepIndex < 96; ++stepIndex) {
            if (KbVoxelOutside(cell)) {
                break;
            }
            vec4 voxel = texture3DLod(s_voxelAlbedo, (cell + vec3_splat(0.5)) / dimension, 0.0);
            vec3 hitCenter = u_voxelGrid.xyz + (cell + vec3_splat(0.5)) * voxelSize;
            if (voxel.a > 0.5 && dot(hitCenter - worldPos, normal) > 0.5 * voxelSize) {
                // Surface normal from the occupancy gradient; fall back to facing the ray.
                vec3 gradient = vec3(
                    KbVoxelOccupied(cell + vec3(1.0, 0.0, 0.0)) - KbVoxelOccupied(cell - vec3(1.0, 0.0, 0.0)),
                    KbVoxelOccupied(cell + vec3(0.0, 1.0, 0.0)) - KbVoxelOccupied(cell - vec3(0.0, 1.0, 0.0)),
                    KbVoxelOccupied(cell + vec3(0.0, 0.0, 1.0)) - KbVoxelOccupied(cell - vec3(0.0, 0.0, 1.0)));
                vec3 hitNormal = length(gradient) > 0.1 ? normalize(-gradient) : -dir;
                if (dot(hitNormal, dir) > 0.0) {
                    hitNormal = -hitNormal;
                }
                vec3 emissive = texture3DLod(s_voxelEmissive, (cell + vec3_splat(0.5)) / dimension, 0.0).rgb;
                total += min(emissive + voxel.rgb * KbVoxelDirect(hitCenter - hitNormal * (0.5 * voxelSize), hitNormal), vec3_splat(8.0));
                hit = true;
                break;
            }
            if (tMax.x < tMax.y && tMax.x < tMax.z) {
                cell.x += stepDir.x;
                tMax.x += inverseDir.x;
            } else if (tMax.y < tMax.z) {
                cell.y += stepDir.y;
                tMax.y += inverseDir.y;
            } else {
                cell.z += stepDir.z;
                tMax.z += inverseDir.z;
            }
        }
        skyVisibility += hit ? 0.0 : (1.0 / 6.0);
    }
    return total * (1.0 / 6.0);
}
