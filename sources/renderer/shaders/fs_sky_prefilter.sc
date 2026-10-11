$input v_texcoord0

// GGX prefilter of the captured sky for one roughness (Karis, "Real Shading in Unreal Engine 4"),
// sampling the captured cube's mips by each sample's solid angle so few samples do not alias.

#include <bgfx_shader.sh>
#include "sky_cube.sh"

SAMPLERCUBE(s_skyCapture, 0);
uniform vec4 u_skyCubeFace;
// x roughness, y capture face size in texels, z capture mip count.
uniform vec4 u_skyPrefilter;

float KbRadicalInverse(float index)
{
    float result = 0.0;
    float factor = 0.5;
    for (int bit = 0; bit < 8; ++bit) {
        result += mod(index, 2.0) * factor;
        index = floor(index * 0.5);
        factor *= 0.5;
    }
    return result;
}

void main()
{
    vec3 N = KbCubeFaceDirection(u_skyCubeFace.x, KbCubeFaceUv(v_texcoord0, u_skyCubeFace.y));
    float roughness = u_skyPrefilter.x;
    if (roughness <= 0.0) {
        gl_FragColor = vec4(textureCubeLod(s_skyCapture, N, 0.0).rgb, 1.0);
        return;
    }
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangentX = normalize(cross(up, N));
    vec3 tangentY = cross(N, tangentX);
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float texelSolidAngle = 4.0 * 3.14159265 / (6.0 * u_skyPrefilter.y * u_skyPrefilter.y);
    const int kSamples = 64;
    vec3 sum = vec3(0.0, 0.0, 0.0);
    float weight = 0.0;
    for (int i = 0; i < kSamples; ++i) {
        float u = (float(i) + 0.5) / float(kSamples);
        float v = KbRadicalInverse(float(i));
        float phi = 2.0 * 3.14159265 * u;
        float cosTheta = sqrt((1.0 - v) / (1.0 + (alpha2 - 1.0) * v));
        float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
        vec3 H = tangentX * (sinTheta * cos(phi)) + tangentY * (sinTheta * sin(phi)) + N * cosTheta;
        vec3 L = 2.0 * dot(N, H) * H - N;
        float nDotL = dot(N, L);
        if (nDotL > 0.0) {
            // With N = V the GGX sample pdf over L is D / 4.
            float d = alpha2 / (3.14159265 * pow(cosTheta * cosTheta * (alpha2 - 1.0) + 1.0, 2.0));
            float sampleSolidAngle = 4.0 / (float(kSamples) * max(d, 0.000001));
            float mip = clamp(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0, u_skyPrefilter.z - 1.0);
            sum += textureCubeLod(s_skyCapture, L, mip).rgb * nDotL;
            weight += nDotL;
        }
    }
    gl_FragColor = vec4(sum / max(weight, 0.0001), 1.0);
}
