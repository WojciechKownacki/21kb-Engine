// Screen-space diffuse global illumination and ambient occlusion: the accumulated results shared by the
// resolve pass (which writes them) and the deferred lighting shader (which reads them).
// s_giAccum holds rgb = incoming diffuse radiance (before the surface albedo) and a = the view depth
// of the surface it was resolved for, in the frame described by u_giPrevViewProj; s_aoAccum holds the
// ambient visibility of the same frame in r.
SAMPLER2D(s_giAccum, 8);
SAMPLER2D(s_aoAccum, 10);
uniform mat4 u_giViewProj;
uniform mat4 u_giPrevViewProj;
uniform vec4 u_giParams; // x = intensity (0 = off), y = ray length (m), z = frame index, w = hit thickness factor
uniform vec4 u_giTemporal; // x = history weight, y = relative depth tolerance, z = 1 when s_giAccum holds a frame
uniform vec4 u_aoParams;   // x = intensity (0 = off), y = radius (m)

float KbGiHash(vec2 p, float seed)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233)) + seed * 37.719) * 43758.5453);
}

vec2 KbGiUv(vec4 clip)
{
    vec2 ndc = clip.xy / clip.w;
    return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

// Accumulated bounce light, ambient visibility and sky visibility at a world position. Returns 1 when
// the stored sample belongs to the same surface; otherwise gi is zero and ao and sky are 1 (unoccluded).
float KbScreenSpaceAccumulated(vec3 worldPos, out vec3 gi, out float ao, out float sky)
{
    gi = vec3(0.0, 0.0, 0.0);
    ao = 1.0;
    sky = 1.0;
    vec4 clip = mul(u_giPrevViewProj, vec4(worldPos, 1.0));
    if (u_giTemporal.z < 0.5 || clip.w <= 0.0001) {
        return 0.0;
    }
    vec2 uv = KbGiUv(clip);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return 0.0;
    }
    vec4 stored = texture2DLod(s_giAccum, uv, 0.0);
    float same = abs(stored.a - clip.w) < u_giTemporal.y * clip.w ? 1.0 : 0.0;
    gi = stored.rgb * same;
    vec2 visibility = texture2DLod(s_aoAccum, uv, 0.0).xy;
    ao = mix(1.0, visibility.x, same);
    sky = mix(1.0, visibility.y, same);
    return same;
}

// KbScreenSpaceAccumulated averaged over a 3 x 3 grid of taps two and a half pixels apart (each tap is
// a bilinear read of four texels, which hides the grid), keeping the ones that
// belong to the same surface. The gather is a handful of random rays per pixel; the temporal
// accumulation averages them over frames and this smooths what is left, at the cost of nine reads.
float KbScreenSpaceAccumulatedSmooth(vec3 worldPos, out vec3 gi, out float ao, out float sky)
{
    gi = vec3(0.0, 0.0, 0.0);
    ao = 1.0;
    sky = 1.0;
    vec4 clip = mul(u_giPrevViewProj, vec4(worldPos, 1.0));
    if (u_giTemporal.z < 0.5 || clip.w <= 0.0001) {
        return 0.0;
    }
    vec2 uv = KbGiUv(clip);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return 0.0;
    }
    vec2 texel = vec2(1.0, 1.0) / max(u_viewRect.zw, vec2(1.0, 1.0));
    vec3 giSum = vec3(0.0, 0.0, 0.0);
    vec2 visibilitySum = vec2(0.0, 0.0);
    float weightSum = 0.0;
    float centerWeight = 0.0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            vec2 sampleUv = uv + vec2(float(dx), float(dy)) * texel * 2.5;
            vec4 stored = texture2DLod(s_giAccum, sampleUv, 0.0);
            float same = abs(stored.a - clip.w) < u_giTemporal.y * clip.w ? 1.0 : 0.0;
            float weight = same * (dx == 0 ? 2.0 : 1.0) * (dy == 0 ? 2.0 : 1.0);
            if (dx == 0 && dy == 0) {
                centerWeight = same;
            }
            giSum += stored.rgb * weight;
            visibilitySum += texture2DLod(s_aoAccum, sampleUv, 0.0).xy * weight;
            weightSum += weight;
        }
    }
    if (weightSum <= 0.0) {
        return 0.0;
    }
    gi = giSum / weightSum;
    vec2 visibility = visibilitySum / weightSum;
    ao = visibility.x;
    sky = visibility.y;
    return centerWeight;
}
