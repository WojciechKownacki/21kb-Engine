// Cascaded shadow lookup shared by the forward and deferred lighting shaders.
// All cascades live in one 2x2 atlas; cascade 0 is the finest, the last one the widest.
uniform mat4 u_shadowCascadeViewProj[4];
uniform vec4 u_shadowCascadeAtlas[4]; // xy = tile offset, z = tile scale, w = penumbra uv per unit of depth
uniform vec4 u_shadowCascadeInfo;     // x = cascade count, y = tile-uv edge margin, z = tile-uv blend width, w = 1 for the wide soft kernel
uniform vec4 u_shadowCascadeBias[4];  // x = depth bias (shadow depth units), y = normal offset (m)

// Returns the atlas shadow coordinate (xy uv, z depth, already biased) of the finest cascade that contains
// worldPos moved along `normal` by the cascade's normal offset, with w = 1 + the penumbra the light's source angle casts per unit of depth between
// occluder and receiver (in atlas uv; 0 = a hard light); w = 0 when no cascade covers it.
// Near the outer edge of that cascade `coarser` holds the next cascade's coordinate with the weight
// of its visibility in w (rising to 1 at the edge); w = 0 when no blending is needed.
vec4 KbResolveShadowCascade(vec3 worldPos, vec3 normal, out vec4 coarser)
{
    coarser = vec4(0.0, 0.0, 0.0, 0.0);
    int count = int(u_shadowCascadeInfo.x);
    for (int i = 0; i < 4; ++i) {
        if (i >= count) {
            break;
        }
        vec4 clip = mul(u_shadowCascadeViewProj[i], vec4(worldPos + normal * u_shadowCascadeBias[i].y, 1.0));
        vec3 c = clip.xyz / max(clip.w, 0.0001);
        // The widest cascade takes everything inside its bounds; finer ones keep a margin so
        // the PCF kernel never reads a neighbouring tile.
        float margin = (i == count - 1) ? 0.0 : u_shadowCascadeInfo.y;
        if (c.x >= margin && c.x <= 1.0 - margin &&
            c.y >= margin && c.y <= 1.0 - margin &&
            c.z >= 0.0 && c.z <= 1.0) {
            float band = u_shadowCascadeInfo.z;
            if (i < count - 1 && band > 0.0) {
                float edge = min(min(c.x - margin, 1.0 - margin - c.x), min(c.y - margin, 1.0 - margin - c.y));
                float weight = 1.0 - smoothstep(0.0, band, edge);
                vec4 nextClip = mul(u_shadowCascadeViewProj[i + 1], vec4(worldPos + normal * u_shadowCascadeBias[i + 1].y, 1.0));
                vec3 n = nextClip.xyz / max(nextClip.w, 0.0001);
                float nextMargin = (i + 1 == count - 1) ? 0.0 : u_shadowCascadeInfo.y;
                if (weight > 0.0 && n.x >= nextMargin && n.x <= 1.0 - nextMargin &&
                    n.y >= nextMargin && n.y <= 1.0 - nextMargin && n.z >= 0.0 && n.z <= 1.0) {
                    coarser = vec4(n.xy * u_shadowCascadeAtlas[i + 1].z + u_shadowCascadeAtlas[i + 1].xy, n.z + u_shadowCascadeBias[i + 1].x, weight);
                }
            }
            return vec4(c.xy * u_shadowCascadeAtlas[i].z + u_shadowCascadeAtlas[i].xy, c.z + u_shadowCascadeBias[i].x, 1.0 + u_shadowCascadeAtlas[i].w);
        }
    }
    return vec4(0.0, 0.0, 0.0, 0.0);
}

#if defined(KB_SHADOW_COMPARE)
// The programs built with KB_SHADOW_COMPARE read the shadow maps through comparison samplers: the hardware tests the
// four nearest texels against the receiver's depth and filters the four results, so a single tap is already a soft edge.
// The maps keep reversed depth, so the "less or equal" test passes where the receiver lies behind the occluder, which is
// the shadowed share. Soft shadows from a source size or a PCSS filter need the stored depth itself and run in the
// programs built without it.
// `mode` is the shadow filter (below 1.5 = hard), `wide` picks the soft kernel: 0 = four taps half a texel off the
// centre (about 3x3 texels), 1 = nine taps a texel apart weighted 1-2-1 (about 5x5 texels).
float KbCompareShadow(sampler2DShadow shadowMap, vec3 coord, float texel, float mode, float wide)
{
    float shadowed = 0.0;
    if (mode < 1.5) {
        shadowed = shadow2D(shadowMap, coord);
    } else if (wide < 0.5) {
        float h = 0.5 * texel;
        shadowed = 0.25 * (shadow2D(shadowMap, vec3(coord.xy + vec2(-h, -h), coord.z)) +
            shadow2D(shadowMap, vec3(coord.xy + vec2(h, -h), coord.z)) +
            shadow2D(shadowMap, vec3(coord.xy + vec2(-h, h), coord.z)) +
            shadow2D(shadowMap, vec3(coord.xy + vec2(h, h), coord.z)));
    } else {
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                float weight = (2.0 - abs(float(x))) * (2.0 - abs(float(y)));
                shadowed += weight * shadow2D(shadowMap, vec3(coord.xy + vec2(float(x), float(y)) * texel, coord.z));
            }
        }
        shadowed *= 0.0625;
    }
    return shadowed;
}
#endif
