// Screen-space reflections for the deferred lighting shader: a ray along the mirrored view direction is
// marched against the G-buffer depth and the radiance at the hit is read from the previous frame's lit
// colour (reprojected by u_giPrevViewProj). Needs ReconstructWorldPosition, s_gbufferDepth, s_gbufferNormal,
// u_deferredCameraPosition and ssgi.sh.
SAMPLER2D(s_ssrLit, 11);
uniform vec4 u_ssrParams; // x = intensity (0 = off), y = ray length (m), w = hit thickness factor

// rgb = reflected radiance, a = confidence (0 when the ray found nothing usable).
vec4 KbScreenSpaceReflection(vec3 worldPos, vec3 normal, vec3 viewDir, float roughness, vec2 pixel)
{
    if (u_ssrParams.x <= 0.0 || u_giTemporal.z < 0.5 || roughness > 0.7) {
        return vec4(0.0, 0.0, 0.0, 0.0);
    }
    vec3 dir = reflect(-viewDir, normal);
    vec3 origin = worldPos + normal * 0.03;
    float jitter = KbGiHash(pixel, u_giParams.z);
    for (int step = 0; step < 32; ++step) {
        float f = (float(step) + 0.5 + jitter) * 0.03125;
        float t = u_ssrParams.y * f * f;
        vec3 p = origin + dir * t;
        vec4 clip = mul(u_giViewProj, vec4(p, 1.0));
        if (clip.w <= 0.0001) {
            break;
        }
        vec2 uv = KbGiUv(clip);
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
            break;
        }
        vec4 hitNormal = texture2DLod(s_gbufferNormal, uv, 0.0);
        if (hitNormal.a < 0.5) {
            continue;
        }
        vec3 q = ReconstructWorldPosition(uv, texture2DLod(s_gbufferDepth, uv, 0.0).x);
        float delta = distance(u_deferredCameraPosition.xyz, p) - distance(u_deferredCameraPosition.xyz, q);
        if (delta > 0.0 && delta < u_ssrParams.w * max(t, 0.25)) {
            if (dot(normalize(hitNormal.xyz * 2.0 - 1.0), dir) >= 0.0) {
                break;
            }
            vec4 prevClip = mul(u_giPrevViewProj, vec4(q, 1.0));
            if (prevClip.w <= 0.0001) {
                break;
            }
            vec2 prevUv = KbGiUv(prevClip);
            if (prevUv.x < 0.0 || prevUv.x > 1.0 || prevUv.y < 0.0 || prevUv.y > 1.0) {
                break;
            }
            vec2 edge = min(uv, vec2(1.0, 1.0) - uv);
            float screenFade = smoothstep(0.0, 0.08, min(edge.x, edge.y));
            float roughnessFade = 1.0 - smoothstep(0.35, 0.7, roughness);
            return vec4(min(texture2DLod(s_ssrLit, prevUv, 0.0).rgb, vec3_splat(8.0)), screenFade * roughnessFade);
        }
    }
    return vec4(0.0, 0.0, 0.0, 0.0);
}
