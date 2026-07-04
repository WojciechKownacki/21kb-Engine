$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_depth, 0);
uniform mat4 u_inverseViewProjection;
uniform mat4 u_previousViewProjection;
uniform vec4 u_temporalParams;
// x: reproject depthless background against the editor grid plane, y: plane world height.
uniform vec4 u_postParams;

void main()
{
    float depth = texture2D(s_depth, v_texcoord0).x;

    // Reversed-Z clears the background to depth 0 and editor overlays (grid) do not write
    // depth, so those pixels must still reproject rather than short-circuit to zero velocity:
    // zero velocity pins history to the screen and smears the background whenever the camera
    // moves. At the far plane the reprojection is exact under camera rotation.
    float clipDepth = u_temporalParams.w > 0.5 ? depth * 2.0 - 1.0 : depth;
    vec2 currentNdc = vec2(v_texcoord0.x * 2.0 - 1.0, 1.0 - v_texcoord0.y * 2.0);
    vec4 world = mul(u_inverseViewProjection, vec4(currentNdc, clipDepth, 1.0));
    world.xyz /= max(world.w, 0.000001);

    // The far plane has no parallax though, which is wrong for the depthless grid overlay
    // under camera translation: its lines then blend against stale history and dim while
    // the camera moves. When the overlay is active, background pixels below the horizon
    // reproject against the actual grid plane instead.
    if (depth <= 0.000001 && u_postParams.x > 0.5) {
        // Reversed-Z puts the near plane at depth 1.0, which maps to clip z 1.0 in both
        // depth conventions (homogeneous: 1.0 * 2 - 1).
        vec4 nearWorld = mul(u_inverseViewProjection, vec4(currentNdc, 1.0, 1.0));
        nearWorld.xyz /= max(nearWorld.w, 0.000001);
        vec3 rayDir = world.xyz - nearWorld.xyz;
        if (abs(rayDir.y) > 0.000001) {
            float t = (u_postParams.y - nearWorld.y) / rayDir.y;
            if (t > 0.0 && t < 1.0) {
                world.xyz = nearWorld.xyz + rayDir * t;
            }
        }
    }
    world.w = 1.0;

    vec4 previousClip = mul(u_previousViewProjection, world);
    vec2 previousNdc = previousClip.xy / max(previousClip.w, 0.000001);
    vec2 previousUv = vec2(previousNdc.x * 0.5 + 0.5, 0.5 - previousNdc.y * 0.5);
    vec2 velocity = v_texcoord0 - previousUv;
    gl_FragColor = vec4(velocity, 0.0, 1.0);
}
