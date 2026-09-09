$input v_texcoord0, v_screenUILocal, v_screenUIPosition

#include <bgfx_shader.sh>

SAMPLER2D(s_screenUITexture, 0);
uniform vec4 u_screenUIFillColor;
uniform vec4 u_screenUIBorderColor;
uniform vec4 u_screenUIBorderWidths;
uniform vec4 u_screenUICornerRadii;
uniform vec4 u_screenUIClipRect;
uniform vec4 u_screenUIStyleParams;
uniform vec4 u_screenUIEffectParams;
uniform vec4 u_screenUITonemapParams;

float rounded_distance(vec2 localPosition, vec2 size, vec4 radii)
{
    bool right = localPosition.x >= size.x * 0.5;
    bool bottom = localPosition.y >= size.y * 0.5;
    float radius = bottom
        ? (right ? radii.z : radii.w)
        : (right ? radii.y : radii.x);
    radius = clamp(radius, 0.0, min(size.x, size.y) * 0.5);
    vec2 q = abs(localPosition - size * 0.5) - (size * 0.5 - vec2_splat(radius));
    return length(max(q, vec2_splat(0.0))) + min(max(q.x, q.y), 0.0) - radius;
}

float rounded_coverage(vec2 localPosition, vec2 size, vec4 radii, float feather)
{
    float distance = rounded_distance(localPosition, size, radii);
    float aa = max(fwidth(distance), 0.75);
    return 1.0 - smoothstep(-aa, max(aa, feather), distance);
}

vec3 aces_fitted(vec3 color)
{
    color = max(color, vec3_splat(0.0));
    return saturate((color * (2.51 * color + vec3_splat(0.03))) /
        (color * (2.43 * color + vec3_splat(0.59)) + vec3_splat(0.14)));
}

vec3 agx_approx(vec3 color)
{
    color = max(color, vec3_splat(0.0));
    vec3 compressed = log2(color + vec3_splat(1.0));
    compressed = compressed / (compressed + vec3_splat(1.65));
    return saturate(compressed * vec3_splat(1.18));
}

vec3 display_color(vec3 hdr)
{
    vec3 exposed = hdr * exp2(u_screenUITonemapParams.x);
    vec3 mapped = u_screenUITonemapParams.z < 0.0
        ? saturate(exposed)
        : (u_screenUITonemapParams.z < 0.5 ? aces_fitted(exposed) : agx_approx(exposed));
    return pow(max(mapped, vec3_splat(0.0)), vec3_splat(u_screenUITonemapParams.y));
}

void main()
{
    float kind = u_screenUIEffectParams.x;
    vec2 size = max(u_screenUIStyleParams.xy, vec2_splat(1.0));
    vec2 localPosition = v_screenUILocal * size;
    vec2 clipSize = max(u_screenUIClipRect.zw - u_screenUIClipRect.xy, vec2_splat(1.0));
    float clipCoverage = rounded_coverage(
        v_screenUIPosition - u_screenUIClipRect.xy,
        clipSize,
        vec4_splat(0.0),
        0.0);
    float coverage = rounded_coverage(localPosition, size, u_screenUICornerRadii, u_screenUIStyleParams.w) * clipCoverage;
    vec4 color = u_screenUIFillColor;

    if (kind > 2.5) {
        color = vec4(display_color(texture2D(s_screenUITexture, v_texcoord0).rgb), 1.0) * u_screenUIFillColor;
    } else if (kind > 1.5) {
        float alpha = texture2D(s_screenUITexture, v_texcoord0).a;
        float outline = 0.0;
        if (u_screenUIEffectParams.y > 0.0) {
            vec2 texel = vec2(1.0 / max(u_screenUIEffectParams.z, 1.0), 1.0 / max(u_screenUIEffectParams.w, 1.0)) * u_screenUIEffectParams.y;
            outline = max(outline, texture2D(s_screenUITexture, v_texcoord0 + vec2(texel.x, 0.0)).a);
            outline = max(outline, texture2D(s_screenUITexture, v_texcoord0 - vec2(texel.x, 0.0)).a);
            outline = max(outline, texture2D(s_screenUITexture, v_texcoord0 + vec2(0.0, texel.y)).a);
            outline = max(outline, texture2D(s_screenUITexture, v_texcoord0 - vec2(0.0, texel.y)).a);
            outline = max(outline, texture2D(s_screenUITexture, v_texcoord0 + texel).a);
            outline = max(outline, texture2D(s_screenUITexture, v_texcoord0 - texel).a);
            outline = max(outline, texture2D(s_screenUITexture, v_texcoord0 + vec2(texel.x, -texel.y)).a);
            outline = max(outline, texture2D(s_screenUITexture, v_texcoord0 + vec2(-texel.x, texel.y)).a);
        }
        color = mix(u_screenUIBorderColor, u_screenUIFillColor, alpha);
        color.a *= max(alpha, outline);
        coverage = clipCoverage;
    } else if (kind > 0.5) {
        color = texture2D(s_screenUITexture, v_texcoord0) * u_screenUIFillColor;
    } else {
        vec2 innerMin = vec2(u_screenUIBorderWidths.x, u_screenUIBorderWidths.y);
        vec2 innerMax = size - vec2(u_screenUIBorderWidths.z, u_screenUIBorderWidths.w);
        vec2 innerSize = max(innerMax - innerMin, vec2_splat(0.0));
        vec4 innerRadii = max(u_screenUICornerRadii - vec4(
            max(u_screenUIBorderWidths.x, u_screenUIBorderWidths.y),
            max(u_screenUIBorderWidths.z, u_screenUIBorderWidths.y),
            max(u_screenUIBorderWidths.z, u_screenUIBorderWidths.w),
            max(u_screenUIBorderWidths.x, u_screenUIBorderWidths.w)), vec4_splat(0.0));
        float insideInner = rounded_coverage(localPosition - innerMin, innerSize, innerRadii, 0.0);
        color = mix(u_screenUIBorderColor, u_screenUIFillColor, insideInner);
    }
    color.a *= coverage * u_screenUIStyleParams.z;
    gl_FragColor = color;
}
