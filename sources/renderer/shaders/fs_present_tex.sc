$input v_texcoord0

#include <bgfx_shader.sh>

#include "present_display.sh"

void main()
{
    vec4 scene = texture2D(s_kbFullscreenColor, v_texcoord0);
    // The same IGN pattern dithers the quantisation of every display: half a step of the display's own depth.
    float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    if (u_hdrOutputParams.x > 1.5) {
        gl_FragColor = vec4(hdr10_encode(scene.rgb) + vec3_splat((noise - 0.5) / 1023.0), 1.0);
    } else {
        vec3 display = display_transform(chromatic_aberration(scene.rgb, v_texcoord0), v_texcoord0);
        if (u_displayAntiAliasing.x > 0.5) {
            display = fxaa_display(v_texcoord0, display);
        }
        display = apply_film_grain(display, gl_FragCoord.xy);
        // Quantisation dither: half an 8-bit step of
        // interleaved gradient noise hides the banding of smooth light falloff. A composed HDR image is
        // float and is quantised later, by the HDR10 encode.
        display += vec3_splat((noise - 0.5) / 255.0 * (u_hdrOutputParams.x > 0.5 ? 0.0 : 1.0));
        gl_FragColor = vec4(display, mix(scene.a, 1.0, u_tonemapParams.w));
    }
}
