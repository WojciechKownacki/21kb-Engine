$input v_color0, v_texcoord0, v_worldPos, v_shadowFlags

#include <bgfx_shader.sh>

SAMPLER2D(s_particleAtlas, 0);
SAMPLER2D(s_particleSceneDepth, 1);
uniform vec4 u_particleCameraBasis[3];
uniform vec4 u_particleDepthParams;
uniform vec4 u_particleFeatureParams;
uniform vec4 u_particleCameraPosition;

// The scene point seen through this fragment (from the scene depth along the view ray), carried
// into the decal's box; outside the box nothing is drawn, inside the texture is projected from above.
void main()
{
    vec2 screenUv = gl_FragCoord.xy * u_viewTexel.xy;
    float deviceDepth = texture2D(s_particleSceneDepth, screenUv).x;
    float viewDepth = abs(u_particleDepthParams.y / max(abs(deviceDepth - u_particleDepthParams.x), 0.000001));
    vec3 ray = normalize(v_worldPos - u_particleCameraPosition.xyz);
    float along = max(abs(dot(ray, normalize(u_particleCameraBasis[2].xyz))), 0.000001);
    vec3 scenePoint = u_particleCameraPosition.xyz + ray * (viewDepth / along);
    vec3 offset = scenePoint - v_shadowFlags.xyz;
    float c = cos(v_shadowFlags.w);
    float s = sin(v_shadowFlags.w);
    // The inverse of the vertex stage's turn about the vertical.
    vec3 local = vec3(offset.x * c + offset.z * s, offset.y, -offset.x * s + offset.z * c);
    vec3 box = vec3(v_texcoord0.x, v_texcoord0.y, v_texcoord0.x);
    vec3 unit = local / max(box, vec3(0.000001, 0.000001, 0.000001));
    if (abs(unit.x) > 0.5 || abs(unit.y) > 0.5 || abs(unit.z) > 0.5) {
        discard;
    }
    vec4 tex = texture2D(s_particleAtlas, vec2(unit.x + 0.5, 0.5 - unit.z));
    float alpha = tex.a * clamp(v_color0.a, 0.0, 1.0);
    if (alpha < 0.002) {
        discard;
    }
    vec3 rgb = tex.rgb * v_color0.rgb;
    bool additive = u_particleFeatureParams.z > 0.5;
    gl_FragColor = additive ? vec4(rgb * alpha, alpha) : vec4(rgb, alpha);
}
