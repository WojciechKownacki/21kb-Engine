// Screen-space diffuse global illumination for the deferred lighting shader.
// Cosine-weighted rays are marched against the G-buffer depth; the radiance found at a hit is read
// from last frame's lit colour (reprojected), so bounced light follows dynamic lights and objects.
// Needs ReconstructWorldPosition, s_gbufferDepth, s_gbufferNormal and u_deferredCameraPosition.
SAMPLER2D(s_giHistory, 8);
uniform mat4 u_giViewProj;
uniform mat4 u_giPrevViewProj;
uniform vec4 u_giParams; // x = intensity (0 = off), y = ray length (m), z = frame index, w = hit thickness factor

float KbGiHash(vec2 p, float seed)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233)) + seed * 37.719) * 43758.5453);
}

vec3 KbGiDirection(vec3 n, float u1, float u2)
{
    float r = sqrt(u1);
    float phi = 6.2831853 * u2;
    vec3 helper = abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 t = normalize(cross(n, helper));
    vec3 b = cross(n, t);
    return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(1.0 - u1, 0.0)));
}

vec2 KbGiUv(vec4 clip)
{
    vec2 ndc = clip.xy / clip.w;
    return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

// Returns incoming diffuse radiance (before multiplying by the surface albedo).
vec3 KbScreenSpaceGi(vec3 worldPos, vec3 normal, vec2 pixel)
{
    vec3 sum = vec3(0.0, 0.0, 0.0);
    vec3 origin = worldPos + normal * 0.03;
    float seed = u_giParams.z;
    for (int ray = 0; ray < 6; ++ray) {
        float rayId = float(ray) + seed * 6.0;
        vec3 dir = KbGiDirection(normal, KbGiHash(pixel, rayId), KbGiHash(pixel.yx, rayId + 0.5));
        float jitter = KbGiHash(pixel + vec2(7.0, 3.0), rayId);
        for (int step = 0; step < 8; ++step) {
            float f = (float(step) + 0.5 + jitter) * 0.125;
            float t = u_giParams.y * f * f;
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
            if (delta > 0.0 && delta < u_giParams.w * max(t, 0.25)) {
                if (dot(normalize(hitNormal.xyz * 2.0 - 1.0), dir) < 0.0) {
                    vec4 prevClip = mul(u_giPrevViewProj, vec4(q, 1.0));
                    if (prevClip.w > 0.0001) {
                        vec2 prevUv = KbGiUv(prevClip);
                        if (prevUv.x >= 0.0 && prevUv.x <= 1.0 && prevUv.y >= 0.0 && prevUv.y <= 1.0) {
                            sum += min(texture2DLod(s_giHistory, prevUv, 0.0).rgb, vec3_splat(8.0));
                        }
                    }
                }
                break;
            }
        }
    }
    return sum * (1.0 / 6.0);
}
