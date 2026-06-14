$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_source, 0);
uniform vec4 u_temporalParams;

void main()
{
    vec2 texel = u_temporalParams.xy;
    vec3 rgbNW = texture2D(s_source, v_texcoord0 + vec2(-1.0, -1.0) * texel).rgb;
    vec3 rgbNE = texture2D(s_source, v_texcoord0 + vec2( 1.0, -1.0) * texel).rgb;
    vec3 rgbSW = texture2D(s_source, v_texcoord0 + vec2(-1.0,  1.0) * texel).rgb;
    vec3 rgbSE = texture2D(s_source, v_texcoord0 + vec2( 1.0,  1.0) * texel).rgb;
    vec4 center = texture2D(s_source, v_texcoord0);
    vec3 rgbM = center.rgb;

    vec3 luma = vec3(0.299, 0.587, 0.114);
    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM = dot(rgbM, luma);
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.03125, 0.0078125);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2_splat(-8.0), vec2_splat(8.0)) * texel;

    vec3 rgbA = 0.5 * (
        texture2D(s_source, v_texcoord0 + dir * (1.0 / 3.0 - 0.5)).rgb +
        texture2D(s_source, v_texcoord0 + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        texture2D(s_source, v_texcoord0 + dir * -0.5).rgb +
        texture2D(s_source, v_texcoord0 + dir * 0.5).rgb);
    float lumaB = dot(rgbB, luma);
    vec3 aa = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
    gl_FragColor = vec4(aa, center.a);
}
