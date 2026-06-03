$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_source, 0);
uniform vec4 u_postParams;

void main()
{
    vec2 stepUv = u_postParams.xy;
    float sourceLod = u_postParams.z;
    vec3 color = vec3_splat(0.0);
    if (abs(stepUv.x) > 0.0 && abs(stepUv.y) > 0.0) {
        vec2 halfStep = stepUv * 0.5;
        color += texture2DLod(s_source, v_texcoord0, sourceLod).rgb * 0.125;
        color += texture2DLod(s_source, v_texcoord0 + vec2(-stepUv.x, -stepUv.y), sourceLod).rgb * 0.0625;
        color += texture2DLod(s_source, v_texcoord0 + vec2(0.0, -stepUv.y), sourceLod).rgb * 0.125;
        color += texture2DLod(s_source, v_texcoord0 + vec2(stepUv.x, -stepUv.y), sourceLod).rgb * 0.0625;
        color += texture2DLod(s_source, v_texcoord0 + vec2(-stepUv.x, 0.0), sourceLod).rgb * 0.125;
        color += texture2DLod(s_source, v_texcoord0 + vec2(stepUv.x, 0.0), sourceLod).rgb * 0.125;
        color += texture2DLod(s_source, v_texcoord0 + vec2(-stepUv.x, stepUv.y), sourceLod).rgb * 0.0625;
        color += texture2DLod(s_source, v_texcoord0 + vec2(0.0, stepUv.y), sourceLod).rgb * 0.125;
        color += texture2DLod(s_source, v_texcoord0 + vec2(stepUv.x, stepUv.y), sourceLod).rgb * 0.0625;
        color += texture2DLod(s_source, v_texcoord0 + vec2(-halfStep.x, -halfStep.y), sourceLod).rgb * 0.03125;
        color += texture2DLod(s_source, v_texcoord0 + vec2(halfStep.x, -halfStep.y), sourceLod).rgb * 0.03125;
        color += texture2DLod(s_source, v_texcoord0 + vec2(-halfStep.x, halfStep.y), sourceLod).rgb * 0.03125;
        color += texture2DLod(s_source, v_texcoord0 + vec2(halfStep.x, halfStep.y), sourceLod).rgb * 0.03125;
    } else {
        color = texture2DLod(s_source, v_texcoord0, sourceLod).rgb * 0.227027;
        color += texture2DLod(s_source, v_texcoord0 + stepUv * 1.384615, sourceLod).rgb * 0.316216;
        color += texture2DLod(s_source, v_texcoord0 - stepUv * 1.384615, sourceLod).rgb * 0.316216;
        color += texture2DLod(s_source, v_texcoord0 + stepUv * 3.230769, sourceLod).rgb * 0.070270;
        color += texture2DLod(s_source, v_texcoord0 - stepUv * 3.230769, sourceLod).rgb * 0.070270;
    }
    gl_FragColor = vec4(color, 1.0);
}
