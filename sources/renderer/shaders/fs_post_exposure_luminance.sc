$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_source, 0);
uniform vec4 u_exposureParams;

float linear_luminance(vec3 color)
{
    return max(dot(max(color, vec3_splat(0.0)), vec3(0.2126, 0.7152, 0.0722)), 0.0001);
}

void main()
{
    const float binCount = 64.0;
    const float sampleCountX = 16.0;
    const float sampleCountY = 16.0;
    const float sampleCount = sampleCountX * sampleCountY;
    float currentBin = floor(min(v_texcoord0.x, 0.999999) * binCount);
    float binWeight = 0.0;

    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            vec2 uv = vec2((float(x) + 0.5) / sampleCountX, (float(y) + 0.5) / sampleCountY);
            float luminance = linear_luminance(texture2D(s_source, uv).rgb);
            float sampleBin = floor(saturate((log2(luminance) - u_exposureParams.z) * u_exposureParams.w) * binCount);
            sampleBin = min(sampleBin, binCount - 1.0);
            binWeight += 1.0 - step(0.5, abs(sampleBin - currentBin));
        }
    }

    float normalizedWeight = saturate(binWeight / sampleCount);
    gl_FragColor = vec4((currentBin + 0.5) / binCount, normalizedWeight, 0.0, normalizedWeight);
}
