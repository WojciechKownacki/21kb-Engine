// Bounce light traced through the world-space voxel grid of SceneGiVoxelGrid (resolve pass only). A
// ray steps voxel by voxel until it enters an occupied one; the radiance leaving that voxel is its
// emissive colour plus its albedo lit by the scene lights (no shadows). Because the grid is built from
// the scene, not from the screen, the rays also find objects outside the camera's view.
SAMPLER3D(s_voxelAlbedo, 12);
SAMPLER3D(s_voxelEmissive, 13);
uniform vec4 u_voxelGrid; // xyz = world position of the minimum corner, w = voxel size
uniform vec4 u_voxelInfo; // x = 1 when enabled, y = light count (<= 8), z = grid dimension
uniform vec4 u_voxelLightDirKind[8];
uniform vec4 u_voxelLightPositionRange[8];
uniform vec4 u_voxelLightColorIntensity[8];
uniform vec4 u_voxelLightSpot[8];

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
        if (dirKind.w > 2.5) {
            continue; // surface emitters are not sampled here
        }
        vec3 lightVector = normalize(-dirKind.xyz);
        float attenuation = 1.0;
        if (dirKind.w > 0.5) {
            vec3 toLight = positionRange.xyz - position;
            float distanceToLight = length(toLight);
            lightVector = toLight / max(distanceToLight, 0.0001);
            float rangeAttenuation = clamp(1.0 - distanceToLight / max(positionRange.w, 0.0001), 0.0, 1.0);
            attenuation = rangeAttenuation * rangeAttenuation;
            if (dirKind.w > 1.5) {
                float coneCos = dot(normalize(dirKind.xyz), -lightVector);
                float coneAttenuation = clamp((coneCos - spot.y) / max(spot.x - spot.y, 0.001), 0.0, 1.0);
                attenuation *= coneAttenuation * coneAttenuation;
            }
        }
        sum += colorIntensity.rgb * (colorIntensity.a * attenuation * max(dot(normal, lightVector), 0.0));
    }
    return sum * 0.31830989;
}

// Returns incoming diffuse radiance (before multiplying by the surface albedo).
vec3 KbVoxelGi(vec3 worldPos, vec3 normal, vec2 pixel)
{
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
    }
    return total * (1.0 / 6.0);
}
