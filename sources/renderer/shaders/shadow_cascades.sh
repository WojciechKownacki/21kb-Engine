// Cascaded shadow lookup shared by the forward and deferred lighting shaders.
// All cascades live in one 2x2 atlas; cascade 0 is the finest, the last one the widest.
uniform mat4 u_shadowCascadeViewProj[4];
uniform vec4 u_shadowCascadeAtlas[4]; // xy = tile offset, z = tile scale
uniform vec4 u_shadowCascadeInfo;     // x = cascade count, y = tile-uv edge margin

// Returns the atlas shadow coordinate (xy uv, z depth) of the finest cascade that contains
// worldPos, with w = 1; w = 0 when no cascade covers it (outside the shadow distance).
vec4 KbResolveShadowCascade(vec3 worldPos)
{
    int count = int(u_shadowCascadeInfo.x);
    for (int i = 0; i < 4; ++i) {
        if (i >= count) {
            break;
        }
        vec4 clip = mul(u_shadowCascadeViewProj[i], vec4(worldPos, 1.0));
        vec3 c = clip.xyz / max(clip.w, 0.0001);
        // The widest cascade takes everything inside its bounds; finer ones keep a margin so
        // the PCF kernel never reads a neighbouring tile.
        float margin = (i == count - 1) ? 0.0 : u_shadowCascadeInfo.y;
        if (c.x >= margin && c.x <= 1.0 - margin &&
            c.y >= margin && c.y <= 1.0 - margin &&
            c.z >= 0.0 && c.z <= 1.0) {
            return vec4(c.xy * u_shadowCascadeAtlas[i].z + u_shadowCascadeAtlas[i].xy, c.z, 1.0);
        }
    }
    return vec4(0.0, 0.0, 0.0, 0.0);
}
