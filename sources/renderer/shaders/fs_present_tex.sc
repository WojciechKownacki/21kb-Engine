$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
SAMPLER3D(s_colorGradeLut, 1);
uniform vec4 u_tonemapParams;
uniform vec4 u_colorGradeParams;

vec3 aces_fitted(vec3 color)
{
    color = max(color, vec3_splat(0.0));
    return saturate((color * (2.51 * color + vec3_splat(0.03))) / (color * (2.43 * color + vec3_splat(0.59)) + vec3_splat(0.14)));
}

vec3 agx_approx(vec3 color)
{
    color = max(color, vec3_splat(0.0));
    vec3 compressed = log2(color + vec3_splat(1.0));
    compressed = compressed / (compressed + vec3_splat(1.65));
    return saturate(compressed * vec3_splat(1.18));
}

vec3 tonemap_display(vec3 color)
{
    if (u_tonemapParams.z < 0.0) {
        return saturate(color);
    }
    return u_tonemapParams.z < 0.5 ? aces_fitted(color) : agx_approx(color);
}

vec3 apply_color_grade_lut(vec3 displayLinear)
{
    float lutSize = max(u_colorGradeParams.x, 2.0);
    vec3 uvw = saturate(displayLinear) * ((lutSize - 1.0) / lutSize) + vec3_splat(0.5 / lutSize);
    vec3 graded = texture3D(s_colorGradeLut, uvw).rgb;
    return mix(displayLinear, graded, saturate(u_colorGradeParams.y));
}

void main()
{
    vec4 scene = texture2D(s_texColor, v_texcoord0);
    vec3 hdr = scene.rgb * exp2(u_tonemapParams.x);
    vec3 display = tonemap_display(hdr);
    display = apply_color_grade_lut(display);
    display = pow(max(display, vec3_splat(0.0)), vec3_splat(u_tonemapParams.y));
    gl_FragColor = vec4(display, scene.a);
}
