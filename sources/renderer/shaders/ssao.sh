// Screen-space ambient occlusion: cosine-weighted samples in the hemisphere above a surface are tested
// against the G-buffer depth; a sample that lies behind the visible surface (and not much further than the
// radius) counts as occluded. Needs the same inputs as ssgi_gather.sh, which it follows.
float KbScreenSpaceAo(vec3 worldPos, vec3 normal, vec2 pixel)
{
    float radius = u_aoParams.y;
    vec3 origin = worldPos + normal * 0.02;
    float seed = u_giParams.z;
    float occlusion = 0.0;
    for (int i = 0; i < 12; ++i) {
        float id = float(i) + seed * 12.0;
        vec3 dir = KbGiDirection(normal, KbGiHash(pixel, id), KbGiHash(pixel.yx, id + 0.5));
        float distanceAlongRay = radius * (0.15 + 0.85 * KbGiHash(pixel + vec2(3.0, 11.0), id));
        vec3 p = origin + dir * distanceAlongRay;
        vec4 clip = mul(u_giViewProj, vec4(p, 1.0));
        if (clip.w <= 0.0001) {
            continue;
        }
        vec2 uv = KbGiUv(clip);
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
            continue;
        }
        if (texture2DLod(s_gbufferNormal, uv, 0.0).a < 0.5) {
            continue;
        }
        vec3 q = ReconstructWorldPosition(uv, texture2DLod(s_gbufferDepth, uv, 0.0).x);
        float delta = distance(u_deferredCameraPosition.xyz, p) - distance(u_deferredCameraPosition.xyz, q);
        occlusion += step(0.02, delta) * (1.0 - smoothstep(radius, radius * 2.0, delta));
    }
    return max(1.0 - u_aoParams.x * (occlusion * (1.0 / 12.0)), 0.0);
}
